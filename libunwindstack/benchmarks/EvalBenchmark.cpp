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

#include <unwindstack/DwarfSection.h>
#include <cstdint>
#include <memory>

#include <benchmark/benchmark.h>

#include "Utils.h"
#include "utils/DwarfSectionImplFake.h"
#include "utils/MemoryFake.h"
#include "utils/RegsFake.h"

namespace unwindstack {

template <typename AddresssType>
class EvalBenchmark : public benchmark::Fixture {
 public:
  EvalBenchmark() {
    memory_.Clear();
    section_ = std::make_unique<DwarfSectionImplFake<AddresssType>>(&memory_);
  }

  void TearDown(benchmark::State& state) override { mem_tracker_.SetBenchmarkCounters(state); }

 protected:
  MemoryFake memory_;
  std::unique_ptr<DwarfSectionImplFake<AddresssType>> section_;
  MemoryTracker mem_tracker_;
};

BENCHMARK_TEMPLATE_F(EvalBenchmark, BM_eval_location_few_regs, uint64_t)(benchmark::State& state) {
  DwarfCie cie{.return_address_register = 5};
  RegsImplFake<uint64_t> regs(10);
  DwarfLocations loc_regs;

  regs.set_pc(0x100);
  regs.set_sp(0x2000);
  regs[5] = 0x20;
  regs[9] = 0x3000;
  loc_regs[CFA_REG] = DwarfLocation{DWARF_LOCATION_REGISTER, {9, 0}};
  bool finished;
  mem_tracker_.StartTrackingAllocations();
  for (auto _ : state) {
    if (!section_->Eval(&cie, &this->memory_, loc_regs, &regs, &finished)) {
      state.SkipWithError("BM_eval_two_regs: Eval() failed.");
      return;
    }
  }
  mem_tracker_.StopTrackingAllocations();
  if (finished || regs.pc() != 0x20U || regs.sp() != 0x3000U) {
    state.SkipWithError(
        "BM_eval_two_regs: Eval() finished successfully but registers were not evaluated "
        "correctly.");
  }
}

BENCHMARK_TEMPLATE_F(EvalBenchmark, BM_eval_location_many_regs, uint64_t)(benchmark::State& state) {
  RegsImplFake<uint64_t> regs(0);
  bool finished;
  for (auto _ : state) {
    state.PauseTiming();
    DwarfCie cie{.return_address_register = 5};
    regs = RegsImplFake<uint64_t>(64);
    DwarfLocations loc_regs;

    regs.set_pc(0x100);
    regs.set_sp(0x2000);
    regs[63] = 0x3000;
    loc_regs[CFA_REG] = DwarfLocation{DWARF_LOCATION_REGISTER, {63, 0}};
    for (uint64_t i = 0; i < 63; i++) {
      regs[i] = 10 * i;
      loc_regs[i] = DwarfLocation{DWARF_LOCATION_REGISTER, {static_cast<uint32_t>(i), 5}};
    }

    mem_tracker_.StartTrackingAllocations();
    state.ResumeTiming();

    if (!section_->Eval(&cie, &this->memory_, loc_regs, &regs, &finished)) {
      state.SkipWithError("BM_eval_all_regs: Eval() failed.");
      return;
    }
    state.PauseTiming();
    mem_tracker_.StopTrackingAllocations();
    state.ResumeTiming();
  }
  if (finished || regs.pc() != 55U || regs.sp() != 0x3000U) {
    state.SkipWithError(
        "BM_eval_two_regs: Eval() finished successfully but registers were not evaluated "
        "correctly.");
  }
}

}  // namespace unwindstack
