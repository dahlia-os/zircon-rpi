// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_UTILS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_UTILS_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include <optional>
#include <string>

#include "src/developer/forensics/feedback_data/annotations/types.h"

namespace forensics {
namespace feedback_data {

AnnotationKeys RestrictAllowlist(const AnnotationKeys& allowlist,
                                 const AnnotationKeys& restrict_to);

// Each annotation in |annotations| that has a value will be converted into a
// fuchshia::feedback::Annotation
std::vector<fuchsia::feedback::Annotation> ToFeedbackAnnotationVector(
    const Annotations& annotations);

std::optional<std::string> ToJsonString(
    const std::vector<fuchsia::feedback::Annotation>& annotations);

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_UTILS_H_
