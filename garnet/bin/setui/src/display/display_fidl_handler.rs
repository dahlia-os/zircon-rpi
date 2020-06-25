// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::fidl_hanging_get_responder,
    crate::fidl_hanging_get_responder_no_imports,
    crate::fidl_hanging_get_result_responder,
    crate::fidl_process,
    crate::fidl_processor::RequestContext,
    crate::request_respond,
    crate::switchboard::base::{LowLightMode, SettingRequest, SettingResponse, SettingType},
    crate::switchboard::hanging_get_handler::Sender,
    fidl_fuchsia_settings::{
        DisplayMarker, DisplayRequest, DisplaySettings, DisplayWatch2Responder,
        DisplayWatchLightSensor2Responder, DisplayWatchLightSensorResponder, DisplayWatchResponder,
        Error, LightSensorData, LowLightMode as FidlLowLightMode,
    },
    fuchsia_async as fasync,
    futures::future::LocalBoxFuture,
    futures::prelude::*,
};

// TODO(fxb/52593): Remove when clients are ported to watch2.
fidl_hanging_get_result_responder!(
    DisplaySettings,
    DisplayWatchResponder,
    DisplayMarker::DEBUG_NAME
);

fidl_hanging_get_responder!(DisplaySettings, DisplayWatch2Responder, DisplayMarker::DEBUG_NAME);

// TODO(fxb/52593): Remove when clients are ported to watch2.
fidl_hanging_get_result_responder!(
    LightSensorData,
    DisplayWatchLightSensorResponder,
    DisplayMarker::DEBUG_NAME
);

fidl_hanging_get_responder_no_imports!(
    LightSensorData,
    DisplayWatchLightSensor2Responder,
    DisplayMarker::DEBUG_NAME
);

impl From<SettingResponse> for LightSensorData {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::LightSensor(data) = response {
            let mut sensor_data = fidl_fuchsia_settings::LightSensorData::empty();
            sensor_data.illuminance_lux = Some(data.illuminance);
            sensor_data.color = Some(data.color);
            sensor_data
        } else {
            panic!("incorrect value sent to display");
        }
    }
}

impl From<SettingResponse> for DisplaySettings {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::Brightness(info) = response {
            let mut display_settings = fidl_fuchsia_settings::DisplaySettings::empty();

            display_settings.auto_brightness = Some(info.auto_brightness);
            display_settings.low_light_mode = match info.low_light_mode {
                LowLightMode::Enable => Some(FidlLowLightMode::Enable),
                LowLightMode::Disable => Some(FidlLowLightMode::Disable),
                LowLightMode::DisableImmediately => Some(FidlLowLightMode::DisableImmediately),
            };

            if !info.auto_brightness {
                display_settings.brightness_value = Some(info.manual_brightness_value);
            }

            display_settings
        } else {
            panic!("incorrect value sent to display");
        }
    }
}

fn to_request(settings: DisplaySettings) -> Option<SettingRequest> {
    let mut request = None;
    if let Some(brightness_value) = settings.brightness_value {
        request = Some(SettingRequest::SetBrightness(brightness_value));
    } else if let Some(enable_auto_brightness) = settings.auto_brightness {
        request = Some(SettingRequest::SetAutoBrightness(enable_auto_brightness));
    } else if let Some(low_light_mode) = settings.low_light_mode {
        request = match low_light_mode {
            FidlLowLightMode::Enable => Some(SettingRequest::SetLowLightMode(LowLightMode::Enable)),
            FidlLowLightMode::Disable => {
                Some(SettingRequest::SetLowLightMode(LowLightMode::Disable))
            }
            FidlLowLightMode::DisableImmediately => {
                Some(SettingRequest::SetLowLightMode(LowLightMode::DisableImmediately))
            }
        };
    }
    request
}

fidl_process!(
    Display,
    SettingType::Display,
    process_request,
    SettingType::Display,
    DisplaySettings,
    DisplayWatch2Responder,
    process_request_2,
    SettingType::LightSensor,
    LightSensorData,
    DisplayWatchLightSensorResponder,
    process_sensor_request,
    SettingType::LightSensor,
    LightSensorData,
    DisplayWatchLightSensor2Responder,
    process_sensor_request_2
);

// TODO(fxb/52593): Replace with logic from process_request_2
// and remove process_request_2 when clients ported to Watch2 and back.
async fn process_request(
    context: RequestContext<DisplaySettings, DisplayWatchResponder>,
    req: DisplayRequest,
) -> Result<Option<DisplayRequest>, anyhow::Error> {
    // Support future expansion of FIDL.
    #[allow(unreachable_patterns)]
    match req {
        DisplayRequest::Watch { responder } => {
            context.watch(responder, false).await;
        }
        _ => {
            return Ok(Some(req));
        }
    }
    return Ok(None);
}

async fn process_request_2(
    context: RequestContext<DisplaySettings, DisplayWatch2Responder>,
    req: DisplayRequest,
) -> Result<Option<DisplayRequest>, anyhow::Error> {
    // Support future expansion of FIDL.
    #[allow(unreachable_patterns)]
    match req {
        DisplayRequest::Set { settings, responder } => {
            if let Some(request) = to_request(settings) {
                fasync::spawn(async move {
                    request_respond!(
                        context,
                        responder,
                        SettingType::Display,
                        request,
                        Ok(()),
                        Err(Error::Unsupported),
                        DisplayMarker::DEBUG_NAME
                    );
                });
            } else {
                responder
                    .send(&mut Err(Error::Unsupported))
                    .log_fidl_response_error(DisplayMarker::DEBUG_NAME);
            }
        }
        DisplayRequest::Watch2 { responder } => {
            context.watch(responder, true).await;
        }
        _ => {
            return Ok(Some(req));
        }
    }

    return Ok(None);
}

// TODO(fxb/52593): Replace with logic from process_request_2
// and remove process_request_2 when clients ported to Watch2 and back.
async fn process_sensor_request(
    context: RequestContext<LightSensorData, DisplayWatchLightSensorResponder>,
    req: DisplayRequest,
) -> Result<Option<DisplayRequest>, anyhow::Error> {
    if let DisplayRequest::WatchLightSensor { delta, responder } = req {
        context
            .watch_with_change_fn(
                // Bucket watch requests to the nearest 0.01.
                // TODO(fxb/55112): this might be just an integer
                format!("{:.2}", delta),
                Box::new(move |old_data: &LightSensorData, new_data: &LightSensorData| {
                    if let (Some(old_lux), Some(new_lux)) =
                        (old_data.illuminance_lux, new_data.illuminance_lux)
                    {
                        (new_lux - old_lux).abs() >= delta
                    } else {
                        true
                    }
                }),
                responder,
                false,
            )
            .await;
    } else {
        return Ok(Some(req));
    }

    return Ok(None);
}

async fn process_sensor_request_2(
    context: RequestContext<LightSensorData, DisplayWatchLightSensor2Responder>,
    req: DisplayRequest,
) -> Result<Option<DisplayRequest>, anyhow::Error> {
    if let DisplayRequest::WatchLightSensor2 { delta, responder } = req {
        context
            .watch_with_change_fn(
                // Bucket watch requests to the nearest 0.01.
                // TODO(fxb/55112): this might be just an integer
                format!("{:.2}", delta),
                Box::new(move |old_data: &LightSensorData, new_data: &LightSensorData| {
                    if let (Some(old_lux), Some(new_lux)) =
                        (old_data.illuminance_lux, new_data.illuminance_lux)
                    {
                        (new_lux - old_lux).abs() >= delta
                    } else {
                        true
                    }
                }),
                responder,
                true,
            )
            .await;
    } else {
        return Ok(Some(req));
    }

    return Ok(None);
}
