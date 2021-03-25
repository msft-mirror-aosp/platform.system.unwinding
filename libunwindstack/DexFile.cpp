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

static bool CheckDexSupport() {
  if (std::string err_msg; !art_api::dex::TryLoadLibdexfileExternal(&err_msg)) {
    ALOGW("Failed to initialize DEX file support: %s", err_msg.c_str());
    return false;
  }
  return true;
}

std::unique_ptr<DexFile> DexFile::Create(uint64_t base_addr, uint64_t file_size, Memory* memory,
                                         MapInfo* info) {
  static bool has_dex_support = CheckDexSupport();
  if (!has_dex_support || file_size == 0) {
    return nullptr;
  }

  // Try to map the file directly from disk.
  std::unique_ptr<Memory> dex_memory;
  if (info != nullptr && !info->name.empty()) {
    if (info->start <= base_addr && base_addr < info->end) {
      uint64_t offset_in_file = (base_addr - info->start) + info->offset;
      if (file_size <= info->end - base_addr) {
        dex_memory = Memory::CreateFileMemory(info->name, offset_in_file, file_size);
        // On error, the results is null and we fall through to the fallback code-path.
      }
    }
  }

  // Fallback: make copy in local buffer.
  if (dex_memory.get() == nullptr) {
    std::unique_ptr<MemoryBuffer> copy(new MemoryBuffer);
    if (!copy->Resize(file_size)) {
      return nullptr;
    }
    if (!memory->ReadFully(base_addr, copy->GetPtr(0), file_size)) {
      return nullptr;
    }
    dex_memory = std::move(copy);
  }

  std::string err_msg;
  size_t actual_size = file_size;
  const char* location = info != nullptr ? info->name.c_str() : "";
  std::unique_ptr<art_api::dex::DexFile> dex =
      art_api::dex::DexFile::OpenFromMemory(dex_memory->GetPtr(), &actual_size, location, &err_msg);
  if (dex != nullptr) {
    return std::unique_ptr<DexFile>(
        new DexFile(std::move(dex_memory), base_addr, file_size, std::move(dex)));
  }
  return nullptr;
}

bool DexFile::GetFunctionName(uint64_t dex_pc, SharedString* method_name, uint64_t* method_offset) {
  // Convert absolute PC to file-relative offset.
  uint64_t dex_offset = dex_pc - base_addr_;

  // Lookup the function in the cache.
  auto it = symbols_.upper_bound(dex_offset);
  if (it != symbols_.end() && it->second.offset <= dex_offset) {
    *method_offset = dex_offset - it->second.offset;
    *method_name = it->second.name;
    return true;
  }

  // Lookup the function in the underlying dex file.
  art_api::dex::MethodInfo method_info = dex_->GetMethodInfoForOffset(dex_offset, false);
  if (method_info.offset == 0) {
    return false;
  }

  // Store the function in the cache.
  Info info{
      .offset = static_cast<uint32_t>(method_info.offset),
      .name = std::string(method_info.name),
  };
  it = symbols_.emplace(method_info.offset + method_info.len, std::move(info)).first;

  // Return the found function.
  *method_offset = dex_offset - it->second.offset;
  *method_name = it->second.name;
  return true;
}

}  // namespace unwindstack
