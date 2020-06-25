// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DEVICE_ID_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DEVICE_ID_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include <string>

#include "src/developer/forensics/feedback_data/annotations/types.h"

namespace forensics {
namespace feedback_data {

// Manages and provides the device id at the provided path.
class DeviceIdProvider : public fuchsia::feedback::DeviceIdProvider {
 public:
  DeviceIdProvider(const std::string& path);

  AnnotationOr GetId();

  // |fuchsia.feedback.DeviceIdProvider|
  void GetId(GetIdCallback callback) override;

 private:
  AnnotationOr device_id_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DEVICE_ID_PROVIDER_H_
