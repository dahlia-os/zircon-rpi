// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_INTERRUPT_MASTER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_INTERRUPT_MASTER_H_

#include <lib/zx/interrupt.h>
#include <lib/zx/port.h>
#include <zircon/types.h>

#include <atomic>
#include <list>
#include <memory>
#include <thread>

#include <ddk/device.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_interfaces.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_buscore.h"

namespace wlan {
namespace brcmfmac {

// This class implements interrupt provider functionality for the PCIE bus.  It receives hardware
// interrupts over the bus, and distributes them to the driver appropriately.
class PcieInterruptProvider : public InterruptProviderInterface {
 public:
  PcieInterruptProvider();
  ~PcieInterruptProvider() override;

  // Static factory function for PcieInterruptProvider instances.
  static zx_status_t Create(zx_device_t* device, PcieBuscore* buscore,
                            std::unique_ptr<PcieInterruptProvider>* out_interrupt_provider);

  // InterruptProviderInterface implementation.
  zx_status_t AddInterruptHandler(InterruptHandler* handler) override;
  zx_status_t RemoveInterruptHandler(InterruptHandler* handler) override;

 private:
  // Handle the modification of the interrupt handlers list.
  zx_status_t ModifyInterruptHandler(int command, InterruptHandler* handler);

  // Interrupt service function.
  void InterruptServiceFunction();

  // PCIE bus core regs.  We hold on to ownership of this instance throughout our lifetime, as we
  // don't wan't to be switching away the BAR0 window while servicing interrupts.
  PcieBuscore::CoreRegs pci_core_regs_;

  // IRQ handling.
  zx::interrupt pci_interrupt_;
  zx::port pci_interrupt_port_;
  std::list<InterruptHandler*> pci_interrupt_handlers_;
  std::thread pci_interrupt_thread_;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_INTERRUPT_MASTER_H_
