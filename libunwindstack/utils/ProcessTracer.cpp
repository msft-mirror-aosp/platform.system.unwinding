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

#include <errno.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include <procinfo/process.h>

#include "ProcessTracer.h"

namespace unwindstack {

ProcessTracer::ProcessTracer(pid_t pid, bool is_tracing_threads)
    : pid_(pid), is_tracing_threads_(is_tracing_threads) {
  if (is_tracing_threads_) is_tracing_threads_ = InitProcessTids();
}

bool ProcessTracer::InitProcessTids() {
  std::string error_msg;
  if (!android::procinfo::GetProcessTids(pid_, &tids_, &error_msg)) {
    fprintf(stderr,
            "Failed to get process tids: %s. Reverting to tracing the "
            "main thread only.\n",
            error_msg.c_str());
    return false;
  }
  if (tids_.erase(pid_) != 1) {
    fprintf(stderr,
            "Failed to erase the main thread from the thread id set. "
            "Reverting to tracing the main thread only.\n");
    return false;
  }
  return true;
}

ProcessTracer::~ProcessTracer() {
  if (cur_attached_tid_ != kNoThreadAttached) Detach(cur_attached_tid_);
  if (!is_running_) Resume();
}

bool ProcessTracer::Stop() {
  if (kill(pid_, SIGSTOP) == kKillFailed) {
    fprintf(stderr, "Failed to send stop signal to pid %d: %s\n", pid_, strerror(errno));
    return false;
  }
  is_running_ = false;
  return true;
}

bool ProcessTracer::Resume() {
  if (kill(pid_, SIGCONT) == kKillFailed) {
    fprintf(stderr, "Failed to send continue signal to pid %d: %s\n", pid_, strerror(errno));
    return false;
  }

  is_running_ = true;
  return true;
}

bool ProcessTracer::Detach(pid_t tid) {
  if (tid != pid_ && tids_.find(tid) == tids_.end()) {
    fprintf(stderr, "Tid %d does not belong to proc %d.\n", tid, pid_);
    return false;
  }

  if (cur_attached_tid_ == kNoThreadAttached) {
    fprintf(stderr, "Cannot detach because no thread is currently attached.\n");
    return false;
  }
  if (is_running_ && !Stop()) return false;

  if (ptrace(PTRACE_DETACH, tid, nullptr, nullptr) == kPtraceFailed) {
    fprintf(stderr, "Failed to detach from tid %d: %s\n", tid, strerror(errno));
    return false;
  }

  cur_attached_tid_ = kNoThreadAttached;
  return true;
}

bool ProcessTracer::Attach(pid_t tid) {
  if (tid != pid_ && tids_.find(tid) == tids_.end()) {
    fprintf(stderr, "Tid %d does not belong to proc %d.\n", tid, pid_);
    return false;
  }

  if (is_running_) Stop();
  if (cur_attached_tid_ != kNoThreadAttached) {
    fprintf(stderr, "Cannot attatch to tid %d. Already attached to tid %d.\n", tid,
            cur_attached_tid_);
    return true;
  }

  if (ptrace(PTRACE_ATTACH, tid, nullptr, nullptr) == kPtraceFailed) {
    fprintf(stderr, "Failed to attached to tid %d: %s\n", tid, strerror(errno));
    return false;
  }
  int status;
  if (waitpid(tid, &status, 0) == kWaitpidFailed) {
    fprintf(stderr, "Failed to stop tid %d: %s\n", tid, strerror(errno));
    return false;
  }

  cur_attached_tid_ = tid;
  return true;
}
}  // namespace unwindstack