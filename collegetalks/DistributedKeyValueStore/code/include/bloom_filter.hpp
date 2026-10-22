#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <functional>
#include <iostream>

namespace kv {

/**
 * Modern Bloom Filter implementing double-hashing (Kirsch-Mitzenmacher optimization).
 * Given two independent hash functions h1 and h2, k hash functions are generated as:
 *   g_i(x) = (h1(x) + i * h2(x) + i * i) % m
 *
 * Guarantees:
 * - False Negative Rate: EXACTLY ZERO.
 * - False Positive Rate: p ≈ (1 - e^(-kn/m))^k.
 */
class BloomFilter {
private:
    size_t m_bits;         // Size of bit vector (m)
    size_t k_hashes;       // Number of hash functions (k)
    size_t inserted_count; // Number of inserted elements (n)
    std::vector<bool> bitset;

    // FNV-1a 64-bit hash
    static uint64_t fnv1a(const std::string& key, uint64_t seed = 0xcbf29ce484222325ULL) {
        uint64_t hash = seed;
        for (char c : key) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 0x100000001b3ULL;
        }
        return hash;
    }

    // Murmur-inspired mixing 64-bit hash
    static uint64_t mix_hash(const std::string& key, uint64_t seed = 0x9e3779b97f4a7c15ULL) {
        uint64_t h = seed ^ (key.length() * 0x517cc1b727220a95ULL);
        for (char c : key) {
            h ^= static_cast<uint8_t>(c);
            h = (h << 31) | (h >> 33);
            h *= 0xff51afd7ed558ccdULL;
        }
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        return h;
    }

    std::vector<size_t> get_hashes(const std::string& key) const {
        std::vector<size_t> indices;
        indices.reserve(k_hashes);
        uint64_t h1 = fnv1a(key);
        uint64_t h2 = mix_hash(key);

        for (size_t i = 0; i < k_hashes; ++i) {
            // Enhanced double hashing: (h1 + i * h2 + i^2) % m
            uint64_t combined = h1 + i * h2 + i * i;
            indices.push_back(static_cast<size_t>(combined % m_bits));
        }
        return indices;
    }

public:
    BloomFilter(size_t bits = 1024, size_t hashes = 4)
        : m_bits(std::max<size_t>(bits, 8)),
          k_hashes(std::max<size_t>(hashes, 1)),
          inserted_count(0),
          bitset(m_bits, false) {}

    // Factory method for target capacity and target false positive rate
    static BloomFilter create_optimal(size_t expected_elements, double target_fpr) {
        if (target_fpr <= 0.0) target_fpr = 0.01;
        if (target_fpr >= 1.0) target_fpr = 0.5;
        if (expected_elements == 0) expected_elements = 100;

        // Formula: m = - (n * ln(p)) / (ln 2)^2
        double ln2 = std::log(2.0);
        double m = -static_cast<double>(expected_elements) * std::log(target_fpr) / (ln2 * ln2);
        size_t bits = static_cast<size_t>(std::ceil(m));

        // Formula: k = (m / n) * ln 2
        double k = (static_cast<double>(bits) / static_cast<double>(expected_elements)) * ln2;
        size_t hashes = std::max<size_t>(1, static_cast<size_t>(std::round(k)));

        return BloomFilter(bits, hashes);
    }

    void insert(const std::string& key) {
        auto indices = get_hashes(key);
        for (size_t idx : indices) {
            bitset[idx] = true;
        }
        inserted_count++;
    }

    /**
     * Test membership.
     * Returns false: Key is DEFINITELY ABSENT. (0% False Negatives)
     * Returns true: Key is POSSIBLY PRESENT. (May be a False Positive)
     */
    bool possibly_contains(const std::string& key) const {
        auto indices = get_hashes(key);
        for (size_t idx : indices) {
            if (!bitset[idx]) {
                return false; // Found a 0-bit -> Definitely absent!
            }
        }
        return true; // All bits 1 -> Possibly present
    }

    // Diagnostics & Educational helpers
    std::vector<size_t> query_probe_details(const std::string& key, std::vector<bool>& bit_values) const {
        auto indices = get_hashes(key);
        bit_values.clear();
        for (size_t idx : indices) {
            bit_values.push_back(bitset[idx]);
        }
        return indices;
    }

    size_t get_bits() const { return m_bits; }
    size_t get_hashes_count() const { return k_hashes; }
    size_t get_inserted_count() const { return inserted_count; }

    size_t count_set_bits() const {
        size_t c = 0;
        for (bool b : bitset) if (b) c++;
        return c;
    }

    double fill_ratio() const {
        return static_cast<double>(count_set_bits()) / static_cast<double>(m_bits);
    }

    /**
     * Theoretical false positive rate formula:
     * p ≈ (1 - e^(-k * n / m))^k
     */
    double theoretical_fpr() const {
        if (m_bits == 0) return 1.0;
        double kn_over_m = (static_cast<double>(k_hashes * inserted_count)) / static_cast<double>(m_bits);
        double term = 1.0 - std::exp(-kn_over_m);
        return std::pow(term, static_cast<double>(k_hashes));
    }

    void clear() {
        std::fill(bitset.begin(), bitset.end(), false);
        inserted_count = 0;
    }
};

} // namespace kv
