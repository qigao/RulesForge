"""Flatten the shipped scenario documents into arrays of candidate facts.

This does not bind schemas or evaluate rules. It preserves record values and
scenario order; order items inherit their enclosing order's ID.
"""

import argparse
import json
from pathlib import Path


def record(value, context):
    if not isinstance(value, dict):
        raise ValueError(f"{context} must be an object")
    return {key: item for key, item in value.items() if not key.startswith("_")}


def records(document, key):
    values = document[key]
    if not isinstance(values, list):
        raise ValueError(f"{key} must be an array")
    return [record(value, f"{key}[{index}]") for index, value in enumerate(values)]


def flatten(mode, document):
    document = record(document, "document")
    if mode == "fraud":
        return {
            "accountProfiles": records(document, "accountProfiles"),
            "highRiskCountries": records(document, "highRiskCountries"),
            "transactions": [value for scenario in records(document, "testScenarios")
                             for value in records(scenario, "transactions")],
        }
    if mode == "travel":
        result = {key: records(document, key) for key in
                  ("flightOptions", "hotelOptions", "visaRequirements")}
        result["travelRequests"] = [record(scenario["request"], "request")
                                    for scenario in records(document, "testScenarios")]
        return result
    if mode == "order":
        result = {key: records(document, key) for key in
                  ("inventory", "warehouses", "promotions")}
        result.update(orders=[], orderItems=[])
        for scenario in records(document, "testOrders"):
            order = record(scenario["order"], "order")
            order_id = order["orderId"]
            if not isinstance(order_id, str) or not order_id:
                raise ValueError("orderId must be a non-empty string")
            result["orders"].append(order)
            for item in records(scenario, "items"):
                if "orderId" in item and item["orderId"] != order_id:
                    raise ValueError("item orderId differs from its enclosing order")
                item["orderId"] = order_id
                result["orderItems"].append(item)
        return result
    raise ValueError(f"unsupported scenario: {mode}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("fraud", "order", "travel"))
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error("input and output must be different files")
    with args.input.open(encoding="utf-8") as source:
        result = flatten(args.mode, json.load(source))
    # Complete validation and serialization before touching the destination.
    text = json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False) + "\n"
    args.output.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
