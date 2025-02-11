// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "instance.h"

#include <fuchsia/hardware/goldfish/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl-utils/bind.h>
#include <lib/zx/bti.h>
#include <zircon/threads.h>

#include <ddk/debug.h>

#include "pipe.h"

namespace goldfish {
namespace {

const char kTag[] = "goldfish-pipe";

}  // namespace

Instance::Instance(zx_device_t* parent)
    : InstanceType(parent), client_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

Instance::~Instance() {
  client_loop_.Quit();
  thrd_join(client_thread_, nullptr);
  client_loop_.Shutdown();
}

zx_status_t Instance::Bind() {
  // Create the thread here using thrd_create_with_name instead of
  // using the async loop's StartThread functionality. This provides
  // a clean way to ensure that all items in |pipes_| are destroyed
  // on the thread they were created.
  int rc = thrd_create_with_name(
      &client_thread_, [](void* arg) -> int { return static_cast<Instance*>(arg)->ClientThread(); },
      this, "goldfish_pipe_client_thread");
  if (rc != thrd_success) {
    return thrd_status_to_zx_status(rc);
  }

  return DdkAdd("pipe", DEVICE_ADD_INSTANCE);
}

zx_status_t Instance::FidlOpenPipe(zx_handle_t pipe_request_handle) {
  zx::channel pipe_request(pipe_request_handle);
  if (!pipe_request.is_valid()) {
    zxlogf(ERROR, "%s: invalid channel", kTag);
    return ZX_ERR_INVALID_ARGS;
  }

  // Create and bind pipe to client thread.
  async::PostTask(client_loop_.dispatcher(), [this, request = std::move(pipe_request)]() mutable {
    auto pipe = Pipe::Create(parent(), client_loop_.dispatcher());
    pipe->SetErrorHandler([this, pipe_ptr = pipe.get()](zx_status_t status) {
      // |status| passed to an error handler is never ZX_OK.
      // Clean close is ZX_ERR_PEER_CLOSED.
      ZX_DEBUG_ASSERT(status != ZX_OK);
      // We know |pipe_ptr| is still alive because |pipe_ptr| is still in |pipes_|.
      ZX_DEBUG_ASSERT(pipes_.find(pipe_ptr) != pipes_.end());

      if (status != ZX_ERR_PEER_CLOSED) {
        zxlogf(ERROR, "%s: pipe error: %d", kTag, status);
      }
      pipes_.erase(pipe_ptr);
    });
    pipe->Bind(std::move(request));
    // Init() must be called after Bind() as it can cause an asynchronous
    // failure. The pipe will be cleaned up later by the error handler in
    // the event of a failure.
    pipe->Init();
    auto pipe_ptr = pipe.get();
    pipes_.insert({pipe_ptr, std::move(pipe)});
  });

  return ZX_OK;
}

zx_status_t Instance::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  using Binder = fidl::Binder<Instance>;

  static const fuchsia_hardware_goldfish_PipeDevice_ops_t kOps = {
      .OpenPipe = Binder::BindMember<&Instance::FidlOpenPipe>,
  };

  return fuchsia_hardware_goldfish_PipeDevice_dispatch(this, txn, msg, &kOps);
}

zx_status_t Instance::DdkClose(uint32_t flags) { return ZX_OK; }

void Instance::DdkRelease() { delete this; }

int Instance::ClientThread() {
  // Run until Quit() is called in dtor.
  client_loop_.Run();

  // Cleanup pipes that are still open.
  pipes_.clear();

  return 0;
}

}  // namespace goldfish
