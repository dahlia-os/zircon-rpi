// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_STORAGE_LIB_PAVER_SKIP_BLOCK_H_
#define SRC_STORAGE_LIB_PAVER_SKIP_BLOCK_H_

#include <fuchsia/hardware/skipblock/llcpp/fidl.h>

#include "src/storage/lib/paver/device-partitioner.h"
#include "src/storage/lib/paver/partition-client.h"

namespace paver {

// DevicePartitioner implementation for devices which have fixed partition maps, but do not expose a
// block device interface. Instead they expose devices with skip-block IOCTL interfaces. Like the
// FixedDevicePartitioner, it will not attempt to write a partition map of any kind to the device.
// Assumes standardized partition layout structure (e.g. ZIRCON-A, ZIRCON-B,
// ZIRCON-R).
class SkipBlockDevicePartitioner {
 public:
  SkipBlockDevicePartitioner(fbl::unique_fd devfs_root) : devfs_root_(std::move(devfs_root)) {}

  zx::status<std::unique_ptr<PartitionClient>> FindPartition(const uint8_t* guid) const;

  zx::status<std::unique_ptr<PartitionClient>> FindFvmPartition() const;

  zx::status<> WipeFvm() const;

  fbl::unique_fd& devfs_root() { return devfs_root_; }

 private:
  fbl::unique_fd devfs_root_;
};

class SkipBlockPartitionClient : public PartitionClient {
 public:
  explicit SkipBlockPartitionClient(zx::channel partition) : partition_(std::move(partition)) {}

  zx::status<size_t> GetBlockSize() override;
  zx::status<size_t> GetPartitionSize() override;
  zx::status<> Read(const zx::vmo& vmo, size_t size) override;
  zx::status<> Write(const zx::vmo& vmo, size_t vmo_size) override;
  zx::status<> Trim() override;
  zx::status<> Flush() override;
  zx::channel GetChannel() override;
  fbl::unique_fd block_fd() override;

  // No copy, no move.
  SkipBlockPartitionClient(const SkipBlockPartitionClient&) = delete;
  SkipBlockPartitionClient& operator=(const SkipBlockPartitionClient&) = delete;
  SkipBlockPartitionClient(SkipBlockPartitionClient&&) = delete;
  SkipBlockPartitionClient& operator=(SkipBlockPartitionClient&&) = delete;

 protected:
  zx::status<> WriteBytes(const zx::vmo& vmo, zx_off_t offset, size_t vmo_size);

 private:
  zx::status<> ReadPartitionInfo();

  ::llcpp::fuchsia::hardware::skipblock::SkipBlock::SyncClient partition_;
  std::optional<::llcpp::fuchsia::hardware::skipblock::PartitionInfo> partition_info_;
};

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_SKIP_BLOCK_H_
