# SNMP operations v2

## Execution audit

Agent request selection is resolved at the UI boundary into an immutable
`SnmpRequestConfig`, which is captured by `SnmpRequestContext`. Ordinary GET,
GET-NEXT, GET-BULK, SET, multiple-varbind requests, and WALK already use the
SNMP++ callback API. SNMP++ completion is dispatched by Agent's short UI timer;
their response formatting remains in `Agent`.

The former `TableViewFrom` path was different: it performed blocking
GET-NEXT/GET calls on the UI thread while combining transport, table traversal,
libsmi lookup, HTML construction, and widget access. `SelectTableInstance` and
the disabled-Qwt graph value helper also retain synchronous calls. Discovery
owns a separate threaded scanning implementation and was not changed.

## Transport and result boundary

`ISnmpTransport` accepts value-based requests for GET, GET-NEXT, GET-BULK, and
SET and returns a `SnmpTransportResult`. Results distinguish success, timeout,
SNMP error, transport failure, cancellation, and normal table completion.
They carry response PDUs and numeric statuses but no UI objects, repositories,
or profile selectors.

`SnmpPlusTransport` is the production adapter. It owns a worker-local SNMP++
session and constructs targets from its captured request configuration.
`ScriptedSnmpTransport` provides deterministic ordered responses and records
requests for tests; it opens no socket.

## Table operation and threading

The UI thread validates the libsmi table/row and converts it to a
`SnmpTablePlan` containing only the row OID and column name/OID pairs.
`SnmpTableOperation` owns a copy of the request context and performs the
existing GET-NEXT row enumeration followed by ordered GET requests for every
column. Existing subtree, response-shape, suffix extraction, and column safety
helpers remain authoritative.

`SnmpTableAsyncRunner` owns one QThread and one worker for a table operation.
The worker owns its transport and never reads MainUI, QWidget, profile, or
credential state. Structured results cross back to the UI thread, where Agent
performs the existing libsmi value rendering and HTML table presentation.
Only one table operation can run at a time.

Cancellation uses a shared atomic token checked between transport calls. It
does not terminate threads or mutate the request context. SNMP++ cannot safely
interrupt an individual blocking request, so cancellation becomes effective
after the current request returns. Runner destruction requests cancellation
and waits for orderly worker completion, preventing orphan threads.

## Compatibility and remaining work

Target version, endpoint, retry, timeout, community/security identity,
context, and bulk values come from the captured configuration. The table
request order and valid traversal algorithm are unchanged. No profile,
credential, preference, or libsmi persistence format changed.

The ordinary callback operations remain on their established SNMP++ path
because forcing them through a worker would add no immediate safety benefit.
`SelectTableInstance` is still synchronous and should be migrated to a
structured instance-enumeration operation later. Discovery could reuse the
result classifications and transport request vocabulary, but its address-range
iteration, thread ownership, shared message state, and cancellation lifecycle
need a separate modernization milestone.
