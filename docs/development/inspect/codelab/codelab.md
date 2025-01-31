# Inspect codelab

This document contains the codelab for Inspect in C++, Dart and Rust.

The code is available at:

* [//src/diagnostics/examples/inspect/cpp][inspect-cpp-codelab].
* [//src/diagnostics/examples/inspect/rust][inspect-rust-codelab].
* [//topaz/public/dart/fuchsia_inspect/codelab][inspect-dart-codelab].

This codelab is organized into several parts, each with their own
subdirectory. The starting point for the codelab is part 1,
and the code for each part contains the solution for the previous parts.

* [C++ Part 1][cpp-part1]
* [Rust Part 1][rust-part1]
* [Dart Part 1][dart-part1]

When working on this codelab, you may continue adding your solutions to
"part\_1", or you may skip around by building on the existing solutions.

## Prerequisites

Set up your development environment.

This codelab assumes you have completed [Getting Started](/docs/getting_started.md) and have:

1. A checked out and built Fuchsia tree.
2. A device or emulator (`fx emu`) that runs Fuchsia.
3. A workstation to serve components (`fx serve`) to your Fuchsia device or emulator.

To build and run the examples in this codelab, add the following arguments
to your `fx set` invocation:

Note: Replace core.x64 with your product and board configuration.

* {C++}

   ```
   fx set core.x64 \
   --with //src/diagnostics/examples/inspect/cpp \
   --with //src/diagnostics/examples/inspect/cpp:tests
   ```

* {Rust}

   ```
   fx set core.x64 \
   --with //src/diagnostics/examples/inspect/rust \
   --with //src/diagnostics/examples/inspect/rust:tests
   ```

* {Dart}

   ```
   fx set workstation.x64
   --with //topaz/public/dart/fuchsia_inspect/codelab:all
   ```

## Part 1: A buggy component

There is a component that serves a protocol called [Reverser][fidl-reverser]:

```fidl
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/fidl/reverser.test.fidl" region_tag="reverser_fidl" adjust_indentation="auto" %}
```

This protocol has a single method, called "Reverse," that simply reverses
any string passed to it. An implementation of the protocol is provided,
but it has a critical bug. The bug makes clients who attempt to call
the Reverse method see that their call hangs indefinitely. It is up to
you to fix this bug.

### Run the component

There is a client application that will launch the Reverser component and send the rest of its
command line arguments as strings to Reverse:


1. See usage

   * {C++}

      ```
      fx shell run inspect_cpp_codelab_client
      ```

   * {Rust}

      ```
      fx shell run inspect_rust_codelab_client
      ```

   * {Dart}

      ```
      fx shell run inspect_dart_codelab_client
      ```

2. Run part 1 code, and reverse the string "Hello"

   * {C++}

      ```
      fx shell run inspect_cpp_codelab_client 1 Hello
      ```

   * {Rust}

      ```
      fx shell run inspect_rust_codelab_client 1 Hello
      ```

      This command prints some output containing errors.

   * {Dart}

      ```
      fx shell run inspect_dart_codelab_client 1 Hello
      ```

   These commands hang.

3. Press Ctrl+C to stop the client and try running with
   more arguments:

   * {C++}

      ```
      fx shell run inspect_cpp_codelab_client 1 Hello World
      ```

   * {Rust}

      ```
      fx shell run inspect_rust_codelab_client 1 Hello World
      ```

   * {Dart}

      ```
      fx shell run inspect_dart_codelab_client 1 Hello World
      ```

      This command also prints no outputs.

   These commands also hang.

You are now ready to look through the code to troubleshoot the issue.

### Look through the code

Now that you can reproduce the problem, take a look at what the client is doing:

* {C++}

   In the [client main][cpp-client-main]:

   ```cpp
   {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/client/main.cc" region_tag="reverse_loop" adjust_indentation="auto" %}
   ```

* {Rust}

   In the [client main][rust-client-main]:

   ```rust
   {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/client/src/main.rs" region_tag="reverse_loop" adjust_indentation="auto" %}
   ```

* {Dart}

  In the [client main][dart-client-main]:

  ```dart
  {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/client/lib/main.dart" region_tag="reverse_loop" adjust_indentation="auto" %}
  ```


In this code snippet, the client calls the `Reverse` method but never
seems to get a response. There doesn't seem to be an error message
or output.

Take a look at the server code for this part of the
codelab. There is a lot of standard component setup:

* {C++}

   In the [part 1 main][cpp-part1-main]:

   - Logging initialization

     ```cpp
     {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_1/main.cc" region_tag="init_logger" adjust_indentation="auto" %}
     ```

   - Creating an asynchronous executor

     ```cpp
     {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_1/main.cc" region_tag="async_executor" adjust_indentation="auto" %}
     ```

   - Serving a public service

     ```cpp
     {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_1/main.cc" region_tag="serve_outgoing" adjust_indentation="auto" %}
     ```

* {Rust}

   In the [part 1 main][rust-part1-main]:

   - Logging initialization

     ```rust
     {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_1/src/main.rs" region_tag="init_logger" adjust_indentation="auto" %}
     ```

   - ServiceFs initialization and collection

     ```rust
     {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_1/src/main.rs" region_tag="servicefs_init" adjust_indentation="auto" %}
     ```

   - ServiceFs collection

     ```rust
     {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_1/src/main.rs" region_tag="servicefs_collect" adjust_indentation="auto" %}
     ```

   - Serving a public service

     ```rust
     {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_1/src/main.rs" region_tag="serve_service" adjust_indentation="auto" %}
     ```

* {Dart}

   In the [part 1 main][dart-part1-main]:

   - Logging initialization

     ```dart
     {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_1/lib/main.dart" region_tag="init_logger" adjust_indentation="auto" %}
     ```

   - Serving a public service

     ```dart
     {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_1/lib/main.dart" region_tag="serve_service" adjust_indentation="auto" %}
     ```

See what the reverser definition is:

* {C++}

   In [reverser.h][cpp-part1-reverser-h]:

   ```cpp
   {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_1/reverser.h" region_tag="reverser_h" adjust_indentation="auto" %}
   ```

   This class implements the `Reverser` protocol. A helper method called
   `CreateDefaultHandler` constructs an `InterfaceRequestHandler` that
   creates new `Reverser`s for incoming requests.

* {Rust}

   In [reverser.rs][rust-part1-reverser]:

   ```rust
   {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_1/src/reverser.rs" region_tag="reverser_def" adjust_indentation="auto" %}
   ```

   This struct serves the `Reverser` protocol. The `ReverserServerFactory` (will make more sense
   later) constructs a `ReverserServer` when a new connection to `Reverser` is established.

- {Dart}

   In [reverser.dart][dart-part1-reverser]:

   ```dart
   {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_1/lib/src/reverser.dart" region_tag="reverser_impl" adjust_indentation="auto" %}
   ```

   This class implements the `Reverser` protocol. A helper method called `getDefaultBinder` returns
   a closure that creates new `Reverser`s for incoming requests.


### Add Inspect

Now that you know the code structure, you can start to instrument the
code with Inspect to find the problem.

Note: [Inspect](/docs/development/inspect/README.md) is a powerful instrumentation feature for
Fuchsia Components. You can expose structured information about the component's state to diagnose
the problem.

You may have previously debugged programs by printing or logging. While
this is often effective, asynchronous Components that run persistently
often output numerous logs about their internal state over time. This
codelab shows how Inspect provides snapshots of your component's current
state without needing to dig through logs.

1. Include Inspect dependencies:

   * {C++}

      In [BUILD.gn][cpp-part1-build]:

      ```
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_2/BUILD.gn" region_tag="part_1_solution_build_dep" adjust_indentation="auto" %}
      ```

   * {Rust}

      In [BUILD.gn][rust-part1-build] in `deps` under `rustc_binary("bin")`:

      ```
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_2/BUILD.gn" region_tag="part_1_solution_build_dep" adjust_indentation="auto" %}
      ```

   * {Dart}

     In [BUILD.gn][dart-part1-build] in `deps` under `dart_library("lib")` and
     `dart_app("bin")`:

     ```
     {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_2/BUILD.gn" region_tag="part_1_solution_build_dep" adjust_indentation="auto" %}
     ```

2. Initialize Inspect:

   * {C++}

      In [main.cc][cpp-part1-main]:

      ```cpp
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_2/main.cc" region_tag="part_1_include_inspect" adjust_indentation="auto" %}
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_2/main.cc" region_tag="part_1_init_inspect" adjust_indentation="auto" %}
      ```


   * {Rust}

      In [main.rs][rust-part1-main]:

      ```rust
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_2/src/main.rs" region_tag="part_1_use_inspect" adjust_indentation="auto" %}
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_2/src/main.rs" region_tag="part_1_serve_inspect" adjust_indentation="auto" %}
      ```

   * {Dart}

      In [main.dart][dart-part1-main]:

      ```dart
      {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_2/lib/main.dart" region_tag="part_1_import_inspect" adjust_indentation="auto" %}
      {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_2/lib/main.dart" region_tag="part_1_init_inspect" adjust_indentation="auto" %}
      ```

   You are now using Inspect.

3. Add a simple "version" property to show which version is running:

   * {C++}

      ```cpp
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_2/main.cc" region_tag="part_1_write_version" adjust_indentation="auto" %}
      ```

      This snippet does the following:

      1. Obtain the "root" node of the Inspect hierarchy.

         The Inspect hierarchy for your component consists of a tree of Nodes,
         each of which contains any number of properties.

      2. Create a new property using `CreateString`.

         This adds a new `StringProperty` on the root. This `StringProperty`
         is called "version", and its value is "part1".

      3. Emplace the new property in the inspector.

         The lifetime of a property is tied to an object returned by `Create`,
         and destroying the object causes the property to disappear. The
         optional third parameter emplaces the new property in `inspector`
         rather than return it.  As a result, the new property lives as long
         as the inspector itself (the entire execution of the component).

   * {Rust}

     ```rust
     {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_2/src/main.rs" region_tag="part_1_write_version" adjust_indentation="auto" %}
     ```

     This snippet does the following:

     1. Obtain the "root" node of the Inspect hierarchy.

        The Inspect hierarchy for your component consists of a tree of Nodes,
        each of which contains any number of properties.

     2. Create a new property using `record_string`.

        This adds a new `StringProperty` on the root. This `StringProperty`
        is called "version", and its value is "part1".

     3. It records it in the root node.

        The usual way of creating properties is through `create_*` methods on nodes. The lifetime of
        a property created with these methods is tied to the object returned and destroying the
        object causes the property to disappear. The library provides convinience methods `record_*`
        that perform creation of a property and tie the property lifetime to the node on which the
        method was called. As a result, the new property lives as long as the node itself (in this
        case, as long as the root node, so the entire execution of the component).

   * {Dart}

     ```dart
     {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_2/lib/main.dart" region_tag="part_1_write_version" adjust_indentation="auto" %}
     ```

     This snippet does the following:

     1. Obtain the "root" node of the Inspect hierarchy.

        The Inspect hierarchy for your component consists of a tree of Nodes,
        each of which contains any number of properties.

     2. Create a new property using `stringProperty(...).setValue(...)`.

        This adds a new `StringProperty` on the root. This `StringProperty`
        is called "version", and its value is "part1".

     3. It records it in the root node.

        The lifetime of a property is tied to the lifetime of the node where it was created (in this
        case root, so the lifetime of the component). To delete the property one would have to call
        `delete()` on it.


### Reading Inspect data

Now that you have added Inspect to your component, you can read what it says:

1. Rebuild and push the component:

   * {C++}

      ```
      fx build-push inspect_cpp_codelab
      ```
   * {Rust}

      ```
      fx build-push inspect_rust_codelab
      ```

   * {Dart}

      ```
      fx build-push inspect_dart_codelab_part_1
      ```

   In some cases you may find it useful to rebuild and update all components:

   ```
   fx build && fx update
   ```

2. Run the client:

   * {C++}

      ```
      fx shell run inspect_cpp_codelab_client 1 Hello
      ```

   * {Rust}

      ```
      fx shell run inspect_rust_codelab_client 1 Hello
      ```

   * {Dart}

      ```
      fx shell run inspect_dart_codelab_client 1 Hello
      ```

   Note that these should still hang.

3. Use `iquery` (Inspect query) to view your output:

   ```
   fx iquery
   ```

   This dumps all of the Inspect data for the entire system, which may be a lot of data.

4. Since `iquery` supports regex matching, run:

   * {C++}

      ```
      $ fx iquery show codelab_\*/inspect_cpp_codelab_part_1.cmx
      # or `fx iquery show --manifest_inspect_cpp_codelab_part_1`
      /hub/r/codelab/1234/c/inspect_cpp_codelab_part_1.cmx/1234/out/diagnostics/root.inspect:
        version = part1
      ```

   * {Rust}

      ```
      $ fx iquery show codelab_\*/inspect_rust_codelab_part_1.cmx
      # or `fx iquery show --manifest_inspect_rust_codelab_part_1`
      /hub/r/codelab/1234/c/inspect_rust_codelab_part_1.cmx/1234/out/diagnostics/root.inspect:
        version = part1
      ```

   * {Dart}

      ```
      $ fx iquery show codelab_\*/inspect_dart_codelab_part_1.cmx
      # or `fx iquery show --manifest_inspect_dart_codelab_part_1`
      /hub/r/codelab/1234/c/inspect_dart_codelab_part_1.cmx/1234/out/diagnostics/root.inspect:
        version = part1
      ```

5. You can also view the output as JSON:

   * {C++}

      ```
      $ fx iquery -f json show codelab_\*/inspect_cpp_codelab_part_1.cmx
      [
          {
              "contents": {
                  "root": {
                      "version": "part1"
                  }
              },
              "path": "/hub/r/codelab/1234/c/inspect_cpp_codelab_part_1.cmx/1234/out/diagnostics/root.inspect"
          }
      ]
      ```

   * {Rust}

      ```
      $ fx iquery -f json show codelab_\*/inspect_rust_codelab_part_1.cmx
      [
          {
              "contents": {
                  "root": {
                      "version": "part1"
                  }
              },
              "path": "/hub/r/codelab/1234/c/inspect_rust_codelab_part_1.cmx/1234/out/diagnostics/root.inspect"
          }
      ]
      ```

   * {Dart}

      ```
      $ fx iquery -f json show codelab_\*/inspect_dart_codelab_part_1.cmx
      [
          {
              "contents": {
                  "root": {
                      "version": "part1"
                  }
              },
              "path": "/hub/r/codelab/1234/c/inspect_dart_codelab_part_1.cmx/1234/out/diagnostics/root.inspect"
          }
      ]
      ```

### Instrumenting the code to find the bug

Now that you have initialized Inspect and know how to read data, you
are ready to instrument your code and uncover the bug.

The previous output shows you how the component is actually running
and that the component is not hanging completely. Otherwise the Inspect
read would hang.

Add new information per-connection to observe if the connection
is even being handled by your component.

1. Add a new child to your root node to contain statistics about the `reverser` service:

   * {C++}

      ```cpp
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_2/main.cc" region_tag="part_1_new_child" adjust_indentation="auto" %}
      ```

   * {Rust}


      ```rust
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_2/src/main.rs" region_tag="part_1_new_child" adjust_indentation="auto" %}
      ```

   * {Dart}


      ```dart
      {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_2/lib/main.dart" region_tag="part_1_new_child" adjust_indentation="auto" %}
      ```

2. Update your server to accept this node:

   * {C++}

      Update the definition of `CreateDefaultHandler` in [reverser.h][cpp-part1-reverser-h]
      and [reverser.cc][part1-reverser-cc]:

      ```cpp
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_2/reverser.h" region_tag="part_1_include" adjust_indentation="auto" %}
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_2/reverser.cc" region_tag="part_1_update_server" adjust_indentation="auto" %}
      ```

   * {Rust}

      Update `ReverserServerFactory::new` to accept this node in [reverser.rs][rust-part1-reverser]:

      ```rust
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_2/src/reverser.rs" region_tag="part_1_use" adjust_indentation="auto" %}
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_2/src/reverser.rs" region_tag="part_1_update_reverser" adjust_indentation="auto" %}
      ```

   * {Dart}

      Update the definition of `getDefaultBinder` in [reverser.dart][dart-part1-reverser]
      and [reverser.cc][part1-reverser-cc]:

      ```dart
      {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_2/lib/src/reverser.dart" region_tag="part_1_import" adjust_indentation="auto" %}
      {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_2/lib/src/reverser.dart" region_tag="part_1_update_reverser" adjust_indentation="auto" %}
      ```

3. Add a property to keep track of the number of connections:

   Note: Nesting related data under a child is a powerful feature of Inspect.

   * {C++}

      ```cpp
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_2/reverser.cc" region_tag="part_1_add_connection_count" adjust_indentation="auto" %}
      ```

     Note: `node` is moved into the handler so that it is not dropped and
     deleted from the output.

   * {Rust}

      ```rust
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_3/src/reverser.rs" region_tag="part_1_add_connection_count" adjust_indentation="auto" %}
      ```

     Note: `node` is moved into the handler so that it is not dropped and
     deleted from the output.

     Note: `node` is kept in ReverserServerFactory so that it is not dropped and deleted from the
     output.

   * {Dart}

      ```dart
      {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_2/lib/src/reverser.dart" region_tag="part_1_add_connection_count" adjust_indentation="auto" %}
      ```

   This snippet demonstrates creating a new `UintProperty` (containing a 64
   bit unsigned int) called `connection_count` and setting it to 0. In the handler
   (which runs for each connection), the property is incremented by 1.

4. Rebuild, re-run your component and then run iquery:

   * {C++}

      ```
      $ fx iquery -f json --manifest inspect_cpp_codelab_part_1
      ```

   * {Rust}

      ```
      $ fx iquery -f json --manifest inspect_rust_codelab_part_1
      ```

   * {Dart}

      ```
      $ fx iquery -f json --manifest inspect_dart_codelab_part_1
      ```

   You should now see:

   ```
   ...
   "contents": {
       "root": {
           "reverser_service": {
               "connection_count": 1,
           },
           "version": "part1"
       }
   }
   ```

The output above demonstrates that the client successfully connected
to the service, so the hanging problem must be caused by the Reverser
implementation itself. In particular, it will be helpful to know:

1. If the connection is still open while the client is hanging.

2. If the `Reverse` method was called.


**Exercise**: Create a child node for each connection, and record
"request\_count" inside the Reverser.

- *Hint*: There is a utility function for generating unique names:

   * {C++}

      ```cpp
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_2/reverser.cc" region_tag="part_1_connection_child" adjust_indentation="auto" %}
      ```

   * {Rust}

      ```rust
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_2/src/reverser.rs" region_tag="part_1_connection_child" adjust_indentation="auto" %}
      ```

   * {Dart}

      ```dart
      {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_2/lib/src/reverser.dart" region_tag="part_1_connection_child" adjust_indentation="auto" %}
      ```

   This will create unique names starting with "connection".


* {C++}

   *Hint*: You will find it helpful to create a constructor for Reverser
   that takes `inspect::Node`. [Part 3](#part-3) of this codelab explains why this is
   a useful pattern.

* {Rust}

   *Hint*: You will find it helpful to create a constructor for `ReverserServer`
   that takes `inspect::Node` for the same reason as we did for `ReverserServerFactory`.

* {Dart}

   *Hint*: You will find it helpful to create a constructor for `ReverserImpl`
   that takes `inspect.Node`. [Part 3](#part-3) of this codelab explains why this is
   a useful pattern.

- *Hint*: You will need to create a member on Reverser to hold the
`request_count` property. Its type will be `inspect::UintProperty`.

- *Follow up*: Does request count give you all of the information you
need? Add `response_count` as well.

- *Advanced*: Can you add a count of *all* requests on *all*
connections? The Reverser objects must share some state. You may find
it helpful to refactor arguments to Reverser into a separate struct
(See solution in [part 2](#part-2) for this approach).

After completing this exercise and running iquery, you should see something like this:

```
...
"contents": {
    "root": {
        "reverser_service": {
            "connection-0x0": {
                "request_count": 1,
            },
            "connection_count": 1,
        },
        "version": "part1"
    }
}
```

The output above shows that the connection is still open and it received one request.

* {C++}

   If you added "response\_count" as well, you may have noticed the bug.
   The `Reverse` method receives a `callback`, but it is never called with the value of `output`.

* {Rust}

   If you added "response\_count" as well, you may have noticed the bug.
   The `Reverse` method receives a `responder`, but it is never called with the value of `result`.

* {Dart}

   If you added "response\_count" as well, you may have noticed the bug.
   The `reverse` method receives never returns the value of `result`.


1. Send the response:

   * {C++}

      ```cpp
      // At the end of Reverser::Reverse
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_2/reverser.cc" region_tag="part_1_callback" adjust_indentation="auto" %}
      ```

   * {Rust}

      ```rust
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_2/src/reverser.rs" region_tag="part_1_respond" adjust_indentation="auto" %}
      ```

   * {Dart}

      ```dart
      {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_2/lib/src/reverser.dart" region_tag="part_1_result" adjust_indentation="auto" %}
      ```

2. Run the client again:

   * {C++}

      ```
      fx shell run inspect_cpp_codelab_client 1 hello
      Input: hello
      Output: olleh
      Done. Press Ctrl+C to exit
      ```

   * {Rust}

      ```
      fx shell run inspect_rust_codelab_client 1 hello
      Input: hello
      Output: olleh
      Done. Press Ctrl+C to exit
      ```

   * {Dart}

      ```
      fx shell run inspect_dart_codelab_client 1 hello
      Input: hello
      Output: olleh
      Done. Press Ctrl+C to exit
      ```

   The component continues running until Ctrl+C is pressed to give you
   a chance to run iquery and observe your output.

This concludes part 1. You may commit your changes so far:

```
git commit -am "solution to part 1"
```

## Part 2: Diagnosing inter-component problems {#part-2}

Note: All links and examples in this section refer to "part\_2" code. If
you are following along, you may continue using "part\_1."

You received a bug report. The "FizzBuzz" team is saying they
are not receiving data from your component.

In addition to serving the Reverser protocol, the component also reaches
out to the "FizzBuzz" service and prints the response:

* {C++}

   ```cpp
   {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_1/main.cc" region_tag="fizzbuzz_connect" adjust_indentation="auto" %}
   ```

* {Rust}

   ```rust
   {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_1/src/main.rs" region_tag="fizzbuzz_connect" adjust_indentation="auto" %}
   ```

* {Dart}

   ```dart
   {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_1/lib/main.dart" region_tag="connect_fizzbuzz" adjust_indentation="auto" %}
   ```

If you see the logs, you will see that this log is never printed.

* {C++}

   ```cpp
   fx log --tag inspect_cpp_codelab
   ```

* {Rust}

   ```rust
   fx log --tag inspect_rust_codelab
   ```

* {Dart}

   ```dart
   fx log --tag inspect_dart_codelab_part_2
   ```

You will need to diagnose and solve this problem.

### Diagnose the issue with Inspect

1. Run the component to see what is happening:

   Note: Replace 2 with 1 if you are continuing from part 1.

   * {C++}

      ```
      $ fx shell run inspect_cpp_codelab_client 2 hello
      ```

   * {Rust}

      ```
      $ fx shell run inspect_rust_codelab_client 2 hello
      ```

   * {Dart}

      ```
      $ fx shell run inspect_dart_codelab_client 2 hello
      ```

   Fortunately the FizzBuzz team instrumented their component using Inspect.

2. Read the FizzBuzz Inspect data using iquery as before, you get:

   ```
   "contents": {
       "root": {
           "fizzbuzz_service": {
               "closed_connection_count": 0,
               "incoming_connection_count": 0,
               "request_count": 0,
               ...
   ```

   This output confirms that FizzBuzz is not receiving any connections.

3. Add Inspect to identify the problem:

   * {C++}

      ```cpp
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_2/main.cc" region_tag="instrument_fizzbuzz" adjust_indentation="auto" %}
      ```

   * {Rust}

      ```rust
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_2/src/main.rs" region_tag="instrument_fizzbuzz" adjust_indentation="auto" %}
      ```

   * {Dart}

      ```dart
      {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_2/lib/main.dart" region_tag="instrument_fizzbuzz" adjust_indentation="auto" %}
      ```

**Exercise**: Add Inspect to the FizzBuzz connection to identify the problem

- *Hint*: Use the snippet above as a starting point, it provides an
error handler for the connection attempt.

* {C++}

   *Follow up*: Can you store the status somewhere? You can convert it
   to a string using `zx_status_get_string(status)`.

   *Advanced*: `inspector` has a method called `Health()` that announces
   overall health status in a special location. Since our service is not
   healthy unless it can connect to FizzBuzz, can you incorporate this:

     ```cpp
     /*
     "fuchsia.inspect.Health": {
         "status": "STARTING_UP"
     }
     */
     inspector.Health().StartingUp();

     /*
     "fuchsia.inspect.Health": {
         "status": "OK"
     }
     */
     inspector.Health().Ok();

     /*
     "fuchsia.inspect.Health": {
         "status": "UNHEALTHY",
         "message": "Something went wrong!"
     }
     */
     inspector.Health().Unhealthy("Something went wrong!");
     ```

* {Rust}

   *Advanced*: `fuchsia_inspect::component` has a function called `health()` that returns an object
   that announces overall health status in a special location (a node child of the root of the
   inspect tree). Since our service is not healthy unless it can connect to FizzBuzz, can
   you incorporate this:

   ```rust
   /*
   "fuchsia.inspect.Health": {
       "status": "STARTING_UP"
   }
   */
   fuchsia_inspect::component::health().set_starting_up();

   /*
   "fuchsia.inspect.Health": {
       "status": "OK"
   }
   */
   fuchsia_inspect::component::health().set_ok();

   /*
   "fuchsia.inspect.Health": {
       "status": "UNHEALTHY",
       "message": "Something went wrong!"
   }
   */
   fuchsia_inspect::component::health().set_unhealthy("something went wrong!");
   ```

* {Dart}

   *Advanced*: `fuchsia_inspect::Inspect` has a getter called `health` that returns an object
   that announces overall health status in a special location (a node child of the root of the
   inspect tree). Since our service is not healthy unless it can connect to FizzBuzz, can
   you incorporate this:

   ```dart
   /*
   "fuchsia.inspect.Health": {
       "status": "STARTING_UP"
   }
   */
   inspect.Inspect().health.setStartingUp();

   /*
   "fuchsia.inspect.Health": {
       "status": "OK"
   }
   */
   inspect.Inspect().health.setOk();

   /*
   "fuchsia.inspect.Health": {
       "status": "UNHEALTHY",
       "message": "Something went wrong!"
   }
   */
   inspect.Inspect().health.setUnhealthy('Something went wrong!');
   ```

Once you complete this exercise, you should see that the connection
error handler is being called with a "not found" error. Inspect
output showed that FizzBuzz is running, so maybe something is
misconfigured. Unfortunately not everything uses Inspect (yet!) so
look at the logs:

* {C++}

   ```
   $ fx log --only FizzBuzz
   ...
   ... Component fuchsia-pkg://fuchsia.com/inspect_cpp_codelab_part_2.cmx
   is not allowed to connect to fuchsia.examples.inspect.FizzBuzz...
   ```

* {Rust}

   ```
   $ fx log --only FizzBuzz
   ...
   ... Component fuchsia-pkg://fuchsia.com/inspect_rust_codelab_part_2.cmx
   is not allowed to connect to fuchsia.examples.inspect.FizzBuzz...
   ```

* {Dart}

   ```
   $ fx log --only FizzBuzz
   ...
   ... Component fuchsia-pkg://fuchsia.com/inspect_dart_codelab_part_2.cmx
   is not allowed to connect to fuchsia.examples.inspect.FizzBuzz...
   ```

Sandboxing errors are a common pitfall that are sometimes difficult to uncover.

Note: While you could have looked at the logs from the beginning to find
the problem, the log output for the system can be extremely verbose. The
particular log that you are looking for was a kernel log from the framework,
which is additionally difficult to test for.

Looking at the sandbox in part2 meta, you can see it is missing the service:

* {C++}

    Find the sandbox meta in [part_2/meta][cpp-part2-meta]

* {Rust}

    Find the sandbox meta in [part_2/meta][rust-part2-meta]

* {Dart}

    Find the sandbox meta in [part_2/meta][dart-part2-meta]

```
"sandbox": {
    "services": [
        "fuchsia.logger.LogSink"
    ]
}
```

Add "fuchsia.examples.inspect.FizzBuzz" to the services array, rebuild,
and run again. You should now see FizzBuzz in the logs and an OK status:

* {C++}

   ```
   $ fx log --tag inspect_cpp_codelab
   [inspect_cpp_codelab, part2] INFO: main.cc(57): Got FizzBuzz: 1 2 Fizz
   4 Buzz Fizz 7 8 Fizz Buzz 11 Fizz 13 14 FizzBuzz 16 17 Fizz 19 Buzz Fizz
   22 23 Fizz Buzz 26 Fizz 28 29 FizzBuzz
   ```

* {Rust}

   ```
   $ fx log --tag inspect_rust_codelab
   [inspect_rust_codelab, part2] INFO: main.rs(52): Got FizzBuzz: 1 2 Fizz
   4 Buzz Fizz 7 8 Fizz Buzz 11 Fizz 13 14 FizzBuzz 16 17 Fizz 19 Buzz Fizz
   22 23 Fizz Buzz 26 Fizz 28 29 FizzBuzz
   ```

* {Dart}

   ```
   $ fx log --tag inspect_dart_codelab
   [inspect_dart_codelab, part2] INFO: main.dart(35): Got FizzBuzz: 1 2 Fizz
   4 Buzz Fizz 7 8 Fizz Buzz 11 Fizz 13 14 FizzBuzz 16 17 Fizz 19 Buzz Fizz
   22 23 Fizz Buzz 26 Fizz 28 29 FizzBuzz
   ```

This concludes Part 2.

You can now commit your solution:

```
git commit -am "solution for part 2"
```

## Part 3: Unit Testing for Inspect {#part-3}

Note: All links and examples in this section refer to "part\_3" code. If
you are following along, you may continue using the part you started with.

All code on Fuchsia should be tested, and this applies to Inspect data as well.

While Inspect data is not *required* to be tested in general, you
need to test Inspect data that is depended upon by other tools such as
Triage or Feedback.

Reverser has a basic unit test. Run it:

* {C++}

   The unit tests is located in [reverser\_unittests.cc][cpp-part3-unittest].

   ```
   fx test inspect_cpp_codelab_unittests
   ```

* {Rust}

   The unit test is located in [reverser.rs > mod tests][rust-part3-unittest].

   ```
   fx test inspect_rust_codelab_unittests
   ```

* {Dart}

   The unit test is located in [reverser\_test.dart][dart-part3-unittest].

   ```
   fx test inspect_dart_codelab_part_3_unittests
   ```

Note: This runs unit tests for all parts of this codelab.

The unit test ensures that Reverser works properly (and doesn't hang!), but it does
not check that the Inspect output is as expected.

Note: If you are following along from part\_1, you will need to uncomment
some lines in the part_1 unit test and pass default values for the Inspect properties to your
Reverser.

Passing Nodes into constructors is a form of [Dependency
Injection](https://en.wikipedia.org/wiki/Dependency_injection), which
allows you to pass in test versions of dependencies to check their state.

The code to open a Reverser looks like the following:

* {C++}

   ```cpp
   {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_3/reverser_unittests.cc" region_tag="open_reverser" adjust_indentation="auto" %}
   // Alternatively
   binding_set_.AddBinding(std::make_unique<Reverser>(inspect::Node()),
                           ptr.NewRequest());
   ```

* {Rust}

   ```rust
   {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_2/src/reverser.rs" region_tag="open_reverser" adjust_indentation="auto" %}
   ```

* {Dart}

   ```dart
   {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_3/test/reverser_test.dart" region_tag="open_reverser" adjust_indentation="auto" %}
   ```

A default version of the Inspect Node is passed into the Reverser. This
allows the reverser code to run properly in tests, but it does not
support asserting on Inspect output.


* {C++}

   **Exercise**: Change `OpenReverser` to take the dependency for Reverser
   as an argument and use it when constructing Reverser.

   - *Hint*: Create an `inspect::Inspector` in the test function. You can
   get the root using `inspector.GetRoot()`.

   - *Hint*: You will need to create a child on the root to pass in to `OpenReverser`.

* {Rust}

   **Exercise**: Change `open_reverser` to take the dependency for a `ReverserServerFactory`
   as an argument and use it when constructing Reverser.

   - *Hint*: Create a `fuchsia_inspect::Inspector` in the test function. You can
     get the root using `inspector.root()`.

   - *Note*: Do not use `component::inspector()` directly in your tests, this creates a static
     inspector that will be alive in all your tests and can lead to flakes or unexpected behaviors.
     For unit tests, alwas prefer to use a new `fuchsia_inspect::Inspector`

   - *Hint*: You will need to create a child on the root to pass in to `ReverserServerFactory::new`.

* {Dart}

   **Exercise**: Change `openReverser` to take the dependency for an `inspect.Node`
   as an argument and use it when constructing Reverser.

   - *Hint*: Use `inspect.Inspect.forTesting` and `FakeVmoHolder` to create
     an Inspect object without fuchsia dependencies to run your test on host.

   - *Hint*: You will need to create a child on the root to pass in to `openReverser`.


**Follow up**: Create multiple reverser connections and test them independently.

Following this exercise, your unit test will set real values in an
Inspect hierarchy.

Add code to test the output in Inspect:

* {C++}

   ```cpp
   {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_4/reverser_unittests.cc" region_tag="include_testing" adjust_indentation="auto" %}
   {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_4/reverser_unittests.cc" region_tag="get_hierarchy" adjust_indentation="auto" %}
   ```

   Note: If you use the LazyNode or LazyValues features, you will need to
   use inspect::ReadFromInspector and run the returned fit::promise to
   completion. See the solution to this part for an example.

   The snippet above reads the underlying virtual memory object (VMO)
   containing Inspect data and parses it into a readable hierarchy.

   You can now read individual properties and children as follows:

   ```cpp
   {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_4/reverser_unittests.cc" region_tag="assertions" adjust_indentation="auto" %}
   ```

* {Rust}

   ```rust
   {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_4/src/reverser.rs" region_tag="include_testing" adjust_indentation="auto" %}
   {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_4/src/reverser.rs" region_tag="test_inspector" adjust_indentation="auto" %}
   {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_4/src/reverser.rs" region_tag="assert_tree" adjust_indentation="auto" %}
   ```

* {Dart}

   ```dart
   {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_4/test/reverser_test.dart" region_tag="reverser_test" adjust_indentation="auto" %}
   ```

   The `VmoMatcher` is a convenient utility for testing inspect integrations. It allows to assert
   existing properties and children and missing ones, among other features.

The snippets above read a snapshot from the underlying virtual memory object (VMO)
containing Inspect data and parses it into a readable hierarchy.

**Exercise**: Add assertions for the rest of your Inspect data.

This concludes Part 3.

You may commit your changes:

```
git commit -am "solution to part 3"
```


## Part 4: Integration Testing for Inspect

Note: All links and examples in this section refer to "part\_4" code. If
you are following along, you may continue using the part you started with.

[Integration testing](https://en.wikipedia.org/wiki/Integration_testing)
is an important part of the software development workflow for
Fuchsia. Integration tests allow you to observe the behavior of your
actual component when it runs on the system.

### Running integration tests

You can run the integration tests for the codelab as follows:

* {C++}

   ```
   $ fx test inspect_cpp_codelab_integration_tests
   ```

* {Rust}

   ```
   $ fx test inspect_rust_codelab_integration_tests
   ```

* {Dart}

   ```
   $ fx test inspect_dart_codelab_part_4_integration_tests
   ```

Note: This runs integration tests for all parts of this codelab.

### View the code

Look at how the integration test is setup:

1. View the component manifest for the integration test:

   * {C++}

     Find the component manifest (cmx) in [cpp/meta][cpp-part4-integration-meta]

   * {Rust}

     Find the component manifest (cmx) in [rust/meta][rust-part4-integration-meta]

   * {Dart}

     Find the component manifest (cmx) in [dart/part_4/meta][dart-part4-integration-meta]

   ```
   {
       "facets": {
           "fuchsia.test": {
               "injected-services": {
                   "fuchsia.diagnostics.ArchiveAccessor":
                       "fuchsia-pkg://fuchsia.com/archivist#meta/observer.cmx"
               }
           }
       },
       "program": {
           "binary": "test/integration_part_4"
       },
       "sandbox": {
           "services": [
               "fuchsia.logger.LogSink",
               "fuchsia.sys.Loader",
               "fuchsia.sys.Environment"
               ...
           ]
       }
   }
   ```

  The important parts of this file are:

  - *Injected services*:
    The `fuchsia.test` facet includes configuration for tests.
    In this file, the `fuchsia.diagnostics.ArchiveAccessor` service is injected
    and points to a component called `observer.cmx`. The observer collects
    information from all components in your test environment and provides
    a reading interface. You can use this information to look at your
    Inspect output.

  - *Sandbox services*:
    Integration tests need to start other components in the test
    environment and wire them up. For this you need `fuchsia.sys.Loader`
    and `fuchsia.sys.Environment`.

2. Look at the integration test itself. The individual test cases are fairly straightforward:

   * {C++}

      Locate the integration test in [part4/tests/integration_test.cc][cpp-part4-integration].

      ```cpp
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_4/tests/integration_test.cc" region_tag="integration_test" adjust_indentation="auto" %}
      ```

      `StartComponentAndConnect` is responsible for creating a new test
      environment and starting the codelab component inside of it. The
      `include_fizzbuzz_service` option instructs the method to optionally
      include FizzBuzz. This feature tests that your Inspect output is as
      expected in case it fails to connect to FizzBuzz as in Part 2.

   * {Rust}

      Locate the integration test in [part4/tests/integration_test.rs][rust-part4-integration].

      ```rust
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_4/tests/integration_test.rs" region_tag="integration_test" adjust_indentation="auto" %}
      ```

      `IntegrationTest::start` is responsible for creating a new test
      environment and starting the codelab component inside of it. The
      `include_fizzbuzz` option instructs the method to optionally
      launch the FizzBuzz component. This feature tests that your Inspect
      output is as expected in case it fails to connect to FizzBuzz as in Part 2.

   * {Dart}

      Locate the integration test in [part_4/test/integration_test.dart][dart-part4-integration].

      ```dart
      {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_4/test/integration_test.dart" region_tag="integration_test" adjust_indentation="auto" %}
      ```

      `env.create()` is responsible for creating a new test environment.
      `startComponentAndConnect` launches the reverser component and optionally launches the
      FizzBuzz component. This feature tests that the Inspect output is as expected in case it fails
      to connect to FizzBuzz as in Part 2.

3. Add the following method to your test fixture to read from the ArchiveAccessor service:

   * {C++}

     ```cpp
     {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_5/tests/integration_test.cc" region_tag="include_json" adjust_indentation="auto" %}
     {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_5/tests/integration_test.cc" region_tag="get_inspect" adjust_indentation="auto" %}
     ```

   * {Rust}

     ```rust
     {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_5/tests/integration_test.rs" region_tag="include_test_stuff" adjust_indentation="auto" %}
     {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_5/tests/integration_test.rs" region_tag="get_inspect" adjust_indentation="auto" %}
     ```

   * {Dart}

     ```dart
     {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_5/test/integration_test.dart" region_tag="include_test_stuff" adjust_indentation="auto" %}
     {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_5/test/integration_test.dart" region_tag="get_inspect" adjust_indentation="auto" %}
     ```


4. **Exercise**. Use the returned data in your tests and add assertions to the returned data:

   * {C++}

      ```cpp
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_5/tests/integration_test.cc" region_tag="parse_result" adjust_indentation="auto" %}
      ```

      Add assertions on the returned JSON data.

      - *Hint*: It may help to print the JSON output to view the schema.

      - *Hint*: You can read values by path as follows:

      - *Hint*: You can `EXPECT_EQ` by passing in the expected value as a rapidjson::Value:
        `rapidjson::Value("OK")`.

      ```cpp
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/cpp/part_5/tests/integration_test.cc" region_tag="hint_get_value" adjust_indentation="auto" %}
      ```

   * {Rust}

      ```rust
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/diagnostics/examples/inspect/rust/part_5/tests/integration_test.rs" region_tag="result_hierarchy" adjust_indentation="auto" %}
      ```

      Add assertions on the returned `NodeHierarchy`.

      - *Hint*: It may help to print the JSON output to view the schema.

   * {Dart}

      ```dart
      {% includecode gerrit_repo="fuchsia/topaz" gerrit_path="public/dart/fuchsia_inspect/codelab/part_5/test/integration_test.dart" region_tag="result_hierarchy" adjust_indentation="auto" %}
      ```

      Add assertions on the returned Map data.

      - *Hint*: It may help to print the JSON output to view the schema.


Your integration test will now ensure your inspect output is correct.

This concludes Part 4.

You may commit your solution:

```
git commit -am "solution to part 4"
```

## Part 5: Feedback Selectors

This section is under construction.

- TODO: Writing a feedback selector and adding tests to your integration test.

- TODO: Selectors for Feedback and other pipelines

[fidl-fizzbuzz]: /src/diagnostics/examples/inspect/fidl/fizzbuzz.test.fidl
[fidl-reverser]: /src/diagnostics/examples/inspect/fidl/reverser.test.fidl

[inspect-cpp-codelab]: /src/diagnostics/examples/inspect/cpp
[cpp-part1]: /src/diagnostics/examples/inspect/cpp/part_1
[cpp-part1-main]: /src/diagnostics/examples/inspect/cpp/part_1/main.cc
[cpp-part1-reverser-h]: /src/diagnostics/examples/inspect/cpp/part_1/reverser.h
[cpp-part1-reverser-cc]: /src/diagnostics/examples/inspect/cpp/part_1/reverser.cc
[cpp-part1-build]: /src/diagnostics/examples/inspect/cpp/part_1/BUILD.gn
[cpp-client-main]: /src/diagnostics/examples/inspect/cpp/client/main.cc#118
[cpp-part2-meta]: /src/diagnostics/examples/inspect/cpp/part_2/meta/inspect_cpp_codelab_part_2.cmx
[cpp-part3-unittest]: /src/diagnostics/examples/inspect/cpp/part_3/reverser_unittests.cc
[cpp-part4-integration]: /src/diagnostics/examples/inspect/cpp/part_4/tests/integration_test.cc
[cpp-part4-integration-meta]: /src/diagnostics/examples/inspect/cpp/meta/integration_part_4.cmx

[inspect-rust-codelab]: /src/diagnostics/examples/inspect/rust
[rust-part1]: /src/diagnostics/examples/inspect/rust/part_1
[rust-part1-main]: /src/diagnostics/examples/inspect/rust/part_1/src/main.rs
[rust-part1-reverser]: /src/diagnostics/examples/inspect/rust/part_1/src/reverser.rs
[rust-part1-build]: /src/diagnostics/examples/inspect/rust/part_1/BUILD.gn
[rust-client-main]: /src/diagnostics/examples/inspect/rust/client/src/main.rs#41
[rust-part2-meta]: /src/diagnostics/examples/inspect/rust/part_2/meta/inspect_rust_codelab_part_2.cmx
[rust-part3-unittest]: /src/diagnostics/examples/inspect/rust/part_3/src/reverser.rs#99
[rust-part4-integration]: /src/diagnostics/examples/inspect/rust/part_4/tests/integration_test.rs
[rust-part4-integration-meta]: /src/diagnostics/examples/inspect/rust/meta/integration_test_part_4.cmx

[inspect-dart-codelab]: https://fuchsia.googlesource.com/topaz/+/master/public/dart/fuchsia_inspect/codelab
[dart-part1]: https://fuchsia.googlesource.com/topaz/+/master/public/dart/fuchsia_inspect/codelab/part_1
[dart-part1-main]: https://fuchsia.googlesource.com/topaz/+/master/public/dart/fuchsia_inspect/codelab/part_1/lib/main.dart
[dart-part1-reverser]: https://fuchsia.googlesource.com/topaz/+/master/public/dart/fuchsia_inspect/codelab/part_1/lib/src/reverser.dart
[dart-part1-build]: https://fuchsia.googlesource.com/topaz/+/master/public/dart/fuchsia_inspect/codelab/part_1/BUILD.gn
[dart-client-main]: https://fuchsia.googlesource.com/topaz/+/master/public/dart/fuchsia_inspect/codelab/client/lib/main.dart#9
[dart-part2-meta]: https://fuchsia.googlesource.com/topaz/+/master/public/dart/fuchsia_inspect/codelab/part_2/meta/inspect_dart_codelab_part_2.cmx
[dart-part3-unittest]: https://fuchsia.googlesource.com/topaz/+/master/public/dart/fuchsia_inspect/codelab/part_3/test/reverser_test.dart
[dart-part4-integration]: https://fuchsia.googlesource.com/topaz/+/master/public/dart/fuchsia_inspect/codelab/part_4/test/integration_test.dart
[dart-part4-integration-meta]: https://fuchsia.googlesource.com/topaz/+/master/public/dart/fuchsia_inspect/codelab/part_4/meta/inspect_dart_codelab_part_4_integration_tests.cmx
