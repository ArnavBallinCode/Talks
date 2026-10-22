# Distributed Key-Value Store: Algorithmic Suite & Verification Benchmarks

C++17 reference implementation for an academic defense on scalable, deterministic distributed key-value storage.

## Core Components
- **`include/consistent_hash_ring.hpp`**: Circular hash ring topology with configurable virtual nodes ($v=150$) and clockwise binary search lookup ($O(\log V)$).
- **`include/cuckoo_hash.hpp`**: Local hash table with guaranteed worst-case $O(1)$ lookups (probing at most 2 candidate slots), iterative displacement kick chains, and automatic prime-modulo rehashing.
- **`include/bloom_filter.hpp`**: Space-efficient probabilistic filter using double-hashing ($h_i(k) = h_a(k) + i \cdot h_b(k)$) guaranteeing strictly 0% false negatives for absent-key short-circuiting.
- **`include/distributed_kv.hpp`**: Three-tier cluster coordination pipeline integrating the Ring, Bloom Filter, and Cuckoo storage engines across $N$ virtual servers.
- **`src/main.cpp`**: Comprehensive verification suite testing displacement loops, cluster churn remap percentages, empirical false positive rates, and comparative benchmarks across all 5 configurations.

## Quick Start
```bash
# Build with C++17 optimization and run full verification suite
make run

# Or manually:
clang++ -std=c++17 -O3 -Iinclude src/main.cpp -o kv_bench
./kv_bench
```
