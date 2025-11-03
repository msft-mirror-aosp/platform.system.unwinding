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

#include <stdint.h>

#include <memory>

#include <android-base/silent_death_test.h>
#include <gtest/gtest.h>

#include <unwindstack/Elf.h>
#include <unwindstack/ElfInterface.h>
#include <unwindstack/MachineArm.h>
#include <unwindstack/MachineArm64.h>
#include <unwindstack/MachineRiscv64.h>
#include <unwindstack/MachineX86.h>
#include <unwindstack/MachineX86_64.h>
#include <unwindstack/MapInfo.h>
#include <unwindstack/RegsArm.h>
#include <unwindstack/RegsArm64.h>
#include <unwindstack/RegsRiscv64.h>
#include <unwindstack/RegsX86.h>
#include <unwindstack/RegsX86_64.h>
#include <unwindstack/UcontextArm.h>
#include <unwindstack/UcontextArm64.h>
#include <unwindstack/UcontextX86.h>
#include <unwindstack/UcontextX86_64.h>

#include "ElfFake.h"
#include "RegsFake.h"
#include "utils/MemoryFake.h"

namespace unwindstack {

class RegsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake_memory_ = new MemoryFake;
    std::shared_ptr<Memory> memory(fake_memory_);
    elf_.reset(new ElfFake(memory));
    elf_interface_ = new ElfInterfaceFake(memory);
    elf_->FakeSetInterface(elf_interface_);
  }

  ElfInterfaceFake* elf_interface_;
  MemoryFake* fake_memory_;
  std::unique_ptr<ElfFake> elf_;
};

TEST_F(RegsTest, regs32) {
  RegsImplFake<uint32_t> regs32(50);
  ASSERT_EQ(50U, regs32.total_regs());

  uint32_t* raw = reinterpret_cast<uint32_t*>(regs32.RawData());
  for (size_t i = 0; i < 50; i++) {
    raw[i] = 0xf0000000 + i;
  }
  regs32.set_pc(0xf0120340);
  regs32.set_sp(0xa0ab0cd0);

  for (size_t i = 0; i < 50; i++) {
    ASSERT_EQ(0xf0000000U + i, regs32[i]) << "Failed comparing register " << i;
  }

  ASSERT_EQ(0xf0120340U, regs32.pc());
  ASSERT_EQ(0xa0ab0cd0U, regs32.sp());

  regs32[32] = 10;
  ASSERT_EQ(10U, regs32[32]);
}

TEST_F(RegsTest, regs64) {
  RegsImplFake<uint64_t> regs64(30);
  ASSERT_EQ(30U, regs64.total_regs());

  uint64_t* raw = reinterpret_cast<uint64_t*>(regs64.RawData());
  for (size_t i = 0; i < 30; i++) {
    raw[i] = 0xf123456780000000UL + i;
  }
  regs64.set_pc(0xf123456780102030UL);
  regs64.set_sp(0xa123456780a0b0c0UL);

  for (size_t i = 0; i < 30; i++) {
    ASSERT_EQ(0xf123456780000000U + i, regs64[i]) << "Failed reading register " << i;
  }

  ASSERT_EQ(0xf123456780102030UL, regs64.pc());
  ASSERT_EQ(0xa123456780a0b0c0UL, regs64.sp());

  regs64[8] = 10;
  ASSERT_EQ(10U, regs64[8]);
}

TEST_F(RegsTest, rel_pc) {
  EXPECT_EQ(4U, GetPcAdjustment(0x10, elf_.get(), ARCH_ARM64));
  EXPECT_EQ(4U, GetPcAdjustment(0x4, elf_.get(), ARCH_ARM64));
  EXPECT_EQ(0U, GetPcAdjustment(0x3, elf_.get(), ARCH_ARM64));
  EXPECT_EQ(0U, GetPcAdjustment(0x2, elf_.get(), ARCH_ARM64));
  EXPECT_EQ(0U, GetPcAdjustment(0x1, elf_.get(), ARCH_ARM64));
  EXPECT_EQ(0U, GetPcAdjustment(0x0, elf_.get(), ARCH_ARM64));

  EXPECT_EQ(4U, GetPcAdjustment(0x10, elf_.get(), ARCH_RISCV64));
  EXPECT_EQ(4U, GetPcAdjustment(0x4, elf_.get(), ARCH_RISCV64));
  EXPECT_EQ(0U, GetPcAdjustment(0x3, elf_.get(), ARCH_RISCV64));
  EXPECT_EQ(0U, GetPcAdjustment(0x2, elf_.get(), ARCH_RISCV64));
  EXPECT_EQ(0U, GetPcAdjustment(0x1, elf_.get(), ARCH_RISCV64));
  EXPECT_EQ(0U, GetPcAdjustment(0x0, elf_.get(), ARCH_RISCV64));

  EXPECT_EQ(1U, GetPcAdjustment(0x100, elf_.get(), ARCH_X86));
  EXPECT_EQ(1U, GetPcAdjustment(0x2, elf_.get(), ARCH_X86));
  EXPECT_EQ(1U, GetPcAdjustment(0x1, elf_.get(), ARCH_X86));
  EXPECT_EQ(0U, GetPcAdjustment(0x0, elf_.get(), ARCH_X86));

  EXPECT_EQ(1U, GetPcAdjustment(0x100, elf_.get(), ARCH_X86_64));
  EXPECT_EQ(1U, GetPcAdjustment(0x2, elf_.get(), ARCH_X86_64));
  EXPECT_EQ(1U, GetPcAdjustment(0x1, elf_.get(), ARCH_X86_64));
  EXPECT_EQ(0U, GetPcAdjustment(0x0, elf_.get(), ARCH_X86_64));
}

TEST_F(RegsTest, rel_pc_arm) {
  // Check fence posts.
  elf_->FakeSetLoadBias(0);
  EXPECT_EQ(2U, GetPcAdjustment(0x5, elf_.get(), ARCH_ARM));
  EXPECT_EQ(2U, GetPcAdjustment(0x4, elf_.get(), ARCH_ARM));
  EXPECT_EQ(2U, GetPcAdjustment(0x3, elf_.get(), ARCH_ARM));
  EXPECT_EQ(2U, GetPcAdjustment(0x2, elf_.get(), ARCH_ARM));
  EXPECT_EQ(0U, GetPcAdjustment(0x1, elf_.get(), ARCH_ARM));
  EXPECT_EQ(0U, GetPcAdjustment(0x0, elf_.get(), ARCH_ARM));

  elf_->FakeSetLoadBias(0x100);
  EXPECT_EQ(0U, GetPcAdjustment(0x1, elf_.get(), ARCH_ARM));
  EXPECT_EQ(2U, GetPcAdjustment(0x2, elf_.get(), ARCH_ARM));
  EXPECT_EQ(2U, GetPcAdjustment(0xff, elf_.get(), ARCH_ARM));
  EXPECT_EQ(2U, GetPcAdjustment(0x105, elf_.get(), ARCH_ARM));
  EXPECT_EQ(2U, GetPcAdjustment(0x104, elf_.get(), ARCH_ARM));
  EXPECT_EQ(2U, GetPcAdjustment(0x103, elf_.get(), ARCH_ARM));
  EXPECT_EQ(2U, GetPcAdjustment(0x102, elf_.get(), ARCH_ARM));
  EXPECT_EQ(0U, GetPcAdjustment(0x101, elf_.get(), ARCH_ARM));
  EXPECT_EQ(0U, GetPcAdjustment(0x100, elf_.get(), ARCH_ARM));

  // Check thumb instructions handling.
  elf_->FakeSetLoadBias(0);
  fake_memory_->SetData32(0x2000, 0);
  EXPECT_EQ(2U, GetPcAdjustment(0x2005, elf_.get(), ARCH_ARM));
  fake_memory_->SetData32(0x2000, 0xe000f000);
  EXPECT_EQ(4U, GetPcAdjustment(0x2005, elf_.get(), ARCH_ARM));

  elf_->FakeSetLoadBias(0x400);
  fake_memory_->SetData32(0x2100, 0);
  EXPECT_EQ(2U, GetPcAdjustment(0x2505, elf_.get(), ARCH_ARM));
  fake_memory_->SetData32(0x2100, 0xf111f111);
  EXPECT_EQ(4U, GetPcAdjustment(0x2505, elf_.get(), ARCH_ARM));
}

TEST_F(RegsTest, elf_invalid) {
  auto map_info = MapInfo::Create(0x1000, 0x2000, 0, 0, "");
  std::shared_ptr<Memory> empty;
  Elf* invalid_elf = new Elf(empty);
  map_info->set_elf(invalid_elf);

  EXPECT_EQ(0x500U, invalid_elf->GetRelPc(0x1500, map_info.get()));
  EXPECT_EQ(2U, GetPcAdjustment(0x500U, invalid_elf, ARCH_ARM));
  EXPECT_EQ(2U, GetPcAdjustment(0x511U, invalid_elf, ARCH_ARM));

  EXPECT_EQ(0x600U, invalid_elf->GetRelPc(0x1600, map_info.get()));
  EXPECT_EQ(4U, GetPcAdjustment(0x600U, invalid_elf, ARCH_ARM64));

  EXPECT_EQ(0x600U, invalid_elf->GetRelPc(0x1600, map_info.get()));
  EXPECT_EQ(4U, GetPcAdjustment(0x600U, invalid_elf, ARCH_RISCV64));

  EXPECT_EQ(0x700U, invalid_elf->GetRelPc(0x1700, map_info.get()));
  EXPECT_EQ(1U, GetPcAdjustment(0x700U, invalid_elf, ARCH_X86));

  EXPECT_EQ(0x800U, invalid_elf->GetRelPc(0x1800, map_info.get()));
  EXPECT_EQ(1U, GetPcAdjustment(0x800U, invalid_elf, ARCH_X86_64));
}

TEST_F(RegsTest, regs_convert) {
  RegsArm arm;
  EXPECT_EQ(0, arm.Convert(0));
  EXPECT_EQ(ARM_REG_LAST - 1, arm.Convert(ARM_REG_LAST - 1));
  EXPECT_EQ(ARM_ALL_REG_LAST, arm.Convert(ARM_REG_LAST));
  EXPECT_EQ(ARM_ALL_REG_LAST, arm.Convert(0x1c22));
  RegsArm64 arm64;
  EXPECT_EQ(0, arm64.Convert(0));
  EXPECT_EQ(ARM64_REG_LAST - 1, arm64.Convert(ARM64_REG_LAST - 1));
  EXPECT_EQ(ARM64_ALL_REG_LAST, arm64.Convert(ARM64_REG_LAST));
  EXPECT_EQ(ARM64_ALL_REG_LAST, arm64.Convert(0x1c22));
  RegsRiscv64 riscv64;
  EXPECT_EQ(0, riscv64.Convert(0));
  EXPECT_EQ(RISCV64_REG_LAST - 1, riscv64.Convert(RISCV64_REG_LAST - 1));
  EXPECT_EQ(RISCV64_ALL_REG_LAST, riscv64.Convert(RISCV64_REG_LAST));
  EXPECT_EQ(RISCV64_ALL_REG_LAST, riscv64.Convert(0x1000));
  RegsX86 x86;
  EXPECT_EQ(0, x86.Convert(0));
  EXPECT_EQ(X86_REG_LAST - 1, x86.Convert(X86_REG_LAST - 1));
  EXPECT_EQ(X86_ALL_REG_LAST, x86.Convert(X86_REG_LAST));
  EXPECT_EQ(X86_ALL_REG_LAST, x86.Convert(0x1c22));
  RegsX86_64 x86_64;
  EXPECT_EQ(0, x86_64.Convert(0));
  EXPECT_EQ(X86_64_REG_LAST - 1, x86_64.Convert(X86_64_REG_LAST - 1));
  EXPECT_EQ(X86_64_ALL_REG_LAST, x86_64.Convert(X86_64_REG_LAST));
  EXPECT_EQ(X86_64_ALL_REG_LAST, x86_64.Convert(0x1c22));
}

TEST_F(RegsTest, arm_verify_sp_pc) {
  RegsArm arm;
  uint32_t* regs = reinterpret_cast<uint32_t*>(arm.RawData());
  regs[13] = 0x100;
  regs[15] = 0x200;
  EXPECT_EQ(0x100U, arm.sp());
  EXPECT_EQ(0x200U, arm.pc());
}

TEST_F(RegsTest, arm64_verify_sp_pc) {
  RegsArm64 arm64;
  uint64_t* regs = reinterpret_cast<uint64_t*>(arm64.RawData());
  regs[31] = 0xb100000000ULL;
  regs[32] = 0xc200000000ULL;
  EXPECT_EQ(0xb100000000U, arm64.sp());
  EXPECT_EQ(0xc200000000U, arm64.pc());
}

TEST_F(RegsTest, riscv64_verify_sp_pc) {
  RegsRiscv64 riscv64;
  uint64_t* regs = reinterpret_cast<uint64_t*>(riscv64.RawData());
  regs[2] = 0x212340000ULL;
  regs[0] = 0x1abcd0000ULL;
  EXPECT_EQ(0x212340000U, riscv64.sp());
  EXPECT_EQ(0x1abcd0000U, riscv64.pc());
}

TEST_F(RegsTest, riscv_convert) {
  RegsRiscv64 regs;
  EXPECT_EQ(0, regs.Convert(0));
  EXPECT_EQ(RISCV64_REG_VLENB, regs.Convert(0x1c22));
}

#if defined(__riscv)
TEST_F(RegsTest, riscv_get_vlenb) {
  RegsRiscv64 regs;
  EXPECT_NE(0U, regs.GetVlenbFromLocal());
  EXPECT_NE(0U, regs.GetVlenbFromRemote(-1));
}
#else
using RegsDeathTest = SilentDeathTest;
TEST_F(RegsDeathTest, riscv_get_vlenb) {
  RegsRiscv64 regs;
  ASSERT_DEATH(regs.GetVlenbFromLocal(), "");
  ASSERT_DEATH(regs.GetVlenbFromRemote(-1), "");
}
#endif

TEST_F(RegsTest, x86_verify_sp_pc) {
  RegsX86 x86;
  uint32_t* regs = reinterpret_cast<uint32_t*>(x86.RawData());
  regs[4] = 0x23450000;
  regs[8] = 0xabcd0000;
  EXPECT_EQ(0x23450000U, x86.sp());
  EXPECT_EQ(0xabcd0000U, x86.pc());
}

TEST_F(RegsTest, x86_64_verify_sp_pc) {
  RegsX86_64 x86_64;
  uint64_t* regs = reinterpret_cast<uint64_t*>(x86_64.RawData());
  regs[7] = 0x1200000000ULL;
  regs[16] = 0x4900000000ULL;
  EXPECT_EQ(0x1200000000U, x86_64.sp());
  EXPECT_EQ(0x4900000000U, x86_64.pc());
}

TEST_F(RegsTest, arm_error_code) {
  arm_ucontext_t ucontext = {.uc_mcontext.error_code = 0x8769U};
  std::unique_ptr<Regs> regs(RegsArm::CreateFromUcontext(&ucontext));
  ASSERT_TRUE(regs.get() != nullptr);
  uint32_t* raw_regs = reinterpret_cast<uint32_t*>(regs->RawData());
  EXPECT_EQ(0x8769U, raw_regs[ArmReg::ARM_REG_ERROR_CODE]);
}

TEST_F(RegsTest, arm64_esr_from_ucontext) {
  arm64_ucontext_t ucontext = {};
  arm64_esr_ctx* ctx = reinterpret_cast<arm64_esr_ctx*>(ucontext.uc_mcontext.reserved);
  ctx->head.magic = 0x45535201U;
  ctx->head.size = sizeof(arm64_esr_ctx);
  ctx->esr = 0x1200adefU;

  std::unique_ptr<Regs> regs(RegsArm64::CreateFromUcontext(&ucontext));
  ASSERT_TRUE(regs.get() != nullptr);
  uint64_t* raw_regs = reinterpret_cast<uint64_t*>(regs->RawData());
  EXPECT_EQ(0x1200adefU, raw_regs[ARM64_REG_ESR]);
}

TEST_F(RegsTest, arm64_esr_from_ucontext_edges) {
  arm64_ucontext_t ucontext = {};
  arm64_ctx* ctx = reinterpret_cast<arm64_ctx*>(ucontext.uc_mcontext.reserved);
  ctx->magic = 0xdeadbeef;
  // Choose a size that should be outside the structure.
  ctx->size = sizeof(ucontext.uc_mcontext.reserved);

  std::unique_ptr<Regs> regs(RegsArm64::CreateFromUcontext(&ucontext));
  ASSERT_TRUE(regs.get() != nullptr);
  uint64_t* raw_regs = reinterpret_cast<uint64_t*>(regs->RawData());
  EXPECT_EQ(0U, raw_regs[ARM64_REG_ESR]);

  // Put the esr context at the end of the ucontext section but with the esr
  // value past the end, so the value should not be set.
  ctx->size = sizeof(ucontext.uc_mcontext.reserved) - sizeof(arm64_ctx);
  arm64_ctx* last_ctx = reinterpret_cast<arm64_ctx*>(reinterpret_cast<uint8_t*>(ctx) + ctx->size);
  last_ctx->magic = 0x45535201U;
  last_ctx->size = sizeof(arm64_esr_ctx);

  regs.reset(RegsArm64::CreateFromUcontext(&ucontext));
  ASSERT_TRUE(regs.get() != nullptr);
  raw_regs = reinterpret_cast<uint64_t*>(regs->RawData());
  EXPECT_EQ(0U, raw_regs[ARM64_REG_ESR]);

  // Now move the esr context data at the absolute end of the section.
  last_ctx->magic = 0;
  last_ctx->size = 0;

  ctx->size = sizeof(ucontext.uc_mcontext.reserved) - sizeof(arm64_esr_ctx);
  arm64_esr_ctx* esr_ctx =
      reinterpret_cast<arm64_esr_ctx*>(reinterpret_cast<uint8_t*>(ctx) + ctx->size);
  esr_ctx->head.magic = 0x45535201U;
  esr_ctx->head.size = sizeof(arm64_esr_ctx);
  esr_ctx->esr = 0xdead1234U;

  regs.reset(RegsArm64::CreateFromUcontext(&ucontext));
  ASSERT_TRUE(regs.get() != nullptr);
  raw_regs = reinterpret_cast<uint64_t*>(regs->RawData());
  EXPECT_EQ(0xdead1234U, raw_regs[ARM64_REG_ESR]);
}

TEST_F(RegsTest, arm64_vg_from_sve_ucontext) {
  arm64_ucontext_t ucontext = {};
  arm64_sve_ctx* ctx = reinterpret_cast<arm64_sve_ctx*>(ucontext.uc_mcontext.reserved);
  ctx->head.magic = 0x53564501U;
  ctx->head.size = sizeof(arm64_sve_ctx);
  ctx->vl = 80;

  std::unique_ptr<Regs> regs(RegsArm64::CreateFromUcontext(&ucontext));
  ASSERT_TRUE(regs.get() != nullptr);
  uint64_t* raw_regs = reinterpret_cast<uint64_t*>(regs->RawData());
  EXPECT_EQ(10U, raw_regs[ARM64_REG_VG]);
}

TEST_F(RegsTest, arm64_vg_from_sve_ucontext_edges) {
  arm64_ucontext_t ucontext = {};
  arm64_ctx* ctx = reinterpret_cast<arm64_ctx*>(ucontext.uc_mcontext.reserved);
  ctx->magic = 0xdeadbeef;
  ctx->size = sizeof(ucontext.uc_mcontext.reserved) - sizeof(arm64_ctx);

  // Put the sve context at the end of the ucontext section but with the vl
  // value past the end, so the value should not be set.
  arm64_ctx* last_ctx = reinterpret_cast<arm64_ctx*>(reinterpret_cast<uint8_t*>(ctx) + ctx->size);
  last_ctx->magic = 0x53564501U;
  last_ctx->size = sizeof(arm64_sve_ctx);

  std::unique_ptr<Regs> regs(RegsArm64::CreateFromUcontext(&ucontext));
  ASSERT_TRUE(regs.get() != nullptr);
  uint64_t* raw_regs = reinterpret_cast<uint64_t*>(regs->RawData());
  EXPECT_EQ(0U, raw_regs[ARM64_REG_VG]);

  // Now move the sve context data at the absolute end of the section.
  last_ctx->magic = 0;
  last_ctx->size = 0;

  ctx->size = sizeof(ucontext.uc_mcontext.reserved) - sizeof(arm64_sve_ctx);
  arm64_sve_ctx* sve_ctx =
      reinterpret_cast<arm64_sve_ctx*>(reinterpret_cast<uint8_t*>(ctx) + ctx->size);
  sve_ctx->head.magic = 0x53564501U;
  sve_ctx->head.size = sizeof(arm64_sve_ctx);
  sve_ctx->vl = 80;

  regs.reset(RegsArm64::CreateFromUcontext(&ucontext));
  ASSERT_TRUE(regs.get() != nullptr);
  raw_regs = reinterpret_cast<uint64_t*>(regs->RawData());
  EXPECT_EQ(10U, raw_regs[ARM64_REG_VG]);
}

TEST_F(RegsTest, arm64_vg_from_extra_ucontext) {
  arm64_ucontext_t ucontext = {};
  // First the extra header.
  arm64_ctx* ctx = reinterpret_cast<arm64_ctx*>(ucontext.uc_mcontext.reserved);
  ctx->magic = 0x45585401U;
  ctx->size = sizeof(arm64_ctx);
  // Add an sve context header, after a null header (the data should be all zero).
  arm64_sve_ctx* sve_ctx =
      reinterpret_cast<arm64_sve_ctx*>(reinterpret_cast<uintptr_t>(ctx) + 2 * sizeof(arm64_ctx));
  sve_ctx->head.magic = 0x53564501U;
  sve_ctx->head.size = sizeof(arm64_sve_ctx);
  sve_ctx->vl = 80;

  std::unique_ptr<Regs> regs(RegsArm64::CreateFromUcontext(&ucontext));
  ASSERT_TRUE(regs.get() != nullptr);
  uint64_t* raw_regs = reinterpret_cast<uint64_t*>(regs->RawData());
  EXPECT_EQ(10U, raw_regs[ARM64_REG_VG]);
}

TEST_F(RegsTest, arm64_vg_from_extra_ucontext_edges) {
  arm64_ucontext_t ucontext = {};
  // First the extra header.
  arm64_ctx* ctx = reinterpret_cast<arm64_ctx*>(ucontext.uc_mcontext.reserved);
  ctx->magic = 0x45585401U;
  ctx->size = sizeof(arm64_ctx);
  arm64_ctx* null_ctx =
      reinterpret_cast<arm64_ctx*>(reinterpret_cast<uintptr_t>(ctx) + sizeof(arm64_ctx));
  // Change the magic of the null context to be non-null.
  null_ctx->magic = 1;
  // Add an sve context header, after a null header (the data should be all zero).
  arm64_sve_ctx* sve_ctx =
      reinterpret_cast<arm64_sve_ctx*>(reinterpret_cast<uintptr_t>(null_ctx) + sizeof(arm64_ctx));
  sve_ctx->head.magic = 0x53564501U;
  sve_ctx->head.size = sizeof(arm64_sve_ctx);
  sve_ctx->vl = 80;

  std::unique_ptr<Regs> regs(RegsArm64::CreateFromUcontext(&ucontext));
  ASSERT_TRUE(regs.get() != nullptr);
  uint64_t* raw_regs = reinterpret_cast<uint64_t*>(regs->RawData());
  EXPECT_EQ(0U, raw_regs[ARM64_REG_VG]);

  // Set the size of the null context to be non-null.
  null_ctx->magic = 0;
  null_ctx->size = 1;

  regs.reset(RegsArm64::CreateFromUcontext(&ucontext));
  ASSERT_TRUE(regs.get() != nullptr);
  raw_regs = reinterpret_cast<uint64_t*>(regs->RawData());
  EXPECT_EQ(0U, raw_regs[ARM64_REG_VG]);

  // Verify that if the null context is null, it actually does work.
  null_ctx->magic = 0;
  null_ctx->size = 0;
  regs.reset(RegsArm64::CreateFromUcontext(&ucontext));
  ASSERT_TRUE(regs.get() != nullptr);
  raw_regs = reinterpret_cast<uint64_t*>(regs->RawData());
  EXPECT_EQ(10U, raw_regs[ARM64_REG_VG]);
}

TEST_F(RegsTest, x86_create_from_ucontext) {
  x86_ucontext_t ucontext = {};
  ucontext.uc_mcontext.eax = 1;
  ucontext.uc_mcontext.ecx = 2;
  ucontext.uc_mcontext.edx = 3;
  ucontext.uc_mcontext.ebx = 4;
  ucontext.uc_mcontext.esp = 5;
  ucontext.uc_mcontext.ebp = 6;
  ucontext.uc_mcontext.esi = 7;
  ucontext.uc_mcontext.edi = 8;
  ucontext.uc_mcontext.eip = 9;
  ucontext.uc_mcontext.efl = 10;
  ucontext.uc_mcontext.cs = 11;
  ucontext.uc_mcontext.ss = 12;
  ucontext.uc_mcontext.ds = 13;
  ucontext.uc_mcontext.es = 14;
  ucontext.uc_mcontext.fs = 15;
  ucontext.uc_mcontext.gs = 16;
  ucontext.uc_mcontext.err = 0x1234;

  std::unique_ptr<Regs> regs(RegsX86::CreateFromUcontext(&ucontext));
  ASSERT_TRUE(regs != nullptr);
  uint32_t* raw_regs = reinterpret_cast<uint32_t*>(regs->RawData());
  EXPECT_EQ(1ULL, raw_regs[X86_REG_EAX]);
  EXPECT_EQ(2ULL, raw_regs[X86_REG_ECX]);
  EXPECT_EQ(3ULL, raw_regs[X86_REG_EDX]);
  EXPECT_EQ(4ULL, raw_regs[X86_REG_EBX]);
  EXPECT_EQ(5ULL, raw_regs[X86_REG_ESP]);
  EXPECT_EQ(6ULL, raw_regs[X86_REG_EBP]);
  EXPECT_EQ(7ULL, raw_regs[X86_REG_ESI]);
  EXPECT_EQ(8ULL, raw_regs[X86_REG_EDI]);
  EXPECT_EQ(9ULL, raw_regs[X86_REG_EIP]);
  EXPECT_EQ(10ULL, raw_regs[X86_REG_EFL]);
  EXPECT_EQ(11ULL, raw_regs[X86_REG_CS]);
  EXPECT_EQ(12ULL, raw_regs[X86_REG_SS]);
  EXPECT_EQ(13ULL, raw_regs[X86_REG_DS]);
  EXPECT_EQ(14ULL, raw_regs[X86_REG_ES]);
  EXPECT_EQ(15ULL, raw_regs[X86_REG_FS]);
  EXPECT_EQ(16ULL, raw_regs[X86_REG_GS]);
  EXPECT_EQ(0x1234U, raw_regs[X86_REG_ERR]);
}

TEST_F(RegsTest, x86_64_create_from_ucontext) {
  x86_64_ucontext_t ucontext = {};
  ucontext.uc_mcontext.rax = 1;
  ucontext.uc_mcontext.rbx = 2;
  ucontext.uc_mcontext.rcx = 3;
  ucontext.uc_mcontext.rdx = 4;
  ucontext.uc_mcontext.r8 = 5;
  ucontext.uc_mcontext.r9 = 6;
  ucontext.uc_mcontext.r10 = 7;
  ucontext.uc_mcontext.r11 = 8;
  ucontext.uc_mcontext.r12 = 9;
  ucontext.uc_mcontext.r13 = 10;
  ucontext.uc_mcontext.r14 = 11;
  ucontext.uc_mcontext.r15 = 12;
  ucontext.uc_mcontext.rdi = 13;
  ucontext.uc_mcontext.rsi = 14;
  ucontext.uc_mcontext.rbp = 15;
  ucontext.uc_mcontext.rsp = 16;
  ucontext.uc_mcontext.rip = 17;
  ucontext.uc_mcontext.err = 0x1234;

  std::unique_ptr<Regs> regs(RegsX86_64::CreateFromUcontext(&ucontext));
  ASSERT_TRUE(regs != nullptr);

  uint64_t* raw_regs = reinterpret_cast<uint64_t*>(regs->RawData());
  EXPECT_EQ(1ULL, raw_regs[X86_64_REG_RAX]);
  EXPECT_EQ(2ULL, raw_regs[X86_64_REG_RBX]);
  EXPECT_EQ(3ULL, raw_regs[X86_64_REG_RCX]);
  EXPECT_EQ(4ULL, raw_regs[X86_64_REG_RDX]);
  EXPECT_EQ(5ULL, raw_regs[X86_64_REG_R8]);
  EXPECT_EQ(6ULL, raw_regs[X86_64_REG_R9]);
  EXPECT_EQ(7ULL, raw_regs[X86_64_REG_R10]);
  EXPECT_EQ(8ULL, raw_regs[X86_64_REG_R11]);
  EXPECT_EQ(9ULL, raw_regs[X86_64_REG_R12]);
  EXPECT_EQ(10ULL, raw_regs[X86_64_REG_R13]);
  EXPECT_EQ(11ULL, raw_regs[X86_64_REG_R14]);
  EXPECT_EQ(12ULL, raw_regs[X86_64_REG_R15]);
  EXPECT_EQ(13ULL, raw_regs[X86_64_REG_RDI]);
  EXPECT_EQ(14ULL, raw_regs[X86_64_REG_RSI]);
  EXPECT_EQ(15ULL, raw_regs[X86_64_REG_RBP]);
  EXPECT_EQ(16ULL, raw_regs[X86_64_REG_RSP]);
  EXPECT_EQ(17ULL, raw_regs[X86_64_REG_RIP]);
  EXPECT_EQ(0x1234U, raw_regs[X86_64_REG_ERR]);
}

TEST_F(RegsTest, arm64_ra_sign_check) {
  RegsArm64 arm64;
  EXPECT_FALSE(arm64.IsRASigned());
  EXPECT_TRUE(arm64.SetPseudoRegister(Arm64Reg::ARM64_PREG_RA_SIGN_STATE, 1));
  EXPECT_TRUE(arm64.IsRASigned());
}

TEST_F(RegsTest, arm64_strip_pac_mask) {
  RegsArm64 arm64;
  EXPECT_TRUE(arm64.SetPseudoRegister(Arm64Reg::ARM64_PREG_RA_SIGN_STATE, 1));
  arm64.SetPACMask(0x007fff8000000000ULL);
  arm64.set_pc(0x0020007214bb3a04ULL);
  EXPECT_EQ(0x0000007214bb3a04ULL, arm64.pc());
}

TEST_F(RegsTest, arm64_fallback_pc) {
  RegsArm64 arm64;
  arm64.SetPACMask(0x007fff8000000000ULL);
  arm64.set_pc(0x0020007214bb3a04ULL);
  arm64.fallback_pc();
  EXPECT_EQ(0x0000007214bb3a04ULL, arm64.pc());
}

TEST_F(RegsTest, machine_type) {
  RegsArm arm_regs;
  EXPECT_EQ(ARCH_ARM, arm_regs.Arch());

  RegsArm64 arm64_regs;
  EXPECT_EQ(ARCH_ARM64, arm64_regs.Arch());

  RegsRiscv64 riscv64_regs;
  EXPECT_EQ(ARCH_RISCV64, riscv64_regs.Arch());

  RegsX86 x86_regs;
  EXPECT_EQ(ARCH_X86, x86_regs.Arch());

  RegsX86_64 x86_64_regs;
  EXPECT_EQ(ARCH_X86_64, x86_64_regs.Arch());
}

template <typename RegisterType>
void clone_test(Regs* regs) {
  RegisterType* register_values = reinterpret_cast<RegisterType*>(regs->RawData());
  int num_regs = regs->total_regs();
  for (int i = 0; i < num_regs; ++i) {
    register_values[i] = i;
  }

  std::unique_ptr<Regs> clone(regs->Clone());
  ASSERT_EQ(regs->total_regs(), clone->total_regs());
  RegisterType* clone_values = reinterpret_cast<RegisterType*>(clone->RawData());
  for (int i = 0; i < num_regs; ++i) {
    EXPECT_EQ(register_values[i], clone_values[i]);
    EXPECT_NE(&register_values[i], &clone_values[i]);
  }
}

TEST_F(RegsTest, clone) {
  std::vector<std::unique_ptr<Regs>> regs;
  regs.emplace_back(new RegsArm());
  regs.emplace_back(new RegsArm64());
  regs.emplace_back(new RegsRiscv64());
  regs.emplace_back(new RegsX86());
  regs.emplace_back(new RegsX86_64());

  for (auto& r : regs) {
    if (r->Is32Bit()) {
      clone_test<uint32_t>(r.get());
    } else {
      clone_test<uint64_t>(r.get());
    }
  }
}

}  // namespace unwindstack
