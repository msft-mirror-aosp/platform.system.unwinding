/*
 * Copyright (C) 2019 The Android Open Source Project
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

#define _GNU_SOURCE 1
#include <stdint.h>
#include <string.h>

#include <string>

#if defined(__BIONIC__)

#include <gtest/gtest.h>

#include <unwindstack/AndroidUnwinder.h>
#include <unwindstack/DwarfSection.h>
#include <unwindstack/Elf.h>
#include <unwindstack/ElfInterface.h>

#include "ForkTest.h"

// This test is specific to bionic to verify that __libc_init is
// properly setting the return address to undefined so that the
// unwind properly terminates.

namespace unwindstack {

using VerifyBionicTermination = ForkTest;

static std::string DumpFrames(const AndroidUnwinderData& data, AndroidUnwinder& unwinder) {
  // Init this way so that the first frame of the backtrace starts on a new line.
  std::string unwind("\n");
  for (auto& frame : data.frames) {
    unwind += unwinder.FormatFrame(frame) + '\n';
  }
  return unwind;
}

static bool ReturnAddressLocationIsUndefined(ArchEnum arch, DwarfSection* section,
                                             uint64_t rel_pc) {
  if (section == nullptr) {
    return false;
  }

  const DwarfFde* fde = section->GetFdeFromPc(rel_pc);
  if (fde == nullptr || fde->cie == nullptr) {
    return false;
  }
  DwarfLocations regs;
  if (!section->GetCfaLocationInfo(rel_pc, fde, &regs, arch)) {
    return false;
  }

  auto reg_entry = regs.find(fde->cie->return_address_register);
  if (reg_entry == regs.end()) {
    return false;
  }
  return reg_entry->second.type == DWARF_LOCATION_UNDEFINED;
}

static void VerifyReturnAddress(const FrameData& frame) {
  Elf* elf = frame.map_info->GetElfObj();
  ASSERT_NE(nullptr, elf) << "No elf object in map info frame";
  ASSERT_TRUE(elf->valid()) << "No valid elf object in map info for frame";
  ElfInterface* interface = elf->interface();
  ASSERT_NE(nullptr, interface) << "Cannot find elf interface in elf object";

  // The undefined register comes from a cfi directive set in __libc__init
  // using the BIONIC_STOP_UNWIND macro.
  // Look for this definition in these DwarfSections in this order:
  //   debug_frame
  //   eh_frame
  //   gnu_debugdata debug_frame
  //   gnu_debugdata eh_frame
  // Always check debug_frame first since it usually conatins the most
  // specific data.
  if (ReturnAddressLocationIsUndefined(elf->arch(), interface->debug_frame(), frame.rel_pc)) {
    return;
  }
  if (ReturnAddressLocationIsUndefined(elf->arch(), interface->eh_frame(), frame.rel_pc)) {
    return;
  }

  ElfInterface* gnu_debugdata = elf->gnu_debugdata_interface();
  ASSERT_TRUE(gnu_debugdata != nullptr)
      << "Could not find undefined return register in debug_frame or eh_frame";
  if (ReturnAddressLocationIsUndefined(elf->arch(), gnu_debugdata->debug_frame(), frame.rel_pc)) {
    return;
  }
  if (ReturnAddressLocationIsUndefined(elf->arch(), gnu_debugdata->eh_frame(), frame.rel_pc)) {
    return;
  }
  FAIL() << "Could not find undefined return register in debug_frame, eh_frame, gnu_debugdata "
            "debug_frame or gnu_debugdata eh_frame";
}

// This assumes that the function starts from the main thread, and that the
// libc.so on device will include symbols so that function names can
// be resolved.
static void VerifyLibcInitTerminate(AndroidUnwinder& unwinder) {
  AndroidUnwinderData data;
  ASSERT_TRUE(unwinder.Unwind(data));

  SCOPED_TRACE(DumpFrames(data, unwinder));

  // Look for the frame that includes __libc_init, there should only
  // be one and it should be the last.
  bool found = false;
  const std::vector<FrameData>& frames = data.frames;
  for (size_t i = 0; i < frames.size(); i++) {
    const FrameData& frame = frames[i];
    if (frame.function_name == "__libc_init" && frame.map_info != nullptr &&
        !frame.map_info->name().empty() &&
        std::string("libc.so") == basename(frame.map_info->name().c_str())) {
      ASSERT_EQ(frames.size(), i + 1) << "__libc_init is not last frame.";
      ASSERT_NO_FATAL_FAILURE(VerifyReturnAddress(frame));
      found = true;
    }
  }
  ASSERT_TRUE(found) << "Unable to find libc.so:__libc_init frame\n";
}

TEST_F(VerifyBionicTermination, local_terminate) {
  AndroidLocalUnwinder unwinder;
  VerifyLibcInitTerminate(unwinder);
}

TEST_F(VerifyBionicTermination, remote_terminate) {
  ASSERT_NO_FATAL_FAILURE(Fork());

  AndroidRemoteUnwinder unwinder(pid_);
  VerifyLibcInitTerminate(unwinder);
}

}  // namespace unwindstack

#endif
