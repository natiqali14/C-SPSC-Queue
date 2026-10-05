// spsc_bounded_queue.h
// Lock-free, bounded single-producer / single-consumer (SPSC) queue.
//
// Written by natiqali14 (Natiq Ali Syed)
// Copyright (c) 2026 Natiq Ali Syed
// SPDX-License-Identifier: MIT
// See LICENSE in the project root for details.

#ifndef COLD_LF_SPSC_BOUNDED_QUEUE_H_
#define COLD_LF_SPSC_BOUNDED_QUEUE_H_

#include <concepts>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <utility>
#include <atomic>

#if defined(__clang__)
  #define COLD_LF_FORCEINLINE [[clang::always_inline]]
#elif defined(__GNUC__)
  #define COLD_LF_FORCEINLINE [[gnu::always_inline]]
#else 
  #define COLD_LF_FORCEINLINE
#endif

namespace cold_lf {

template<typename P>
concept WaitPolicy = requires  {
  {P::pause() } noexcept;
};

struct spin_policy {
  static void pause() noexcept {/* do nothing */};
};

template<typename Alloc, typename T, typename ...Args>
concept IsAllocNoExcept = requires (Alloc& alloc, T* ptr, Args&&  ... args) {
  { std::allocator_traits<Alloc>::construct(alloc, ptr, std::forward<Args>(args)...) } noexcept;
};

template<typename Tp, std::size_t Cap, typename Alloc = std::allocator<Tp>, std::size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size, WaitPolicy WAIT_POLICY = spin_policy>
class spsc_bounded_queue {
  using data_t = Tp;
  using ptr_t = data_t*;
  using alloc_traits = std::allocator_traits<Alloc>;

  static_assert(Cap != 0, "0 capacity given");
  static_assert((Cap & (Cap - 1)) == 0, "Queue capacity should be a power of 2");

public:
  spsc_bounded_queue(const spsc_bounded_queue&) = delete;
  spsc_bounded_queue(spsc_bounded_queue&&) = delete;
  spsc_bounded_queue& operator=(const spsc_bounded_queue&) = delete;
  spsc_bounded_queue& operator=(spsc_bounded_queue&&) = delete;

  spsc_bounded_queue() : data_ (alloc_traits::allocate(alloc_, Cap)) {}
  spsc_bounded_queue(Alloc& alloc) : alloc_(alloc), data_ (alloc_traits::allocate(alloc_, Cap)) {} 
  ~spsc_bounded_queue() {
    while (true) {
      auto tail = can_pop();
      if (tail.has_value()) {
        pop(tail.value());
      }
      else {
        break;
      }
    }
    alloc_traits::deallocate(alloc_, data_, Cap);
  }

  void wait_and_push(const data_t& data) noexcept(IsAllocNoExcept<Alloc, Tp, data_t>)
  requires std::copy_constructible<data_t> {
    p_emplace(can_push_with_wait(), data);
  }

  void wait_and_push(data_t&& data) noexcept(IsAllocNoExcept<Alloc, Tp, data_t>)
  requires std::move_constructible<data_t> {
    p_emplace(can_push_with_wait(), std::move(data));
  }

  template<typename ...Args>
  void wait_and_emplace(Args&& ...args) noexcept(IsAllocNoExcept<Alloc, Tp, Args...>) 
  requires std::constructible_from<data_t, Args...> {
    p_emplace(can_push_with_wait(), std::forward<Args>(args)...);
  }



  [[nodiscard]] bool try_push(const data_t& data) noexcept(IsAllocNoExcept<Alloc, Tp, data_t>)
  requires std::copy_constructible<data_t> {
    auto head= can_push();
    if (!head.has_value()) {
      return false;
    }
    p_emplace(head.value(), data);
    return true;
  }

  [[nodiscard]] bool try_push(data_t&& data) noexcept(IsAllocNoExcept<Alloc, Tp, data_t>)
  requires std::move_constructible<data_t> {
   auto head = can_push();
    if (!head.has_value()) {
      return false;
    }
    p_emplace(head.value(), std::move(data));
    return true;
  }

  template<typename ...Args>
  [[nodiscard]] bool emplace(Args&& ...args) noexcept(IsAllocNoExcept<Alloc, Tp, Args...>) 
  requires std::constructible_from<data_t, Args...> {
    auto head = can_push();
    if (!head.has_value()) {
      return false;
    }
    p_emplace(head.value(), std::forward<Args>(args)...);
    return true;
  }

  [[nodiscard]] data_t& wait_and_front() {
    auto index = get_remainder(can_pop_with_wait());
    auto data = get_slot(index);
    return *data;
  }

  [[nodiscard]] data_t* front() {
    auto tail = can_pop();
    if (tail.has_value()) {
      auto index = tail.value() % Cap;
      auto data = get_slot(index);
      return data;
    }
    return nullptr;
    
  }

  void pop() {
    pop(can_pop_with_wait());
  }

  [[nodiscard]] bool try_pop() {
    auto tail = can_pop();
    if (!tail.has_value()) {
      return false;
    }
    pop(tail.value());
    return true;
  }

  bool empty() const {
      return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
  }

private:
  void pop(std::uint64_t current_tail) {
    auto index = get_remainder(current_tail);
    auto ptr = get_slot(index);
    alloc_traits::destroy(alloc_, ptr);
    tail_.store(current_tail + 1, std::memory_order_release);
  }

  template<typename ... Args>
  void p_emplace(std::uint64_t current_head, Args&& ... args) {
      auto index = get_remainder(current_head);
      alloc_traits::construct(alloc_, &data_[index], std::forward<Args>(args)...);
      head_.store(current_head + 1, std::memory_order_release);
  }

  COLD_LF_FORCEINLINE [[nodiscard]] ptr_t get_slot(std::uint64_t index) {
    return &data_[index];
  }

  COLD_LF_FORCEINLINE [[nodiscard]] std::optional<std::uint64_t> can_push() {
    auto current_head = head_.load(std::memory_order_acquire);
    auto current_tail = tail_.load(std::memory_order_acquire);
    if (current_head - current_tail >= Cap) {
      return std::nullopt;
    }
    return current_head;
  }

  COLD_LF_FORCEINLINE [[nodiscard]] std::optional<std::uint64_t> can_pop() {
    auto current_head = head_.load(std::memory_order_acquire);
    auto current_tail = tail_.load(std::memory_order_acquire);
    if (current_tail >= current_head) {
      return std::nullopt;
    }
    return current_tail;
  }

  std::uint64_t can_push_with_wait() {
    auto head = can_push();
    while (!head.has_value()) {
      WAIT_POLICY::pause();
      head = can_push();
    }
    return head.value();
  }

  std::uint64_t can_pop_with_wait() {
    auto tail = can_pop();
    while (!tail.has_value()) {
      WAIT_POLICY::pause();
      tail = can_pop();
    }
    return tail.value();
  }

  COLD_LF_FORCEINLINE std::uint64_t get_remainder(std::uint64_t index) {
    return index & (Cap - 1);
  }

private:
  alignas(CACHE_LINE_SIZE) std::atomic<std::uint64_t> head_ {};
  alignas(CACHE_LINE_SIZE) std::atomic<std::uint64_t> tail_ {};
  
  alignas(CACHE_LINE_SIZE) ptr_t data_;
  [[no_unique_address]] alignas(CACHE_LINE_SIZE) Alloc alloc_ {};
};
}

#endif  // COLD_LF_SPSC_BOUNDED_QUEUE_H_
