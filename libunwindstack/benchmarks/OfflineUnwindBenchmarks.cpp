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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>

#include <benchmark/benchmark.h>

#include <unwindstack/Arch.h>
#include <unwindstack/Unwinder.h>

#include "Utils.h"
#include "utils/OfflineUnwindUtils.h"

// This collection of benchmarks exercises Unwinder::Unwind for offline unwinds.
//
// See `libunwindstack/utils/OfflineUnwindUtils.h` for more info on offline unwinds
// and b/192012600 for additional information regarding these benchmarks.
namespace unwindstack {
namespace {

class OfflineUnwindBenchmark : public benchmark::Fixture {
 public:
  void TearDown(benchmark::State& state) override {
    offline_utils_.ReturnToCurrentWorkingDirectory();
    mem_tracker_.SetBenchmarkCounters(state);
  }

  void RunBenchmark(benchmark::State& state, const std::string& offline_files_dir,
                    size_t expected_num_frames, ArchEnum arch, bool cache_maps, bool is_jit_debug) {
    std::string error_msg;
    if (!offline_utils_.Init(offline_files_dir, arch, error_msg, /*add_stack=*/true, cache_maps)) {
      state.SkipWithError(error_msg.c_str());
      return;
    }
    if (is_jit_debug && !offline_utils_.SetJitProcessMemory(error_msg)) {
      state.SkipWithError(error_msg.c_str());
      return;
    }

    std::unique_ptr<JitDebug> jit_debug;
    std::stringstream err_stream;
    Unwinder unwinder(0, nullptr, nullptr);
    for (auto _ : state) {
      state.PauseTiming();
      // Need to init unwinder with new copy of regs each iteration because unwinding changes
      // the attributes of the regs object.
      std::unique_ptr<Regs> regs_copy(offline_utils_.GetRegs()->Clone());
      // If we don't want to use cached maps, make sure to reset them.
      if (!cache_maps && !offline_utils_.ResetMaps(error_msg)) {
        state.SkipWithError(error_msg.c_str());
        return;
      }
      mem_tracker_.StartTrackingAllocations();
      state.ResumeTiming();

      std::shared_ptr<Memory> process_memory = offline_utils_.GetProcessMemory();
      unwinder = Unwinder(128, offline_utils_.GetMaps(), regs_copy.get(), process_memory);
      if (is_jit_debug) {
        jit_debug = CreateJitDebug(regs_copy->Arch(), process_memory);
        unwinder.SetJitDebug(jit_debug.get());
      }
      unwinder.Unwind();

      state.PauseTiming();
      mem_tracker_.StopTrackingAllocations();
      if (unwinder.NumFrames() != expected_num_frames) {
        err_stream << "Failed to unwind properly.Expected " << expected_num_frames
                   << " frames, but unwinder contained " << unwinder.NumFrames() << " frames.\n";
        break;
      }
      state.ResumeTiming();
    }
  }

 protected:
  MemoryTracker mem_tracker_;
  OfflineUnwindUtils offline_utils_;
};

BENCHMARK_F(OfflineUnwindBenchmark, BM_offline_straddle_arm64)(benchmark::State& state) {
  RunBenchmark(state, "straddle_arm64/", /*expected_num_frames=*/6, ARCH_ARM64,
               /*cached_maps=*/false, /*is_jit_debug=*/false);
}

BENCHMARK_F(OfflineUnwindBenchmark, BM_offline_straddle_arm64_cached_maps)
(benchmark::State& state) {
  RunBenchmark(state, "straddle_arm64/", /*expected_num_frames=*/6, ARCH_ARM64,
               /*cached_maps=*/true, /*is_jit_debug=*/false);
}

BENCHMARK_F(OfflineUnwindBenchmark, BM_offline_jit_debug_x86)(benchmark::State& state) {
  RunBenchmark(state, "jit_debug_x86/", /*expected_num_frames=*/69, ARCH_X86,
               /*cached_maps=*/false, /*is_jit_debug=*/true);
}

}  // namespace
}  // namespace unwindstack
