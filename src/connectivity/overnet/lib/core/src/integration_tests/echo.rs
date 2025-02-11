// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Echo integration test for Fuchsia (much like the echo example - indeed the initial code came
//! from there, but self contained and more adaptable to different scenarios)

#![cfg(test)]

use {
    super::Overnet,
    crate::router::test_util::{run, run_repeatedly},
    anyhow::{Context as _, Error},
    fidl::endpoints::{ClientEnd, RequestStream, ServiceMarker},
    fidl_fidl_examples_echo as echo,
    fidl_fuchsia_overnet::{
        ServiceConsumerProxyInterface, ServiceProviderRequest, ServiceProviderRequestStream,
        ServicePublisherProxyInterface,
    },
    fuchsia_async::Task,
    futures::prelude::*,
    std::sync::Arc,
};

////////////////////////////////////////////////////////////////////////////////
// Test scenarios

#[test]
fn simple() -> Result<(), Error> {
    run(async move {
        let client = Overnet::new(88801.into())?;
        let server = Overnet::new(88802.into())?;
        super::connect(&client, &server)?;
        run_echo_test(client, server, Some("HELLO INTEGRATION TEST WORLD")).await
    })
}

#[test]
fn kilobyte() -> Result<(), Error> {
    run(async move {
        let client = Overnet::new(88811.into())?;
        let server = Overnet::new(88812.into())?;
        super::connect(&client, &server)?;
        run_echo_test(client, server, Some(&std::iter::repeat('a').take(1024).collect::<String>()))
            .await
    })
}

#[test]
fn quite_large() -> Result<(), Error> {
    run(async move {
        let client = Overnet::new(88821.into())?;
        let server = Overnet::new(88822.into())?;
        super::connect(&client, &server)?;
        run_echo_test(client, server, Some(&std::iter::repeat('a').take(60000).collect::<String>()))
            .await
    })
}

#[test]
fn quic() -> Result<(), Error> {
    run(async move {
        let client = Overnet::new(88831.into())?;
        let server = Overnet::new(88832.into())?;
        super::connect_with_quic(&client, &server)?;
        run_echo_test(client, server, Some("HELLO INTEGRATION TEST WORLD")).await
    })
}

#[test]
fn interspersed_log_messages() -> Result<(), Error> {
    run_repeatedly(1, |i| async move {
        let client = Overnet::new((i * 100000 + 88841).into())?;
        let server = Overnet::new((i * 100000 + 88842).into())?;
        let _t = Task::spawn({
            let client = client.clone();
            let server = server.clone();
            async move {
                if let Err(e) = super::connect_with_interspersed_log_messages(client, server).await
                {
                    panic!("interspersed_log_messages connection failed: {:?}", e);
                }
            }
        });
        run_echo_test(client, server, Some("HELLO INTEGRATION TEST WORLD")).map_ok(drop).await
    })
}

////////////////////////////////////////////////////////////////////////////////
// Client implementation

async fn exec_client(overnet: Arc<Overnet>, text: Option<&str>) -> Result<(), Error> {
    let svc = overnet.connect_as_service_consumer()?;
    loop {
        let peers = svc.list_peers().await?;
        log::info!("{:?} Got peers: {:?}", overnet.node_id(), peers);
        for mut peer in peers {
            if peer.description.services.is_none() {
                continue;
            }
            if peer
                .description
                .services
                .unwrap()
                .iter()
                .find(|name| *name == echo::EchoMarker::NAME)
                .is_none()
            {
                continue;
            }
            let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
            svc.connect_to_service(&mut peer.id, echo::EchoMarker::NAME, s).unwrap();
            let proxy =
                fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
            let cli = echo::EchoProxy::new(proxy);
            log::info!("{:?} Sending {:?} to {:?}", overnet.node_id(), text, peer.id);
            assert_eq!(cli.echo_string(text).await.unwrap(), text.map(|s| s.to_string()));
            return Ok(());
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Server implementation

async fn exec_server(overnet: Arc<Overnet>) -> Result<(), Error> {
    let node_id = overnet.node_id();
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    let chan = fidl::AsyncChannel::from_channel(s).context("failed to make async channel")?;
    overnet
        .connect_as_service_publisher()?
        .publish_service(echo::EchoMarker::NAME, ClientEnd::new(p))?;
    ServiceProviderRequestStream::from_channel(chan)
        .map_err(Into::<Error>::into)
        .try_for_each_concurrent(None, |req| async move {
            let ServiceProviderRequest::ConnectToService {
                chan,
                info: _,
                control_handle: _control_handle,
            } = req;
            log::info!("{:?} Received service request for service", node_id);
            let chan =
                fidl::AsyncChannel::from_channel(chan).context("failed to make async channel")?;
            let mut stream = echo::EchoRequestStream::from_channel(chan);
            while let Some(echo::EchoRequest::EchoString { value, responder }) =
                stream.try_next().await.context("error running echo server")?
            {
                log::info!("{:?} Received echo request for string {:?}", node_id, value);
                responder.send(value.as_ref().map(|s| &**s)).context("error sending response")?;
                log::info!("{:?} echo response sent successfully", node_id);
            }
            Ok(())
        })
        .await?;
    drop(overnet);
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// Test driver

async fn run_echo_test(
    client: Arc<Overnet>,
    server: Arc<Overnet>,
    text: Option<&str>,
) -> Result<(), Error> {
    let server = Task::spawn(async move {
        let server_id = server.node_id();
        exec_server(server).await.unwrap();
        log::info!("{:?} SERVER DONE", server_id)
    });
    let r = exec_client(client, text).await;
    drop(server);
    r
}
