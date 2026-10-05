#include <gtest/gtest.h>
#include <spsc_bounded_queue.h>
#include <thread>
#include <unordered_map>
#include <utils.h>
// test fixture
template<typename Tp>
class SpscBoundedQueueTest : public ::testing::Test {
protected:
  static constexpr size_t QUEUE_CAPACITY = 2048;
  cold_lf::spsc_bounded_queue<Tp, QUEUE_CAPACITY> q_;
};

class SpscBoundedQueueDestructionTest : public ::testing::Test {
  protected:
  void SetUp() override {
    cold_lf::destruction::total_destructed_ = 0;
  }
};

using Types = ::testing::Types<std::uint8_t, std::uint64_t, cold_lf::payload_msg>;
TYPED_TEST_SUITE(SpscBoundedQueueTest, Types);

template<typename Tp>
struct TestValue;

template<>
struct TestValue<std::uint8_t> {
  static std::uint8_t make(std::uint64_t seq) {
    return static_cast<std::uint8_t>(seq);
  }
};

template<>
struct TestValue<std::uint64_t> {
  static std::uint64_t make(std::uint64_t seq) {
    return seq;
  }
};

template<>
struct TestValue<cold_lf::payload_msg> {
  static cold_lf::payload_msg make(std::uint64_t seq) {
    return cold_lf::payload_msg {seq};
  }
};

//////////////////////////////////////////  SIMPLE TESTS  //////////////////////////////////////////

TEST(SpscBoundedLfQ, QEmpty) {
  cold_lf::spsc_bounded_queue<int, 8> q;
  EXPECT_EQ(q.empty(), true);
}

TEST(SpscBoundedLfQ, QEmptyPop) {
  cold_lf::spsc_bounded_queue<int, 8> q;
  EXPECT_EQ(q.try_pop(), false);
}

TEST(SpscBoundedLfQ, QFullPush) {
  cold_lf::spsc_bounded_queue<int, 2> q;
  EXPECT_EQ(q.try_push(1), true);
  EXPECT_EQ(q.try_push(1), true);
  EXPECT_EQ(q.try_push(1), false);
}

TEST(SpscBoundedLfQ, QCopyConstruction) {
  cold_lf::spsc_bounded_queue<cold_lf::move_copy, 2> q;
  cold_lf::move_copy mc;
  auto success = q.try_push(mc);
  auto *const mc_ref = q.front();
  EXPECT_TRUE(mc_ref != nullptr);
  EXPECT_EQ(mc_ref->copy_constructed_, true);
}

TEST(SpscBoundedLfQ, QMoveConstruction) {
  {
    cold_lf::spsc_bounded_queue<cold_lf::move_copy, 2> q;
    auto success = q.try_push(cold_lf::move_copy{});
    auto *const mc_ref = q.front();
    EXPECT_TRUE(mc_ref != nullptr);
    EXPECT_EQ(mc_ref->move_constructed_, true);
  }

  {
    cold_lf::spsc_bounded_queue<cold_lf::move_copy, 2> q;
    cold_lf::move_copy mc;
    auto success = q.try_push(std::move(mc));
    auto *const mc_ref = q.front();
    EXPECT_TRUE(mc_ref != nullptr);
    EXPECT_EQ(mc_ref->move_constructed_, true);
  }
}

TEST(SpscBoundedLfQ, QNotEmpty) {
  cold_lf::spsc_bounded_queue<int, 8> q;
  auto a = q.try_push(1);
  EXPECT_EQ(a, true);
  EXPECT_EQ(q.empty(), false);
}

TEST(SpscBoundedLfQ, QWithMultiplePushAndPop) {
  cold_lf::spsc_bounded_queue<int, 8> q;
  auto s = q.try_push(1);
  EXPECT_EQ(s, true);
  auto s2 = q.try_push(2);
  EXPECT_EQ(s2, true);
  auto s3 = q.try_push(3);
  EXPECT_EQ(s3, true);
  EXPECT_EQ(1, *q.front());
  EXPECT_EQ(q.empty(), false);
  auto s4 = q.try_pop();
  EXPECT_EQ(s4, true);
  EXPECT_EQ(2, *q.front());
  EXPECT_EQ(q.empty(), false);
  auto s5 = q.try_pop();
  EXPECT_EQ(s5, true);
  EXPECT_EQ(3, *q.front());
  EXPECT_EQ(q.empty(), false);
  auto s6 = q.try_pop();
  EXPECT_EQ(s6, true);
  EXPECT_EQ(q.empty(), true);
}

TEST(SpscBoundedLfQ, QWithMultiplePushAndPopWithWait) {
  cold_lf::spsc_bounded_queue<int, 8> q;
  q.wait_and_push(1);
  q.wait_and_push(2);
  q.wait_and_push(3);
  EXPECT_EQ(1, q.wait_and_front());
  EXPECT_EQ(q.empty(), false);
  q.pop();
  EXPECT_EQ(2, q.wait_and_front());
  EXPECT_EQ(q.empty(), false);
  q.pop();
  EXPECT_EQ(3, q.wait_and_front());
  EXPECT_EQ(q.empty(), false);
  q.pop();
  EXPECT_EQ(q.empty(), true);
}

TEST_F(SpscBoundedQueueDestructionTest, QDestruction) {
  static constexpr size_t QUEUE_CAPACITY = 2048;
  {
    cold_lf::spsc_bounded_queue<cold_lf::destruction, QUEUE_CAPACITY> q;
    for (auto i {0}; i < QUEUE_CAPACITY; i++) {
      auto _ = q.emplace();
    }
  }

  EXPECT_EQ(cold_lf::destruction::total_destructed_, QUEUE_CAPACITY);

}

//////////////////////////////////////////  TYPED TESTS  //////////////////////////////////////////
template<typename Tp>
void check_for_consumed_items(std::unordered_map<std::uint64_t, Tp>& consumed_items, std::uint64_t ITER) {
  for (std::uint64_t i {}; i < ITER; i++) {
    auto it = consumed_items.find(i);
    EXPECT_TRUE(it != consumed_items.end());
    if (it == consumed_items.end()) continue;
    auto data = TestValue<Tp>::make(i);
    EXPECT_TRUE(data == it->second);
  }
}

TYPED_TEST(SpscBoundedQueueTest, StressConcurrencyTestWithLValAndTry) {
  using Tp = TypeParam;
  static constexpr std::uint64_t ITER = 1024 * 16;
  // producer thread
  std::thread producer;
  std::thread consumer;
  {
    producer = std::thread([this](){
      cold_lf::pin_thread(0);
      for (std::uint64_t i {}; i < ITER; i++) {
        auto data = TestValue<Tp>::make(i);
        while (!this->q_.try_push(data));
      }
    });
  }

  // consumer thread
  {
    consumer = std::thread([this](){
      cold_lf::pin_thread(2);
      std::unordered_map<std::uint64_t, Tp> consumed_items;
      for (std::uint64_t i {}; i < ITER; i++) {
        while (this->q_.empty());
        consumed_items.emplace(i, *this->q_.front());
        EXPECT_TRUE(this->q_.try_pop());
      }

      check_for_consumed_items(consumed_items, ITER);

    });
  }
  producer.join();
  consumer.join();
}

TYPED_TEST(SpscBoundedQueueTest, StressConcurrencyTestWithLValAndWait) {
  using Tp = TypeParam;
  static constexpr std::uint64_t ITER = 1024 * 16;
  // producer thread
  std::thread producer;
  std::thread consumer;
  {
    producer = std::thread([this](){
      cold_lf::pin_thread(0);
      for (std::uint64_t i {}; i < ITER; i++) {
        auto data = TestValue<Tp>::make(i);
        this->q_.wait_and_push(data);
      }
    });
  }

  // consumer thread
  {
    consumer = std::thread([this](){
      cold_lf::pin_thread(2);
      std::unordered_map<std::uint64_t, Tp> consumed_items;
      for (std::uint64_t i {}; i < ITER; i++) {
        consumed_items.emplace(i, this->q_.wait_and_front());
        this->q_.pop();
      }

      check_for_consumed_items(consumed_items, ITER);

    });
  }
  producer.join();
  consumer.join();
}

TYPED_TEST(SpscBoundedQueueTest, StressConcurrencyTestWithRValAndTry) {
  using Tp = TypeParam;
  static constexpr std::uint64_t ITER = 1024 * 16;
  // producer thread
  std::thread producer;
  std::thread consumer;
  {
    producer = std::thread([this](){
      cold_lf::pin_thread(0);
      for (std::uint64_t i {}; i < ITER; i++) {
        auto data = TestValue<Tp>::make(i);
        while (!this->q_.try_push(std::move(data)));
      }
    });
  }

  // consumer thread
  {
    consumer = std::thread([this](){
      cold_lf::pin_thread(2);
      std::unordered_map<std::uint64_t, Tp> consumed_items;
      for (std::uint64_t i {}; i < ITER; i++) {
        while (this->q_.empty());
        consumed_items.emplace(i, std::move(*this->q_.front()));
        EXPECT_TRUE(this->q_.try_pop());
      }

      check_for_consumed_items(consumed_items, ITER);

    });
  }
  producer.join();
  consumer.join();
}

TYPED_TEST(SpscBoundedQueueTest, StressConcurrencyTestWithRValAndWait) {
  using Tp = TypeParam;
  static constexpr std::uint64_t ITER = 1024 * 16;
  // producer thread
  std::thread producer;
  std::thread consumer;
  {
    producer = std::thread([this](){
      cold_lf::pin_thread(0);
      for (std::uint64_t i {}; i < ITER; i++) {
        auto data = TestValue<Tp>::make(i);
        this->q_.wait_and_push(std::move(data));
      }
    });
  }

  // consumer thread
  {
    consumer = std::thread([this](){
      cold_lf::pin_thread(2);
      std::unordered_map<std::uint64_t, Tp> consumed_items;
      for (std::uint64_t i {}; i < ITER; i++) {
        consumed_items.emplace(i, std::move(this->q_.wait_and_front()));
        this->q_.pop();
      }

      check_for_consumed_items(consumed_items, ITER);

    });
  }
  producer.join();
  consumer.join();
}

TYPED_TEST(SpscBoundedQueueTest, StressConcurrencyTestWithEmplaceAndTry) {
  using Tp = TypeParam;
  static constexpr std::uint64_t ITER = 1024 * 16;
  // producer thread
  std::thread producer;
  std::thread consumer;
  {
    producer = std::thread([this](){
      cold_lf::pin_thread(0);
      for (std::uint64_t i {}; i < ITER; i++) {
        while (!this->q_.emplace(i));
      }
    });
  }

  // consumer thread
  {
    consumer = std::thread([this](){
      cold_lf::pin_thread(2);
      std::unordered_map<std::uint64_t, Tp> consumed_items;
      for (std::uint64_t i {}; i < ITER; i++) {
        while (this->q_.empty());
        consumed_items.emplace(i, *this->q_.front());
        EXPECT_TRUE(this->q_.try_pop());
      }

      check_for_consumed_items(consumed_items, ITER);

    });
  }
  producer.join();
  consumer.join();
}

TYPED_TEST(SpscBoundedQueueTest, StressConcurrencyTestWithEmplaceAndWait) {
  using Tp = TypeParam;
  static constexpr std::uint64_t ITER = 1024 * 16;
  // producer thread
  std::thread producer;
  std::thread consumer;
  {
    producer = std::thread([this](){
      cold_lf::pin_thread(0);
      for (std::uint64_t i {}; i < ITER; i++) {
        this->q_.wait_and_emplace(i);
      }
    });
  }

  // consumer thread
  {
    consumer = std::thread([this](){
      cold_lf::pin_thread(2);
      std::unordered_map<std::uint64_t, Tp> consumed_items;
      for (std::uint64_t i {}; i < ITER; i++) {
        consumed_items.emplace(i, this->q_.wait_and_front());
        this->q_.pop();
      }

      check_for_consumed_items(consumed_items, ITER);

    });
  }
  producer.join();
  consumer.join();
}


