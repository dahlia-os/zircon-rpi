// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "1024"]

#[macro_use]
extern crate anyhow;

pub mod audio;
pub mod backlight;
pub mod basemgr;
pub mod battery_simulator;
pub mod bluetooth;
pub mod camera;
pub mod common_utils;
pub mod component;
pub mod component_search;
pub mod device;
pub mod diagnostics;
pub mod factory_reset;
pub mod factory_store;
pub mod file;
pub mod gpio;
pub mod hardware_power_statecontrol;
pub mod hwinfo;
pub mod i2c;
pub mod input;
pub mod input_report;
pub mod kernel;
pub mod launch;
pub mod light;
pub mod location;
pub mod logging;
pub mod netstack;
pub mod paver;
pub mod repository_manager;
pub mod scenic;
pub mod server;
pub mod setui;
pub mod sysinfo;
pub mod tiles;
pub mod traceutil;
pub mod tracing;
pub mod update;
pub mod weave;
pub mod webdriver;
pub mod wlan;
pub mod wlan_deprecated;
pub mod wlan_phy;
pub mod wlan_policy;
