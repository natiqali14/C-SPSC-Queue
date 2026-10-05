// benchmark_util.h
// utility for benchmarking
//
// Written by natiqali14 (Natiq Ali Syed)
// Copyright (c) 2026 Natiq Ali Syed
// SPDX-License-Identifier: MIT
// See LICENSE in the project root for details.

#ifndef COLD_BENCHMARK_UTIL_H_
#define COLD_BENCHMARK_UTIL_H_

#include <algorithm>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <ranges>
#include <span>
namespace cold_lf::benchmark {

namespace timer {
  // TODO change this clock to something more optimized
  using timer = std::chrono::steady_clock;

  using timer_unit = std::chrono::steady_clock::time_point;

  using time_passed = std::chrono::steady_clock::duration;

  const timer_unit g_start_time = timer::now();

  inline timer_unit get_current_time() {
    return timer::now();
  }

  inline time_passed get_time_passed(const timer_unit from) {
    timer_unit to = timer::now();
    return (timer::now() - from);
  }

  inline double get_time_passed_s(const time_passed t) {
    return static_cast<double>(t.count()) / 1000000000.0;
  }


}


struct throughput_metrics {
  std::uint64_t min_{};
  std::uint64_t max_{};
  std::uint64_t median_{};
  
  template<typename Container>
  requires std::ranges::random_access_range<Container>
  && requires (typename Container::value_type v1, typename Container::value_type v2) {
    {v1 < v2 } -> std::convertible_to<bool>;
  }
  static throughput_metrics create(Container &c, std::uint64_t no_of_instructions) {
    throughput_metrics m{};
    std::ranges::sort(c);
    auto size = std::ranges::size(c);
    m.median_ = no_of_instructions/c[size/2];
    m.min_ = no_of_instructions/c[size -1];
    m.max_ = no_of_instructions/ c[0];
    return m;
  }

  friend std::ostream& operator<<(std::ostream& os, const throughput_metrics& m);
};

std::ostream& operator<<(std::ostream& os, const throughput_metrics& m) {
  os << "{ min: " << m.min_
  << " median: " << m.median_
  << " max: " << m.max_
  << " }";
  return os;
}

}

#endif  // COLD_BENCHMARK_UTIL_H_
