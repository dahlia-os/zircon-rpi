// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::{strategies::base::AppStrategy, InternalSender, MessageInternal, RenderOptions},
    geometry::{IntSize, Size},
    view::{
        strategies::{
            base::{ScenicParams, ViewStrategyParams, ViewStrategyPtr},
            scenic::ScenicViewStrategy,
        },
        ViewKey,
    },
};
use anyhow::{bail, Context as _, Error};
use async_trait::async_trait;
use fidl::endpoints::{create_endpoints, create_proxy};
use fidl_fuchsia_ui_app::{ViewProviderRequest, ViewProviderRequestStream};
use fidl_fuchsia_ui_scenic::{ScenicProxy, SessionListenerRequest};
use fidl_fuchsia_ui_views::ViewToken;
use fuchsia_async::{self as fasync};
use fuchsia_component::{self as component};
use fuchsia_scenic::{Session, SessionPtr, ViewRefPair, ViewTokenPair};
use futures::{channel::mpsc::UnboundedSender, StreamExt, TryFutureExt, TryStreamExt};

pub(crate) struct ScenicAppStrategy {
    pub(crate) scenic: ScenicProxy,
}

impl ScenicAppStrategy {
    fn setup_session(
        &self,
        view_key: ViewKey,
        app_sender: &UnboundedSender<MessageInternal>,
    ) -> Result<SessionPtr, Error> {
        let (session_listener, session_listener_request) = create_endpoints()?;
        let (session_proxy, session_request) = create_proxy()?;

        self.scenic.create_session(session_request, Some(session_listener))?;
        let sender = app_sender.clone();
        fasync::spawn_local(
            session_listener_request
                .into_stream()?
                .map_ok(move |request| match request {
                    SessionListenerRequest::OnScenicEvent { events, .. } => {
                        for event in events {
                            match event {
                                fidl_fuchsia_ui_scenic::Event::Gfx(gfx_event) => match gfx_event {
                                    fidl_fuchsia_ui_gfx::Event::Metrics(metrics_event) => {
                                        sender
                                            .unbounded_send(MessageInternal::MetricsChanged(
                                                view_key,
                                                Size::new(
                                                    metrics_event.metrics.scale_x,
                                                    metrics_event.metrics.scale_y,
                                                ),
                                            ))
                                            .expect("MessageInternal::MetricsChanged");
                                    }
                                    fidl_fuchsia_ui_gfx::Event::ViewPropertiesChanged(
                                        view_properties_event,
                                    ) => {
                                        let bounding_box =
                                            &view_properties_event.properties.bounding_box;
                                        let new_size = Size::new(
                                            bounding_box.max.x - bounding_box.min.x,
                                            bounding_box.max.y - bounding_box.min.y,
                                        );
                                        sender
                                            .unbounded_send(MessageInternal::SizeChanged(
                                                view_key, new_size,
                                            ))
                                            .expect("MessageInternal::SizeChanged");
                                    }
                                    _ => (),
                                },
                                fidl_fuchsia_ui_scenic::Event::Input(input_event) => {
                                    sender
                                        .unbounded_send(MessageInternal::ScenicInputEvent(
                                            view_key,
                                            input_event,
                                        ))
                                        .expect("MessageInternal::ScenicInputEvent");
                                }
                                _ => (),
                            }
                        }
                    }
                    _ => (),
                })
                .try_collect::<()>()
                .unwrap_or_else(|e| eprintln!("view listener error: {:?}", e)),
        );

        Ok(Session::new(session_proxy))
    }
}

#[async_trait(?Send)]
impl AppStrategy for ScenicAppStrategy {
    async fn create_view_strategy(
        &self,
        key: ViewKey,
        render_options: RenderOptions,
        app_sender: UnboundedSender<MessageInternal>,
        strategy_params: ViewStrategyParams,
    ) -> Result<ViewStrategyPtr, Error> {
        let session = self.setup_session(key, &app_sender)?;
        let strategy_params = match strategy_params {
            ViewStrategyParams::Scenic(params) => params,
            _ => bail!("Incorrect ViewStrategyParams passed to create_view_strategy for scenic"),
        };
        Ok(ScenicViewStrategy::new(
            &session,
            render_options,
            strategy_params.view_token,
            strategy_params.control_ref,
            strategy_params.view_ref,
            app_sender.clone(),
        )
        .await?)
    }

    fn create_view_for_testing(
        &self,
        app_sender: &UnboundedSender<MessageInternal>,
    ) -> Result<(), Error> {
        let token = ViewTokenPair::new().context("ViewTokenPair::new")?;
        let ViewRefPair { control_ref, view_ref } =
            ViewRefPair::new().context("ViewRefPair::new")?;
        app_sender
            .unbounded_send(MessageInternal::CreateView(ViewStrategyParams::Scenic(ScenicParams {
                view_token: token.view_token,
                control_ref,
                view_ref,
            })))
            .expect("send");
        Ok(())
    }

    fn supports_scenic(&self) -> bool {
        return true;
    }

    fn start_services(
        &self,
        outgoing_services_names: Vec<&'static str>,
        app_sender: UnboundedSender<MessageInternal>,
    ) -> Result<(), Error> {
        let mut fs = component::server::ServiceFs::new_local();
        let mut public = fs.dir("svc");

        let sender = app_sender.clone();
        let f = move |stream: ViewProviderRequestStream| {
            let sender = sender.clone();
            fasync::spawn_local(
                stream
                    .try_for_each(move |req| {
                        let (token, control_ref, view_ref) = match req {
                            ViewProviderRequest::CreateView { token, .. } => {
                                // We do not get passed a view ref so create our own
                                let ViewRefPair { control_ref, view_ref } =
                                    ViewRefPair::new().expect("unable to create view ref pair");
                                (token, control_ref, view_ref)
                            }
                            ViewProviderRequest::CreateViewWithViewRef {
                                token,
                                view_ref_control,
                                view_ref,
                                ..
                            } => (token, view_ref_control, view_ref),
                        };
                        let view_token = ViewToken { value: token };
                        sender
                            .unbounded_send(MessageInternal::CreateView(
                                ViewStrategyParams::Scenic(ScenicParams {
                                    view_token,
                                    control_ref,
                                    view_ref,
                                }),
                            ))
                            .expect("send");
                        futures::future::ready(Ok(()))
                    })
                    .unwrap_or_else(|e| eprintln!("error running ViewProvider server: {:?}", e)),
            )
        };
        public.add_fidl_service(f);

        for name in outgoing_services_names {
            let sender = app_sender.clone();
            public.add_service_at(name, move |channel| {
                sender
                    .unbounded_send(MessageInternal::ServiceConnection(channel, name))
                    .expect("unbounded_send");
                None
            });
        }

        match fs.take_and_serve_directory_handle() {
            Err(e) => eprintln!("Error publishing services: {:#}", e),
            Ok(_) => (),
        }

        fasync::spawn_local(fs.collect());

        Ok(())
    }

    fn get_scenic_proxy(&self) -> Option<&ScenicProxy> {
        return Some(&self.scenic);
    }

    fn get_frame_buffer_size(&self) -> Option<IntSize> {
        None
    }

    fn get_pixel_size(&self) -> u32 {
        4
    }

    fn get_pixel_format(&self) -> fuchsia_framebuffer::PixelFormat {
        fuchsia_framebuffer::PixelFormat::Argb8888
    }

    fn get_linear_stride_bytes(&self) -> u32 {
        0
    }

    async fn post_setup(
        &mut self,
        _: fuchsia_framebuffer::PixelFormat,
        _internal_sender: &InternalSender,
    ) -> Result<(), Error> {
        Ok(())
    }
}
