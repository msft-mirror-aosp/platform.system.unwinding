/*
 * Copyright (C) 2018 The Android Open Source Project
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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>

#define LOG_TAG "unwind"
#include <log/log.h>

#include <android-base/unique_fd.h>
#include <art_api/dex_file_support.h>

#include <unwindstack/MapInfo.h>
#include <unwindstack/Memory.h>

#include "DexFile.h"
#include "MemoryBuffer.h"

namespace unwindstack {

std::map<DexFile::MappedFileKey, std::weak_ptr<DexFile>> DexFile::g_mapped_dex_files;
std::mutex DexFile::g_lock;

static bool CheckDexSupport() {
  if (std::string err_msg; !art_api::dex::TryLoadLibdexfile(&err_msg)) {
    ALOGW("Failed to initialize DEX file support: %s", err_msg.c_str());
    return false;
  }
  return true;
}

std::shared_ptr<DexFile> DexFile::CreateFromDisk(uint64_t addr, uint64_t size, MapInfo* map) {
  if (map == nullptr || map->name.empty()) {
    return nullptr;  // MapInfo not backed by file.
  }
  if (!(map->start <= addr && addr < map->end)) {
    return nullptr;  // addr is not in MapInfo range.
  }
  if (size > (map->end - addr)) {
    return nullptr;  // size is past the MapInfo end.
  }
  uint64_t offset_in_file = (addr - map->start) + map->offset;

  // Fast-path: Check if the dex file was already mapped from disk.
  std::lock_guard<std::mutex> guard(g_lock);
  MappedFileKey cache_key(map->name, offset_in_file, size);
  std::weak_ptr<DexFile>& cache_entry = g_mapped_dex_files[cache_key];
  std::shared_ptr<DexFile> dex_file = cache_entry.lock();
  if (dex_file != nullptr) {
    return dex_file;
  }

  // Load the file from disk and cache it.
  std::unique_ptr<Memory> memory = Memory::CreateFileMemory(map->name, offset_in_file, size);
  if (memory == nullptr) {
    return nullptr;  // failed to map the file.
  }
  std::unique_ptr<art_api::dex::DexFile> dex;
  art_api::dex::DexFile::Create(memory->GetPtr(), size, nullptr, map->name.c_str(), &dex);
  if (dex == nullptr) {
    return nullptr;  // invalid DEX file.
  }
  dex_file.reset(new DexFile(std::move(memory), addr, size, std::move(dex)));
  cache_entry = dex_file;
  return dex_file;
}

std::shared_ptr<DexFile> DexFile::Create(uint64_t base_addr, uint64_t file_size, Memory* memory,
                                         MapInfo* info) {
  static bool has_dex_support = CheckDexSupport();
  if (!has_dex_support || file_size == 0) {
    return nullptr;
  }
  std::shared_ptr<DexFile> dex_file = CreateFromDisk(base_addr, file_size, info);
  if (dex_file != nullptr) {
    return dex_file;
  }

  // Fallback: make copy in local buffer.
  std::unique_ptr<MemoryBuffer> copy(new MemoryBuffer);
  if (!copy->Resize(file_size)) {
    return nullptr;
  }
  if (!memory->ReadFully(base_addr, copy->GetPtr(0), file_size)) {
    return nullptr;
  }
  std::unique_ptr<art_api::dex::DexFile> dex;
  art_api::dex::DexFile::Create(copy->GetPtr(0), file_size, nullptr, "", &dex);
  if (dex == nullptr) {
    return nullptr;
  }
  dex_file.reset(new DexFile(std::move(copy), base_addr, file_size, std::move(dex)));
  return dex_file;
}

bool DexFile::GetFunctionName(uint64_t dex_pc, SharedString* method_name, uint64_t* method_offset) {
  uint64_t dex_offset = dex_pc - base_addr_;  // Convert absolute PC to file-relative offset.

  // Lookup the function in the cache.
  std::lock_guard<std::mutex> guard(lock_);
  auto it = symbols_.upper_bound(dex_offset);
  if (it == symbols_.end() || dex_offset < it->second.offset) {
    // Lookup the function in the underlying dex file.
    size_t found = dex_->FindMethodAtOffset(dex_offset, [&](const auto& method) {
      size_t code_size, name_size;
      uint32_t offset = method.GetCodeOffset(&code_size);
      const char* name = method.GetQualifiedName(/*with_params=*/false, &name_size);
      it = symbols_.emplace(offset + code_size, Info{offset, std::string(name, name_size)}).first;
    });
    if (found == 0) {
      return false;
    }
  }

  // Return the found function.
  *method_offset = dex_offset - it->second.offset;
  *method_name = it->second.name;
  return true;
}

}  // namespace unwindstack
