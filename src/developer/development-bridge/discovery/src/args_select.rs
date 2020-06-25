// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "select",
    description = "lists components matching a selector. 
Selector format: <component moniker>:(in|out|exposed)[:<service name>]. Wildcards may be used anywhere in the selector.
Example: 'remote-control:out:*' would return all services in 'out' for the component remote-control."
)]
pub struct SelectCommand {
    #[argh(positional)]
    pub selector: String,
}
