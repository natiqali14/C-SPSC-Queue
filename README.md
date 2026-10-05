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
struct pause_policy {
  static void pause() noexcept { CPU_RELAX(); }
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
./build/benchmarks/cold_lf_spsc_benchmark
```

**Method (throughput):** a producer thread pushes 10,000,000 `std::uint64_t` items and a consumer thread pops them, with the two threads pinned to different cores. One timer covers the whole batch, and the result is items per second. This is repeated 100 times, and the min, median and max are reported. Timing the whole batch instead of every single push matters: a clock read costs ~20–50 ns, which is far more than one push.

**Results:**

| Queue | Min (M items/s) | Median (M items/s) | Max (M items/s) | ns per item (median) |
|---|---|---|---|---|
| cold_lf | 238.6 | 258.6 | 264.2 | 3.9 |
| Rigtorp `SPSCQueue` | 425.2 | 450.4 | 453.4 | 2.2 |

Conditions: Intel Core Ultra 5 225H, Linux, GCC 15.2, Release build, laptop in performance power mode, producer on core 0 and consumer on core 3 (both performance cores), queue capacity 65,536.

Rigtorp's queue is currently about **1.74× - 2.2x faster**. That ratio held steady across power modes even when the raw numbers changed by more than 2×. Finding out where the gap comes from is ongoing work.

The core numbers are hard-coded in `benchmarks/benchmark.cpp`. Change them if your machine has fewer cores or a different layout.

## Project layout

```
.
├── CMakeLists.txt
├── LICENSE
├── README.md
├── src/
│   ├── spsc_bounded_queue.h   the queue
│   ├── defines.h              integer aliases, COLD_LF_FORCEINLINE, CPU_RELAX
│   └── utils.h                pin_thread and test payload types
├── tests/
│   └── test_spsc_bounded_lf.cpp
└── benchmarks/
    ├── benchmark.cpp
    ├── benchmark_util.h       timer and statistics helpers
    └── rigtorp_queue.h        third-party baseline (MIT)
```

## Roadmap

- [ ] Find out why cold_lf's throughput is ~1.74× lower than Rigtorp's `SPSCQueue`
- [ ] Add a round-trip latency benchmark
- [ ] Benchmark with larger payloads (`payload_msg`)

## License

cold_lf is released under the [MIT License](LICENSE).

## Third-party code

`benchmarks/rigtorp_queue.h` is Erik Rigtorp's [SPSCQueue](https://github.com/rigtorp/SPSCQueue), Copyright (c) 2020 Erik Rigtorp, used under the MIT license (see the header of that file). It is only used by the benchmark, not by the queue itself.
