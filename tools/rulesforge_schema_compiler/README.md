# rulesforge_compiler

Extract declaration and enum types from a full RulesForge `.rfl` source file,
validate that the extracted object model is accepted by `data_bind`, and render
external code files.

## Usage

Render built-in outputs:

```bash
rulesforge_compiler rules.rfl --lang c  -o types.h
rulesforge_compiler rules.rfl --lang cpp -o types.hpp
rulesforge_compiler rules.rfl --lang go -o types.go
rulesforge_compiler rules.rfl --lang ts -o types.ts
rulesforge_compiler rules.rfl --lang py -o types.py
```

Render custom output with a Mustache template:

```bash
rulesforge_compiler rules.rfl -t sdk.mustache -o sdk.txt
```

Skip codec validation:

```bash
rulesforge_compiler rules.rfl -o declarations.rfl --skip-validate
```

Validate one format only:

```bash
rulesforge_compiler rules.rfl --json-only
rulesforge_compiler rules.rfl --binary-only
```

## Scope

This tool does three things only:

- parse full RulesForge `.rfl`
- keep `package`, `enum`, `declare`, and declaration annotations
- fail early if `data_bind` cannot build JSON or binary codecs from those declarations
- try to narrow validation failure to the offending declaration closure
- render built-in `C / C++ / Go / TypeScript / Python` outputs
- optionally render custom external output through a Mustache template

It does not execute rules, generate DLL projects, or emit MIR/C code.

## Template Model

Available top-level sections:

- `package`
- `enums`
- `declarations`

Enum fields:

- `name`
- `underlying_type`
- `values`

Declaration fields:

- `name`
- `annotations`
- `fields`

Field fields:

- `name`
- `type`
