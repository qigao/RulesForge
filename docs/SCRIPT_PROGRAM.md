# Script Programs

A Program binds an immutable RFL/schema snapshot to named Toad exports. RETE
selects the activation, a `call` command computes a value, and ordinary RHS
commands update session facts. The host continues to own external I/O.

For a DLL client tutorial and application extension workflow, see the
[development guide](DEVELOPMENT_GUIDE.md). This reference specifies Program
bindings, admission, ownership and generations.

## Deployment

Use the same exact SDK restoration entry as the rest of RulesForge, selecting
Toad 0.1.3 or a compatible newer released SDK through `TOAD_VERSION` and its
qualified `TOAD_ROOT`. Base, CMeta, Utils and cache versions must match the Toad
manifest. Configuration fails for an older Toad API. Never patch restored SDKs.

Install `rulesforge_toad` with the normal RulesForge install target. A deployment
directory contains its provider DLL/so/dylib, the JSON bundle, RFL, signature
schema and scripts. Referenced paths must resolve inside that directory,
including symlinks; duplicate binding/module names and unknown JSON fields fail.
The native provider path is fixed for a Program's lifetime; SDK/provider upgrades
use a new Program or a process deployment. Script reload updates source bundles.

Toad is an external runtime dependency alongside Base and Utils, with its own
LICENSE/NOTICE/THIRD_PARTY_NOTICES. The restore entry configures native runtime
paths. The application deployment must make the matching DLLs/shared libraries
available when loading RulesForge. RulesForge keeps that selected Script runtime
loaded while its own library is loaded; generation scopes pin the adapter.

For a Windows LLVM-MinGW deployment, the bundle has this shape (the provider
filename differs by platform):

```json
{
  "provider": "librulesforge_toad.dll",
  "rules": "pricing.rfl",
  "signatures": "pricing.schema",
  "mode": "interpreter",
  "modules": [{"name":"pricing","path":"pricing.toad"}],
  "bindings": [{
    "name":"pricing.calculate", "module":"pricing", "function":"calculate",
    "parameters":["double"], "result":"Price"
  }],
  "limits": {
    "source_bytes":4194304, "value_bytes":4194304, "value_nodes":65536,
    "value_depth":64, "steps":100000, "loops":10000, "recursion":100,
    "retained_bytes":134217728, "stack_bytes":1048576, "max_sessions":8
  }
}
```

The existing `mode` field accepts `"interpreter"` or `"jit"`. JIT uses the released
Toad MIR backend through the same module/instance ABI. Each call still gets a
fresh instance; changing mode does not retain script globals between calls.
This selection applies to Toad CALL exports. Core RFL scalar expressions use
their own MIR programs and retain RFL numeric semantics.

Every limit is required and positive. The deployment has at most 64 modules,
256 bindings, 16 parameters per binding, 4 MiB per text file and 16 MiB total
bundle/rules/signature/script text. Materialized signature shapes have a combined 65,536-node limit and a depth
limit of 32, including repeated expansion of named types. Values and copied RHS results
are bounded; the accumulated copied result budget applies to the entire RHS, including
results subsequently leaving a lexical scope. No automatic retry or JIT fallback
occurs. Memory byte accounting is a boundary budget, not a hard process RSS cap.

The signature schema uses DataBind syntax:

```text
schema Pricing [id(1), version(1), byte_order(little)];
message Price { double total; }
```

Parameters and results can be `bool`, `int64`, `uint64` (limited to INT64_MAX),
`double`, `string`, `uuid`, `decimal`, `money`, or a named message. Message fields
can contain supported scalars, messages and lists of supported values.
Nullable fields accept null. Optional/defaulted fields, additional validation
constraints, enums, temporal values, bytes, sets, arbitrary maps, recursive
schemas and dynamic function values are currently rejected at signature loading.
A result record must contain exactly the declared field set with unique keys.
Represent general records using message schemas rather than an untyped map type.

The Toad script uses explicitly admitted named functions:

```text
func calculate(amount) { return map {total: amount + 5}; };
export("calculate");
```

Each invocation starts a new instance. Its globals do not survive the call,
including a call followed by a failed RHS transaction. Arithmetic yielding NUMBER
must declare `double`; no implicit NUMBER-to-int64 or Money conversion occurs.
Money/Decimal are exact transport records, not arbitrary-precision arithmetic.

Programs enable Toad's `TOAD_SCRIPT_MODULE_EXPLICIT_CALLS`. Ambient I/O, imports,
eval, dynamic member invocation, templates, callable rebinding and OOP/generator
forms fail admission. Named script helpers and `list` construction are supported.
Only explicitly configured native module Functions are added to this set. This is a trusted-code
computation boundary, not an untrusted-code sandbox or a hard CPU deadline.
Compilation/initialization and native operations have no promised hard timeout.

### Application-defined native computation

Keeping formulas in scripts remains suitable for script-only bundles. Native
modules add reuse of existing C/C++ calculations without moving fact ownership
out of RETE. Per-call loading would couple execution to mutable paths and break
generation pinning; a generation-owned registry instead uses the existing ACE
scope and Toad lease contracts without a second module ABI or loader framework.

The optional `native_modules` array lists CMeta Plugin DSOs by deployment-relative
path, for example `"native_modules":[{"path":"libmaintenance_math.dll"}]` on
Windows LLVM-MinGW. Omission means no native computation modules; the existing
bundle format remains valid. Paths must stay within the deployment directory,
including symlinks, and duplicate paths are rejected. At most 16 modules are
allowed, subject to Toad's total 256-callback admission limit.

Each candidate generation owns an address-stable CMeta registry and its loaded,
started modules. The provider borrows this registry only through the generation
scope and calls Toad's `toad_script_context_register_plugin()` before compiling
any script module. Each context holds its own leases. Admission and validation
must complete before generation publication; failure releases candidate resources
without changing the currently published generation. Existing sessions pin their
original generation's scripts and native module references. Deploy replacement
DSOs under distinct immutable paths; replacing a file still loaded by the OS is
not a supported reload mechanism.

The owner closes all Toad instances/modules/results/contexts before releasing
the generation scope. Once its sessions are gone, generation drain ends service
borrows, then the native registry stops, quiesces and unloads in reverse order.
No callbacks, queues or payloads escape a synchronous call. Multiple session
owners may invoke the same native DSO concurrently, so these stateless functions
must support concurrent calls. Native code must not retain borrowed arguments,
throw across the C ABI or perform undeclared external side effects. It is trusted
host code: RulesForge cannot enforce purity or roll back its native side effects.

The scalar boundary and conversion rules are those of Toad 0.1.3. Only passive
scalar Function manifests are admitted; interfaces/aggregates and managed module
lifecycles are rejected. Failed native calls propagate as script/RHS failures,
with existing fact rollback. There is no automatic retry, replacement loader or
alternate implementation. Existing script-only bundles remain the migration and
rollback path; public RulesForge C entry points are unchanged.

## Rules and ownership

```rfl
import schema "order.schema"
rule "Price order"
when
    $order: Order(done == false)
then
    call "pricing.calculate"($order.amount) into $price
    update $order { total = $price.total, done = true }
end
```

`order.schema` declares the input/output Order fields. Binding names are static
quoted strings resolved during Program compilation. Arguments are typed field
references, literals or earlier result values; whole Fact/Token pointers and
implicit fact IDs are rejected. `into` introduces a read-only value visible
after that call in the current block. It cannot shadow another binding or be
used as an UPDATE/RETRACT target. Results in branches or loops do not escape.
Ordinary KB loading rejects CALL because no Program bindings are present.

The adapter borrows arguments only until invocation returns and copies validated
results before resetting the Toad result/instance. Facts remain session-owned.
No JSON round trip occurs on the computation path. Script errors, type failures
and quotas trigger the existing RHS rollback. Previously committed rules are not
rolled back; rollback failure leaves the session inconsistent.

## C entry points

The complete signatures, statuses and ownership rules are in
[`rules_forge.h`](../rulesforge/include/rules_forge.h).
The runnable [`script_program_demo`](../examples/script_program/script_program_demo.cpp)
shows fact insertion, execution and cleanup through these entry points.

```c
ruleforge_program_t program = NULL;
ruleforge_stateful_session_t session = NULL;
ruleforge_status_t status = ruleforge_program_create("deployment/bundle.json", &program);
if (status == RULES_FORGE_OK) {
    status = ruleforge_program_create_session(program, &session);
    if (status == RULES_FORGE_OK) {
        /* Insert facts, fire rules and inspect results using existing APIs. */
        status = ruleforge_session_destroy(session);
        /* A failed destroy preserves ownership; handle it before closing Program. */
    }
    /* close withdraws admission; destroy requires all sessions to be released. */
}
```

Create/use/destroy a Program session and its derived handles on the same thread.
Separate sessions can execute on separate owner threads. Control operations must
be caller-serialized; session creation can overlap reload. Destroy must exclude
all concurrent Program API calls. Same-session execution reentry is rejected.

`program_reload` constructs and validates a complete candidate before ACE
publication. New sessions acquire the new generation; existing sessions keep
their original rules, schema and scripts. Two attached generations are allowed;
a third update returns RESOURCE_LIMIT while both are pinned. Failed candidates
leave the published generation intact. Release old sessions before retrying an
explicit update; no forced migration or eviction occurs.

Close is permanent and does not terminate existing sessions. After destroying
sessions, call `ruleforge_program_close`, then `ruleforge_program_destroy`.
Failed destroy retains the Program handle. Drain releases dependent objects
before Plugin leases and unload. Continuous Program sessions and external
side-effect callbacks are not exposed: replay/delivery contracts require separate
qualification. Emit request facts and let the host perform external actions;
in-memory facts do not constitute a durable outbox.

## Runnable Scenarios

The [scenario guide](SCENARIOS.md) links seven independent domain examples. Each
contains schema-bound mock responses, RFL policy, a pure Toad computation module,
an executable, generated interpreter/JIT bundles and formal tests. They share
only the C API host adapter and mock persistence/delivery protocol.

Every business suite includes an explicit JIT case using the released runtime.
That qualification covers the asserted computations, not every possible script
or every test input in both modes. These examples create stateful Program sessions;
the cold-chain length window does not expose Continuous Program sessions.
The [maintenance example](../examples/maintenance/README.md) separately shows an
admitted CMeta native module and generation-pinned computation.
