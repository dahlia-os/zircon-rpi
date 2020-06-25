// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/utils.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/block/partition/llcpp/fidl.h>
#include <fuchsia/hardware/skipblock/llcpp/fidl.h>
#include <fuchsia/sysinfo/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>

#include <gpt/gpt.h>

#include "src/storage/lib/paver/partition-client.h"
#include "src/storage/lib/paver/pave-logging.h"

namespace paver {

namespace {
namespace partition = ::llcpp::fuchsia::hardware::block::partition;
namespace skipblock = ::llcpp::fuchsia::hardware::skipblock;
}  // namespace

// Not static so test can manipulate it.
zx_duration_t g_wipe_timeout = ZX_SEC(3);

BlockWatcherPauser::~BlockWatcherPauser() {
  if (valid_) {
    auto result = watcher_.Resume();
    if (result.status() != ZX_OK) {
      ERROR("Failed to unpause the block watcher: %s\n", zx_status_get_string(result.status()));
    } else if (result->status != ZX_OK) {
      ERROR("Failed to unpause the block watcher: %s\n", zx_status_get_string(result->status));
    }
  }
}

zx::status<BlockWatcherPauser> BlockWatcherPauser::Create(const zx::channel& svc_root) {
  zx::channel local, remote;
  auto status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  status = fdio_service_connect_at(svc_root.get(), llcpp::fuchsia::fshost::BlockWatcher::Name,
                                   remote.release());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  BlockWatcherPauser pauser(std::move(local));
  if (auto status = pauser.Pause(); status.is_error()) {
    return status.take_error();
  }

  return zx::ok(std::move(pauser));
}

zx::status<> BlockWatcherPauser::Pause() {
  auto result = watcher_.Pause();
  auto status = zx::make_status(result.ok() ? result->status : result.status());

  valid_ = status.is_ok();
  return status;
}

zx::status<zx::channel> OpenPartition(const fbl::unique_fd& devfs_root, const char* path,
                                      fbl::Function<bool(const zx::channel&)> should_filter_file,
                                      zx_duration_t timeout) {
  ZX_ASSERT(path != nullptr);

  struct CallbackInfo {
    zx::channel out_partition;
    fbl::Function<bool(const zx::channel&)> should_filter_file;
  };

  CallbackInfo info = {
      .out_partition = zx::channel(),
      .should_filter_file = std::move(should_filter_file),
  };

  auto cb = [](int dirfd, int event, const char* filename, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
      return ZX_OK;
    }
    if ((strcmp(filename, ".") == 0) || strcmp(filename, "..") == 0) {
      return ZX_OK;
    }
    fdio_cpp::UnownedFdioCaller caller(dirfd);

    zx::channel partition_local, partition_remote;
    if (zx::channel::create(0, &partition_local, &partition_remote) != ZX_OK) {
      return ZX_OK;
    }
    if (fdio_service_connect_at(caller.borrow_channel(), filename, partition_remote.release()) !=
        ZX_OK) {
      return ZX_OK;
    }
    auto info = static_cast<CallbackInfo*>(cookie);
    if (info->should_filter_file(partition_local)) {
      return ZX_OK;
    }
    info->out_partition = std::move(partition_local);
    return ZX_ERR_STOP;
  };

  fbl::unique_fd dir_fd(openat(devfs_root.get(), path, O_RDONLY));
  if (!dir_fd) {
    return zx::error(ZX_ERR_IO);
  }

  zx_time_t deadline = zx_deadline_after(timeout);
  if (fdio_watch_directory(dir_fd.get(), cb, deadline, &info) != ZX_ERR_STOP) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  return zx::ok(std::move(info.out_partition));
}

constexpr char kBlockDevPath[] = "class/block/";

zx::status<zx::channel> OpenBlockPartition(const fbl::unique_fd& devfs_root,
                                           const uint8_t* unique_guid, const uint8_t* type_guid,
                                           zx_duration_t timeout) {
  ZX_ASSERT(unique_guid || type_guid);

  auto cb = [&](const zx::channel& chan) {
    if (type_guid) {
      auto result = partition::Partition::Call::GetTypeGuid(zx::unowned(chan));
      if (!result.ok()) {
        return true;
      }
      auto& response = result.value();
      if (response.status != ZX_OK ||
          memcmp(response.guid->value.data(), type_guid, partition::GUID_LENGTH) != 0) {
        return true;
      }
    }
    if (unique_guid) {
      auto result = partition::Partition::Call::GetInstanceGuid(zx::unowned(chan));
      if (!result.ok()) {
        return true;
      }
      const auto& response = result.value();
      if (response.status != ZX_OK ||
          memcmp(response.guid->value.data(), unique_guid, partition::GUID_LENGTH) != 0) {
        return true;
      }
    }
    return false;
  };

  return OpenPartition(devfs_root, kBlockDevPath, cb, timeout);
}

constexpr char kSkipBlockDevPath[] = "class/skip-block/";

zx::status<zx::channel> OpenSkipBlockPartition(const fbl::unique_fd& devfs_root,
                                               const uint8_t* type_guid, zx_duration_t timeout) {
  ZX_ASSERT(type_guid);

  auto cb = [&](const zx::channel& chan) {
    auto result = skipblock::SkipBlock::Call::GetPartitionInfo(zx::unowned(chan));
    if (!result.ok()) {
      return true;
    }
    const auto& response = result.value();
    if (response.status != ZX_OK || memcmp(response.partition_info.partition_guid.data(), type_guid,
                                           skipblock::GUID_LEN) != 0) {
      return true;
    }
    return false;
  };

  return OpenPartition(devfs_root, kSkipBlockDevPath, cb, timeout);
}

bool HasSkipBlockDevice(const fbl::unique_fd& devfs_root) {
  // Our proxy for detected a skip-block device is by checking for the
  // existence of a device enumerated under the skip-block class.
  const uint8_t type[GPT_GUID_LEN] = GUID_ZIRCON_A_VALUE;
  return OpenSkipBlockPartition(devfs_root, type, ZX_SEC(1)).is_ok();
}

// Attempts to open and overwrite the first block of the underlying
// partition. Does not rebind partition drivers.
//
// At most one of |unique_guid| and |type_guid| may be nullptr.
zx::status<> WipeBlockPartition(const fbl::unique_fd& devfs_root, const uint8_t* unique_guid,
                                const uint8_t* type_guid) {
  auto status = OpenBlockPartition(devfs_root, unique_guid, type_guid, g_wipe_timeout);
  if (status.is_error()) {
    ERROR("Warning: Could not open partition to wipe: %s\n", status.status_string());
    return status.take_error();
  }

  // Overwrite the first block to (hackily) ensure the destroyed partition
  // doesn't "reappear" in place.
  BlockPartitionClient block_partition(std::move(status.value()));
  auto status2 = block_partition.GetBlockSize();
  if (status2.is_error()) {
    ERROR("Warning: Could not get block size of partition: %s\n", status2.status_string());
    return status2.take_error();
  }
  const size_t block_size = status2.value();

  // Rely on vmos being 0 initialized.
  zx::vmo vmo;
  {
    auto status =
        zx::make_status(zx::vmo::create(fbl::round_up(block_size, ZX_PAGE_SIZE), 0, &vmo));
    if (status.is_error()) {
      ERROR("Warning: Could not create vmo: %s\n", status.status_string());
      return status.take_error();
    }
  }

  if (auto status = block_partition.Write(vmo, block_size); status.is_error()) {
    ERROR("Warning: Could not write to block device: %s\n", status.status_string());
    return status.take_error();
  }

  if (auto status = block_partition.Flush(); status.is_error()) {
    ERROR("Warning: Failed to synchronize block device: %s\n", status.status_string());
    return status.take_error();
  }

  return zx::ok();
}

zx::status<> IsBoard(const fbl::unique_fd& devfs_root, fbl::StringPiece board_name) {
  zx::channel local, remote;
  auto status = zx::make_status(zx::channel::create(0, &local, &remote));
  if (status.is_error()) {
    return status.take_error();
  }

  fdio_cpp::UnownedFdioCaller caller(devfs_root.get());
  status = zx::make_status(
      fdio_service_connect_at(caller.borrow_channel(), "sys/platform", remote.release()));
  if (status.is_error()) {
    return status.take_error();
  }

  auto result = ::llcpp::fuchsia::sysinfo::SysInfo::Call::GetBoardName(zx::unowned(local));
  status = zx::make_status(result.ok() ? result->status : result.status());
  if (status.is_error()) {
    return status.take_error();
  }
  if (strncmp(result->name.data(), board_name.data(), result->name.size()) == 0) {
    return zx::ok();
  }

  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> IsBootloader(const fbl::unique_fd& devfs_root, fbl::StringPiece vendor) {
  zx::channel local, remote;
  zx::status<> status = zx::make_status(zx::channel::create(0, &local, &remote));
  if (status.is_error()) {
    return status.take_error();
  }

  fdio_cpp::UnownedFdioCaller caller(devfs_root.get());
  status = zx::make_status(
      fdio_service_connect_at(caller.borrow_channel(), "sys/platform", remote.release()));
  if (status.is_error()) {
    return status.take_error();
  }

  auto result = ::llcpp::fuchsia::sysinfo::SysInfo::Call::GetBootloaderVendor(zx::unowned(local));
  status = zx::make_status(result.ok() ? result->status : result.status());
  if (status.is_error()) {
    return status.take_error();
  }
  if (strncmp(result->vendor.data(), vendor.data(), result->vendor.size()) == 0) {
    return zx::ok();
  }

  return zx::error(ZX_ERR_NOT_SUPPORTED);
}
}  // namespace paver
