// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::message_hub_definition;
use crate::switchboard::base::{SettingAction, SettingEvent};
use std::fmt::Debug;

#[derive(PartialEq, Clone, Debug, Eq, Hash)]
pub enum Address {
    Switchboard,
    Registry,
}

// The types of data that can be sent.
#[derive(Clone, Debug)]
pub enum Payload {
    Action(SettingAction),
    Event(SettingEvent),
}

message_hub_definition!(Payload, Address);
