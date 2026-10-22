#pragma once

#include <string>
#include <map>
#include <vector>
#include <set>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <cmath>
#include <iostream>

namespace kv {

/**
 * Consistent Hash Ring with Virtual Nodes (vnodes).
 *
 * Guarantees:
 * - Expected key remapping on adding server (N -> N+1): E[Δ] ≈ 1 / (N + 1).
 *   (Compared to naive modulo hashing which remaps ≈ N / (N + 1) of all keys!)
 * - Lookup time: O(log V) where V = N * vnodes.
 * - Uniform load balance: Standard deviation of keys per server scales as O(1 / sqrt(vnodes)).
 */
class ConsistentHashRing {
public:
    using Token = uint32_t;
    using ServerId = std::string;

private:
    size_t vnodes_per_server_;
    // Sorted map representing the circular ring: Token -> ServerId
    std::map<Token, ServerId> ring_;
    std::set<ServerId> physical_servers_;

    // Murmur-style 32-bit hash
    static Token hash_fn(const std::string& key) {
        uint32_t h = 0x811c9dc5;
        for (char c : key) {
            h ^= static_cast<uint8_t>(c);
            h *= 0x01000193;
        }
        // Additional mixing
        h ^= h >> 16;
        h *= 0x85ebca6b;
        h ^= h >> 13;
        h *= 0xc2b2ae35;
        h ^= h >> 16;
        return h;
    }

public:
    explicit ConsistentHashRing(size_t vnodes = 50)
        : vnodes_per_server_(std::max<size_t>(1, vnodes)) {}

    void set_vnodes(size_t vnodes) {
        vnodes_per_server_ = std::max<size_t>(1, vnodes);
        rebuild_ring();
    }

    size_t get_vnodes() const { return vnodes_per_server_; }
    size_t total_ring_tokens() const { return ring_.size(); }
    size_t physical_server_count() const { return physical_servers_.size(); }
    const std::set<ServerId>& get_servers() const { return physical_servers_; }

    void add_server(const ServerId& server) {
        if (physical_servers_.find(server) != physical_servers_.end()) {
            return;
        }
        physical_servers_.insert(server);
        for (size_t i = 0; i < vnodes_per_server_; ++i) {
            std::string vnode_id = server + "#vn" + std::to_string(i);
            Token token = hash_fn(vnode_id);
            ring_[token] = server;
        }
    }

    void remove_server(const ServerId& server) {
        if (physical_servers_.find(server) == physical_servers_.end()) {
            return;
        }
        physical_servers_.erase(server);
        for (size_t i = 0; i < vnodes_per_server_; ++i) {
            std::string vnode_id = server + "#vn" + std::to_string(i);
            Token token = hash_fn(vnode_id);
            ring_.erase(token);
        }
    }

    /**
     * SUCCESSOR LOOKUP: O(log V)
     * Walks clockwise to find the first token >= hash(key).
     * If at the end, wraps around to ring_.begin().
     */
    ServerId get_server(const std::string& key, Token* out_key_token = nullptr, Token* out_server_token = nullptr) const {
        if (ring_.empty()) return "";

        Token key_token = hash_fn(key);
        if (out_key_token) *out_key_token = key_token;

        // Binary search for first token >= key_token
        auto it = ring_.lower_bound(key_token);
        if (it == ring_.end()) {
            // Cyclic wrap-around to the first node on the ring
            it = ring_.begin();
        }

        if (out_server_token) *out_server_token = it->first;
        return it->second;
    }

    // Diagnostics: Inspect ring distribution
    std::map<ServerId, size_t> simulate_key_distribution(const std::vector<std::string>& keys) const {
        std::map<ServerId, size_t> counts;
        for (const auto& s : physical_servers_) counts[s] = 0;
        for (const auto& k : keys) {
            ServerId target = get_server(k);
            if (!target.empty()) counts[target]++;
        }
        return counts;
    }

    std::vector<std::pair<Token, ServerId>> get_ring_tokens() const {
        return std::vector<std::pair<Token, ServerId>>(ring_.begin(), ring_.end());
    }

private:
    void rebuild_ring() {
        ring_.clear();
        for (const auto& server : physical_servers_) {
            for (size_t i = 0; i < vnodes_per_server_; ++i) {
                std::string vnode_id = server + "#vn" + std::to_string(i);
                Token token = hash_fn(vnode_id);
                ring_[token] = server;
            }
        }
    }
};

/**
 * Standard Modulo Hashing for direct comparison:
 *   server = hash(key) % N
 */
class ModuloHashRouter {
private:
    std::vector<std::string> servers_;

    static uint32_t hash_fn(const std::string& key) {
        uint32_t h = 0x811c9dc5;
        for (char c : key) {
            h ^= static_cast<uint8_t>(c);
            h *= 0x01000193;
        }
        return h;
    }

public:
    void set_servers(const std::vector<std::string>& servers) {
        servers_ = servers;
    }

    std::string get_server(const std::string& key) const {
        if (servers_.empty()) return "";
        size_t idx = hash_fn(key) % servers_.size();
        return servers_[idx];
    }
};

} // namespace kv
