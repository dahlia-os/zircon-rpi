// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This program hosts the `Trigger` service, which starts the component and hangs.
use {
    fidl_fidl_test_components as ftest, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryStreamExt},
};

fn main() {
    let mut executor = fasync::Executor::new().expect("error creating executor");
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn_local(async move {
            run_trigger_service(stream).await;
        });
    });
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing directory");
    executor.run_singlethreaded(fs.collect::<()>());
    loop {}
}

async fn run_trigger_service(mut stream: ftest::TriggerRequestStream) {
    while let Some(event) = stream.try_next().await.expect("failed to serve trigger service") {
        let ftest::TriggerRequest::Run { responder } = event;
        responder.send("").expect("failed to send trigger response");
    }
}
