// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    component_manager_lib::{
        builtin_environment::BuiltinEnvironmentBuilder,
        elf_runner::ElfRunner,
        model::{
            binding::Binder, moniker::AbsoluteMoniker, realm::BindReason, testing::test_helpers,
        },
        startup,
    },
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_io::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fidl_fuchsia_test_manager::HarnessMarker,
    fuchsia_component::server::{ServiceFs, ServiceObj},
    fuchsia_syslog as syslog,
    futures::prelude::*,
    log::*,
    std::{path::PathBuf, process, sync::Arc},
};

/// Returns a usage message for the supported arguments.
pub fn usage() -> String {
    format!(
        "Usage: {} ",
        std::env::args().next().unwrap_or("component_manager_for_test".to_string())
    )
}

/// This is a prototype used with ftf to launch v2 tests.
/// This is a temporary workaround till we get around using actual component manager.
/// We will start test as root component and using hub v2 attach its "expose/svc" directory to this
/// managers out/svc(v1 version) directory.
#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["component_manager_for_test"])?;
    let args = std::env::args();
    // replace first argument(binary name) with test manager envelope so that it
    // can be launched as root component.
    let args =
        vec!["fuchsia-pkg://fuchsia.com/component_manager_for_test#meta/test_manager_envelope.cm"
            .to_owned()]
        .into_iter()
        .chain(args.skip(1));

    let args = match startup::Arguments::new(args) {
        Ok(args) => args,
        Err(err) => {
            error!("{}\n{}", err, usage());
            return Err(err);
        }
    };
    info!("Component manager for test is starting up...");

    // Create an ELF runner for the root component.
    let runner = Arc::new(ElfRunner::new(&args));

    // Set up environment.
    let builtin_environment = BuiltinEnvironmentBuilder::new()
        .set_args(args)
        .add_runner("elf".into(), runner)
        .add_available_resolvers_from_namespace()?
        .build()
        .await?;
    let hub_proxy = builtin_environment.bind_service_fs_for_hub().await?;

    let root_moniker = AbsoluteMoniker::root();
    match builtin_environment.model.bind(&root_moniker, &BindReason::Root).await {
        Ok(_) => {
            // TODO: Exit the component manager when the root component's binding is lost
            // (when it terminates).
        }
        Err(error) => {
            error!("Failed to bind to root component: {:?}", error);
            process::exit(1)
        }
    }

    // make sure root component exposes test suite protocol.
    let expose_dir_proxy = io_util::open_directory(
        &hub_proxy,
        &PathBuf::from("exec/expose/svc"),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
    )
    .expect("Failed to open directory");

    assert_eq!(
        vec![HarnessMarker::DEBUG_NAME],
        test_helpers::list_directory(&expose_dir_proxy).await
    );

    // bind expose/svc to out/svc of this v1 component.
    let mut fs = ServiceFs::<ServiceObj<'_, ()>>::new();
    fs.add_remote("svc", expose_dir_proxy);

    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}
