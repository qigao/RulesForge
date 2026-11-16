#include "session.hpp"
#include <array>
#include <cmath>
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: maintenance_demo <bundle.json>\n";
    return 2;
  }
  try {
    using namespace maintenance_example;
    Program program(argv[1]);
    Session session(program);
    session.configure();
    session.configure(true, 4, 1, "press-8");
    std::array<char const *, 8> waveforms = {"[1,1,1,1]", "[1,2,1,2]", "[1,2,1,2]", "[1,5,1,5]",
                                             "[1,1,1,1]", "[1,1,1,1]", "[1,1,1,1]", "[1,1,1,1]"};
    std::array<char const *, 8> expected = {"WARMUP", "WARMUP", "INSPECT", "STOP",
                                            "STOP",   "STOP",   "STOP",    "MONITOR"};
    std::cout << "step,action,window_count,mean_risk,peak_risk,expected_cost\n";
    for (size_t index = 0; index < waveforms.size(); ++index) {
      if (index == 3) {
        session.reading("noise", "[1,9,1,9]", "press-8");
        session.fire();
      }
      auto value = session.step("sample-" + std::to_string(index + 1), waveforms[index]);
      if (value.action != expected[index])
        throw std::runtime_error("Unexpected maintenance action");
      std::cout << index + 1 << ',' << value.action << ',' << value.count << ',' << value.mean
                << ',' << value.peak << ',' << value.cost << '\n';
    }
    auto final = session.evaluation("sample-8");
    if (final.count != 4 || std::abs(final.mean) > 1e-9 || std::abs(final.peak) > 1e-9)
      throw std::runtime_error("Old risk did not leave the window");
    if (session.count("Observations") != 9 || session.count("ActionRequests") != 5)
      throw std::runtime_error("Unexpected retained observations or action requests");
    return 0;
  } catch (std::exception const &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
