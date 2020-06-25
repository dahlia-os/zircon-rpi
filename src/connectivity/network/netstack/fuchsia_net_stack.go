// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"fmt"
	"sort"
	"syscall/zx/fidl"

	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/eth"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/netdevice"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/routes"

	"fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/hardware/network"
	"fidl/fuchsia/logger"
	"fidl/fuchsia/net"
	"fidl/fuchsia/net/name"
	"fidl/fuchsia/net/stack"
	"fidl/fuchsia/netstack"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	tcpipstack "gvisor.dev/gvisor/pkg/tcpip/stack"
)

type stackImpl struct {
	ns          *Netstack
	dnsWatchers *dnsServerWatcherCollection
}

func getInterfaceInfo(nicInfo tcpipstack.NICInfo) stack.InterfaceInfo {
	ifs := nicInfo.Context.(*ifState)
	ifs.mu.Lock()
	defer ifs.mu.Unlock()

	// TODO(fxbug.dev/52383): distinguish between enabled and link up.
	administrativeStatus := stack.AdministrativeStatusDisabled
	physicalStatus := stack.PhysicalStatusDown
	if ifs.mu.state == link.StateStarted {
		administrativeStatus = stack.AdministrativeStatusEnabled
		physicalStatus = stack.PhysicalStatusUp
	}

	addrs := make([]stack.InterfaceAddress, 0, len(nicInfo.ProtocolAddresses))
	for _, a := range nicInfo.ProtocolAddresses {
		if a.Protocol != ipv4.ProtocolNumber && a.Protocol != ipv6.ProtocolNumber {
			continue
		}
		addrs = append(addrs, stack.InterfaceAddress{
			IpAddress: fidlconv.ToNetIpAddress(a.AddressWithPrefix.Address),
			PrefixLen: uint8(a.AddressWithPrefix.PrefixLen),
		})
	}

	var features uint32
	if ifs.endpoint.Capabilities()&tcpipstack.CapabilityLoopback != 0 {
		features |= ethernet.InfoFeatureLoopback
	}

	var topopath, filepath string
	if client, ok := ifs.controller.(*eth.Client); ok {
		topopath = client.Topopath()
		filepath = client.Filepath()
		features |= client.Info.Features
	}

	mac := &ethernet.MacAddress{}
	copy(mac.Octets[:], ifs.endpoint.LinkAddress())

	return stack.InterfaceInfo{
		Id: uint64(ifs.nicid),
		Properties: stack.InterfaceProperties{
			Name:                 nicInfo.Name,
			Topopath:             topopath,
			Filepath:             filepath,
			Mac:                  mac,
			Mtu:                  ifs.endpoint.MTU(),
			AdministrativeStatus: administrativeStatus,
			PhysicalStatus:       physicalStatus,
			Features:             features,
			Addresses:            addrs,
		},
	}
}

func (ns *Netstack) getNetInterfaces() []stack.InterfaceInfo {
	nicInfos := ns.stack.NICInfo()
	out := make([]stack.InterfaceInfo, 0, len(nicInfos))
	for _, nicInfo := range nicInfos {
		out = append(out, getInterfaceInfo(nicInfo))
	}

	sort.Slice(out, func(i, j int) bool {
		return out[i].Id < out[j].Id
	})
	return out
}

func (ns *Netstack) addInterface(config stack.InterfaceConfig, device stack.DeviceDefinition) stack.StackAddInterfaceResult {

	var namePrefix string
	var ep tcpipstack.LinkEndpoint
	var controller link.Controller

	switch device.Which() {
	case stack.DeviceDefinitionEthernet:
		client, err := netdevice.NewMacAddressingClient(context.Background(), &device.Ethernet.NetworkDevice, &device.Ethernet.Mac, &netdevice.SimpleSessionConfigFactory{
			FrameTypes: []network.FrameType{network.FrameTypeEthernet},
		})
		if err != nil {
			_ = syslog.Warnf("failed to create network device client for Ethernet interface: %s", err)
			return stack.StackAddInterfaceResultWithErr(stack.ErrorInternal)
		}
		ep = eth.NewLinkEndpoint(client)
		controller = client
		namePrefix = "eth"
	case stack.DeviceDefinitionIp:
		client, err := netdevice.NewClient(context.Background(), &device.Ip, &netdevice.SimpleSessionConfigFactory{
			FrameTypes: []network.FrameType{network.FrameTypeIpv4, network.FrameTypeIpv6},
		})
		if err != nil {
			_ = syslog.Warnf("failed to create network device client for IP interface: %s", err)
			return stack.StackAddInterfaceResultWithErr(stack.ErrorInternal)
		}
		ep = client
		controller = client
		namePrefix = "ip"
	default:
		_ = syslog.Errorf("unsupported device definition: %d", device.Which())
		return stack.StackAddInterfaceResultWithErr(stack.ErrorInvalidArgs)
	}

	ifs, err := ns.addEndpoint(makeEndpointName(namePrefix, config.GetNameWithDefault("")), ep, controller, true /* doFilter */, routes.Metric(config.GetMetricWithDefault(0)), false /* enabled */)
	if err != nil {
		_ = syslog.Errorf("ns.addEndpoint failed: %s", err)
		return stack.StackAddInterfaceResultWithErr(stack.ErrorInternal)
	}
	return stack.StackAddInterfaceResultWithResponse(stack.StackAddInterfaceResponse{Id: uint64(ifs.nicid)})
}

func (ns *Netstack) delInterface(id uint64) stack.StackDelEthernetInterfaceResult {
	var result stack.StackDelEthernetInterfaceResult

	nicInfo, ok := ns.stack.NICInfo()[tcpip.NICID(id)]
	if !ok {
		result.SetErr(stack.ErrorNotFound)
		return result
	}

	ifs := nicInfo.Context.(*ifState)
	if err := ifs.controller.Close(); err != nil {
		syslog.Errorf("ifs.controller.Close() failed (NIC: %d): %v", id, err)
		result.SetErr(stack.ErrorInternal)
		return result
	}

	result.SetResponse(stack.StackDelEthernetInterfaceResponse{})
	return result
}

func (ns *Netstack) getInterface(id uint64) stack.StackGetInterfaceInfoResult {
	var result stack.StackGetInterfaceInfoResult

	nicInfo, ok := ns.stack.NICInfo()[tcpip.NICID(id)]
	if !ok {
		result.SetErr(stack.ErrorNotFound)
		return result
	}

	result.SetResponse(stack.StackGetInterfaceInfoResponse{
		Info: getInterfaceInfo(nicInfo),
	})
	return result
}

func (ns *Netstack) enableInterface(id uint64) stack.StackEnableInterfaceResult {
	var result stack.StackEnableInterfaceResult

	nicInfo, ok := ns.stack.NICInfo()[tcpip.NICID(id)]
	if !ok {
		result.SetErr(stack.ErrorNotFound)
		return result
	}

	ifs := nicInfo.Context.(*ifState)
	if err := ifs.controller.Up(); err != nil {
		syslog.Errorf("ifs.controller.Up() failed (NIC %d): %s", id, err)
		result.SetErr(stack.ErrorInternal)
		return result
	}

	result.SetResponse(stack.StackEnableInterfaceResponse{})
	return result
}

func (ns *Netstack) disableInterface(id uint64) stack.StackDisableInterfaceResult {
	var result stack.StackDisableInterfaceResult

	nicInfo, ok := ns.stack.NICInfo()[tcpip.NICID(id)]
	if !ok {
		result.SetErr(stack.ErrorNotFound)
		return result
	}

	ifs := nicInfo.Context.(*ifState)
	if err := ifs.controller.Down(); err != nil {
		syslog.Errorf("ifs.controller.Down() failed (NIC %d): %s", id, err)
		result.SetErr(stack.ErrorInternal)
		return result
	}

	result.SetResponse(stack.StackDisableInterfaceResponse{})
	return result
}

func toProtocolAddr(ifAddr stack.InterfaceAddress) tcpip.ProtocolAddress {
	protocolAddr := tcpip.ProtocolAddress{
		AddressWithPrefix: tcpip.AddressWithPrefix{
			PrefixLen: int(ifAddr.PrefixLen),
		},
	}

	switch typ := ifAddr.IpAddress.Which(); typ {
	case net.IpAddressIpv4:
		protocolAddr.Protocol = ipv4.ProtocolNumber
		protocolAddr.AddressWithPrefix.Address = tcpip.Address(ifAddr.IpAddress.Ipv4.Addr[:])
	case net.IpAddressIpv6:
		protocolAddr.Protocol = ipv6.ProtocolNumber
		protocolAddr.AddressWithPrefix.Address = tcpip.Address(ifAddr.IpAddress.Ipv6.Addr[:])
	default:
		panic(fmt.Sprintf("unknown IpAddress type %d", typ))
	}
	return protocolAddr
}

func (ns *Netstack) addInterfaceAddr(id uint64, ifAddr stack.InterfaceAddress) stack.StackAddInterfaceAddressResult {
	var result stack.StackAddInterfaceAddressResult

	protocolAddr := toProtocolAddr(ifAddr)
	if protocolAddr.AddressWithPrefix.PrefixLen > 8*len(protocolAddr.AddressWithPrefix.Address) {
		result.SetErr(stack.ErrorInvalidArgs)
		return result
	}

	found, err := ns.addInterfaceAddress(tcpip.NICID(id), protocolAddr)
	if err != nil {
		syslog.Errorf("(*Netstack).addInterfaceAddr(%s) failed (NIC %d): %s", protocolAddr.AddressWithPrefix, id, err)
		result.SetErr(stack.ErrorBadState)
		return result
	}

	if !found {
		result.SetErr(stack.ErrorNotFound)
		return result
	}

	result.SetResponse(stack.StackAddInterfaceAddressResponse{})
	return result
}

func (ns *Netstack) delInterfaceAddr(id uint64, ifAddr stack.InterfaceAddress) stack.StackDelInterfaceAddressResult {
	var result stack.StackDelInterfaceAddressResult

	protocolAddr := toProtocolAddr(ifAddr)
	if protocolAddr.AddressWithPrefix.PrefixLen > 8*len(protocolAddr.AddressWithPrefix.Address) {
		result.SetErr(stack.ErrorInvalidArgs)
		return result
	}

	found, err := ns.removeInterfaceAddress(tcpip.NICID(id), protocolAddr)
	if err != nil {
		syslog.Errorf("(*Netstack).delInterfaceAddr(%s) failed (NIC %d): %s", protocolAddr.AddressWithPrefix, id, err)
		result.SetErr(stack.ErrorInternal)
		return result
	}
	if !found {
		result.SetErr(stack.ErrorNotFound)
		return result
	}

	result.SetResponse(stack.StackDelInterfaceAddressResponse{})
	return result
}

func (ns *Netstack) getForwardingTable() []stack.ForwardingEntry {
	ert := ns.GetExtendedRouteTable()
	entries := make([]stack.ForwardingEntry, 0, len(ert))
	for _, er := range ert {
		entries = append(entries, fidlconv.TcpipRouteToForwardingEntry(er.Route))
	}
	return entries
}

// validateSubnet returns true if the prefix length is valid and no
// address bits are set beyond the prefix length.
func validateSubnet(subnet net.Subnet) bool {
	var ipBytes []uint8
	switch typ := subnet.Addr.Which(); typ {
	case net.IpAddressIpv4:
		ipBytes = subnet.Addr.Ipv4.Addr[:]
	case net.IpAddressIpv6:
		ipBytes = subnet.Addr.Ipv6.Addr[:]
	default:
		panic(fmt.Sprintf("unknown IpAddress type %d", typ))
	}
	if int(subnet.PrefixLen) > len(ipBytes)*8 {
		return false
	}
	prefixBytes := subnet.PrefixLen / 8
	ipBytes = ipBytes[prefixBytes:]
	if prefixBits := subnet.PrefixLen - (prefixBytes * 8); prefixBits > 0 {
		// prefixBits is only greater than zero when ipBytes is non-empty.
		mask := uint8((1 << (8 - prefixBits)) - 1)
		ipBytes[0] &= mask
	}
	for _, byte := range ipBytes {
		if byte != 0 {
			return false
		}
	}
	return true
}

func (ns *Netstack) addForwardingEntry(entry stack.ForwardingEntry) stack.StackAddForwardingEntryResult {
	var result stack.StackAddForwardingEntryResult

	if !validateSubnet(entry.Subnet) {
		result.SetErr(stack.ErrorInvalidArgs)
		return result
	}

	if err := ns.AddRoute(fidlconv.ForwardingEntryToTcpipRoute(entry), metricNotSet, false /* not dynamic */); err != nil {
		syslog.Errorf("adding forwarding entry %+v to route table failed: %s", entry, err)
		result.SetErr(stack.ErrorInvalidArgs)
		return result
	}
	result.SetResponse(stack.StackAddForwardingEntryResponse{})
	return result
}

func (ns *Netstack) delForwardingEntry(subnet net.Subnet) stack.StackDelForwardingEntryResult {
	var result stack.StackDelForwardingEntryResult

	if !validateSubnet(subnet) {
		result.SetErr(stack.ErrorInvalidArgs)
		return result
	}

	if err := ns.DelRoute(tcpip.Route{Destination: fidlconv.ToTCPIPSubnet(subnet)}); err != nil {
		syslog.Errorf("deleting forwarding entry %+v from route table failed: %s", subnet, err)
		result.SetErr(stack.ErrorNotFound)
		return result
	}
	result.SetResponse(stack.StackDelForwardingEntryResponse{})
	return result
}

func (ni *stackImpl) AddEthernetInterface(_ fidl.Context, topologicalPath string, device ethernet.DeviceWithCtxInterface) (stack.StackAddEthernetInterfaceResult, error) {
	var result stack.StackAddEthernetInterfaceResult

	ifs, err := ni.ns.addEth(topologicalPath, netstack.InterfaceConfig{}, &device)
	if err != nil {
		result.SetErr(stack.ErrorInternal)
		return result, nil
	}

	result.SetResponse(stack.StackAddEthernetInterfaceResponse{
		Id: uint64(ifs.nicid),
	})
	return result, nil
}

func (ni *stackImpl) AddInterface(_ fidl.Context, config stack.InterfaceConfig, device stack.DeviceDefinition) (stack.StackAddInterfaceResult, error) {
	return ni.ns.addInterface(config, device), nil
}

func (ni *stackImpl) DelEthernetInterface(_ fidl.Context, id uint64) (stack.StackDelEthernetInterfaceResult, error) {
	return ni.ns.delInterface(id), nil
}

func (ni *stackImpl) ListInterfaces(fidl.Context) ([]stack.InterfaceInfo, error) {
	return ni.ns.getNetInterfaces(), nil
}

func (ni *stackImpl) GetInterfaceInfo(_ fidl.Context, id uint64) (stack.StackGetInterfaceInfoResult, error) {
	return ni.ns.getInterface(id), nil
}

func (ni *stackImpl) EnableInterface(_ fidl.Context, id uint64) (stack.StackEnableInterfaceResult, error) {
	return ni.ns.enableInterface(id), nil
}

func (ni *stackImpl) DisableInterface(_ fidl.Context, id uint64) (stack.StackDisableInterfaceResult, error) {
	return ni.ns.disableInterface(id), nil
}

func (ni *stackImpl) AddInterfaceAddress(_ fidl.Context, id uint64, addr stack.InterfaceAddress) (stack.StackAddInterfaceAddressResult, error) {
	return ni.ns.addInterfaceAddr(id, addr), nil
}

func (ni *stackImpl) DelInterfaceAddress(_ fidl.Context, id uint64, addr stack.InterfaceAddress) (stack.StackDelInterfaceAddressResult, error) {
	return ni.ns.delInterfaceAddr(id, addr), nil
}

func (ni *stackImpl) GetForwardingTable(fidl.Context) ([]stack.ForwardingEntry, error) {
	return ni.ns.getForwardingTable(), nil
}

func (ni *stackImpl) AddForwardingEntry(_ fidl.Context, entry stack.ForwardingEntry) (stack.StackAddForwardingEntryResult, error) {
	return ni.ns.addForwardingEntry(entry), nil
}

func (ni *stackImpl) DelForwardingEntry(_ fidl.Context, subnet net.Subnet) (stack.StackDelForwardingEntryResult, error) {
	return ni.ns.delForwardingEntry(subnet), nil
}

func (ni *stackImpl) EnablePacketFilter(_ fidl.Context, id uint64) (stack.StackEnablePacketFilterResult, error) {
	nicInfo, ok := ni.ns.stack.NICInfo()[tcpip.NICID(id)]

	var result stack.StackEnablePacketFilterResult
	if !ok {
		result.SetErr(stack.ErrorNotFound)
		return result, nil
	}

	ifs := nicInfo.Context.(*ifState)
	if ifs.filterEndpoint == nil {
		result.SetErr(stack.ErrorNotSupported)
	} else {
		ifs.filterEndpoint.Enable()
		result.SetResponse(stack.StackEnablePacketFilterResponse{})
	}
	return result, nil
}

func (ni *stackImpl) DisablePacketFilter(_ fidl.Context, id uint64) (stack.StackDisablePacketFilterResult, error) {
	nicInfo, ok := ni.ns.stack.NICInfo()[tcpip.NICID(id)]

	var result stack.StackDisablePacketFilterResult
	if !ok {
		result.SetErr(stack.ErrorNotFound)
		return result, nil
	}

	ifs := nicInfo.Context.(*ifState)
	if ifs.filterEndpoint == nil {
		result.SetErr(stack.ErrorNotSupported)
	} else {
		ifs.filterEndpoint.Disable()
		result.SetResponse(stack.StackDisablePacketFilterResponse{})
	}
	return result, nil
}

func (ni *stackImpl) EnableIpForwarding(fidl.Context) error {
	ni.ns.stack.SetForwarding(true)
	return nil
}

func (ni *stackImpl) DisableIpForwarding(fidl.Context) error {
	ni.ns.stack.SetForwarding(false)
	return nil
}

func (ni *stackImpl) GetDnsServerWatcher(ctx_ fidl.Context, watcher name.DnsServerWatcherWithCtxInterfaceRequest) error {
	return ni.dnsWatchers.Bind(watcher)
}

type logImpl struct {
	logger *syslog.Logger
}

func (li *logImpl) SetLogLevel(_ fidl.Context, level logger.LogLevelFilter) (stack.LogSetLogLevelResult, error) {
	li.logger.SetSeverity(syslog.LogLevel(level))
	syslog.VLogTf(syslog.DebugVerbosity, "fuchsia_net_stack", "SetSyslogLevel: %s", level)
	return stack.LogSetLogLevelResultWithResponse(stack.LogSetLogLevelResponse{}), nil
}
