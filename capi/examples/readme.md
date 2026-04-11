```bash
capi_demo -r payments.rfl -j payments_test_data.json -m testOrders:com.example.pricing.Order -q OrdersWithDiscount -f quantity,unitPrice,finalPrice
```

```bash
capi_demo -r payments.rfl -c payments_test_data.csv -T com.example.pricing.Order -q OrdersWithDiscount -f quantity,unitPrice,finalPrice
```

## C API Examples

`capi/examples` now contains direct C API usage examples only.

If you want RulesForge extension examples, go to:

- `../../plugins/examples`
- `../../plugins/README.md`
