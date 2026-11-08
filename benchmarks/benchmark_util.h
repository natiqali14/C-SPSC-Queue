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
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <vector>
namespace cold_lf::benchmark {

namespace timer {
  // TODO change this clock to something more optimized
  using timer = std::chrono::steady_clock;

  using timer_unit = std::chrono::steady_clock::time_point;

  using time_passed = std::chrono::steady_clock::duration;

  inline timer_unit get_current_time() {
    return timer::now();
  }

  inline time_passed get_time_passed(const timer_unit from) {
    return timer::now() - from;
  }

  inline double get_time_passed_s(const time_passed t) {
    return std::chrono::duration<double>(t).count();
  }


}

// Stops the compiler from deleting work whose result is never used.
// The empty asm tells the compiler "something might read or change value here",
// so it has to really build value in memory.
template<typename T>
inline void do_not_optimize(T& value) {
#if defined(__clang__) || defined(__GNUC__)
  asm volatile("" : "+m"(value) : : "memory");
#else
  volatile auto* bytes = reinterpret_cast<volatile unsigned char*>(&value);
  (void)bytes[0];
#endif
}


// Throughput in items per second. Everything is a double so that
// subtracting two results can go negative without wrapping around.
struct throughput_metrics {
  double min_{};     // slowest run
  double p10_{};     // 10% of runs were slower than this
  double median_{};
  double p90_{};     // 10% of runs were faster than this
  double max_{};     // fastest run

  // times_s: how long each run took, in seconds.
  // items: how many items went through the queue in one run.
  static throughput_metrics create(std::vector<double> times_s, std::uint64_t items) {
    assert(!times_s.empty());
    // Shortest time first, so index 0 is the fastest run.
    std::ranges::sort(times_s);
    auto ops_at = [&](double fraction) {
      auto index = static_cast<std::size_t>(fraction * static_cast<double>(times_s.size() - 1));
      return static_cast<double>(items) / times_s[index];
    };

    throughput_metrics m{};
    m.max_ = ops_at(0.0);
    m.p90_ = ops_at(0.1);
    m.median_ = ops_at(0.5);
    m.p10_ = ops_at(0.9);
    m.min_ = ops_at(1.0);
    return m;
  }

  friend std::ostream& operator<<(std::ostream& os, const throughput_metrics& m);
};

// inline: this header could be included from more than one .cpp file.
inline std::ostream& operator<<(std::ostream& os, const throughput_metrics& m) {
  constexpr double MILLION = 1'000'000.0;
  os << std::fixed << std::setprecision(2)
  << "{ min: " << m.min_ / MILLION
  << " p10: " << m.p10_ / MILLION
  << " median: " << m.median_ / MILLION
  << " p90: " << m.p90_ / MILLION
  << " max: " << m.max_ / MILLION
  << " } M items/s";
  return os;
}

}

#endif  // COLD_BENCHMARK_UTIL_H_
