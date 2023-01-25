#include <string>
#include <iostream>
#include <sstream>
#include <limits>

#include <parlay/primitives.h>
#include <parlay/random.h>
#include <parlay/io.h>
#include <parlay/internal/get_time.h>
#include <parlay/internal/group_by.h>
#include "zipfian.h"
#include "parse_command_line.h"

#include "timer_loops.h"

void assert_key_exists(bool b) {
  if(!b) {
    std::cout << "key not found" << std::endl;
    abort();
  }
}

void print_array(bool* a, int N) {
  for(int i = 0; i < N; i++)
    std::cout << a[i];
  std::cout << std::endl;
}

template <typename SetType>
void test_persistence_concurrent(SetType& os) {
  int N = 1000;
  auto tr = os.empty(N);

  auto a = parlay::random_shuffle(parlay::tabulate(N, [&] (size_t i) {return i+1;}));
  std::atomic<bool> done = false;

  int num_threads = 2;

  parlay::parallel_for(0, num_threads, [&] (size_t tid) {
    if(tid == num_threads-1) {  // update thread
      std::cout << "starting to insert" << std::endl;
      for(int i = 0; i < N; i++) {
        // std::cout << "inserting " << i << " " << a[i] << std::endl;
        os.insert(tr, a[i], i+1);
      }
      std::cout << "starting to delete" << std::endl;
      for(int i = N-1; i >= 0; i--)
        os.remove(tr, a[i]);
      std::cout << "done updating" << std::endl;
      done = true;
    } 
    else {  // query threads
      std::cout << "starting to query" << std::endl;
      int counter = 0;
      int counter2 = 0;
      while(!done) {
        bool seen[N+1];
        int max_seen;
        verlib::with_snapshot([&] {
          max_seen = -1;
          for(int i = 1; i <= N; i++) seen[i] = false;
          for(int i = 1; i <= N; i++) {
            auto val = os.find_(tr, i);
            if(val.has_value()) {
              seen[val.value()] = true;
              max_seen = std::max(max_seen, (int) val.value());
            }
          }
        });
        std::cout << "max_seen: " << max_seen << std::endl;
        // print_array(seen, N);
        for(int i = 1; i <= max_seen; i++)
          if(!seen[i]) {
            std::cout << "inconsistent snapshot" << std::endl;
            break;
            abort();
          }
        counter2++;
        if(max_seen > 2 && max_seen < N-3) 
          counter++; // saw an intermediate state
      }
      if(counter < 3) {
        std::cout << "not enough iterations by query thread" << std::endl;
      }
    } 
  }, 1);
  os.retire(tr);
// #endif
}

template <typename SetType>
void test_sets(SetType& os, size_t default_size, commandLine P) {
  // processes to run experiments with
  int p = P.getOptionIntValue("-p", parlay::num_workers());  

  int rounds = P.getOptionIntValue("-r", 1);

  // do fixed time experiment
  bool fixed_time = !P.getOption("-insert_find_delete");

  // number of distinct keys (keys will be selected among 2n distinct keys)
  long n = P.getOptionIntValue("-n", default_size);
  long nn = fixed_time ? 2*n : n;

  // verbose
  bool verbose = P.getOption("-verbose");

  // clear the memory pool between rounds
  //bool clear = P.getOption("-clear");

  // number of samples 
  long m = P.getOptionIntValue("-m", fixed_time ? 10 * n : n);
    
  // check consistency, on by default
  bool do_check = ! P.getOption("-no_check");

  // run a trivial test
  bool init_test = P.getOption("-simple_test"); // run trivial test

  // use zipfian distribution
  double zipfian_param = P.getOptionDoubleValue("-z", 0.0);
  bool use_zipfian = (zipfian_param != 0.0);

  // use numbers from 1...2n if dense otherwise sparse numbers
  bool use_sparse = !P.getOption("-dense");
  int range_percent = P.getOptionIntValue("-range",0);
#ifdef Dense_Keys  // for range queries on hash tables
  if (range_percent > 0)
    use_sparse = false;
#endif

  int buckets = P.getOptionIntValue("-bu", n);

  // print memory usage statistics
  bool stats = P.getOption("-stats");

  if (init_test) {  // trivial test inserting 4 elements and deleting one
    std::cout << "running sanity checks" << std::endl;
    auto tr = os.empty(4);
    os.print(tr);
    os.insert(tr, 3, 123);
    os.print(tr);
    os.insert(tr, 7, 123);
    os.print(tr);
    os.insert(tr, 1, 123);
    // os.print(tr);
    os.insert(tr, 11, 123);
    // os.print(tr);
    os.remove(tr, 3);
    // os.print(tr);
    assert_key_exists(os.find(tr, 7).has_value());
    assert_key_exists(os.find(tr, 1).has_value());
    assert_key_exists(os.find(tr, 11).has_value());
    assert(!os.find(tr, 10).has_value());
    assert(!os.find(tr, 3).has_value());

    verlib::with_snapshot([&] {
	assert_key_exists(os.find_(tr, 7).has_value());
	assert_key_exists(os.find_(tr, 1).has_value());
	assert_key_exists(os.find_(tr, 11).has_value());
	assert(!os.find_(tr, 10).has_value());
	assert(!os.find_(tr, 3).has_value());
      });

    os.retire(tr);

    // run persistence tests
    test_persistence_concurrent(os);
    
  } else {  // main test
    using key_type = unsigned long;

    // generate 2*n unique numbers in random order
    parlay::sequence<key_type> a;
    key_type max_key;

    if (use_sparse) {
      max_key = ~0ul;
      auto x = parlay::delayed_tabulate(1.2*nn,[&] (size_t i) {
	  return (key_type) parlay::hash64(i);}); // generate 64-bit keys
      auto xx = parlay::remove_duplicates(x);
      auto y = parlay::random_shuffle(xx);
      // don't use zero since it breaks setbench code
      a = parlay::tabulate(nn, [&] (size_t i) {return std::min(max_key-1,y[i])+1;}); 
    } else {
      max_key = nn;
      a = parlay::random_shuffle(parlay::tabulate(nn, [] (key_type i) {
	    return i+1;}));
    }

    parlay::sequence<key_type> b;
    if (use_zipfian) { 
      Zipfian z(nn, zipfian_param);
      b = parlay::tabulate(m, [&] (int i) { return a[z(i)]; });
    } else
      b = parlay::tabulate(m, [&] (int i) {return a[parlay::hash64(i) % nn]; });

    if (fixed_time) {
      run_mixed_operations(os, b, a, zipfian_param, P);

    } else {
      run_insert_find_remove(os, b, nn, P);
    }
  }
}
