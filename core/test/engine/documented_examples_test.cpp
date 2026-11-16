#include "model/fact.hpp"
#include "runtime/query_result.hpp"
#include "runtime/stateful_session.hpp"
#include "rfl_parser.hpp"
#include "tinytest.hpp"

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
std::shared_ptr<KnowledgeBase> load_example(char const *relative) {
  ParsingResult result;
  auto path = (std::filesystem::path(RULESFORGE_DOC_EXAMPLES_DIR) / relative).string();
  auto kb = build_knowledge_base(result, path);
  if (!result.success || !kb) {
    std::string message = path;
    for (auto const &error : result.errors) message += "\n" + error.to_string();
    throw std::runtime_error(message);
  }
  return kb;
}
}

suite("Documented rule packs") {
  it("compiles the loan rules") {
    check_not_null(load_example("loan-eligibility/loan-eligibility.rfl").get());
  }
  it("compiles the insurance rules") {
    check_not_null(load_example("insurance-pricing/insurance-pricing.rfl").get());
  }
  it("compiles the fulfillment rules") {
    check_not_null(load_example("order-fulfillment/order-fulfillment.rfl").get());
  }
  it("compiles the travel rules") {
    check_not_null(load_example("travel-booking/travel-booking.rfl").get());
  }
  it("compiles the price plan rules") {
    check_not_null(load_example("price-plan/price-plan-dispatch.rfl").get());
  }
  it("resolves modular imports and computes the documented Gold price") {
    auto kb = load_example("modular-rules/main.rfl");
    auto session = kb->create_session();
    auto customer = std::make_shared<Fact>();
    customer->type = "ecommerce.common.Customer";
    customer->fields["id"] = int64_t{1};
    customer->fields["name"] = std::string("John Doe");
    customer->fields["email"] = std::string("john@example.com");
    customer->fields["tier"] = std::string("Gold");
    session->add_fact(customer);
    auto order = std::make_shared<Fact>();
    order->type = "ecommerce.common.Order";
    order->fields["id"] = int64_t{1001};
    order->fields["customerId"] = int64_t{1};
    order->fields["itemCount"] = int64_t{5};
    order->fields["subtotal"] = 250.0;
    session->add_fact(order);
    check_greater(session->fire_all_rules(100), 0);
    auto results = session->execute_query("PricingResults");
    check(results.success());
    check_equal(results.size(), size_t{1});
    for (auto row : results) {
      auto price = row.getFieldAs<double>("price", "finalPrice");
      check(price.has_value());
      check_within(*price, 212.5, 0.000001);
    }
  }
}
