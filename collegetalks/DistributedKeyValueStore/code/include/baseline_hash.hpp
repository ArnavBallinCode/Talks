#pragma once

#include <vector>
#include <list>
#include <string>
#include <optional>
#include <utility>

namespace kv {

/**
 * Baseline Hash Table using Separate Chaining.
 * Used for rigorous benchmark comparison against Cuckoo Hashing.
 */
template <typename ValueType = std::string>
class BaselineHashTable {
private:
    struct Node {
        std::string key;
        ValueType value;
    };

    size_t capacity_;
    size_t size_;
    std::vector<std::list<Node>> buckets_;

    size_t hash_key(const std::string& key) const {
        // Standard FNV-1a hash
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (char c : key) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 0x100000001b3ULL;
        }
        return static_cast<size_t>(hash % capacity_);
    }

public:
    explicit BaselineHashTable(size_t initial_capacity = 16)
        : capacity_(std::max<size_t>(initial_capacity, 8)),
          size_(0),
          buckets_(capacity_) {}

    size_t capacity() const { return capacity_; }
    size_t size() const { return size_; }
    double load_factor() const { return static_cast<double>(size_) / static_cast<double>(capacity_); }

    void insert(const std::string& key, const ValueType& val) {
        if (load_factor() > 0.75) {
            rehash(capacity_ * 2);
        }

        size_t idx = hash_key(key);
        for (auto& node : buckets_[idx]) {
            if (node.key == key) {
                node.value = val;
                return;
            }
        }
        buckets_[idx].push_back({key, val});
        size_++;
    }

    std::optional<ValueType> get(const std::string& key) const {
        size_t idx = hash_key(key);
        for (const auto& node : buckets_[idx]) {
            if (node.key == key) {
                return node.value;
            }
        }
        return std::nullopt;
    }

    bool erase(const std::string& key) {
        size_t idx = hash_key(key);
        auto& list = buckets_[idx];
        for (auto it = list.begin(); it != list.end(); ++it) {
            if (it->key == key) {
                list.erase(it);
                size_--;
                return true;
            }
        }
        return false;
    }

    void rehash(size_t new_capacity) {
        std::vector<std::list<Node>> old_buckets = std::move(buckets_);
        capacity_ = new_capacity;
        buckets_.assign(capacity_, std::list<Node>{});
        size_ = 0;

        for (const auto& bucket : old_buckets) {
            for (const auto& node : bucket) {
                insert(node.key, node.value);
            }
        }
    }
};

} // namespace kv
