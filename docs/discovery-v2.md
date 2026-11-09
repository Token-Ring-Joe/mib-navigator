# Discovery v2 and remaining blocking Query cleanup

## Table-instance enumeration

Table-instance enumeration now uses `SnmpInstanceOperation`,
`ISnmpTransport`, and `SnmpInstanceAsyncRunner`. The operation captures a
`SnmpRequestContext`, walks with ordered GET-NEXT requests, applies the existing
subtree and safe suffix helpers, and returns instance labels as values. It has
no UI dependency. Agent presents the selection dialog and issues the selected
GET, GET-NEXT, or GET-BULK only after the worker result returns.

The MIB selection compatibility call uses the same worker operation with a
nested Qt event loop. Network I/O therefore remains off the UI thread while
the existing call/return API and modal selection behavior are preserved.

## Discovery audit and scan plan

Previously, `DiscoveryThread::run()` read Discovery widgets, preferences,
Agent Profile services, and mutable profile data directly from its worker
thread. Its SNMP session was constructed on the UI thread and used on the
worker, and cancellation wrote a plain boolean through a shared raw pointer.
Adding discovered profiles later also reread the currently selected template
and destination.

`DiscoveryScanPlan` now captures the template record and stable ID,
destination folder ID, wait time, enabled transports, ordered address/protocol
probe batches, effective reusable/inline communities, and SNMPv3 request
identity before execution starts. `DiscoveryOperation` iterates this value
plan through an injectable probe executor and returns structured completion
and candidate values. The scripted executor permits deterministic socket-free
tests.

The production path retains SnmpB's specialized raw broadcast/range probe
transport so packet and scan semantics do not change. It is adapted through
the operation executor boundary. The SNMP session is now created, used, and
destroyed inside the Discovery execution thread. The thread no longer reads
widgets, preferences, repositories, or services.

## Cancellation, results, and profile creation

Cancellation is an atomic cooperative token checked while sending addresses,
while waiting for replies, and between probe batches. It stops scheduling new
batches and permits current blocking/select work to return normally. No forced
thread termination is used. Destruction requests cancellation and waits for
orderly completion.

Candidate presentation remains on the UI thread. Each displayed candidate is
tagged with the captured template and destination IDs. Profile creation still
uses `AgentProfileService::createFromTemplate`, copies the reusable community
binding, and requests placement through `DeviceTreePlacementService`. A failed
or stale destination placement leaves the independently created profile in
Unfiled; it never deletes the profile.

Ordinary Query callback operations remain unchanged. The disabled Qwt graph
helper is the only remaining synchronous SNMP helper. A future milestone may
replace Discovery's QThread subclass with a composed runner/worker pair, but
the active thread now has deterministic ownership and no UI/service access.
