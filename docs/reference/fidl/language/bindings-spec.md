# FIDL bindings specification

This document is a specification of Fuchsia Interface Definition Language
(**FIDL**) bindings. It is meant to provide guidance and best practices for
bindings authors, and recommend specific approaches for their ergonomic use.

In this document, the following keywords are to be interpreted as described in
[RFC2119][RFC2119]: **MAY**, **MUST**, **MUST NOT**, **OPTIONAL**,
**RECOMMENDED**, **REQUIRED**, **SHALL**, **SHALL NOT**, **SHOULD**, **SHOULD
NOT**.

## Generated code indication

A comment must be placed at the top of machine-generated code to indicate it is
machine generated. For languages with a standard on how to indicate generated
sources (as opposed to human-written code), that standard must be followed.

In [Go][go-generated-code-comment] for instance, generated sources must be
marked with a comment following the pattern

```go
// Code generated by <tool>; DO NOT EDIT.
```

## Scoping

It is RECOMMENDED to namespace machine-generated code to avoid clashing with
user-defined symbols. This can be implement using scoping constructs provided by
the language, like namespaces in C++, modules in Rust, or packages in Go and
Dart. If the generated scope can have a name, it SHOULD be named using
components of the FIDL library name which contains the definitions for the
generated code, which allows each FIDL library to exist in a unique scope. In
cases where scoping is not possible and the namespace is shared, some processing
of the generated names (see [Naming](#naming)) may be necessary.

## Naming {#naming}

In general, the names used in the generated code SHOULD match the names used in
the FIDL definition. Possible exceptions are listed in the following sections.

### Casing

Casing changes SHOULD be made to fit the idiomatic style of the language (e.g.
using snake_case or CamelCase). `fidlc` will ensure that identifier uniqueness
is enforced taking into account potential casing differences (see [FTP 40][ftp040]).

### Reserved keywords and name clashes

The generated code MUST take into account the reserved keywords in the target
language to avoid unexpected when a keyword from the target language is used in
the FIDL definition. An example scheme would be to prefix conflicting names with
an underscore `_` (assuming no keywords begin with an underscore).

The generated code MUST avoid generating code that causes naming conflicts. For
example, in a function whose parameters are generated based on a FIDL
definition, it MUST be impossible for the names of the local variables in the
generated to clash with possible generated names.

## Ordinals

### Method ordinals

Ordinals used for methods are large 64-bit numbers. Bindings SHOULD emit these
ordinals in hexadecimal, i.e. `0x60e700e002995ef8`, not `6982550709377523448`.

### Union, and table ordinals

Ordinals used for `union` and `table` start at 1, and must form a dense space.
Therefore, these numbers are typically small, and bindings SHOULD emit these
ordinals in decimal notation.

## Native types

It is RECOMMENDED that bindings use the most specific and ergonomic native types
where possible when converting built-in FIDL types to native types in the target
language. For example, the Dart bindings use `Int32List` to represent a
`vector<int32>:N` and `array<int32>:N` rather than the more generic `List<int>`.

## Generated types and values

### Constant support

Generated code MUST generate variables containing matching values for each
`const` definition in the corresponding FIDL. These variables SHOULD be marked
as immutable in languages that support this (e.g. `const` in C++, Rust, and Go,
or `final` in Dart).

### Bits support

Bindings MUST provide generated values for each bits member. They MAY also
generate values representing the bits with no flags set, as well as the bits
with every flag set (the "bits mask"). These values SHOULD be scoped to each set
of bits.

It is RECOMMENDED to support the following operators over generated values:

* bitwise and, i.e `&`
* bitwise or, i.e `|`
* bitwise exclusive-or, i.e `^`
* bitwise not, i.e `~`

To provide bitwise operations which always result in valid bits values,
implementations of bitwise not should further mask the resulting value with the
mask of all values. In pseudo code:

```
~value1   means   mask & ~bits_of(value1)
```

This mask value is provided in the [JSON IR][jsonir] for convenience.

Bindings SHOULD NOT support other operators since they could result in invalid
bits value (or risk a non-obvious translation of their meaning), e.g.:

* bitwise shifts, i.e `<<` or `>>`
* bitwise unsigned shift, i.e `>>>`

For cases where the generated code includes a type wrapping the underlying
numeric bits value, it SHOULD be possible to convert between the raw value and
the wrapper type. It is RECOMMENDED for this conversion to be explicit.

### Enum support

Bindings MUST provide generated values for each enum member. These values SHOULD
be scoped to each enum.

For cases where the generated code includes a type wrapping the underlying
numeric enum value, it SHOULD be possible to convert between the raw value and
the wrapper type. It is RECOMMENDED for this conversion to be explicit.

### Struct support

Bindings MUST provide a type for each struct that supports the following
operations:

* Construction with explicit values for each member.
* Reading and writing members.

Bindings MAY support default values for structs. The default values are
specified in the [JSON IR][jsonir].

### Union support

Bindings MUST provide a type for each union that supports the following
operations:

* Construction with an explicit variant set. It is NOT RECOMMENDED for bindings
  to offer construction without a variant. This should be considered only for
  performance reasons or due to limitations of the target language.
* Reading/writing the variant of the union and the data associated with that
  variant.

For languages without union types or union value literals, it is RECOMMENDED to
support factory methods for constructing new unions given a value for one of the
possible variants. For example, in a C like language, this would allow replacing
code like:

```C
my_union_t foo;
foo.set_variant(bar);
do_stuff(foo);
```

with something like:

```C
do_stuff(my_union_with_variant(bar));
```

These factory methods SHOULD be named as "[Type]-with-[Variant]", cased properly
for the target language.

Examples of this exist for the
[HLCPP](https://fuchsia-review.googlesource.com/c/fuchsia/+/309246/) and
[Go](https://fuchsia-review.googlesource.com/c/fuchsia/+/313205/) bindings.

#### Flexible unions

The bindings MUST succeed when decoding a flexible union with an unknown
variant. These unknown unions MAY provide ways for the user to read the
underlying raw bytes and handles of the payload or the unknown ordinal.
Additionally, it is OPTIONAL for the bindings to support re-encoding the raw
bytes and handles when sending a flexible union with an unknown variant.

Generated code for unions MAY allow the user to read the underlying raw ordinal
of the message.

### Table support

Bindings MUST provide a type for each table that supports the following
operations:

* Construction where specifying values for each member is optional.
* Reading and writing each member, including checking whether a given member is
  set. These SHOULD follow the naming scheme: `get_[member]`, `set_[member]`,
  and `has_[member]`, cased properly for the target language.

Bindings MAY support default values for tables. The default values are specified
in the [JSON IR][jsonir].

Bindings MAY provide constructors for tables that only require specifying values
for fields that have a value. For example, in Rust this can be accomplished
using the `::empty()` constructor along with struct update syntax. Supporting
construction this ways allows users to write code that is robust against
addition of new fields to the table.

## Protocol support

### Error types

It is OPTIONAL that bindings provide some form of special support for protocol
methods with an error type matching the idiomatic way errors are handled in the
target language.

For example, languages that provide some form of a "result" type (i.e. a union
type that contains a "success" variant and an "error" variant), such as Rust's
`result::Result`, or `fit::result` in C++ MAY provide automatic conversions to
and from these types when receiving or sending method responses with an error
type.

Languages with exceptions can have the generated protocol method code optionally
raise an exception corresponding to the error type.

In cases where this is not possible, the generated code MAY provide convenience
functions for responding directly with a successful response or error value, or
for receiving an error type response, in order avoid boilerplate user code for
initializing result unions.

## Error handling

Protocols MAY surface transport errors back to the user. Transport errors can be
categorized as errors encountered when converting between the native type and
the wire format data, or as errors from the underlying transport mechanism (for
example, an error obtained from calling `zx_channel_write`). These errors MAY
consist of the error status, as well as any other diagnostics information.

### Attributes

Bindings MUST support the following [attributes][attributes]:

* `[Transitional]`

## Best practices

### Alternative output

It is OPTIONAL for bindings to provide alternative output methods to the FIDL
wire format.

One type of output could be user-friendly debug printing for the generated
types. For example, printing a value of the bits:

```fidl
bits Mode {
  Read = 1;
  Write = 2;
};
```

could print the string `"Mode.Read | Mode.Write"` rather than the raw value
`"0b11"`.

Similar user-friendly printing can be implemented for each of the generated FIDL
types.

Another example of alternative output would be serializing FIDL values to JSON.
Users SHOULD have the option to opt-in or out to this functionality in order to
follow the principle of "only pay for what you use". An example of this is
`dart_fidl_json`, which is implemented using `fidlmerge`.

### Message memory allocation

Bindings MAY provide the option for users to provide their own memory to use
when sending or receiving messages, which allows the user to control memory
allocation.

### Wire format memory layout

Bindings MAY have the in memory layout of the generated FIDL types match the
wire format of the type. Doing this can in theory avoid extra copies, as the
data can be used directly as the transactional message, or vice versa. In
practice, sending a FIDL message may still involve a copying step where the
components of a message are assembled into a contiguous chunk of memory (called
"linearization"). The downside of such an approach is that it makes the bindings
more rigid: changes to the FIDL wire format become more complex to implement.

The [LLCPP bindings][llcpp-tutorial] are the only binding which take this
approach.

### Equality comparison

For aggregate types such as structs, tables, and unions, bindings MAY provide
equality operators that perform a deep comparison on two instances of the same
type. These operators SHOULD NOT be provided for resource types (see FTP-057) as
comparison of handles is not possible. Avoiding exposing equality operators for
resource types prevents source breakages caused by an equality operation
'disappearing' when a handle is added to the type.

### Copying

For aggregate types such as structs, tables, and unions, bindings MAY provide
functionality for copying instances of these types. Copying SHOULD NOT be
provided for resource types (see [FTP-057][ftp057]) as making copies of handles
is not guaranteed to succeed. Avoiding exposing copy operators for resource
types prevents source breakages caused by a copy operation 'disappearing' or
having its signature change when a handle is added to the type.

### Test utilities

It is OPTIONAL for bindings to generate additional code specifically to be used
during testing. For example, the bindings can generate stub implementations of
each protocol so that users only need too verride specific methods that are
going to be exercised in a test.

### Epitaphs

Bindings SHOULD provide support for epitaphs, i.e. generated code that allows
servers to send epitaphs and clients to receive and handle epitaphs.

### Setters and Getters

Bindings MAY provide setters and getters for fields on aggregate types (structs,
unions, and tables). Even in languages where getter/setter methods are
un-idiomatic, using these methods will allow renaming internal field names
without breaking usages of that field.

## Related Documents

* [FTP-024: Mandatory Source Compatibility][ftp024]

<!-- xrefs -->
[jsonir]: /docs/reference/fidl/language/json-ir.md
[ftp024]: /docs/contribute/governance/fidl/ftp/ftp-024.md
[ftp040]: /docs/contribute/governance/fidl/ftp/ftp-040.md
[ftp057]: /docs/contribute/governance/fidl/ftp/ftp-057.md
[RFC2119]: https://tools.ietf.org/html/rfc2119
[go-generated-code-comment]: https://github.com/golang/go/issues/13560#issuecomment-288457920
[attributes]: /docs/reference/fidl/language/attributes.md
[llcpp-tutorial]: /docs/development/languages/fidl/tutorials/tutorial-llcpp.md
