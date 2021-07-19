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

#include <cstdio>
#define _GNU_SOURCE 1
#include <inttypes.h>
#include <stdio.h>
#include <sys/mman.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <unwindstack/Elf.h>
#include <unwindstack/JitDebug.h>
#include <unwindstack/MapInfo.h>
#include <unwindstack/Maps.h>
#include <unwindstack/Memory.h>
#include <unwindstack/Regs.h>
#include <unwindstack/Unwinder.h>
#include "utils/ProcessTracer.h"

#include <android-base/parseint.h>
#include <android-base/stringprintf.h>

namespace {
constexpr pid_t kMinPid = 1;
constexpr int kAllCmdOptionsParsed = -1;

struct map_info_t {
  uint64_t start;
  uint64_t end;
  uint64_t offset;
  uint64_t flags;
  std::string name;
};

int usage(int exit_code) {
  fprintf(stderr, "usage: unwind_for_offline [-t] <PID>\n\n");
  fprintf(stderr, "-t, --threads   dump offline snapshot for all threads of <PID>\n");
  return exit_code;
}

bool CreateAndChangeDumpDir(std::filesystem::path thread_dir, pid_t tid, bool is_main_thread) {
  std::string dir_name = std::to_string(tid);
  if (is_main_thread) dir_name += "_main-thread";
  thread_dir /= dir_name;
  if (!std::filesystem::create_directory(thread_dir)) {
    fprintf(stderr, "Failed to create directory for tid %d\n", tid);
    return false;
  }
  std::filesystem::current_path(thread_dir);
  return true;
}

bool SaveRegs(unwindstack::Regs* regs) {
  std::unique_ptr<FILE, decltype(&fclose)> fp(fopen("regs.txt", "w+"), &fclose);
  if (fp == nullptr) {
    perror("Failed to create file regs.txt");
    return false;
  }
  regs->IterateRegisters([&fp](const char* name, uint64_t value) {
    fprintf(fp.get(), "%s: %" PRIx64 "\n", name, value);
  });

  return true;
}

bool SaveStack(pid_t pid, const std::vector<std::pair<uint64_t, uint64_t>>& stacks) {
  for (size_t i = 0; i < stacks.size(); i++) {
    std::string file_name;
    if (stacks.size() != 1) {
      file_name = "stack" + std::to_string(i) + ".data";
    } else {
      file_name = "stack.data";
    }

    // Do this first, so if it fails, we don't create the file.
    uint64_t sp_start = stacks[i].first;
    uint64_t sp_end = stacks[i].second;
    std::vector<uint8_t> buffer(sp_end - sp_start);
    auto process_memory = unwindstack::Memory::CreateProcessMemory(pid);
    if (!process_memory->Read(sp_start, buffer.data(), buffer.size())) {
      printf("Unable to read stack data.\n");
      return false;
    }

    printf("Saving the stack 0x%" PRIx64 "-0x%" PRIx64 "\n", sp_start, sp_end);

    std::unique_ptr<FILE, decltype(&fclose)> fp(fopen(file_name.c_str(), "w+"), &fclose);
    if (fp == nullptr) {
      perror("Failed to create stack.data");
      return false;
    }

    size_t bytes = fwrite(&sp_start, 1, sizeof(sp_start), fp.get());
    if (bytes != sizeof(sp_start)) {
      printf("Failed to write sp_start data: sizeof(sp_start) %zu, written %zu\n", sizeof(sp_start),
             bytes);
      return false;
    }

    bytes = fwrite(buffer.data(), 1, buffer.size(), fp.get());
    if (bytes != buffer.size()) {
      printf("Failed to write all stack data: stack size %zu, written %zu\n", buffer.size(), bytes);
      return false;
    }
  }

  return true;
}

bool CreateElfFromMemory(std::shared_ptr<unwindstack::Memory>& memory, map_info_t* info) {
  std::string cur_name;
  if (info->name.empty()) {
    cur_name = android::base::StringPrintf("anonymous_%" PRIx64, info->start);
  } else {
    cur_name = android::base::StringPrintf("%s_%" PRIx64, basename(info->name.c_str()), info->start);
  }

  std::vector<uint8_t> buffer(info->end - info->start);
  // If this is a mapped in file, it might not be possible to read the entire
  // map, so read all that is readable.
  size_t bytes = memory->Read(info->start, buffer.data(), buffer.size());
  if (bytes == 0) {
    printf("Cannot read data from address %" PRIx64 " length %zu\n", info->start, buffer.size());
    return false;
  }

  std::unique_ptr<FILE, decltype(&fclose)> output(fopen(cur_name.c_str(), "w+"), &fclose);
  if (output == nullptr) {
    perror((std::string("Cannot create ") + cur_name).c_str());
    return false;
  }

  size_t bytes_written = fwrite(buffer.data(), 1, bytes, output.get());
  if (bytes_written != bytes) {
    printf("Failed to write all data to file: bytes read %zu, written %zu\n", bytes, bytes_written);
    return false;
  }

  // Replace the name with the new name.
  info->name = cur_name;

  return true;
}

bool CopyElfFromFile(map_info_t* info, bool* file_copied) {
  std::string cur_name = basename(info->name.c_str());
  if (*file_copied) {
    info->name = cur_name;
    return true;
  }

  std::unique_ptr<FILE, decltype(&fclose)> fp(fopen(info->name.c_str(), "r"), &fclose);
  if (fp == nullptr) {
    perror((std::string("Cannot open ") + info->name).c_str());
    return false;
  }

  std::unique_ptr<FILE, decltype(&fclose)> output(fopen(cur_name.c_str(), "w+"), &fclose);
  if (output == nullptr) {
    perror((std::string("Cannot create file " + cur_name)).c_str());
    return false;
  }
  std::vector<uint8_t> buffer(10000);
  size_t bytes;
  while ((bytes = fread(buffer.data(), 1, buffer.size(), fp.get())) > 0) {
    size_t bytes_written = fwrite(buffer.data(), 1, bytes, output.get());
    if (bytes_written != bytes) {
      printf("Bytes written doesn't match bytes read: read %zu, written %zu\n", bytes,
             bytes_written);
      return false;
    }
  }

  // Replace the name with the new name.
  info->name = cur_name;

  return true;
}

map_info_t* FillInAndGetMapInfo(std::unordered_map<uint64_t, map_info_t>& maps_by_start,
                                unwindstack::MapInfo* map_info) {
  auto info = &maps_by_start[map_info->start()];
  info->start = map_info->start();
  info->end = map_info->end();
  info->offset = map_info->offset();
  info->name = map_info->name();
  info->flags = map_info->flags();

  return info;
}

void SaveMapInformation(std::shared_ptr<unwindstack::Memory>& process_memory, map_info_t* info,
                        bool* file_copied) {
  if (CopyElfFromFile(info, file_copied)) {
    return;
  }
  *file_copied = false;

  // Try to create the elf from memory, this will handle cases where
  // the data only exists in memory such as vdso data on x86.
  if (CreateElfFromMemory(process_memory, info)) {
    return;
  }

  printf("Cannot save memory or file for map ");
  if (!info->name.empty()) {
    printf("%s\n", info->name.c_str());
  } else {
    printf("anonymous:%" PRIx64 "\n", info->start);
  }
}

bool SaveData(pid_t tid, const std::filesystem::path& cwd, bool is_main_thread) {
  unwindstack::Regs* regs = unwindstack::Regs::RemoteGet(tid);
  if (regs == nullptr) {
    printf("Unable to get remote reg data.\n");
    return false;
  }

  if (!CreateAndChangeDumpDir(cwd, tid, is_main_thread)) return false;

  // Save the current state of the registers.
  if (!SaveRegs(regs)) {
    return false;
  }

  // Do an unwind so we know how much of the stack to save, and what
  // elf files are involved.
  unwindstack::UnwinderFromPid unwinder(1024, tid);
  unwinder.SetRegs(regs);
  uint64_t sp = regs->sp();
  unwinder.Unwind();

  std::unordered_map<uint64_t, map_info_t> maps_by_start;
  std::vector<std::pair<uint64_t, uint64_t>> stacks;
  unwindstack::Maps* maps = unwinder.GetMaps();
  uint64_t sp_map_start = 0;
  unwindstack::MapInfo* map_info = maps->Find(sp);
  if (map_info != nullptr) {
    stacks.emplace_back(std::make_pair(sp, map_info->end()));
    sp_map_start = map_info->start();
  }

  for (const auto& frame : unwinder.frames()) {
    map_info = maps->Find(frame.sp);
    if (map_info != nullptr && sp_map_start != map_info->start()) {
      stacks.emplace_back(std::make_pair(frame.sp, map_info->end()));
      sp_map_start = map_info->start();
    }

    if (maps_by_start.count(frame.map_start) == 0) {
      map_info = maps->Find(frame.map_start);
      if (map_info == nullptr) {
        continue;
      }

      auto info = FillInAndGetMapInfo(maps_by_start, map_info);
      bool file_copied = false;
      SaveMapInformation(unwinder.GetProcessMemory(), info, &file_copied);

      // If you are using a a linker that creates two maps (one read-only, one
      // read-executable), it's necessary to capture the previous map
      // information if needed.
      unwindstack::MapInfo* prev_map = map_info->prev_map();
      if (prev_map != nullptr && map_info->offset() != 0 && prev_map->offset() == 0 &&
          prev_map->flags() == PROT_READ && map_info->name() == prev_map->name() &&
          maps_by_start.count(prev_map->start()) == 0) {
        info = FillInAndGetMapInfo(maps_by_start, prev_map);
        SaveMapInformation(unwinder.GetProcessMemory(), info, &file_copied);
      }
    }
  }

  for (size_t i = 0; i < unwinder.NumFrames(); i++) {
    printf("%s\n", unwinder.FormatFrame(i).c_str());
  }

  if (!SaveStack(tid, stacks)) {
    return false;
  }

  std::vector<std::pair<uint64_t, map_info_t>> sorted_maps(maps_by_start.begin(),
                                                           maps_by_start.end());
  std::sort(sorted_maps.begin(), sorted_maps.end(),
            [](auto& a, auto& b) { return a.first < b.first; });

  std::unique_ptr<FILE, decltype(&fclose)> fp(fopen("maps.txt", "w+"), &fclose);
  if (fp == nullptr) {
    perror("Failed to create maps.txt");
    return false;
  }

  for (auto& element : sorted_maps) {
    char perms[5] = {"---p"};
    map_info_t& map = element.second;
    if (map.flags & PROT_READ) {
      perms[0] = 'r';
    }
    if (map.flags & PROT_WRITE) {
      perms[1] = 'w';
    }
    if (map.flags & PROT_EXEC) {
      perms[2] = 'x';
    }
    fprintf(fp.get(), "%" PRIx64 "-%" PRIx64 " %s %" PRIx64 " 00:00 0", map.start, map.end, perms,
            map.offset);
    if (!map.name.empty()) {
      fprintf(fp.get(), "   %s", map.name.c_str());
    }
    fprintf(fp.get(), "\n");
  }

  return true;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage(EXIT_FAILURE);

  int opt;
  bool dump_threads = false;
  while ((opt = getopt(argc, argv, "t")) != kAllCmdOptionsParsed) {
    switch (opt) {
      case 't': {
        dump_threads = true;
        break;
      }
      case '?': {
        if (isprint(optopt))
          fprintf(stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
        return usage(EXIT_FAILURE);
      }
      default: {
        return usage(EXIT_FAILURE);
      }
    }
  }
  if (optind != argc - 1) return usage(EXIT_FAILURE);

  pid_t pid;
  if (!android::base::ParseInt(argv[optind], &pid, kMinPid, std::numeric_limits<pid_t>::max()))
    return usage(EXIT_FAILURE);

  unwindstack::ProcessTracer proc(pid, dump_threads);
  if (!proc.Stop()) return EXIT_FAILURE;
  std::filesystem::path cwd = std::filesystem::current_path();

  if (!proc.Attach(proc.pid())) return EXIT_FAILURE;
  if (!SaveData(proc.pid(), cwd, /*is_main_thread=*/proc.IsTracingThreads())) return EXIT_FAILURE;
  if (!proc.Detach(proc.pid())) return EXIT_FAILURE;
  for (const pid_t& tid : proc.tids()) {
    if (!proc.Attach(tid)) return EXIT_FAILURE;
    if (!SaveData(tid, cwd, /*is_main_thread=*/false)) return EXIT_FAILURE;
    if (!proc.Detach(tid)) return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
