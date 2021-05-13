/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <mutex>
#include <string>

#include <android-base/stringprintf.h>

#include <unwindstack/Elf.h>
#include <unwindstack/MapInfo.h>
#include <unwindstack/Maps.h>

#include "MemoryFileAtOffset.h"
#include "MemoryRange.h"

namespace unwindstack {

bool MapInfo::InitFileMemoryFromPreviousReadOnlyMap(MemoryFileAtOffset* memory) {
  // One last attempt, see if the previous map is read-only with the
  // same name and stretches across this map.
  if (prev_real_map_ == nullptr || prev_real_map_->flags_ != PROT_READ) {
    return false;
  }

  uint64_t map_size = end_ - prev_real_map_->end_;
  if (!memory->Init(name_, prev_real_map_->offset_, map_size)) {
    return false;
  }

  uint64_t max_size;
  if (!Elf::GetInfo(memory, &max_size) || max_size < map_size) {
    return false;
  }

  if (!memory->Init(name_, prev_real_map_->offset_, max_size)) {
    return false;
  }

  elf_offset_ = offset_ - prev_real_map_->offset_;
  elf_start_offset_ = prev_real_map_->offset_;
  return true;
}

Memory* MapInfo::GetFileMemory() {
  std::unique_ptr<MemoryFileAtOffset> memory(new MemoryFileAtOffset);
  if (offset_ == 0) {
    if (memory->Init(name_, 0)) {
      return memory.release();
    }
    return nullptr;
  }

  // These are the possibilities when the offset is non-zero.
  // - There is an elf file embedded in a file, and the offset is the
  //   the start of the elf in the file.
  // - There is an elf file embedded in a file, and the offset is the
  //   the start of the executable part of the file. The actual start
  //   of the elf is in the read-only segment preceeding this map.
  // - The whole file is an elf file, and the offset needs to be saved.
  //
  // Map in just the part of the file for the map. If this is not
  // a valid elf, then reinit as if the whole file is an elf file.
  // If the offset is a valid elf, then determine the size of the map
  // and reinit to that size. This is needed because the dynamic linker
  // only maps in a portion of the original elf, and never the symbol
  // file data.
  uint64_t map_size = end_ - start_;
  if (!memory->Init(name_, offset_, map_size)) {
    return nullptr;
  }

  // Check if the start of this map is an embedded elf.
  uint64_t max_size = 0;
  if (Elf::GetInfo(memory.get(), &max_size)) {
    elf_start_offset_ = offset_;
    if (max_size > map_size) {
      if (memory->Init(name_, offset_, max_size)) {
        return memory.release();
      }
      // Try to reinit using the default map_size.
      if (memory->Init(name_, offset_, map_size)) {
        return memory.release();
      }
      elf_start_offset_ = 0;
      return nullptr;
    }
    return memory.release();
  }

  // No elf at offset, try to init as if the whole file is an elf.
  if (memory->Init(name_, 0) && Elf::IsValidElf(memory.get())) {
    elf_offset_ = offset_;
    // Need to check how to set the elf start offset. If this map is not
    // the r-x map of a r-- map, then use the real offset value. Otherwise,
    // use 0.
    if (prev_real_map_ == nullptr || prev_real_map_->offset_ != 0 ||
        prev_real_map_->flags_ != PROT_READ || prev_real_map_->name_ != name_) {
      elf_start_offset_ = offset_;
    }
    return memory.release();
  }

  // See if the map previous to this one contains a read-only map
  // that represents the real start of the elf data.
  if (InitFileMemoryFromPreviousReadOnlyMap(memory.get())) {
    return memory.release();
  }

  // Failed to find elf at start of file or at read-only map, return
  // file object from the current map.
  if (memory->Init(name_, offset_, map_size)) {
    return memory.release();
  }
  return nullptr;
}

Memory* MapInfo::CreateMemory(const std::shared_ptr<Memory>& process_memory) {
  if (end_ <= start_) {
    return nullptr;
  }

  elf_offset_ = 0;

  // Fail on device maps.
  if (flags_ & MAPS_FLAGS_DEVICE_MAP) {
    return nullptr;
  }

  // First try and use the file associated with the info.
  if (!name_.empty()) {
    Memory* memory = GetFileMemory();
    if (memory != nullptr) {
      return memory;
    }
  }

  if (process_memory == nullptr) {
    return nullptr;
  }

  memory_backed_elf_ = true;

  // Need to verify that this elf is valid. It's possible that
  // only part of the elf file to be mapped into memory is in the executable
  // map. In this case, there will be another read-only map that includes the
  // first part of the elf file. This is done if the linker rosegment
  // option is used.
  std::unique_ptr<MemoryRange> memory(new MemoryRange(process_memory, start_, end_ - start_, 0));
  if (Elf::IsValidElf(memory.get())) {
    // Might need to peek at the next map to create a memory object that
    // includes that map too.
    if (offset_ != 0 || name_.empty() || next_real_map_ == nullptr ||
        offset_ >= next_real_map_->offset_ || next_real_map_->name_ != name_) {
      return memory.release();
    }

    // There is a possibility that the elf object has already been created
    // in the next map. Since this should be a very uncommon path, just
    // redo the work. If this happens, the elf for this map will eventually
    // be discarded.
    MemoryRanges* ranges = new MemoryRanges;
    ranges->Insert(new MemoryRange(process_memory, start_, end_ - start_, 0));
    ranges->Insert(new MemoryRange(process_memory, next_real_map_->start_,
                                   next_real_map_->end_ - next_real_map_->start_,
                                   next_real_map_->offset_ - offset_));

    return ranges;
  }

  // Find the read-only map by looking at the previous map. The linker
  // doesn't guarantee that this invariant will always be true. However,
  // if that changes, there is likely something else that will change and
  // break something.
  if (offset_ == 0 || name_.empty() || prev_real_map_ == nullptr ||
      prev_real_map_->name_ != name_ || prev_real_map_->offset_ >= offset_) {
    memory_backed_elf_ = false;
    return nullptr;
  }

  // Make sure that relative pc values are corrected properly.
  elf_offset_ = offset_ - prev_real_map_->offset_;
  // Use this as the elf start offset, otherwise, you always get offsets into
  // the r-x section, which is not quite the right information.
  elf_start_offset_ = prev_real_map_->offset_;

  MemoryRanges* ranges = new MemoryRanges;
  ranges->Insert(new MemoryRange(process_memory, prev_real_map_->start_,
                                 prev_real_map_->end_ - prev_real_map_->start_, 0));
  ranges->Insert(new MemoryRange(process_memory, start_, end_ - start_, elf_offset_));

  return ranges;
}

Elf* MapInfo::GetElf(const std::shared_ptr<Memory>& process_memory, ArchEnum expected_arch) {
  {
    // Make sure no other thread is trying to add the elf to this map.
    std::lock_guard<std::mutex> guard(mutex_);

    if (elf_.get() != nullptr) {
      return elf_.get();
    }

    bool locked = false;
    if (Elf::CachingEnabled() && !name_.empty()) {
      Elf::CacheLock();
      locked = true;
      if (Elf::CacheGet(this)) {
        Elf::CacheUnlock();
        return elf_.get();
      }
    }

    Memory* memory = CreateMemory(process_memory);
    if (locked) {
      if (Elf::CacheAfterCreateMemory(this)) {
        delete memory;
        Elf::CacheUnlock();
        return elf_.get();
      }
    }
    elf_.reset(new Elf(memory));
    // If the init fails, keep the elf around as an invalid object so we
    // don't try to reinit the object.
    elf_->Init();
    if (elf_->valid() && expected_arch != elf_->arch()) {
      // Make the elf invalid, mismatch between arch and expected arch.
      elf_->Invalidate();
    }

    if (locked) {
      Elf::CacheAdd(this);
      Elf::CacheUnlock();
    }
  }

  if (!elf_->valid()) {
    elf_start_offset_ = offset_;
  } else if (prev_real_map_ != nullptr && elf_start_offset_ != offset_ &&
             prev_real_map_->offset_ == elf_start_offset_ && prev_real_map_->name_ == name_) {
    // If there is a read-only map then a read-execute map that represents the
    // same elf object, make sure the previous map is using the same elf
    // object if it hasn't already been set.
    std::lock_guard<std::mutex> guard(prev_real_map_->mutex_);
    if (prev_real_map_->elf_.get() == nullptr) {
      prev_real_map_->elf_ = elf_;
      prev_real_map_->memory_backed_elf_ = memory_backed_elf_;
    } else {
      // Discard this elf, and use the elf from the previous map instead.
      elf_ = prev_real_map_->elf_;
    }
  }
  return elf_.get();
}

bool MapInfo::GetFunctionName(uint64_t addr, SharedString* name, uint64_t* func_offset) {
  {
    // Make sure no other thread is trying to update this elf object.
    std::lock_guard<std::mutex> guard(mutex_);
    if (elf_ == nullptr) {
      return false;
    }
  }
  // No longer need the lock, once the elf object is created, it is not deleted
  // until this object is deleted.
  return elf_->GetFunctionName(addr, name, func_offset);
}

uint64_t MapInfo::GetLoadBias(const std::shared_ptr<Memory>& process_memory) {
  int64_t cur_load_bias = load_bias_.load();
  if (cur_load_bias != INT64_MAX) {
    return cur_load_bias;
  }

  {
    // Make sure no other thread is trying to add the elf to this map.
    std::lock_guard<std::mutex> guard(mutex_);
    if (elf_ != nullptr) {
      if (elf_->valid()) {
        cur_load_bias = elf_->GetLoadBias();
        load_bias_ = cur_load_bias;
        return cur_load_bias;
      } else {
        load_bias_ = 0;
        return 0;
      }
    }
  }

  // Call lightweight static function that will only read enough of the
  // elf data to get the load bias.
  std::unique_ptr<Memory> memory(CreateMemory(process_memory));
  cur_load_bias = Elf::GetLoadBias(memory.get());
  load_bias_ = cur_load_bias;
  return cur_load_bias;
}

MapInfo::~MapInfo() {
  delete build_id_.load();
}

SharedString MapInfo::GetBuildID() {
  SharedString* id = build_id_.load();
  if (id != nullptr) {
    return *id;
  }

  // No need to lock, at worst if multiple threads do this at the same
  // time it should be detected and only one thread should win and
  // save the data.

  // Now need to see if the elf object exists.
  // Make sure no other thread is trying to add the elf to this map.
  mutex_.lock();
  Elf* elf_obj = elf_.get();
  mutex_.unlock();
  std::string result;
  if (elf_obj != nullptr) {
    result = elf_obj->GetBuildID();
  } else {
    // This will only work if we can get the file associated with this memory.
    // If this is only available in memory, then the section name information
    // is not present and we will not be able to find the build id info.
    std::unique_ptr<Memory> memory(GetFileMemory());
    if (memory != nullptr) {
      result = Elf::GetBuildID(memory.get());
    }
  }
  return SetBuildID(std::move(result));
}

SharedString MapInfo::SetBuildID(std::string&& new_build_id) {
  std::unique_ptr<SharedString> new_build_id_ptr(new SharedString(std::move(new_build_id)));
  SharedString* expected_id = nullptr;
  // Strong version since we need to reliably return the stored pointer.
  if (build_id_.compare_exchange_strong(expected_id, new_build_id_ptr.get())) {
    // Value saved, so make sure the memory is not freed.
    return *new_build_id_ptr.release();
  } else {
    // The expected value is set to the stored value on failure.
    return *expected_id;
  }
}

std::string MapInfo::GetPrintableBuildID() {
  std::string raw_build_id = GetBuildID();
  if (raw_build_id.empty()) {
    return "";
  }
  std::string printable_build_id;
  for (const char& c : raw_build_id) {
    // Use %hhx to avoid sign extension on abis that have signed chars.
    printable_build_id += android::base::StringPrintf("%02hhx", c);
  }
  return printable_build_id;
}

}  // namespace unwindstack
