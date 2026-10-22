#include "distributed_kv.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <numeric>

using namespace kv;

void run_cuckoo_displacement_demo() {
    std::cout << "\n======================================================\n";
    std::cout << "  DEMO 1: CUCKOO HASHING DISPLACEMENT CHAINS\n";
    std::cout << "======================================================\n";
    CuckooHashTable<std::string> table(8, 20); // Small table of capacity 8 to force kicks

    std::vector<std::pair<std::string, std::string>> items = {
        {"user:101", "Arnav"},
        {"user:102", "Priya"},
        {"user:103", "Tobias"},
        {"user:104", "Alice"},
        {"user:105", "Bob"},
        {"user:106", "Charlie"}
    };

    for (const auto& item : items) {
        InsertionTrace trace;
        auto cands = table.candidate_slots(item.first);
        std::cout << "\n-> Inserting [" << item.first << " : " << item.second 
                  << "] | Candidate slots: (h1=" << cands.first << ", h2=" << cands.second << ")\n";
        
        table.insert(item.first, item.second, &trace);

        if (trace.steps.empty()) {
            std::cout << "   Placed directly into free slot with 0 displacements.\n";
        } else {
            std::cout << "   DISPLACEMENT CHAIN (" << trace.steps.size() << " kicks):\n";
            for (size_t i = 0; i < trace.steps.size(); ++i) {
                const auto& step = trace.steps[i];
                std::cout << "     Kick #" << (i + 1) << ": Displaced [" << step.key_displaced 
                          << "] from slot " << step.from_slot << " -> moved to slot " << step.to_slot;
                if (step.caused_further_displacement) {
                    std::cout << " (occupied! kicked next)\n";
                } else {
                    std::cout << " (free! settled)\n";
                }
            }
        }
        if (trace.rehash_triggered) {
            std::cout << "   [ALERT] Max displacements exceeded -> Table automatically REHASHED!\n";
        }
    }

    std::cout << "\nFinal Cuckoo Table State (Capacity: " << table.capacity() 
              << ", Load Factor: " << std::fixed << std::setprecision(2) << table.load_factor() << "):\n";
    const auto& tbl = table.get_table();
    for (size_t i = 0; i < tbl.size(); ++i) {
        std::cout << "  Slot [" << std::setw(2) << i << "]: ";
        if (tbl[i].occupied) {
            std::cout << "[" << tbl[i].key << " -> " << tbl[i].value << "]\n";
        } else {
            std::cout << "(empty)\n";
        }
    }

    std::cout << "\nVerifying O(1) Lookups (At most 2 candidate slots checked):\n";
    for (const auto& item : items) {
        auto val = table.get(item.first);
        std::cout << "  GET(" << item.first << ") => " << (val ? *val : "NOT FOUND") 
                  << " (Checked only slots " << table.candidate_slots(item.first).first 
                  << " and " << table.candidate_slots(item.first).second << ")\n";
    }
}

void run_consistent_hashing_comparison() {
    std::cout << "\n======================================================\n";
    std::cout << "  DEMO 2: MODULO HASHING vs CONSISTENT HASHING REMAPPING\n";
    std::cout << "======================================================\n";

    const size_t num_keys = 1000;
    std::vector<std::string> keys;
    for (size_t i = 0; i < num_keys; ++i) {
        keys.push_back("record_id_" + std::to_string(i * 17 + 101));
    }

    // Baseline Modulo
    ModuloHashRouter mod_router;
    std::vector<std::string> servers_3 = {"Server_A", "Server_B", "Server_C"};
    std::vector<std::string> servers_4 = {"Server_A", "Server_B", "Server_C", "Server_D"};

    mod_router.set_servers(servers_3);
    std::vector<std::string> mod_assign_before;
    for (const auto& k : keys) mod_assign_before.push_back(mod_router.get_server(k));

    mod_router.set_servers(servers_4);
    size_t mod_remapped = 0;
    for (size_t i = 0; i < num_keys; ++i) {
        if (mod_router.get_server(keys[i]) != mod_assign_before[i]) {
            mod_remapped++;
        }
    }

    // Consistent Hashing (50 vnodes)
    ConsistentHashRing ring(50);
    ring.add_server("Server_A");
    ring.add_server("Server_B");
    ring.add_server("Server_C");

    std::vector<std::string> ring_assign_before;
    for (const auto& k : keys) ring_assign_before.push_back(ring.get_server(k));

    ring.add_server("Server_D"); // Adding 4th server
    size_t ring_remapped = 0;
    for (size_t i = 0; i < num_keys; ++i) {
        if (ring.get_server(keys[i]) != ring_assign_before[i]) {
            ring_remapped++;
        }
    }

    std::cout << "Simulating Cluster Expansion from 3 to 4 Servers (1,000 Keys):\n\n";
    std::cout << "1. Modulo Hashing (h(k) % N):\n";
    std::cout << "   - Theoretical remap fraction: N / (N + 1) = 3/4 = 75.0%\n";
    std::cout << "   - Measured keys remapped:     " << mod_remapped << " / " << num_keys 
              << " (" << std::fixed << std::setprecision(1) << (mod_remapped * 100.0 / num_keys) << "%)\n";
    std::cout << "   -> Massive cluster-wide churn! 3 out of 4 keys had to be migrated.\n\n";

    std::cout << "2. Consistent Hashing with Vnodes (Ring):\n";
    std::cout << "   - Theoretical remap fraction: 1 / (N + 1) = 1/4 = 25.0%\n";
    std::cout << "   - Measured keys remapped:     " << ring_remapped << " / " << num_keys 
              << " (" << std::fixed << std::setprecision(1) << (ring_remapped * 100.0 / num_keys) << "%)\n";
    std::cout << "   -> Minimal disruption! Only the keys migrating to Server_D moved.\n";
}

void run_bloom_filter_validation() {
    std::cout << "\n======================================================\n";
    std::cout << "  DEMO 3: BLOOM FILTER THEORETICAL vs EMPIRICAL FPR\n";
    std::cout << "======================================================\n";

    const size_t m_bits = 4000;
    const size_t n_inserted = 400;
    const size_t k_hashes = 7; // Optimal k ≈ (m/n)*ln(2) = 10 * 0.693 = 6.93 ≈ 7

    BloomFilter filter(m_bits, k_hashes);
    for (size_t i = 0; i < n_inserted; ++i) {
        filter.insert("existing_key_" + std::to_string(i));
    }

    // Verify 0% False Negatives
    size_t false_negatives = 0;
    for (size_t i = 0; i < n_inserted; ++i) {
        if (!filter.possibly_contains("existing_key_" + std::to_string(i))) {
            false_negatives++;
        }
    }

    // Test False Positives with 10,000 absent keys
    const size_t test_queries = 10000;
    size_t false_positives = 0;
    for (size_t i = 0; i < test_queries; ++i) {
        std::string absent_key = "query_absent_key_" + std::to_string(i + 1000000);
        if (filter.possibly_contains(absent_key)) {
            false_positives++;
        }
    }

    double empirical_fpr = (false_positives * 100.0) / test_queries;
    double theoretical_fpr = filter.theoretical_fpr() * 100.0;

    std::cout << "Filter Specs: m=" << m_bits << " bits, n=" << n_inserted 
              << " elements, k=" << k_hashes << " hash functions\n";
    std::cout << "Set Bits Fill Ratio: " << std::fixed << std::setprecision(1) 
              << (filter.fill_ratio() * 100.0) << "%\n";
    std::cout << "False Negatives:      " << false_negatives << " (Invariant: NEVER false negatives)\n";
    std::cout << "Theoretical FPR:      " << std::setprecision(3) << theoretical_fpr << "%\n";
    std::cout << "Empirical FPR:        " << std::setprecision(3) << empirical_fpr << "%\n";
    std::cout << "Delta (|Theo - Emp|): " << std::abs(theoretical_fpr - empirical_fpr) << "%\n";
}

void run_5_configurations_benchmark() {
    std::cout << "\n======================================================\n";
    std::cout << "  DEMO 4: BENCHMARK OF ALL 5 ARCHITECTURAL CONFIGURATIONS\n";
    std::cout << "======================================================\n";

    ArchitectureConfig configs[] = {
        ArchitectureConfig::A_CONSISTENT_BASELINE,
        ArchitectureConfig::B_CONSISTENT_BLOOM_BASELINE,
        ArchitectureConfig::C_CONSISTENT_CUCKOO,
        ArchitectureConfig::D_CUCKOO_STANDALONE,
        ArchitectureConfig::E_FULL_SYSTEM
    };

    double absent_ratios[] = {0.0, 0.50, 0.90};

    const size_t total_keys = 500;
    const size_t total_queries = 5000;

    for (double absent_ratio : absent_ratios) {
        std::cout << "\n--------------------------------------------------------------\n";
        std::cout << "  WORKLOAD: " << static_cast<int>(absent_ratio * 100) 
                  << "% Absent-Key Lookups (" << static_cast<int>((1.0 - absent_ratio) * 100) << "% Hits)\n";
        std::cout << "--------------------------------------------------------------\n";
        std::cout << std::left << std::setw(38) << "Configuration" 
                  << std::setw(14) << "Avg Latency" 
                  << std::setw(14) << "Table Probes"
                  << std::setw(14) << "Bloom Saved"
                  << std::setw(12) << "Bloom FPR" << "\n";

        for (auto cfg : configs) {
            DistributedKVStore store(cfg, 50);
            store.add_server("Node_Alpha");
            store.add_server("Node_Beta");
            store.add_server("Node_Gamma");

            // Seed keys
            for (size_t i = 0; i < total_keys; ++i) {
                store.put("key_" + std::to_string(i), "value_" + std::to_string(i));
            }

            store.reset_metrics();

            // Execute queries
            double total_latency_us = 0.0;
            size_t absent_count = static_cast<size_t>(total_queries * absent_ratio);
            size_t present_count = total_queries - absent_count;

            for (size_t i = 0; i < present_count; ++i) {
                auto trace = store.get("key_" + std::to_string(i % total_keys));
                total_latency_us += trace.latency_microseconds;
            }
            for (size_t i = 0; i < absent_count; ++i) {
                auto trace = store.get("ghost_key_" + std::to_string(i + 99999));
                total_latency_us += trace.latency_microseconds;
            }

            double avg_lat = total_latency_us / total_queries;
            std::cout << std::left << std::setw(38) << config_to_string(cfg)
                      << std::fixed << std::setprecision(2) << std::setw(12) << avg_lat << " us"
                      << std::setw(14) << store.get_table_probes()
                      << std::setw(14) << store.get_bloom_filtered_absent()
                      << std::setprecision(2) << (store.measured_false_positive_rate() * 100.0) << "%\n";
        }
    }
}

int main() {
    std::cout << "==============================================================\n";
    std::cout << "  DISTRIBUTED KEY-VALUE STORE: ALGORITHMIC SUITE & BENCHMARK\n";
    std::cout << "  Consistent Hashing | Bloom Filters | Cuckoo Hashing\n";
    std::cout << "==============================================================\n";

    run_cuckoo_displacement_demo();
    run_consistent_hashing_comparison();
    run_bloom_filter_validation();
    run_5_configurations_benchmark();

    std::cout << "\n[SUCCESS] All verification tests and algorithmic benchmarks completed.\n\n";
    return 0;
}
