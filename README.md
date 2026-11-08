# C++ Lock Free SPSC Queue

A header-only, lock-free, bounded **single-producer / single-consumer (SPSC)** queue for C++20.

One thread pushes, one other thread pops, and the two never take a lock. Instead they coordinate through two atomic counters. This makes it suitable for passing messages between two pinned threads, for example a network thread handing data to a processing thread.

```cpp
cold_lf::spsc_bounded_queue<std::uint64_t, 1024> q;   // 1024 slots, fixed at compile time
```

## Features

- Header-only and lock-free
- Fixed, power-of-two capacity with all slots usable
- Custom allocators and a pluggable wait policy
- Throughput in the same range as [Rigtorp's `SPSCQueue`](https://github.com/rigtorp/SPSCQueue), with wins on both sides depending on queue size and item type (see [Benchmarks](#benchmarks))

## Requirements

- A C++20 compiler (tested with GCC 15.2)
- CMake 3.20 or newer (only for building the tests and benchmarks)
- Linux or Windows for thread pinning in the tests and benchmarks (`pin_thread` in `src/utils.h`)

## Usage

### Adding it to your project

The queue is a single header, copy `src/spsc_bounded_queue.h` into your project

If you need the tests and benchmarks, turn them on, for testing GoogleTest will be downloaded:

```cmake
set(COLD_LF_BUILD_TESTING ON CACHE BOOL "" FORCE)
set(COLD_LF_BUILD_BENCHMARKING ON CACHE BOOL "" FORCE)
```

### Example

```cpp
#include <spsc_bounded_queue.h>

#include <cstdint>
#include <iostream>
#include <thread>

int main() {
  cold_lf::spsc_bounded_queue<std::uint64_t, 1024> q;

  std::thread producer([&] {
    for (std::uint64_t i = 0; i < 1'000'000; i++) {
      while (!q.try_push(i));          // spin until there is a free slot
    }
  });

  std::thread consumer([&] {
    std::uint64_t sum = 0;
    for (std::uint64_t i = 0; i < 1'000'000; i++) {
      std::uint64_t* item = nullptr;
      while ((item = q.front()) == nullptr);   // spin until an item arrives
      sum += *item;
      q.pop();
    }
    std::cout << "sum = " << sum << '\n';      // prints: sum = 499999500000
  });

  producer.join();
  consumer.join();
}
```

### Thread rules

This is an **SPSC** queue, which means:

- Exactly **one** thread may call the push side: `try_push`, `emplace`, `wait_and_push`, `wait_and_emplace`.
- Exactly **one** other thread may call the pop side: `front`, `wait_and_front`, `pop`, `try_pop`.
- `empty()` can be called from either thread. The answer may already be out of date by the time you use it.

Using more than one producer or more than one consumer is a data race and undefined behavior.

## API

```cpp
template<typename Tp,
         std::size_t Cap,
         typename Alloc = std::allocator<Tp>,
         std::size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size,
         WaitPolicy WAIT_POLICY = spin_policy>
class spsc_bounded_queue;
```

| Template parameter | Meaning |
|---|---|
| `Tp` | Element type. |
| `Cap` | Number of slots. Must be a non-zero power of two (checked with `static_assert`). |
| `Alloc` | Allocator used for the slot storage and for constructing and destroying elements. |
| `CACHE_LINE_SIZE` | Alignment used to keep the internal fields on separate cache lines. |
| `WAIT_POLICY` | A type with `static void pause() noexcept`, called on every spin of the `wait_and_*` functions. |

**Producer side**

| Function | Behavior |
|---|---|
| `bool try_push(const Tp&)` / `bool try_push(Tp&&)` | Copies or moves the item in. Returns `false` if the queue is full. |
| `bool emplace(Args&&...)` | Constructs the item in place from `args`. Returns `false` if the queue is full. |
| `void wait_and_push(const Tp&)` / `void wait_and_push(Tp&&)` | Waits until there's a free slot, then pushes. |
| `void wait_and_emplace(Args&&...)` | Waits until there's a free slot, then constructs in place. |

**Consumer side**

| Function | Behavior |
|---|---|
| `Tp* front()` | Pointer to the oldest item, or `nullptr` if the queue is empty. The item stays in the queue. |
| `Tp& wait_and_front()` | Waits until an item exists, then returns a reference to it. |
| `void pop()` | Destroys the oldest item. **Waits if the queue is empty**, so call it after `front()` has returned non-null. |
| `bool try_pop()` | Destroys the oldest item. Returns `false` if the queue is empty. |

**Either side**

| Function | Behavior |
|---|---|
| `bool empty() const` | `true` if there are no items at the moment of the call. |

The queue is neither copyable nor movable. Its destructor destroys any items still inside.

### Custom wait policy

By default the `wait_and_*` functions spin as hard as they can (`spin_policy::pause()` does nothing). 
```cpp
#include <immintrin.h>   // _mm_pause (x86 only)

struct pause_policy {
  static void pause() noexcept { _mm_pause(); }
};

cold_lf::spsc_bounded_queue<std::uint64_t, 1024, std::allocator<std::uint64_t>, 64, pause_policy> q;
```

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

| CMake option | Default | What it does |
|---|---|---|
| `COLD_LF_BUILD_TESTING` | `OFF` | Builds the GoogleTest suite. GoogleTest 1.17 is downloaded with `FetchContent`. |
| `COLD_LF_BUILD_BENCHMARKING` | `OFF` | Builds the benchmark against Rigtorp's `SPSCQueue`. |

## Tests

```bash
ctest --test-dir build --output-on-failure
```

## Benchmarks

The benchmark compares `cold_lf` with [Erik Rigtorp's `SPSCQueue`](https://github.com/rigtorp/SPSCQueue), a widely used, fast SPSC queue, as a baseline.

```bash
./build/benchmarks/cold_lf_spsc_benchmark [queue] [payload] [q_size]
```

| Argument | Values | Default |
|---|---|---|
| `queue` | `rigtorp`, `cold_lf`, `both` | `both` |
| `payload` | `8bit` (`std::uint8_t`), `64bit` (`std::uint64_t`), `big` (`payload_msg`, about 2 KB), `all` | `all` |
| `q_size` | `1`, `8`, `64`, `256`, `1024`, `8192`, `32768` | `1024` |

For example, `./build/benchmarks/cold_lf_spsc_benchmark cold_lf 64bit 1024` runs only cold_lf, with `std::uint64_t` items and 1024 slots.

**Method (throughput):** the producer and consumer threads are pinned to two different cores. In one run the producer pushes a fixed number of items (1,000,000 for `8bit` and `64bit`, 100,000 for `big`), and the consumer pops every item and copies it out. One timer covers the whole run, from the producer's first push to the consumer's last pop, and throughput = items / time. Timing a whole run instead of every single push matters: a clock read costs ~20–50 ns, which is far more than one push. Before each run the producer waits until the consumer is ready, so thread start-up is never timed. Each case does 200 runs, drops the first 10 as warmup, and reports min, p10, median, p90 and max in millions of items per second.

The core numbers (`PRODUCER_CORE`, `CONSUMER_CORE`) are hard-coded in `benchmarks/benchmark.cpp`. Change them if your machine has fewer cores or a different layout.

### Running every combination

```bash
benchmarks/run_benchmarks.sh
```

This runs every queue size and payload, first with Rigtorp and then with cold_lf, each in its own process. It writes two files to `benchmark_results/`:

- `<date_time>_results.csv`: one line per run, followed by a table that says which queue had the higher median for every queue size and payload.
- `<date_time>_full.log`: everything the benchmark printed.

### Results

**In short: cold_lf's throughput is in the same range as Rigtorp's `SPSCQueue`.** Neither queue is faster in every case. Rigtorp is faster with a 1-slot queue and in a few `std::uint64_t` cases. cold_lf is faster for small items in several of the larger queue sizes. With ~2 KB items the two are within a few percent of each other.

Two runs of the script on a Linux server. This table only lists cases where the same queue won both times:

| Payload | Queue size | Faster |
|---|---|---|
| `8bit`, `64bit`, `big` | 1 | Rigtorp (7–69%) |
| `64bit` | 64, 1024 | Rigtorp (19–115%) |
| `8bit` | 256, 1024, 32768 | cold_lf (23–153%) |
| `64bit` | 8192, 32768 | cold_lf (40–85%) |
| `big` | 8 and larger | Tie (within ~6%) |

The other cases changed winner between the two runs. Some medians moved by up to 2× between runs, so treat the percentages as rough. Results also depend on the CPU: on an Intel Core Ultra 5 225H laptop, cold_lf was slightly faster at queue size 1. Run the script a few times on your own machine before drawing conclusions.

## Project layout

```
.
├── CMakeLists.txt
├── LICENSE
├── README.md
├── src/
│   ├── spsc_bounded_queue.h   the queue
│   └── utils.h                pin_thread and test payload types
├── tests/
│   └── test_spsc_bounded_lf.cpp
└── benchmarks/
    ├── benchmark.cpp
    ├── benchmark_util.h       timer and statistics helpers
    ├── run_benchmarks.sh      runs every combination, saves results
    └── rigtorp_queue.h        third-party baseline (MIT)
```

## License

cold_lf is released under the [MIT License](LICENSE).

## Third-party code

`benchmarks/rigtorp_queue.h` is Erik Rigtorp's [SPSCQueue](https://github.com/rigtorp/SPSCQueue), Copyright (c) 2020 Erik Rigtorp, used under the MIT license (see the header of that file). It is only used by the benchmark, not by the queue itself.
