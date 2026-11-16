#include <cmath>
#include <limits>
#include <toad/plugin.h>

TOAD_PLUGIN_FUNCTION(double, maintenance_risk_score, (double, mean, CMETA_PARAM_IN),
                     (double, variance, CMETA_PARAM_IN), (double, roughness, CMETA_PARAM_IN),
                     (double, baseline, CMETA_PARAM_IN));

double maintenance_risk_score(double mean, double variance, double roughness, double baseline) {
  // Non-finite results are the scalar bridge's explicit HOST_ERROR boundary.
  if (baseline <= 0 || variance < 0 || roughness < 0)
    return std::numeric_limits<double>::quiet_NaN();
  double scale = baseline * baseline;
  if (!std::isfinite(scale) || scale == 0)
    return std::numeric_limits<double>::quiet_NaN();
  double risk = 10.0 * variance / scale + 5.0 * roughness / scale +
                10.0 * std::abs(mean - baseline) / baseline;
  if (!std::isfinite(risk))
    return std::numeric_limits<double>::quiet_NaN();
  return risk > 100.0 ? 100.0 : risk;
}

TOAD_PLUGIN_FUNCTION(double, maintenance_expected_loss, (double, mean_risk, CMETA_PARAM_IN),
                     (double, failure_cost, CMETA_PARAM_IN), (double, criticality, CMETA_PARAM_IN));

double maintenance_expected_loss(double mean_risk, double failure_cost, double criticality) {
  if (mean_risk < 0 || failure_cost < 0 || criticality <= 0)
    return std::numeric_limits<double>::quiet_NaN();
  return mean_risk / 100.0 * failure_cost * criticality;
}

#define MAINTENANCE_EXPORTS(X)                                                                     \
  X(function, maintenance_risk_score, "maintenance_risk_score", "maintenance.math", 1u, 0u)        \
  X(function, maintenance_expected_loss, "maintenance_expected_loss", "maintenance.math", 1u, 0u)
CMETA_PLUGIN_DECLARE(maintenance_math, "maintenance.math", (1u, 0u, 0u), MAINTENANCE_EXPORTS,
                     CMETA_PLUGIN_PASSIVE());
