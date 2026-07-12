#include "radix256.hpp"
#include "radix256dense.hpp"
#include "pradix256dense.hpp"
#include "radixhash.hpp"

#include <iostream>
#include <vector>
#include <cassert>
#include <chrono>
#include <random>
#include <iomanip>
#include <string>
#include <algorithm>
#include <set>
#include <unordered_set>


#ifndef NODE_TYPE
# warning "NODE_TYPE not specified"
# define NODE_TYPE pidhii::radix256dense_node
#endif

// Benchmark utilities
class BenchmarkTimer {
public:
  using Clock = std::chrono::high_resolution_clock;
  using TimePoint = Clock::time_point;
  using Duration = std::chrono::duration<double, std::micro>;

  void start() {
    start_time = Clock::now();
  }

  double elapsed_microseconds() {
    auto end_time = Clock::now();
    return std::chrono::duration_cast<Duration>(end_time - start_time).count();
  }

private:
  TimePoint start_time;
};

// Generate random strings for testing
std::vector<std::string> generate_random_strings(size_t count, size_t min_len, size_t max_len, unsigned seed = 42) {
  std::mt19937 rng(seed);
  std::uniform_int_distribution<size_t> len_dist(min_len, max_len);
  std::uniform_int_distribution<char> char_dist('a', 'z');
  
  std::vector<std::string> strings;
  strings.reserve(count);
  
  for (size_t i = 0; i < count; ++i) {
    size_t len = len_dist(rng);
    std::string str;
    str.reserve(len);
    for (size_t j = 0; j < len; ++j) {
      str += char_dist(rng);
    }
    strings.push_back(std::move(str));
  }
  
  return strings;
}

// Generate strings with common prefixes for realistic testing
std::vector<std::string> generate_prefixed_strings(size_t count, unsigned seed = 42) {
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> prefix_dist(0, 4);
  std::uniform_int_distribution<size_t> suffix_len_dist(5, 15);
  std::uniform_int_distribution<char> char_dist('a', 'z');
  
  std::vector<std::string> prefixes = {"user_", "data_", "config_", "temp_", "cache_"};
  std::vector<std::string> strings;
  strings.reserve(count);
  
  for (size_t i = 0; i < count; ++i) {
    std::string str = prefixes[prefix_dist(rng)];
    size_t suffix_len = suffix_len_dist(rng);
    for (size_t j = 0; j < suffix_len; ++j) {
      str += char_dist(rng);
    }
    strings.push_back(std::move(str));
  }
  
  return strings;
}

// Generate strings that are NOT in the tree (for miss testing)
std::vector<std::string> generate_missing_strings(const std::vector<std::string>& existing, size_t count, unsigned seed = 100) {
  std::mt19937 rng(seed);
  std::uniform_int_distribution<size_t> len_dist(5, 20);
  std::uniform_int_distribution<char> char_dist('a', 'z');
  
  std::vector<std::string> missing;
  missing.reserve(count);
  
  // Generate random strings and ensure they're not in existing set
  while (missing.size() < count) {
    size_t len = len_dist(rng);
    std::string str;
    str.reserve(len);
    for (size_t j = 0; j < len; ++j) {
      str += char_dist(rng);
    }
    
    // Check if this string is not in existing (simple linear search for benchmark setup)
    if (std::find(existing.begin(), existing.end(), str) == existing.end()) {
      missing.push_back(std::move(str));
    }
  }
  
  return missing;
}

void print_benchmark_header(const std::string& title) {
  std::cout << "\n" << std::string(100, '=') << "\n";
  std::cout << "  " << title << "\n";
  std::cout << std::string(100, '=') << "\n";
}

void print_comparison_header() {
  std::cout << std::left << std::setw(45) << "Test"
            << std::setw(18) << "RadixTree"
            << std::setw(18) << "std::set"
            << std::setw(18) << "std::unordered"
            << "\n";
  std::cout << std::string(100, '-') << "\n";
}

void print_comparison_result(const std::string& test_name, double radix_time, double set_time, double unordered_time) {
  std::cout << std::left << std::setw(45) << test_name
            << std::setw(18) << (std::to_string(static_cast<int>(radix_time)) + " μs")
            << std::setw(18) << (std::to_string(static_cast<int>(set_time)) + " μs")
            << std::setw(18) << (std::to_string(static_cast<int>(unordered_time)) + " μs")
            << "\n";
}

// Configuration for benchmark repetitions
constexpr size_t BENCHMARK_REPETITIONS = 5;  // Number of times each test is repeated

// Helper to run a benchmark multiple times and return average
template<typename Func>
double run_repeated_benchmark(Func&& func, size_t repetitions = BENCHMARK_REPETITIONS) {
  double total_time = 0.0;
  for (size_t i = 0; i < repetitions; ++i) {
    total_time += func();
  }
  return total_time / repetitions;
}

void print_speedup(const std::string& label, double radix_time, double set_time, double unordered_time) {
  double vs_set = set_time / radix_time;
  double vs_unordered = unordered_time / radix_time;
  
  std::cout << std::left << std::setw(45) << ("  " + label)
            << std::setw(18) << "1.00x"
            << std::setw(18) << (std::to_string(vs_set).substr(0, 4) + "x")
            << std::setw(18) << (std::to_string(vs_unordered).substr(0, 4) + "x")
            << "\n";
}

// Benchmark 1: Insert performance comparison
void benchmark_insert_comparison(unsigned base_seed, size_t repetitions) {
  print_benchmark_header("BENCHMARK 1: INSERT PERFORMANCE COMPARISON");
  print_comparison_header();
  
  // Test 1.1: Insert random strings (10k, 5-20 chars)
  {
    auto strings = generate_random_strings(10000, 5, 20, base_seed);
    
    // RadixTree
    double radix_time = run_repeated_benchmark([&]() {
      pidhii::radix256_node radix_root;
      BenchmarkTimer timer;
      timer.start();
      for (const auto& str : strings) {
        pidhii::insert(&radix_root, str);
      }
      return timer.elapsed_microseconds();
    }, repetitions);
    
    // std::set
    double set_time = run_repeated_benchmark([&]() {
      std::set<std::string> set_container;
      BenchmarkTimer timer;
      timer.start();
      for (const auto& str : strings) {
        set_container.insert(str);
      }
      return timer.elapsed_microseconds();
    }, repetitions);
    
    // std::unordered_set
    double unordered_time = run_repeated_benchmark([&]() {
      std::unordered_set<std::string> unordered_container;
      BenchmarkTimer timer;
      timer.start();
      for (const auto& str : strings) {
        unordered_container.insert(str);
      }
      return timer.elapsed_microseconds();
    }, repetitions);
    
    print_comparison_result("Insert 10k random (5-20 chars)", radix_time, set_time, unordered_time);
    print_speedup("Speedup vs RadixTree", radix_time, set_time, unordered_time);
  }
  
  // Test 1.2: Insert prefixed strings (10k)
  {
    auto strings = generate_prefixed_strings(10000, base_seed + 1);
    
    double radix_time = run_repeated_benchmark([&]() {
      pidhii::radix256_node radix_root;
      BenchmarkTimer timer;
      timer.start();
      for (const auto& str : strings) {
        pidhii::insert(&radix_root, str);
      }
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double set_time = run_repeated_benchmark([&]() {
      std::set<std::string> set_container;
      BenchmarkTimer timer;
      timer.start();
      for (const auto& str : strings) {
        set_container.insert(str);
      }
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double unordered_time = run_repeated_benchmark([&]() {
      std::unordered_set<std::string> unordered_container;
      BenchmarkTimer timer;
      timer.start();
      for (const auto& str : strings) {
        unordered_container.insert(str);
      }
      return timer.elapsed_microseconds();
    }, repetitions);
    
    print_comparison_result("Insert 10k prefixed strings", radix_time, set_time, unordered_time);
    print_speedup("Speedup vs RadixTree", radix_time, set_time, unordered_time);
  }
  
  // Test 1.3: Insert short strings (10k, 3-8 chars)
  {
    auto strings = generate_random_strings(10000, 3, 8, base_seed + 2);
    
    double radix_time = run_repeated_benchmark([&]() {
      pidhii::radix256_node radix_root;
      BenchmarkTimer timer;
      timer.start();
      for (const auto& str : strings) {
        pidhii::insert(&radix_root, str);
      }
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double set_time = run_repeated_benchmark([&]() {
      std::set<std::string> set_container;
      BenchmarkTimer timer;
      timer.start();
      for (const auto& str : strings) {
        set_container.insert(str);
      }
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double unordered_time = run_repeated_benchmark([&]() {
      std::unordered_set<std::string> unordered_container;
      BenchmarkTimer timer;
      timer.start();
      for (const auto& str : strings) {
        unordered_container.insert(str);
      }
      return timer.elapsed_microseconds();
    }, repetitions);
    
    print_comparison_result("Insert 10k short (3-8 chars)", radix_time, set_time, unordered_time);
    print_speedup("Speedup vs RadixTree", radix_time, set_time, unordered_time);
  }
  
  // Test 1.4: Insert long strings (5k, 30-60 chars)
  {
    auto strings = generate_random_strings(5000, 30, 60, base_seed + 3);
    
    double radix_time = run_repeated_benchmark([&]() {
      NODE_TYPE radix_root;
      BenchmarkTimer timer;
      timer.start();
      for (const auto& str : strings) {
        pidhii::insert(&radix_root, str);
      }
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double set_time = run_repeated_benchmark([&]() {
      std::set<std::string> set_container;
      BenchmarkTimer timer;
      timer.start();
      for (const auto& str : strings) {
        set_container.insert(str);
      }
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double unordered_time = run_repeated_benchmark([&]() {
      std::unordered_set<std::string> unordered_container;
      BenchmarkTimer timer;
      timer.start();
      for (const auto& str : strings) {
        unordered_container.insert(str);
      }
      return timer.elapsed_microseconds();
    }, repetitions);
    
    print_comparison_result("Insert 5k long (30-60 chars)", radix_time, set_time, unordered_time);
    print_speedup("Speedup vs RadixTree", radix_time, set_time, unordered_time);
  }
  
  // Test 1.5: Large-scale insert (100k, 10-25 chars)
  {
    auto strings = generate_random_strings(100000, 10, 25, base_seed + 4);
    
    double radix_time = run_repeated_benchmark([&]() {
      NODE_TYPE radix_root;
      BenchmarkTimer timer;
      timer.start();
      for (const auto& str : strings) {
        pidhii::insert(&radix_root, str);
      }
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double set_time = run_repeated_benchmark([&]() {
      std::set<std::string> set_container;
      BenchmarkTimer timer;
      timer.start();
      for (const auto& str : strings) {
        set_container.insert(str);
      }
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double unordered_time = run_repeated_benchmark([&]() {
      std::unordered_set<std::string> unordered_container;
      BenchmarkTimer timer;
      timer.start();
      for (const auto& str : strings) {
        unordered_container.insert(str);
      }
      return timer.elapsed_microseconds();
    }, repetitions);
    
    print_comparison_result("Insert 100k random (10-25 chars)", radix_time, set_time, unordered_time);
    print_speedup("Speedup vs RadixTree", radix_time, set_time, unordered_time);
  }
}

// Benchmark 2: Find performance comparison (hit - strings in container)
void benchmark_find_hit_comparison(unsigned base_seed, size_t repetitions) {
  print_benchmark_header("BENCHMARK 2: FIND PERFORMANCE COMPARISON (HIT - Strings Present)");
  print_comparison_header();
  
  // Test 2.1: Find in random strings (10k, 5-20 chars)
  {
    auto strings = generate_random_strings(10000, 5, 20, base_seed + 10);
    
    // Build containers
    NODE_TYPE radix_root;
    std::set<std::string> set_container;
    std::unordered_set<std::string> unordered_container;
    
    for (const auto& str : strings) {
      pidhii::insert(&radix_root, str);
      set_container.insert(str);
      unordered_container.insert(str);
    }
    
    // Benchmark RadixTree
    double radix_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t found = 0;
      for (const auto& str : strings) {
        if (pidhii::find(&radix_root, str) != nullptr) found++;
      }
      assert(found == strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    // Benchmark std::set
    double set_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t found = 0;
      for (const auto& str : strings) {
        if (set_container.find(str) != set_container.end()) found++;
      }
      assert(found == strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    // Benchmark std::unordered_set
    double unordered_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t found = 0;
      for (const auto& str : strings) {
        if (unordered_container.find(str) != unordered_container.end()) found++;
      }
      assert(found == strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    print_comparison_result("Find 10k random (all present)", radix_time, set_time, unordered_time);
    print_speedup("Speedup vs RadixTree", radix_time, set_time, unordered_time);
  }
  
  // Test 2.2: Find in prefixed strings (10k)
  {
    auto strings = generate_prefixed_strings(10000, base_seed + 11);
    
    NODE_TYPE radix_root;
    std::set<std::string> set_container;
    std::unordered_set<std::string> unordered_container;
    
    for (const auto& str : strings) {
      pidhii::insert(&radix_root, str);
      set_container.insert(str);
      unordered_container.insert(str);
    }
    
    double radix_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t found = 0;
      for (const auto& str : strings) {
        if (pidhii::find(&radix_root, str) != nullptr) found++;
      }
      assert(found == strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double set_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t found = 0;
      for (const auto& str : strings) {
        if (set_container.find(str) != set_container.end()) found++;
      }
      assert(found == strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double unordered_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t found = 0;
      for (const auto& str : strings) {
        if (unordered_container.find(str) != unordered_container.end()) found++;
      }
      assert(found == strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    print_comparison_result("Find 10k prefixed (all present)", radix_time, set_time, unordered_time);
    print_speedup("Speedup vs RadixTree", radix_time, set_time, unordered_time);
  }
  
  // Test 2.3: Find in short strings (10k, 3-8 chars)
  {
    auto strings = generate_random_strings(10000, 3, 8, base_seed + 12);
    
    NODE_TYPE radix_root;
    std::set<std::string> set_container;
    std::unordered_set<std::string> unordered_container;
    
    for (const auto& str : strings) {
      pidhii::insert(&radix_root, str);
      set_container.insert(str);
      unordered_container.insert(str);
    }
    
    double radix_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t found = 0;
      for (const auto& str : strings) {
        if (pidhii::find(&radix_root, str) != nullptr) found++;
      }
      assert(found == strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double set_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t found = 0;
      for (const auto& str : strings) {
        if (set_container.find(str) != set_container.end()) found++;
      }
      assert(found == strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double unordered_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t found = 0;
      for (const auto& str : strings) {
        if (unordered_container.find(str) != unordered_container.end()) found++;
      }
      assert(found == strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    print_comparison_result("Find 10k short (all present)", radix_time, set_time, unordered_time);
    print_speedup("Speedup vs RadixTree", radix_time, set_time, unordered_time);
  }
  
  // Test 2.4: Find in large container (100k)
  {
    auto strings = generate_random_strings(100000, 10, 25, base_seed + 13);
    
    NODE_TYPE radix_root;
    std::set<std::string> set_container;
    std::unordered_set<std::string> unordered_container;
    
    for (const auto& str : strings) {
      pidhii::insert(&radix_root, str);
      set_container.insert(str);
      unordered_container.insert(str);
    }
    
    // Sample 10k strings
    std::vector<std::string> sample;
    for (size_t i = 0; i < strings.size(); i += 10) {
      sample.push_back(strings[i]);
    }
    
    double radix_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t found = 0;
      for (const auto& str : sample) {
        if (pidhii::find(&radix_root, str) != nullptr) found++;
      }
      assert(found == sample.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double set_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t found = 0;
      for (const auto& str : sample) {
        if (set_container.find(str) != set_container.end()) found++;
      }
      assert(found == sample.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double unordered_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t found = 0;
      for (const auto& str : sample) {
        if (unordered_container.find(str) != unordered_container.end()) found++;
      }
      assert(found == sample.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    print_comparison_result("Find 10k in 100k container", radix_time, set_time, unordered_time);
    print_speedup("Speedup vs RadixTree", radix_time, set_time, unordered_time);
  }
}

// Benchmark 3: Find performance comparison (miss - strings NOT in container)
void benchmark_find_miss_comparison(unsigned base_seed, size_t repetitions) {
  print_benchmark_header("BENCHMARK 3: FIND PERFORMANCE COMPARISON (MISS - Strings Missing)");
  print_comparison_header();
  
  // Test 3.1: Find missing in random tree (10k tree, 5k misses)
  {
    auto tree_strings = generate_random_strings(10000, 5, 20, base_seed + 20);
    auto missing_strings = generate_missing_strings(tree_strings, 5000, base_seed + 100);
    
    NODE_TYPE radix_root;
    std::set<std::string> set_container;
    std::unordered_set<std::string> unordered_container;
    
    for (const auto& str : tree_strings) {
      pidhii::insert(&radix_root, str);
      set_container.insert(str);
      unordered_container.insert(str);
    }
    
    double radix_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t not_found = 0;
      for (const auto& str : missing_strings) {
        if (pidhii::find(&radix_root, str) == nullptr) not_found++;
      }
      assert(not_found == missing_strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double set_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t not_found = 0;
      for (const auto& str : missing_strings) {
        if (set_container.find(str) == set_container.end()) not_found++;
      }
      assert(not_found == missing_strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double unordered_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t not_found = 0;
      for (const auto& str : missing_strings) {
        if (unordered_container.find(str) == unordered_container.end()) not_found++;
      }
      assert(not_found == missing_strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    print_comparison_result("Find 5k missing (random)", radix_time, set_time, unordered_time);
    print_speedup("Speedup vs RadixTree", radix_time, set_time, unordered_time);
  }
  
  // Test 3.2: Find missing in prefixed tree
  {
    auto tree_strings = generate_prefixed_strings(10000, base_seed + 21);
    auto missing_strings = generate_missing_strings(tree_strings, 5000, base_seed + 200);
    
    NODE_TYPE radix_root;
    std::set<std::string> set_container;
    std::unordered_set<std::string> unordered_container;
    
    for (const auto& str : tree_strings) {
      pidhii::insert(&radix_root, str);
      set_container.insert(str);
      unordered_container.insert(str);
    }
    
    double radix_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t not_found = 0;
      for (const auto& str : missing_strings) {
        if (pidhii::find(&radix_root, str) == nullptr) not_found++;
      }
      assert(not_found == missing_strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double set_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t not_found = 0;
      for (const auto& str : missing_strings) {
        if (set_container.find(str) == set_container.end()) not_found++;
      }
      assert(not_found == missing_strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double unordered_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t not_found = 0;
      for (const auto& str : missing_strings) {
        if (unordered_container.find(str) == unordered_container.end()) not_found++;
      }
      assert(not_found == missing_strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    print_comparison_result("Find 5k missing (prefixed)", radix_time, set_time, unordered_time);
    print_speedup("Speedup vs RadixTree", radix_time, set_time, unordered_time);
  }
  
  // Test 3.3: Find similar-prefix missing
  {
    auto tree_strings = generate_prefixed_strings(10000, base_seed + 22);
    
    NODE_TYPE radix_root;
    std::set<std::string> set_container;
    std::unordered_set<std::string> unordered_container;
    
    for (const auto& str : tree_strings) {
      pidhii::insert(&radix_root, str);
      set_container.insert(str);
      unordered_container.insert(str);
    }
    
    std::vector<std::string> similar_missing;
    for (const auto& str : tree_strings) {
      if (similar_missing.size() >= 5000) break;
      similar_missing.push_back(str + "X");
    }
    
    double radix_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t not_found = 0;
      for (const auto& str : similar_missing) {
        if (pidhii::find(&radix_root, str) == nullptr) not_found++;
      }
      assert(not_found == similar_missing.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double set_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t not_found = 0;
      for (const auto& str : similar_missing) {
        if (set_container.find(str) == set_container.end()) not_found++;
      }
      assert(not_found == similar_missing.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double unordered_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t not_found = 0;
      for (const auto& str : similar_missing) {
        if (unordered_container.find(str) == unordered_container.end()) not_found++;
      }
      assert(not_found == similar_missing.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    print_comparison_result("Find 5k similar-prefix missing", radix_time, set_time, unordered_time);
    print_speedup("Speedup vs RadixTree", radix_time, set_time, unordered_time);
  }
  
  // Test 3.4: Find missing in large container (100k tree, 10k misses)
  {
    auto tree_strings = generate_random_strings(100000, 10, 25, base_seed + 23);
    auto missing_strings = generate_missing_strings(tree_strings, 10000, base_seed + 300);
    
    NODE_TYPE radix_root;
    std::set<std::string> set_container;
    std::unordered_set<std::string> unordered_container;
    
    for (const auto& str : tree_strings) {
      pidhii::insert(&radix_root, str);
      set_container.insert(str);
      unordered_container.insert(str);
    }
    
    double radix_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t not_found = 0;
      for (const auto& str : missing_strings) {
        if (pidhii::find(&radix_root, str) == nullptr) not_found++;
      }
      assert(not_found == missing_strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double set_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t not_found = 0;
      for (const auto& str : missing_strings) {
        if (set_container.find(str) == set_container.end()) not_found++;
      }
      assert(not_found == missing_strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    double unordered_time = run_repeated_benchmark([&]() {
      BenchmarkTimer timer;
      timer.start();
      size_t not_found = 0;
      for (const auto& str : missing_strings) {
        if (unordered_container.find(str) == unordered_container.end()) not_found++;
      }
      assert(not_found == missing_strings.size());
      return timer.elapsed_microseconds();
    }, repetitions);
    
    print_comparison_result("Find 10k missing in 100k container", radix_time, set_time, unordered_time);
    print_speedup("Speedup vs RadixTree", radix_time, set_time, unordered_time);
  }
}

void print_usage(const char* program_name) {
  std::cout << "Usage: " << program_name << " [SEED] [REPETITIONS]\n\n";
  std::cout << "Benchmark radix tree performance against STL containers.\n\n";
  std::cout << "Arguments:\n";
  std::cout << "  SEED         Random seed for reproducible results (default: 42)\n";
  std::cout << "  REPETITIONS  Number of times to repeat each test (default: " << BENCHMARK_REPETITIONS << ")\n\n";
  std::cout << "Examples:\n";
  std::cout << "  " << program_name << "           # Run with defaults (seed=42, reps=" << BENCHMARK_REPETITIONS << ")\n";
  std::cout << "  " << program_name << " 12345     # Run with seed 12345, default repetitions\n";
  std::cout << "  " << program_name << " 42 10     # Run with seed 42, 10 repetitions\n";
}

int main(int argc, char* argv[]) {
  unsigned seed = 42;  // Default seed
  size_t repetitions = BENCHMARK_REPETITIONS;  // Default repetitions
  
  // Parse command-line arguments
  if (argc > 1) {
    if (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
      print_usage(argv[0]);
      return 0;
    }
    
    try {
      seed = std::stoul(argv[1]);
    } catch (...) {
      std::cerr << "Error: Invalid seed value '" << argv[1] << "'\n";
      std::cerr << "Seed must be a positive integer.\n\n";
      print_usage(argv[0]);
      return 1;
    }
  }
  
  if (argc > 2) {
    try {
      repetitions = std::stoul(argv[2]);
      if (repetitions == 0) {
        std::cerr << "Error: Repetitions must be at least 1\n\n";
        print_usage(argv[0]);
        return 1;
      }
    } catch (...) {
      std::cerr << "Error: Invalid repetitions value '" << argv[2] << "'\n";
      std::cerr << "Repetitions must be a positive integer.\n\n";
      print_usage(argv[0]);
      return 1;
    }
  }
  
  std::cout << "\n";
  std::cout << "╔══════════════════════════════════════════════════════════════════════════════════════════════╗\n";
  std::cout << "║                    RADIX TREE vs STL CONTAINERS BENCHMARK COMPARISON                         ║\n";
  std::cout << "╚══════════════════════════════════════════════════════════════════════════════════════════════╝\n";
  std::cout << "  Random seed: " << seed << " (for reproducible results)\n";
  std::cout << "  Repetitions: " << repetitions << " (each test runs " << repetitions << " times, results show average timing)\n";
  
  benchmark_insert_comparison(seed, repetitions);
  benchmark_find_hit_comparison(seed, repetitions);
  benchmark_find_miss_comparison(seed, repetitions);
  
  std::cout << "\n" << std::string(100, '=') << "\n";
  std::cout << "  Benchmark suite completed successfully!\n";
  std::cout << "  Random seed used: " << seed << "\n";
  std::cout << "  Note: Speedup values > 1.0 indicate RadixTree is faster\n";
  std::cout << "        Speedup values < 1.0 indicate RadixTree is slower\n";
  std::cout << std::string(100, '=') << "\n\n";
  
  return 0;
}
