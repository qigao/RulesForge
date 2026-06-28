# RulesForge User Guide

This document is the product guide. For exact language support, use [`dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md). If this guide and code ever disagree, code wins.

RulesForge is a RETE-based rule engine. It supports dynamic schema/data binding and executes supported rule kernels through a JIT-backed runtime.

## 1. Choose The Right API Surface

RulesForge exposes three practical integration layers:

- C API in [`include/rule_forge.h`](/C:/projects/cpp/rulesforge/include/rule_forge.h)
- C++ engine API in `rulesforge/include` plus parser API in `parser/include`
- explicit host callback boundary through the C API

Recommendation:

- new application embedding: start with the C API
- advanced in-process engine control: use the C++ API
- external side effects: register explicit host callbacks

## 2. Runtime Model

The current runtime is built around two primary objects:

- `KnowledgeBase`: compiled rules and host callback registrations
- `StatefulSession`: facts, agenda, queries, tracing, validation mode, runtime metrics

Thread model:

- `KnowledgeBase` may be shared after rule loading and callback registration are complete
- `StatefulSession` is not thread-safe

## 3. Loading Rules

Current rule-loading paths include:

- in-memory RFL source
- one RFL file
- many RFL files with import resolution
- decision table CSV through the C API

Relevant APIs:

- `ruleforge_kb_load_drl()`
- `ruleforge_kb_load_drl_file()`
- `ruleforge_kb_load_drl_files()`
- `ruleforge_kb_load_decision_table_csv()`

## 4. Loading Facts

RulesForge exposes facts to the engine. The public C API also provides schema-aware data binding helpers backed by `TurboScript::DataBind`.

- already constructed fact objects
- JSON with schema
- CSV with schema
- XML with schema
- binary TBE payloads with schema

C API entry points:

- `ruleforge_session_add_fact_json()`
- `ruleforge_session_add_fact_json_schema()`
- `ruleforge_session_add_fact_binary_schema()`
- `ruleforge_session_add_facts_csv_schema()`
- `ruleforge_session_add_facts_xml_schema()`
- field-based fact construction APIs

C++ entry points:

- `session->add_fact(fact)`
- `session->add_data(DataSource::fact(fact))`

Notes:

- The C++ engine runtime remains fact-only.
- Schema-aware C API helpers call `TurboScript::DataBind`, convert bound values into session-owned facts, and insert those facts.
- Schema declarations can be imported in RFL with `import "name.schema"`.

## 5. Queries

RulesForge supports named queries in RFL and runtime query execution after facts are loaded and rules have fired.

C API:

- `ruleforge_session_query()`
- `ruleforge_query_result_get_size()`
- `ruleforge_query_result_get_fact_at_index()`

C++:

- `session->execute_query("QueryName")`

## 6. Host Callbacks

RHS host calls:

- register explicitly with `ruleforge_kb_register_native_function()`
- use them for `invoke(...)` style logic inside rules
- keep them deterministic and treat failures as runtime errors

## 7. What Is Actually Implemented In RFL

The parser and runtime currently cover:

- `package`, `import`, `global`, `declare`, `enum`, `function`, `query`, `rule`
- rule attributes such as `salience`, `agenda-group`, `activation-group`, `no-loop`, `enabled`, `duration`, `timer`, `extends`
- pattern forms including `not`, `exists`, `forall`, `accumulate`, and query calls
- RHS actions including `insert`, `insertLogical`, `update`, `retract`, `halt`, and control flow
- sliding windows such as `over window:time(...)`

For exact syntax and caveats, read [`dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md).

## 8. Production Checklist

- build rules once, reuse the compiled `KnowledgeBase`
- create one `StatefulSession` per thread or request
- choose one data-loading format per integration boundary and keep it boring
- validate rule packs and sample data in CI
- use tracing and metrics only when you need them
- keep custom host callbacks small, deterministic, and safe

## 9. Where To Go Next

- quick path: [`QUICKSTART.md`](/C:/projects/cpp/rulesforge/docs/QUICKSTART.md)
- deployment: [`DEPLOYMENT.md`](/C:/projects/cpp/rulesforge/docs/DEPLOYMENT.md)
- examples: [`examples/README.md`](/C:/projects/cpp/rulesforge/docs/examples/README.md)
- data binding ownership: [`TURBOSCRIPT_DATABIND_PARSER_COMPARISON.md`](/C:/projects/cpp/rulesforge/docs/TURBOSCRIPT_DATABIND_PARSER_COMPARISON.md)
