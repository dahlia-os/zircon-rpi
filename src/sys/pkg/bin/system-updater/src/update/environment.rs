// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{metrics, paver},
    anyhow::{Context, Error},
    async_trait::async_trait,
    fidl_fuchsia_hardware_power_statecontrol::{
        AdminMarker as PowerStateControlMarker, AdminProxy as PowerStateControlProxy,
    },
    fidl_fuchsia_paver::{BootManagerProxy, DataSinkProxy},
    fidl_fuchsia_pkg::{PackageCacheProxy, PackageResolverProxy},
    fidl_fuchsia_space::ManagerProxy as SpaceManagerProxy,
    fuchsia_component::client::connect_to_service,
    futures::{future::BoxFuture, prelude::*},
};

/// A trait to provide the ability to create a metrics client.
pub trait CobaltConnector {
    /// Create a new metrics client and return a future that completes when all events are flushed
    /// to the service.
    fn connect(&self) -> (metrics::Client, BoxFuture<'static, ()>);
}

/// A trait to provide access to /config/build-info.
#[async_trait]
pub trait BuildInfo {
    /// Read the current board name, returning None if the file does not exist.
    async fn board(&self) -> Result<Option<String>, Error>;
}

/// The collection of external data files and services an update attempt will utilize to perform
/// the update.
pub struct Environment<B = NamespaceBuildInfo, C = NamespaceCobaltConnector> {
    pub(super) data_sink: DataSinkProxy,
    pub(super) boot_manager: BootManagerProxy,
    pub(super) pkg_resolver: PackageResolverProxy,
    pub(super) pkg_cache: PackageCacheProxy,
    pub(super) space_manager: SpaceManagerProxy,
    pub(super) power_state_control: PowerStateControlProxy,
    pub(super) build_info: B,
    pub(super) cobalt_connector: C,
}

impl Environment {
    pub fn connect_in_namespace() -> Result<Self, Error> {
        let (data_sink, boot_manager) = paver::connect_in_namespace()?;
        Ok(Self {
            data_sink,
            boot_manager,
            pkg_resolver: connect_to_service::<fidl_fuchsia_pkg::PackageResolverMarker>()
                .context("connect to fuchsia.pkg.PackageResolver")?,
            pkg_cache: connect_to_service::<fidl_fuchsia_pkg::PackageCacheMarker>()
                .context("connect to fuchsia.pkg.PackageCache")?,
            space_manager: connect_to_service::<fidl_fuchsia_space::ManagerMarker>()
                .context("connect to fuchsia.space.Manager")?,
            power_state_control: connect_to_service::<PowerStateControlMarker>()
                .context("connect to fuchsia.hardware.power.statecontrol.Admin")?,
            build_info: NamespaceBuildInfo,
            cobalt_connector: NamespaceCobaltConnector,
        })
    }
}

#[derive(Debug)]
pub struct NamespaceCobaltConnector;

impl CobaltConnector for NamespaceCobaltConnector {
    fn connect(&self) -> (metrics::Client, BoxFuture<'static, ()>) {
        let (cobalt, forwarder_task) = metrics::connect_to_cobalt();
        (cobalt, forwarder_task.boxed())
    }
}

#[derive(Debug)]
pub struct NamespaceBuildInfo;

#[async_trait]
impl BuildInfo for NamespaceBuildInfo {
    async fn board(&self) -> Result<Option<String>, Error> {
        let build_info = io_util::directory::open_in_namespace(
            "/config/build-info",
            io_util::OPEN_RIGHT_READABLE,
        )
        .context("while opening /config/build-info")?;

        let file =
            match io_util::directory::open_file(&build_info, "board", io_util::OPEN_RIGHT_READABLE)
                .await
            {
                Ok(file) => file,
                Err(io_util::node::OpenError::OpenError(fuchsia_zircon::Status::NOT_FOUND)) => {
                    return Ok(None)
                }
                Err(e) => return Err(e).context("while opening /config/build-info/board"),
            };

        let contents = io_util::file::read_to_string(&file)
            .await
            .context("while reading /config/build-info/board")?;
        Ok(Some(contents))
    }
}
