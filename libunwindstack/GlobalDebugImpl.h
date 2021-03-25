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

#ifndef _LIBUNWINDSTACK_GLOBAL_DEBUG_IMPL_H
#define _LIBUNWINDSTACK_GLOBAL_DEBUG_IMPL_H

#include <stdint.h>
#include <sys/mman.h>

#include <memory>
#include <vector>

#include <unwindstack/Global.h>
#include <unwindstack/Maps.h>

#include "Check.h"
#include "GlobalDebugInterface.h"
#include "MemoryRange.h"

// This implements the JIT Compilation Interface.
// See https://sourceware.org/gdb/onlinedocs/gdb/JIT-Interface.html
//
// We use it to get in-memory ELF files created by the ART compiler,
// but we also use it to get list of DEX files used by the runtime.

namespace unwindstack {

// Implementation templated for ELF/DEX and for different architectures.
template <typename Symfile, typename Uintptr_T, typename Uint64_T>
class GlobalDebugImpl : public GlobalDebugInterface<Symfile>, public Global {
 public:
  struct JITCodeEntry {
    Uintptr_T next;
    Uintptr_T prev;
    Uintptr_T symfile_addr;
    Uint64_T symfile_size;
  };

  struct JITDescriptor {
    uint32_t version;
    uint32_t action_flag;
    Uintptr_T relevant_entry;
    Uintptr_T first_entry;
  };

  GlobalDebugImpl(ArchEnum arch, std::shared_ptr<Memory>& memory,
                  std::vector<std::string>& search_libs, const char* global_variable_name)
      : Global(memory, search_libs), global_variable_name_(global_variable_name) {
    SetArch(arch);
  }

  uint64_t ReadDescriptor(uint64_t addr) {
    JITDescriptor desc;
    if (!memory_->ReadFully(addr, &desc, sizeof(desc))) {
      return 0;
    }

    if (desc.version != 1 || desc.first_entry == 0) {
      // Either unknown version, or no jit entries.
      return 0;
    }

    return desc.first_entry;
  }

  uint64_t ReadEntry(uint64_t* start, uint64_t* size) {
    JITCodeEntry code;
    if (!memory_->ReadFully(entry_addr_, &code, sizeof(code))) {
      return 0;
    }

    *start = code.symfile_addr;
    *size = code.symfile_size.value;
    return code.next;
  }

  void ProcessArch() {}

  bool ReadVariableData(uint64_t ptr) {
    entry_addr_ = ReadDescriptor(ptr);
    return entry_addr_ != 0;
  }

  void Init(Maps* maps) {
    if (initialized_) {
      return;
    }
    // Regardless of what happens below, consider the init finished.
    initialized_ = true;

    FindAndReadVariable(maps, global_variable_name_);
  }

  // Iterate over all symfiles and call the provided callback for reach symfile.
  // Returns true if any callback returns true (which also aborts each iteration).
  template <typename Callback /* (Symfile*) -> bool */>
  bool ForEachSymfile(Maps* maps, Callback callback) {
    // Use a single lock, this object should be used so infrequently that
    // a fine grain lock is unnecessary.
    std::lock_guard<std::mutex> guard(lock_);
    if (!initialized_) {
      Init(maps);
    }

    // Search the existing elf object first.
    for (std::unique_ptr<Symfile>& entry : entries_) {
      if (callback(entry.get())) {
        return true;
      }
    }

    while (entry_addr_ != 0) {
      uint64_t start;
      uint64_t size;
      entry_addr_ = ReadEntry(&start, &size);

      std::unique_ptr<Symfile> entry;
      if (!this->Load(maps, memory_, start, size, /*out*/ entry)) {
        continue;  // Failed to load symbol file.
      }
      entries_.push_back(std::move(entry));
      if (callback(entries_.back().get())) {
        return true;
      }
    }
    return false;
  }

  bool GetFunctionName(Maps* maps, uint64_t pc, SharedString* name, uint64_t* offset) {
    // NB: If symfiles overlap in PC ranges, this will check all of them.
    return ForEachSymfile(maps, [pc, name, offset](Symfile* file) {
      return file->IsValidPc(pc) && file->GetFunctionName(pc, name, offset);
    });
  }

  Symfile* Find(Maps* maps, uint64_t pc) {
    // NB: If symfiles overlap in PC ranges, this will return the first match.
    Symfile* result = nullptr;
    ForEachSymfile(maps, [&result, pc](Symfile* file) {
      if (file->IsValidPc(pc)) {
        result = file;
        return true;
      }
      return false;
    });
    return result;
  }

 private:
  const char* global_variable_name_ = nullptr;
  uint64_t entry_addr_ = 0;
  bool initialized_ = false;
  std::vector<std::unique_ptr<Symfile>> entries_;

  std::mutex lock_;
};

template <typename Symfile>
std::unique_ptr<GlobalDebugInterface<Symfile>> CreateGlobalDebugImpl(
    ArchEnum arch, std::shared_ptr<Memory>& memory, std::vector<std::string> search_libs,
    const char* global_variable_name) {
  // uint64_t values on x86 are not naturally aligned,
  // but uint64_t values on ARM are naturally aligned.
  struct Uint64_P {
    uint64_t value;
  } __attribute__((packed));
  struct Uint64_A {
    uint64_t value;
  } __attribute__((aligned(8)));

  CHECK(arch != ARCH_UNKNOWN);
  switch (arch) {
    case ARCH_X86: {
      using Impl = GlobalDebugImpl<Symfile, uint32_t, Uint64_P>;
      static_assert(offsetof(typename Impl::JITCodeEntry, symfile_size) == 12, "layout");
      static_assert(sizeof(typename Impl::JITCodeEntry) == 20, "layout");
      static_assert(sizeof(typename Impl::JITDescriptor) == 16, "layout");
      return std::make_unique<Impl>(arch, memory, search_libs, global_variable_name);
    }
    case ARCH_ARM:
    case ARCH_MIPS: {
      using Impl = GlobalDebugImpl<Symfile, uint32_t, Uint64_A>;
      static_assert(offsetof(typename Impl::JITCodeEntry, symfile_size) == 16, "layout");
      static_assert(sizeof(typename Impl::JITCodeEntry) == 24, "layout");
      static_assert(sizeof(typename Impl::JITDescriptor) == 16, "layout");
      return std::make_unique<Impl>(arch, memory, search_libs, global_variable_name);
    }
    case ARCH_ARM64:
    case ARCH_X86_64:
    case ARCH_MIPS64: {
      using Impl = GlobalDebugImpl<Symfile, uint64_t, Uint64_A>;
      static_assert(offsetof(typename Impl::JITCodeEntry, symfile_size) == 24, "layout");
      static_assert(sizeof(typename Impl::JITCodeEntry) == 32, "layout");
      static_assert(sizeof(typename Impl::JITDescriptor) == 24, "layout");
      return std::make_unique<Impl>(arch, memory, search_libs, global_variable_name);
    }
    default:
      abort();
  }
}

}  // namespace unwindstack

#endif  // _LIBUNWINDSTACK_GLOBAL_DEBUG_IMPL_H
