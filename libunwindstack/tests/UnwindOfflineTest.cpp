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

#include <sys/mman.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include <unwindstack/RegsArm64.h>
#include <unwindstack/Unwinder.h>

#include "TestUtils.h"
#include "utils/MemoryFake.h"
#include "utils/OfflineUnwindUtils.h"

// This collection of tests exercises Unwinder::Unwind for offline unwinds.
//
// See `libunwindstack/utils/OfflineUnwindUtils.h` for more info on offline unwinds
// and b/192012600 for additional information regarding offline unwind benchmarks.
namespace unwindstack {
namespace {

class UnwindOfflineTest : public ::testing::Test {
 protected:
  void TearDown() override { offline_utils_.ReturnToCurrentWorkingDirectory(); }

  OfflineUnwindUtils offline_utils_;
};

TEST_F(UnwindOfflineTest, pc_straddle_arm) {
  std::string error_msg;
  if (!offline_utils_.Init("straddle_arm/", ARCH_ARM, &error_msg)) FAIL() << error_msg;

  Regs* regs = offline_utils_.GetRegs();
  std::unique_ptr<Regs> regs_copy(regs->Clone());
  Unwinder unwinder(128, offline_utils_.GetMaps(), regs, offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(4U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 0001a9f8  libc.so (abort+64)\n"
      "  #01 pc 00006a1b  libbase.so (android::base::DefaultAborter(char const*)+6)\n"
      "  #02 pc 00007441  libbase.so (android::base::LogMessage::~LogMessage()+748)\n"
      "  #03 pc 00015147  /does/not/exist/libhidlbase.so\n",
      frame_info);
  EXPECT_EQ(0xf31ea9f8U, unwinder.frames()[0].pc);
  EXPECT_EQ(0xe9c866f8U, unwinder.frames()[0].sp);
  EXPECT_EQ(0xf2da0a1bU, unwinder.frames()[1].pc);
  EXPECT_EQ(0xe9c86728U, unwinder.frames()[1].sp);
  EXPECT_EQ(0xf2da1441U, unwinder.frames()[2].pc);
  EXPECT_EQ(0xe9c86730U, unwinder.frames()[2].sp);
  EXPECT_EQ(0xf3367147U, unwinder.frames()[3].pc);
  EXPECT_EQ(0xe9c86778U, unwinder.frames()[3].sp);

  // Display build ids now.
  unwinder.SetRegs(regs_copy.get());
  unwinder.SetDisplayBuildID(true);
  unwinder.Unwind();

  frame_info = DumpFrames(unwinder);
  ASSERT_EQ(4U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 0001a9f8  libc.so (abort+64) (BuildId: 2dd0d4ba881322a0edabeed94808048c)\n"
      "  #01 pc 00006a1b  libbase.so (android::base::DefaultAborter(char const*)+6) (BuildId: "
      "ed43842c239cac1a618e600ea91c4cbd)\n"
      "  #02 pc 00007441  libbase.so (android::base::LogMessage::~LogMessage()+748) (BuildId: "
      "ed43842c239cac1a618e600ea91c4cbd)\n"
      "  #03 pc 00015147  /does/not/exist/libhidlbase.so\n",
      frame_info);
}

TEST_F(UnwindOfflineTest, pc_in_gnu_debugdata_arm) {
  std::string error_msg;
  if (!offline_utils_.Init("gnu_debugdata_arm/", ARCH_ARM, &error_msg)) FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(2U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 0006dc49  libandroid_runtime.so "
      "(android::AndroidRuntime::javaThreadShell(void*)+80)\n"
      "  #01 pc 0006dce5  libandroid_runtime.so "
      "(android::AndroidRuntime::javaCreateThreadEtc(int (*)(void*), void*, char const*, int, "
      "unsigned int, void**))\n",
      frame_info);
  EXPECT_EQ(0xf1f6dc49U, unwinder.frames()[0].pc);
  EXPECT_EQ(0xd8fe6930U, unwinder.frames()[0].sp);
  EXPECT_EQ(0xf1f6dce5U, unwinder.frames()[1].pc);
  EXPECT_EQ(0xd8fe6958U, unwinder.frames()[1].sp);
}

TEST_F(UnwindOfflineTest, pc_straddle_arm64) {
  std::string error_msg;
  if (!offline_utils_.Init("straddle_arm64/", ARCH_ARM64, &error_msg)) FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(6U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 0000000000429fd8  libunwindstack_test (SignalInnerFunction+24)\n"
      "  #01 pc 000000000042a078  libunwindstack_test (SignalMiddleFunction+8)\n"
      "  #02 pc 000000000042a08c  libunwindstack_test (SignalOuterFunction+8)\n"
      "  #03 pc 000000000042d8fc  libunwindstack_test "
      "(unwindstack::RemoteThroughSignal(int, unsigned int)+20)\n"
      "  #04 pc 000000000042d8d8  libunwindstack_test "
      "(unwindstack::UnwindTest_remote_through_signal_Test::TestBody()+32)\n"
      "  #05 pc 0000000000455d70  libunwindstack_test (testing::Test::Run()+392)\n",
      frame_info);
  EXPECT_EQ(0x64d09d4fd8U, unwinder.frames()[0].pc);
  EXPECT_EQ(0x7fe0d84040U, unwinder.frames()[0].sp);
  EXPECT_EQ(0x64d09d5078U, unwinder.frames()[1].pc);
  EXPECT_EQ(0x7fe0d84070U, unwinder.frames()[1].sp);
  EXPECT_EQ(0x64d09d508cU, unwinder.frames()[2].pc);
  EXPECT_EQ(0x7fe0d84080U, unwinder.frames()[2].sp);
  EXPECT_EQ(0x64d09d88fcU, unwinder.frames()[3].pc);
  EXPECT_EQ(0x7fe0d84090U, unwinder.frames()[3].sp);
  EXPECT_EQ(0x64d09d88d8U, unwinder.frames()[4].pc);
  EXPECT_EQ(0x7fe0d840f0U, unwinder.frames()[4].sp);
  EXPECT_EQ(0x64d0a00d70U, unwinder.frames()[5].pc);
  EXPECT_EQ(0x7fe0d84110U, unwinder.frames()[5].sp);
}

TEST_F(UnwindOfflineTest, jit_debug_x86) {
  std::string error_msg;
  if (!offline_utils_.Init("jit_debug_x86/", ARCH_X86, &error_msg,
                           ProcessMemoryFlag::kIncludeJitMemory))
    FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.SetJitDebug(offline_utils_.GetJitDebug());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(69U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 00068fb8  libarttestd.so (art::CauseSegfault()+72)\n"
      "  #01 pc 00067f00  libarttestd.so (Java_Main_unwindInProcess+10032)\n"
      "  #02 pc 000021a8  137-cfi.odex (boolean Main.unwindInProcess(boolean, int, "
      "boolean)+136)\n"
      "  #03 pc 0000fe80  anonymous:ee74c000 (boolean Main.bar(boolean)+64)\n"
      "  #04 pc 006ad4d2  libartd.so (art_quick_invoke_stub+338)\n"
      "  #05 pc 00146ab5  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+885)\n"
      "  #06 pc 0039cf0d  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+653)\n"
      "  #07 pc 00392552  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+354)\n"
      "  #08 pc 0039399a  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+234)\n"
      "  #09 pc 00684362  libartd.so (artQuickToInterpreterBridge+1058)\n"
      "  #10 pc 006b35bd  libartd.so (art_quick_to_interpreter_bridge+77)\n"
      "  #11 pc 0000fe03  anonymous:ee74c000 (int Main.compare(Main, Main)+51)\n"
      "  #12 pc 006ad4d2  libartd.so (art_quick_invoke_stub+338)\n"
      "  #13 pc 00146ab5  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+885)\n"
      "  #14 pc 0039cf0d  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+653)\n"
      "  #15 pc 00392552  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+354)\n"
      "  #16 pc 0039399a  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+234)\n"
      "  #17 pc 00684362  libartd.so (artQuickToInterpreterBridge+1058)\n"
      "  #18 pc 006b35bd  libartd.so (art_quick_to_interpreter_bridge+77)\n"
      "  #19 pc 0000fd3b  anonymous:ee74c000 (int Main.compare(java.lang.Object, "
      "java.lang.Object)+107)\n"
      "  #20 pc 006ad4d2  libartd.so (art_quick_invoke_stub+338)\n"
      "  #21 pc 00146ab5  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+885)\n"
      "  #22 pc 0039cf0d  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+653)\n"
      "  #23 pc 00392552  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+354)\n"
      "  #24 pc 0039399a  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+234)\n"
      "  #25 pc 00684362  libartd.so (artQuickToInterpreterBridge+1058)\n"
      "  #26 pc 006b35bd  libartd.so (art_quick_to_interpreter_bridge+77)\n"
      "  #27 pc 0000fbdb  anonymous:ee74c000 (int "
      "java.util.Arrays.binarySearch0(java.lang.Object[], int, int, java.lang.Object, "
      "java.util.Comparator)+331)\n"
      "  #28 pc 006ad6a2  libartd.so (art_quick_invoke_static_stub+418)\n"
      "  #29 pc 00146acb  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+907)\n"
      "  #30 pc 0039cf0d  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+653)\n"
      "  #31 pc 00392552  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+354)\n"
      "  #32 pc 0039399a  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+234)\n"
      "  #33 pc 00684362  libartd.so (artQuickToInterpreterBridge+1058)\n"
      "  #34 pc 006b35bd  libartd.so (art_quick_to_interpreter_bridge+77)\n"
      "  #35 pc 0000f624  anonymous:ee74c000 (boolean Main.foo()+164)\n"
      "  #36 pc 006ad4d2  libartd.so (art_quick_invoke_stub+338)\n"
      "  #37 pc 00146ab5  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+885)\n"
      "  #38 pc 0039cf0d  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+653)\n"
      "  #39 pc 00392552  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+354)\n"
      "  #40 pc 0039399a  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+234)\n"
      "  #41 pc 00684362  libartd.so (artQuickToInterpreterBridge+1058)\n"
      "  #42 pc 006b35bd  libartd.so (art_quick_to_interpreter_bridge+77)\n"
      "  #43 pc 0000eedb  anonymous:ee74c000 (void Main.runPrimary()+59)\n"
      "  #44 pc 006ad4d2  libartd.so (art_quick_invoke_stub+338)\n"
      "  #45 pc 00146ab5  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+885)\n"
      "  #46 pc 0039cf0d  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+653)\n"
      "  #47 pc 00392552  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+354)\n"
      "  #48 pc 0039399a  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+234)\n"
      "  #49 pc 00684362  libartd.so (artQuickToInterpreterBridge+1058)\n"
      "  #50 pc 006b35bd  libartd.so (art_quick_to_interpreter_bridge+77)\n"
      "  #51 pc 0000ac21  anonymous:ee74c000 (void Main.main(java.lang.String[])+97)\n"
      "  #52 pc 006ad6a2  libartd.so (art_quick_invoke_static_stub+418)\n"
      "  #53 pc 00146acb  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+907)\n"
      "  #54 pc 0039cf0d  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+653)\n"
      "  #55 pc 00392552  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+354)\n"
      "  #56 pc 0039399a  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+234)\n"
      "  #57 pc 00684362  libartd.so (artQuickToInterpreterBridge+1058)\n"
      "  #58 pc 006b35bd  libartd.so (art_quick_to_interpreter_bridge+77)\n"
      "  #59 pc 006ad6a2  libartd.so (art_quick_invoke_static_stub+418)\n"
      "  #60 pc 00146acb  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+907)\n"
      "  #61 pc 005aac95  libartd.so "
      "(art::InvokeWithArgArray(art::ScopedObjectAccessAlreadyRunnable const&, art::ArtMethod*, "
      "art::ArgArray*, art::JValue*, char const*)+85)\n"
      "  #62 pc 005aab5a  libartd.so "
      "(art::InvokeWithVarArgs(art::ScopedObjectAccessAlreadyRunnable const&, _jobject*, "
      "_jmethodID*, char*)+362)\n"
      "  #63 pc 0048a3dd  libartd.so "
      "(art::JNI::CallStaticVoidMethodV(_JNIEnv*, _jclass*, _jmethodID*, char*)+125)\n"
      "  #64 pc 0018448c  libartd.so "
      "(art::CheckJNI::CallMethodV(char const*, _JNIEnv*, _jobject*, _jclass*, _jmethodID*, char*, "
      "art::Primitive::Type, art::InvokeType)+1964)\n"
      "  #65 pc 0017cf06  libartd.so "
      "(art::CheckJNI::CallStaticVoidMethodV(_JNIEnv*, _jclass*, _jmethodID*, char*)+70)\n"
      "  #66 pc 00001d8c  dalvikvm32 "
      "(_JNIEnv::CallStaticVoidMethod(_jclass*, _jmethodID*, ...)+60)\n"
      "  #67 pc 00001a80  dalvikvm32 (main+1312)\n"
      "  #68 pc 00018275  libc.so\n",
      frame_info);
  EXPECT_EQ(0xeb89bfb8U, unwinder.frames()[0].pc);
  EXPECT_EQ(0xffeb5280U, unwinder.frames()[0].sp);
  EXPECT_EQ(0xeb89af00U, unwinder.frames()[1].pc);
  EXPECT_EQ(0xffeb52a0U, unwinder.frames()[1].sp);
  EXPECT_EQ(0xec6061a8U, unwinder.frames()[2].pc);
  EXPECT_EQ(0xffeb5ce0U, unwinder.frames()[2].sp);
  EXPECT_EQ(0xee75be80U, unwinder.frames()[3].pc);
  EXPECT_EQ(0xffeb5d30U, unwinder.frames()[3].sp);
  EXPECT_EQ(0xf728e4d2U, unwinder.frames()[4].pc);
  EXPECT_EQ(0xffeb5d60U, unwinder.frames()[4].sp);
  EXPECT_EQ(0xf6d27ab5U, unwinder.frames()[5].pc);
  EXPECT_EQ(0xffeb5d80U, unwinder.frames()[5].sp);
  EXPECT_EQ(0xf6f7df0dU, unwinder.frames()[6].pc);
  EXPECT_EQ(0xffeb5e20U, unwinder.frames()[6].sp);
  EXPECT_EQ(0xf6f73552U, unwinder.frames()[7].pc);
  EXPECT_EQ(0xffeb5ec0U, unwinder.frames()[7].sp);
  EXPECT_EQ(0xf6f7499aU, unwinder.frames()[8].pc);
  EXPECT_EQ(0xffeb5f40U, unwinder.frames()[8].sp);
  EXPECT_EQ(0xf7265362U, unwinder.frames()[9].pc);
  EXPECT_EQ(0xffeb5fb0U, unwinder.frames()[9].sp);
  EXPECT_EQ(0xf72945bdU, unwinder.frames()[10].pc);
  EXPECT_EQ(0xffeb6110U, unwinder.frames()[10].sp);
  EXPECT_EQ(0xee75be03U, unwinder.frames()[11].pc);
  EXPECT_EQ(0xffeb6160U, unwinder.frames()[11].sp);
  EXPECT_EQ(0xf728e4d2U, unwinder.frames()[12].pc);
  EXPECT_EQ(0xffeb6180U, unwinder.frames()[12].sp);
  EXPECT_EQ(0xf6d27ab5U, unwinder.frames()[13].pc);
  EXPECT_EQ(0xffeb61b0U, unwinder.frames()[13].sp);
  EXPECT_EQ(0xf6f7df0dU, unwinder.frames()[14].pc);
  EXPECT_EQ(0xffeb6250U, unwinder.frames()[14].sp);
  EXPECT_EQ(0xf6f73552U, unwinder.frames()[15].pc);
  EXPECT_EQ(0xffeb62f0U, unwinder.frames()[15].sp);
  EXPECT_EQ(0xf6f7499aU, unwinder.frames()[16].pc);
  EXPECT_EQ(0xffeb6370U, unwinder.frames()[16].sp);
  EXPECT_EQ(0xf7265362U, unwinder.frames()[17].pc);
  EXPECT_EQ(0xffeb63e0U, unwinder.frames()[17].sp);
  EXPECT_EQ(0xf72945bdU, unwinder.frames()[18].pc);
  EXPECT_EQ(0xffeb6530U, unwinder.frames()[18].sp);
  EXPECT_EQ(0xee75bd3bU, unwinder.frames()[19].pc);
  EXPECT_EQ(0xffeb6580U, unwinder.frames()[19].sp);
  EXPECT_EQ(0xf728e4d2U, unwinder.frames()[20].pc);
  EXPECT_EQ(0xffeb65b0U, unwinder.frames()[20].sp);
  EXPECT_EQ(0xf6d27ab5U, unwinder.frames()[21].pc);
  EXPECT_EQ(0xffeb65e0U, unwinder.frames()[21].sp);
  EXPECT_EQ(0xf6f7df0dU, unwinder.frames()[22].pc);
  EXPECT_EQ(0xffeb6680U, unwinder.frames()[22].sp);
  EXPECT_EQ(0xf6f73552U, unwinder.frames()[23].pc);
  EXPECT_EQ(0xffeb6720U, unwinder.frames()[23].sp);
  EXPECT_EQ(0xf6f7499aU, unwinder.frames()[24].pc);
  EXPECT_EQ(0xffeb67a0U, unwinder.frames()[24].sp);
  EXPECT_EQ(0xf7265362U, unwinder.frames()[25].pc);
  EXPECT_EQ(0xffeb6810U, unwinder.frames()[25].sp);
  EXPECT_EQ(0xf72945bdU, unwinder.frames()[26].pc);
  EXPECT_EQ(0xffeb6960U, unwinder.frames()[26].sp);
  EXPECT_EQ(0xee75bbdbU, unwinder.frames()[27].pc);
  EXPECT_EQ(0xffeb69b0U, unwinder.frames()[27].sp);
  EXPECT_EQ(0xf728e6a2U, unwinder.frames()[28].pc);
  EXPECT_EQ(0xffeb69f0U, unwinder.frames()[28].sp);
  EXPECT_EQ(0xf6d27acbU, unwinder.frames()[29].pc);
  EXPECT_EQ(0xffeb6a20U, unwinder.frames()[29].sp);
  EXPECT_EQ(0xf6f7df0dU, unwinder.frames()[30].pc);
  EXPECT_EQ(0xffeb6ac0U, unwinder.frames()[30].sp);
  EXPECT_EQ(0xf6f73552U, unwinder.frames()[31].pc);
  EXPECT_EQ(0xffeb6b60U, unwinder.frames()[31].sp);
  EXPECT_EQ(0xf6f7499aU, unwinder.frames()[32].pc);
  EXPECT_EQ(0xffeb6be0U, unwinder.frames()[32].sp);
  EXPECT_EQ(0xf7265362U, unwinder.frames()[33].pc);
  EXPECT_EQ(0xffeb6c50U, unwinder.frames()[33].sp);
  EXPECT_EQ(0xf72945bdU, unwinder.frames()[34].pc);
  EXPECT_EQ(0xffeb6dd0U, unwinder.frames()[34].sp);
  EXPECT_EQ(0xee75b624U, unwinder.frames()[35].pc);
  EXPECT_EQ(0xffeb6e20U, unwinder.frames()[35].sp);
  EXPECT_EQ(0xf728e4d2U, unwinder.frames()[36].pc);
  EXPECT_EQ(0xffeb6e50U, unwinder.frames()[36].sp);
  EXPECT_EQ(0xf6d27ab5U, unwinder.frames()[37].pc);
  EXPECT_EQ(0xffeb6e70U, unwinder.frames()[37].sp);
  EXPECT_EQ(0xf6f7df0dU, unwinder.frames()[38].pc);
  EXPECT_EQ(0xffeb6f10U, unwinder.frames()[38].sp);
  EXPECT_EQ(0xf6f73552U, unwinder.frames()[39].pc);
  EXPECT_EQ(0xffeb6fb0U, unwinder.frames()[39].sp);
  EXPECT_EQ(0xf6f7499aU, unwinder.frames()[40].pc);
  EXPECT_EQ(0xffeb7030U, unwinder.frames()[40].sp);
  EXPECT_EQ(0xf7265362U, unwinder.frames()[41].pc);
  EXPECT_EQ(0xffeb70a0U, unwinder.frames()[41].sp);
  EXPECT_EQ(0xf72945bdU, unwinder.frames()[42].pc);
  EXPECT_EQ(0xffeb71f0U, unwinder.frames()[42].sp);
  EXPECT_EQ(0xee75aedbU, unwinder.frames()[43].pc);
  EXPECT_EQ(0xffeb7240U, unwinder.frames()[43].sp);
  EXPECT_EQ(0xf728e4d2U, unwinder.frames()[44].pc);
  EXPECT_EQ(0xffeb72a0U, unwinder.frames()[44].sp);
  EXPECT_EQ(0xf6d27ab5U, unwinder.frames()[45].pc);
  EXPECT_EQ(0xffeb72c0U, unwinder.frames()[45].sp);
  EXPECT_EQ(0xf6f7df0dU, unwinder.frames()[46].pc);
  EXPECT_EQ(0xffeb7360U, unwinder.frames()[46].sp);
  EXPECT_EQ(0xf6f73552U, unwinder.frames()[47].pc);
  EXPECT_EQ(0xffeb7400U, unwinder.frames()[47].sp);
  EXPECT_EQ(0xf6f7499aU, unwinder.frames()[48].pc);
  EXPECT_EQ(0xffeb7480U, unwinder.frames()[48].sp);
  EXPECT_EQ(0xf7265362U, unwinder.frames()[49].pc);
  EXPECT_EQ(0xffeb74f0U, unwinder.frames()[49].sp);
  EXPECT_EQ(0xf72945bdU, unwinder.frames()[50].pc);
  EXPECT_EQ(0xffeb7680U, unwinder.frames()[50].sp);
  EXPECT_EQ(0xee756c21U, unwinder.frames()[51].pc);
  EXPECT_EQ(0xffeb76d0U, unwinder.frames()[51].sp);
  EXPECT_EQ(0xf728e6a2U, unwinder.frames()[52].pc);
  EXPECT_EQ(0xffeb76f0U, unwinder.frames()[52].sp);
  EXPECT_EQ(0xf6d27acbU, unwinder.frames()[53].pc);
  EXPECT_EQ(0xffeb7710U, unwinder.frames()[53].sp);
  EXPECT_EQ(0xf6f7df0dU, unwinder.frames()[54].pc);
  EXPECT_EQ(0xffeb77b0U, unwinder.frames()[54].sp);
  EXPECT_EQ(0xf6f73552U, unwinder.frames()[55].pc);
  EXPECT_EQ(0xffeb7850U, unwinder.frames()[55].sp);
  EXPECT_EQ(0xf6f7499aU, unwinder.frames()[56].pc);
  EXPECT_EQ(0xffeb78d0U, unwinder.frames()[56].sp);
  EXPECT_EQ(0xf7265362U, unwinder.frames()[57].pc);
  EXPECT_EQ(0xffeb7940U, unwinder.frames()[57].sp);
  EXPECT_EQ(0xf72945bdU, unwinder.frames()[58].pc);
  EXPECT_EQ(0xffeb7a80U, unwinder.frames()[58].sp);
  EXPECT_EQ(0xf728e6a2U, unwinder.frames()[59].pc);
  EXPECT_EQ(0xffeb7ad0U, unwinder.frames()[59].sp);
  EXPECT_EQ(0xf6d27acbU, unwinder.frames()[60].pc);
  EXPECT_EQ(0xffeb7af0U, unwinder.frames()[60].sp);
  EXPECT_EQ(0xf718bc95U, unwinder.frames()[61].pc);
  EXPECT_EQ(0xffeb7b90U, unwinder.frames()[61].sp);
  EXPECT_EQ(0xf718bb5aU, unwinder.frames()[62].pc);
  EXPECT_EQ(0xffeb7c50U, unwinder.frames()[62].sp);
  EXPECT_EQ(0xf706b3ddU, unwinder.frames()[63].pc);
  EXPECT_EQ(0xffeb7d10U, unwinder.frames()[63].sp);
  EXPECT_EQ(0xf6d6548cU, unwinder.frames()[64].pc);
  EXPECT_EQ(0xffeb7d70U, unwinder.frames()[64].sp);
  EXPECT_EQ(0xf6d5df06U, unwinder.frames()[65].pc);
  EXPECT_EQ(0xffeb7df0U, unwinder.frames()[65].sp);
  EXPECT_EQ(0x56574d8cU, unwinder.frames()[66].pc);
  EXPECT_EQ(0xffeb7e40U, unwinder.frames()[66].sp);
  EXPECT_EQ(0x56574a80U, unwinder.frames()[67].pc);
  EXPECT_EQ(0xffeb7e70U, unwinder.frames()[67].sp);
  EXPECT_EQ(0xf7363275U, unwinder.frames()[68].pc);
  EXPECT_EQ(0xffeb7ef0U, unwinder.frames()[68].sp);
}

TEST_F(UnwindOfflineTest, jit_debug_arm) {
  std::string error_msg;
  if (!offline_utils_.Init("jit_debug_arm/", ARCH_ARM, &error_msg,
                           ProcessMemoryFlag::kIncludeJitMemory))
    FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.SetJitDebug(offline_utils_.GetJitDebug());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(76U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 00018a5e  libarttestd.so (Java_Main_unwindInProcess+866)\n"
      "  #01 pc 0000212d  137-cfi.odex (boolean Main.unwindInProcess(boolean, int, "
      "boolean)+92)\n"
      "  #02 pc 00011cb1  anonymous:e2796000 (boolean Main.bar(boolean)+72)\n"
      "  #03 pc 00462175  libartd.so (art_quick_invoke_stub_internal+68)\n"
      "  #04 pc 00467129  libartd.so (art_quick_invoke_stub+228)\n"
      "  #05 pc 000bf7a9  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+864)\n"
      "  #06 pc 00247833  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+382)\n"
      "  #07 pc 0022e935  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+244)\n"
      "  #08 pc 0022f71d  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+128)\n"
      "  #09 pc 00442865  libartd.so (artQuickToInterpreterBridge+796)\n"
      "  #10 pc 004666ff  libartd.so (art_quick_to_interpreter_bridge+30)\n"
      "  #11 pc 00011c31  anonymous:e2796000 (int Main.compare(Main, Main)+64)\n"
      "  #12 pc 00462175  libartd.so (art_quick_invoke_stub_internal+68)\n"
      "  #13 pc 00467129  libartd.so (art_quick_invoke_stub+228)\n"
      "  #14 pc 000bf7a9  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+864)\n"
      "  #15 pc 00247833  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+382)\n"
      "  #16 pc 0022e935  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+244)\n"
      "  #17 pc 0022f71d  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+128)\n"
      "  #18 pc 00442865  libartd.so (artQuickToInterpreterBridge+796)\n"
      "  #19 pc 004666ff  libartd.so (art_quick_to_interpreter_bridge+30)\n"
      "  #20 pc 00011b77  anonymous:e2796000 (int Main.compare(java.lang.Object, "
      "java.lang.Object)+118)\n"
      "  #21 pc 00462175  libartd.so (art_quick_invoke_stub_internal+68)\n"
      "  #22 pc 00467129  libartd.so (art_quick_invoke_stub+228)\n"
      "  #23 pc 000bf7a9  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+864)\n"
      "  #24 pc 00247833  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+382)\n"
      "  #25 pc 0022e935  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+244)\n"
      "  #26 pc 0022f71d  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+128)\n"
      "  #27 pc 00442865  libartd.so (artQuickToInterpreterBridge+796)\n"
      "  #28 pc 004666ff  libartd.so (art_quick_to_interpreter_bridge+30)\n"
      "  #29 pc 00011a29  anonymous:e2796000 (int "
      "java.util.Arrays.binarySearch0(java.lang.Object[], int, int, java.lang.Object, "
      "java.util.Comparator)+304)\n"
      "  #30 pc 00462175  libartd.so (art_quick_invoke_stub_internal+68)\n"
      "  #31 pc 0046722f  libartd.so (art_quick_invoke_static_stub+226)\n"
      "  #32 pc 000bf7bb  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+882)\n"
      "  #33 pc 00247833  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+382)\n"
      "  #34 pc 0022e935  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+244)\n"
      "  #35 pc 0022f71d  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+128)\n"
      "  #36 pc 00442865  libartd.so (artQuickToInterpreterBridge+796)\n"
      "  #37 pc 004666ff  libartd.so (art_quick_to_interpreter_bridge+30)\n"
      "  #38 pc 0001139b  anonymous:e2796000 (boolean Main.foo()+178)\n"
      "  #39 pc 00462175  libartd.so (art_quick_invoke_stub_internal+68)\n"
      "  #40 pc 00467129  libartd.so (art_quick_invoke_stub+228)\n"
      "  #41 pc 000bf7a9  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+864)\n"
      "  #42 pc 00247833  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+382)\n"
      "  #43 pc 0022e935  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+244)\n"
      "  #44 pc 0022f71d  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+128)\n"
      "  #45 pc 00442865  libartd.so (artQuickToInterpreterBridge+796)\n"
      "  #46 pc 004666ff  libartd.so (art_quick_to_interpreter_bridge+30)\n"
      "  #47 pc 00010aa7  anonymous:e2796000 (void Main.runPrimary()+70)\n"
      "  #48 pc 00462175  libartd.so (art_quick_invoke_stub_internal+68)\n"
      "  #49 pc 00467129  libartd.so (art_quick_invoke_stub+228)\n"
      "  #50 pc 000bf7a9  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+864)\n"
      "  #51 pc 00247833  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+382)\n"
      "  #52 pc 0022e935  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+244)\n"
      "  #53 pc 0022f71d  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+128)\n"
      "  #54 pc 00442865  libartd.so (artQuickToInterpreterBridge+796)\n"
      "  #55 pc 004666ff  libartd.so (art_quick_to_interpreter_bridge+30)\n"
      "  #56 pc 0000ba99  anonymous:e2796000 (void Main.main(java.lang.String[])+144)\n"
      "  #57 pc 00462175  libartd.so (art_quick_invoke_stub_internal+68)\n"
      "  #58 pc 0046722f  libartd.so (art_quick_invoke_static_stub+226)\n"
      "  #59 pc 000bf7bb  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+882)\n"
      "  #60 pc 00247833  libartd.so "
      "(art::interpreter::ArtInterpreterToCompiledCodeBridge(art::Thread*, art::ArtMethod*, "
      "art::ShadowFrame*, unsigned short, art::JValue*)+382)\n"
      "  #61 pc 0022e935  libartd.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+244)\n"
      "  #62 pc 0022f71d  libartd.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+128)\n"
      "  #63 pc 00442865  libartd.so (artQuickToInterpreterBridge+796)\n"
      "  #64 pc 004666ff  libartd.so (art_quick_to_interpreter_bridge+30)\n"
      "  #65 pc 00462175  libartd.so (art_quick_invoke_stub_internal+68)\n"
      "  #66 pc 0046722f  libartd.so (art_quick_invoke_static_stub+226)\n"
      "  #67 pc 000bf7bb  libartd.so "
      "(art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char "
      "const*)+882)\n"
      "  #68 pc 003b292d  libartd.so "
      "(art::InvokeWithArgArray(art::ScopedObjectAccessAlreadyRunnable const&, art::ArtMethod*, "
      "art::ArgArray*, art::JValue*, char const*)+52)\n"
      "  #69 pc 003b26c3  libartd.so "
      "(art::InvokeWithVarArgs(art::ScopedObjectAccessAlreadyRunnable const&, _jobject*, "
      "_jmethodID*, std::__va_list)+210)\n"
      "  #70 pc 00308411  libartd.so "
      "(art::JNI::CallStaticVoidMethodV(_JNIEnv*, _jclass*, _jmethodID*, std::__va_list)+76)\n"
      "  #71 pc 000e6a9f  libartd.so "
      "(art::CheckJNI::CallMethodV(char const*, _JNIEnv*, _jobject*, _jclass*, _jmethodID*, "
      "std::__va_list, art::Primitive::Type, art::InvokeType)+1486)\n"
      "  #72 pc 000e19b9  libartd.so "
      "(art::CheckJNI::CallStaticVoidMethodV(_JNIEnv*, _jclass*, _jmethodID*, std::__va_list)+40)\n"
      "  #73 pc 0000159f  dalvikvm32 "
      "(_JNIEnv::CallStaticVoidMethod(_jclass*, _jmethodID*, ...)+30)\n"
      "  #74 pc 00001349  dalvikvm32 (main+896)\n"
      "  #75 pc 000850c9  libc.so\n",
      frame_info);
  EXPECT_EQ(0xdfe66a5eU, unwinder.frames()[0].pc);
  EXPECT_EQ(0xff85d180U, unwinder.frames()[0].sp);
  EXPECT_EQ(0xe044712dU, unwinder.frames()[1].pc);
  EXPECT_EQ(0xff85d200U, unwinder.frames()[1].sp);
  EXPECT_EQ(0xe27a7cb1U, unwinder.frames()[2].pc);
  EXPECT_EQ(0xff85d290U, unwinder.frames()[2].sp);
  EXPECT_EQ(0xed75c175U, unwinder.frames()[3].pc);
  EXPECT_EQ(0xff85d2b0U, unwinder.frames()[3].sp);
  EXPECT_EQ(0xed761129U, unwinder.frames()[4].pc);
  EXPECT_EQ(0xff85d2e8U, unwinder.frames()[4].sp);
  EXPECT_EQ(0xed3b97a9U, unwinder.frames()[5].pc);
  EXPECT_EQ(0xff85d370U, unwinder.frames()[5].sp);
  EXPECT_EQ(0xed541833U, unwinder.frames()[6].pc);
  EXPECT_EQ(0xff85d3d8U, unwinder.frames()[6].sp);
  EXPECT_EQ(0xed528935U, unwinder.frames()[7].pc);
  EXPECT_EQ(0xff85d428U, unwinder.frames()[7].sp);
  EXPECT_EQ(0xed52971dU, unwinder.frames()[8].pc);
  EXPECT_EQ(0xff85d470U, unwinder.frames()[8].sp);
  EXPECT_EQ(0xed73c865U, unwinder.frames()[9].pc);
  EXPECT_EQ(0xff85d4b0U, unwinder.frames()[9].sp);
  EXPECT_EQ(0xed7606ffU, unwinder.frames()[10].pc);
  EXPECT_EQ(0xff85d5d0U, unwinder.frames()[10].sp);
  EXPECT_EQ(0xe27a7c31U, unwinder.frames()[11].pc);
  EXPECT_EQ(0xff85d640U, unwinder.frames()[11].sp);
  EXPECT_EQ(0xed75c175U, unwinder.frames()[12].pc);
  EXPECT_EQ(0xff85d660U, unwinder.frames()[12].sp);
  EXPECT_EQ(0xed761129U, unwinder.frames()[13].pc);
  EXPECT_EQ(0xff85d698U, unwinder.frames()[13].sp);
  EXPECT_EQ(0xed3b97a9U, unwinder.frames()[14].pc);
  EXPECT_EQ(0xff85d720U, unwinder.frames()[14].sp);
  EXPECT_EQ(0xed541833U, unwinder.frames()[15].pc);
  EXPECT_EQ(0xff85d788U, unwinder.frames()[15].sp);
  EXPECT_EQ(0xed528935U, unwinder.frames()[16].pc);
  EXPECT_EQ(0xff85d7d8U, unwinder.frames()[16].sp);
  EXPECT_EQ(0xed52971dU, unwinder.frames()[17].pc);
  EXPECT_EQ(0xff85d820U, unwinder.frames()[17].sp);
  EXPECT_EQ(0xed73c865U, unwinder.frames()[18].pc);
  EXPECT_EQ(0xff85d860U, unwinder.frames()[18].sp);
  EXPECT_EQ(0xed7606ffU, unwinder.frames()[19].pc);
  EXPECT_EQ(0xff85d970U, unwinder.frames()[19].sp);
  EXPECT_EQ(0xe27a7b77U, unwinder.frames()[20].pc);
  EXPECT_EQ(0xff85d9e0U, unwinder.frames()[20].sp);
  EXPECT_EQ(0xed75c175U, unwinder.frames()[21].pc);
  EXPECT_EQ(0xff85da10U, unwinder.frames()[21].sp);
  EXPECT_EQ(0xed761129U, unwinder.frames()[22].pc);
  EXPECT_EQ(0xff85da48U, unwinder.frames()[22].sp);
  EXPECT_EQ(0xed3b97a9U, unwinder.frames()[23].pc);
  EXPECT_EQ(0xff85dad0U, unwinder.frames()[23].sp);
  EXPECT_EQ(0xed541833U, unwinder.frames()[24].pc);
  EXPECT_EQ(0xff85db38U, unwinder.frames()[24].sp);
  EXPECT_EQ(0xed528935U, unwinder.frames()[25].pc);
  EXPECT_EQ(0xff85db88U, unwinder.frames()[25].sp);
  EXPECT_EQ(0xed52971dU, unwinder.frames()[26].pc);
  EXPECT_EQ(0xff85dbd0U, unwinder.frames()[26].sp);
  EXPECT_EQ(0xed73c865U, unwinder.frames()[27].pc);
  EXPECT_EQ(0xff85dc10U, unwinder.frames()[27].sp);
  EXPECT_EQ(0xed7606ffU, unwinder.frames()[28].pc);
  EXPECT_EQ(0xff85dd20U, unwinder.frames()[28].sp);
  EXPECT_EQ(0xe27a7a29U, unwinder.frames()[29].pc);
  EXPECT_EQ(0xff85dd90U, unwinder.frames()[29].sp);
  EXPECT_EQ(0xed75c175U, unwinder.frames()[30].pc);
  EXPECT_EQ(0xff85ddc0U, unwinder.frames()[30].sp);
  EXPECT_EQ(0xed76122fU, unwinder.frames()[31].pc);
  EXPECT_EQ(0xff85de08U, unwinder.frames()[31].sp);
  EXPECT_EQ(0xed3b97bbU, unwinder.frames()[32].pc);
  EXPECT_EQ(0xff85de90U, unwinder.frames()[32].sp);
  EXPECT_EQ(0xed541833U, unwinder.frames()[33].pc);
  EXPECT_EQ(0xff85def8U, unwinder.frames()[33].sp);
  EXPECT_EQ(0xed528935U, unwinder.frames()[34].pc);
  EXPECT_EQ(0xff85df48U, unwinder.frames()[34].sp);
  EXPECT_EQ(0xed52971dU, unwinder.frames()[35].pc);
  EXPECT_EQ(0xff85df90U, unwinder.frames()[35].sp);
  EXPECT_EQ(0xed73c865U, unwinder.frames()[36].pc);
  EXPECT_EQ(0xff85dfd0U, unwinder.frames()[36].sp);
  EXPECT_EQ(0xed7606ffU, unwinder.frames()[37].pc);
  EXPECT_EQ(0xff85e110U, unwinder.frames()[37].sp);
  EXPECT_EQ(0xe27a739bU, unwinder.frames()[38].pc);
  EXPECT_EQ(0xff85e180U, unwinder.frames()[38].sp);
  EXPECT_EQ(0xed75c175U, unwinder.frames()[39].pc);
  EXPECT_EQ(0xff85e1b0U, unwinder.frames()[39].sp);
  EXPECT_EQ(0xed761129U, unwinder.frames()[40].pc);
  EXPECT_EQ(0xff85e1e0U, unwinder.frames()[40].sp);
  EXPECT_EQ(0xed3b97a9U, unwinder.frames()[41].pc);
  EXPECT_EQ(0xff85e268U, unwinder.frames()[41].sp);
  EXPECT_EQ(0xed541833U, unwinder.frames()[42].pc);
  EXPECT_EQ(0xff85e2d0U, unwinder.frames()[42].sp);
  EXPECT_EQ(0xed528935U, unwinder.frames()[43].pc);
  EXPECT_EQ(0xff85e320U, unwinder.frames()[43].sp);
  EXPECT_EQ(0xed52971dU, unwinder.frames()[44].pc);
  EXPECT_EQ(0xff85e368U, unwinder.frames()[44].sp);
  EXPECT_EQ(0xed73c865U, unwinder.frames()[45].pc);
  EXPECT_EQ(0xff85e3a8U, unwinder.frames()[45].sp);
  EXPECT_EQ(0xed7606ffU, unwinder.frames()[46].pc);
  EXPECT_EQ(0xff85e4c0U, unwinder.frames()[46].sp);
  EXPECT_EQ(0xe27a6aa7U, unwinder.frames()[47].pc);
  EXPECT_EQ(0xff85e530U, unwinder.frames()[47].sp);
  EXPECT_EQ(0xed75c175U, unwinder.frames()[48].pc);
  EXPECT_EQ(0xff85e5a0U, unwinder.frames()[48].sp);
  EXPECT_EQ(0xed761129U, unwinder.frames()[49].pc);
  EXPECT_EQ(0xff85e5d8U, unwinder.frames()[49].sp);
  EXPECT_EQ(0xed3b97a9U, unwinder.frames()[50].pc);
  EXPECT_EQ(0xff85e660U, unwinder.frames()[50].sp);
  EXPECT_EQ(0xed541833U, unwinder.frames()[51].pc);
  EXPECT_EQ(0xff85e6c8U, unwinder.frames()[51].sp);
  EXPECT_EQ(0xed528935U, unwinder.frames()[52].pc);
  EXPECT_EQ(0xff85e718U, unwinder.frames()[52].sp);
  EXPECT_EQ(0xed52971dU, unwinder.frames()[53].pc);
  EXPECT_EQ(0xff85e760U, unwinder.frames()[53].sp);
  EXPECT_EQ(0xed73c865U, unwinder.frames()[54].pc);
  EXPECT_EQ(0xff85e7a0U, unwinder.frames()[54].sp);
  EXPECT_EQ(0xed7606ffU, unwinder.frames()[55].pc);
  EXPECT_EQ(0xff85e8f0U, unwinder.frames()[55].sp);
  EXPECT_EQ(0xe27a1a99U, unwinder.frames()[56].pc);
  EXPECT_EQ(0xff85e960U, unwinder.frames()[56].sp);
  EXPECT_EQ(0xed75c175U, unwinder.frames()[57].pc);
  EXPECT_EQ(0xff85e990U, unwinder.frames()[57].sp);
  EXPECT_EQ(0xed76122fU, unwinder.frames()[58].pc);
  EXPECT_EQ(0xff85e9c8U, unwinder.frames()[58].sp);
  EXPECT_EQ(0xed3b97bbU, unwinder.frames()[59].pc);
  EXPECT_EQ(0xff85ea50U, unwinder.frames()[59].sp);
  EXPECT_EQ(0xed541833U, unwinder.frames()[60].pc);
  EXPECT_EQ(0xff85eab8U, unwinder.frames()[60].sp);
  EXPECT_EQ(0xed528935U, unwinder.frames()[61].pc);
  EXPECT_EQ(0xff85eb08U, unwinder.frames()[61].sp);
  EXPECT_EQ(0xed52971dU, unwinder.frames()[62].pc);
  EXPECT_EQ(0xff85eb50U, unwinder.frames()[62].sp);
  EXPECT_EQ(0xed73c865U, unwinder.frames()[63].pc);
  EXPECT_EQ(0xff85eb90U, unwinder.frames()[63].sp);
  EXPECT_EQ(0xed7606ffU, unwinder.frames()[64].pc);
  EXPECT_EQ(0xff85ec90U, unwinder.frames()[64].sp);
  EXPECT_EQ(0xed75c175U, unwinder.frames()[65].pc);
  EXPECT_EQ(0xff85ed00U, unwinder.frames()[65].sp);
  EXPECT_EQ(0xed76122fU, unwinder.frames()[66].pc);
  EXPECT_EQ(0xff85ed38U, unwinder.frames()[66].sp);
  EXPECT_EQ(0xed3b97bbU, unwinder.frames()[67].pc);
  EXPECT_EQ(0xff85edc0U, unwinder.frames()[67].sp);
  EXPECT_EQ(0xed6ac92dU, unwinder.frames()[68].pc);
  EXPECT_EQ(0xff85ee28U, unwinder.frames()[68].sp);
  EXPECT_EQ(0xed6ac6c3U, unwinder.frames()[69].pc);
  EXPECT_EQ(0xff85eeb8U, unwinder.frames()[69].sp);
  EXPECT_EQ(0xed602411U, unwinder.frames()[70].pc);
  EXPECT_EQ(0xff85ef48U, unwinder.frames()[70].sp);
  EXPECT_EQ(0xed3e0a9fU, unwinder.frames()[71].pc);
  EXPECT_EQ(0xff85ef90U, unwinder.frames()[71].sp);
  EXPECT_EQ(0xed3db9b9U, unwinder.frames()[72].pc);
  EXPECT_EQ(0xff85f008U, unwinder.frames()[72].sp);
  EXPECT_EQ(0xab0d459fU, unwinder.frames()[73].pc);
  EXPECT_EQ(0xff85f038U, unwinder.frames()[73].sp);
  EXPECT_EQ(0xab0d4349U, unwinder.frames()[74].pc);
  EXPECT_EQ(0xff85f050U, unwinder.frames()[74].sp);
  EXPECT_EQ(0xedb0d0c9U, unwinder.frames()[75].pc);
  EXPECT_EQ(0xff85f0c0U, unwinder.frames()[75].sp);
}

struct LeakType {
  LeakType(Maps* maps, Regs* regs, std::shared_ptr<Memory>& process_memory)
      : maps(maps), regs(regs), process_memory(process_memory) {}

  Maps* maps;
  Regs* regs;
  std::shared_ptr<Memory>& process_memory;
};

static void OfflineUnwind(void* data) {
  LeakType* leak_data = reinterpret_cast<LeakType*>(data);

  std::unique_ptr<Regs> regs_copy(leak_data->regs->Clone());
  std::unique_ptr<JitDebug> jit_debug =
      CreateJitDebug(leak_data->regs->Arch(), leak_data->process_memory);
  Unwinder unwinder(128, leak_data->maps, regs_copy.get(), leak_data->process_memory);
  unwinder.SetJitDebug(jit_debug.get());
  unwinder.Unwind();
  ASSERT_EQ(76U, unwinder.NumFrames());
}

TEST_F(UnwindOfflineTest, unwind_offline_check_for_leaks) {
  std::string error_msg;
  if (!offline_utils_.Init("jit_debug_arm/", ARCH_ARM, &error_msg,
                           ProcessMemoryFlag::kIncludeJitMemory))
    FAIL() << error_msg;

  std::shared_ptr<Memory> process_memory = offline_utils_.GetProcessMemory();
  LeakType data(offline_utils_.GetMaps(), offline_utils_.GetRegs(), process_memory);
  TestCheckForLeaks(OfflineUnwind, &data);
}

// The eh_frame_hdr data is present but set to zero fdes. This should
// fallback to iterating over the cies/fdes and ignore the eh_frame_hdr.
// No .gnu_debugdata section in the elf file, so no symbols.
TEST_F(UnwindOfflineTest, bad_eh_frame_hdr_arm64) {
  std::string error_msg;
  if (!offline_utils_.Init("bad_eh_frame_hdr_arm64/", ARCH_ARM64, &error_msg)) FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(5U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 0000000000000550  waiter64\n"
      "  #01 pc 0000000000000568  waiter64\n"
      "  #02 pc 000000000000057c  waiter64\n"
      "  #03 pc 0000000000000590  waiter64\n"
      "  #04 pc 00000000000a8e98  libc.so (__libc_init+88)\n",
      frame_info);
  EXPECT_EQ(0x60a9fdf550U, unwinder.frames()[0].pc);
  EXPECT_EQ(0x7fdd141990U, unwinder.frames()[0].sp);
  EXPECT_EQ(0x60a9fdf568U, unwinder.frames()[1].pc);
  EXPECT_EQ(0x7fdd1419a0U, unwinder.frames()[1].sp);
  EXPECT_EQ(0x60a9fdf57cU, unwinder.frames()[2].pc);
  EXPECT_EQ(0x7fdd1419b0U, unwinder.frames()[2].sp);
  EXPECT_EQ(0x60a9fdf590U, unwinder.frames()[3].pc);
  EXPECT_EQ(0x7fdd1419c0U, unwinder.frames()[3].sp);
  EXPECT_EQ(0x7542d68e98U, unwinder.frames()[4].pc);
  EXPECT_EQ(0x7fdd1419d0U, unwinder.frames()[4].sp);
}

// The elf has bad eh_frame unwind information for the pcs. If eh_frame
// is used first, the unwind will not match the expected output.
TEST_F(UnwindOfflineTest, debug_frame_first_x86) {
  std::string error_msg;
  if (!offline_utils_.Init("debug_frame_first_x86/", ARCH_X86, &error_msg)) FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(5U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 00000685  waiter (call_level3+53)\n"
      "  #01 pc 000006b7  waiter (call_level2+23)\n"
      "  #02 pc 000006d7  waiter (call_level1+23)\n"
      "  #03 pc 000006f7  waiter (main+23)\n"
      "  #04 pc 00018275  libc.so\n",
      frame_info);
  EXPECT_EQ(0x56598685U, unwinder.frames()[0].pc);
  EXPECT_EQ(0xffcf9e38U, unwinder.frames()[0].sp);
  EXPECT_EQ(0x565986b7U, unwinder.frames()[1].pc);
  EXPECT_EQ(0xffcf9e50U, unwinder.frames()[1].sp);
  EXPECT_EQ(0x565986d7U, unwinder.frames()[2].pc);
  EXPECT_EQ(0xffcf9e60U, unwinder.frames()[2].sp);
  EXPECT_EQ(0x565986f7U, unwinder.frames()[3].pc);
  EXPECT_EQ(0xffcf9e70U, unwinder.frames()[3].sp);
  EXPECT_EQ(0xf744a275U, unwinder.frames()[4].pc);
  EXPECT_EQ(0xffcf9e80U, unwinder.frames()[4].sp);
}

// Make sure that a pc that is at the beginning of an fde unwinds correctly.
TEST_F(UnwindOfflineTest, eh_frame_hdr_begin_x86_64) {
  std::string error_msg;
  if (!offline_utils_.Init("eh_frame_hdr_begin_x86_64/", ARCH_X86_64, &error_msg))
    FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(5U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 0000000000000a80  unwind_test64 (calling3)\n"
      "  #01 pc 0000000000000dd9  unwind_test64 (calling2+633)\n"
      "  #02 pc 000000000000121e  unwind_test64 (calling1+638)\n"
      "  #03 pc 00000000000013ed  unwind_test64 (main+13)\n"
      "  #04 pc 00000000000202b0  libc.so\n",
      frame_info);
  EXPECT_EQ(0x561550b17a80U, unwinder.frames()[0].pc);
  EXPECT_EQ(0x7ffcc8596ce8U, unwinder.frames()[0].sp);
  EXPECT_EQ(0x561550b17dd9U, unwinder.frames()[1].pc);
  EXPECT_EQ(0x7ffcc8596cf0U, unwinder.frames()[1].sp);
  EXPECT_EQ(0x561550b1821eU, unwinder.frames()[2].pc);
  EXPECT_EQ(0x7ffcc8596f40U, unwinder.frames()[2].sp);
  EXPECT_EQ(0x561550b183edU, unwinder.frames()[3].pc);
  EXPECT_EQ(0x7ffcc8597190U, unwinder.frames()[3].sp);
  EXPECT_EQ(0x7f4de62162b0U, unwinder.frames()[4].pc);
  EXPECT_EQ(0x7ffcc85971a0U, unwinder.frames()[4].sp);
}

TEST_F(UnwindOfflineTest, art_quick_osr_stub_arm) {
  std::string error_msg;
  if (!offline_utils_.Init("art_quick_osr_stub_arm/", ARCH_ARM, &error_msg,
                           ProcessMemoryFlag::kIncludeJitMemory))
    FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.SetJitDebug(offline_utils_.GetJitDebug());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(25U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 0000c788  <anonymous:d0250000> "
      "(com.example.simpleperf.simpleperfexamplewithnative.MixActivity.access$000)\n"
      "  #01 pc 0000cdd5  <anonymous:d0250000> "
      "(com.example.simpleperf.simpleperfexamplewithnative.MixActivity$1.run+60)\n"
      "  #02 pc 004135bb  libart.so (art_quick_osr_stub+42)\n"
      "  #03 pc 002657a5  libart.so "
      "(art::jit::Jit::MaybeDoOnStackReplacement(art::Thread*, art::ArtMethod*, unsigned int, int, "
      "art::JValue*)+876)\n"
      "  #04 pc 004021a7  libart.so (MterpMaybeDoOnStackReplacement+86)\n"
      "  #05 pc 00412474  libart.so (ExecuteMterpImpl+66164)\n"
      "  #06 pc cd8365b0  <unknown>\n"  // symbol in dex file
      "  #07 pc 001d7f1b  libart.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+374)\n"
      "  #08 pc 001dc593  libart.so "
      "(art::interpreter::ArtInterpreterToInterpreterBridge(art::Thread*, "
      "art::CodeItemDataAccessor const&, art::ShadowFrame*, art::JValue*)+154)\n"
      "  #09 pc 001f4d01  libart.so "
      "(bool art::interpreter::DoCall<false, false>(art::ArtMethod*, art::Thread*, "
      "art::ShadowFrame&, art::Instruction const*, unsigned short, art::JValue*)+732)\n"
      "  #10 pc 003fe427  libart.so (MterpInvokeInterface+1354)\n"
      "  #11 pc 00405b94  libart.so (ExecuteMterpImpl+14740)\n"
      "  #12 pc 7004873e  <unknown>\n"  // symbol in dex file
      "  #13 pc 001d7f1b  libart.so "
      "(art::interpreter::Execute(art::Thread*, art::CodeItemDataAccessor const&, "
      "art::ShadowFrame&, art::JValue, bool)+374)\n"
      "  #14 pc 001dc4d5  libart.so "
      "(art::interpreter::EnterInterpreterFromEntryPoint(art::Thread*, art::CodeItemDataAccessor "
      "const&, art::ShadowFrame*)+92)\n"
      "  #15 pc 003f25ab  libart.so (artQuickToInterpreterBridge+970)\n"
      "  #16 pc 00417aff  libart.so (art_quick_to_interpreter_bridge+30)\n"
      "  #17 pc 00413575  libart.so (art_quick_invoke_stub_internal+68)\n"
      "  #18 pc 00418531  libart.so (art_quick_invoke_stub+236)\n"
      "  #19 pc 000b468d  libart.so (art::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned "
      "int, art::JValue*, char const*)+136)\n"
      "  #20 pc 00362f49  libart.so "
      "(art::(anonymous namespace)::InvokeWithArgArray(art::ScopedObjectAccessAlreadyRunnable "
      "const&, art::ArtMethod*, art::(anonymous namespace)::ArgArray*, art::JValue*, char "
      "const*)+52)\n"
      "  #21 pc 00363cd9  libart.so "
      "(art::InvokeVirtualOrInterfaceWithJValues(art::ScopedObjectAccessAlreadyRunnable const&, "
      "_jobject*, _jmethodID*, jvalue*)+332)\n"
      "  #22 pc 003851dd  libart.so (art::Thread::CreateCallback(void*)+868)\n"
      "  #23 pc 00062925  libc.so (__pthread_start(void*)+22)\n"
      "  #24 pc 0001de39  libc.so (__start_thread+24)\n",
      frame_info);
  EXPECT_EQ(0xd025c788U, unwinder.frames()[0].pc);
  EXPECT_EQ(0xcd4ff140U, unwinder.frames()[0].sp);
  EXPECT_EQ(0xd025cdd5U, unwinder.frames()[1].pc);
  EXPECT_EQ(0xcd4ff140U, unwinder.frames()[1].sp);
  EXPECT_EQ(0xe4a755bbU, unwinder.frames()[2].pc);
  EXPECT_EQ(0xcd4ff160U, unwinder.frames()[2].sp);
  EXPECT_EQ(0xe48c77a5U, unwinder.frames()[3].pc);
  EXPECT_EQ(0xcd4ff190U, unwinder.frames()[3].sp);
  EXPECT_EQ(0xe4a641a7U, unwinder.frames()[4].pc);
  EXPECT_EQ(0xcd4ff298U, unwinder.frames()[4].sp);
  EXPECT_EQ(0xe4a74474U, unwinder.frames()[5].pc);
  EXPECT_EQ(0xcd4ff2b8U, unwinder.frames()[5].sp);
  EXPECT_EQ(0xcd8365b0U, unwinder.frames()[6].pc);
  EXPECT_EQ(0xcd4ff2e0U, unwinder.frames()[6].sp);
  EXPECT_EQ(0xe4839f1bU, unwinder.frames()[7].pc);
  EXPECT_EQ(0xcd4ff2e0U, unwinder.frames()[7].sp);
  EXPECT_EQ(0xe483e593U, unwinder.frames()[8].pc);
  EXPECT_EQ(0xcd4ff330U, unwinder.frames()[8].sp);
  EXPECT_EQ(0xe4856d01U, unwinder.frames()[9].pc);
  EXPECT_EQ(0xcd4ff380U, unwinder.frames()[9].sp);
  EXPECT_EQ(0xe4a60427U, unwinder.frames()[10].pc);
  EXPECT_EQ(0xcd4ff430U, unwinder.frames()[10].sp);
  EXPECT_EQ(0xe4a67b94U, unwinder.frames()[11].pc);
  EXPECT_EQ(0xcd4ff498U, unwinder.frames()[11].sp);
  EXPECT_EQ(0x7004873eU, unwinder.frames()[12].pc);
  EXPECT_EQ(0xcd4ff4c0U, unwinder.frames()[12].sp);
  EXPECT_EQ(0xe4839f1bU, unwinder.frames()[13].pc);
  EXPECT_EQ(0xcd4ff4c0U, unwinder.frames()[13].sp);
  EXPECT_EQ(0xe483e4d5U, unwinder.frames()[14].pc);
  EXPECT_EQ(0xcd4ff510U, unwinder.frames()[14].sp);
  EXPECT_EQ(0xe4a545abU, unwinder.frames()[15].pc);
  EXPECT_EQ(0xcd4ff538U, unwinder.frames()[15].sp);
  EXPECT_EQ(0xe4a79affU, unwinder.frames()[16].pc);
  EXPECT_EQ(0xcd4ff640U, unwinder.frames()[16].sp);
  EXPECT_EQ(0xe4a75575U, unwinder.frames()[17].pc);
  EXPECT_EQ(0xcd4ff6b0U, unwinder.frames()[17].sp);
  EXPECT_EQ(0xe4a7a531U, unwinder.frames()[18].pc);
  EXPECT_EQ(0xcd4ff6e8U, unwinder.frames()[18].sp);
  EXPECT_EQ(0xe471668dU, unwinder.frames()[19].pc);
  EXPECT_EQ(0xcd4ff770U, unwinder.frames()[19].sp);
  EXPECT_EQ(0xe49c4f49U, unwinder.frames()[20].pc);
  EXPECT_EQ(0xcd4ff7c8U, unwinder.frames()[20].sp);
  EXPECT_EQ(0xe49c5cd9U, unwinder.frames()[21].pc);
  EXPECT_EQ(0xcd4ff850U, unwinder.frames()[21].sp);
  EXPECT_EQ(0xe49e71ddU, unwinder.frames()[22].pc);
  EXPECT_EQ(0xcd4ff8e8U, unwinder.frames()[22].sp);
  EXPECT_EQ(0xe7df3925U, unwinder.frames()[23].pc);
  EXPECT_EQ(0xcd4ff958U, unwinder.frames()[23].sp);
  EXPECT_EQ(0xe7daee39U, unwinder.frames()[24].pc);
  EXPECT_EQ(0xcd4ff960U, unwinder.frames()[24].sp);
}

TEST_F(UnwindOfflineTest, jit_map_arm) {
  std::string error_msg;
  if (!offline_utils_.Init("jit_map_arm/", ARCH_ARM, &error_msg)) FAIL() << error_msg;

  Maps* maps = offline_utils_.GetMaps();
  maps->Add(0xd025c788, 0xd025c9f0, 0, PROT_READ | PROT_EXEC | MAPS_FLAGS_JIT_SYMFILE_MAP,
            "jit_map0.so", 0);
  maps->Add(0xd025cd98, 0xd025cff4, 0, PROT_READ | PROT_EXEC | MAPS_FLAGS_JIT_SYMFILE_MAP,
            "jit_map1.so", 0);
  maps->Sort();

  Unwinder unwinder(128, maps, offline_utils_.GetRegs(), offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(6U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 00000000  jit_map0.so "
      "(com.example.simpleperf.simpleperfexamplewithnative.MixActivity.access$000)\n"
      "  #01 pc 0000003d  jit_map1.so "
      "(com.example.simpleperf.simpleperfexamplewithnative.MixActivity$1.run+60)\n"
      "  #02 pc 004135bb  libart.so (art_quick_osr_stub+42)\n"

      "  #03 pc 003851dd  libart.so (art::Thread::CreateCallback(void*)+868)\n"
      "  #04 pc 00062925  libc.so (__pthread_start(void*)+22)\n"
      "  #05 pc 0001de39  libc.so (__start_thread+24)\n",
      frame_info);

  EXPECT_EQ(0xd025c788U, unwinder.frames()[0].pc);
  EXPECT_EQ(0xcd4ff140U, unwinder.frames()[0].sp);
  EXPECT_EQ(0xd025cdd5U, unwinder.frames()[1].pc);
  EXPECT_EQ(0xcd4ff140U, unwinder.frames()[1].sp);
  EXPECT_EQ(0xe4a755bbU, unwinder.frames()[2].pc);
  EXPECT_EQ(0xcd4ff160U, unwinder.frames()[2].sp);
  EXPECT_EQ(0xe49e71ddU, unwinder.frames()[3].pc);
  EXPECT_EQ(0xcd4ff8e8U, unwinder.frames()[3].sp);
  EXPECT_EQ(0xe7df3925U, unwinder.frames()[4].pc);
  EXPECT_EQ(0xcd4ff958U, unwinder.frames()[4].sp);
  EXPECT_EQ(0xe7daee39U, unwinder.frames()[5].pc);
  EXPECT_EQ(0xcd4ff960U, unwinder.frames()[5].sp);
}

TEST_F(UnwindOfflineTest, offset_arm) {
  std::string error_msg;
  if (!offline_utils_.Init("offset_arm/", ARCH_ARM, &error_msg)) FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(19U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 0032bfa0  libunwindstack_test (SignalInnerFunction+40)\n"
      "  #01 pc 0032bfeb  libunwindstack_test (SignalMiddleFunction+2)\n"
      "  #02 pc 0032bff3  libunwindstack_test (SignalOuterFunction+2)\n"
      "  #03 pc 0032fed3  libunwindstack_test "
      "(unwindstack::SignalCallerHandler(int, siginfo*, void*)+26)\n"
      "  #04 pc 0002652c  libc.so (__restore)\n"
      "  #05 pc 00000000  <unknown>\n"
      "  #06 pc 0032c2d9  libunwindstack_test (InnerFunction+736)\n"
      "  #07 pc 0032cc4f  libunwindstack_test (MiddleFunction+42)\n"
      "  #08 pc 0032cc81  libunwindstack_test (OuterFunction+42)\n"
      "  #09 pc 0032e547  libunwindstack_test "
      "(unwindstack::RemoteThroughSignal(int, unsigned int)+270)\n"
      "  #10 pc 0032ed99  libunwindstack_test "
      "(unwindstack::UnwindTest_remote_through_signal_with_invalid_func_Test::TestBody()+16)\n"
      "  #11 pc 00354453  libunwindstack_test (testing::Test::Run()+154)\n"
      "  #12 pc 00354de7  libunwindstack_test (testing::TestInfo::Run()+194)\n"
      "  #13 pc 00355105  libunwindstack_test (testing::TestCase::Run()+180)\n"
      "  #14 pc 0035a215  libunwindstack_test "
      "(testing::internal::UnitTestImpl::RunAllTests()+664)\n"
      "  #15 pc 00359f4f  libunwindstack_test (testing::UnitTest::Run()+110)\n"
      "  #16 pc 0034d3db  libunwindstack_test (main+38)\n"
      "  #17 pc 00092c0d  libc.so (__libc_init+48)\n"
      "  #18 pc 0004202f  libunwindstack_test (_start_main+38)\n",
      frame_info);

  EXPECT_EQ(0x2e55fa0U, unwinder.frames()[0].pc);
  EXPECT_EQ(0xf43d2cccU, unwinder.frames()[0].sp);
  EXPECT_EQ(0x2e55febU, unwinder.frames()[1].pc);
  EXPECT_EQ(0xf43d2ce0U, unwinder.frames()[1].sp);
  EXPECT_EQ(0x2e55ff3U, unwinder.frames()[2].pc);
  EXPECT_EQ(0xf43d2ce8U, unwinder.frames()[2].sp);
  EXPECT_EQ(0x2e59ed3U, unwinder.frames()[3].pc);
  EXPECT_EQ(0xf43d2cf0U, unwinder.frames()[3].sp);
  EXPECT_EQ(0xf413652cU, unwinder.frames()[4].pc);
  EXPECT_EQ(0xf43d2d10U, unwinder.frames()[4].sp);
  EXPECT_EQ(0U, unwinder.frames()[5].pc);
  EXPECT_EQ(0xffcc0ee0U, unwinder.frames()[5].sp);
  EXPECT_EQ(0x2e562d9U, unwinder.frames()[6].pc);
  EXPECT_EQ(0xffcc0ee0U, unwinder.frames()[6].sp);
  EXPECT_EQ(0x2e56c4fU, unwinder.frames()[7].pc);
  EXPECT_EQ(0xffcc1060U, unwinder.frames()[7].sp);
  EXPECT_EQ(0x2e56c81U, unwinder.frames()[8].pc);
  EXPECT_EQ(0xffcc1078U, unwinder.frames()[8].sp);
  EXPECT_EQ(0x2e58547U, unwinder.frames()[9].pc);
  EXPECT_EQ(0xffcc1090U, unwinder.frames()[9].sp);
  EXPECT_EQ(0x2e58d99U, unwinder.frames()[10].pc);
  EXPECT_EQ(0xffcc1438U, unwinder.frames()[10].sp);
  EXPECT_EQ(0x2e7e453U, unwinder.frames()[11].pc);
  EXPECT_EQ(0xffcc1448U, unwinder.frames()[11].sp);
  EXPECT_EQ(0x2e7ede7U, unwinder.frames()[12].pc);
  EXPECT_EQ(0xffcc1458U, unwinder.frames()[12].sp);
  EXPECT_EQ(0x2e7f105U, unwinder.frames()[13].pc);
  EXPECT_EQ(0xffcc1490U, unwinder.frames()[13].sp);
  EXPECT_EQ(0x2e84215U, unwinder.frames()[14].pc);
  EXPECT_EQ(0xffcc14c0U, unwinder.frames()[14].sp);
  EXPECT_EQ(0x2e83f4fU, unwinder.frames()[15].pc);
  EXPECT_EQ(0xffcc1510U, unwinder.frames()[15].sp);
  EXPECT_EQ(0x2e773dbU, unwinder.frames()[16].pc);
  EXPECT_EQ(0xffcc1528U, unwinder.frames()[16].sp);
  EXPECT_EQ(0xf41a2c0dU, unwinder.frames()[17].pc);
  EXPECT_EQ(0xffcc1540U, unwinder.frames()[17].sp);
  EXPECT_EQ(0x2b6c02fU, unwinder.frames()[18].pc);
  EXPECT_EQ(0xffcc1558U, unwinder.frames()[18].sp);
}

// Test using a non-zero load bias library that has the fde entries
// encoded as 0xb, which is not set as pc relative.
TEST_F(UnwindOfflineTest, debug_frame_load_bias_arm) {
  std::string error_msg;
  if (!offline_utils_.Init("debug_frame_load_bias_arm/", ARCH_ARM, &error_msg)) FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(8U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 0005138c  libc.so (__ioctl+8)\n"
      "  #01 pc 0002140f  libc.so (ioctl+30)\n"
      "  #02 pc 00039535  libbinder.so (android::IPCThreadState::talkWithDriver(bool)+204)\n"
      "  #03 pc 00039633  libbinder.so (android::IPCThreadState::getAndExecuteCommand()+10)\n"
      "  #04 pc 00039b57  libbinder.so (android::IPCThreadState::joinThreadPool(bool)+38)\n"
      "  #05 pc 00000c21  mediaserver (main+104)\n"
      "  #06 pc 00084b89  libc.so (__libc_init+48)\n"
      "  #07 pc 00000b77  mediaserver (_start_main+38)\n",
      frame_info);

  EXPECT_EQ(0xf0be238cU, unwinder.frames()[0].pc);
  EXPECT_EQ(0xffd4a638U, unwinder.frames()[0].sp);
  EXPECT_EQ(0xf0bb240fU, unwinder.frames()[1].pc);
  EXPECT_EQ(0xffd4a638U, unwinder.frames()[1].sp);
  EXPECT_EQ(0xf1a75535U, unwinder.frames()[2].pc);
  EXPECT_EQ(0xffd4a650U, unwinder.frames()[2].sp);
  EXPECT_EQ(0xf1a75633U, unwinder.frames()[3].pc);
  EXPECT_EQ(0xffd4a6b0U, unwinder.frames()[3].sp);
  EXPECT_EQ(0xf1a75b57U, unwinder.frames()[4].pc);
  EXPECT_EQ(0xffd4a6d0U, unwinder.frames()[4].sp);
  EXPECT_EQ(0x8d1cc21U, unwinder.frames()[5].pc);
  EXPECT_EQ(0xffd4a6e8U, unwinder.frames()[5].sp);
  EXPECT_EQ(0xf0c15b89U, unwinder.frames()[6].pc);
  EXPECT_EQ(0xffd4a700U, unwinder.frames()[6].sp);
  EXPECT_EQ(0x8d1cb77U, unwinder.frames()[7].pc);
  EXPECT_EQ(0xffd4a718U, unwinder.frames()[7].sp);
}

TEST_F(UnwindOfflineTest, shared_lib_in_apk_arm64) {
  std::string error_msg;
  if (!offline_utils_.Init("shared_lib_in_apk_arm64/", ARCH_ARM64, &error_msg)) FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(7U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 000000000014ccbc  linker64 (__dl_syscall+28)\n"
      "  #01 pc 000000000005426c  linker64 "
      "(__dl__ZL24debuggerd_signal_handleriP7siginfoPv+1128)\n"
      "  #02 pc 00000000000008c0  vdso.so (__kernel_rt_sigreturn)\n"
      "  #03 pc 00000000000846f4  libc.so (abort+172)\n"
      "  #04 pc 0000000000084ad4  libc.so (__assert2+36)\n"
      "  #05 pc 000000000003d5b4  ANGLEPrebuilt.apk!libfeature_support_angle.so (offset 0x4000) "
      "(ANGLEGetUtilityAPI+56)\n"
      "  #06 pc 000000000007fe68  libc.so (__libc_init)\n",
      frame_info);

  EXPECT_EQ(0x7e82c4fcbcULL, unwinder.frames()[0].pc);
  EXPECT_EQ(0x7df8ca3bf0ULL, unwinder.frames()[0].sp);
  EXPECT_EQ(0x7e82b5726cULL, unwinder.frames()[1].pc);
  EXPECT_EQ(0x7df8ca3bf0ULL, unwinder.frames()[1].sp);
  EXPECT_EQ(0x7e82b018c0ULL, unwinder.frames()[2].pc);
  EXPECT_EQ(0x7df8ca3da0ULL, unwinder.frames()[2].sp);
  EXPECT_EQ(0x7e7eecc6f4ULL, unwinder.frames()[3].pc);
  EXPECT_EQ(0x7dabf3db60ULL, unwinder.frames()[3].sp);
  EXPECT_EQ(0x7e7eeccad4ULL, unwinder.frames()[4].pc);
  EXPECT_EQ(0x7dabf3dc40ULL, unwinder.frames()[4].sp);
  EXPECT_EQ(0x7dabc405b4ULL, unwinder.frames()[5].pc);
  EXPECT_EQ(0x7dabf3dc50ULL, unwinder.frames()[5].sp);
  EXPECT_EQ(0x7e7eec7e68ULL, unwinder.frames()[6].pc);
  EXPECT_EQ(0x7dabf3dc70ULL, unwinder.frames()[6].sp);
  // Ignore top frame since the test code was modified to end in __libc_init.
}

TEST_F(UnwindOfflineTest, shared_lib_in_apk_memory_only_arm64) {
  std::string error_msg;
  if (!offline_utils_.Init("shared_lib_in_apk_memory_only_arm64/", ARCH_ARM64, &error_msg))
    FAIL() << error_msg;
  // Add the memory that represents the shared library.

  std::shared_ptr<Memory> process_memory = offline_utils_.GetProcessMemory();
  MemoryOfflineParts* memory = reinterpret_cast<MemoryOfflineParts*>(process_memory.get());
  const std::string* offline_files_path = offline_utils_.GetOfflineFilesPath();
  if (offline_files_path == nullptr) FAIL() << "GetOfflineFilesPath() failed.";

  if (!AddMemory(*offline_files_path + "lib_mem.data", memory, &error_msg)) FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(), process_memory);
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(7U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 000000000014ccbc  linker64 (__dl_syscall+28)\n"
      "  #01 pc 000000000005426c  linker64 "
      "(__dl__ZL24debuggerd_signal_handleriP7siginfoPv+1128)\n"
      "  #02 pc 00000000000008c0  vdso.so (__kernel_rt_sigreturn)\n"
      "  #03 pc 00000000000846f4  libc.so (abort+172)\n"
      "  #04 pc 0000000000084ad4  libc.so (__assert2+36)\n"
      "  #05 pc 000000000003d5b4  ANGLEPrebuilt.apk (offset 0x21d5000)\n"
      "  #06 pc 000000000007fe68  libc.so (__libc_init)\n",
      frame_info);

  EXPECT_EQ(0x7e82c4fcbcULL, unwinder.frames()[0].pc);
  EXPECT_EQ(0x7df8ca3bf0ULL, unwinder.frames()[0].sp);
  EXPECT_EQ(0x7e82b5726cULL, unwinder.frames()[1].pc);
  EXPECT_EQ(0x7df8ca3bf0ULL, unwinder.frames()[1].sp);
  EXPECT_EQ(0x7e82b018c0ULL, unwinder.frames()[2].pc);
  EXPECT_EQ(0x7df8ca3da0ULL, unwinder.frames()[2].sp);
  EXPECT_EQ(0x7e7eecc6f4ULL, unwinder.frames()[3].pc);
  EXPECT_EQ(0x7dabf3db60ULL, unwinder.frames()[3].sp);
  EXPECT_EQ(0x7e7eeccad4ULL, unwinder.frames()[4].pc);
  EXPECT_EQ(0x7dabf3dc40ULL, unwinder.frames()[4].sp);
  EXPECT_EQ(0x7dabc405b4ULL, unwinder.frames()[5].pc);
  EXPECT_EQ(0x7dabf3dc50ULL, unwinder.frames()[5].sp);
  EXPECT_EQ(0x7e7eec7e68ULL, unwinder.frames()[6].pc);
  EXPECT_EQ(0x7dabf3dc70ULL, unwinder.frames()[6].sp);
  // Ignore top frame since the test code was modified to end in __libc_init.
}

TEST_F(UnwindOfflineTest, shared_lib_in_apk_single_map_arm64) {
  std::string error_msg;
  if (!offline_utils_.Init("shared_lib_in_apk_single_map_arm64/", ARCH_ARM64, &error_msg))
    FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(13U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 00000000000814bc  libc.so (syscall+28)\n"
      "  #01 pc 00000000008cdf5c  test.apk (offset 0x5000)\n"
      "  #02 pc 00000000008cde9c  test.apk (offset 0x5000)\n"
      "  #03 pc 00000000008cdd70  test.apk (offset 0x5000)\n"
      "  #04 pc 00000000008ce408  test.apk (offset 0x5000)\n"
      "  #05 pc 00000000008ce8d8  test.apk (offset 0x5000)\n"
      "  #06 pc 00000000008ce814  test.apk (offset 0x5000)\n"
      "  #07 pc 00000000008bcf60  test.apk (offset 0x5000)\n"
      "  #08 pc 0000000000133024  test.apk (offset 0x5000)\n"
      "  #09 pc 0000000000134ad0  test.apk (offset 0x5000)\n"
      "  #10 pc 0000000000134b64  test.apk (offset 0x5000)\n"
      "  #11 pc 00000000000e406c  libc.so (__pthread_start(void*)+36)\n"
      "  #12 pc 0000000000085e18  libc.so (__start_thread+64)\n",
      frame_info);

  EXPECT_EQ(0x7cbe0b14bcULL, unwinder.frames()[0].pc);
  EXPECT_EQ(0x7be4f077d0ULL, unwinder.frames()[0].sp);
  EXPECT_EQ(0x7be6715f5cULL, unwinder.frames()[1].pc);
  EXPECT_EQ(0x7be4f077d0ULL, unwinder.frames()[1].sp);
  EXPECT_EQ(0x7be6715e9cULL, unwinder.frames()[2].pc);
  EXPECT_EQ(0x7be4f07800ULL, unwinder.frames()[2].sp);
  EXPECT_EQ(0x7be6715d70ULL, unwinder.frames()[3].pc);
  EXPECT_EQ(0x7be4f07840ULL, unwinder.frames()[3].sp);
  EXPECT_EQ(0x7be6716408ULL, unwinder.frames()[4].pc);
  EXPECT_EQ(0x7be4f07860ULL, unwinder.frames()[4].sp);
  EXPECT_EQ(0x7be67168d8ULL, unwinder.frames()[5].pc);
  EXPECT_EQ(0x7be4f07880ULL, unwinder.frames()[5].sp);
  EXPECT_EQ(0x7be6716814ULL, unwinder.frames()[6].pc);
  EXPECT_EQ(0x7be4f078f0ULL, unwinder.frames()[6].sp);
  EXPECT_EQ(0x7be6704f60ULL, unwinder.frames()[7].pc);
  EXPECT_EQ(0x7be4f07910ULL, unwinder.frames()[7].sp);
  EXPECT_EQ(0x7be5f7b024ULL, unwinder.frames()[8].pc);
  EXPECT_EQ(0x7be4f07950ULL, unwinder.frames()[8].sp);
  EXPECT_EQ(0x7be5f7cad0ULL, unwinder.frames()[9].pc);
  EXPECT_EQ(0x7be4f07aa0ULL, unwinder.frames()[9].sp);
  EXPECT_EQ(0x7be5f7cb64ULL, unwinder.frames()[10].pc);
  EXPECT_EQ(0x7be4f07ce0ULL, unwinder.frames()[10].sp);
  EXPECT_EQ(0x7cbe11406cULL, unwinder.frames()[11].pc);
  EXPECT_EQ(0x7be4f07d00ULL, unwinder.frames()[11].sp);
  EXPECT_EQ(0x7cbe0b5e18ULL, unwinder.frames()[12].pc);
  EXPECT_EQ(0x7be4f07d20ULL, unwinder.frames()[12].sp);
}

TEST_F(UnwindOfflineTest, invalid_elf_offset_arm) {
  std::string error_msg;
  if (!offline_utils_.Init("invalid_elf_offset_arm/", ARCH_ARM, &error_msg,
                           ProcessMemoryFlag::kNoMemory))
    FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(1U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ("  #00 pc 00aa7508  invalid.apk (offset 0x12e4000)\n", frame_info);
  EXPECT_EQ(0xc898f508, unwinder.frames()[0].pc);
  EXPECT_EQ(0xc2044218, unwinder.frames()[0].sp);
}

TEST_F(UnwindOfflineTest, load_bias_ro_rx_x86_64) {
  std::string error_msg;
  if (!offline_utils_.Init("load_bias_ro_rx_x86_64/", ARCH_X86_64, &error_msg)) FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(17U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 00000000000e9dd4  libc.so (__write+20)\n"
      "  #01 pc 000000000007ab9c  libc.so (_IO_file_write+44)\n"
      "  #02 pc 0000000000079f3e  libc.so\n"
      "  #03 pc 000000000007bce8  libc.so (_IO_do_write+24)\n"
      "  #04 pc 000000000007b26e  libc.so (_IO_file_xsputn+270)\n"
      "  #05 pc 000000000004f7f9  libc.so (_IO_vfprintf+1945)\n"
      "  #06 pc 0000000000057cb5  libc.so (_IO_printf+165)\n"
      "  #07 pc 0000000000ed1796  perfetto_unittests "
      "(testing::internal::PrettyUnitTestResultPrinter::OnTestIterationStart(testing::UnitTest "
      "const&, int)+374)\n"
      "  #08 pc 0000000000ed30fd  perfetto_unittests "
      "(testing::internal::TestEventRepeater::OnTestIterationStart(testing::UnitTest const&, "
      "int)+125)\n"
      "  #09 pc 0000000000ed5e25  perfetto_unittests "
      "(testing::internal::UnitTestImpl::RunAllTests()+581)\n"
      "  #10 pc 0000000000ef63f3  perfetto_unittests "
      "(bool "
      "testing::internal::HandleSehExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, "
      "bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char "
      "const*)+131)\n"
      "  #11 pc 0000000000ee2a21  perfetto_unittests "
      "(bool "
      "testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, "
      "bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char "
      "const*)+113)\n"
      "  #12 pc 0000000000ed5bb9  perfetto_unittests (testing::UnitTest::Run()+185)\n"
      "  #13 pc 0000000000e900f0  perfetto_unittests (RUN_ALL_TESTS()+16)\n"
      "  #14 pc 0000000000e900d8  perfetto_unittests (main+56)\n"
      "  #15 pc 000000000002352a  libc.so (__libc_start_main+234)\n"
      "  #16 pc 0000000000919029  perfetto_unittests (_start+41)\n",
      frame_info);

  EXPECT_EQ(0x7f9326a57dd4ULL, unwinder.frames()[0].pc);
  EXPECT_EQ(0x7ffd224153c8ULL, unwinder.frames()[0].sp);
  EXPECT_EQ(0x7f93269e8b9cULL, unwinder.frames()[1].pc);
  EXPECT_EQ(0x7ffd224153d0ULL, unwinder.frames()[1].sp);
  EXPECT_EQ(0x7f93269e7f3eULL, unwinder.frames()[2].pc);
  EXPECT_EQ(0x7ffd22415400ULL, unwinder.frames()[2].sp);
  EXPECT_EQ(0x7f93269e9ce8ULL, unwinder.frames()[3].pc);
  EXPECT_EQ(0x7ffd22415440ULL, unwinder.frames()[3].sp);
  EXPECT_EQ(0x7f93269e926eULL, unwinder.frames()[4].pc);
  EXPECT_EQ(0x7ffd22415450ULL, unwinder.frames()[4].sp);
  EXPECT_EQ(0x7f93269bd7f9ULL, unwinder.frames()[5].pc);
  EXPECT_EQ(0x7ffd22415490ULL, unwinder.frames()[5].sp);
  EXPECT_EQ(0x7f93269c5cb5ULL, unwinder.frames()[6].pc);
  EXPECT_EQ(0x7ffd22415a10ULL, unwinder.frames()[6].sp);
  EXPECT_EQ(0xed1796ULL, unwinder.frames()[7].pc);
  EXPECT_EQ(0x7ffd22415af0ULL, unwinder.frames()[7].sp);
  EXPECT_EQ(0xed30fdULL, unwinder.frames()[8].pc);
  EXPECT_EQ(0x7ffd22415b70ULL, unwinder.frames()[8].sp);
  EXPECT_EQ(0xed5e25ULL, unwinder.frames()[9].pc);
  EXPECT_EQ(0x7ffd22415bb0ULL, unwinder.frames()[9].sp);
  EXPECT_EQ(0xef63f3ULL, unwinder.frames()[10].pc);
  EXPECT_EQ(0x7ffd22415c60ULL, unwinder.frames()[10].sp);
  EXPECT_EQ(0xee2a21ULL, unwinder.frames()[11].pc);
  EXPECT_EQ(0x7ffd22415cc0ULL, unwinder.frames()[11].sp);
  EXPECT_EQ(0xed5bb9ULL, unwinder.frames()[12].pc);
  EXPECT_EQ(0x7ffd22415d40ULL, unwinder.frames()[12].sp);
  EXPECT_EQ(0xe900f0ULL, unwinder.frames()[13].pc);
  EXPECT_EQ(0x7ffd22415d90ULL, unwinder.frames()[13].sp);
  EXPECT_EQ(0xe900d8ULL, unwinder.frames()[14].pc);
  EXPECT_EQ(0x7ffd22415da0ULL, unwinder.frames()[14].sp);
  EXPECT_EQ(0x7f932699152aULL, unwinder.frames()[15].pc);
  EXPECT_EQ(0x7ffd22415dd0ULL, unwinder.frames()[15].sp);
  EXPECT_EQ(0x919029ULL, unwinder.frames()[16].pc);
  EXPECT_EQ(0x7ffd22415e90ULL, unwinder.frames()[16].sp);
}

TEST_F(UnwindOfflineTest, load_bias_different_section_bias_arm64) {
  std::string error_msg;
  if (!offline_utils_.Init("load_bias_different_section_bias_arm64/", ARCH_ARM64, &error_msg))
    FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(12U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 00000000000d59bc  linker64 (__dl_syscall+28)\n"
      "  #01 pc 00000000000554e8  linker64 (__dl__ZL24debuggerd_signal_handleriP7siginfoPv+1148)\n"
      "  #02 pc 00000000000008c0  vdso (__kernel_rt_sigreturn)\n"
      "  #03 pc 000000000007f3e8  libc.so (abort+168)\n"
      "  #04 pc 00000000000459fc  test (std::__ndk1::__throw_bad_cast()+4)\n"
      "  #05 pc 0000000000056d80  test (testing::Test::Run()+88)\n"
      "  #06 pc 000000000005724c  test (testing::TestInfo::Run()+112)\n"
      "  #07 pc 0000000000057558  test (testing::TestSuite::Run()+116)\n"
      "  #08 pc 000000000005bffc  test (testing::internal::UnitTestImpl::RunAllTests()+464)\n"
      "  #09 pc 000000000005bd9c  test (testing::UnitTest::Run()+116)\n"
      "  #10 pc 00000000000464e4  test (main+144)\n"
      "  #11 pc 000000000007aa34  libc.so (__libc_init+108)\n",
      frame_info);

  EXPECT_EQ(0x7112cb99bcULL, unwinder.frames()[0].pc);
  EXPECT_EQ(0x7112bdbbf0ULL, unwinder.frames()[0].sp);
  EXPECT_EQ(0x7112c394e8ULL, unwinder.frames()[1].pc);
  EXPECT_EQ(0x7112bdbbf0ULL, unwinder.frames()[1].sp);
  EXPECT_EQ(0x7112be28c0ULL, unwinder.frames()[2].pc);
  EXPECT_EQ(0x7112bdbda0ULL, unwinder.frames()[2].sp);
  EXPECT_EQ(0x71115ab3e8ULL, unwinder.frames()[3].pc);
  EXPECT_EQ(0x7fdd4a3f00ULL, unwinder.frames()[3].sp);
  EXPECT_EQ(0x5f739dc9fcULL, unwinder.frames()[4].pc);
  EXPECT_EQ(0x7fdd4a3fe0ULL, unwinder.frames()[4].sp);
  EXPECT_EQ(0x5f739edd80ULL, unwinder.frames()[5].pc);
  EXPECT_EQ(0x7fdd4a3ff0ULL, unwinder.frames()[5].sp);
  EXPECT_EQ(0x5f739ee24cULL, unwinder.frames()[6].pc);
  EXPECT_EQ(0x7fdd4a4010ULL, unwinder.frames()[6].sp);
  EXPECT_EQ(0x5f739ee558ULL, unwinder.frames()[7].pc);
  EXPECT_EQ(0x7fdd4a4040ULL, unwinder.frames()[7].sp);
  EXPECT_EQ(0x5f739f2ffcULL, unwinder.frames()[8].pc);
  EXPECT_EQ(0x7fdd4a4070ULL, unwinder.frames()[8].sp);
  EXPECT_EQ(0x5f739f2d9cULL, unwinder.frames()[9].pc);
  EXPECT_EQ(0x7fdd4a4100ULL, unwinder.frames()[9].sp);
  EXPECT_EQ(0x5f739dd4e4ULL, unwinder.frames()[10].pc);
  EXPECT_EQ(0x7fdd4a4130ULL, unwinder.frames()[10].sp);
  EXPECT_EQ(0x71115a6a34ULL, unwinder.frames()[11].pc);
  EXPECT_EQ(0x7fdd4a4170ULL, unwinder.frames()[11].sp);
}

TEST_F(UnwindOfflineTest, eh_frame_bias_x86) {
  std::string error_msg;
  if (!offline_utils_.Init("eh_frame_bias_x86/", ARCH_X86, &error_msg)) FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(11U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc ffffe430  vdso.so (__kernel_vsyscall+16)\n"
      "  #01 pc 00082a4b  libc.so (__epoll_pwait+43)\n"
      "  #02 pc 000303a3  libc.so (epoll_pwait+115)\n"
      "  #03 pc 000303ed  libc.so (epoll_wait+45)\n"
      "  #04 pc 00010ea2  tombstoned (epoll_dispatch+226)\n"
      "  #05 pc 0000c5e7  tombstoned (event_base_loop+1095)\n"
      "  #06 pc 0000c193  tombstoned (event_base_dispatch+35)\n"
      "  #07 pc 00005c77  tombstoned (main+884)\n"
      "  #08 pc 00015f66  libc.so (__libc_init+102)\n"
      "  #09 pc 0000360e  tombstoned (_start+98)\n"
      "  #10 pc 00000001  <unknown>\n",
      frame_info);

  EXPECT_EQ(0xffffe430ULL, unwinder.frames()[0].pc);
  EXPECT_EQ(0xfffe1a30ULL, unwinder.frames()[0].sp);
  EXPECT_EQ(0xeb585a4bULL, unwinder.frames()[1].pc);
  EXPECT_EQ(0xfffe1a40ULL, unwinder.frames()[1].sp);
  EXPECT_EQ(0xeb5333a3ULL, unwinder.frames()[2].pc);
  EXPECT_EQ(0xfffe1a60ULL, unwinder.frames()[2].sp);
  EXPECT_EQ(0xeb5333edULL, unwinder.frames()[3].pc);
  EXPECT_EQ(0xfffe1ab0ULL, unwinder.frames()[3].sp);
  EXPECT_EQ(0xeb841ea2ULL, unwinder.frames()[4].pc);
  EXPECT_EQ(0xfffe1ae0ULL, unwinder.frames()[4].sp);
  EXPECT_EQ(0xeb83d5e7ULL, unwinder.frames()[5].pc);
  EXPECT_EQ(0xfffe1b30ULL, unwinder.frames()[5].sp);
  EXPECT_EQ(0xeb83d193ULL, unwinder.frames()[6].pc);
  EXPECT_EQ(0xfffe1bd0ULL, unwinder.frames()[6].sp);
  EXPECT_EQ(0xeb836c77ULL, unwinder.frames()[7].pc);
  EXPECT_EQ(0xfffe1c00ULL, unwinder.frames()[7].sp);
  EXPECT_EQ(0xeb518f66ULL, unwinder.frames()[8].pc);
  EXPECT_EQ(0xfffe1d00ULL, unwinder.frames()[8].sp);
  EXPECT_EQ(0xeb83460eULL, unwinder.frames()[9].pc);
  EXPECT_EQ(0xfffe1d40ULL, unwinder.frames()[9].sp);
  EXPECT_EQ(0x00000001ULL, unwinder.frames()[10].pc);
  EXPECT_EQ(0xfffe1d74ULL, unwinder.frames()[10].sp);
}

TEST_F(UnwindOfflineTest, signal_load_bias_arm) {
  std::string error_msg;
  if (!offline_utils_.Init("signal_load_bias_arm/", ARCH_ARM, &error_msg)) FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(17U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 0029ef9e  libunwindstack_unit_test (SignalInnerFunction+10)\n"
      "  #01 pc 0029efa7  libunwindstack_unit_test (SignalMiddleFunction+2)\n"
      "  #02 pc 0029efaf  libunwindstack_unit_test (SignalOuterFunction+2)\n"
      "  #03 pc 002a280b  libunwindstack_unit_test (unwindstack::SignalCallerHandler(int, "
      "siginfo*, void*)+10)\n"
      "  #04 pc 00058bd4  libc.so (__restore)\n"
      "  #05 pc 0029f01e  libunwindstack_unit_test (InnerFunction+106)\n"
      "  #06 pc 0029f633  libunwindstack_unit_test (MiddleFunction+16)\n"
      "  #07 pc 0029f64b  libunwindstack_unit_test (OuterFunction+16)\n"
      "  #08 pc 002a1711  libunwindstack_unit_test (unwindstack::RemoteThroughSignal(int, unsigned "
      "int)+260)\n"
      "  #09 pc 002a1603  libunwindstack_unit_test "
      "(unwindstack::UnwindTest_remote_through_signal_Test::TestBody()+10)\n"
      "  #10 pc 002c8fe3  libunwindstack_unit_test (testing::Test::Run()+130)\n"
      "  #11 pc 002c9b25  libunwindstack_unit_test (testing::TestInfo::Run()+184)\n"
      "  #12 pc 002c9e27  libunwindstack_unit_test (testing::TestSuite::Run()+202)\n"
      "  #13 pc 002d193d  libunwindstack_unit_test "
      "(testing::internal::UnitTestImpl::RunAllTests()+660)\n"
      "  #14 pc 002d160b  libunwindstack_unit_test (testing::UnitTest::Run()+134)\n"
      "  #15 pc 002de035  libunwindstack_unit_test (IsolateMain+680)\n"
      "  #16 pc 00058155  libc.so (__libc_init+68)\n",
      frame_info);

  EXPECT_EQ(0xb6955f9eULL, unwinder.frames()[0].pc);
  EXPECT_EQ(0xf2790ce8ULL, unwinder.frames()[0].sp);
  EXPECT_EQ(0xb6955fa7ULL, unwinder.frames()[1].pc);
  EXPECT_EQ(0xf2790ce8ULL, unwinder.frames()[1].sp);
  EXPECT_EQ(0xb6955fafULL, unwinder.frames()[2].pc);
  EXPECT_EQ(0xf2790cf0ULL, unwinder.frames()[2].sp);
  EXPECT_EQ(0xb695980bULL, unwinder.frames()[3].pc);
  EXPECT_EQ(0xf2790cf8ULL, unwinder.frames()[3].sp);
  EXPECT_EQ(0xf23febd4ULL, unwinder.frames()[4].pc);
  EXPECT_EQ(0xf2790d10ULL, unwinder.frames()[4].sp);
  EXPECT_EQ(0xb695601eULL, unwinder.frames()[5].pc);
  EXPECT_EQ(0xffe67798ULL, unwinder.frames()[5].sp);
  EXPECT_EQ(0xb6956633ULL, unwinder.frames()[6].pc);
  EXPECT_EQ(0xffe67890ULL, unwinder.frames()[6].sp);
  EXPECT_EQ(0xb695664bULL, unwinder.frames()[7].pc);
  EXPECT_EQ(0xffe678a0ULL, unwinder.frames()[7].sp);
  EXPECT_EQ(0xb6958711ULL, unwinder.frames()[8].pc);
  EXPECT_EQ(0xffe678b0ULL, unwinder.frames()[8].sp);
  EXPECT_EQ(0xb6958603ULL, unwinder.frames()[9].pc);
  EXPECT_EQ(0xffe67ac8ULL, unwinder.frames()[9].sp);
  EXPECT_EQ(0xb697ffe3ULL, unwinder.frames()[10].pc);
  EXPECT_EQ(0xffe67ad8ULL, unwinder.frames()[10].sp);
  EXPECT_EQ(0xb6980b25ULL, unwinder.frames()[11].pc);
  EXPECT_EQ(0xffe67ae8ULL, unwinder.frames()[11].sp);
  EXPECT_EQ(0xb6980e27ULL, unwinder.frames()[12].pc);
  EXPECT_EQ(0xffe67b18ULL, unwinder.frames()[12].sp);
  EXPECT_EQ(0xb698893dULL, unwinder.frames()[13].pc);
  EXPECT_EQ(0xffe67b48ULL, unwinder.frames()[13].sp);
  EXPECT_EQ(0xb698860bULL, unwinder.frames()[14].pc);
  EXPECT_EQ(0xffe67bb0ULL, unwinder.frames()[14].sp);
  EXPECT_EQ(0xb6995035ULL, unwinder.frames()[15].pc);
  EXPECT_EQ(0xffe67bd0ULL, unwinder.frames()[15].sp);
  EXPECT_EQ(0xf23fe155ULL, unwinder.frames()[16].pc);
  EXPECT_EQ(0xffe67d10ULL, unwinder.frames()[16].sp);
}

TEST_F(UnwindOfflineTest, empty_arm64) {
  std::string error_msg;
  if (!offline_utils_.Init("empty_arm64/", ARCH_ARM64, &error_msg)) FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(7U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 00000000000963a4  libc.so (__ioctl+4)\n"
      "  #01 pc 000000000005344c  libc.so (ioctl+140)\n"
      "  #02 pc 0000000000050ce4  libbinder.so "
      "(android::IPCThreadState::talkWithDriver(bool)+308)\n"
      "  #03 pc 0000000000050e98  libbinder.so "
      "(android::IPCThreadState::getAndExecuteCommand()+24)\n"
      "  #04 pc 00000000000516ac  libbinder.so (android::IPCThreadState::joinThreadPool(bool)+60)\n"
      "  #05 pc 00000000000443b0  netd (main+1056)\n"
      "  #06 pc 0000000000045594  libc.so (__libc_init+108)\n",
      frame_info);

  EXPECT_EQ(0x72a02203a4U, unwinder.frames()[0].pc);
  EXPECT_EQ(0x7ffb6c0b50U, unwinder.frames()[0].sp);
  EXPECT_EQ(0x72a01dd44cU, unwinder.frames()[1].pc);
  EXPECT_EQ(0x7ffb6c0b50U, unwinder.frames()[1].sp);
  EXPECT_EQ(0x729f759ce4U, unwinder.frames()[2].pc);
  EXPECT_EQ(0x7ffb6c0c50U, unwinder.frames()[2].sp);
  EXPECT_EQ(0x729f759e98U, unwinder.frames()[3].pc);
  EXPECT_EQ(0x7ffb6c0ce0U, unwinder.frames()[3].sp);
  EXPECT_EQ(0x729f75a6acU, unwinder.frames()[4].pc);
  EXPECT_EQ(0x7ffb6c0d10U, unwinder.frames()[4].sp);
  EXPECT_EQ(0x5d478af3b0U, unwinder.frames()[5].pc);
  EXPECT_EQ(0x7ffb6c0d40U, unwinder.frames()[5].sp);
  EXPECT_EQ(0x72a01cf594U, unwinder.frames()[6].pc);
  EXPECT_EQ(0x7ffb6c0f30U, unwinder.frames()[6].sp);
}

// This test has a libc.so where the __restore has been changed so
// that the signal handler match does not occur and it uses the
// fde to do the unwind.
TEST_F(UnwindOfflineTest, signal_fde_x86) {
  std::string error_msg;
  if (!offline_utils_.Init("signal_fde_x86/", ARCH_X86, &error_msg)) FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(20U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 007914d9  libunwindstack_test (SignalInnerFunction+25)\n"
      "  #01 pc 007914fc  libunwindstack_test (SignalMiddleFunction+28)\n"
      "  #02 pc 0079152c  libunwindstack_test (SignalOuterFunction+28)\n"
      "  #03 pc 0079af62  libunwindstack_test (unwindstack::SignalCallerHandler(int, siginfo*, "
      "void*)+50)\n"
      "  #04 pc 00058fb0  libc.so (__restore)\n"
      "  #05 pc 00000000  <unknown>\n"
      "  #06 pc 0079161a  libunwindstack_test (InnerFunction+218)\n"
      "  #07 pc 007923aa  libunwindstack_test (MiddleFunction+42)\n"
      "  #08 pc 007923ea  libunwindstack_test (OuterFunction+42)\n"
      "  #09 pc 00797444  libunwindstack_test (unwindstack::RemoteThroughSignal(int, unsigned "
      "int)+868)\n"
      "  #10 pc 007985b8  libunwindstack_test "
      "(unwindstack::UnwindTest_remote_through_signal_with_invalid_func_Test::TestBody()+56)\n"
      "  #11 pc 00817a19  libunwindstack_test\n"
      "  #12 pc 008178c5  libunwindstack_test (testing::Test::Run()+277)\n"
      "  #13 pc 00818d3e  libunwindstack_test (testing::TestInfo::Run()+318)\n"
      "  #14 pc 008198b4  libunwindstack_test (testing::TestSuite::Run()+436)\n"
      "  #15 pc 00828cb0  libunwindstack_test "
      "(testing::internal::UnitTestImpl::RunAllTests()+1216)\n"
      "  #16 pc 0082870f  libunwindstack_test (testing::UnitTest::Run()+367)\n"
      "  #17 pc 0084031e  libunwindstack_test (IsolateMain+2334)\n"
      "  #18 pc 0083f9e9  libunwindstack_test (main+41)\n"
      "  #19 pc 00050646  libc.so (__libc_init+118)\n",
      frame_info);

  EXPECT_EQ(0x5ae0d4d9U, unwinder.frames()[0].pc);
  EXPECT_EQ(0xecb37188U, unwinder.frames()[0].sp);
  EXPECT_EQ(0x5ae0d4fcU, unwinder.frames()[1].pc);
  EXPECT_EQ(0xecb37190U, unwinder.frames()[1].sp);
  EXPECT_EQ(0x5ae0d52cU, unwinder.frames()[2].pc);
  EXPECT_EQ(0xecb371b0U, unwinder.frames()[2].sp);
  EXPECT_EQ(0x5ae16f62U, unwinder.frames()[3].pc);
  EXPECT_EQ(0xecb371d0U, unwinder.frames()[3].sp);
  EXPECT_EQ(0xec169fb0U, unwinder.frames()[4].pc);
  EXPECT_EQ(0xecb371f0U, unwinder.frames()[4].sp);
  EXPECT_EQ(0x0U, unwinder.frames()[5].pc);
  EXPECT_EQ(0xffcfac6cU, unwinder.frames()[5].sp);
  EXPECT_EQ(0x5ae0d61aU, unwinder.frames()[6].pc);
  EXPECT_EQ(0xffcfac6cU, unwinder.frames()[6].sp);
  EXPECT_EQ(0x5ae0e3aaU, unwinder.frames()[7].pc);
  EXPECT_EQ(0xffcfad60U, unwinder.frames()[7].sp);
  EXPECT_EQ(0x5ae0e3eaU, unwinder.frames()[8].pc);
  EXPECT_EQ(0xffcfad90U, unwinder.frames()[8].sp);
  EXPECT_EQ(0x5ae13444U, unwinder.frames()[9].pc);
  EXPECT_EQ(0xffcfadc0U, unwinder.frames()[9].sp);
  EXPECT_EQ(0x5ae145b8U, unwinder.frames()[10].pc);
  EXPECT_EQ(0xffcfb020U, unwinder.frames()[10].sp);
  EXPECT_EQ(0x5ae93a19U, unwinder.frames()[11].pc);
  EXPECT_EQ(0xffcfb050U, unwinder.frames()[11].sp);
  EXPECT_EQ(0x5ae938c5U, unwinder.frames()[12].pc);
  EXPECT_EQ(0xffcfb090U, unwinder.frames()[12].sp);
  EXPECT_EQ(0x5ae94d3eU, unwinder.frames()[13].pc);
  EXPECT_EQ(0xffcfb0f0U, unwinder.frames()[13].sp);
  EXPECT_EQ(0x5ae958b4U, unwinder.frames()[14].pc);
  EXPECT_EQ(0xffcfb160U, unwinder.frames()[14].sp);
  EXPECT_EQ(0x5aea4cb0U, unwinder.frames()[15].pc);
  EXPECT_EQ(0xffcfb1d0U, unwinder.frames()[15].sp);
  EXPECT_EQ(0x5aea470fU, unwinder.frames()[16].pc);
  EXPECT_EQ(0xffcfb270U, unwinder.frames()[16].sp);
  EXPECT_EQ(0x5aebc31eU, unwinder.frames()[17].pc);
  EXPECT_EQ(0xffcfb2c0U, unwinder.frames()[17].sp);
  EXPECT_EQ(0x5aebb9e9U, unwinder.frames()[18].pc);
  EXPECT_EQ(0xffcfc3c0U, unwinder.frames()[18].sp);
  EXPECT_EQ(0xec161646U, unwinder.frames()[19].pc);
  EXPECT_EQ(0xffcfc3f0U, unwinder.frames()[19].sp);
}

// This test has a libc.so where the __restore_rt has been changed so
// that the signal handler match does not occur and it uses the
// fde to do the unwind.
TEST_F(UnwindOfflineTest, signal_fde_x86_64) {
  std::string error_msg;
  if (!offline_utils_.Init("signal_fde_x86_64/", ARCH_X86_64, &error_msg)) FAIL() << error_msg;

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(18U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 000000000058415b  libunwindstack_test (SignalInnerFunction+11)\n"
      "  #01 pc 0000000000584168  libunwindstack_test (SignalMiddleFunction+8)\n"
      "  #02 pc 0000000000584178  libunwindstack_test (SignalOuterFunction+8)\n"
      "  #03 pc 000000000058ac77  libunwindstack_test (unwindstack::SignalCallerHandler(int, "
      "siginfo*, void*)+23)\n"
      "  #04 pc 0000000000057d10  libc.so (__restore_rt)\n"
      "  #05 pc 0000000000000000  <unknown>\n"
      "  #06 pc 0000000000584244  libunwindstack_test (InnerFunction+196)\n"
      "  #07 pc 0000000000584b44  libunwindstack_test (MiddleFunction+20)\n"
      "  #08 pc 0000000000584b64  libunwindstack_test (OuterFunction+20)\n"
      "  #09 pc 0000000000588457  libunwindstack_test (unwindstack::RemoteThroughSignal(int, "
      "unsigned int)+583)\n"
      "  #10 pc 0000000000588f67  libunwindstack_test "
      "(unwindstack::UnwindTest_remote_through_signal_with_invalid_func_Test::TestBody()+23)\n"
      "  #11 pc 00000000005d9c38  libunwindstack_test (testing::Test::Run()+216)\n"
      "  #12 pc 00000000005daf9a  libunwindstack_test (testing::TestInfo::Run()+266)\n"
      "  #13 pc 00000000005dba46  libunwindstack_test (testing::TestSuite::Run()+390)\n"
      "  #14 pc 00000000005ea4c6  libunwindstack_test "
      "(testing::internal::UnitTestImpl::RunAllTests()+1190)\n"
      "  #15 pc 00000000005e9f61  libunwindstack_test (testing::UnitTest::Run()+337)\n"
      "  #16 pc 0000000000600155  libunwindstack_test (IsolateMain+2037)\n"
      "  #17 pc 000000000004e405  libc.so (__libc_init+101)\n",
      frame_info);

  EXPECT_EQ(0x5bb41271e15bU, unwinder.frames()[0].pc);
  EXPECT_EQ(0x707eb5aa8320U, unwinder.frames()[0].sp);
  EXPECT_EQ(0x5bb41271e168U, unwinder.frames()[1].pc);
  EXPECT_EQ(0x707eb5aa8330U, unwinder.frames()[1].sp);
  EXPECT_EQ(0x5bb41271e178U, unwinder.frames()[2].pc);
  EXPECT_EQ(0x707eb5aa8340U, unwinder.frames()[2].sp);
  EXPECT_EQ(0x5bb412724c77U, unwinder.frames()[3].pc);
  EXPECT_EQ(0x707eb5aa8350U, unwinder.frames()[3].sp);
  EXPECT_EQ(0x707eb2ca5d10U, unwinder.frames()[4].pc);
  EXPECT_EQ(0x707eb5aa8380U, unwinder.frames()[4].sp);
  EXPECT_EQ(0x0U, unwinder.frames()[5].pc);
  EXPECT_EQ(0x7ffcaadde078U, unwinder.frames()[5].sp);
  EXPECT_EQ(0x5bb41271e244U, unwinder.frames()[6].pc);
  EXPECT_EQ(0x7ffcaadde078U, unwinder.frames()[6].sp);
  EXPECT_EQ(0x5bb41271eb44U, unwinder.frames()[7].pc);
  EXPECT_EQ(0x7ffcaadde1a0U, unwinder.frames()[7].sp);
  EXPECT_EQ(0x5bb41271eb64U, unwinder.frames()[8].pc);
  EXPECT_EQ(0x7ffcaadde1c0U, unwinder.frames()[8].sp);
  EXPECT_EQ(0x5bb412722457U, unwinder.frames()[9].pc);
  EXPECT_EQ(0x7ffcaadde1e0U, unwinder.frames()[9].sp);
  EXPECT_EQ(0x5bb412722f67U, unwinder.frames()[10].pc);
  EXPECT_EQ(0x7ffcaadde510U, unwinder.frames()[10].sp);
  EXPECT_EQ(0x5bb412773c38U, unwinder.frames()[11].pc);
  EXPECT_EQ(0x7ffcaadde530U, unwinder.frames()[11].sp);
  EXPECT_EQ(0x5bb412774f9aU, unwinder.frames()[12].pc);
  EXPECT_EQ(0x7ffcaadde560U, unwinder.frames()[12].sp);
  EXPECT_EQ(0x5bb412775a46U, unwinder.frames()[13].pc);
  EXPECT_EQ(0x7ffcaadde5b0U, unwinder.frames()[13].sp);
  EXPECT_EQ(0x5bb4127844c6U, unwinder.frames()[14].pc);
  EXPECT_EQ(0x7ffcaadde5f0U, unwinder.frames()[14].sp);
  EXPECT_EQ(0x5bb412783f61U, unwinder.frames()[15].pc);
  EXPECT_EQ(0x7ffcaadde6c0U, unwinder.frames()[15].sp);
  EXPECT_EQ(0x5bb41279a155U, unwinder.frames()[16].pc);
  EXPECT_EQ(0x7ffcaadde720U, unwinder.frames()[16].sp);
  EXPECT_EQ(0x707eb2c9c405U, unwinder.frames()[17].pc);
  EXPECT_EQ(0x7ffcaaddf870U, unwinder.frames()[17].sp);
}

TEST_F(UnwindOfflineTest, pauth_pc_arm64) {
  std::string error_msg;
  if (!offline_utils_.Init("pauth_pc_arm64/", ARCH_ARM64, &error_msg)) FAIL() << error_msg;

  static_cast<RegsArm64*>(offline_utils_.GetRegs())->SetPACMask(0x007fff8000000000ULL);

  Unwinder unwinder(128, offline_utils_.GetMaps(), offline_utils_.GetRegs(),
                    offline_utils_.GetProcessMemory());
  unwinder.Unwind();

  std::string frame_info(DumpFrames(unwinder));
  ASSERT_EQ(26U, unwinder.NumFrames()) << "Unwind:\n" << frame_info;
  EXPECT_EQ(
      "  #00 pc 00000000000404a8  toybox (do_print+28)\n"
      "  #01 pc 0000000000040270  toybox (do_find+5072)\n"
      "  #02 pc 000000000002c640  toybox (dirtree_handle_callback+40)\n"
      "  #03 pc 000000000002c588  toybox (dirtree_recurse+200)\n"
      "  #04 pc 000000000002c6a8  toybox (dirtree_handle_callback+144)\n"
      "  #05 pc 000000000002c588  toybox (dirtree_recurse+200)\n"
      "  #06 pc 000000000002c6a8  toybox (dirtree_handle_callback+144)\n"
      "  #07 pc 000000000002c588  toybox (dirtree_recurse+200)\n"
      "  #08 pc 000000000002c6a8  toybox (dirtree_handle_callback+144)\n"
      "  #09 pc 000000000002c588  toybox (dirtree_recurse+200)\n"
      "  #10 pc 000000000002c6a8  toybox (dirtree_handle_callback+144)\n"
      "  #11 pc 000000000002c588  toybox (dirtree_recurse+200)\n"
      "  #12 pc 000000000002c6a8  toybox (dirtree_handle_callback+144)\n"
      "  #13 pc 000000000002c588  toybox (dirtree_recurse+200)\n"
      "  #14 pc 000000000002c6a8  toybox (dirtree_handle_callback+144)\n"
      "  #15 pc 000000000002c588  toybox (dirtree_recurse+200)\n"
      "  #16 pc 000000000002c6a8  toybox (dirtree_handle_callback+144)\n"
      "  #17 pc 000000000002c588  toybox (dirtree_recurse+200)\n"
      "  #18 pc 000000000002c6a8  toybox (dirtree_handle_callback+144)\n"
      "  #19 pc 000000000002c588  toybox (dirtree_recurse+200)\n"
      "  #20 pc 000000000002c6a8  toybox (dirtree_handle_callback+144)\n"
      "  #21 pc 000000000003ee54  toybox (find_main+272)\n"
      "  #22 pc 0000000000034834  toybox (toy_exec_which+88)\n"
      "  #23 pc 00000000000342cc  toybox (toybox_main+148)\n"
      "  #24 pc 00000000000348b4  toybox (main+120)\n"
      "  #25 pc 00000000000499d8  libc.so "
      "(__libc_init+112)\n",
      frame_info);

  EXPECT_EQ(0x5c390884a8U, unwinder.frames()[0].pc);
  EXPECT_EQ(0x7ff3511750U, unwinder.frames()[0].sp);
  EXPECT_EQ(0x5c39088270U, unwinder.frames()[1].pc);
  EXPECT_EQ(0x7ff3511770U, unwinder.frames()[1].sp);
  EXPECT_EQ(0x5c39074640U, unwinder.frames()[2].pc);
  EXPECT_EQ(0x7ff3511930U, unwinder.frames()[2].sp);
  EXPECT_EQ(0x5c39074588U, unwinder.frames()[3].pc);
  EXPECT_EQ(0x7ff3511960U, unwinder.frames()[3].sp);
  EXPECT_EQ(0x5c390746a8U, unwinder.frames()[4].pc);
  EXPECT_EQ(0x7ff35119a0U, unwinder.frames()[4].sp);
  EXPECT_EQ(0x5c39074588U, unwinder.frames()[5].pc);
  EXPECT_EQ(0x7ff35119d0U, unwinder.frames()[5].sp);
  EXPECT_EQ(0x5c390746a8U, unwinder.frames()[6].pc);
  EXPECT_EQ(0x7ff3511a10U, unwinder.frames()[6].sp);
  EXPECT_EQ(0x5c39074588U, unwinder.frames()[7].pc);
  EXPECT_EQ(0x7ff3511a40U, unwinder.frames()[7].sp);
  EXPECT_EQ(0x5c390746a8U, unwinder.frames()[8].pc);
  EXPECT_EQ(0x7ff3511a80U, unwinder.frames()[8].sp);
  EXPECT_EQ(0x5c39074588U, unwinder.frames()[9].pc);
  EXPECT_EQ(0x7ff3511ab0U, unwinder.frames()[9].sp);
  EXPECT_EQ(0x5c390746a8U, unwinder.frames()[10].pc);
  EXPECT_EQ(0x7ff3511af0U, unwinder.frames()[10].sp);
  EXPECT_EQ(0x5c39074588U, unwinder.frames()[11].pc);
  EXPECT_EQ(0x7ff3511b20U, unwinder.frames()[11].sp);
  EXPECT_EQ(0x5c390746a8U, unwinder.frames()[12].pc);
  EXPECT_EQ(0x7ff3511b60U, unwinder.frames()[12].sp);
  EXPECT_EQ(0x5c39074588U, unwinder.frames()[13].pc);
  EXPECT_EQ(0x7ff3511b90U, unwinder.frames()[13].sp);
  EXPECT_EQ(0x5c390746a8U, unwinder.frames()[14].pc);
  EXPECT_EQ(0x7ff3511bd0U, unwinder.frames()[14].sp);
  EXPECT_EQ(0x5c39074588U, unwinder.frames()[15].pc);
  EXPECT_EQ(0x7ff3511c00U, unwinder.frames()[15].sp);
  EXPECT_EQ(0x5c390746a8U, unwinder.frames()[16].pc);
  EXPECT_EQ(0x7ff3511c40U, unwinder.frames()[16].sp);
  EXPECT_EQ(0x5c39074588U, unwinder.frames()[17].pc);
  EXPECT_EQ(0x7ff3511c70U, unwinder.frames()[17].sp);
  EXPECT_EQ(0x5c390746a8U, unwinder.frames()[18].pc);
  EXPECT_EQ(0x7ff3511cb0U, unwinder.frames()[18].sp);
  EXPECT_EQ(0x5c39074588U, unwinder.frames()[19].pc);
  EXPECT_EQ(0x7ff3511ce0U, unwinder.frames()[19].sp);
  EXPECT_EQ(0x5c390746a8U, unwinder.frames()[20].pc);
  EXPECT_EQ(0x7ff3511d20U, unwinder.frames()[20].sp);
  EXPECT_EQ(0x5c39086e54U, unwinder.frames()[21].pc);
  EXPECT_EQ(0x7ff3511d50U, unwinder.frames()[21].sp);
  EXPECT_EQ(0x5c3907c834U, unwinder.frames()[22].pc);
  EXPECT_EQ(0x7ff3511db0U, unwinder.frames()[22].sp);
  EXPECT_EQ(0x5c3907c2ccU, unwinder.frames()[23].pc);
  EXPECT_EQ(0x7ff3511dc0U, unwinder.frames()[23].sp);
  EXPECT_EQ(0x5c3907c8b4U, unwinder.frames()[24].pc);
  EXPECT_EQ(0x7ff3511e40U, unwinder.frames()[24].sp);
  EXPECT_EQ(0x7e4ede29d8U, unwinder.frames()[25].pc);
  EXPECT_EQ(0x7ff3511e70U, unwinder.frames()[25].sp);
}

}  // namespace
}  // namespace unwindstack
