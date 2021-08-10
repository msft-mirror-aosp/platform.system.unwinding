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
#include <vector>

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

  void RunBenchmark(benchmark::State& state, const std::string& offline_files_dir, ArchEnum arch,
                    ProcessMemoryFlag memory_flag, bool cache_maps = false) {
    std::string error_msg;
    if (!offline_utils_.Init(offline_files_dir, arch, &error_msg, memory_flag, cache_maps)) {
      state.SkipWithError(error_msg.c_str());
      return;
    }

    for (auto _ : state) {
      state.PauseTiming();
      size_t expected_num_frames;
      if (!offline_utils_.GetExpectedNumFrames(&expected_num_frames, &error_msg)) {
        state.SkipWithError(error_msg.c_str());
        return;
      }
      // Need to init unwinder with new copy of regs each iteration because unwinding changes
      // the attributes of the regs object.
      std::unique_ptr<Regs> regs_copy(offline_utils_.GetRegs()->Clone());
      // The Maps object will still hold the parsed maps from the previous unwinds. So reset them
      // unless we want to assume all Maps are cached.
      if (!cache_maps) {
        if (!offline_utils_.CreateMaps(&error_msg)) {
          state.SkipWithError(error_msg.c_str());
          return;
        }
      }
      mem_tracker_.StartTrackingAllocations();
      state.ResumeTiming();

      Unwinder unwinder = Unwinder(128, offline_utils_.GetMaps(), regs_copy.get(),
                                   offline_utils_.GetProcessMemory());
      if (memory_flag == ProcessMemoryFlag::kIncludeJitMemory) {
        unwinder.SetJitDebug(offline_utils_.GetJitDebug());
      }
      unwinder.Unwind();

      state.PauseTiming();
      mem_tracker_.StopTrackingAllocations();
      if (unwinder.NumFrames() != expected_num_frames) {
        std::stringstream err_stream;
        err_stream << "Failed to unwind properly.Expected " << expected_num_frames
                   << " frames, but unwinder contained " << unwinder.NumFrames() << " frames.\n";
        state.SkipWithError(err_stream.str().c_str());
        return;
      }
      state.ResumeTiming();
    }
  }

 protected:
  MemoryTracker mem_tracker_;
  OfflineUnwindUtils offline_utils_;
};

BENCHMARK_F(OfflineUnwindBenchmark, BM_offline_straddle_arm64)(benchmark::State& state) {
  RunBenchmark(state, "straddle_arm64/", ARCH_ARM64, ProcessMemoryFlag::kNone);
}

BENCHMARK_F(OfflineUnwindBenchmark, BM_offline_straddle_arm64_cached_maps)
(benchmark::State& state) {
  RunBenchmark(state, "straddle_arm64/", ARCH_ARM64, ProcessMemoryFlag::kNone,
               /*cached_maps=*/true);
}

BENCHMARK_F(OfflineUnwindBenchmark, BM_offline_jit_debug_arm)(benchmark::State& state) {
  RunBenchmark(state, "jit_debug_arm/", ARCH_ARM, ProcessMemoryFlag::kIncludeJitMemory);
}

}  // namespace
}  // namespace unwindstack
