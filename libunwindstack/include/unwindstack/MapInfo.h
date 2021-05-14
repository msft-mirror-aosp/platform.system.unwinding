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

#ifndef _LIBUNWINDSTACK_MAP_INFO_H
#define _LIBUNWINDSTACK_MAP_INFO_H

#include <stdint.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include <unwindstack/Elf.h>
#include <unwindstack/SharedString.h>

namespace unwindstack {

class MemoryFileAtOffset;

class MapInfo {
 public:
  MapInfo(MapInfo* prev_map, MapInfo* prev_real_map, uint64_t start, uint64_t end, uint64_t offset,
          uint64_t flags, const char* name)
      : start_(start),
        end_(end),
        offset_(offset),
        flags_(flags),
        name_(name),
        prev_map_(prev_map),
        prev_real_map_(prev_real_map),
        load_bias_(INT64_MAX),
        build_id_(0) {
    if (prev_real_map != nullptr) prev_real_map->next_real_map_ = this;
  }
  MapInfo(MapInfo* prev_map, MapInfo* prev_real_map, uint64_t start, uint64_t end, uint64_t offset,
          uint64_t flags, SharedString name)
      : start_(start),
        end_(end),
        offset_(offset),
        flags_(flags),
        name_(name),
        prev_map_(prev_map),
        prev_real_map_(prev_real_map),
        load_bias_(INT64_MAX),
        build_id_(0) {
    if (prev_real_map != nullptr) prev_real_map->next_real_map_ = this;
  }
  ~MapInfo();

  inline uint64_t start() const { return start_; }
  inline void set_start(uint64_t value) { start_ = value; }
  inline uint64_t end() const { return end_; }
  inline void set_end(uint64_t value) { end_ = value; }
  inline uint64_t offset() const { return offset_; }
  inline void set_offset(uint64_t value) { offset_ = value; }
  inline uint16_t flags() const { return flags_; }
  inline void set_flags(uint16_t value) { flags_ = value; }
  inline SharedString& name() { return name_; }
  inline void set_name(SharedString& value) { name_ = value; }
  inline void set_name(const char* value) { name_ = value; }
  inline std::shared_ptr<Elf>& elf() { return elf_; }
  inline void set_elf(std::shared_ptr<Elf>& value) { elf_ = value; }
  inline void set_elf(Elf* value) { elf_.reset(value); }
  inline uint64_t elf_offset() const { return elf_offset_; }
  inline void set_elf_offset(uint64_t value) { elf_offset_ = value; }
  inline uint64_t elf_start_offset() const { return elf_start_offset_; }
  inline void set_elf_start_offset(uint64_t value) { elf_start_offset_ = value; }
  inline MapInfo* prev_map() const { return prev_map_; }
  inline void set_prev_map(MapInfo* value) { prev_map_ = value; }
  inline MapInfo* prev_real_map() const { return prev_real_map_; }
  inline void set_prev_real_map(MapInfo* value) { prev_real_map_ = value; }
  inline MapInfo* next_real_map() const { return next_real_map_; }
  inline void set_next_real_map(MapInfo* value) { next_real_map_ = value; }
  inline std::atomic_int64_t& load_bias() { return load_bias_; }
  inline void set_load_bias(int64_t value) { load_bias_ = value; }
  inline std::atomic<SharedString*>& build_id() { return build_id_; }
  inline void set_build_id(SharedString* value) { build_id_ = value; }
  inline bool memory_backed_elf() const { return memory_backed_elf_; }
  inline void set_memory_backed_elf(bool value) { memory_backed_elf_ = value; }

 private:
  uint64_t start_ = 0;
  uint64_t end_ = 0;
  uint64_t offset_ = 0;
  uint16_t flags_ = 0;
  SharedString name_;
  std::shared_ptr<Elf> elf_;
  // The offset of the beginning of this mapping to the beginning of the
  // ELF file.
  // elf_offset == offset - elf_start_offset.
  // This value is only non-zero if the offset is non-zero but there is
  // no elf signature found at that offset.
  uint64_t elf_offset_ = 0;
  // This value is the offset into the file of the map in memory that is the
  // start of the elf. This is not equal to offset when the linker splits
  // shared libraries into a read-only and read-execute map.
  uint64_t elf_start_offset_ = 0;

  MapInfo* prev_map_ = nullptr;
  // This is the previous map that is not empty with a 0 offset. For
  // example, this set of maps:
  //  1000-2000  r--p 000000 00:00 0 libc.so
  //  2000-3000  ---p 000000 00:00 0 libc.so
  //  3000-4000  r-xp 003000 00:00 0 libc.so
  // The last map's prev_map would point to the 2000-3000 map, while the
  // prev_real_map would point to the 1000-2000 map.
  MapInfo* prev_real_map_ = nullptr;

  // Same as above but set to point to the next map.
  MapInfo* next_real_map_ = nullptr;

  std::atomic_int64_t load_bias_;

  // This is a pointer to a new'd std::string.
  // Using an atomic value means that we don't need to lock and will
  // make it easier to move to a fine grained lock in the future.
  std::atomic<SharedString*> build_id_;

  // Set to true if the elf file data is coming from memory.
  bool memory_backed_elf_ = false;

 public:
  // This function guarantees it will never return nullptr.
  Elf* GetElf(const std::shared_ptr<Memory>& process_memory, ArchEnum expected_arch);

  uint64_t GetLoadBias(const std::shared_ptr<Memory>& process_memory);

  Memory* CreateMemory(const std::shared_ptr<Memory>& process_memory);

  bool GetFunctionName(uint64_t addr, SharedString* name, uint64_t* func_offset);

  // Returns the raw build id read from the elf data.
  SharedString GetBuildID();

  // Used internally, and by tests. It sets the value only if it was not already set.
  SharedString SetBuildID(std::string&& new_build_id);

  // Returns the printable version of the build id (hex dump of raw data).
  std::string GetPrintableBuildID();

  inline bool IsBlank() { return offset() == 0 && flags() == 0 && name().empty(); }

 private:
  MapInfo(const MapInfo&) = delete;
  void operator=(const MapInfo&) = delete;

  Memory* GetFileMemory();
  bool InitFileMemoryFromPreviousReadOnlyMap(MemoryFileAtOffset* memory);

  // Protect the creation of the elf object.
  std::mutex mutex_;
};

}  // namespace unwindstack

#endif  // _LIBUNWINDSTACK_MAP_INFO_H
