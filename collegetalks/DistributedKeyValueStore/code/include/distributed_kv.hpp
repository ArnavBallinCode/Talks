#pragma once

#include "bloom_filter.hpp"
#include "cuckoo_hash.hpp"
#include "baseline_hash.hpp"
#include "consistent_hash_ring.hpp"

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <iostream>

namespace kv {

enum class ArchitectureConfig {
    A_CONSISTENT_BASELINE,        // Ring + Baseline Table
    B_CONSISTENT_BLOOM_BASELINE,  // Ring + Bloom Filter + Baseline Table
    C_CONSISTENT_CUCKOO,          // Ring + Cuckoo Table
    D_CUCKOO_STANDALONE,          // Cuckoo Table alone (no ring)
    E_FULL_SYSTEM                 // Ring + Bloom Filter + Cuckoo Table (Complete)
};

inline const char* config_to_string(ArchitectureConfig cfg) {
    switch (cfg) {
        case ArchitectureConfig::A_CONSISTENT_BASELINE: return "Config A: Ring + Baseline Hash";
        case ArchitectureConfig::B_CONSISTENT_BLOOM_BASELINE: return "Config B: Ring + Bloom + Baseline Hash";
        case ArchitectureConfig::C_CONSISTENT_CUCKOO: return "Config C: Ring + Cuckoo Hash";
        case ArchitectureConfig::D_CUCKOO_STANDALONE: return "Config D: Standalone Cuckoo Hash (Control)";
        case ArchitectureConfig::E_FULL_SYSTEM: return "Config E: Full System (Ring + Bloom + Cuckoo)";
    }
    return "Unknown";
}

struct TraceResult {
    std::string key;
    std::string target_server;
    uint32_t key_token = 0;
    uint32_t server_token = 0;
    bool bloom_checked = false;
    bool bloom_hit = false;
    bool bloom_short_circuited = false;
    bool table_probed = false;
    bool key_found = false;
    bool false_positive_bloom = false;
    std::string retrieved_value;
    double latency_microseconds = 0.0;
};

/**
 * Single Server Node in the cluster.
 */
struct ServerNode {
    std::string id;
    BloomFilter bloom;
    CuckooHashTable<std::string> cuckoo_table;
    BaselineHashTable<std::string> baseline_table;

    explicit ServerNode(const std::string& node_id, size_t bloom_bits = 4096, size_t bloom_k = 7, size_t cuckoo_cap = 64)
        : id(node_id),
          bloom(bloom_bits, bloom_k),
          cuckoo_table(cuckoo_cap),
          baseline_table(cuckoo_cap) {}
};

/**
 * Distributed Key-Value Store Simulator.
 */
class DistributedKVStore {
private:
    ArchitectureConfig config_;
    ConsistentHashRing ring_;
    std::map<std::string, std::shared_ptr<ServerNode>> servers_;

    // Standalone server for Config D
    std::shared_ptr<ServerNode> standalone_node_;

    // Global performance counters
    size_t total_gets_ = 0;
    size_t bloom_filtered_absent_ = 0;
    size_t bloom_false_positives_ = 0;
    size_t table_probes_ = 0;
    size_t key_hits_ = 0;
    size_t key_misses_ = 0;

public:
    explicit DistributedKVStore(ArchitectureConfig config = ArchitectureConfig::E_FULL_SYSTEM, size_t vnodes = 50)
        : config_(config), ring_(vnodes) {
        standalone_node_ = std::make_shared<ServerNode>("Standalone-S0");
    }

    void set_config(ArchitectureConfig config) { config_ = config; }
    ArchitectureConfig get_config() const { return config_; }

    void add_server(const std::string& server_id) {
        if (servers_.find(server_id) != servers_.end()) return;
        servers_[server_id] = std::make_shared<ServerNode>(server_id);
        ring_.add_server(server_id);
    }

    void remove_server(const std::string& server_id) {
        servers_.erase(server_id);
        ring_.remove_server(server_id);
    }

    const ConsistentHashRing& ring() const { return ring_; }

    /**
     * PUT(key, value)
     */
    bool put(const std::string& key, const std::string& value) {
        if (config_ == ArchitectureConfig::D_CUCKOO_STANDALONE) {
            return standalone_node_->cuckoo_table.insert(key, value);
        }

        std::string server_id = ring_.get_server(key);
        if (server_id.empty()) return false;
        auto node = servers_[server_id];

        // 1. Update Bloom Filter if config uses it
        if (config_ == ArchitectureConfig::B_CONSISTENT_BLOOM_BASELINE ||
            config_ == ArchitectureConfig::E_FULL_SYSTEM) {
            node->bloom.insert(key);
        }

        // 2. Insert into appropriate local storage table
        if (config_ == ArchitectureConfig::C_CONSISTENT_CUCKOO ||
            config_ == ArchitectureConfig::E_FULL_SYSTEM) {
            return node->cuckoo_table.insert(key, value);
        } else {
            node->baseline_table.insert(key, value);
            return true;
        }
    }

    /**
     * GET(key) with step-by-step diagnostic trace.
     */
    TraceResult get(const std::string& key) {
        auto start = std::chrono::high_resolution_clock::now();
        TraceResult trace;
        trace.key = key;
        total_gets_++;

        // Case: Config D (Standalone)
        if (config_ == ArchitectureConfig::D_CUCKOO_STANDALONE) {
            trace.target_server = "Standalone-S0";
            trace.table_probed = true;
            table_probes_++;
            auto res = standalone_node_->cuckoo_table.get(key);
            if (res.has_value()) {
                trace.key_found = true;
                trace.retrieved_value = *res;
                key_hits_++;
            } else {
                trace.key_found = false;
                key_misses_++;
            }
            auto end = std::chrono::high_resolution_clock::now();
            trace.latency_microseconds = std::chrono::duration<double, std::micro>(end - start).count();
            return trace;
        }

        // 1. Consistent Hashing Ring Routing
        std::string server_id = ring_.get_server(key, &trace.key_token, &trace.server_token);
        trace.target_server = server_id;

        if (server_id.empty() || servers_.find(server_id) == servers_.end()) {
            trace.key_found = false;
            return trace;
        }

        auto node = servers_[server_id];

        // 2. Bloom Filter Check (Configs B & E)
        bool use_bloom = (config_ == ArchitectureConfig::B_CONSISTENT_BLOOM_BASELINE ||
                          config_ == ArchitectureConfig::E_FULL_SYSTEM);

        if (use_bloom) {
            trace.bloom_checked = true;
            bool bloom_present = node->bloom.possibly_contains(key);
            trace.bloom_hit = bloom_present;

            if (!bloom_present) {
                // Short circuit! Key is DEFINITELY ABSENT. Skip table lookup!
                trace.bloom_short_circuited = true;
                trace.table_probed = false;
                trace.key_found = false;
                bloom_filtered_absent_++;
                key_misses_++;
                auto end = std::chrono::high_resolution_clock::now();
                trace.latency_microseconds = std::chrono::duration<double, std::micro>(end - start).count();
                return trace;
            }
        }

        // 3. Local Hash Table Lookup
        trace.table_probed = true;
        table_probes_++;

        bool use_cuckoo = (config_ == ArchitectureConfig::C_CONSISTENT_CUCKOO ||
                           config_ == ArchitectureConfig::E_FULL_SYSTEM);

        std::optional<std::string> val;
        if (use_cuckoo) {
            val = node->cuckoo_table.get(key);
        } else {
            val = node->baseline_table.get(key);
        }

        if (val.has_value()) {
            trace.key_found = true;
            trace.retrieved_value = *val;
            key_hits_++;
        } else {
            trace.key_found = false;
            key_misses_++;
            if (use_bloom && trace.bloom_hit) {
                // Bloom Filter said possibly present, but table proved absent -> False Positive!
                trace.false_positive_bloom = true;
                bloom_false_positives_++;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        trace.latency_microseconds = std::chrono::duration<double, std::micro>(end - start).count();
        return trace;
    }

    // Diagnostics & Metrics
    void reset_metrics() {
        total_gets_ = 0;
        bloom_filtered_absent_ = 0;
        bloom_false_positives_ = 0;
        table_probes_ = 0;
        key_hits_ = 0;
        key_misses_ = 0;
    }

    size_t get_total_gets() const { return total_gets_; }
    size_t get_bloom_filtered_absent() const { return bloom_filtered_absent_; }
    size_t get_bloom_false_positives() const { return bloom_false_positives_; }
    size_t get_table_probes() const { return table_probes_; }
    size_t get_key_hits() const { return key_hits_; }
    size_t get_key_misses() const { return key_misses_; }

    double measured_false_positive_rate() const {
        size_t absent_queries = bloom_filtered_absent_ + bloom_false_positives_;
        if (absent_queries == 0) return 0.0;
        return static_cast<double>(bloom_false_positives_) / static_cast<double>(absent_queries);
    }
};

} // namespace kv
