/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef _LIBUNWINDSTACK_UTILS_OFFLINE_UNWIND_UTILS_H
#define _LIBUNWINDSTACK_UTILS_OFFLINE_UNWIND_UTILS_H

#include <filesystem>
#include <memory>
#include <string>

#include <unwindstack/Unwinder.h>

#include "MemoryOffline.h"

namespace unwindstack {

// These utils facilitate performing offline unwinds. Offline unwinds are similar to local
// unwinds, however, instead of pausing the process to gather the current execution state
// (stack, registers, Elf / maps), a snapshot of the process is taken. This snapshot data
// is used at a later time (when the process is no longer running) to unwind the process
// at the point the snapshot was taken.

std::string GetOfflineFilesDirectory();

std::string DumpFrames(const Unwinder& unwinder);

bool AddMemory(std::string file_name, MemoryOfflineParts* parts, std::string& error_msg);

class OfflineUnwindUtils {
 public:
  Regs* GetRegs() { return regs_.get(); }

  Maps* GetMaps() { return maps_.get(); }

  std::shared_ptr<Memory> GetProcessMemory() { return process_memory_; }

  std::string GetOfflineDirectory() { return offline_dir_; }

  bool Init(const std::string& offline_files_dir, ArchEnum arch, std::string& error_msg,
            bool add_stack = true, bool set_maps = true);

  bool ResetMaps(std::string& error_msg);

  bool SetProcessMemory(std::string& error_msg);

  bool SetJitProcessMemory(std::string& error_msg);

  void ReturnToCurrentWorkingDirectory() { std::filesystem::current_path(cwd_); }

 private:
  bool SetRegs(ArchEnum arch, std::string& error_msg);

  template <typename AddressType>
  bool ReadRegs(RegsImpl<AddressType>* regs,
                const std::unordered_map<std::string, uint32_t>& name_to_reg,
                std::string& error_msg);

  static std::unordered_map<std::string, uint32_t> arm_regs_;
  static std::unordered_map<std::string, uint32_t> arm64_regs_;
  static std::unordered_map<std::string, uint32_t> x86_regs_;
  static std::unordered_map<std::string, uint32_t> x86_64_regs_;

  std::string cwd_;
  std::string offline_dir_;
  std::string map_buffer_;
  std::unique_ptr<Regs> regs_;
  std::unique_ptr<Maps> maps_;
  std::shared_ptr<Memory> process_memory_;
};

}  // namespace unwindstack

#endif  // _LIBUNWINDSTACK_UTILS_OFFLINE_UNWIND_UTILS_H