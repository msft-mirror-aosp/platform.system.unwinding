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

#include <memory>
#include <vector>

#include <unwindstack/Elf.h>
#include <unwindstack/JitDebug.h>
#include <unwindstack/Maps.h>

#include "Check.h"
#include "MemoryRange.h"

// This implements the JIT Compilation Interface.
// See https://sourceware.org/gdb/onlinedocs/gdb/JIT-Interface.html

namespace unwindstack {

template <typename Uintptr_T, typename Uint64_T>
class JitDebugImpl : public JitDebug, public Global {
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

  JitDebugImpl(ArchEnum arch, std::shared_ptr<Memory>& memory,
               std::vector<std::string>& search_libs)
      : Global(memory, search_libs) {
    SetArch(arch);
  }

  ~JitDebugImpl() {
    for (auto* elf : elf_list_) {
      delete elf;
    }
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

    FindAndReadVariable(maps, "__jit_debug_descriptor");
  }

  Elf* Find(Maps* maps, uint64_t pc) {
    // Use a single lock, this object should be used so infrequently that
    // a fine grain lock is unnecessary.
    std::lock_guard<std::mutex> guard(lock_);
    if (!initialized_) {
      Init(maps);
    }

    // Search the existing elf object first.
    for (Elf* elf : elf_list_) {
      if (elf->IsValidPc(pc)) {
        return elf;
      }
    }

    while (entry_addr_ != 0) {
      uint64_t start;
      uint64_t size;
      entry_addr_ = ReadEntry(&start, &size);

      Elf* elf = new Elf(new MemoryRange(memory_, start, size, 0));
      elf->Init();
      if (!elf->valid()) {
        // The data is not formatted in a way we understand, do not attempt
        // to process any other entries.
        entry_addr_ = 0;
        delete elf;
        return nullptr;
      }
      elf_list_.push_back(elf);

      if (elf->IsValidPc(pc)) {
        return elf;
      }
    }
    return nullptr;
  }
};

std::unique_ptr<JitDebug> CreateJitDebug(ArchEnum arch, std::shared_ptr<Memory>& memory,
                                         std::vector<std::string> search_libs) {
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
      using Impl = JitDebugImpl<uint32_t, Uint64_P>;
      static_assert(offsetof(typename Impl::JITCodeEntry, symfile_size) == 12, "layout");
      static_assert(sizeof(typename Impl::JITCodeEntry) == 20, "layout");
      static_assert(sizeof(typename Impl::JITDescriptor) == 16, "layout");
      return std::unique_ptr<JitDebug>(new Impl(arch, memory, search_libs));
    }
    case ARCH_ARM:
    case ARCH_MIPS: {
      using Impl = JitDebugImpl<uint32_t, Uint64_A>;
      static_assert(offsetof(typename Impl::JITCodeEntry, symfile_size) == 16, "layout");
      static_assert(sizeof(typename Impl::JITCodeEntry) == 24, "layout");
      static_assert(sizeof(typename Impl::JITDescriptor) == 16, "layout");
      return std::unique_ptr<JitDebug>(new Impl(arch, memory, search_libs));
    }
    case ARCH_ARM64:
    case ARCH_X86_64:
    case ARCH_MIPS64: {
      using Impl = JitDebugImpl<uint64_t, Uint64_A>;
      static_assert(offsetof(typename Impl::JITCodeEntry, symfile_size) == 24, "layout");
      static_assert(sizeof(typename Impl::JITCodeEntry) == 32, "layout");
      static_assert(sizeof(typename Impl::JITDescriptor) == 24, "layout");
      return std::unique_ptr<JitDebug>(new Impl(arch, memory, search_libs));
    }
    default:
      abort();
  }
}

}  // namespace unwindstack
