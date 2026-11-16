"""Formal CLI failure-path and scenario-preprocessor regressions."""

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

parser = argparse.ArgumentParser()
parser.add_argument("--root", required=True, type=Path)
parser.add_argument("--demo", required=True)
args, remaining = parser.parse_known_args()
# Keep running this source-tree example from creating tracked bytecode artifacts.
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    "flatten_example_data", args.root / "examples/tools/flatten_example_data.py")
flatten_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flatten_module)


class DemoTests(unittest.TestCase):
    def test_help_returns_success_without_creating_runtime_resources(self):
        for options in (["--help"], ["-h"], ["-v", "--help"]):
            with self.subTest(options=options):
                result = subprocess.run([args.demo, *options], capture_output=True,
                                        text=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("--rfl", result.stdout)
                self.assertIn("--validation", result.stdout)
                self.assertNotIn("Version:", result.stdout)
                self.assertEqual(result.stderr, "")

    def test_help_does_not_hide_an_unknown_option(self):
        result = subprocess.run([args.demo, "--help", "--unknown-option"], capture_output=True,
                                text=True, timeout=15)
        self.assert_failure(result, "unknown-option")
        self.assertNotIn("Version:", result.stdout)

    def test_help_spelling_remains_literal_in_option_values_or_after_separator(self):
        for options in (["--rfl", "--help"], ["--", "--help"]):
            with self.subTest(options=options):
                result = subprocess.run([args.demo, *options], capture_output=True,
                                        text=True, timeout=15)
                self.assert_failure(result, "--help")
                self.assertNotIn("Options:", result.stdout)

    def test_missing_value_and_invalid_choice_fail_before_initialization(self):
        for options, diagnostic in ((["--rfl"], "requires a value"),
                                    (["--validation", "invalid"], "validation"),
                                    (["--trace=invalid"], "cannot take a value")):
            with self.subTest(options=options):
                result = subprocess.run([args.demo, *options], capture_output=True,
                                        text=True, timeout=15)
                self.assert_failure(result, diagnostic)
                self.assertNotIn("Version:", result.stdout)

    def test_short_option_bundles_are_rejected_before_entering_the_sdk(self):
        result = subprocess.run([args.demo, "-vh"], capture_output=True, text=True, timeout=15)
        self.assert_failure(result, "short options separately")

    def test_long_options_with_equals_and_positional_input_paths(self):
        rules = str(args.root / "examples/payments/payments.rfl")
        facts = str(args.root / "examples/payments/payments_test_data.json")
        variants = [
            [f"--rfl={rules}", f"--json={facts}", "--map=testOrders:Order", "--validation=strict"],
            [rules, facts, "-m", "testOrders:Order"],
            ["-r", rules, facts, "-m", "testOrders:Order"],
            ["-m", "testOrders:Order", "--", rules, facts],
        ]
        for options in variants:
            with self.subTest(options=options):
                result = subprocess.run([args.demo, *options], capture_output=True,
                                        text=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("Loaded 10 Order facts", result.stdout)
                self.assertIn("Demo completed successfully", result.stdout)

    def test_repeated_and_comma_separated_mappings_preserve_all_arrays(self):
        for options in (["-m", "first:Order", "--map=second:Order"],
                        ["--map", "first:Order,second:Order"]):
            with self.subTest(options=options):
                result = self.run_demo(*options, document={"first": [], "second": []})
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("from 'first'", result.stdout)
                self.assertIn("from 'second'", result.stdout)

    def test_mapping_capacity_failure_is_not_silent_truncation(self):
        result = self.run_demo(*(["-m", "testOrders:Order"] * 1025))
        self.assert_failure(result, "Too many --map arguments")
        self.assertNotIn("Version:", result.stdout)

    def run_demo(self, *options, document=None):
        with tempfile.TemporaryDirectory() as directory:
            source = args.root / "examples/payments/payments_test_data.json"
            if document is not None:
                source = Path(directory) / "facts.json"
                source.write_text(json.dumps(document), encoding="utf-8")
            return subprocess.run(
                [args.demo, "-r", str(args.root / "examples/payments/payments.rfl"),
                 "-j", str(source), *options], capture_output=True, text=True, timeout=15)

    def assert_failure(self, result, diagnostic):
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn(diagnostic, result.stderr)
        self.assertNotIn("Demo completed successfully", result.stdout)
        self.assertNotIn("Rules fired:", result.stdout)

    def test_unknown_schema_type_fails(self):
        result = self.run_demo("-m", "testOrders:MissingType")
        self.assert_failure(result, "MissingType")
        self.assertEqual(result.stderr.count("Error: Add fact"), 1)

    def test_missing_array_fails(self):
        self.assert_failure(self.run_demo("-m", "missing:Order"), "not found")

    def test_non_array_mapping_fails(self):
        self.assert_failure(self.run_demo("-m", "orders:Order", document={"orders": {}}),
                            "not an array")

    def test_non_object_item_fails(self):
        self.assert_failure(self.run_demo("-m", "orders:Order", document={"orders": [1]}),
                            "must be an object")

    def test_later_invalid_fact_stops_before_firing(self):
        document = {"orders": [{"quantity": 3, "unitPrice": 10.0, "finalPrice": 0.0},
                               {"quantity": "bad", "unitPrice": 10.0, "finalPrice": 0.0}]}
        self.assert_failure(self.run_demo("-m", "orders:Order", document=document),
                            "JSON insertion failed")

    def test_removed_schema_option_is_rejected(self):
        self.assert_failure(self.run_demo("-m", "testOrders:Order", "--schema", "missing.schema"),
                            "schema")

    def test_empty_array_is_valid(self):
        result = self.run_demo("-m", "orders:Order", document={"orders": []})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Loaded 0 Order facts", result.stdout)

    def test_csv_uses_imported_schema_without_schema_option(self):
        result = subprocess.run(
            [args.demo, "-r", str(args.root / "examples/payments/payments.rfl"),
             "-c", str(args.root / "examples/payments/payments_test_data.csv"), "-T", "Order"],
            capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Loaded 10 Order facts", result.stdout)


class FlattenTests(unittest.TestCase):
    def sample(self, relative):
        return json.loads((args.root / "examples" / relative).read_text(encoding="utf-8"))

    def test_fraud_preserves_scenario_order_and_profiles(self):
        source = self.sample("fraud-detection/fraud-test-data.json")
        result = flatten_module.flatten("fraud", source)
        expected = [transaction for scenario in source["testScenarios"]
                    for transaction in scenario["transactions"]]
        self.assertEqual(result["transactions"], expected)
        self.assertEqual(result["accountProfiles"], source["accountProfiles"])

    def test_order_items_inherit_order_id_without_changing_source(self):
        source = self.sample("order-fulfillment/order-test-data.json")
        result = flatten_module.flatten("order", source)
        expected = [{**item, "orderId": scenario["order"]["orderId"]}
                    for scenario in source["testOrders"] for item in scenario["items"]]
        self.assertEqual(result["orderItems"], expected)
        self.assertNotIn("orderId", source["testOrders"][0]["items"][0])

    def test_travel_retains_requests_and_catalogs(self):
        source = self.sample("travel-booking/travel-test-data.json")
        result = flatten_module.flatten("travel", source)
        self.assertEqual(result["travelRequests"], [s["request"] for s in source["testScenarios"]])
        self.assertEqual(result["flightOptions"], source["flightOptions"])

    def test_rejects_malformed_shape(self):
        with self.assertRaises(ValueError):
            flatten_module.flatten("fraud", {"accountProfiles": {}})

    def test_rejects_conflicting_item_parent(self):
        source = self.sample("order-fulfillment/order-test-data.json")
        source["testOrders"][0]["items"][0]["orderId"] = "different-order"
        with self.assertRaises(ValueError):
            flatten_module.flatten("order", source)

    def test_cli_writes_flat_document_and_rejects_input_overwrite(self):
        tool = args.root / "examples/tools/flatten_example_data.py"
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.json"
            target = Path(directory) / "flat.json"
            text = json.dumps(self.sample("fraud-detection/fraud-test-data.json"))
            source.write_text(text, encoding="utf-8")
            result = subprocess.run([sys.executable, str(tool), "fraud", str(source), str(target)],
                                    capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertGreater(len(json.loads(target.read_text(encoding="utf-8"))["transactions"]), 0)
            result = subprocess.run([sys.executable, str(tool), "fraud", str(source), str(source)],
                                    capture_output=True, text=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(source.read_text(encoding="utf-8"), text)


if __name__ == "__main__":
    unittest.main(argv=["examples_test", *remaining])
