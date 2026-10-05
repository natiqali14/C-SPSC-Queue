// utils.h
// Thread pinning and payload types shared by the tests and benchmarks.
//
// Written by natiqali14 (Natiq Ali Syed)
// Copyright (c) 2026 Natiq Ali Syed
// SPDX-License-Identifier: MIT
// See LICENSE in the project root for details.

#ifndef COLD_LF_TEST_UTILS_H_
#define COLD_LF_TEST_UTILS_H_

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif


#include <atomic>
#include <random>
#include <array>

namespace cold_lf {
inline bool pin_thread(int core) {
#ifdef _WIN32
  HANDLE handle = GetCurrentThread();
  DWORD_PTR mask = (1ULL << core);
  DWORD_PTR previous_mask = SetThreadAffinityMask(handle, mask);
  if (previous_mask == 0) {
      return false;
  } else {
      return true;
  }
#elif defined(__linux__)
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  CPU_SET(core, &cpu_set);
  auto this_thread_id = pthread_self();
  return pthread_setaffinity_np(this_thread_id, sizeof(cpu_set), &cpu_set) == 0;

#endif
  return false;

}

struct payload_msg {
  static constexpr std::uint64_t MAGIC_NUMBER = 0x434f4c444c46;
  static constexpr std::uint16_t PAYLOAD_SIZE = 256;
  std::uint64_t magic_number_;
  std::array<std::uint64_t, PAYLOAD_SIZE> payload_ {};
  std::uint64_t seq_ {0};
  std::uint64_t checksum_ {0};

  payload_msg(const payload_msg& other) noexcept = default; 

  payload_msg& operator=(const payload_msg& other) noexcept = default;

  payload_msg(payload_msg&& other) noexcept = default;

  payload_msg& operator=(payload_msg&& other) noexcept = default;
  
  payload_msg(std::uint64_t seq) : seq_(seq) {
    magic_number_ = MAGIC_NUMBER;
    fill_payload();
    set_checksum();
  }

  bool operator==(const payload_msg& msg) const {
    if (msg.seq_ != seq_) return false;
    if (msg.checksum_ != checksum_) return false;
    if (msg.magic_number_ != magic_number_) return false;
    for (auto i {0}; i < PAYLOAD_SIZE; i++) {
      if (msg.payload_[i] != payload_[i]) return false;
    }
    return true;
  }

  void fill_payload() {
    std::mt19937_64 engine {seq_};
    for (auto i {0}; i < PAYLOAD_SIZE; i++) {
      payload_[i] = engine();
    }
  }

  void set_checksum() {
    checksum_ = magic_number_ ^ seq_;
    for (auto i {0}; i < PAYLOAD_SIZE; i++) {
      checksum_ ^= payload_[i];
    }
  }

};

struct move_copy {
public:
  move_copy() = default;
  move_copy(const move_copy& obj) {
    copy_constructed_ = true;
    move_constructed_ = false;
  }
  move_copy(move_copy&& obj) {
    move_constructed_ = true;
    copy_constructed_ = false;
  }

  move_copy& operator=(const move_copy& obj) {
    copy_constructed_ = true;
    move_constructed_ = false;
    return *this;
  }

  move_copy& operator=(move_copy&& obj) {
    move_constructed_ = true;
    copy_constructed_ = false;
    return *this;
  }

  bool move_constructed_ {false};
  bool copy_constructed_ {false};
};

struct destruction {
  inline static std::atomic<std::uint64_t> total_destructed_ {};
  destruction() = default;
  ~destruction() {
    total_destructed_.fetch_add(1, std::memory_order_relaxed);
  }
};

}

#endif  // COLD_LF_TEST_UTILS_H_
