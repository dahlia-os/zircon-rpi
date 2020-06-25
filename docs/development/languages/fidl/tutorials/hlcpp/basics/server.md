# Implement a FIDL server

## Prerequisites

This tutorial builds on the [Compiling FIDL][fidl-intro] tutorial. For the
full set of FIDL tutorials, refer to the [overview][overview].

## Overview

This tutorial shows you how to implement a FIDL protocol
(`fuchsia.examples.Echo`) and run it on Fuchsia.

This document covers how to complete the following tasks:

* Implement a FIDL protocol.
* Build and run a package on Fuchsia.
* Serve a FIDL protocol.

The tutorial starts by creating a component that is served to a Fuchsia device
and run. Then, it gradually adds functionality to get the server up and running.

If you'd like to follow along by writing the code yourself, feel
free to delete the following directories to get started:

    rm -r examples/fidl/hlcpp/server/*

## Create and run a component {#component}

### Create the component

Add a `main()` function to `examples/fidl/hlcpp/server/main.cc`:

```c++
#include <stdio.h>

int main(int argc, const char** argv) {
  printf("Hello, world!\n");
  return 0;
}
```

Declare a target for the server in `examples/fidl/hlcpp/server/BUILD.gn`:

```gn
import("//src/sys/build/components.gni")

# Declare an executable for the server. This produces a binary with the
# specified output name that can run on Fuchsia.
executable("bin") {
  output_name = "fidl_echo_hlcpp_server"
  sources = [ "main.cc" ]
}

{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/hlcpp/server/BUILD.gn" region_tag="rest" %}
```

To get the server component up and running, there are three targets that are
defined. First, there is the raw executable file for the server that is built to run on Fuchsia.

Then, there is a component that is set up to simply run the server executable,
which is described using the component's manifest file:

```cmx
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/hlcpp/server/server.cmx" %}
```

Note that the binary name in the manifest matches the output name of the
`executable`.

Finally, the component is put into a package, which is the
unit of software distribution on Fuchsia. In this case, the package just
contains a single component.

For more details on packages, components, and how to build them, refer to
the [Building components][building-components] page.

### Run the component

Note: The instructions in this section are geared towards running the component
on QEMU, as this is the simplest way to get started with running Fuchsia, but
it is also possible to pick a different [product configuration][products] and
run on actual hardware if you are already familiar with running components on
other product configurations.

Add the server to your configuration and build:

    fx set core.x64 --with //examples/fidl/hlcpp/server
    fx build

Ensure `fx serve` is running in a separate tab and connected to an instance of
Fuchsia (e.g. running in QEMU sing `fx qemu`), then run the server:

    fx shell run fuchsia-pkg://fuchsia.com/echo-hlcpp-server#meta/echo-server.cmx

Note that the component is referenced by its [URL][glossary-url], which is determined
using the [`fuchsia-pkg://` scheme][glossary-scheme]. The package name
in the URL matches the `package_name` field in the `fuchsia_package`
declaration, and the manifest path in `meta/` matches the target name of the
`fuchsia_component`.

## Implement the server

### Add a dependency on the FIDL library

1. Add `"//examples/fidl/fuchsia.examples"` to the `deps` of the `executable`
2. Include the bindings into the main file with `#include <fuchsia/examples/cpp/fidl.h>`

The full `bin` target declaration should now look like this:

```
executable("bin") {
  output_name = "fidl_echo_hlcpp_server"
  sources = [ "main.cc" ]
  deps = [ "//examples/fidl/fuchsia.examples" ]
}
```

### Add an implementation for the protocol {#impl}

Add the following to `main.cc`, above the `main()` function:

```cpp
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/hlcpp/server/main.cc" region_tag="server" %}
```

The implementation contains the following elements:

* The class subclasses the [generated protocol class][bindings-iface] and
  overrides its pure virtual methods corresponding to the protocol methods.
* The method for `EchoString` replies with the request value by calling the
  callback on it.
* The method for `SendString` does not take a callback since this method does
  not have a response. Instead, the implementation sends an `OnString` event
  using the an `Echo_EventSender`.
* The class contains a pointer to an `Echo_EventSender`. This will be set
  later in the `main()` function.

Check that it's correct by rebuilding with `fx build`.

## Serve the protocol {#main}

To run a component that implements a FIDL protocol, make a request to the
[component manager][component-manager] to expose that FIDL protocol to other
components. The component manager then routes any requests for the echo protocol
to our server.

To fulfill these requests, the component manager requires the name of the protocol
as well as a handler that it should call when it has any incoming requests to
connect to a protocol matching the specified name.

The handler passed to it is a function that takes a channel (whose remote
end is owned by the client), and binds it to a `fidl::Binding` that has been
initialized using the server implementation. The `fidl::Binding` is a class
from the FIDL runtime that takes a FIDL protocol implementation and a channel,
and then listens on the channel for incoming requests. It will then decode
the requests, dispatch them to the correct method on our server class, and
write any response back to the client. Our main method will keep listening
for incoming requests on an [async loop][async-loop].

This complete process is described in further detail in the
[Life of a protocol open][protocol-open].

Let's take a look at the code:

### Initialize the event loop

The first aspect is the use of an async loop:

```cpp
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/hlcpp/server/main.cc" region_tag="main" highlight="2,15" %}
```

The code first initializes the loop and registers it as the default dispatcher
for the current thread. This comes first, as the async code in the `main()`
function will register itself with the default dispatcher, which is a static
thread local variable (which is why it does not need to be passed explicitly in
the rest of the code). At the end of the main function, the code runs the async loop.

### Initialize the binding

Then, the code initializes the `fidl::Binding` as mentioned above:

```cpp
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/hlcpp/server/main.cc" region_tag="main" highlight="4,5,6" %}
```

In order to run, a binding needs two things: an implementation of a protocol,
and a channel that the binding will listen for messages for that protocol on.
The binding is first initialized using the echo implementation, and will be
bound to a channel later.

The code also sets the event sender that is used to send events to the client:
it is obtained using the `events()` method on the `Binding`, and then passed to
the `EchoImpl` class.

### Define a protocol request handler {#handler}

Next, the code defines a handler for incoming requests from a client:

```cpp
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/hlcpp/server/main.cc" region_tag="main" highlight="7,8,9,10" %}
```

* Here, an "incoming request" is not a request for a specific method of `Echo`
  protocol, but rather a general request from a client to connect to an
  implementation of the `Echo` protocol.
* The request is defined as a `fidl::InterfaceRequest<Echo>`. This is a
  type-safe wrapper around a channel that indicates two things:
  * `InterfaceRequest` indicates that this is the server end of a channel (i.e.
    a client is connected to the remote end of the channel)
  * The template parameter `Echo` means that the client expects that a server
    implementing the `Echo` protocol binds itself to this channel. The client
    analog of this (i.e. the type that is being used on the client side to
    represent the other end of this channel) is a `fidl::InterfaceHandle<Echo>`.
* The handler simply takes the channel sent from the client, and binds it to the
  `Echo` binding.
* Once this happens, the `Binding` starts handling messages on the channel
  according to the `Echo` protocol. This is an example of [protocol request
  pipelining][pipeline] which is explored in a [later tutorial][pipeline-tut].

### Register the protocol request handler

Finally, the code registers the handler with the component manager:

```cpp
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/hlcpp/server/main.cc" region_tag="main" highlight="11,12" %}
```

The first line initializes and serves the outgoing directory, which contains
protocols that this component exposes to other components, and the second line
adds the handler to the outgoing directory.

An implicit second parameter besides the handler is the name that this handler
should be registered to - by default, this parameter is the name of the protocol
being passed in, which is generated because of the presence [`[Discoverable]`
attribute][discoverable] on the `Echo` protocol. In other words, after executing
this line you should be able to call `ls` on the component's `/out` directory
and see an entry called `"fuchsia.examples.Echo`.

### Add new dependencies {#deps}

This new code requires the following additional dependencies:

* `"//zircon/system/ulib/async-loop:async-loop-cpp"` and `"//zircon/system/ulib/async-loop:async-loop-default"`, which contain the async loop code.
* `"//sdk/lib/sys/cpp"`: The component framework C++ runtime, which contains
  utility code for interacting with the component's environment.
* `"//sdk/lib/fidl/cpp"`: The FIDL C++ runtime, which contains utility code for
  using the FIDL bindings.

The full `bin` target declaration should now look like this:

```
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/hlcpp/server/BUILD.gn" region_tag="bin" %}
```

Import the dependencies by including them at the top of `examples/fidl/hlcpp/server/main.cc`:

```cpp
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/hlcpp/server/main.cc" region_tag="includes" %}
```

## Run the server

Run the server:

  fx shell run fuchsia-pkg://fuchsia.com/echo-hlcpp-server#meta/echo-server.cmx

You should see the `printf` output from the `main()` function followed by the
server hanging. This is expected. Instead of exiting right away, the server
keeps waiting for incoming requests. The next step will be to write a client for
the server.

<!-- xrefs -->
[fidl-intro]: /docs/development/languages/fidl/tutorials/fidl.md
[building-components]: /docs/development/components/build.md
[products]: /docs/concepts/build_system/boards_and_products.md
[getting-started]: /docs/getting_started.md
[glossary-url]: /docs/glossary.md#component-url
[glossary-scheme]: /docs/glossary.md#fuchsia-pkg-url
[declaring-fidl]: /docs/development/languages/fidl/tutorials/fidl.md
[depending-fidl]: /docs/development/languages/fidl/tutorials/hlcpp/basics/using-fidl.md
[component-manager]: /docs/concepts/components/component_manager.md
[protocol-open]: /docs/concepts/components/life_of_a_protocol_open.md
[discoverable]: /docs/reference/fidl/bindings/hlcpp-bindings.md#discoverable
[bindings-iface]: /docs/reference/fidl/bindings/hlcpp-bindings.md#protocols
[pipeline]: /docs/concepts/api/fidl.md#request-pipelining
[pipeline-tut]: /docs/development/languages/fidl/tutorials/hlcpp/topics/request-pipelining.md
[compiling-fidl]: /docs/development/languages/fidl/tutorials/fidl.md
[async-loop]: /zircon/system/ulib/async-loop/include/lib/async-loop/cpp/loop.h
[overview]: /docs/development/languages/fidl/tutorials/overview.md
