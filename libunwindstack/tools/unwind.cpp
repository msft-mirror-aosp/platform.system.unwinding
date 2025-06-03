/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <elf.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <procinfo/process.h>
#include <unwindstack/AndroidUnwinder.h>
#include <unwindstack/Regs.h>

static bool Attach(pid_t pid) {
  if (ptrace(PTRACE_SEIZE, pid, 0, 0) == -1) {
    return false;
  }

  if (ptrace(PTRACE_INTERRUPT, pid, 0, 0) == -1) {
    ptrace(PTRACE_DETACH, pid, 0, 0);
    return false;
  }

  // Allow at least 1 second to attach properly.
  for (size_t i = 0; i < 1000; i++) {
    siginfo_t si;
    if (ptrace(PTRACE_GETSIGINFO, pid, 0, &si) == 0) {
      return true;
    }
    usleep(1000);
  }
  printf("%d: Failed to stop.\n", pid);
  return false;
}

void DoUnwind(pid_t pid, bool print_abi = false) {
  unwindstack::Regs* regs = unwindstack::Regs::RemoteGet(pid);
  if (regs == nullptr) {
    printf("Unable to get remote reg data\n");
    return;
  }

  if (print_abi) {
    printf("ABI: ");
    switch (regs->Arch()) {
      case unwindstack::ARCH_ARM:
        printf("arm");
        break;
      case unwindstack::ARCH_X86:
        printf("x86");
        break;
      case unwindstack::ARCH_ARM64:
        printf("arm64");
        break;
      case unwindstack::ARCH_X86_64:
        printf("x86_64");
        break;
      case unwindstack::ARCH_RISCV64:
        printf("riscv64");
        break;
      default:
        printf("unknown\n");
        return;
    }
    printf("\n");
  }

  unwindstack::AndroidRemoteUnwinder unwinder(pid);
  unwindstack::AndroidUnwinderData data;
  if (!unwinder.Unwind(regs, data)) {
    printf("Unable to unwind pid %d: %s\n", pid, data.GetErrorString().c_str());
    return;
  }
  data.DemangleFunctionNames();

  // Print the frames.
  for (const auto& frame : data.frames) {
    printf("%s\n", unwinder.FormatFrame(frame).c_str());
  }
}

int main(int argc, char** argv) {
  if (argc != 2) {
    printf("Usage: unwind <PID>\n");
    return 1;
  }

  pid_t pid = atoi(argv[1]);
  if (!Attach(pid)) {
    printf("Failed to attach to pid %d: %s\n", pid, strerror(errno));
    return 1;
  }

  std::string proc(android::base::StringPrintf("/proc/%d/", pid));
  printf("Pid: %d\n", pid);
  std::string executable;
  android::base::Readlink(proc + "exe", &executable);
  if (executable.empty()) {
    executable = "Unknown";
  }
  printf("Executable: %s\n", executable.c_str());
  std::string cmdline;
  android::base::ReadFileToString(proc + "cmdline", &cmdline);
  if (cmdline.empty()) {
    cmdline = "Unknown";
  }
  printf("Command Line: %s\n", cmdline.c_str());

  DoUnwind(pid, /*print_abi*/ true);

  ptrace(PTRACE_DETACH, pid, 0, 0);

  std::vector<pid_t> tids;
  android::procinfo::GetProcessTids(pid, &tids);
  std::sort(tids.begin(), tids.end());
  for (const auto& tid : tids) {
    if (tid == pid) {
      // Main thread has already been unwound.
      continue;
    }
    if (!Attach(tid)) {
      printf("Failed to attach to pid %d: %s\n", tid, strerror(errno));
      return 1;
    }

    std::string thread_name;
    android::base::ReadFileToString(android::base::StringPrintf("/proc/%d/comm", tid),
                                    &thread_name);
    thread_name = android::base::Trim(thread_name);
    if (thread_name.empty()) {
      thread_name = "Unknown Thread";
    }
    printf("\nTid: %d Thread name: %s\n", tid, thread_name.c_str());

    DoUnwind(tid);

    ptrace(PTRACE_DETACH, tid, 0, 0);
  }

  return 0;
}
