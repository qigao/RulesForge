```bash
capi_demo -r payments.rfl -s payments.schema -j payments_test_data.json -m testOrders:Order -q OrdersWithDiscount -b order -f quantity,unitPrice,finalPrice
```

```bash
capi_demo -r payments.rfl -s payments.schema -c payments_test_data.csv -T Order -q OrdersWithDiscount -b order -f quantity,unitPrice,finalPrice
```

```bash
capi_demo -r payments.rfl -s payments.schema -c payments_test_data.csv -T Order --validation warn --trace --memory -q OrdersWithDiscount -b order -f quantity,finalPrice
```

## C API Examples

`capi/examples` now contains direct C API usage examples only.

Current `capi_demo` reflects the 0.5.0 C API:

- requires a `.schema` file for JSON and CSV loading
- expects the RFL rule file to import external input types from that `.schema`
- keeps RFL `declare` for internal derived facts only
- supports execution mode selection (`--mode v1|v2`; `v2` is usually 20%-30% faster)
- supports session validation mode (`--validation none|warn|strict`)
- can print execution traces (`--trace`, `--trace-network`)
- can print session memory statistics (`--memory`)

`payments.rfl` imports `payments.schema`, so both JSON and CSV commands above use the same external type source.

For C API ownership and failure semantics, see `../../include/rule_forge.h`.
