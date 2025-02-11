// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/process_impl.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/target_impl.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"

namespace zxdb {

namespace {

class ProcessSink : public MockRemoteAPI {
 public:
  ProcessSink() = default;
  ~ProcessSink() = default;

  const debug_ipc::ResumeRequest& resume_request() const { return resume_request_; }
  int resume_count() const { return resume_count_; }

  void Resume(const debug_ipc::ResumeRequest& request,
              fit::callback<void(const Err&, debug_ipc::ResumeReply)> cb) override {
    resume_count_++;
    resume_request_ = request;
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb)]() mutable { cb(Err(), debug_ipc::ResumeReply()); });
  }

  // No-op.
  void Threads(const debug_ipc::ThreadsRequest& request,
               fit::callback<void(const Err&, debug_ipc::ThreadsReply)> cb) override {
    thread_request_made_ = true;
  }

  bool thread_request_made() const { return thread_request_made_; }

 private:
  debug_ipc::ResumeRequest resume_request_;
  int resume_count_ = 0;

  bool thread_request_made_ = false;
};

class ProcessImplTest : public RemoteAPITest {
 public:
  ProcessImplTest() = default;
  ~ProcessImplTest() override = default;

  ProcessSink* sink() { return sink_; }

 private:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() override {
    auto sink = std::make_unique<ProcessSink>();
    sink_ = sink.get();
    return sink;
  }

 private:
  ProcessSink* sink_;  // Owned by the session.
};

}  // namespace

// Tests that the correct threads are resumed after the modules are loaded.
TEST_F(ProcessImplTest, OnModules) {
  constexpr uint64_t kProcessKoid = 1234;
  Process* process = InjectProcess(kProcessKoid);
  ASSERT_TRUE(process);

  EXPECT_FALSE(sink()->thread_request_made());

  debug_ipc::NotifyModules notify;
  notify.process_koid = kProcessKoid;
  notify.modules.resize(1);
  notify.modules[0].name = "comctl32.dll";
  notify.modules[0].base = 0x7685348234;

  constexpr uint64_t kThread1Koid = 237645;
  constexpr uint64_t kThread2Koid = 809712;
  notify.stopped_thread_koids.push_back(kThread1Koid);
  notify.stopped_thread_koids.push_back(kThread2Koid);

  session().DispatchNotifyModules(notify);

  EXPECT_TRUE(sink()->thread_request_made());

  // Should have resumed both of those threads.
  ASSERT_EQ(1, sink()->resume_count());
  const debug_ipc::ResumeRequest& resume = sink()->resume_request();
  EXPECT_EQ(kProcessKoid, resume.process_koid);
  EXPECT_EQ(debug_ipc::ResumeRequest::How::kContinue, resume.how);
  EXPECT_EQ(notify.stopped_thread_koids, resume.thread_koids);
}

TEST_F(ProcessImplTest, GetTLSHelpers) {
  constexpr uint64_t kProcessKoid = 1234;
  constexpr uint64_t kThreadKoid = 5678;
  constexpr char kBuildId[] = "abcd";
  constexpr uint64_t kModuleBase = 0x1000;
  Process* process = InjectProcess(kProcessKoid);
  ASSERT_TRUE(process);
  Thread* thread = InjectThread(kProcessKoid, kThreadKoid);
  ASSERT_TRUE(thread);
  SymbolContext symbol_context(kModuleBase);

  auto module_symbols = fxl::MakeRefCounted<MockModuleSymbols>("libc.so");
  session().system().GetSymbols()->InjectModuleForTesting(kBuildId, module_symbols.get());

  std::vector<debug_ipc::Module> modules;
  debug_ipc::Module module;
  module.name = "libc";
  module.base = kModuleBase;
  module.build_id = kBuildId;
  modules.push_back(module);

  TargetImpl* target = session().system().GetTargetImpls()[0];
  target->process()->OnModules(modules, std::vector<uint64_t>());

  constexpr uint64_t kThrdTAddr = 0x1000;
  constexpr uint64_t kLinkMapTlsModIdAddr = 0x2000;
  constexpr uint64_t kTlsBaseAddr = 0x3000;
  constexpr uint64_t kSymSize = 4;

  ElfSymbolRecord thrd_t_record(ElfSymbolType::kNormal, kThrdTAddr, kSymSize, "zxdb.thrd_t");
  ElfSymbolRecord link_map_tls_modid_record(ElfSymbolType::kNormal, kLinkMapTlsModIdAddr, kSymSize,
                                            "zxdb.link_map_tls_modid");
  ElfSymbolRecord tlsbase_record(ElfSymbolType::kNormal, kTlsBaseAddr, kSymSize, "zxdb.tlsbase");

  auto thrd_t_sym = fxl::MakeRefCounted<ElfSymbol>(module_symbols->GetWeakPtr(), thrd_t_record);
  auto link_map_tls_modid_sym =
      fxl::MakeRefCounted<ElfSymbol>(module_symbols->GetWeakPtr(), link_map_tls_modid_record);
  auto tlsbase_sym = fxl::MakeRefCounted<ElfSymbol>(module_symbols->GetWeakPtr(), tlsbase_record);

  module_symbols->AddSymbolLocations("zxdb.thrd_t", {Location(kThrdTAddr + kModuleBase, FileLine(),
                                                              0, symbol_context, thrd_t_sym)});
  module_symbols->AddSymbolLocations("zxdb.link_map_tls_modid",
                                     {Location(kLinkMapTlsModIdAddr + kModuleBase, FileLine(), 0,
                                               symbol_context, link_map_tls_modid_sym)});
  module_symbols->AddSymbolLocations(
      "zxdb.tlsbase",
      {Location(kTlsBaseAddr + kModuleBase, FileLine(), 0, symbol_context, tlsbase_sym)});

  sink()->AddMemory(kThrdTAddr + kModuleBase, {0x1, 0x2, 0x3, 0x4});
  sink()->AddMemory(kLinkMapTlsModIdAddr + kModuleBase, {0x5, 0x6, 0x7, 0x8});
  sink()->AddMemory(kTlsBaseAddr + kModuleBase, {0x9, 0xa, 0xb, 0xc});

  bool called = false;
  bool called_2 = false;
  Process::TLSHelpers helpers;
  Process::TLSHelpers helpers_2;
  Err err;
  Err err_2;
  process->GetTLSHelpers([&called, &helpers, &err](ErrOr<const Process::TLSHelpers*> got) {
    called = true;

    if (got.has_error()) {
      err = got.err();
    } else {
      helpers = *got.value();
    }
  });

  process->GetTLSHelpers([&called_2, &helpers_2, &err_2](ErrOr<const Process::TLSHelpers*> got) {
    called_2 = true;

    if (got.has_error()) {
      err_2 = got.err();
    } else {
      helpers_2 = *got.value();
    }
  });

  EXPECT_FALSE(called);
  EXPECT_FALSE(called_2);
  loop().RunUntilNoTasks();
  EXPECT_TRUE(called);
  EXPECT_TRUE(called_2);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_FALSE(err_2.has_error());

  ASSERT_EQ(helpers.thrd_t.size(), kSymSize);
  ASSERT_EQ(helpers.link_map_tls_modid.size(), kSymSize);
  ASSERT_EQ(helpers.tlsbase.size(), kSymSize);

  EXPECT_EQ(0x1, helpers.thrd_t[0]);
  EXPECT_EQ(0x2, helpers.thrd_t[1]);
  EXPECT_EQ(0x3, helpers.thrd_t[2]);
  EXPECT_EQ(0x4, helpers.thrd_t[3]);

  EXPECT_EQ(0x5, helpers.link_map_tls_modid[0]);
  EXPECT_EQ(0x6, helpers.link_map_tls_modid[1]);
  EXPECT_EQ(0x7, helpers.link_map_tls_modid[2]);
  EXPECT_EQ(0x8, helpers.link_map_tls_modid[3]);

  EXPECT_EQ(0x9, helpers.tlsbase[0]);
  EXPECT_EQ(0xa, helpers.tlsbase[1]);
  EXPECT_EQ(0xb, helpers.tlsbase[2]);
  EXPECT_EQ(0xc, helpers.tlsbase[3]);

  EXPECT_EQ(0x1, helpers_2.thrd_t[0]);
  EXPECT_EQ(0x2, helpers_2.thrd_t[1]);
  EXPECT_EQ(0x3, helpers_2.thrd_t[2]);
  EXPECT_EQ(0x4, helpers_2.thrd_t[3]);

  EXPECT_EQ(0x5, helpers_2.link_map_tls_modid[0]);
  EXPECT_EQ(0x6, helpers_2.link_map_tls_modid[1]);
  EXPECT_EQ(0x7, helpers_2.link_map_tls_modid[2]);
  EXPECT_EQ(0x8, helpers_2.link_map_tls_modid[3]);

  EXPECT_EQ(0x9, helpers_2.tlsbase[0]);
  EXPECT_EQ(0xa, helpers_2.tlsbase[1]);
  EXPECT_EQ(0xb, helpers_2.tlsbase[2]);
  EXPECT_EQ(0xc, helpers_2.tlsbase[3]);

  called = false;
  process->GetTLSHelpers([&called, &helpers, &err](ErrOr<const Process::TLSHelpers*> got) {
    called = true;

    if (got.has_error()) {
      err = got.err();
    } else {
      helpers = *got.value();
    }
  });

  EXPECT_TRUE(called);
  ASSERT_FALSE(err.has_error()) << err.msg();

  ASSERT_EQ(helpers.thrd_t.size(), kSymSize);
  ASSERT_EQ(helpers.link_map_tls_modid.size(), kSymSize);
  ASSERT_EQ(helpers.tlsbase.size(), kSymSize);

  EXPECT_EQ(0x1, helpers.thrd_t[0]);
  EXPECT_EQ(0x2, helpers.thrd_t[1]);
  EXPECT_EQ(0x3, helpers.thrd_t[2]);
  EXPECT_EQ(0x4, helpers.thrd_t[3]);

  EXPECT_EQ(0x5, helpers.link_map_tls_modid[0]);
  EXPECT_EQ(0x6, helpers.link_map_tls_modid[1]);
  EXPECT_EQ(0x7, helpers.link_map_tls_modid[2]);
  EXPECT_EQ(0x8, helpers.link_map_tls_modid[3]);

  EXPECT_EQ(0x9, helpers.tlsbase[0]);
  EXPECT_EQ(0xa, helpers.tlsbase[1]);
  EXPECT_EQ(0xb, helpers.tlsbase[2]);
  EXPECT_EQ(0xc, helpers.tlsbase[3]);
}

}  // namespace zxdb
