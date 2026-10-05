#include <algorithm>
#include <concepts>
#include <cstddef>
#include <spsc_bounded_queue.h>
#include <thread>
#include <iostream>
#include <vector>

/// file includes rigtorp's spsc queue -> https://github.com/rigtorp/SPSCQueue.git
#include <rigtorp_queue.h>
#include <benchmark_util.h>
#include <utils.h>



std::vector<double> conversion(const std::vector<cold_lf::benchmark::timer::time_passed>& t) {
  std::vector<double> converted;
  for (std::size_t i{}; i < t.size(); i++) {
      converted.push_back(cold_lf::benchmark::timer::get_time_passed_s(t[i]));
  }
  return converted;
}

enum CREATE_QUEUE {
  RIGTORP,
  COLD_LF
};

template<typename T, std::size_t N, CREATE_QUEUE E>
auto create_queue() {
  if constexpr (E == RIGTORP) {
    return rigtorp::SPSCQueue<T>(N);
  }
  else {
    return cold_lf::spsc_bounded_queue<T, N>{};
  }
}

template<typename T>
concept PayloadCreator = requires (T, std::uint64_t i) {
  typename T::value_type;
  {T::make(i)} -> std::same_as<typename T::value_type>;
};

template<CREATE_QUEUE E, typename T, std::size_t N, PayloadCreator P>
cold_lf::benchmark::throughput_metrics cal_through_puts(int loops, std::uint64_t total_instructions, std::uint32_t core_id1, std::uint32_t core_id2) {
  auto q = create_queue<T, N, E>();
  std::vector<cold_lf::benchmark::timer::time_passed> through_puts;

  std::vector<T> to_be_pushed;
  std::uint32_t n = N + 100;
  for (auto i {0}; i < n; i++) {
    to_be_pushed.push_back(P::make(i));
  }
  std::atomic<cold_lf::benchmark::timer::timer_unit> s;

  std::atomic_bool start {false};
  for (auto i {0}; i < loops; i++) {
    auto pop_thread = std::thread([&] {
      cold_lf::pin_thread(core_id2);
      for (auto i {0UL}; i < total_instructions; i++) {
        while (!q.front());
        q.pop();
      }
      through_puts.push_back(cold_lf::benchmark::timer::get_time_passed(s));
      
    });

    auto push_thread = std::thread([&] {
      cold_lf::pin_thread(core_id1);
      while (!start);
      s = cold_lf::benchmark::timer::get_current_time();
      for (auto i {0UL}; i < total_instructions; i++) {
        while (!q.try_push(std::move(to_be_pushed[i % n])));
      }
    });

    start = true;
    push_thread.join();
    pop_thread.join();
    

  }
  
  auto through_puts_s = conversion({through_puts.begin() + 1, through_puts.end()});

  auto metrics = cold_lf::benchmark::throughput_metrics::create(through_puts_s, total_instructions);
  return metrics;
}



template<typename T>
struct payload_creator;

template<>
struct payload_creator<cold_lf::payload_msg> {
  using value_type = cold_lf::payload_msg;
  static cold_lf::payload_msg make(std::uint64_t i) {
    return {i};
  }
};

template<>
struct payload_creator<std::uint64_t> {
  using value_type = std::uint64_t;
  static std::uint64_t make(std::uint64_t i) {
    return i;
  }
};

int main() {
  constexpr std::uint64_t TOTAL_INSTRUCTIONS = 10000000;
  constexpr std::uint64_t Q_SIZE = 65536;

  auto cold_lf_metrics2 = cal_through_puts<COLD_LF, std::uint64_t, Q_SIZE, payload_creator<std::uint64_t>>(100,TOTAL_INSTRUCTIONS, 0, 3);
  auto rigtorp_metrics2 = cal_through_puts<RIGTORP, std::uint64_t, Q_SIZE, payload_creator<std::uint64_t>>(100,TOTAL_INSTRUCTIONS, 0, 3);

  std::cout << "Throughput: for Cold lf Q [std::uint64_t] " << cold_lf_metrics2 << "\n";
  std::cout << "Throughput: for Rigtorp Q [std::uint64_t]" << rigtorp_metrics2<< "\n";
  return 0;
}