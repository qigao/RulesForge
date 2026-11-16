#include <cmath>
#include <limits>
#include <toad/plugin.h>

TOAD_PLUGIN_FUNCTION(double, boiler_heat_kw, (double, flow, CMETA_PARAM_IN),
                     (double, supply, CMETA_PARAM_IN), (double, return_temp, CMETA_PARAM_IN),
                     (double, rho, CMETA_PARAM_IN), (double, cp, CMETA_PARAM_IN));
double boiler_heat_kw(double flow, double supply, double return_temp, double rho, double cp) {
  if (flow < 0 || supply < return_temp || rho <= 0 || cp <= 0)
    return std::numeric_limits<double>::quiet_NaN();
  return rho * cp * flow * (supply - return_temp) / 3600.0;
}
TOAD_PLUGIN_FUNCTION(double, boiler_efficiency_pct, (double, water_kwh, CMETA_PARAM_IN),
                     (double, gas_volume, CMETA_PARAM_IN), (double, lhv, CMETA_PARAM_IN));
double boiler_efficiency_pct(double water_kwh, double gas_volume, double lhv) {
  if (water_kwh < 0 || gas_volume <= 0 || lhv <= 0)
    return std::numeric_limits<double>::quiet_NaN();
  return 100.0 * water_kwh / (gas_volume * lhv);
}
TOAD_PLUGIN_FUNCTION(double, boiler_balance_pct, (double, total, CMETA_PARAM_IN),
                     (double, header, CMETA_PARAM_IN));
double boiler_balance_pct(double total, double header) {
  if (total <= 0 || header < 0)
    return std::numeric_limits<double>::quiet_NaN();
  return 100.0 * (total - header) / total;
}
TOAD_PLUGIN_FUNCTION(double, boiler_pump_power_kw, (double, nominal_kw, CMETA_PARAM_IN),
                     (double, speed, CMETA_PARAM_IN));
double boiler_pump_power_kw(double nominal_kw, double speed) {
  if (nominal_kw < 0 || speed < 0 || speed > 1)
    return std::numeric_limits<double>::quiet_NaN();
  return nominal_kw * speed * speed * speed;
}
#define EXPORTS(X)                                                                                 \
  X(function, boiler_heat_kw, "boiler_heat_kw", "boiler.physics", 1u, 0u)                          \
  X(function, boiler_efficiency_pct, "boiler_efficiency_pct", "boiler.physics", 1u, 0u)            \
  X(function, boiler_balance_pct, "boiler_balance_pct", "boiler.physics", 1u, 0u)                  \
  X(function, boiler_pump_power_kw, "boiler_pump_power_kw", "boiler.physics", 1u, 0u)
CMETA_PLUGIN_DECLARE(physics, "boiler.physics", (1u, 0u, 0u), EXPORTS, CMETA_PLUGIN_PASSIVE());
