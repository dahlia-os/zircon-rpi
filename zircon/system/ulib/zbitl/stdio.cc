// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cksum.h>
#include <lib/zbitl/stdio.h>

#include <cerrno>
#include <cstdio>

namespace zbitl {
namespace {

constexpr size_t kBufferSize = BUFSIZ;

using error_type = StorageTraits<FILE*>::error_type;

}  // namespace

std::variant<error_type, uint32_t> StorageTraits<FILE*>::Capacity(FILE* f) {
  long int eof = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
  if (eof < 0) {
    return errno;
  }
  return static_cast<uint32_t>(
      std::min(static_cast<long int>(std::numeric_limits<uint32_t>::max()), eof));
}

std::variant<error_type, zbi_header_t> StorageTraits<FILE*>::Header(FILE* f, uint32_t offset) {
  zbi_header_t header;
  if (fseek(f, offset, SEEK_SET) != 0) {
    return errno;
  }
  if (fread(&header, sizeof(header), 1, f) != 1) {
    return ferror(f) ? errno : ESPIPE;
  }
  return header;
}

std::variant<error_type, uint32_t> StorageTraits<FILE*>::Crc32(FILE* f, uint32_t offset,
                                                               uint32_t length) {
  if (fseek(f, offset, SEEK_SET) != 0) {
    return errno;
  }

  auto size = [&]() { return std::min(static_cast<size_t>(length), kBufferSize); };
  auto buf = std::make_unique<uint8_t[]>(size());

  uint32_t crc = 0;
  while (length > 0) {
    size_t n = fread(buf.get(), 1, size(), f);
    if (n == 0) {
      return ferror(f) ? errno : ESPIPE;
    }
    ZX_ASSERT(n <= kBufferSize);
    crc = crc32(crc, buf.get(), n);
    length -= n;
  }

  return crc;
}

}  // namespace zbitl
