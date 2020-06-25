// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{Event, EventSource},
    fuchsia_hyper::HttpsClient,
    futures::{
        stream::Stream,
        task::{Context, Poll},
    },
    hyper::{Body, Request, StatusCode},
    std::pin::Pin,
    thiserror::Error,
};

/// An http SSE client.
#[derive(Debug)]
pub struct Client {
    source: EventSource,
    chunks: Body,
    events: std::vec::IntoIter<Event>,
}

impl Client {
    /// Connects to an http url and, on success, returns a `Stream` of SSE events.
    pub async fn connect(
        https_client: HttpsClient,
        url: impl AsRef<str>,
    ) -> Result<Self, ClientConnectError> {
        let request = Request::get(url.as_ref())
            .header("accept", "text/event-stream")
            .body(Body::empty())
            .map_err(|e| ClientConnectError::CreateRequest(e))?;
        let response =
            https_client.request(request).await.map_err(|e| ClientConnectError::MakeRequest(e))?;
        if response.status() != StatusCode::OK {
            return Err(ClientConnectError::HttpStatus(response.status()));
        }
        Ok(Self {
            source: EventSource::new(),
            chunks: response.into_body(),
            events: vec![].into_iter(),
        })
    }
}

#[derive(Debug, Error)]
pub enum ClientConnectError {
    #[error("error creating http request: {}", _0)]
    CreateRequest(hyper::http::Error),

    #[error("error making http request: {}", _0)]
    MakeRequest(hyper::error::Error),

    #[error("http server responded with status other than OK: {}", _0)]
    HttpStatus(hyper::StatusCode),
}

impl Stream for Client {
    type Item = Result<Event, ClientPollError>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        loop {
            if let Some(event) = self.events.next() {
                return Poll::Ready(Some(Ok(event)));
            }
            match Pin::new(&mut self.chunks).poll_next(cx) {
                Poll::Ready(Some(Ok(chunk))) => {
                    self.events = self.source.parse(&chunk).into_iter();
                }
                Poll::Ready(Some(Err(e))) => {
                    return Poll::Ready(Some(Err(ClientPollError::NextChunk(e))))
                }
                Poll::Ready(None) => {
                    return Poll::Ready(None);
                }
                Poll::Pending => {
                    return Poll::Pending;
                }
            }
        }
    }
}

#[derive(Debug, Error)]
pub enum ClientPollError {
    #[error("error downloading next chunk: {}", _0)]
    NextChunk(hyper::error::Error),
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_async::{self as fasync, net::TcpListener},
        fuchsia_hyper::new_https_client,
        futures::{
            future::{Future, TryFutureExt},
            stream::{StreamExt, TryStreamExt},
        },
        hyper::{
            server::{accept::from_stream, Server},
            service::{make_service_fn, service_fn},
            Response,
        },
        matches::assert_matches,
        std::{
            convert::Infallible,
            net::{Ipv4Addr, SocketAddr},
        },
    };

    fn spawn_server<F>(handle_req: fn(Request<Body>) -> F) -> String
    where
        F: Future<Output = Result<Response<Body>, hyper::Error>> + Send + 'static,
    {
        let (connections, url) = {
            let listener =
                TcpListener::bind(&SocketAddr::new(Ipv4Addr::LOCALHOST.into(), 0)).unwrap();
            let local_addr = listener.local_addr().unwrap();
            (
                listener
                    .accept_stream()
                    .map_ok(|(conn, _addr)| fuchsia_hyper::TcpStream { stream: conn }),
                format!("http://{}", local_addr),
            )
        };
        let server = Server::builder(from_stream(connections))
            .executor(fuchsia_hyper::Executor)
            .serve(make_service_fn(move |_socket: &fuchsia_hyper::TcpStream| async move {
                Ok::<_, Infallible>(service_fn(handle_req))
            }))
            .unwrap_or_else(|e| panic!("mock sse server failed: {:?}", e));
        fasync::spawn(server);
        url
    }

    fn make_event() -> Event {
        Event::from_type_and_data("event_type", "data_contents").unwrap()
    }

    #[fasync::run_singlethreaded(test)]
    async fn receive_one_event() {
        async fn handle_req(_req: Request<Body>) -> Result<Response<Body>, hyper::Error> {
            Ok(Response::builder()
                .status(StatusCode::OK)
                .header("content-type", "text/event-stream")
                .body(make_event().to_vec().into())
                .unwrap())
        }
        let url = spawn_server(handle_req);

        let client = Client::connect(new_https_client(), url).await.unwrap();
        let events: Result<Vec<_>, _> = client.collect::<Vec<_>>().await.into_iter().collect();

        assert_eq!(events.unwrap(), vec![make_event()]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn client_sends_correct_http_headers() {
        async fn handle_req(req: Request<Body>) -> Result<Response<Body>, hyper::Error> {
            assert_eq!(req.method(), &hyper::Method::GET);
            assert_eq!(
                req.headers().get("accept").map(|h| h.as_bytes()),
                Some(&b"text/event-stream"[..])
            );
            Ok(Response::builder()
                .status(StatusCode::OK)
                .header("content-type", "text/event-stream")
                .body(make_event().to_vec().into())
                .unwrap())
        }
        let url = spawn_server(handle_req);

        let client = Client::connect(new_https_client(), url).await.unwrap();
        client.collect::<Vec<_>>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn error_create_request() {
        assert_matches!(
            Client::connect(new_https_client(), "\n").await,
            Err(ClientConnectError::CreateRequest(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn error_make_request() {
        assert_matches!(
            Client::connect(new_https_client(), "bad_url").await,
            Err(ClientConnectError::MakeRequest(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn error_http_status() {
        async fn handle_req(_req: Request<Body>) -> Result<Response<Body>, hyper::Error> {
            Ok(Response::builder().status(StatusCode::NOT_FOUND).body(Body::empty()).unwrap())
        }
        let url = spawn_server(handle_req);

        assert_matches!(
            Client::connect(new_https_client(), url).await,
            Err(ClientConnectError::HttpStatus(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn error_downloading_chunk() {
        // If the body of an http response is not large enough, hyper will download the body
        // along with the header in the initial fuchsia_hyper::HttpsClient.request(). This means
        // that even if the body is implemented with a stream that fails before the transfer is
        // complete, the failure will occur during the initial request, before awaiting on the
        // body chunk stream.
        const BODY_SIZE_LARGE_ENOUGH_TO_TRIGGER_DELAYED_STREAMING: usize = 1_000_000;

        async fn handle_req(_req: Request<Body>) -> Result<Response<Body>, hyper::Error> {
            Ok(Response::builder()
                .status(StatusCode::OK)
                .header(
                    "content-length",
                    &format!("{}", BODY_SIZE_LARGE_ENOUGH_TO_TRIGGER_DELAYED_STREAMING),
                )
                .header("content-type", "text/event-stream")
                .body(Body::wrap_stream(futures::stream::iter(vec![
                    Ok(vec![b' '; BODY_SIZE_LARGE_ENOUGH_TO_TRIGGER_DELAYED_STREAMING - 1]),
                    Err("error-text".to_string()),
                ])))
                .unwrap())
        }
        let url = spawn_server(handle_req);
        let mut client = Client::connect(new_https_client(), url).await.unwrap();

        assert_matches!(client.try_next().await, Err(ClientPollError::NextChunk(_)));
    }
}
