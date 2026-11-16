# Modular Rules Example

This example loads multiple RFL files and computes a Gold customer's price.
All four files share the `ecommerce.common` type namespace; import paths select
files relative to the supplied base directory, independently of that namespace.

```text
modular-rules/
├── common/
│   └── types.rfl
├── validation_rules.rfl
├── pricing_rules.rfl
└── main.rfl
```

`main.rfl` imports `common.types`, `validation_rules` and `pricing_rules`.
The first resolves to `common/types.rfl`; the other two resolve to the matching
files in the base directory. The imported rule files also import `common.types`;
the loader visits each file once. Underscores are valid import-name characters.

`common/types.rfl` declares `Customer`, `Order`, `ValidationResult` and
`PricingResult`. Validation rules check customer details and positive order
quantity/subtotal. Pricing rules select the customer-tier discount and an
additional large-order discount. `main.rfl` exposes the `PricingResults` query.

## Run

From the repository root in the restored SDK environment:

```powershell
./build/native-release-user/bin/drills_engine.exe examples/modular-rules/main.rfl
ctest --preset native-release-user -R '^documented_examples_test\.engine$' -V
```

The first command checks compilation. The formal regression additionally
inserts an `ecommerce.common.Customer` with tier `Gold` and an
`ecommerce.common.Order` with five items and subtotal 250, fires the rules and
asserts a final price of **212.50**.

The compiled C++ usage is maintained in
[`documented_examples_test.cpp`](../../core/test/engine/documented_examples_test.cpp)
rather than duplicating an uncompiled snippet here. It uses the in-tree
`build_knowledge_base(result, path)` loader, which selects the importing file's
directory as its import base. A caller with explicit search roots can use
`build_knowledge_base(path, base_dirs, result)`.

These C++ APIs support the in-tree engine examples and tests. Applications,
including C++ applications, use the installed
[public C ABI](../../rulesforge/include/rules_forge.h); this RFL-only example
is not a schema-bound `capi_demo` input pack.
