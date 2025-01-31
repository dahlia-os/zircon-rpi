// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/test/interpreter_test.h"

#include <lib/fdio/directory.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "fuchsia/shell/llcpp/fidl.h"
#include "fuchsia/sys/cpp/fidl.h"
#include "lib/async-loop/default.h"
#include "zircon/status.h"

llcpp::fuchsia::shell::ExecuteResult InterpreterTestContext::GetResult() const {
  std::string string = error_stream.str();
  if (!string.empty()) {
    std::cout << string;
  }
  return result;
}

InterpreterTest::InterpreterTest()
    : loop_(&kAsyncLoopConfigAttachToCurrentThread),
      context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {
  ::fidl::InterfaceHandle<fuchsia::io::Directory> directory;

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = "fuchsia-pkg://fuchsia.com/shell_server#meta/shell_server.cmx";
  launch_info.directory_request = directory.NewRequest().TakeChannel();

  fuchsia::sys::LauncherPtr launcher;
  context_->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());

  shell_provider_ = std::make_unique<sys::ServiceDirectory>(std::move(directory));
}

void InterpreterTest::Finish(FinishAction action) {
  std::vector<std::string> no_errors;
  Finish(action, no_errors);
}

void InterpreterTest::Finish(FinishAction action, const std::vector<std::string>& expected_errors) {
  Run(action);
  // Shutdown the interpreter (that also closes the channel => we can't use it anymore after this
  // call).
  auto errors = shell().Shutdown();
  // Checks if the errors are what we expected.
  bool ok = true;
  if (expected_errors.size() != errors->errors.count()) {
    ok = false;
  } else {
    for (size_t i = 0; i < expected_errors.size(); ++i) {
      if (expected_errors[i] != std::string(errors->errors[i].data(), errors->errors[i].size())) {
        ok = false;
        break;
      }
    }
  }
  if (!ok) {
    std::cout << "Shutdown incorrect\n";
    if (!expected_errors.empty()) {
      std::cout << "Expected:\n";
      for (const auto& error : expected_errors) {
        std::cout << "  " << error << '\n';
      }
      if (errors->errors.empty()) {
        std::cout << "Got no error\n";
      } else {
        std::cout << "Got:\n";
      }
    }
    for (const auto& error : errors->errors) {
      std::cout << "  " << std::string(error.data(), error.size()) << '\n';
    }
    ASSERT_TRUE(ok);
  }
  if (action != kError) {
    std::string global_errors = global_error_stream_.str();
    if (!global_errors.empty()) {
      std::cout << global_errors;
    }
  }
}

void InterpreterTest::Run(FinishAction action) {
  llcpp::fuchsia::shell::Shell::EventHandlers handlers;
  bool done = false;
  enum Errs : zx_status_t { kNoContext = 1, kNoResult, kWrongAction };
  std::string msg;
  while (!done) {
    handlers.on_error = [this, &msg, &done, action](
                            uint64_t context_id,
                            fidl::VectorView<llcpp::fuchsia::shell::Location> locations,
                            fidl::StringView error_message) -> zx_status_t {
      if (action == kError) {
        done = true;
      }
      if (context_id == 0) {
        global_error_stream_ << std::string(error_message.data(), error_message.size()) << "\n";
      } else {
        InterpreterTestContext* context = GetContext(context_id);
        if (context == nullptr) {
          msg = "context == nullptr in on_error";
          return kNoContext;
        }
        for (const auto& location : locations) {
          if (location.has_node_id()) {
            context->error_stream << "node " << location.node_id().file_id << ':'
                                  << location.node_id().node_id << ' ';
          }
        }
        context->error_stream << std::string(error_message.data(), error_message.size()) << "\n";
      }
      return ZX_OK;
    };

    handlers.on_dump_done = [this, &done, &msg, action](uint64_t context_id) -> zx_status_t {
      if (action == kDump) {
        done = true;
      }
      InterpreterTestContext* context = GetContext(context_id);
      if (context == nullptr) {
        msg = "context == nullptr in on_dump_done";
        return kNoContext;
      }
      return ZX_OK;
    };

    handlers.on_execution_done = [this, &msg, &done, action](
                                     uint64_t context_id,
                                     llcpp::fuchsia::shell::ExecuteResult result) -> zx_status_t {
      if (action != kExecute) {
        msg = "Expected action: kExecute was: " + std::to_string(action);
        return kWrongAction;
      }
      done = true;

      InterpreterTestContext* context = GetContext(context_id);
      if (context == nullptr) {
        msg = "context == nullptr in on_execution_done";
        return kNoContext;
      }
      context->result = result;
      if (!globals_to_load_.empty() && (result == llcpp::fuchsia::shell::ExecuteResult::OK)) {
        // Now that the execution is finished, loads all the global variables we asked using
        // LoadGlobal.
        for (const auto& global : globals_to_load_) {
          ++pending_globals_;
          fidl::Buffer<llcpp::fuchsia::shell::Node> request_buffer;
          auto& response_buffer =
              to_be_deleted_.emplace_back(new fidl::Buffer<llcpp::fuchsia::shell::Node>());
          auto response = shell().LoadGlobal(request_buffer.view(), fidl::unowned_str(global),
                                             response_buffer->view());
          auto& nodes = response->nodes;
          if (!nodes.empty()) {
            globals_.emplace(global, std::move(response));
          }
        }
      }
      return ZX_OK;
    };

    handlers.on_text_result = [this, &msg, &done, action](uint64_t context_id,
                                                          fidl::StringView result,
                                                          bool partial_result) -> zx_status_t {
      if (action == kTextResult) {
        done = true;
      }
      InterpreterTestContext* context = GetContext(context_id);
      if (context == nullptr) {
        msg = "context == nullptr in on_text_result";
        return kNoContext;
      }
      std::string result_string(result.data(), result.size());
      if (last_text_result_partial_) {
        if (text_results_.empty()) {
          msg = "text results empty";
          return kNoResult;
        }
        text_results_.back() += result_string;
      } else {
        text_results_.emplace_back(std::move(result_string));
      }
      last_text_result_partial_ = partial_result;
      return ZX_OK;
    };

    handlers.on_result = [this, &msg](uint64_t context_id,
                                      fidl::VectorView<llcpp::fuchsia::shell::Node> nodes,
                                      bool partial_result) -> zx_status_t {
      InterpreterTestContext* context = GetContext(context_id);
      if (context == nullptr) {
        msg = "context == nullptr in on_text_result";
        return kNoContext;
      }
      if (partial_result) {
        msg = " partial results not supported";
        return kNoResult;
      }
      shell::common::DeserializeResult deserialize;
      results_.emplace_back(deserialize.Deserialize(nodes));
      return ZX_OK;
    };
    ASSERT_EQ(ZX_OK, shell_->HandleEvents(std::move(handlers))) << msg;
  }
};

InterpreterTestContext* InterpreterTest::CreateContext() {
  uint64_t id = ++last_context_id_;
  auto context = std::make_unique<InterpreterTestContext>(id);
  auto result = context.get();
  contexts_.emplace(id, std::move(context));
  return result;
}

InterpreterTestContext* InterpreterTest::GetContext(uint64_t context_id) {
  auto result = contexts_.find(context_id);
  if (result == contexts_.end()) {
    return nullptr;
  }
  return result->second.get();
}

std::string InterpreterTest::GlobalString(const std::string& name) const {
  auto node = DeserializeGlobal(name);
  if (node == nullptr) {
    return "";
  }
  std::stringstream ss;
  node->Dump(ss);
  return ss.str();
}

void InterpreterTest::SetUp() {
  zx_handle_t client_ch;
  zx_handle_t server_ch;
  zx_channel_create(0, &client_ch, &server_ch);
  zx::channel client_channel(client_ch);
  shell_ = std::make_unique<llcpp::fuchsia::shell::Shell::SyncClient>(std::move(client_channel));

  // Reset context ids.
  last_context_id_ = 0;
  // Resets the global error stream for the test (to be able to run multiple tests).
  global_error_stream_.str() = "";

  zx::channel server_channel(server_ch);
  // Creates a new connection to the server.
  ASSERT_EQ(ZX_OK, shell_provider_->Connect("fuchsia.shell.Shell", std::move(server_channel)));
}
