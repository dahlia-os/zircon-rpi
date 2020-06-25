// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_STORAGE_LIB_PAVER_GPT_H_
#define SRC_STORAGE_LIB_PAVER_GPT_H_

#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include <fbl/function.h>
#include <gpt/gpt.h>

#include "src/storage/lib/paver/device-partitioner.h"

namespace paver {

using gpt::GptDevice;

// Useful for when a GPT table is available (e.g. x86 devices). Provides common
// utility functions.
class GptDevicePartitioner {
 public:
  using FilterCallback = fbl::Function<bool(const gpt_partition_t&)>;

  struct InitializeGptResult {
    std::unique_ptr<GptDevicePartitioner> gpt;
    bool initialize_partition_tables;
  };

  // Find and initialize a GPT based device.
  //
  // If block_device is provided, then search is skipped, and block_device is used
  // directly. If it is not provided, we search for a device with a valid GPT,
  // with an entry for an FVM. If multiple devices with valid GPT containing
  // FVM entries are found, an error is returned.
  static zx::status<InitializeGptResult> InitializeGpt(fbl::unique_fd devfs_root,
                                                       const zx::channel& svc_root,
                                                       const fbl::unique_fd& block_device);

  // Returns block info for a specified block device.
  const ::llcpp::fuchsia::hardware::block::BlockInfo& GetBlockInfo() const { return block_info_; }

  GptDevice* GetGpt() const { return gpt_.get(); }
  zx::unowned_channel Channel() const { return caller_.channel(); }

  struct FindFirstFitResult {
    size_t start;
    size_t length;
  };

  // Find the first spot that has at least |bytes_requested| of space.
  //
  // Returns the |start_out| block and |length_out| blocks, indicating
  // how much space was found, on success. This may be larger than
  // the number of bytes requested.
  zx::status<FindFirstFitResult> FindFirstFit(size_t bytes_requested) const;

  // Creates a partition, adds an entry to the GPT, and returns a file descriptor to it.
  // Assumes that the partition does not already exist.
  zx::status<std::unique_ptr<PartitionClient>> AddPartition(const char* name, const uint8_t* type,
                                                            size_t minimum_size_bytes,
                                                            size_t optional_reserve_bytes) const;

  struct FindPartitionResult {
    std::unique_ptr<PartitionClient> partition;
    gpt_partition_t* gpt_partition;
  };

  // Returns a file descriptor to a partition which can be paved,
  // if one exists.
  zx::status<FindPartitionResult> FindPartition(FilterCallback filter) const;

  // Wipes a specified partition from the GPT, and overwrites first 8KiB with
  // nonsense.
  zx::status<> WipeFvm() const;

  // Removes all partitions from GPT.
  zx::status<> WipePartitionTables() const;

  // Wipes all partitions meeting given criteria.
  zx::status<> WipePartitions(FilterCallback filter) const;

  const fbl::unique_fd& devfs_root() { return devfs_root_; }

  const zx::channel& svc_root() { return svc_root_; }

 private:
  using GptDevices = std::vector<std::pair<std::string, fbl::unique_fd>>;

  // Find all block devices which could contain a GPT.
  static bool FindGptDevices(const fbl::unique_fd& devfs_root, GptDevices* out);

  // Initializes GPT for a device which was explicitly provided. If |gpt_device| doesn't have a
  // valid GPT, it will initialize it with a valid one.
  static zx::status<std::unique_ptr<GptDevicePartitioner>> InitializeProvidedGptDevice(
      fbl::unique_fd devfs_root, const zx::channel& svc_root, fbl::unique_fd gpt_device);

  GptDevicePartitioner(fbl::unique_fd devfs_root, const zx::channel& svc_root, fbl::unique_fd fd,
                       std::unique_ptr<GptDevice> gpt,
                       ::llcpp::fuchsia::hardware::block::BlockInfo block_info)
      : devfs_root_(std::move(devfs_root)),
        svc_root_(fdio_service_clone(svc_root.get())),
        caller_(std::move(fd)),
        gpt_(std::move(gpt)),
        block_info_(block_info) {}

  zx::status<std::array<uint8_t, GPT_GUID_LEN>> CreateGptPartition(const char* name,
                                                                   const uint8_t* type,
                                                                   uint64_t offset,
                                                                   uint64_t blocks) const;

  fbl::unique_fd devfs_root_;
  zx::channel svc_root_;
  fdio_cpp::FdioCaller caller_;
  mutable std::unique_ptr<GptDevice> gpt_;
  ::llcpp::fuchsia::hardware::block::BlockInfo block_info_;
};

using GptGuid = std::array<uint8_t, GPT_GUID_LEN>;

zx::status<GptGuid> GptPartitionType(Partition type);

zx::status<> RebindGptDriver(const zx::channel& svc_root, zx::unowned_channel chan);

inline void utf16_to_cstring(char* dst, const uint8_t* src, size_t charcount) {
  while (charcount > 0) {
    *dst++ = *src;
    src += 2;
    charcount -= 2;
  }
}

inline bool FilterByType(const gpt_partition_t& part,
                         const std::array<uint8_t, GPT_GUID_LEN>& type) {
  return memcmp(part.type, type.data(), GPT_GUID_LEN) == 0;
}

bool FilterByTypeAndName(const gpt_partition_t& part, const std::array<uint8_t, GPT_GUID_LEN>& type,
                         fbl::StringPiece name);

inline bool IsFvmPartition(const gpt_partition_t& part) {
  const std::array<uint8_t, GPT_GUID_LEN> partition_type = GUID_FVM_VALUE;
  return FilterByType(part, partition_type);
}

// Returns true if the spec partition is Zircon A/B/R.
inline bool IsZirconPartitionSpec(const PartitionSpec& spec) {
  return spec.partition == Partition::kZirconA || spec.partition == Partition::kZirconB ||
         spec.partition == Partition::kZirconR;
}

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_GPT_H_
