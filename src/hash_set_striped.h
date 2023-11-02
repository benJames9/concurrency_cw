 #ifndef HASH_SET_STRIPED_H
#define HASH_SET_STRIPED_H

#include <algorithm>
#include <functional>
#include <vector>
#include <mutex>
#include <iostream>

#include "src/hash_set_base.h"

template <typename T>
class HashSetStriped : public HashSetBase<T> {
 public:
  explicit HashSetStriped(size_t initial_capacity)
        : set_size_(0), capacity_(initial_capacity), table_(initial_capacity), mutexes_(initial_capacity) {}

  bool Add(T elem) final {
    Acquire(elem);

    if (Contains_(elem)) {
      Release(elem);
      return false;
    }

    // Add element to correct bucket from hash value
    size_t bucket_index = std::hash<T>()(elem) % capacity_.load();
    table_[bucket_index].push_back(elem);
    set_size_.fetch_add(1);

    // Double number of buckets if resize policy is satisfied.
    if (Policy()) {
	    Release(elem);
      Resize();
    } else {
	    Release(elem);
		}

    return true;
  }

  bool Remove(T elem) final {
    Acquire(elem);

    if (!Contains_(elem)) {
    	Release(elem);
      return false;
    }
    
    // Remove element from correct bucket (based on hash value)
    size_t bucket_index = std::hash<T>()(elem) % capacity_.load();
    std::vector<T>& bucket = table_[bucket_index];
    bucket.erase(std::remove(bucket.begin(), bucket.end(), elem), bucket.end());
    // Decrements size atomically
    set_size_.fetch_sub(1);
		Release(elem);
    
    return true;
  }

  // Looks up element in correct bucket (based on hash value)
  [[nodiscard]] bool Contains(T elem) final {
		Acquire(elem);
		bool result = Contains_(elem);
		Release(elem);
		return result;
  }

  [[nodiscard]] size_t Size() const final {
    return set_size_.load();
  }

 private:
  const size_t bucket_capacity_ = 4;
  std::atomic<size_t> set_size_;
	std::atomic<size_t> capacity_; // number of buckets
  std::vector<std::vector<T>> table_;
  mutable std::vector<std::mutex> mutexes_;
 

  // Returns true if average bucket size exceeds bucket_capacity_
  bool Policy() {
	  return set_size_ / capacity_.load() > bucket_capacity_;
  }

  // Perform a resizing, re-hashing all elements
  void Resize() { 
		size_t old_size = capacity_.load();
		AcquireAll();

		// Return if thread has already updated the capacity 
		if (old_size != capacity_.load()) {
			ReleaseAll();
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
		capacity_.store(new_size);

    // Explicitly clear old table 
    table_.clear();
    table_ = new_table;
    ReleaseAll();
  }

  bool Contains_(T elem) {
    size_t bucket_index = std::hash<T>()(elem) % capacity_.load();
    std::vector<T>& bucket = table_[bucket_index];
    return std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
  }

  void Acquire(T x) {
    mutexes_[std::hash<T>()(x) % mutexes_.size()].lock();
  }

  void Release(T x) {
    mutexes_[std::hash<T>()(x) % mutexes_.size()].unlock();
  }

  void AcquireAll() {
    for (size_t i = 0; i < mutexes_.size(); i++) {
      mutexes_[i].lock();
    }
  }

  void ReleaseAll() {
    for (size_t i = 1; i <= mutexes_.size(); i++) {
      mutexes_[mutexes_.size() - i].unlock();
    }
  }
};

#endif  // HASH_SET_STRIPED_H
