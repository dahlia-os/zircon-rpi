# Component lifecycle

Component instances progress through four major lifecycle events: create,
start, stop, and destroy.

Component instances may retain isolated persistent state on a storage medium
while they are not running which helps them maintain the
[illusion of continuity][principle-continuity] across restarts.

## Creating a component instance

When a component instance is created, the component frameworks assigns a
unique identity to the instance, adds it to the
[component topology][doc-topology], and makes its capabilities
available for other components to use.

Once created, a component instance can then be started or destroyed.

### Starting a component instance

Starting a component instance loads and runs the component's program
and provides it access to the capabilities that it requires.

[Every component runs for a reason][principle-accountability]. The
component framework only starts a component instance when it has work to do,
such as when another component requests to use its instance's capabilities.

Once started, a component instance continues to run until it is stopped.

### Stopping a component instance

Stopping a component instance terminates the component's program but preserves
its [persistent state][doc-storage] so that it can continue where it left off
when subsequently restarted.

The component framework may stop a component instance for a variety of
reasons, such as:

- When all of its clients have disconnected.
- When its parent is being stopped.
- When its package needs to be updated.
- When there are insufficient resources to keep running the component.
- When other components need resources more urgently.
- When the component is about to be destroyed.
- When the system is shutting down.

A component can implement a [lifecycle handler][doc-lifecycle] to be notified
of its impending termination and other events on a best effort basis. Note
that a component can be terminated involuntarily and without notice in
circumstances such as resource exhaustion, crashes, or power failure.

Components can stop themselves by exiting. The means by which a component exits
depend on the runner that runs the component.

Once stopped, a component instance can then be restarted or destroyed.

### Destroying a component instance

Destroying a component instance permanently deletes all of its associated
state and releases the system resources it consumed.

Once destroyed, a component instance ceases to exist and cannot be restarted.
New instances of the same component can still be created but they will each
have their own identity and state distinct from all prior instances.

[doc-lifecycle]: lifecycle.md
[doc-storage]: capabilities/storage.md
[doc-topology]: topology.md
[principle-accountability]: design_principles.md#accountability
[principle-continuity]: design_principles.md#illusion-of-continuity
