#include <algorithm>
#include <toad/plugin.h>
TOAD_PLUGIN_FUNCTION(double, boiler_risk_index, (double, vibration_z, CMETA_PARAM_IN),
                     (double, pressure_drop, CMETA_PARAM_IN), (double, flow_drop, CMETA_PARAM_IN));
double boiler_risk_index(double vibration_z, double pressure_drop, double flow_drop) {
  return std::clamp(50.0 * std::clamp(vibration_z / 5.0, 0.0, 1.0) +
                        75.0 * std::max(0.0, pressure_drop) + 60.0 * std::max(0.0, flow_drop),
                    0.0, 100.0);
}
#define EXPORTS(X) X(function, boiler_risk_index, "boiler_risk_index", "boiler.scoring", 1u, 0u)
CMETA_PLUGIN_DECLARE(scoring, "boiler.scoring", (1u, 0u, 0u), EXPORTS, CMETA_PLUGIN_PASSIVE());
