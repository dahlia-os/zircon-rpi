# Tutorials

The tutorials in this section gradually walk you through how to use FIDL
and run code on Fuchsia. There are no prerequisites other than completing the
[Getting Started][getting-started] section and being comfortable writing code in
your chosen language, but the tutorials build on eachother. The progression of
tutorials is as follows:

1. [Compiling FIDL][compiling-fidl], which is a basic introduction to writing
   and building FIDL files.
2. Each binding has a "getting started" section which introduces you to the
   basics of FIDL and contains an ordered sequence of tutorials covering:
    1. Compiling FIDL into generated bindings in your language of choice and
       using the bindings in a project.
    2. Implementing a server for a FIDL protocol.
    3. Implementing a client for a FIDL protocol.
    4. Running the client and server together, on Fuchsia.
3. Besides the getting started section, each binding section has an assorted set
   of tutorials based on the specific features of each binding. These do not
   have any pre-specified order.

Each tutorial is accompanied by example code in the [FIDL examples][src]
directory. Feel free to follow along by reading the code, or by deleting the
example code and rewriting it yourself based on the tutorials.

If you're using C++ and wondering which tutorial to follow, take a look
at the [HLCPP and LLCPP comparison doc][c-family].

  * [High Level C++ (HLCPP) Tutorial][hlcpp]

<!-- xrefs -->
[fidl-concepts]: /docs/concepts/fidl/overview.md
[compiling-fidl]: /docs/development/languages/fidl/tutorials/fidl.md
[hlcpp]: hlcpp/README.md
<!-- [llcpp]: /docs/development/languages/fidl/tutorials/tutorial-llcpp.md
[rust]: /docs/development/languages/fidl/tutorials/tutorial-rust.md
[dart]: /docs/development/languages/fidl/tutorials/tutorial-dart.md
[go]: /docs/development/languages/fidl/tutorials/tutorial-go.md
[c]: /docs/development/languages/fidl/tutorials/tutorial-c.md -->
[c-family]: /docs/development/languages/fidl/guides/c-family-comparison.md
