// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/fd_reader.h"

#include <fcntl.h>
#include <lib/fit/result.h>

#include <cstdio>
#include <cstdlib>

#include <fbl/span.h>
#include <fbl/unique_fd.h>

namespace storage::volume_image {

fit::result<FdReader, std::string> FdReader::Create(std::string_view path) {
  if (path.empty()) {
    return fit::error("Cannot obtain file descriptor from empty path.");
  }

  std::string pathname(path);
  fbl::unique_fd fd(open(pathname.c_str(), O_RDONLY));
  if (!fd.is_valid()) {
    std::string error = "Failed to obtain file descriptor from ";
    error.append(pathname).append(". More specifically ").append(strerror(errno));
    return fit::error(error);
  }
  return fit::ok(FdReader(std::move(fd), path));
}

std::string FdReader::Read(uint64_t offset, fbl::Span<uint8_t> buffer) const {
  size_t bytes_read = 0;
  while (bytes_read < buffer.size()) {
    uint8_t* destination = buffer.data() + bytes_read;
    size_t remaining_bytes = buffer.size() - bytes_read;
    off_t source_offset = offset + bytes_read;
    int result = pread(fd_.get(), destination, remaining_bytes, source_offset);

    if (result < 0) {
      std::string_view error_description(strerror(errno));
      std::string error = "Read failed from ";
      error.append(name_).append(". More specifically ").append(error_description);
      return error;
    }
    if (result == 0) {
      std::string_view error_description(strerror(errno));
      std::string error = "Read failed from  ";
      error.append(name_).append(". End of file reached before reading requested bytes.");
      return error;
    }

    bytes_read += result;
  }
  return std::string();
}

}  // namespace storage::volume_image
