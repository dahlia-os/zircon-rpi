# Event capabilities {#event-capabilities}

Event capabilities allow components receive or offer events under the scope of a
particular realm.

Components that wish to listen for events should also have at least one
of these protocols routed to them:

- [`fuchsia.sys2.EventSource`][event-source]: allows the component to listen for events
  asynchronously. Component manager won't wait for the component to handle
  the event.
- [`fuchsia.sys2.BlockingEventSource`][blocking-event-source]: allows the component to
  listen for events synchronously. Component manager will wait for the component to
  handle the event. This is used for [black box tests][blackbox-tests].

At the moment, events can only originate from the framework itself and
are limited to lifecycle events. Refer to [`fuchsia.sys2.EventType`][event-type]
for the complete list of supported events and their explanations.

## Event filters {#event-filters}

Most event declarations consist of only the event name. However, some of them
may contain filters. Event filters support filtering events based on additional
parameters defined in a key-value mapping.

These filters can be routed as subsets. For example, let's say component A offers an
event `foo` with filters `x: [/a, /b, /c]`. A component B might route this event using
only a subset of filters `x: [/b, /c]` and a component C could use this event using a
single filter `x: /b`.

For example, the `capability_ready` event defines a filter for the `path`. The `path`
is one or more paths exposed to framework that the component is interested in offering
or listening to.

## Offering events {#offering-events}

Events may be [offered][routing-terminology] to children. For example, a
component wishing to expose `started`, `stopped` and `capability_ready` to a child of
itself could do the following:

```
{
    offer: [
        {
            event: [
                "started",
                "stopped",
            ],
            from: "realm",
            to: [ "#child" ],
        },
        {
            event: "capability_ready",
            from: "realm",
            as: "foo_bar_ready",
            filter: { path: [ "/foo", "/bar"] },
            to: [ "#child" ],
        }
    ]
}
```

Events can be offered from two sources:

- `realm`: A component that was offered an event (`started` for example) can offer this
same event from its containing realm. The scope of the offered event will be the same
scope of the `started` event that the component was offered.

- `framework`: A component can also offer an event that its parent didn't offer to it. The
scope of this event will be the component's realm itself and all its descendants.


## Using events {#using-events}

A component that wants to receive events declares in its manifest the events it is
interested in and the `EventSource` protocol. Both the protocol and the events should
be offered to the component.

Events can come from two sources:

- `framework`: events used from framework are scoped to the component using
  them. For example, given a topology `A -> B -> C` where `A` is the parent of `B`
  and `B` of `C`. Suppose that `B` uses `started` from `framework`. `B` will be
  able to see when `C` starts but it won't be able to see when a sibling of
  itself (another child of `A`) starts.

- `realm`: events used from the realm have been offered by the parent and are
  scoped to the parent's scope.
  For example, given a topology `A -> B -> C` where `A` is the parent of `B`
  and `B` of `C`. Suppose that `A` offers `started` to `B` and `B` uses `started`
  from `realm`. `B` will be able to see when `C` starts but it will also be able
  to see when a sibling of itself (a child of `A`) starts.

For example, a component that was offered the events from the [example above](#offering-events)
could use some of them as follows:

```
{
    use: [
        {
            protocol: "/svc/fuchsia.sys2.EventSource",
            from: "realm",
        },
        {
            event: "started",
            from: "realm",
        },
        {
            event: ["stopped", "destroyed"],
            from: "framework"
        }
        {
            event: "foo_bar_ready",
            from: "realm",
            filter: { path: "/foo" },
        }
    ]
}
```

Above, the component was offered `started`, `stopped` and `foo_bar_ready`. In
this example, the component uses the `started` it was offered and `foo_bar_ready`
but only for `/foo` capabilities, not `/bar`. Also, the component decided to not use the
`stopped` event it was offered. Instead the component used the event from `framework`, which means
that it will only see `stopped` and `destroyed` events for components in its own realm.

[blackbox-tests]: ../black_box_testing.md
[blocking-event-source]: https://fuchsia.dev/reference/fidl/fuchsia.sys2#BlockingEventSource
[event-source]: https://fuchsia.dev/reference/fidl/fuchsia.sys2#EventSource
[event-type]: https://fuchsia.dev/reference/fidl/fuchsia.sys2#EventType
[routing-terminology]: ../component_manifests.md#routing-terminology
