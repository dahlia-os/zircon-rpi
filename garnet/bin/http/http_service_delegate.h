// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HTTP_HTTP_SERVICE_DELEGATE_H_
#define GARNET_BIN_HTTP_HTTP_SERVICE_DELEGATE_H_

#include <lib/sys/cpp/component_context.h>

#include <memory>

#include "garnet/bin/http/http_service_impl.h"
#include "src/lib/fxl/macros.h"

namespace http {

class HttpServiceDelegate {
 public:
  HttpServiceDelegate(async_dispatcher_t* dispatcher);
  ~HttpServiceDelegate();

 private:
  std::unique_ptr<sys::ComponentContext> context_;
  HttpServiceImpl http_provider_;

  FXL_DISALLOW_COPY_AND_ASSIGN(HttpServiceDelegate);
};

}  // namespace http

#endif  // GARNET_BIN_HTTP_HTTP_SERVICE_DELEGATE_H_
