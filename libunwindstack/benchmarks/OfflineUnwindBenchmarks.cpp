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

#include <err.h>
#include <inttypes.h>
#include <malloc.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <sstream>

#include <benchmark/benchmark.h>

#include <android-base/file.h>

#include <unwindstack/MachineArm.h>
#include <unwindstack/MachineArm64.h>
#include <unwindstack/MachineX86.h>
#include <unwindstack/MachineX86_64.h>
#include <unwindstack/Maps.h>
#include <unwindstack/RegsArm.h>
#include <unwindstack/RegsArm64.h>
#include <unwindstack/RegsX86.h>
#include <unwindstack/RegsX86_64.h>
#include <unwindstack/Unwinder.h>

#include "MemoryOffline.h"
#include "Utils.h"

namespace unwindstack {

static void AddMemory(std::string file_name, MemoryOfflineParts* parts) {
  MemoryOffline* memory = new MemoryOffline;
  if (!memory->Init(file_name.c_str(), 0)) {
    errx(1, "Failed to add stack '%s' to stack memory.", file_name.c_str());
  }
  parts->Add(memory);
}

class OfflineUnwindBenchmark : public benchmark::Fixture {
 public:
  void TearDown(benchmark::State& state) override {
    std::filesystem::current_path(cwd_);
    total_iterations_ += state.iterations();
#if defined(__BIONIC__)
    state.counters["MEAN_RSS_BYTES"] = total_rss_bytes_ / static_cast<double>(total_iterations_);
    state.counters["MAX_RSS_BYTES"] = max_rss_bytes_;
    state.counters["MIN_RSS_BYTES"] = min_rss_bytes_;
#endif
    state.counters["MEAN_ALLOCATED_BYTES"] =
        total_alloc_bytes_ / static_cast<double>(total_iterations_);
    state.counters["MAX_ALLOCATED_BYTES"] = max_alloc_bytes_;
    state.counters["MIN_ALLOCATED_BYTES"] = min_alloc_bytes_;
  }

 protected:
  void Init(const char* file_dir, ArchEnum arch, bool set_maps = false) {
    // Change to offline files directory so we can read the ELF files
    cwd_ = std::filesystem::current_path();
    offline_dir_ = android::base::GetExecutableDirectory() + "/tests/files/offline/" + file_dir;
    std::filesystem::current_path(std::filesystem::path(offline_dir_));

    if (!android::base::ReadFileToString((offline_dir_ + "maps.txt"), &map_buffer)) {
      errx(1, "Failed to read from '%s' into memory.", (offline_dir_ + "maps.txt").c_str());
    }
    if (set_maps) {
      ResetMaps();
    }

    SetProcessMemory();
    SetRegs(arch);
  }

  void ResetMaps() {
    maps_.reset(new BufferMaps(map_buffer.c_str()));
    if (!maps_->Parse()) {
      errx(1, "Failed to parse offline maps.");
    }
  }

  void SetProcessMemory() {
    std::string stack_name(offline_dir_ + "stack.data");
    struct stat st;
    if (stat(stack_name.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
      auto stack_memory = std::make_unique<MemoryOffline>();
      if (!stack_memory->Init((offline_dir_ + "stack.data").c_str(), 0)) {
        errx(1, "Failed to initialize stack memory from %s.",
             (offline_dir_ + "stack.data").c_str());
      }
      process_memory_.reset(stack_memory.release());
    } else {
      std::unique_ptr<MemoryOfflineParts> stack_memory(new MemoryOfflineParts);
      for (size_t i = 0;; i++) {
        stack_name = offline_dir_ + "stack" + std::to_string(i) + ".data";
        if (stat(stack_name.c_str(), &st) == -1 || !S_ISREG(st.st_mode)) {
          if (i == 0) {
            errx(1, "No stack data files found.");
          }
          break;
        }
        AddMemory(stack_name, stack_memory.get());
      }
      process_memory_.reset(stack_memory.release());
    }
  }

  void SetJitProcessMemory() {
    MemoryOfflineParts* memory = new MemoryOfflineParts;
    AddMemory(offline_dir_ + "descriptor.data", memory);
    AddMemory(offline_dir_ + "stack.data", memory);
    for (size_t i = 0; i < 7; i++) {
      AddMemory(offline_dir_ + "entry" + std::to_string(i) + ".data", memory);
      AddMemory(offline_dir_ + "jit" + std::to_string(i) + ".data", memory);
    }
    process_memory_.reset(memory);
  }

  void SetRegs(ArchEnum arch) {
    switch (arch) {
      case ARCH_ARM: {
        RegsArm* regs = new RegsArm;
        regs_.reset(regs);
        ReadRegs<uint32_t>(offline_dir_, regs, arm_regs_);
        break;
      }
      case ARCH_ARM64: {
        RegsArm64* regs = new RegsArm64;
        regs_.reset(regs);
        ReadRegs<uint64_t>(offline_dir_, regs, arm64_regs_);
        break;
      }
      case ARCH_X86: {
        RegsX86* regs = new RegsX86;
        regs_.reset(regs);
        ReadRegs<uint32_t>(offline_dir_, regs, x86_regs_);
        break;
      }
      case ARCH_X86_64: {
        RegsX86_64* regs = new RegsX86_64;
        regs_.reset(regs);
        ReadRegs<uint64_t>(offline_dir_, regs, x86_64_regs_);
        break;
      }
      default:
        errx(1, "Unknown arch %s", std::to_string(arch).c_str());
    }
  }

  template <typename AddressType>
  void ReadRegs(const std::string& offline_dir_, RegsImpl<AddressType>* regs,
                const std::unordered_map<std::string, uint32_t>& name_to_reg) {
    FILE* fp = fopen((offline_dir_ + "regs.txt").c_str(), "r");
    if (fp == nullptr) {
      err(1, NULL);
    }
    while (!feof(fp)) {
      uint64_t value;
      char reg_name[100];
      if (fscanf(fp, "%s %" SCNx64 "\n", reg_name, &value) != 2) {
        errx(1, "Failed to read in register name/values from %s.",
             (offline_dir_ + "regs.txt").c_str());
      }
      std::string name(reg_name);
      if (!name.empty()) {
        // Remove the : from the end.
        name.resize(name.size() - 1);
      }
      auto entry = name_to_reg.find(name);
      if (entry == name_to_reg.end()) {
        errx(1, "Unknown register named %s", reg_name);
      }
      (*regs)[entry->second] = value;
    }
    fclose(fp);
  }

  void StartTrackingAllocations() {
#if defined(__BIONIC__)
    mallopt(M_PURGE, 0);
    rss_bytes_before_ = 0;
    GatherRss(&rss_bytes_before_);
#endif
    alloc_bytes_before_ = mallinfo().uordblks;
  }

  void StopTrackingAllocations() {
#if defined(__BIONIC__)
    mallopt(M_PURGE, 0);
#endif
    uint64_t bytes_alloced = mallinfo().uordblks - alloc_bytes_before_;
    total_alloc_bytes_ += bytes_alloced;
    if (bytes_alloced > max_alloc_bytes_) max_alloc_bytes_ = bytes_alloced;
    if (bytes_alloced < min_alloc_bytes_) min_alloc_bytes_ = bytes_alloced;
#if defined(__BIONIC__)
    uint64_t rss_bytes = 0;
    GatherRss(&rss_bytes);
    total_rss_bytes_ += rss_bytes - rss_bytes_before_;
    if (rss_bytes > max_rss_bytes_) max_rss_bytes_ = rss_bytes;
    if (rss_bytes < min_rss_bytes_) min_rss_bytes_ = rss_bytes;
#endif
  }

  static std::unordered_map<std::string, uint32_t> arm_regs_;
  static std::unordered_map<std::string, uint32_t> arm64_regs_;
  static std::unordered_map<std::string, uint32_t> x86_regs_;
  static std::unordered_map<std::string, uint32_t> x86_64_regs_;

  std::string cwd_;
  std::string offline_dir_;
  std::string map_buffer;
  std::unique_ptr<Regs> regs_;
  std::unique_ptr<Maps> maps_;
  std::shared_ptr<Memory> process_memory_;
#if defined(__BIONIC__)
  uint64_t total_rss_bytes_ = 0;
  uint64_t min_rss_bytes_ = 0;
  uint64_t max_rss_bytes_ = 0;
  uint64_t rss_bytes_before_;
#endif
  uint64_t total_alloc_bytes_ = 0;
  uint64_t min_alloc_bytes_ = std::numeric_limits<uint64_t>::max();
  uint64_t max_alloc_bytes_ = 0;
  uint64_t alloc_bytes_before_;
  // Benchmarks may run multiple times (the whole benchmark not just what is in the ranged based
  // for loop) but this instance is not destructed and re-constructed each time. So this holds the
  // total number of iterations of the ranged for loop across all runs of a single benchmark.
  size_t total_iterations_ = 0;
};

std::unordered_map<std::string, uint32_t> OfflineUnwindBenchmark::arm_regs_ = {
    {"r0", ARM_REG_R0},  {"r1", ARM_REG_R1}, {"r2", ARM_REG_R2},   {"r3", ARM_REG_R3},
    {"r4", ARM_REG_R4},  {"r5", ARM_REG_R5}, {"r6", ARM_REG_R6},   {"r7", ARM_REG_R7},
    {"r8", ARM_REG_R8},  {"r9", ARM_REG_R9}, {"r10", ARM_REG_R10}, {"r11", ARM_REG_R11},
    {"ip", ARM_REG_R12}, {"sp", ARM_REG_SP}, {"lr", ARM_REG_LR},   {"pc", ARM_REG_PC},
};

std::unordered_map<std::string, uint32_t> OfflineUnwindBenchmark::arm64_regs_ = {
    {"x0", ARM64_REG_R0},      {"x1", ARM64_REG_R1},   {"x2", ARM64_REG_R2},
    {"x3", ARM64_REG_R3},      {"x4", ARM64_REG_R4},   {"x5", ARM64_REG_R5},
    {"x6", ARM64_REG_R6},      {"x7", ARM64_REG_R7},   {"x8", ARM64_REG_R8},
    {"x9", ARM64_REG_R9},      {"x10", ARM64_REG_R10}, {"x11", ARM64_REG_R11},
    {"x12", ARM64_REG_R12},    {"x13", ARM64_REG_R13}, {"x14", ARM64_REG_R14},
    {"x15", ARM64_REG_R15},    {"x16", ARM64_REG_R16}, {"x17", ARM64_REG_R17},
    {"x18", ARM64_REG_R18},    {"x19", ARM64_REG_R19}, {"x20", ARM64_REG_R20},
    {"x21", ARM64_REG_R21},    {"x22", ARM64_REG_R22}, {"x23", ARM64_REG_R23},
    {"x24", ARM64_REG_R24},    {"x25", ARM64_REG_R25}, {"x26", ARM64_REG_R26},
    {"x27", ARM64_REG_R27},    {"x28", ARM64_REG_R28}, {"x29", ARM64_REG_R29},
    {"sp", ARM64_REG_SP},      {"lr", ARM64_REG_LR},   {"pc", ARM64_REG_PC},
    {"pst", ARM64_REG_PSTATE},
};

std::unordered_map<std::string, uint32_t> OfflineUnwindBenchmark::x86_regs_ = {
    {"eax", X86_REG_EAX}, {"ebx", X86_REG_EBX}, {"ecx", X86_REG_ECX},
    {"edx", X86_REG_EDX}, {"ebp", X86_REG_EBP}, {"edi", X86_REG_EDI},
    {"esi", X86_REG_ESI}, {"esp", X86_REG_ESP}, {"eip", X86_REG_EIP},
};

std::unordered_map<std::string, uint32_t> OfflineUnwindBenchmark::x86_64_regs_ = {
    {"rax", X86_64_REG_RAX}, {"rbx", X86_64_REG_RBX}, {"rcx", X86_64_REG_RCX},
    {"rdx", X86_64_REG_RDX}, {"r8", X86_64_REG_R8},   {"r9", X86_64_REG_R9},
    {"r10", X86_64_REG_R10}, {"r11", X86_64_REG_R11}, {"r12", X86_64_REG_R12},
    {"r13", X86_64_REG_R13}, {"r14", X86_64_REG_R14}, {"r15", X86_64_REG_R15},
    {"rdi", X86_64_REG_RDI}, {"rsi", X86_64_REG_RSI}, {"rbp", X86_64_REG_RBP},
    {"rsp", X86_64_REG_RSP}, {"rip", X86_64_REG_RIP},
};

static std::string DumpFrames(const Unwinder& unwinder) {
  std::string str;
  for (size_t i = 0; i < unwinder.NumFrames(); i++) {
    str += unwinder.FormatFrame(i) + "\n";
  }
  return str;
}

static void VerifyFrames(const Unwinder unwinder, const std::string& expected_frame_info,
                         std::stringstream& err_stream, benchmark::State& state) {
  std::string frame_info(DumpFrames(unwinder));
  if (frame_info != expected_frame_info) {
    if (err_stream.rdbuf()->in_avail() == 0) {
      err_stream << "Failed to unwind properly. ";
    }
    err_stream << "Unwinder contained frames:\n"
               << frame_info << "\nExpected frames:\n"
               << expected_frame_info;
    state.SkipWithError(err_stream.str().c_str());
  }
}

BENCHMARK_F(OfflineUnwindBenchmark, BM_offline_straddle_arm64)(benchmark::State& state) {
  Init("straddle_arm64/", ARCH_ARM64);
  Unwinder unwinder(0, nullptr, nullptr);
  std::stringstream err_stream;
  for (auto _ : state) {
    state.PauseTiming();
    // Need to init unwinder with new copy of regs each iteration because unwinding changes
    // the attributes of the regs object.
    std::unique_ptr<Regs> regs_copy(regs_->Clone());
    // Ensure unwinder does not used cached map data in next iteration
    ResetMaps();

    StartTrackingAllocations();
    state.ResumeTiming();

    unwinder = Unwinder(128, maps_.get(), regs_copy.get(), process_memory_);
    unwinder.Unwind();

    state.PauseTiming();
    StopTrackingAllocations();
    if (unwinder.NumFrames() != 6U) {
      err_stream << "Failed to unwind properly.Expected 6 frames, but unwinder contained "
                 << unwinder.NumFrames() << " frames.\n";
      break;
    }
    state.ResumeTiming();
  }

  std::string expected_frame_info =
      "  #00 pc 0000000000429fd8  libunwindstack_test (SignalInnerFunction+24)\n"
      "  #01 pc 000000000042a078  libunwindstack_test (SignalMiddleFunction+8)\n"
      "  #02 pc 000000000042a08c  libunwindstack_test (SignalOuterFunction+8)\n"
      "  #03 pc 000000000042d8fc  libunwindstack_test "
      "(unwindstack::RemoteThroughSignal(int, unsigned int)+20)\n"
      "  #04 pc 000000000042d8d8  libunwindstack_test "
      "(unwindstack::UnwindTest_remote_through_signal_Test::TestBody()+32)\n"
      "  #05 pc 0000000000455d70  libunwindstack_test (testing::Test::Run()+392)\n";
  VerifyFrames(unwinder, expected_frame_info, err_stream, state);
}

BENCHMARK_F(OfflineUnwindBenchmark, BM_offline_straddle_arm64_cached_maps)
(benchmark::State& state) {
  // Initialize maps in Init and do not reset unwinder's maps each time so the unwinder
  // uses the cached maps from the first iteration of the loop.
  Init("straddle_arm64/", ARCH_ARM64, /*set_maps=*/true);
  Unwinder unwinder(0, nullptr, nullptr);
  std::stringstream err_stream;
  for (auto _ : state) {
    state.PauseTiming();
    // Need to init unwinder with new copy of regs each iteration because unwinding changes
    // the attributes of the regs object.
    std::unique_ptr<Regs> regs_copy(regs_->Clone());

    StartTrackingAllocations();
    state.ResumeTiming();

    unwinder = Unwinder(128, maps_.get(), regs_copy.get(), process_memory_);
    unwinder.Unwind();

    state.PauseTiming();
    StopTrackingAllocations();
    if (unwinder.NumFrames() != 6U) {
      err_stream << "Failed to unwind properly. Expected 6 frames, but unwinder contained "
                 << unwinder.NumFrames() << " frames.\n";
      break;
    }
    state.ResumeTiming();
  }

  std::string expected_frame_info =
      "  #00 pc 0000000000429fd8  libunwindstack_test (SignalInnerFunction+24)\n"
      "  #01 pc 000000000042a078  libunwindstack_test (SignalMiddleFunction+8)\n"
      "  #02 pc 000000000042a08c  libunwindstack_test (SignalOuterFunction+8)\n"
      "  #03 pc 000000000042d8fc  libunwindstack_test "
      "(unwindstack::RemoteThroughSignal(int, unsigned int)+20)\n"
      "  #04 pc 000000000042d8d8  libunwindstack_test "
      "(unwindstack::UnwindTest_remote_through_signal_Test::TestBody()+32)\n"
      "  #05 pc 0000000000455d70  libunwindstack_test (testing::Test::Run()+392)\n";
  VerifyFrames(unwinder, expected_frame_info, err_stream, state);
}

BENCHMARK_F(OfflineUnwindBenchmark, BM_offline_jit_debug_x86)(benchmark::State& state) {
  Init("jit_debug_x86/", ARCH_X86);
  SetJitProcessMemory();

  Unwinder unwinder(0, nullptr, nullptr);
  std::stringstream err_stream;
  for (auto _ : state) {
    state.PauseTiming();
    std::unique_ptr<Regs> regs_copy(regs_->Clone());
    ResetMaps();

    StartTrackingAllocations();
    state.ResumeTiming();

    std::unique_ptr<JitDebug> jit_debug = CreateJitDebug(regs_copy->Arch(), process_memory_);
    unwinder = Unwinder(128, maps_.get(), regs_copy.get(), process_memory_);
    unwinder.SetJitDebug(jit_debug.get());
    unwinder.Unwind();

    state.PauseTiming();
    StopTrackingAllocations();

    if (unwinder.NumFrames() != 69U) {
      err_stream << "Failed to unwind properly. Expected 6 frames, but unwinder contained "
                 << unwinder.NumFrames() << " frames.\n";
      break;
    }
    state.ResumeTiming();
  }

  std::string expected_frame_info =
      "  #00 pc 00068fb8  libarttestd.so (art::CauseSegfault()+72)\n"
      "  #01 pc 00067f00  libarttestd.so (Java_Main_unwindInProcess+10032)\n"
      "  #02 pc 000021a8  137-cfi.odex (boolean Main.unwindInProcess(boolean, int, "
      "boolean)+136)\n"
      "  #03 pc 0000fe80  anonymous:ee74c000 (boolean Main.bar(boolean)+64)\n"
      "  #04 pc 006ad4d2  libartd.so (art_quick_invoke_stub+338)\n"
      "  #05 pc 00146ab5  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+885)\n"
      "  #06 pc 0039cf0d  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+653)\n"
      "  #07 pc 00392552  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+354)\n"
      "  #08 pc 0039399a  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+234)\n"
      "  #09 pc 00684362  libartd.so (artQuickToInterpreterBridge+1058)\n"
      "  #10 pc 006b35bd  libartd.so (art_quick_to_interpreter_bridge+77)\n"
      "  #11 pc 0000fe03  anonymous:ee74c000 (int Main.compare(Main, Main)+51)\n"
      "  #12 pc 006ad4d2  libartd.so (art_quick_invoke_stub+338)\n"
      "  #13 pc 00146ab5  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+885)\n"
      "  #14 pc 0039cf0d  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+653)\n"
      "  #15 pc 00392552  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+354)\n"
      "  #16 pc 0039399a  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+234)\n"
      "  #17 pc 00684362  libartd.so (artQuickToInterpreterBridge+1058)\n"
      "  #18 pc 006b35bd  libartd.so (art_quick_to_interpreter_bridge+77)\n"
      "  #19 pc 0000fd3b  anonymous:ee74c000 (int Main.compare(java.lang.Object, "
      "java.lang.Object)+107)\n"
      "  #20 pc 006ad4d2  libartd.so (art_quick_invoke_stub+338)\n"
      "  #21 pc 00146ab5  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+885)\n"
      "  #22 pc 0039cf0d  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+653)\n"
      "  #23 pc 00392552  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+354)\n"
      "  #24 pc 0039399a  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+234)\n"
      "  #25 pc 00684362  libartd.so (artQuickToInterpreterBridge+1058)\n"
      "  #26 pc 006b35bd  libartd.so (art_quick_to_interpreter_bridge+77)\n"
      "  #27 pc 0000fbdb  anonymous:ee74c000 (int "
      "java.util.Arrays.binarySearch0(java.lang.Object[], int, int, java.lang.Object, "
      "java.util.Comparator)+331)\n"
      "  #28 pc 006ad6a2  libartd.so (art_quick_invoke_static_stub+418)\n"
      "  #29 pc 00146acb  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+907)\n"
      "  #30 pc 0039cf0d  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+653)\n"
      "  #31 pc 00392552  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+354)\n"
      "  #32 pc 0039399a  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+234)\n"
      "  #33 pc 00684362  libartd.so (artQuickToInterpreterBridge+1058)\n"
      "  #34 pc 006b35bd  libartd.so (art_quick_to_interpreter_bridge+77)\n"
      "  #35 pc 0000f624  anonymous:ee74c000 (boolean Main.foo()+164)\n"
      "  #36 pc 006ad4d2  libartd.so (art_quick_invoke_stub+338)\n"
      "  #37 pc 00146ab5  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+885)\n"
      "  #38 pc 0039cf0d  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+653)\n"
      "  #39 pc 00392552  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+354)\n"
      "  #40 pc 0039399a  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+234)\n"
      "  #41 pc 00684362  libartd.so (artQuickToInterpreterBridge+1058)\n"
      "  #42 pc 006b35bd  libartd.so (art_quick_to_interpreter_bridge+77)\n"
      "  #43 pc 0000eedb  anonymous:ee74c000 (void Main.runPrimary()+59)\n"
      "  #44 pc 006ad4d2  libartd.so (art_quick_invoke_stub+338)\n"
      "  #45 pc 00146ab5  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+885)\n"
      "  #46 pc 0039cf0d  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+653)\n"
      "  #47 pc 00392552  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+354)\n"
      "  #48 pc 0039399a  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+234)\n"
      "  #49 pc 00684362  libartd.so (artQuickToInterpreterBridge+1058)\n"
      "  #50 pc 006b35bd  libartd.so (art_quick_to_interpreter_bridge+77)\n"
      "  #51 pc 0000ac21  anonymous:ee74c000 (void Main.main(java.lang.String[])+97)\n"
      "  #52 pc 006ad6a2  libartd.so (art_quick_invoke_static_stub+418)\n"
      "  #53 pc 00146acb  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+907)\n"
      "  #54 pc 0039cf0d  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+653)\n"
      "  #55 pc 00392552  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+354)\n"
      "  #56 pc 0039399a  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+234)\n"
      "  #57 pc 00684362  libartd.so (artQuickToInterpreterBridge+1058)\n"
      "  #58 pc 006b35bd  libartd.so (art_quick_to_interpreter_bridge+77)\n"
      "  #59 pc 006ad6a2  libartd.so (art_quick_invoke_static_stub+418)\n"
      "  #60 pc 00146acb  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+907)\n"
      "  #61 pc 005aac95  libartd.so "
      "(art::InvokeWithArgArray(art::ScopedObjectAccessAlreadyRunnable const&, art::ArtMethod*, "
      "art::ArgArray*, art::JValue*, char const*)+85)\n"
      "  #62 pc 005aab5a  libartd.so "
      "(art::InvokeWithVarArgs(art::ScopedObjectAccessAlreadyRunnable const&, _jobject*, "
      "_jmethodID*, char*)+362)\n"
      "  #63 pc 0048a3dd  libartd.so "
      "(art::JNI::CallStaticVoidMethodV(_JNIEnv*, _jclass*, _jmethodID*, char*)+125)\n"
      "  #64 pc 0018448c  libartd.so "
      "(art::CheckJNI::CallMethodV(char const*, _JNIEnv*, _jobject*, _jclass*, _jmethodID*, char*, "
      "art::Primitive::Type, art::InvokeType)+1964)\n"
      "  #65 pc 0017cf06  libartd.so "
      "(art::CheckJNI::CallStaticVoidMethodV(_JNIEnv*, _jclass*, _jmethodID*, char*)+70)\n"
      "  #66 pc 00001d8c  dalvikvm32 "
      "(_JNIEnv::CallStaticVoidMethod(_jclass*, _jmethodID*, ...)+60)\n"
      "  #67 pc 00001a80  dalvikvm32 (main+1312)\n"
      "  #68 pc 00018275  libc.so\n";
  VerifyFrames(unwinder, expected_frame_info, err_stream, state);
}

}  // namespace unwindstack
