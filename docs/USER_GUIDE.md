# RulesForge User Guide

This document is the product guide. For exact language support, use [`dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md). If this guide and code ever disagree, code wins.

## 1. Choose The Right API Surface

RulesForge exposes three practical integration layers:

- C API in [`include/rule_forge.h`](/C:/projects/cpp/rulesforge/include/rule_forge.h)
- C++ engine API in `rulesforge/include` plus parser API in `parser/include`
- plugin ABI in [`include/rule_forge_plugin.h`](/C:/projects/cpp/rulesforge/include/rule_forge_plugin.h)

Recommendation:

- new application embedding: start with the C API
- advanced in-process engine control: use the C++ API
- external reusable source/sink integrations: use the plugin ABI

## 2. Runtime Model

The current code is built around two primary objects:

- `KnowledgeBase`: compiled rules, native-function registrations, codec registry
- `StatefulSession`: facts, agenda, queries, tracing, validation mode, runtime metrics

Thread model:

- `KnowledgeBase` may be shared after rule loading and native registration are complete
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

The engine currently supports three practical fact ingestion formats:

- JSON
- CSV
- binary payloads backed by codec declarations/imports

C API entry points:

- `ruleforge_session_add_fact_json()`
- `ruleforge_session_add_facts_csv()`
- `ruleforge_session_add_fact_binary()`

C++ entry points:

- `session->add_data(DataSource::json(...))`
- `session->add_data(DataSource::csv(...))`
- `session->add_data(DataSource::binary(...))`

Notes:

- JSON loading can apply JMESPath selection in the C++ `DataSource::json(content, expr)` path
- CSV in the C++ path currently reads from a file path
- binary loading depends on declared/imported codecs being available in the knowledge base

## 5. Queries

RulesForge supports named queries in RFL and runtime query execution after facts are loaded and rules have fired.

C API:

- `ruleforge_session_query()`
- `ruleforge_query_result_get_size()`
- `ruleforge_query_result_get_fact_at_index()`

C++:

- `session->execute_query("QueryName")`

## 6. Native Functions And Plugins

There are two separate extension paths. Do not confuse them.

Native RHS functions:

- register directly with `ruleforge_kb_register_native_function()`
- or load a DLL/so function table with `ruleforge_kb_load_native_function_table()`
- used for `invoke(...)` style logic inside rules

Source/sink plugins:

- defined by [`include/rule_forge_plugin.h`](/C:/projects/cpp/rulesforge/include/rule_forge_plugin.h)
- used by plugin-based ingestion/routing components
- examples live under [`plugins/examples`](/C:/projects/cpp/rulesforge/plugins/examples) and plugin docs under [`plugins/README.md`](/C:/projects/cpp/rulesforge/plugins/README.md)

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
- keep custom native functions small, deterministic, and safe

## 9. Where To Go Next

- quick path: [`QUICKSTART.md`](/C:/projects/cpp/rulesforge/docs/QUICKSTART.md)
- deployment: [`DEPLOYMENT.md`](/C:/projects/cpp/rulesforge/docs/DEPLOYMENT.md)
- examples: [`examples/README.md`](/C:/projects/cpp/rulesforge/docs/examples/README.md)
- plugin integration: [`plugins/README.md`](/C:/projects/cpp/rulesforge/plugins/README.md)
