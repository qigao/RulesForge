```bash
capi_demo -r payments.rfl -j payments_test_data.json -m testOrders:com.example.pricing.Order -q OrdersWithDiscount -b order -f quantity,unitPrice,finalPrice
```

```bash
capi_demo -r payments.rfl -c payments_test_data.csv -T com.example.pricing.Order -q OrdersWithDiscount -b order -f quantity,unitPrice,finalPrice
```

```bash
capi_demo -r payments.rfl -c payments_test_data.csv -T com.example.pricing.Order --validation warn --trace --memory -q OrdersWithDiscount -b order -f quantity,finalPrice
```

## C API Examples

`capi/examples` now contains direct C API usage examples only.

Current `capi_demo` reflects the 0.3.0 C API:

- uses stable fact-handle APIs for JSON and CSV loading (`*_ex`)
- supports session validation mode (`--validation none|warn|strict`)
- can print execution traces (`--trace`, `--trace-network`)
- can print session memory statistics (`--memory`)

For host callback ownership and failure semantics, see `../../docs/C_API_CONTRACT.md`.
