# IDA MCP workflow

Use the connected ida-pro-mcp tool schemas as the API authority; client-side prefixes may differ. The current API uses `idb_list` / `idb_open` and an explicit `database` argument for analysis tools. Keep analysis read-only unless the user asks to annotate or change the IDB.

## Start and route

1. Call `idb_list({})`. Its `sessions` includes both adopted sessions and discovered GUI/worker instances. A discovered entry may have `session_id: ""` and `adopted: false`; `is_active: true` does not make it a routable session.
2. Match the requested sample by full path and module. Reuse an existing non-empty session ID only after identity verification. To adopt a discovered GUI, call `idb_open` with its exact reported `input_path`, `mode: "prefer_gui"`, `run_auto_analysis: false`, `build_caches: false`, and `init_hexrays: false`. The reported path can be an `.i64` database; do not substitute a similarly named DLL from another directory. Check `success`, `error`, and the returned `session.session_id` before continuing.
3. Set `database` to that returned session ID for every subsequent analysis call. It is an opaque routing token, not a file path, port, module name, or label. Do not invent IDs or call removed global-selection tools.
4. Call `server_health({database: session_id})` to verify input/IDB path, module, image base, and readiness. Use `survey_binary({database: session_id, detail_level: "minimal"})` as the first binary-analysis query, especially for large DWM databases. Record architecture, image base/size, function count, and hashes; verify PE version and paired PDB separately using the repository audit tools.
5. Before each small query batch, confirm that its `database` still maps to the intended sample. Recheck health when switching samples, reconnecting, reopening, or observing a stale-session error. Treat an unexpected path, module, image base, or hash as a routing failure rather than binary evidence. An IDB path and its original PE path can differ; reconcile them with survey metadata and exact PE identity.
6. Keep multi-sample audits serialized under the repository policy. Explicit session routing replaces the old global-selection protocol; it does not authorize concurrent work on user databases. On a stale session, rediscover and verify the replacement before retrying; never fall back to an empty `database` or the active GUI.

### Opening and cleanup

`idb_open` defaults to `prefer_headless`, which ignores a running GUI when choosing a new session. Use `prefer_gui` explicitly when the requested sample is already open in IDA. It can spawn a worker if no matching GUI remains, so inspect the resulting session/backend instead of assuming adoption succeeded. `force_gui` can launch a GUI, while `force_headless` only uses workers. Do not launch another IDA process just to probe connectivity.

Opening a new sample can create working database files and automatic analysis changes the database. For an audit of an existing IDB, disable the three open/warmup flags above and use available analysis; if the database is not ready, report that limitation instead of silently reanalyzing it. When the task needs initial analysis of a raw PE, use a disposable working copy outside the source tree and preserve the original sample and its identity. Worker idle TTL is a resource setting, not an identity guarantee.

Record which sessions predated the task and which workers it created. Leave pre-existing sessions open. To release a task-created session, use `idb_close({database: session_id, save: false})` explicitly: owned workers terminate, whereas adopted instances detach without being killed. Never call `idb_save` or close with `save: true` during an audit-only task. Unsaved operation is not permission to annotate or patch. Never delete loose IDA working files while the database is open.

Do not hard-code ports in repository files or reports. Ports are session routing details, not sample identity.

## Read-only capability map

All analysis calls below require the verified `database` argument. Read their current schemas before constructing filters; similarly named tools do not necessarily accept the same query shape.

| Need | Tools and limits |
| --- | --- |
| Function/name discovery | `lookup_funcs` with `queries` (string or array); `func_query` / `entity_query` for narrow filtered searches |
| Decisive function | `decompile` takes one `addr` string; use `analyze_batch` with a `queries` array and selected sections for multiple functions |
| Machine instructions | `disasm` with `addr`, `offset`, `max_instructions`; follow `cursor.next` until complete when all paths matter |
| Callers and references | `xref_query` with `direction: "to"`, `xref_type: "code"`, and pagination inside `queries`; `xrefs_to`, `callees`, `callgraph`, `basic_blocks` for supporting paths |
| Data and anchors | `get_bytes`, `get_int`, `get_string`, `find_bytes`, `find`, `find_regex`; signatures and `trace_data_flow` are candidate discovery, not semantic proof |
| Existing types | `type_query`, `type_inspect`, `read_struct`, `stack_frame`; inferred/decompiler types still require ABI evidence |

Inspect per-item errors and truncation flags, not just transport success. `analyze_batch` summaries cap callers, xrefs, blocks, and instructions; use paginated targeted queries to establish exhaustive coverage for a hook audit. A capped call graph cannot prove that every caller was inspected.

Avoid all IDB mutations during an audit-only request, including rename/comment/bookmark operations, `diff_before_after`, type application or inference, `make_data`, code/function definition, patches, undefinition, and save. Tool convenience or a before/after preview does not make an operation read-only.

### Python fallback

Prefer typed read tools. When a missing capability requires IDAPython, pass the same verified `database` to `py_eval` and inspect both `stderr` and the returned result. Locals persist across calls by default; use `new_locals: true` for self-contained audit snippets, explicitly import dependencies, and avoid carrying addresses or sample identity from earlier queries. Resetting locals does not undo IDB changes. Globals are rebuilt on each call, so do not rely on persistent locals being visible inside nested function scopes.

For a larger reviewed read-only script, `py_exec_file` accepts an absolute `file_path` visible to the IDA host and executes with one shared globals namespace. Review the script for database/file mutations before execution. Neither Python tool is a sandbox or an exception to the audit-only boundary.

## Query sequence

1. Gate the class or capability with exact and wildcard symbol searches.
2. Use byte patterns, historical instructions, and register choices only to discover candidates; compiler inlining and register allocation make them unsuitable as final proof.
3. Decompile the smallest decisive semantic function first.
4. Inspect disassembly when pseudocode hides units, adjusted `this`, bit fields, or an indirect call.
5. Follow xrefs to one independent confirmation path.
6. Compare the same semantic functions across samples; never compare raw addresses.

## Audit optimized hook contracts

When the selected item is an inline hook, extend the normal query sequence beyond the callee prototype:

1. List all code xrefs to the hooked address. Follow direct call wrappers and tail-jump thunks outward until reaching semantic callers.
2. At each call site, inspect disassembly on each feasible successor path. Look for volatile GPR, XMM, or EFLAGS values that remain live after the call, including reuse as arguments to another function.
3. Inspect the hooked callee and every wrapper to confirm the candidate value is preserved on the relevant path. Do not assume the public ABI's volatile classification describes the optimizer's private clobber model.
4. Inspect the eventual consumer. A call-site register assignment is not evidence when the next callee ignores or overwrites that argument.
5. Compare an exact older or newer binary when available to determine whether the dependency is revision-specific. Re-run PE/PDB pairing for every comparison sample.

Automated data-flow scans are discovery aids. Manually reject infeasible CFG merges, algebraically value-independent read/write idioms such as `sbb reg,reg`, unused call arguments, and unconsumed upper XMM lanes. Also inspect condition-code readers (`jcc`, `cmovcc`, `setcc`, `adc`, and `sbb`) before the next flag-defining instruction. Report unresolved indirect-call boundaries as unverified rather than assuming preservation.

If the dependency is verified, capture the complete call chain, decisive instructions, preserved state, exact PE/PDB identity, affected version interval, and why ordinary dispatcher code can clobber it. Keep this evidence separate from the declared Symbol signature; the implementation remedy is normally a narrowly scoped custom physical dispatcher, not a fabricated ABI variant.

If a batch request is rejected or too large, split it into smaller read-only requests. A missing exact symbol is a cue to search semantic anchors, callers, strings, or constructor patterns—not proof of removal.

## Sample-label caveat

The specimens examined while designing this skill illustrated why identity matters. Samples labeled `25H2` and `26H1` retained several uDWM entry points while object fields moved; dwmcore lost some legacy-MIL paths while retaining other core classes. These observations are workflow examples only. Do not reuse their values without verifying PE/PDB identity and re-reading the active binary.
