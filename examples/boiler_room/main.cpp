#include "simulation.hpp"
#include "text.hpp"
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
  try {
    if (argc < 2 || argc > 3)
      throw std::invalid_argument("Usage: boiler_room_demo bundle.json "
                                  "[normal|circulation|inefficient|cold|dispatch|window]");
    boiler_room::Config config;
    if (argc == 3) {
      auto spec = boiler_room::parse_scenario(argv[2]);
      config.scenario = spec.scenario;
      config.seconds = spec.seconds;
    }
    auto report = boiler_room::simulate(argv[1], config);
    for (size_t source = 0; source < boiler_room::source_count; ++source)
      std::cout << boiler_room::source_name(source) << ": " << report.received[source]
                << " messages\n";
    for (auto const &event : report.transitions) {
      char const *kind = event.kind == 1 ? "RAISED" : event.kind == 2 ? "CLEARED" : "ACKNOWLEDGED";
      std::cout << event.tick / 1000 << "s " << event.asset << ' ' << event.code << ' ' << kind
                << " occurrence=" << event.occurrence << " revision=" << event.revision << '\n';
    }
    boiler_room::DispatchDecision const *previous = nullptr;
    for (auto const &decision : report.decisions) {
      bool changed = !previous || previous->action != decision.action ||
                     previous->window_levels != decision.window_levels;
      if (previous)
        for (size_t n = 0; n < decision.candidates.size(); ++n)
          changed |= previous->candidates[n].reason != decision.candidates[n].reason;
      previous = &decision;
      if (!changed)
        continue;
      std::cout << decision.tick / 1000 << "s ADVICE " << decision.action
                << " reason=" << decision.reason;
      if (decision.feasible)
        std::cout << " score=" << decision.score << " heat_margin_kW=" << decision.margin;
      std::cout << '\n';
      for (size_t p = 0; p < decision.windows.size(); ++p) {
        auto const &w = decision.windows[p];
        std::cout << "  window P" << p + 1 << ' ' << decision.window_levels[p]
                  << " valid_frames=" << w.samples << "/30 coverage_pct=" << w.coverage;
        if (decision.window_levels[p] != "UNKNOWN") {
          std::cout << " mean=" << w.mean << " peak=" << w.peak
                    << " continuous_s=" << w.continuous_seconds;
          if (w.trend_ready)
            std::cout << " trend_delta=" << w.trend_delta << " trend_per_s=" << w.trend_per_second;
          else
            std::cout << " trend=null";
        } else
          std::cout << " metrics=null";
        std::cout << '\n';
      }
      for (auto const &evidence : decision.incidents)
        std::cout << "  evidence " << evidence.pump << " loop=" << evidence.loop
                  << " risk=" << evidence.risk << " flow=" << evidence.flow
                  << " pressure=" << evidence.pressure << '\n';
      for (auto const &plan : decision.candidates) {
        std::cout << "  " << plan.action << ' ' << plan.status << " reason=" << plan.reason;
        if (plan.calculated)
          std::cout << " capacity_kW=" << plan.capacity << " margin_kW=" << plan.margin
                    << " energy_kWh=" << plan.energy_kwh << " residual_risk=" << plan.residual_risk
                    << " energy_cost=" << plan.energy_cost << " risk_cost=" << plan.risk_cost
                    << " score=" << plan.score;
        else
          std::cout << " metrics=null";
        std::cout << '\n';
      }
    }
    std::cout << "frames=" << report.frames.size() << " duplicate=" << report.duplicates
              << " retained=" << report.retained << " late=" << report.late
              << " max_window=" << report.max_window_samples << '\n';
    auto const &last = report.frames.back();
    std::cout << "B1 heat_kW=";
    if (last.heat_valid[0])
      std::cout << last.heat[0];
    else
      std::cout << "null";
    std::cout << " efficiency_pct=";
    if (last.efficiency_valid[0])
      std::cout << last.efficiency[0];
    else
      std::cout << "null";
    std::cout << "\n";
    return 0;
  } catch (std::exception const &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
