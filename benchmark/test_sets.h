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
void test_persistence_concurrent() {
  int N = 1000;
  auto tr = SetType(N);

  auto a = parlay::random_shuffle(parlay::tabulate(N, [&] (size_t i) {return i+1;}));
  std::atomic<bool> done = false;

  int num_threads = 2;

  parlay::parallel_for(0, num_threads, [&] (size_t tid) {
     using V = typename SetType::V;
     V default_value;
     if(tid == num_threads-1) {  // update thread
      std::cout << "starting to insert" << std::endl;
      for(int i = 0; i < N; i++) {
        // std::cout << "inserting " << i << " " << a[i] << std::endl;
	default_value[0] = i+1;
        tr.insert(a[i], default_value);
      }
      std::cout << "starting to delete" << std::endl;
      for(int i = N-1; i >= 0; i--)
        tr.remove(a[i]);
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
            auto val = tr.find_(i);
            if(val.has_value()) {
              seen[val.value()[0]] = true;
              max_seen = std::max(max_seen, (int) val.value()[0]);
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
// #endif
}

template <typename SetType>
void test_sets(size_t default_size, commandLine P) {

  // processes to run experiments with
  int p = P.getOptionIntValue("-p", parlay::num_workers());  

  int rounds = P.getOptionIntValue("-r", 1);

  // do fixed time experiment
  bool fixed_time = !P.getOption("-insert_find_delete");

  // Use upsert instead of insert and remove
  bool use_upsert = P.getOption("-upsert");
#ifndef UPSERT
  if (use_upsert) {
    std::cout << "upsert not supported for this data structure" << std::endl;
    use_upsert = false;
  }
#endif

  // number of distinct keys (keys will be selected among n distinct keys)
  uint64_t n = P.getOptionLongValue("-n", default_size);
  uint64_t nscale = P.getOptionIntValue("-nscale", 2);
  uint64_t nn = (fixed_time && !use_upsert) ? nscale*n : n;

  // verbose
  bool verbose = P.getOption("-verbose");

  // read data from file
  std::string filename = P.getOptionValue("-file", "");

  // clear the memory pool between rounds
  //bool clear = P.getOption("-clear");

  // check consistency, on by default
  bool do_check = ! P.getOption("-no_check");

  // run a trivial test
  bool init_test = P.getOption("-simple_test"); // run trivial test

  // use zipfian distribution
  double zipfian_param = P.getOptionDoubleValue("-z", 0.0);
  bool use_zipfian = (zipfian_param != 0.0);

  // use numbers from 1...2n if dense otherwise sparse numbers
  bool use_sparse = !P.getOption("-dense");
#ifdef DENSE
  use_sparse = false;
#endif
  int range_percent = P.getOptionIntValue("-range",0);
#ifdef Dense_Keys  // for range queries on hash tables
  if (range_percent > 0)
    use_sparse = false;
#endif

  int buckets = P.getOptionIntValue("-bu", n);

  // number of samples
  int range_size = P.getOptionIntValue("-rs",16);

  // print memory usage statistics
  bool stats = P.getOption("-stats");
  using V = typename SetType::V;
  V default_value;
  default_value[0] = 123;

  if (init_test) {  // trivial test inserting 4 elements and deleting one
    std::cout << "running sanity checks" << std::endl;
    auto tr = SetType(4);
    tr.print();
    tr.insert(3, default_value);
    tr.print();
    tr.insert(7, default_value);
    tr.print();
    tr.insert(1, default_value);
    tr.print();
    tr.insert(11, default_value);
    tr.print();
    tr.remove(3);
    tr.print();
    assert_key_exists(tr.find(7).has_value());
    assert_key_exists(tr.find(1).has_value());
    assert_key_exists(tr.find(11).has_value());
    assert(!tr.find(10).has_value());
    assert(!tr.find(3).has_value());

    verlib::with_snapshot([&] {
	assert_key_exists(tr.find_(7).has_value());
	assert_key_exists(tr.find_(1).has_value());
	assert_key_exists(tr.find_(11).has_value());
	assert(!tr.find_(10).has_value());
	assert(!tr.find_(3).has_value());
      });


    // run persistence tests
    test_persistence_concurrent<SetType>();
    
  } else {  // main test
    using key_type = unsigned long;
    parlay::sequence<key_type> a;
    parlay::sequence<key_type> b;
    key_type max_key = 0;

    if(verbose)
      std::cout << "generating keys" << std::endl;

    if (filename.size() == 0) {
    // generate 2*n unique numbers in random order
      if (use_sparse) {
        max_key = ~0ul;
        if(verbose) std::cout << "delayed tabulate" << std::endl;
        // generate 64-bit keys
        auto x = parlay::delayed_tabulate(1.2*nn,[&] (size_t i) { return (key_type) parlay::hash64(i);}); 
        if(verbose) std::cout << "remove duplicates" << std::endl;
        auto xx = parlay::remove_duplicates(x);
        if(verbose) std::cout << "random shuffle" << std::endl;
        auto y = parlay::random_shuffle(xx);
        // don't use zero since it breaks setbench code
        if(verbose) std::cout << "tabulate" << std::endl;
        a = parlay::tabulate(nn, [&] (size_t i) {return std::min(max_key-1,y[i])+1;}); 
      } else {
        max_key = nn;
        a = parlay::random_shuffle(parlay::tabulate(nn, [] (key_type i) {
                       return i+1;}));
      }
    } else {
      auto fmap = parlay::file_map(filename);
      if (fmap.size() == 0) {
        std::cout << "bad filename: " << filename << std::endl;
        abort();
      }
      n = *((long*) fmap.begin());
      auto data = parlay::tabulate(n, [&] (long i) {
            return *((unsigned long*) (8 + fmap.begin() + i * 8));});
      
      a = parlay::random_shuffle(parlay::remove_duplicates(data));
    }

    long m = P.getOptionIntValue("-m", fixed_time ? 10 * n + 1000 * p : n);

    if(verbose)
      std::cout << "generating distribution" << std::endl;

    if (use_zipfian) { 
      Zipfian z(nn, zipfian_param);
      b = parlay::tabulate(m, [&] (int i) { return a[z(i)]; });
      a = parlay::random_shuffle(a);
    } else {
      b = parlay::tabulate(m, [=] (int i) {return a[parlay::hash64(i) % nn]; });
    }

    if (fixed_time) {
      run_mixed_operations<SetType>(b, a, zipfian_param, max_key, use_upsert, P);

    } else {
      run_insert_find_remove<SetType>(b, nn, P);
    }
  }
}
