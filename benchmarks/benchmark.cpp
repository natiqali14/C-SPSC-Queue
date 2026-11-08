#include <atomic>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <spsc_bounded_queue.h>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

/// file includes rigtorp's spsc queue -> https://github.com/rigtorp/SPSCQueue.git
#include <rigtorp_queue.h>
#include <benchmark_util.h>
#include <utils.h>



std::vector<double> conversion(const std::vector<cold_lf::benchmark::timer::time_passed>& t) {
  std::vector<double> converted;
  converted.reserve(t.size());
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

template<CREATE_QUEUE E, std::size_t N, PayloadCreator P>
cold_lf::benchmark::throughput_metrics cal_through_puts(int loops, int warmup_loops, std::uint64_t items, std::uint32_t producer_core, std::uint32_t consumer_core) {
  using T = P::value_type;
  static_assert((N & (N - 1)) == 0, "N must be a power of 2 so i & (N - 1) works as i % N");
  auto q = create_queue<T, N, E>();

  std::vector<T> to_be_pushed;
  to_be_pushed.reserve(N);
  for (std::uint64_t i {0}; i < N; i++) {
    to_be_pushed.push_back(P::make(i));
  }

  std::vector<cold_lf::benchmark::timer::time_passed> through_puts;
  through_puts.reserve(loops);

  std::atomic<cold_lf::benchmark::timer::timer_unit> start_time {};
  
  std::atomic<int> consumer_ready_for {-1};

  auto pop_thread = std::thread([&] {
    if (!cold_lf::pin_thread(consumer_core)) {
      std::cerr << "warning: could not pin consumer to core " << consumer_core << "\n";
    }
    for (int run {0}; run < loops; run++) {
      consumer_ready_for.store(run, std::memory_order_release);
      for (std::uint64_t i {0}; i < items; i++) {
        T* front = q.front();
        while (front == nullptr) {
          front = q.front();
        }

        T value = *front;
        q.pop();
        cold_lf::benchmark::do_not_optimize(value);
      }
      through_puts.push_back(cold_lf::benchmark::timer::get_time_passed(start_time.load()));
    }
  });

  auto push_thread = std::thread([&] {
    if (!cold_lf::pin_thread(producer_core)) {
      std::cerr << "warning: could not pin producer to core " << producer_core << "\n";
    }
    for (int run {0}; run < loops; run++) {
      while (consumer_ready_for.load(std::memory_order_acquire) != run);
      start_time.store(cold_lf::benchmark::timer::get_current_time());
      for (std::uint64_t i {0}; i < items; i++) {
        while (!q.try_push(to_be_pushed[i & (N - 1)]));
      }
    }
  });

  push_thread.join();
  pop_thread.join();

  auto through_puts_s = conversion({through_puts.begin() + warmup_loops, through_puts.end()});
  return cold_lf::benchmark::throughput_metrics::create(through_puts_s, items);
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
template<>
struct payload_creator<std::uint8_t> {
  using value_type = std::uint8_t;
  static std::uint8_t make(std::uint64_t i) {
    return static_cast<std::uint8_t>(i);
  }
};

using bit8 =  std::uint8_t;
using bit64 = std::uint64_t;
using big_payload = cold_lf::payload_msg;

// Used when no queue size is given on the command line.
constexpr std::size_t DEFAULT_Q_SIZE = 1024;
constexpr int RUNS = 200;
constexpr int WARMUP_RUNS = 10;

constexpr std::uint32_t PRODUCER_CORE = 0;
constexpr std::uint32_t CONSUMER_CORE = 3;

template<PayloadCreator P, std::size_t N>
void compare(const std::string& type_name, std::uint64_t items, bool use_rigtorp, bool use_cold_lf) {
  cold_lf::benchmark::throughput_metrics rig_m;
  cold_lf::benchmark::throughput_metrics lf_m;

  std::cout << "--- " << type_name << ", " << items << " items per run ---\n";

  if (use_rigtorp) {
    rig_m = cal_through_puts<RIGTORP, N, P>(RUNS, WARMUP_RUNS, items, PRODUCER_CORE, CONSUMER_CORE);
    std::cout << "Rigtorp Q [" << type_name << "] " << rig_m << "\n";
  }

  if (use_cold_lf) {
    lf_m = cal_through_puts<COLD_LF, N, P>(RUNS, WARMUP_RUNS, items, PRODUCER_CORE, CONSUMER_CORE);
    std::cout << "Cold lf Q [" << type_name << "] " << lf_m << "\n";
  }

  if (use_rigtorp && use_cold_lf) {
    double diff = (lf_m.median_ - rig_m.median_) / rig_m.median_ * 100.0;
    std::cout << "[" << type_name << "] median: cold_lf is "
              << std::setprecision(2) << std::abs(diff) << "% "
              << (diff >= 0 ? "faster" : "slower") << " than rigtorp\n";
  }
  std::cout << "\n";
}

void print_header(std::size_t q_size) {
  std::cout
    << "What is measured:\n"
    << "  One run = the producer (core " << PRODUCER_CORE << ") pushes a fixed number of items and the\n"
    << "  consumer (core " << CONSUMER_CORE << ") pops and copies out every one of them.\n"
    << "  Run time = from the producer's first push until the consumer's last pop.\n"
    << "  Throughput = items in the run / run time.\n"
    << "\n"
    << "How to read it:\n"
    << "  All numbers are throughput in millions of items per second (M items/s). Higher is better.\n"
    << "  Each case does " << RUNS << " runs; the first " << WARMUP_RUNS << " are warmup and dropped,\n"
    << "  so the numbers below summarise " << (RUNS - WARMUP_RUNS) << " runs:\n"
    << "    min    = the slowest run\n"
    << "    p10    = 10% of runs were slower than this\n"
    << "    median = the middle run (half slower, half faster)\n"
    << "    p90    = 10% of runs were faster than this\n"
    << "    max    = the fastest run\n"
    << "  The faster/slower line compares the two medians.\n"
    << "\n"
    << "Queue size: " << q_size << " slots\n\n";
}

// Runs the payload types that were asked for, on a queue with N slots.
template<std::size_t N>
void run_payloads(const std::string& payload, bool use_rigtorp, bool use_cold_lf) {
  print_header(N);
  if (payload == "8bit" || payload == "all") {
    compare<payload_creator<bit8>, N>("std::uint8_t", 1'000'000, use_rigtorp, use_cold_lf);
  }
  if (payload == "64bit" || payload == "all") {
    compare<payload_creator<bit64>, N>("std::uint64_t", 1'000'000, use_rigtorp, use_cold_lf);
  }
  if (payload == "big" || payload == "all") {
    compare<payload_creator<big_payload>, N>("cold_lf::payload_msg", 100'000, use_rigtorp, use_cold_lf);
  }
}

// cold_lf's size is a template parameter, so it must be known while compiling.
// That's why every supported size is listed here: each case compiles its own
// copy of the benchmark, and the size typed on the command line just picks
// which copy runs. Returns false for a size that isn't in the list.
bool run_with_q_size(std::size_t q_size, const std::string& payload, bool use_rigtorp, bool use_cold_lf) {
  switch (q_size) {
    case 1:     run_payloads<1>(payload, use_rigtorp, use_cold_lf);     return true;
    case 8:     run_payloads<8>(payload, use_rigtorp, use_cold_lf);     return true;
    case 64:    run_payloads<64>(payload, use_rigtorp, use_cold_lf);    return true;
    case 256:   run_payloads<256>(payload, use_rigtorp, use_cold_lf);   return true;
    case 1024:  run_payloads<1024>(payload, use_rigtorp, use_cold_lf);  return true;
    case 8192:  run_payloads<8192>(payload, use_rigtorp, use_cold_lf);  return true;
    case 32768: run_payloads<32768>(payload, use_rigtorp, use_cold_lf); return true;
    default:    return false;
  }
}

void print_usage(const char* program) {
  std::cerr << "usage: " << program << " [queue] [payload] [q_size]\n"
            << "  queue:   rigtorp | cold_lf | both                (default: both)\n"
            << "  payload: 8bit | 64bit | big | all                (default: all)\n"
            << "  q_size:  1 | 8 | 64 | 256 | 1024 | 8192 | 32768  (default: " << DEFAULT_Q_SIZE << ")\n";
}

int main(int argc, char* argv[]) {
  std::string queue = argc > 1 ? argv[1] : "both";
  std::string payload = argc > 2 ? argv[2] : "all";

  std::size_t q_size = DEFAULT_Q_SIZE;
  if (argc > 3) {
    // from_chars turns text into a number. It reports an error for text
    // like "abc", and `end` tells us if it stopped early, like in "8x".
    std::string_view text = argv[3];
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), q_size);
    if (error != std::errc{} || end != text.data() + text.size()) {
      print_usage(argv[0]);
      return 1;
    }
  }

  bool use_rigtorp = (queue == "rigtorp" || queue == "both");
  bool use_cold_lf = (queue == "cold_lf" || queue == "both");
  bool known_payload = (payload == "8bit" || payload == "64bit" || payload == "big" || payload == "all");
  if ((!use_rigtorp && !use_cold_lf) || !known_payload) {
    print_usage(argv[0]);
    return 1;
  }

  if (!run_with_q_size(q_size, payload, use_rigtorp, use_cold_lf)) {
    print_usage(argv[0]);
    return 1;
  }
  return 0;
}
