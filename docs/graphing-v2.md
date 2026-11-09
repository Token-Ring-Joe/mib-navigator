# Graphing v2 architecture

The disabled graph code persisted a list of graphs in `graphs.conf`. Each graph
stored a name and polling interval; numbered child groups stored up to ten
series with a display name, Agent Profile display name, protocol, pen color,
width/style, and an OID string. The Qwt prototype combined those records with
widgets, mutable `AgentProfile` lookup, synchronous polling, a timer, and Qwt
curves. Its intended history was 30 raw numeric samples. Rate, delta, counter
wrap/reset, units, and durable telemetry were not implemented.

Graphing v2 separates this into value types (`GraphDefinition`,
`GraphSeriesDefinition`, `GraphSample`, and bounded `GraphSeriesState`), a
`GraphRepository`, `GraphService`, numeric conversion, synchronous sampling
operation, and a cooperative `GraphAsyncRunner`. Definitions use stable graph,
series, and Agent Profile IDs plus canonical numeric OIDs. No graph value owns a
widget, credential store, SNMP session, `SmiNode`, or Qwt object.

The repository reads the legacy graphs array and numbered series groups. A
legacy Agent Profile name is resolved only when exactly one profile matches.
Missing or ambiguous references remain loaded and diagnosable. Loading never
rewrites the file; a service mutation writes schema version 2 and stable IDs,
while retaining the legacy name/style fields for compatibility. `agents.conf`
and credential persistence are untouched.

Each sampling plan captures a `SnmpRequestContext`. One GET is issued per
legacy series through `ISnmpTransport`; results cross the asynchronous boundary
as value-based samples. INTEGER, Gauge32, Counter32, Counter64, and TimeTicks
are plotted as raw numbers. Exceptions, nonnumeric values, timeout, SNMP error,
transport failure, missing values, and cancellation are explicit statuses.
Cancellation is cooperative: an in-flight blocking transport call may finish.
History defaults to and is bounded at 30 samples, evicting the oldest first.

Symbolic labels are display-only and may be resolved transiently from the MIB
service; the numeric OID remains authoritative, so MIB reload/unload cannot
invalidate a definition. Profile renames are harmless because profile IDs are
authoritative. Deleted profiles leave unresolved definitions. Credential
changes affect later request captures, never a request already in progress.

The approved official Qwt 6.3.0 distribution is vendored under
`third_party/qwt-6.3.0`. Its upstream `COPYING`, `README`, copyright notices,
and qmake metadata are retained unchanged. A project CMake target compiles only
the base and 2-D plot implementation needed for curves, axes, grids, dates, and
legends; examples, tests, designer plugins, polar plots, OpenGL canvases, and
specialty widgets are not built. SnmpB identifies its use of Qwt in the About
dialog, as requested by the included Qwt License 1.0.

`GraphPlotPresenter` is the only application layer that includes Qwt. It maps
stable series IDs to curves, renders sample timestamps on a UTC date axis,
maps legacy pen settings with safe fallbacks, and uses NaN gaps for invalid
samples rather than plotting false zeroes. Qwt owns no profiles, credentials,
SNMP transports, definitions, persistence, history, or MIB state.
The generic numeric Y axis uses 12 significant digits so nearby large raw SNMP
values retain distinguishable tick labels without changing sample values or
introducing type-specific units.

`GraphManager` adapts the existing Graph tab to `GraphService`, presents a
separate live plot page, and provides graph/series editing plus Start, Stop,
and Clear controls. Every polling cycle resolves each stable profile ID,
captures a fresh request configuration, and starts only when the preceding
cycle has completed. The single-shot scheduling pattern prevents overlapping
or queued cycles. Stop cancels cooperatively and prevents the next timer.
Series with unresolved profiles are retained and skipped with non-modal status.
Labels are resolved transiently through libsmi with module-qualified symbolic
names and numeric fallback; definitions retain only numeric OIDs.

Numeric conversion uses SNMP++ typed accessors rather than printable display
strings. This is essential for TimeTicks, whose printable representation is a
human-readable duration rather than a locale-independent number. INTEGER,
Gauge32, Counter32, Counter64, and TimeTicks retain their raw numeric values.
Per-series status distinguishes timeout, SNMP status, transport failure,
unsupported syntax, missing value, and cancellation without exposing secrets.
One failed series does not discard successful samples from another series.

The legacy `qwt/` 6.1.2 tree remains solely as historical source reference and
is not part of the active CMake dependency graph. The old `graph.cpp` and
`graph.h` implementation are likewise not compiled.

On Windows, create a runnable Release installation with Qt's deployment helper:

```powershell
cmake --install build-graph-release --config Release --prefix "$PWD\build-graph-release\install"
```

The generated install script discovers and stages the Qt runtime DLLs and
plugins required by `mib-navigator.exe`; no developer Qt path or fixed DLL list is
encoded in the project. Qwt is linked statically and therefore requires no
separate Qwt runtime DLL. Bundled MIBs, PIBs, and upstream Qwt license material
are installed by the same command.

Ordinary Query operations still use the established SNMP++ callback path.
Their captured request-context work already removes the most serious mutable
selection risk. Moving presentation/result construction behind structured
results would improve isolated testing, but is not required for Graphing v2 and
should be a separate behavior-preserving workstream.
