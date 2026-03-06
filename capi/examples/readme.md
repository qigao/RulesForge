```bash
capi_demo -r payments.rfl -j payments_test_data.json -m testOrders:com.example.pricing.Order -q OrdersWithDiscount -f quantity,unitPrice,finalPrice
```

```bash
capi_demo -r payments.rfl -c payments_test_data.csv -T com.example.pricing.Order -q OrdersWithDiscount -f quantity,unitPrice,finalPrice
```

```bash
# Build and run DLL function-table demo (loads ruleforge_native_plugin at runtime)
cmake --build <build_dir> --target native_function_table_demo native_table_plugin
./native_function_table_demo
```

Docs:

- Quickstart EN: `CAPI_NATIVE_DLL_QUICKSTART_EN.md`
- Quickstart ZH: `CAPI_NATIVE_DLL_QUICKSTART_ZH.md`
- EN: `CAPI_NATIVE_DLL_EN.md`
- ZH: `CAPI_NATIVE_DLL_ZH.md`
- Native Functions ZH: `NATIVE_FUNCTIONS_ZH.md`
