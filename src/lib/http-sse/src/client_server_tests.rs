// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    super::*,
    fuchsia_async::{self as fasync, net::TcpListener},
    fuchsia_hyper::new_https_client,
    futures::{
        future::{join, TryFutureExt},
        stream::{StreamExt, TryStreamExt},
    },
    hyper::{
        server::{accept::from_stream, Server},
        service::{make_service_fn, service_fn},
    },
    matches::assert_matches,
    std::{
        convert::Infallible,
        net::{Ipv4Addr, SocketAddr},
        sync::Arc,
    },
};

fn spawn_server(buffer_size: usize) -> (String, EventSender) {
    let (connections, url) = {
        let listener = TcpListener::bind(&SocketAddr::new(Ipv4Addr::LOCALHOST.into(), 0)).unwrap();
        let local_addr = listener.local_addr().unwrap();
        (
            listener
                .accept_stream()
                .map_ok(|(conn, _addr)| fuchsia_hyper::TcpStream { stream: conn }),
            format!("http://{}", local_addr),
        )
    };
    let (sse_response_creator, event_sender) =
        SseResponseCreator::with_additional_buffer_size(buffer_size);
    let sse_response_creator = Arc::new(sse_response_creator);
    let server = Server::builder(from_stream(connections))
        .executor(fuchsia_hyper::Executor)
        .serve(make_service_fn(move |_socket: &fuchsia_hyper::TcpStream| {
            let sse_response_creator = Arc::clone(&sse_response_creator);
            async move {
                Ok::<_, Infallible>(service_fn(move |_req| {
                    let sse_response_creator = Arc::clone(&sse_response_creator);
                    async move { Ok::<_, Infallible>(sse_response_creator.create().await) }
                }))
            }
        }))
        .unwrap_or_else(|e| panic!("mock sse server failed: {:?}", e));
    fasync::spawn(server);
    (url, event_sender)
}

#[fasync::run_singlethreaded(test)]
async fn single_client_single_event() {
    let (url, event_sender) = spawn_server(1);
    let mut client = Client::connect(new_https_client(), &url).await.unwrap();
    let event = Event::from_type_and_data("event_type", "event_data").unwrap();

    let (_, recv) = join(event_sender.send(&event), client.next()).await;

    assert_matches!(recv, Some(Ok(e)) if e == event);
}

#[fasync::run_singlethreaded(test)]
async fn multiple_clients_multiple_events() {
    let (url, event_sender) = spawn_server(2);
    let client0 = Client::connect(new_https_client(), &url).await.unwrap();
    let client1 = Client::connect(new_https_client(), &url).await.unwrap();
    let events = vec![
        Event::from_type_and_data("event_type0", "event_data0").unwrap(),
        Event::from_type_and_data("event_type1", "event_data1").unwrap(),
    ];

    for event in events.iter() {
        event_sender.send(event).await;
    }
    let client0_events = client0.take(2).collect::<Vec<_>>();
    let client1_events = client1.take(2).collect::<Vec<_>>();
    let (client0_events, client1_events) = join(client0_events, client1_events).await;

    assert_eq!(client0_events.into_iter().map(|r| r.unwrap()).collect::<Vec<_>>(), events);
    assert_eq!(client1_events.into_iter().map(|r| r.unwrap()).collect::<Vec<_>>(), events);
}
