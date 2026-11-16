#include <cmath>
#include <limits>
#include <toad/plugin.h>
TOAD_PLUGIN_FUNCTION(double, boiler_ewma_alpha, (double, dt, CMETA_PARAM_IN),
                     (double, tau, CMETA_PARAM_IN));
double boiler_ewma_alpha(double dt, double tau) {
  if (dt <= 0 || tau <= 0)
    return std::numeric_limits<double>::quiet_NaN();
  return -std::expm1(-dt / tau);
}
// Example counters/phases are bounded below one million. -1 is a domain error
// consumed by the script before any state is committed; no narrowing is implicit.
TOAD_PLUGIN_FUNCTION(int, boiler_state_integer, (double, value, CMETA_PARAM_IN));
int boiler_state_integer(double value) {
  if (!std::isfinite(value) || value < 0 || value > 1000000 || std::floor(value) != value)
    return -1;
  return static_cast<int>(value);
}
#define EXPORTS(X)                                                                                 \
  X(function, boiler_ewma_alpha, "boiler_ewma_alpha", "boiler.statistics", 1u, 0u)                 \
  X(function, boiler_state_integer, "boiler_state_integer", "boiler.statistics", 1u, 0u)
CMETA_PLUGIN_DECLARE(statistics, "boiler.statistics", (1u, 0u, 0u), EXPORTS,
                     CMETA_PLUGIN_PASSIVE());
