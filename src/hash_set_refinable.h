#ifndef HASH_SET_REFINABLE_H
#define HASH_SET_REFINABLE_H

#include <algorithm>
#include <functional>
#include <immintrin.h>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "src/hash_set_base.h"

template <typename T> class HashSetRefinable : public HashSetBase<T> {
public:
  explicit HashSetRefinable(size_t initial_capacity)
      : set_size_(0), capacity_(initial_capacity), table_(initial_capacity) {
    // Init pointers to mutexes
    std::unique_lock<std::shared_mutex> lock(locks_mutex_);
    for (size_t i = 0; i < initial_capacity; i++) {
      mutexes_.push_back(std::make_unique<std::mutex>());
    }
  }

  bool Add(T elem) final {
    std::shared_lock<std::shared_mutex> locks_mutex(locks_mutex_);
    std::unique_lock<std::mutex> lock = Acquire(elem);

    if (Contains_(elem)) {
      return false;
    }

    // Add element to correct bucket from hash value
    size_t bucket_index = std::hash<T>()(elem) % capacity_.load();
    table_[bucket_index].push_back(elem);
    set_size_.fetch_add(1);

    /* Double number of buckets if resize policy is satisfied.
       Unlock so resize() can obtain all locks. */
    if (Policy()) {
      locks_mutex.unlock();
      lock.unlock();
      Resize();
    }

    return true;
  }

  bool Remove(T elem) final {
    std::shared_lock<std::shared_mutex> locks_mutex(locks_mutex_);
    std::unique_lock<std::mutex> lock = Acquire(elem);

    if (!Contains_(elem)) {
      return false;
    }

    // Remove element from correct bucket (based on hash value)
    size_t bucket_index = std::hash<T>()(elem) % capacity_.load();
    std::vector<T> &bucket = table_[bucket_index];
    bucket.erase(std::remove(bucket.begin(), bucket.end(), elem), bucket.end());

    // Decrements size atomically
    set_size_.fetch_sub(1);
    return true;
  }

  // Looks up element in correct bucket (based on hash value)
  [[nodiscard]] bool Contains(T elem) final {
    std::shared_lock<std::shared_mutex> locks_mutex(locks_mutex_);
    std::unique_lock<std::mutex> lock = Acquire(elem);

    return Contains_(elem);
  }

  [[nodiscard]] size_t Size() const final { return set_size_.load(); }

private:
  const size_t bucket_capacity_ = 4;
  std::atomic<size_t> set_size_;
  std::atomic<size_t> capacity_; // number of buckets
  std::vector<std::vector<T>> table_;
  mutable std::vector<std::unique_ptr<std::mutex>> mutexes_;
  std::shared_mutex locks_mutex_;

  // Returns true if average bucket size exceeds bucket_capacity_
  bool Policy() { return set_size_ / capacity_.load() > bucket_capacity_; }

  // Perform a resizing, re-hashing all elements
  void Resize() {
    size_t old_size = capacity_.load();
    std::unique_lock<std::shared_mutex> lock(locks_mutex_);
    std::vector<std::unique_lock<std::mutex>> locks = AcquireAll();

    // Return if thread has already updated the capacity
    if (old_size != capacity_.load()) {
      return;
    }

    size_t new_size = old_size * 2;
    std::vector<std::vector<T>> new_table(new_size);

    // Copy elems to new table
    for (auto bucket : table_) {
      for (T elem : bucket) {
        size_t new_bucket_index = std::hash<T>()(elem) % new_table.size();
        new_table[new_bucket_index].push_back(elem);
      }
    }

    // Add new mutexes
    for (size_t i = 0; i < old_size; i++) {
      mutexes_.push_back(std::make_unique<std::mutex>());
    }

    capacity_.store(new_size);

    // Explicitly clear old table
    table_.clear();
    table_ = new_table;
  }

  // Private contains - Always lock before calling
  bool Contains_(T elem) {
    size_t bucket_index = std::hash<T>()(elem) % capacity_.load();
    std::vector<T> &bucket = table_[bucket_index];
    return std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
  }

  // Lock with RAII
  std::unique_lock<std::mutex> Acquire(T x) {
    std::unique_lock<std::mutex> lock(
        *mutexes_[std::hash<T>()(x) % mutexes_.size()]);

    // Transfer ownership to caller
    return lock;
  }

  // Lock all mutexes consecutively with RAII
  std::vector<std::unique_lock<std::mutex>> AcquireAll() {
    std::vector<std::unique_lock<std::mutex>> locks;
    for (auto &mutex : mutexes_) {
      locks.push_back(std::unique_lock<std::mutex>(*mutex));
    }

    // Ownership transferred to caller
    return locks;
  }
};

#endif // HASH_SET_REFINABLE_H
