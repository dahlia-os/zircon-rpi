// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::Debug;
use std::mem::size_of;

use fidl_fuchsia_net as net;
use fidl_fuchsia_netstack as netstack;
use fidl_fuchsia_sys as sys;
use fuchsia_async::{self as fasync, DurationExt as _, TimeoutExt as _};
use fuchsia_component::client::AppBuilder;
use fuchsia_zircon as zx;

use anyhow::{self, Context};
use futures::future::{self, Future, FutureExt as _};
use futures::stream::TryStreamExt as _;
use net_types::ethernet::Mac;
use net_types::ip::{self as net_types_ip, Ip};
use net_types::{SpecifiedAddress, Witness};
use netstack_testing_macros::variants_test;
use packet::serialize::{InnerPacketBuilder, Serializer};
use packet_formats::ethernet::{EtherType, EthernetFrameBuilder};
use packet_formats::icmp::ndp::{
    self,
    options::{NdpOption, PrefixInformation},
    NeighborAdvertisement, NeighborSolicitation, RouterAdvertisement, RouterSolicitation,
};
use packet_formats::icmp::{IcmpMessage, IcmpPacketBuilder, IcmpUnusedCode};
use packet_formats::ip::IpProto;
use packet_formats::ipv6::Ipv6PacketBuilder;
use packet_formats::testutil::parse_icmp_packet_in_ip_packet_in_ethernet_frame;
use zerocopy::ByteSlice;

use crate::constants::{eth as eth_consts, ipv6 as ipv6_consts};
use crate::environments::*;
use crate::*;

/// As per [RFC 4861] sections 4.1-4.5, NDP packets MUST have a hop limit of 255.
///
/// [RFC 4861]: https://tools.ietf.org/html/rfc4861
const NDP_MESSAGE_TTL: u8 = 255;

/// The expected number of Router Solicitations sent by the netstack when an
/// interface is brought up as a host.
const EXPECTED_ROUTER_SOLICIATIONS: u8 = 3;

/// The expected interval between sending Router Solicitation messages when
/// soliciting IPv6 routers.
const EXPECTED_ROUTER_SOLICITATION_INTERVAL: zx::Duration = zx::Duration::from_seconds(4);

/// The expected number of Neighbor Solicitations sent by the netstack when
/// performing Duplicate Address Detection.
const EXPECTED_DUP_ADDR_DETECT_TRANSMITS: u8 = 1;

/// The expected interval between sending Neighbor Solicitation messages when
/// performing Duplicate Address Detection.
const EXPECTED_DAD_RETRANSMIT_TIMER: zx::Duration = zx::Duration::from_seconds(1);

/// Sets up an environment with a network used for tests requiring manual packet
/// inspection and transmission.
///
/// Returns the network, environment, netstack client, interface (added to the
/// netstack) and a fake endpoint used to read and write raw ethernet packets.
/// The interface will be up when `setup_network` returns successfully.
async fn setup_network<E, S>(
    sandbox: &TestSandbox,
    name: S,
) -> Result<(
    TestNetwork<'_>,
    TestEnvironment<'_>,
    netstack::NetstackProxy,
    TestInterface<'_>,
    TestFakeEndpoint<'_>,
)>
where
    E: Endpoint,
    S: Copy + Into<String> + EthertapName,
{
    let network = sandbox.create_network(name).await.context("failed to create network")?;
    let environment = sandbox
        .create_netstack_environment::<Netstack2, _>(name)
        .context("failed to create netstack environment")?;
    let iface = environment
        .join_network::<E, _>(&network, name.ethertap_compatible_name(), InterfaceConfig::None)
        .await
        .context("failed to configure networking")?;
    let fake_ep = network.create_fake_endpoint()?;

    // Wait for the interface to be up.
    let netstack = environment
        .connect_to_service::<netstack::NetstackMarker>()
        .context("failed to connect to netstack service")?;
    let () = wait_for_interface_up(
        netstack.take_event_stream(),
        iface.id(),
        ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT,
    )
    .await?;

    return Ok((network, environment, netstack, iface, fake_ep));
}

/// Writes an NDP message to the provided fake endpoint.
///
/// Given the source and destination MAC and IP addresses, NDP message and
/// options, the full NDP packet (including IPv6 and Ethernet headers) will be
/// transmitted to the fake endpoint's network.
pub(super) async fn write_ndp_message<
    B: ByteSlice + Debug,
    M: IcmpMessage<net_types_ip::Ipv6, B, Code = IcmpUnusedCode> + Debug,
>(
    src_mac: Mac,
    dst_mac: Mac,
    src_ip: net_types_ip::Ipv6Addr,
    dst_ip: net_types_ip::Ipv6Addr,
    message: M,
    options: &[NdpOption<'_>],
    ep: &TestFakeEndpoint<'_>,
) -> Result {
    let ser = ndp::OptionsSerializer::<_>::new(options.iter())
        .into_serializer()
        .encapsulate(IcmpPacketBuilder::<_, B, _>::new(src_ip, dst_ip, IcmpUnusedCode, message))
        .encapsulate(Ipv6PacketBuilder::new(src_ip, dst_ip, NDP_MESSAGE_TTL, IpProto::Icmpv6))
        .encapsulate(EthernetFrameBuilder::new(src_mac, dst_mac, EtherType::Ipv6))
        .serialize_vec_outer()
        .map_err(|e| anyhow::anyhow!("failed to serialize NDP packet: {:?}", e))?
        .unwrap_b();
    let () = ep.write(ser.as_ref()).await.context("failed to write to fake endpoint")?;
    Ok(())
}

/// Launches a new netstack with the endpoint and returns the IPv6 addresses
/// initially assigned to it.
///
/// If `run_netstack_and_get_ipv6_addrs_for_endpoint` returns successfully, it
/// is guaranteed that the launched netstack has been terminated. Note, if
/// `run_netstack_and_get_ipv6_addrs_for_endpoint` does not return successfully,
/// the launched netstack will still be terminated, but no guarantees are made
/// about when that will happen.
async fn run_netstack_and_get_ipv6_addrs_for_endpoint<N: Netstack>(
    endpoint: &TestEndpoint<'_>,
    launcher: &sys::LauncherProxy,
    name: String,
) -> Result<Vec<net::Subnet>> {
    // Launch the netstack service.

    let mut app = AppBuilder::new(N::VERSION.get_url())
        .spawn(launcher)
        .context("failed to spawn netstack")?;
    let netstack = app
        .connect_to_service::<netstack::NetstackMarker>()
        .context("failed to connect to netstack service")?;

    // Add the device and get its interface state from netstack.
    // TODO(48907) Support Network Device. This helper fn should use stack.fidl
    // and be agnostic over interface type.
    let id = netstack
        .add_ethernet_device(
            &name,
            &mut netstack::InterfaceConfig {
                name: name[..fidl_fuchsia_posix_socket::INTERFACE_NAME_LENGTH.into()].to_string(),
                filepath: "/fake/filepath/for_test".to_string(),
                metric: 0,
                ip_address_config: netstack::IpAddressConfig::Dhcp(true),
            },
            endpoint
                .get_ethernet()
                .await
                .context("add_ethernet_device requires an Ethernet endpoint")?,
        )
        .await
        .context("failed to add ethernet device")?;
    let interface = netstack
        .get_interfaces2()
        .await
        .context("failed to get interfaces")?
        .into_iter()
        .find(|interface| interface.id == id)
        .ok_or(anyhow::anyhow!("failed to find added ethernet device"))?;

    // Kill the netstack.
    //
    // Note, simply dropping `component_controller` would also kill the netstack
    // but we explicitly kill it and wait for the terminated event before
    // proceeding.
    let () = app.kill().context("failed to kill app")?;
    let _exit_status = app.wait().await.context("failed to observe netstack termination")?;

    Ok(interface.ipv6addrs)
}

/// Test that across netstack runs, a device will initially be assigned the same
/// IPv6 addresses.
#[variants_test]
async fn consistent_initial_ipv6_addrs<E: Endpoint>(name: &str) -> Result {
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let env = sandbox
        .create_environment(name, &[KnownServices::SecureStash])
        .context("failed to create environment")?;
    let launcher = env.get_launcher().context("failed to get launcher")?;
    let endpoint = sandbox
        .create_endpoint::<Ethernet, _>(name.ethertap_compatible_name())
        .await
        .context("failed to create endpoint")?;

    // Make sure netstack uses the same addresses across runs for a device.
    let first_run_addrs = run_netstack_and_get_ipv6_addrs_for_endpoint::<Netstack2>(
        &endpoint,
        &launcher,
        name.to_string(),
    )
    .await?;
    let second_run_addrs = run_netstack_and_get_ipv6_addrs_for_endpoint::<Netstack2>(
        &endpoint,
        &launcher,
        name.to_string(),
    )
    .await?;
    assert_eq!(first_run_addrs, second_run_addrs);

    Ok(())
}

/// Tests that `EXPECTED_ROUTER_SOLICIATIONS` Router Solicitation messages are transmitted
/// when the interface is brought up.
#[variants_test]
async fn sends_router_solicitations<E: Endpoint>(name: &str) -> Result {
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let (_network, _environment, _netstack, _iface, fake_ep) =
        setup_network::<E, _>(&sandbox, name).await?;

    // Make sure exactly `EXPECTED_ROUTER_SOLICIATIONS` RS messages are transmited
    // by the netstack.
    let mut observed_rs = 0;
    loop {
        // When we have already observed the expected number of RS messages, do a
        // negative check to make sure that we don't send anymore.
        let extra_timeout = if observed_rs == EXPECTED_ROUTER_SOLICIATIONS {
            ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT
        } else {
            ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT
        };

        let ret = fake_ep
            .frame_stream()
            .try_filter_map(|(data, dropped)| {
                assert_eq!(dropped, 0);
                let mut observed_slls = Vec::new();
                future::ok(
                    parse_icmp_packet_in_ip_packet_in_ethernet_frame::<
                        net_types_ip::Ipv6,
                        _,
                        RouterSolicitation,
                        _,
                    >(&data, |p| {
                        for option in p.body().iter() {
                            if let NdpOption::SourceLinkLayerAddress(a) = option {
                                let mut mac_bytes = [0; 6];
                                mac_bytes.copy_from_slice(&a[..size_of::<Mac>()]);
                                observed_slls.push(Mac::new(mac_bytes));
                            } else {
                                // We should only ever have an NDP Source Link-Layer Address
                                // option in a RS.
                                panic!("unexpected option in RS = {:?}", option);
                            }
                        }
                    })
                    .map_or(
                        None,
                        |(_src_mac, dst_mac, src_ip, dst_ip, ttl, _message, _code)| {
                            Some((dst_mac, src_ip, dst_ip, ttl, observed_slls))
                        },
                    ),
                )
            })
            .try_next()
            .map(|r| r.context("error getting OnData event"))
            .on_timeout((EXPECTED_ROUTER_SOLICITATION_INTERVAL + extra_timeout).after_now(), || {
                // If we already observed `EXPECTED_ROUTER_SOLICIATIONS` RS, then we shouldn't
                // have gotten any more; the timeout is expected.
                if observed_rs == EXPECTED_ROUTER_SOLICIATIONS {
                    return Ok(None);
                }

                return Err(anyhow::anyhow!("timed out waiting for the {}-th RS", observed_rs));
            })
            .await?;

        let (dst_mac, src_ip, dst_ip, ttl, observed_slls) = match ret {
            Some((dst_mac, src_ip, dst_ip, ttl, observed_slls)) => {
                (dst_mac, src_ip, dst_ip, ttl, observed_slls)
            }
            None => break,
        };

        assert_eq!(
            dst_mac,
            Mac::from(&net_types_ip::Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS)
        );

        // DAD should have resolved for the link local IPv6 address that is assigned to
        // the interface when it is first brought up. When a link local address is
        // assigned to the interface, it should be used for transmitted RS messages.
        if observed_rs > 0 {
            assert!(src_ip.is_specified())
        }

        assert_eq!(dst_ip, net_types_ip::Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS.get());

        assert_eq!(ttl, NDP_MESSAGE_TTL);

        // The Router Solicitation should only ever have at max 1 source
        // link-layer option.
        assert!(observed_slls.len() <= 1);
        let observed_sll = observed_slls.into_iter().nth(0);
        if src_ip.is_specified() {
            if observed_sll.is_none() {
                panic!("expected source-link-layer address option if RS has a specified source IP address");
            }
        } else if observed_sll.is_some() {
            panic!("unexpected source-link-layer address option for RS with unspecified source IP address");
        }

        observed_rs += 1;
    }

    assert_eq!(observed_rs, EXPECTED_ROUTER_SOLICIATIONS);

    Ok(())
}

/// Tests that both stable and temporary SLAAC addresses are generated for a SLAAC prefix.
#[variants_test]
async fn slaac_with_privacy_extensions<E: Endpoint>(name: &str) -> Result {
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let (_network, _environment, netstack, iface, fake_ep) =
        setup_network::<E, _>(&sandbox, name).await?;

    // Wait for a Router Solicitation.
    //
    // The first RS should be sent immediately.
    let () = fake_ep
        .frame_stream()
        .try_filter_map(|(data, dropped)| {
            assert_eq!(dropped, 0);
            future::ok(
                parse_icmp_packet_in_ip_packet_in_ethernet_frame::<
                    net_types_ip::Ipv6,
                    _,
                    RouterSolicitation,
                    _,
                >(&data, |_| {})
                .map_or(None, |_| Some(())),
            )
        })
        .try_next()
        .map(|r| r.context("error getting OnData event"))
        .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
            Err(anyhow::anyhow!("timed out waiting for RS packet"))
        })
        .await?
        .ok_or(anyhow::anyhow!("failed to get next OnData event"))?;

    // Send a Router Advertisement with information for a SLAAC prefix.
    let ra = RouterAdvertisement::new(
        0,     /* current_hop_limit */
        false, /* managed_flag */
        false, /* other_config_flag */
        0,     /* router_lifetime */
        0,     /* reachable_time */
        0,     /* retransmit_timer */
    );
    let pi = PrefixInformation::new(
        ipv6_consts::PREFIX.prefix(),  /* prefix_length */
        false,                         /* on_link_flag */
        true,                          /* autonomous_address_configuration_flag */
        99999,                         /* valid_lifetime */
        99999,                         /* preferred_lifetime */
        ipv6_consts::PREFIX.network(), /* prefix */
    );
    let options = [NdpOption::PrefixInformation(&pi)];
    let () = write_ndp_message::<&[u8], _>(
        eth_consts::MAC_ADDR,
        Mac::from(&net_types_ip::Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS),
        ipv6_consts::LINK_LOCAL_ADDR,
        net_types_ip::Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
        ra,
        &options,
        &fake_ep,
    )
    .await
    .context("failed to write NDP message")?;

    // Wait for the SLAAC addresses to be generated.
    //
    // We expect two addresses for the SLAAC prefixes to be assigned to the NIC as the
    // netstack should generate both a stable and temporary SLAAC address.
    let expected_addrs = 2;
    netstack
        .take_event_stream()
        .try_filter_map(|netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
            if let Some(netstack::NetInterface { ipv6addrs, .. }) =
                interfaces.iter().find(|i| u64::from(i.id) == iface.id())
            {
                let mut slaac_addrs = 0;

                for ip in ipv6addrs {
                    if let net::IpAddress::Ipv6(a) = ip.addr {
                        if ipv6_consts::PREFIX.contains(&net_types_ip::Ipv6Addr::new(a.addr)) {
                            slaac_addrs += 1;
                        }
                    }
                }

                if slaac_addrs == expected_addrs {
                    return future::ok(Some(()));
                }
            }

            future::ok(None)
        })
        .try_next()
        .map(|r| r.context("error getting OnInterfaceChanged event"))
        .on_timeout(
            (EXPECTED_DAD_RETRANSMIT_TIMER * EXPECTED_DUP_ADDR_DETECT_TRANSMITS * expected_addrs
                + ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT)
                .after_now(),
            || Err(anyhow::anyhow!("timed out waiting for SLAAC addresses")),
        )
        .await?
        .ok_or(anyhow::anyhow!("failed to get next OnInterfaceChanged event"))
}

/// Adds `ipv6_consts::LINK_LOCAL_ADDR` to the interface and makes sure a Neighbor Solicitation
/// message is transmitted by the netstack for DAD.
///
/// Calls `fail_dad_fn` after the DAD message is observed so callers can simulate a remote
/// node that has some interest in the same address.
async fn add_address_for_dad<
    'a,
    'b: 'a,
    R: 'b + Future<Output = Result>,
    FN: FnOnce(&'b TestInterface<'a>, &'b TestFakeEndpoint<'a>) -> R,
>(
    iface: &'b TestInterface<'a>,
    fake_ep: &'b TestFakeEndpoint<'a>,
    fail_dad_fn: FN,
) -> Result {
    let () = iface
        .add_ip_addr(net::Subnet {
            addr: net::IpAddress::Ipv6(net::Ipv6Address {
                addr: ipv6_consts::LINK_LOCAL_ADDR.ipv6_bytes(),
            }),
            prefix_len: 64,
        })
        .await?;

    // The first DAD message should be sent immediately.
    let ret = fake_ep
        .frame_stream()
        .try_filter_map(|(data, dropped)| {
            assert_eq!(dropped, 0);
            future::ok(
                parse_icmp_packet_in_ip_packet_in_ethernet_frame::<
                    net_types_ip::Ipv6,
                    _,
                    NeighborSolicitation,
                    _,
                >(&data, |p| assert_eq!(p.body().iter().count(), 0))
                .map_or(None, |(_src_mac, dst_mac, src_ip, dst_ip, ttl, message, _code)| {
                    // If the NS is not for the address we just added, this is for some
                    // other address. We ignore it as it is not relevant to our test.
                    if message.target_address() != &ipv6_consts::LINK_LOCAL_ADDR {
                        return None;
                    }

                    Some((dst_mac, src_ip, dst_ip, ttl))
                }),
            )
        })
        .try_next()
        .map(|r| r.context("error getting OnData event"))
        .on_timeout(ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT.after_now(), || {
            Err(anyhow::anyhow!(
                "timed out waiting for a neighbor solicitation targetting {}",
                ipv6_consts::LINK_LOCAL_ADDR
            ))
        })
        .await?
        .ok_or(anyhow::anyhow!("failed to get next OnData event"))?;

    let (dst_mac, src_ip, dst_ip, ttl) = ret;
    let expected_dst = ipv6_consts::LINK_LOCAL_ADDR.to_solicited_node_address();
    assert_eq!(src_ip, net_types_ip::Ipv6::UNSPECIFIED_ADDRESS);
    assert_eq!(dst_ip, expected_dst.get());
    assert_eq!(dst_mac, Mac::from(&expected_dst));
    assert_eq!(ttl, NDP_MESSAGE_TTL);

    let () = fail_dad_fn(iface, fake_ep).await?;

    Ok(())
}

/// Tests that if the netstack attempts to assign an address to an interface, and a remote node
/// is already assigned the address or attempts to assign the address at the same time, DAD
/// fails on the local interface.
///
/// If no remote node has any interest in an address the netstack is attempting to assign to
/// an interface, DAD should succeed.
// TODO(53644): Reenable when we figure out how to handle timing issues in CQ when the address
// may resolve before the netstack processes the NA/NS messagee.
#[allow(unused)]
async fn duplicate_address_detection<E: Endpoint>(name: &str) -> Result {
    /// Makes sure that `ipv6_consts::LINK_LOCAL_ADDR` is not assigned to the interface after the
    /// DAD resolution time.
    async fn check_address_failed_dad(iface: &TestInterface<'_>) -> Result {
        let () = fasync::Timer::new(fasync::Time::after(
            EXPECTED_DAD_RETRANSMIT_TIMER * EXPECTED_DUP_ADDR_DETECT_TRANSMITS
                + ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT,
        ))
        .fuse()
        .await;

        let addr = net::Subnet {
            addr: net::IpAddress::Ipv6(net::Ipv6Address {
                addr: ipv6_consts::LINK_LOCAL_ADDR.ipv6_bytes(),
            }),
            prefix_len: 64,
        };
        assert!(!iface.get_addrs().await?.iter().any(|a| a == &addr));

        Ok(())
    }

    /// Transmits a Neighbor Solicitation message and expects `ipv6_consts::LINK_LOCAL_ADDR`
    /// to not be assigned to the interface after the normal resolution time for DAD.
    async fn fail_dad_with_ns(iface: &TestInterface<'_>, fake_ep: &TestFakeEndpoint<'_>) -> Result {
        let snmc = ipv6_consts::LINK_LOCAL_ADDR.to_solicited_node_address();
        let () = write_ndp_message::<&[u8], _>(
            eth_consts::MAC_ADDR,
            Mac::from(&snmc),
            net_types_ip::Ipv6::UNSPECIFIED_ADDRESS,
            snmc.get(),
            NeighborSolicitation::new(ipv6_consts::LINK_LOCAL_ADDR),
            &[],
            fake_ep,
        )
        .await
        .context("failed to write NDP message")?;

        check_address_failed_dad(iface).await
    }

    /// Transmits a Neighbor Advertisement message and expects `ipv6_consts::LINK_LOCAL_ADDR`
    /// to not be assigned to the interface after the normal resolution time for DAD.
    async fn fail_dad_with_na(iface: &TestInterface<'_>, fake_ep: &TestFakeEndpoint<'_>) -> Result {
        let () = write_ndp_message::<&[u8], _>(
            eth_consts::MAC_ADDR,
            Mac::from(&net_types_ip::Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS),
            ipv6_consts::LINK_LOCAL_ADDR,
            net_types_ip::Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
            NeighborAdvertisement::new(
                false, /* router_flag */
                false, /* solicited_flag */
                false, /* override_flag */
                ipv6_consts::LINK_LOCAL_ADDR,
            ),
            &[NdpOption::TargetLinkLayerAddress(&eth_consts::MAC_ADDR.bytes())],
            fake_ep,
        )
        .await
        .context("failed to write NDP message")?;

        check_address_failed_dad(iface).await
    }

    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let (_network, _environment, netstack, iface, fake_ep) =
        setup_network::<E, _>(&sandbox, name).await?;

    // Add an address and expect it to fail DAD because we simulate another node
    // performing DAD at the same time.
    let () = add_address_for_dad(&iface, &fake_ep, fail_dad_with_ns).await?;

    // Add an address and expect it to fail DAD because we simulate another node
    // already owning the address.
    let () = add_address_for_dad(&iface, &fake_ep, fail_dad_with_na).await?;

    // Add the address, and make sure it gets assigned.
    let () = add_address_for_dad(&iface, &fake_ep, |_, _| async { Ok(()) }).await?;
    netstack
        .take_event_stream()
        .try_filter_map(|netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
            if let Some(netstack::NetInterface { ipv6addrs, .. }) =
                interfaces.iter().find(|i| u64::from(i.id) == iface.id())
            {
                for ip in ipv6addrs {
                    if let net::IpAddress::Ipv6(a) = ip.addr {
                        if ipv6_consts::LINK_LOCAL_ADDR == net_types_ip::Ipv6Addr::new(a.addr) {
                            return future::ok(Some(()));
                        }
                    }
                }
            }

            future::ok(None)
        })
        .try_next()
        .map(|r| r.context("error getting OnInterfaceChanged event"))
        .on_timeout(
            (EXPECTED_DAD_RETRANSMIT_TIMER * EXPECTED_DUP_ADDR_DETECT_TRANSMITS
                + ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT)
                .after_now(),
            || {
                Err(anyhow::anyhow!(
                    "timed out waiting for the address {} to be assigned",
                    ipv6_consts::LINK_LOCAL_ADDR
                ))
            },
        )
        .await?
        .ok_or(anyhow::anyhow!("failed to get next OnInterfaceChanged event"))
}
