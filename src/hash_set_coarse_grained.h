#ifndef HASH_SET_COARSE_GRAINED_H
#define HASH_SET_COARSE_GRAINED_H

#include <algorithm>
#include <functional>
#include <vector>
#include <mutex>
#include <iostream>

#include "src/hash_set_base.h"

template <typename T>
class HashSetCoarseGrained : public HashSetBase<T> {
 public:
  explicit HashSetCoarseGrained(size_t initial_capacity)
        : set_size_(0), table_(initial_capacity) {}

  bool Add(T elem) final {
    if (Contains(elem)) {
      return false;
    }

    std::scoped_lock<std::mutex> lock(mutex_);

    // Add element to correct bucket from hash value
    size_t bucket_index = std::hash<T>()(elem) % table_.size();
    table_[bucket_index].push_back(elem);
    set_size_.fetch_add(1);

    // Double number of buckets if resize policy is satisfied.
    if (Policy()) {
      Resize();
    }

    // std::cout << "\ntable contents:\n";
    // for (auto bucket : table_) {
    //     for (T item : bucket) {
    //         std::cout << item << "\n";
    //     }
    // }
    return true;
  }

  bool Remove(T elem) final {
    if (!Contains(elem)) {
      return false;
    }

    std::scoped_lock<std::mutex> lock(mutex_);
    
    // Remove element from correct bucket (based on hash value)
    size_t bucket_index = std::hash<T>()(elem) % table_.size();
    std::vector<T> bucket = table_[bucket_index];
    // bucket.erase(std::remove(bucket.begin(), bucket.end(), elem), bucket.end());
    table_[bucket_index].erase(std::remove(bucket.begin(), bucket.end(), elem), bucket.end());
    set_size_.fetch_sub(1);
    
    // std::cout << "\ntable contents:\n";
    // for (auto b : table_) {
    //     for (T item : b) {
    //         std::cout << item << "\n";
    //     }
    // }

    return true;
  }

  // Looks up element in correct bucket (based on hash value)
  [[nodiscard]] bool Contains(T elem) final {
    std::scoped_lock<std::mutex> lock(mutex_);
    size_t bucket_index = std::hash<T>()(elem) % table_.size();
    std::vector<T> bucket = table_[bucket_index];
    return std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
  }

  [[nodiscard]] size_t Size() const final {
    // std::cout << "getting size... ";
    return set_size_.load();
  }

 private:
  const size_t bucket_capacity_ = 4;
  std::atomic<size_t> set_size_;
  std::vector<std::vector<T>> table_;
  std::mutex mutex_; 

  // Returns true if average bucket size exceeds bucket_capacity_
  bool Policy() {
    return set_size_ / table_.size() > bucket_capacity_;
  }

  // Perform a resizing, re-hashing all elements
  void Resize() { 
    size_t new_size = table_.size() * 2;
    std::vector<std::vector<T>> new_table(new_size);

    // Copy elems to new table
    for (auto bucket : table_) {
        for (T elem : bucket) {
            size_t new_bucket_index = std::hash<T>()(elem) % new_table.size();
            new_table[new_bucket_index].push_back(elem);
        }
    } 

    // Explicitly clear old table 
    table_.clear();
    table_ = new_table;
  }
};

#endif  // HASH_SET_COARSE_GRAINED_H
