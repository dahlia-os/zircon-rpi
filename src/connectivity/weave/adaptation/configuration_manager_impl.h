// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONFIGURATION_MANAGER_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONFIGURATION_MANAGER_IMPL_H_

// clang-format off
#include <Weave/DeviceLayer/WeaveDeviceConfig.h>
#include <Weave/DeviceLayer/internal/GenericConfigurationManagerImpl.h>
// clang-format on

#include <fuchsia/factory/cpp/fidl.h>
#include <fuchsia/hwinfo/cpp/fidl.h>
#include <fuchsia/weave/cpp/fidl.h>
#include <fuchsia/wlan/device/service/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include "src/connectivity/weave/adaptation/environment_config.h"
#include "src/connectivity/weave/adaptation/weave_config_manager.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {

/**
 * Defines the ConfigurationManager singleton object for OpenWeave. This class
 * internally proxies calls on ConfigurationMgr to either the delegate that
 * contains the platform-specific implementation, or default implementations
 * provided by GenericConfigurationManagerImpl.
 *
 * This class and its name scheme is enforced by the adaptation layer.
 */
class ConfigurationManagerImpl final
    : public ConfigurationManager,
      public Internal::GenericConfigurationManagerImpl<ConfigurationManagerImpl>,
      private Internal::EnvironmentConfig {
  // Allow the ConfigurationManager interface class to delegate method calls to
  // the implementation methods provided by this class.
  friend class ConfigurationManager;

  // Allow the GenericConfigurationManagerImpl base class to access helper methods and types
  // defined on this class.
  friend class Internal::GenericConfigurationManagerImpl<ConfigurationManagerImpl>;

 public:
  /**
   * Delegate class to handle platform-specific implementations of the
   * ConfigurationManager API surface. This enables tests to swap out the
   * implementation of the static ConfigurationManager instance.
   */
  class Delegate {
   public:
    using GroupKeyStoreBase = ::nl::Weave::Profiles::Security::AppKeys::GroupKeyStoreBase;
    using Key = ::nl::Weave::Platform::PersistedStorage::Key;

    virtual ~Delegate() = default;

    // Provides a handle to ConfigurationManagerImpl object that this delegate
    // was attached to. This allows the delegate to invoke functions on
    // GenericConfigurationManagerImpl if required.
    void SetConfigurationManagerImpl(ConfigurationManagerImpl* impl) { impl_ = impl; }

    // ConfigurationManager APIs.

    // Performs any required initialization for the delegate. This method must
    // be called before other methods.
    virtual WEAVE_ERROR Init(void) = 0;

    // Populates |device_id| with the weave device ID.
    virtual WEAVE_ERROR GetDeviceId(uint64_t& device_id) = 0;

    // Populates |buf| with the firmware revision, where |buf_size| is the
    // length of |buf| in bytes. If |buf| does not have enough capacity to hold
    // the firmware revision, |out_len| is not modified. Otherwise, |out_len|
    // contains the size of the data.
    virtual WEAVE_ERROR GetFirmwareRevision(char* buf, size_t buf_size, size_t& out_len) = 0;

    // Populates |buf| with the manufacturer device certificate, where
    // |buf_size| is the length of |buf| in bytes. If |buf| does not have
    // enough capacity to hold the firmware revision, |out_len| is not modified.
    // Otherwise, |out_len| contains the size of the data.
    virtual WEAVE_ERROR GetManufacturerDeviceCertificate(uint8_t* buf, size_t buf_size,
                                                         size_t& out_len) = 0;

    // Populates |product_id| with the weave product ID.
    virtual WEAVE_ERROR GetProductId(uint16_t& product_id) = 0;

    // Populates |buf| with the MAC address of the primary WiFi interface.
    // It is expected that the |buf| has at least |ETH_MAX| bytes.
    virtual WEAVE_ERROR GetPrimaryWiFiMACAddress(uint8_t* buf) = 0;

    // Populates |vendor_id| with the weave vendor ID.
    virtual WEAVE_ERROR GetVendorId(uint16_t& vendor_id) = 0;

    // Gets the instance of group key store used by this ConfigurationManager.
    virtual GroupKeyStoreBase* GetGroupKeyStore(void) = 0;

    // Returns whether factory reset is supported.
    virtual bool CanFactoryReset(void) = 0;

    // Erases all mutable configuration held by the delegate.
    virtual void InitiateFactoryReset(void) = 0;

    // Reads stored K/V int pairs and writes them to |value|. If the key is not
    // found or is not of 'uint32_t' type, |value| is unmodified.
    virtual WEAVE_ERROR ReadPersistedStorageValue(Key key, uint32_t& value) = 0;

    // Writes K/V int pair to store.
    virtual WEAVE_ERROR WritePersistedStorageValue(Key key, uint32_t value) = 0;

    // Acquires the BLE device name prefix and populates it in
    // |device_name_prefix|, with the length in |out_len|. If the provided
    // |buf_size| is not sufficient, |out_len| will not be modified.
    virtual WEAVE_ERROR GetBleDeviceNamePrefix(char* device_name_prefix,
                                               size_t device_name_prefix_size, size_t* out_len) = 0;

    // Returns whether WoBLE is enabled.
    virtual bool IsWoBLEEnabled() = 0;

    // Acquires the device descriptor in Weave TLV format and populates it in
    // |buf|, with the length in |encoded_len|. If the provided |buf_size| is
    // not sufficient, |encoded_len| will not be modified.
    virtual WEAVE_ERROR GetDeviceDescriptorTLV(uint8_t* buf, size_t buf_size,
                                               size_t& encoded_len) = 0;

   protected:
    ConfigurationManagerImpl* impl_;
  };

  // Sets the delegate containing the platform-specific implementation. It is
  // invalid to invoke the ConfigurationManager without setting a delegate
  // first. However, the OpenWeave surface requires a no-constructor
  // instantiation of this class, so it is up to the caller to enforce this.
  void SetDelegate(std::unique_ptr<Delegate> delegate);

  // Gets the delegate currently in use. This may return nullptr if no delegate
  // was set on this class.
  Delegate* GetDelegate();

  // Reads the BLE device name prefix, see definition in delegate.
  WEAVE_ERROR GetBleDeviceNamePrefix(char* device_name_prefix, size_t device_name_prefix_size,
                                     size_t* out_len);

  // Returns whether WoBLE is enabled, see definition in delegate.
  bool IsWoBLEEnabled();

 private:
  using GroupKeyStoreBase = ::nl::Weave::Profiles::Security::AppKeys::GroupKeyStoreBase;
  using Key = ::nl::Weave::Platform::PersistedStorage::Key;

  std::unique_ptr<Delegate> delegate_;

  // ConfigurationManagerImpl APIs. These are proxy functions that invoke
  // functions of the same prototype in the |delegate_|. The function prototype
  // definitions are owned by ConfigurationManager in OpenWeave.
  WEAVE_ERROR _Init(void);
  WEAVE_ERROR _GetDeviceId(uint64_t& device_id);
  WEAVE_ERROR _GetFirmwareRevision(char* buf, size_t buf_size, size_t& out_len);
  WEAVE_ERROR _GetManufacturerDeviceCertificate(uint8_t* buf, size_t buf_size, size_t& out_len);
  WEAVE_ERROR _GetProductId(uint16_t& product_id);
  WEAVE_ERROR _GetPrimaryWiFiMACAddress(uint8_t* buf);
  WEAVE_ERROR _GetVendorId(uint16_t& vendor_id);
  WEAVE_ERROR _GetDeviceDescriptorTLV(uint8_t* buf, size_t buf_size, size_t& encoded_len);

  GroupKeyStoreBase* _GetGroupKeyStore(void);
  bool _CanFactoryReset(void);
  void _InitiateFactoryReset(void);

  WEAVE_ERROR _ReadPersistedStorageValue(::nl::Weave::Platform::PersistedStorage::Key key,
                                         uint32_t& value);
  WEAVE_ERROR _WritePersistedStorageValue(::nl::Weave::Platform::PersistedStorage::Key key,
                                          uint32_t value);

  // Friend functions that are used by OpenWeave core implementations to access
  // the static instance of ConfigurationManager and ConfigurationManagerImpl.
  friend ConfigurationManager& ConfigurationMgr(void);
  friend ConfigurationManagerImpl& ConfigurationMgrImpl(void);

  static ConfigurationManagerImpl sInstance;
};

/**
 * Returns the public interface of the ConfigurationManager singleton object.
 *
 * Weave applications should use this to access features of the ConfigurationManager object
 * that are common to all platforms.
 */
inline ConfigurationManager& ConfigurationMgr(void) { return ConfigurationManagerImpl::sInstance; }

/**
 * Returns the platform-specific implementation of the ConfigurationManager singleton object.
 *
 * Weave applications can use this to gain access to features of the ConfigurationManager
 * that are specific to the Fuchsia platform.
 */
inline ConfigurationManagerImpl& ConfigurationMgrImpl(void) {
  return ConfigurationManagerImpl::sInstance;
}

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONFIGURATION_MANAGER_IMPL_H_
