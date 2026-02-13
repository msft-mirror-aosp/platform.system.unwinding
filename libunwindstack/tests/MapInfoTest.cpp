/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <thread>

#include <gtest/gtest.h>

#include <unwindstack/MapInfo.h>
#include <unwindstack/Maps.h>

#include "ElfFake.h"

namespace unwindstack {

TEST(MapInfoTest, maps_constructor_const_char) {
  auto prev_map = MapInfo::Create(0, 0, 0, 0, "");
  auto map_info = MapInfo::Create(prev_map, 1, 2, 3, 4, "map");

  EXPECT_TRUE(prev_map->use_global_elf_cache());
  EXPECT_EQ(prev_map.get(), map_info->prev_map().get());
  EXPECT_EQ(1UL, map_info->start());
  EXPECT_EQ(2UL, map_info->end());
  EXPECT_EQ(3UL, map_info->offset());
  EXPECT_EQ(4UL, map_info->flags());
  EXPECT_EQ("map", map_info->name());
  EXPECT_EQ(UINT64_MAX, map_info->load_bias());
  EXPECT_EQ(0UL, map_info->elf_offset());
  EXPECT_TRUE(map_info->elf().get() == nullptr);
  EXPECT_TRUE(map_info->use_global_elf_cache());
}

TEST(MapInfoTest, maps_constructor_use_global_cache_settings) {
  auto prev_map = MapInfo::Create(0, 0, 0, 0, "", false);
  auto map_info = MapInfo::Create(prev_map, 1, 2, 3, 4, "map", false);

  EXPECT_FALSE(prev_map->use_global_elf_cache());
  EXPECT_FALSE(map_info->use_global_elf_cache());

  map_info.reset(new MapInfo(0, 0, 0, 0, ""));
  EXPECT_TRUE(map_info->use_global_elf_cache());

  map_info.reset(new MapInfo(0, 0, 0, 0, "", false));
  EXPECT_FALSE(map_info->use_global_elf_cache());

  map_info.reset(new MapInfo(prev_map, 0, 0, 0, 0, ""));
  EXPECT_TRUE(map_info->use_global_elf_cache());

  map_info.reset(new MapInfo(prev_map, 0, 0, 0, 0, "", false));
  EXPECT_FALSE(map_info->use_global_elf_cache());
}

TEST(MapInfoTest, maps_constructor_string) {
  std::string name("string_map");
  auto prev_map = MapInfo::Create(0, 0, 0, 0, "");
  auto map_info = MapInfo::Create(prev_map, 1, 2, 3, 4, name);

  EXPECT_TRUE(prev_map->use_global_elf_cache());
  EXPECT_EQ(prev_map, map_info->prev_map());
  EXPECT_EQ(1UL, map_info->start());
  EXPECT_EQ(2UL, map_info->end());
  EXPECT_EQ(3UL, map_info->offset());
  EXPECT_EQ(4UL, map_info->flags());
  EXPECT_EQ("string_map", map_info->name());
  EXPECT_EQ(UINT64_MAX, map_info->load_bias());
  EXPECT_EQ(0UL, map_info->elf_offset());
  EXPECT_TRUE(map_info->elf().get() == nullptr);
  EXPECT_TRUE(map_info->use_global_elf_cache());
}

TEST(MapInfoTest, real_map_check) {
  auto map1 = MapInfo::Create(0, 0x1000, 0, PROT_READ, "fake.so");
  auto map2 = MapInfo::Create(map1, 0, 0, 0, 0, "");
  auto map3 = MapInfo::Create(map2, 0x1000, 0x2000, 0x1000, PROT_READ | PROT_EXEC, "fake.so");

  EXPECT_EQ(nullptr, map1->prev_map());
  EXPECT_EQ(nullptr, map1->GetPrevRealMap());
  EXPECT_EQ(map2, map1->next_map());
  EXPECT_EQ(map3, map1->GetNextRealMap());

  EXPECT_EQ(map1, map2->prev_map());
  EXPECT_EQ(nullptr, map2->GetPrevRealMap());
  EXPECT_EQ(map3, map2->next_map());
  EXPECT_EQ(nullptr, map2->GetNextRealMap());

  EXPECT_EQ(map2, map3->prev_map());
  EXPECT_EQ(map1, map3->GetPrevRealMap());
  EXPECT_EQ(nullptr, map3->next_map());
  EXPECT_EQ(nullptr, map3->GetNextRealMap());

  // Verify that if the middle map is not blank, then the Get{Next,Prev}RealMap
  // functions return nullptrs.
  map2->set_offset(1);
  EXPECT_EQ(nullptr, map1->GetPrevRealMap());
  EXPECT_EQ(nullptr, map1->GetNextRealMap());
  EXPECT_EQ(nullptr, map3->GetPrevRealMap());
  EXPECT_EQ(nullptr, map3->GetNextRealMap());
  map2->set_offset(0);
  EXPECT_EQ(map3, map1->GetNextRealMap());

  map2->set_flags(1);
  EXPECT_EQ(nullptr, map1->GetPrevRealMap());
  EXPECT_EQ(nullptr, map1->GetNextRealMap());
  EXPECT_EQ(nullptr, map3->GetPrevRealMap());
  EXPECT_EQ(nullptr, map3->GetNextRealMap());
  map2->set_flags(0);
  EXPECT_EQ(map3, map1->GetNextRealMap());

  map2->set_name("something");
  EXPECT_EQ(nullptr, map1->GetPrevRealMap());
  EXPECT_EQ(nullptr, map1->GetNextRealMap());
  EXPECT_EQ(nullptr, map3->GetPrevRealMap());
  EXPECT_EQ(nullptr, map3->GetNextRealMap());
  map2->set_name("");
  EXPECT_EQ(map3, map1->GetNextRealMap());

  // Verify if the map has the name [page size compat] it's still considered blank.
  map2->set_name("[page size compat]");
  EXPECT_EQ(nullptr, map1->GetPrevRealMap());
  EXPECT_EQ(map3, map1->GetNextRealMap());
  EXPECT_EQ(map1, map3->GetPrevRealMap());
  EXPECT_EQ(nullptr, map3->GetNextRealMap());
  map2->set_name("");
  EXPECT_EQ(map3, map1->GetNextRealMap());

  // Verify that if the Get{Next,Prev}RealMap names must match.
  map1->set_name("another");
  EXPECT_EQ(nullptr, map1->GetPrevRealMap());
  EXPECT_EQ(nullptr, map1->GetNextRealMap());
  EXPECT_EQ(nullptr, map3->GetPrevRealMap());
  EXPECT_EQ(nullptr, map3->GetNextRealMap());
  map1->set_name("fake.so");
  EXPECT_EQ(map3, map1->GetNextRealMap());

  map3->set_name("another");
  EXPECT_EQ(nullptr, map1->GetPrevRealMap());
  EXPECT_EQ(nullptr, map1->GetNextRealMap());
  EXPECT_EQ(nullptr, map3->GetPrevRealMap());
  EXPECT_EQ(nullptr, map3->GetNextRealMap());
  map3->set_name("fake.so");
  EXPECT_EQ(map3, map1->GetNextRealMap());
}

TEST(MapInfoTest, read_only_map_check) {
  // Many maps between read-only and execute map, read-only map zero offset.
  auto map1 = MapInfo::Create(0x2000, 0x3000, 0, PROT_READ, "lib1.so");
  auto map2 = MapInfo::Create(map1, 0x3000, 0x4000, 0, PROT_READ, "[anon:nothing]");
  auto map3 = MapInfo::Create(map2, 0x4000, 0x5000, 0, 0, "[anon:nothing]");
  auto map4 = MapInfo::Create(map3, 0x5000, 0x6000, 0, PROT_READ | PROT_WRITE, "");
  auto map5 = MapInfo::Create(map4, 0x6000, 0x7000, 0x4000, PROT_READ | PROT_EXEC, "lib1.so");
  // Maps between read-only and execute map, read-only map non-zero offset.
  auto map6 = MapInfo::Create(map5, 0x8000, 0x9000, 0x4000, PROT_READ, "lib2.so");
  auto map7 = MapInfo::Create(map6, 0x9000, 0xa000, 0, 0, "[anon:nothing]");
  auto map8 = MapInfo::Create(map7, 0xa000, 0xb000, 0x6000, PROT_READ | PROT_EXEC, "lib2.so");
  // The read-only map is < start - offset of executable map, so should not be found.
  auto map9 = MapInfo::Create(map8, 0x10000, 0x11000, 0, PROT_READ, "lib3.so");
  auto map10 = MapInfo::Create(map9, 0x12000, 0x13000, 0x1000, PROT_READ | PROT_EXEC, "lib3.so");

  EXPECT_EQ(map1, map5->GetPrevReadOnlyMap());

  EXPECT_EQ(map6, map8->GetPrevReadOnlyMap());

  EXPECT_EQ(nullptr, map10->GetPrevReadOnlyMap());

  // Check that all other calls return nullptr.
  EXPECT_TRUE(map1->GetPrevReadOnlyMap() == nullptr);
  EXPECT_TRUE(map2->GetPrevReadOnlyMap() == nullptr);
  EXPECT_TRUE(map3->GetPrevReadOnlyMap() == nullptr);
  EXPECT_TRUE(map4->GetPrevReadOnlyMap() == nullptr);
  EXPECT_TRUE(map6->GetPrevReadOnlyMap() == nullptr);
  EXPECT_TRUE(map7->GetPrevReadOnlyMap() == nullptr);
  EXPECT_TRUE(map9->GetPrevReadOnlyMap() == nullptr);
}

TEST(MapInfoTest, get_function_name) {
  std::shared_ptr<Memory> empty;
  ElfFake* elf = new ElfFake(empty);
  ElfInterfaceFake* interface = new ElfInterfaceFake(empty);
  elf->FakeSetInterface(interface);
  interface->FakePushFunctionData(FunctionData("function", 1000));

  auto map_info = MapInfo::Create(1, 2, 3, 4, "");
  map_info->set_elf(elf);

  SharedString name;
  uint64_t offset;
  ASSERT_TRUE(map_info->GetFunctionName(1000, &name, &offset));
  EXPECT_EQ("function", name);
  EXPECT_EQ(1000UL, offset);
}

TEST(MapInfoTest, multiple_thread_get_elf_fields) {
  auto map_info = MapInfo::Create(0, 0, 0, 0, "");

  static constexpr size_t kNumConcurrentThreads = 100;
  MapInfo::ElfFields* elf_fields[kNumConcurrentThreads];

  std::atomic_bool wait;
  wait = true;
  // Create all of the threads and have them do the call at the same time
  // to make it likely that a race will occur.
  std::vector<std::thread*> threads;
  for (size_t i = 0; i < kNumConcurrentThreads; i++) {
    std::thread* thread = new std::thread([i, &wait, &map_info, &elf_fields]() {
      while (wait)
        ;
      elf_fields[i] = &map_info->GetElfFields();
    });
    threads.push_back(thread);
  }

  // Set them all going and wait for the threads to finish.
  wait = false;
  for (auto thread : threads) {
    thread->join();
    delete thread;
  }

  // Now verify that all of elf fields are exactly the same and valid.
  MapInfo::ElfFields* expected_elf_fields = &map_info->GetElfFields();
  ASSERT_TRUE(expected_elf_fields != nullptr);
  for (size_t i = 0; i < kNumConcurrentThreads; i++) {
    EXPECT_EQ(expected_elf_fields, elf_fields[i]) << "Thread " << i << " mismatched.";
  }
}

TEST(MapInfoTest, elf_file_not_readable) {
  auto map_info_readable = MapInfo::Create(0, 0x1000, 0, PROT_READ, "fake.so");
  map_info_readable->set_memory_backed_elf(true);
  ASSERT_TRUE(map_info_readable->ElfFileNotReadable());

  auto map_info_no_name = MapInfo::Create(0, 0x1000, 0, PROT_READ, "");
  map_info_no_name->set_memory_backed_elf(true);
  ASSERT_FALSE(map_info_no_name->ElfFileNotReadable());

  auto map_info_bracket = MapInfo::Create(0, 0x2000, 0, PROT_READ, "[vdso]");
  map_info_bracket->set_memory_backed_elf(true);
  ASSERT_FALSE(map_info_bracket->ElfFileNotReadable());

  auto map_info_memfd = MapInfo::Create(0, 0x3000, 0, PROT_READ, "/memfd:jit-cache");
  map_info_memfd->set_memory_backed_elf(true);
  ASSERT_FALSE(map_info_memfd->ElfFileNotReadable());
}

}  // namespace unwindstack
