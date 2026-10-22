#pragma once

#include <vector>
#include <string>
#include <optional>
#include <iostream>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace kv {

/**
 * Step in a Cuckoo displacement chain for educational visualization and logging.
 */
struct DisplacementStep {
    std::string key_displaced;
    size_t from_slot;
    size_t to_slot;
    bool caused_further_displacement;
};

struct InsertionTrace {
    std::string initial_key;
    bool success;
    bool rehash_triggered;
    size_t total_displacements;
    std::vector<DisplacementStep> steps;
};

/**
 * Cuckoo Hash Table with 2 Candidate Slots per Key.
 *
 * Guarantees:
 * - Lookup: WORST-CASE O(1) time! At most 2 candidate slots inspected.
 * - Deletion: WORST-CASE O(1) time.
 * - Insertion: Expected O(1) amortized, with cuckoo displacement chain.
 */
template <typename ValueType = std::string>
class CuckooHashTable {
public:
    struct Entry {
        std::string key;
        ValueType value;
        bool occupied = false;
    };

private:
    size_t capacity_;
    size_t size_;
    size_t max_displacements_;
    std::vector<Entry> table_;

    // Seeds for independent hash functions
    uint64_t seed1_;
    uint64_t seed2_;

    // Performance metrics
    size_t total_insertions_ = 0;
    size_t total_displacements_ = 0;
    size_t max_displacements_observed_ = 0;
    size_t rehash_count_ = 0;
    mutable size_t lookup_probes_count_ = 0;

    static uint64_t hash_murmur(const std::string& key, uint64_t seed) {
        uint64_t h = seed ^ (key.length() * 0x5bd1e995ULL);
        for (char c : key) {
            uint64_t k = static_cast<uint8_t>(c);
            k *= 0xcc9e2d51ULL;
            k = (k << 15) | (k >> 49);
            k *= 0x1b873593ULL;
            h ^= k;
            h = (h << 13) | (h >> 51);
            h = h * 5 + 0xe6546b64ULL;
        }
        h ^= h >> 16;
        h *= 0x85ebca6bULL;
        h ^= h >> 13;
        return h;
    }

    size_t h1(const std::string& key) const {
        return static_cast<size_t>(hash_murmur(key, seed1_) % capacity_);
    }

    size_t h2(const std::string& key) const {
        return static_cast<size_t>(hash_murmur(key, seed2_) % capacity_);
    }

    size_t alt_slot(const std::string& key, size_t current_slot) const {
        size_t s1 = h1(key);
        size_t s2 = h2(key);
        return (current_slot == s1) ? s2 : s1;
    }

public:
    explicit CuckooHashTable(size_t initial_capacity = 16, size_t max_displacements = 50)
        : capacity_(std::max<size_t>(initial_capacity, 8)),
          size_(0),
          max_displacements_(max_displacements),
          table_(capacity_),
          seed1_(0x736f6d6570736575ULL),
          seed2_(0x646f72616e646f6dULL) {
        if (max_displacements_ == 0) {
            max_displacements_ = std::max<size_t>(50, 4 * static_cast<size_t>(std::log2(capacity_)));
        }
    }

    size_t capacity() const { return capacity_; }
    size_t size() const { return size_; }
    double load_factor() const { return static_cast<double>(size_) / static_cast<double>(capacity_); }

    std::pair<size_t, size_t> candidate_slots(const std::string& key) const {
        return {h1(key), h2(key)};
    }

    /**
     * WORST-CASE O(1) LOOKUP!
     * Directly checks slot h1(key). If not there, checks slot h2(key).
     * At most 2 memory reads. Never chains.
     */
    std::optional<ValueType> get(const std::string& key) const {
        lookup_probes_count_++;
        size_t pos1 = h1(key);
        if (table_[pos1].occupied && table_[pos1].key == key) {
            return table_[pos1].value;
        }

        size_t pos2 = h2(key);
        if (table_[pos2].occupied && table_[pos2].key == key) {
            return table_[pos2].value;
        }

        return std::nullopt;
    }

    bool contains(const std::string& key) const {
        return get(key).has_value();
    }

    /**
     * WORST-CASE O(1) DELETION!
     */
    bool erase(const std::string& key) {
        size_t pos1 = h1(key);
        if (table_[pos1].occupied && table_[pos1].key == key) {
            table_[pos1].occupied = false;
            table_[pos1].key.clear();
            table_[pos1].value = ValueType();
            size_--;
            return true;
        }

        size_t pos2 = h2(key);
        if (table_[pos2].occupied && table_[pos2].key == key) {
            table_[pos2].occupied = false;
            table_[pos2].key.clear();
            table_[pos2].value = ValueType();
            size_--;
            return true;
        }

        return false;
    }

    /**
     * Insert with step-by-step cuckoo displacement tracking.
     */
    bool insert(const std::string& key, const ValueType& val, InsertionTrace* trace_out = nullptr) {
        total_insertions_++;

        InsertionTrace local_trace;
        local_trace.initial_key = key;
        local_trace.success = false;
        local_trace.rehash_triggered = false;
        local_trace.total_displacements = 0;

        // Check if key already exists in either candidate slot (update value)
        size_t pos1 = h1(key);
        if (table_[pos1].occupied && table_[pos1].key == key) {
            table_[pos1].value = val;
            local_trace.success = true;
            if (trace_out) *trace_out = local_trace;
            return true;
        }
        size_t pos2 = h2(key);
        if (table_[pos2].occupied && table_[pos2].key == key) {
            table_[pos2].value = val;
            local_trace.success = true;
            if (trace_out) *trace_out = local_trace;
            return true;
        }

        // If either candidate slot is completely free, insert immediately without displacement
        if (!table_[pos1].occupied) {
            table_[pos1] = {key, val, true};
            size_++;
            local_trace.success = true;
            if (trace_out) *trace_out = local_trace;
            return true;
        }
        if (!table_[pos2].occupied) {
            table_[pos2] = {key, val, true};
            size_++;
            local_trace.success = true;
            if (trace_out) *trace_out = local_trace;
            return true;
        }

        // Both candidate slots are occupied!
        // Start the Cuckoo displacement chain (kicking out existing entries).
        std::string cur_key = key;
        ValueType cur_val = val;
        size_t cur_pos = pos1; // Start by displacing slot 1

        for (size_t kick = 0; kick < max_displacements_; ++kick) {
            // Displace resident
            std::string displaced_key = table_[cur_pos].key;
            ValueType displaced_val = table_[cur_pos].value;

            // Place incoming item
            table_[cur_pos] = {cur_key, cur_val, true};

            total_displacements_++;
            local_trace.total_displacements++;

            // Find destination for displaced resident
            size_t next_pos = alt_slot(displaced_key, cur_pos);

            DisplacementStep step;
            step.key_displaced = displaced_key;
            step.from_slot = cur_pos;
            step.to_slot = next_pos;
            step.caused_further_displacement = table_[next_pos].occupied;
            local_trace.steps.push_back(step);

            if (!table_[next_pos].occupied) {
                // Landed in free slot! Chain resolved successfully.
                table_[next_pos] = {displaced_key, displaced_val, true};
                size_++;
                local_trace.success = true;
                max_displacements_observed_ = std::max(max_displacements_observed_, kick + 1);
                if (trace_out) *trace_out = local_trace;
                return true;
            }

            // Continue the kick chain
            cur_key = displaced_key;
            cur_val = displaced_val;
            cur_pos = next_pos;
        }

        // Cycle detected or displacement threshold exceeded -> Rehash!
        local_trace.rehash_triggered = true;
        rehash_count_++;
        rehash(capacity_ * 2);

        // Retry inserting current unseated item
        bool res = insert(cur_key, cur_val, nullptr);
        local_trace.success = res;
        if (trace_out) *trace_out = local_trace;
        return res;
    }

    void rehash(size_t new_capacity = 0) {
        if (new_capacity == 0) new_capacity = capacity_ * 2;
        std::vector<Entry> old_table = std::move(table_);
        capacity_ = new_capacity;
        table_.assign(capacity_, Entry{});
        size_ = 0;

        // Perturb hash seeds slightly to break topological cycles
        seed1_ ^= 0x517cc1b727220a95ULL;
        seed2_ ^= 0x9e3779b97f4a7c15ULL;
        max_displacements_ = std::max<size_t>(50, 4 * static_cast<size_t>(std::log2(capacity_)));

        for (const auto& entry : old_table) {
            if (entry.occupied) {
                insert(entry.key, entry.value, nullptr);
            }
        }
    }

    // Inspection
    const std::vector<Entry>& get_table() const { return table_; }
    size_t get_total_displacements() const { return total_displacements_; }
    size_t get_max_displacements_observed() const { return max_displacements_observed_; }
    size_t get_rehash_count() const { return rehash_count_; }
};

} // namespace kv
