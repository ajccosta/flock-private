// Contains main timer loops
//
// run_mixed_operation:
// runs on given keys for a fixed amount of time specified by -tt with
// a mix of operations, specified as follows
//   -u <update percent>
//   -range <range query percent>
// any left over percent goes to finds.
//   -rs <size>
// specifies the size of the range query or multifind.
//   -mfind
// specifies the queries should be grouped as multi finds (not compatible with range)
// If
//   -range_query_threads <num threads>
// is specified then that percent of threads are dedicated to range
// queries and the rest use the mix above.
// Reports throughput.
//
// run_insert_find_remove:
// inserts, then finds, then removes the given keys
//

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <optional>
#include <type_traits>
#include <tuple>
#include <vector>

#include <parlay/parallel.h>
#include <parlay/primitives.h>
#include <parlay/random.h>
#include <parlay/alloc.h>

#include "memory_usage.h"

#define REPORT_BYTES

#define noop

struct statistics {
  long total;
  long added_count; // inserted - deleted
  long range_count;
  long range_sum;
  long mfind_count;
  long retry_count;
  long update_count;
  long update_success_count;
  long query_count;
  long query_total_value;
  long within_latency;
  long transactions;
  long aborts;
  long num_allocs;
  long num_frees;
  long num_approx_frees;
  long num_failed_frees;
  statistics() :
    total(0), added_count(0), range_count(0), range_sum(0), mfind_count(0), retry_count(0), 
    update_count(0), update_success_count(0),
    query_count(0), query_total_value(0), within_latency(0), transactions(0), aborts(0),
    num_allocs(0), num_frees(0), num_approx_frees(0), num_failed_frees(0)
    {}
};

// for adding across processors
statistics adds(statistics s1, statistics s2) {
  statistics r;
  r.total = s1.total + s2.total;
  r.added_count = s1.added_count + s2.added_count;
  r.range_count = s1.range_count + s2.range_count;
  r.range_sum = s1.range_sum + s2.range_sum;
  r.mfind_count = s1.mfind_count + s2.mfind_count;
  r.retry_count = s1.retry_count + s2.retry_count;
  r.update_count = s1.update_count + s2.update_count;
  r.update_success_count = s1.update_success_count + s2.update_success_count;
  r.query_count = s1.query_count + s2.query_count;
  r.query_total_value = s1.query_total_value + s2.query_total_value;
  r.within_latency = s1.within_latency + s2.within_latency;
  r.transactions = s1.transactions + s2.transactions;
  r.aborts = s1.aborts + s2.aborts;
  r.num_allocs = s1.num_allocs + s2.num_allocs;
  r.num_frees = s1.num_frees + s2.num_frees;
  r.num_approx_frees = s1.num_approx_frees + s2.num_approx_frees;
  r.num_failed_frees = s1.num_failed_frees + s2.num_failed_frees;
  return r;
}    

#ifdef STM
template <typename F>
auto do_atomic(const F& f) {
  if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
    verlib::atomic_region(f);
  } else {
    return verlib::atomic_region([&] () -> std::optional<std::invoke_result_t<F>> {
	return std::optional(f());});
  }
}

template <typename F>
auto do_ro_atomic(const F& f) {
  return verlib::atomic_read_only([&] {return f();});}
#else
template <typename F>
auto do_atomic(const F& f) {
  if constexpr (std::is_void_v<std::invoke_result_t<F>>) { f(); }
  else { return f();}
}

template <typename F>
auto do_ro_atomic(const F& f) { return f();}
#endif			   

template<typename SetType, typename K>
void run_mixed_operations(parlay::sequence<K>& all,
                          const parlay::sequence<K>& unique,
                          float zipfian_param,
			  K max_key,
			  bool use_upsert,
                          commandLine P) {
  int64_t m = all.size();
  int64_t nn = unique.size();
  int64_t nscale = P.getOptionIntValue("-nscale", 2);
  int64_t prefill = P.getOptionIntValue("-prefill", nn/nscale);
  int64_t n = use_upsert ? nn/2 : nn;

  int p = P.getOptionIntValue("-p", parlay::num_workers());  

  int rounds = P.getOptionIntValue("-r", 1);

  double trial_time = P.getOptionDoubleValue("-t", 1.0);

  // Mix of operations
  int update_percent = P.getOptionIntValue("-u", 20); 
  int range_size = P.getOptionIntValue("-rs",16);
  int range_percent = P.getOptionIntValue("-range",0);
  int multifind = P.getOption("-mfind");
  int trans_size = P.getOptionIntValue("-trans", 0);
  int latency = P.getOptionIntValue("-latency", 0); // in microseconds
#ifndef Latency
  if (latency > 0)
    std::cout << "not compiled for the -latency argument, ignoring" << std::endl;
#endif
  
  // mix of threads
  int range_query_threads = P.getOptionIntValue("-rqthreads", 0);
  
#ifdef Range_Search
  K range_gap = (max_key/n)*range_size;

#else
  if (range_percent > 0) {
    std::cout << "range search not implemented for this structure" << std::endl;
    return;
  }
#endif

  // number of hash buckets for hash tables
  long buckets = P.getOptionLongValue("-bu", n);

  // shuffles the memory pool to break sharing of cache lines
  // by neighboring list/tree nodes
  bool shuffle = P.getOption("-shuffle");

  // verbose
  bool verbose = P.getOption("-verbose");

  // check consistency, on by default
  bool do_check = ! P.getOption("-no_check");

  // print memory usage statistics
  bool stats = P.getOption("-stats");

  // clear the memory pool between rounds
  bool clear = P.getOption("-clear");

  // warmup data-structure (perform random inserts and deletes)
  int warmup_size = P.getOptionIntValue("-warmup", 0);
  bool warmup = warmup_size != 0;

  bool test_allocator = P.getOption("-test_allocator");

  //Whether or not to skip the warmup round
  bool skip_warmup_round = P.getOption("-no_warmup_round");

  const char* thread_perc_c = P.getOptionValue("-thread-perc");
  bool use_thread_perc = false;
  std::vector<std::vector<int>> thread_perc_vec;
  size_t ncols = 0, nrows = 0;
  if(thread_perc_c) {
    use_thread_perc = true;
    std::string thread_perc(thread_perc_c);
    std::optional<std::tuple<std::vector<std::vector<int>>,std::size_t,std::size_t>> mo = P.parseMatrix(thread_perc);
    if(!mo) {
      std::cerr << "Thread percentage matrix is malformed." << std::endl;
      std::exit(-1);
    }
    auto [mat, n_rows, n_cols] = *std::move(mo);
    //globally scoped vars
    thread_perc_vec = std::move(mat);
    ncols = n_cols; //per-thread type, how many segments exist
    nrows = n_rows; //how many thread types exist
    for (auto& row : mat)
      for (int x : row)
        if(x < 0 || x > 100) {
          std::cerr << "Thread percentage values should [0, 100]." << std::endl;
          std::exit(-1);
        }
  }

  enum op_type : char {Find, Insert, Remove, Range, MultiFind};

  float update_per = update_percent;
  if (multifind) {
    float update_frac = update_per / 100.0;
    update_per = 100.0 * range_size * update_frac / (1 + (range_size - 1) * update_frac);
  }

  parlay::sequence<op_type> op_types;
  size_t indexing_sum;
  if(use_thread_perc) {
    op_types = parlay::tabulate(m * nrows, [&] (size_t i) -> op_type {
      auto h = parlay::hash64(m+i)%2000;
      if(h < 20*update_per) {
        //insert tendency
        //  100 means do only inserts
        //  0 means do only deletes
        size_t workload_segment = i / m; //which segment of the workload this op is on
        size_t thread_rr = (i / (m / ncols)) % ncols; //thread round robin assignment
        size_t ins_tendency = thread_perc_vec[workload_segment][thread_rr] * ((20*update_per)/100);
        if(h < ins_tendency) return Insert;
        else return Remove;
      } else {
        return Find;
      }
    });
  } else {
    nrows = 1;
    op_types = parlay::tabulate(m, [&] (size_t i) -> op_type {
      auto h = parlay::hash64(m+i)%2000;
      if (h < 10*update_per) return Insert;
      else if (h < 20*update_per) return Remove;
      else if (multifind) return MultiFind;
      else if (h < 20*(update_percent + range_percent)) return Range;
      else return Find;
    });
  }

  auto get_operation = [&] (size_t i, size_t proc_id) -> op_type {
    size_t thread_rr_offset = m * (proc_id % nrows);
    return op_types[i + thread_rr_offset];
  };

  parlay::internal::timer t("Benchmark run_mixed_operations");

  for (int i = skip_warmup_round ? 1 : 0; i < rounds+1; i++) {
    { //forced scope
    if (verbose)
      std::cout << "creating " << buckets << " buckets" << std::endl;
    auto tr = SetType(buckets);
    long len;
    if (do_check) {
      size_t len = tr.check();
      if (len != 0) {
        std::cout << "BAD LENGTH = " << len << std::endl;
      }
    }

    if (verbose) std::cout << "round " << i << std::endl;
    size_t initial_size;
    using V = typename SetType::V;
							  
    V default_value;
    default_value[0] = 3;

    if (verbose)
      std::cout << "prefilling to " << prefill << std::endl;

    // insert <prefill> distinct elements
    parlay::parallel_for(0, prefill, [&] (size_t i) {
      do_atomic([&] {
        tr.insert(unique[i], default_value);
      });
    }, 10, true);

    //if (!use_upsert) {
    //  parlay::parallel_for(0, n, [&] (size_t i) {
    //    do_atomic([&] {tr.remove(unique[i]);});
    //  }, 10, true);
    //}

    if (shuffle) SetType::shuffle(n);
    
    if (verbose)
      std::cout << "initial size = " << tr.check() << std::endl;
    
    //insert and delete for a while
    if(warmup) {
      parlay::parallel_for(0, warmup_size, [&] (size_t i) {
        auto k = unique[parlay::hash64(i + i) % nn];
        if(i%2==0)
          tr.insert(k, default_value); 
        else
          tr.remove(k);
      }, 10, true);
    }

    if (do_check) {
      initial_size = tr.check();
    }

    size_t mp = m/p;
    // set some number of threads to only do range queries
    //TODO fix range queries
    //parlay::parallel_for(0, range_query_threads, [&] (size_t i) {
    //    for(int j = i*mp; j < (i+1)*mp; j++) get_operation(j) = Range;
    //  }, 1);
    
    parlay::sequence<statistics> processor_stats(p);

    std::atomic<bool> finish = false;
    auto range_buffers = parlay::tabulate(p, [&] (size_t i) {
           return std::vector<K>(100ul + 2*std::max(range_size, trans_size));});

    t.start();
    auto start = std::chrono::system_clock::now();
    auto get_time = [] () {return std::chrono::system_clock::now();};
    auto time_diff = [] (auto start_time) {
      auto current = std::chrono::system_clock::now();
      using nano = std::chrono::nanoseconds;
      return std::chrono::duration_cast<nano>(current - start_time).count();
    };
    
    // every once in a while check if time is over
    auto check_end = [&] (long& cnt, statistics& local_stats,
                          int proc_id, const auto& start_time) -> bool {
      if (cnt >= 100 || finish) {
        double duration = time_diff(start_time);
        if (duration > 1000000000 * trial_time || finish) {
          if (!finish) finish.store(true);
#ifdef TransStats
	  local_stats.transactions = tlf_internal::num_transactions - local_stats.transactions;
	  local_stats.aborts = tlf_internal::num_aborts - local_stats.aborts;
#endif
          processor_stats[proc_id] = local_stats;
          return true;
        }
        cnt = 0;
      }
      return false;
    };

    // Run a loop with just transactions
    auto run_transactions = [&] (long start, long end, long proc_id) {
      long cnt = 0;
      long j = start;
      long jj = start;
      statistics local_stats;
#ifdef TransStats
      local_stats.transactions = tlf_internal::num_transactions;
      local_stats.aborts = tlf_internal::num_aborts;
#endif
      auto start_time = get_time();
      auto start_op_time = start_time;

      while (true) {
        if (check_end(cnt, local_stats, proc_id, start_time)) return;

#ifdef Latency
        if (latency > 0) start_op_time = get_time();
#endif
        if (j + trans_size > end) j = start;
        if (jj + trans_size > end) jj = start + trans_size;

        // run trans_size operations atomically in a transaction
        auto [added, queries, found, update_success] = verlib::atomic_region([&] {
          int loc_added = 0;
          int loc_queries = 0;
          int loc_found = 0;
          int loc_update_success = 0;
          for (int k=0; k < trans_size; k++) {
            if (get_operation(jj+k, proc_id) == Find) {
              loc_queries += 1;
              auto val = tr.find_locked(all[j+k]);
              if (val.has_value()) loc_found += val.value()[0];
#ifdef UPSERT
	    } else if (use_upsert) {
	      if (tr.upsert_(all[j+k], default_value)) {loc_added++; loc_update_success++;}
#endif
            } else if (get_operation(jj+k, proc_id) == Insert) {
              if (tr.insert(all[j+k], default_value)) {loc_added++; loc_update_success++;}
            } else if (get_operation(jj+k, proc_id) == Remove) {
              if (tr.remove(all[j+k])) {loc_added--; loc_update_success++;}
            }
          }
          return std::optional{std::tuple{loc_added, loc_queries, loc_found, loc_update_success}};});
#ifdef Latency
        if (latency > 0)
          if (time_diff(start_op_time) < ((double) latency))
            within_latency++;
#endif
        local_stats.added_count += added;
        local_stats.query_count += queries;
        local_stats.query_total_value += found;
        local_stats.update_count += trans_size - queries;
        local_stats.update_success_count += update_success;
        j += trans_size;
        jj += trans_size;
        cnt += trans_size;
        local_stats.total += trans_size;
      } 
    };

    // **********************
    // Run a loop with operation mix
    // **********************
    auto run_operations = [&] (long start, long end, long proc_id) {
      long cnt = 0;
      long j = start;
      long jj = start;
      statistics local_stats;
#ifdef TransStats
      local_stats.transactions = tlf_internal::num_transactions;
      local_stats.aborts = tlf_internal::num_aborts;
#endif
      std::vector<K>& range_keys = range_buffers[proc_id];
      auto start_time = get_time();
      auto start_op_time = start_time;

      while (true) {
        if (check_end(cnt, local_stats, proc_id, start_time)) return;

#ifdef Latency
        if (latency > 0) start_op_time = get_time();
#endif
        if (get_operation(jj, proc_id) == Find) {
          local_stats.query_count++;
          auto val = do_ro_atomic([&] {return tr.find(all[j]);});
          if (val.has_value()) {
            local_stats.query_total_value += val.value()[0];
          }
#ifdef UPSERT
	} else if (use_upsert) {
	  local_stats.update_count++;
	  if (do_atomic([&] {return tr.upsert(all[j], default_value);})) {
	    local_stats.added_count++;
            local_stats.update_success_count++;
          }
#endif
        } else if (get_operation(jj, proc_id) == Insert) {
          local_stats.update_count++;
	  if (do_atomic([&] {return tr.insert(all[j], default_value);})) {
            local_stats.added_count++;
            local_stats.update_success_count++;
          }
        }
        else if (get_operation(jj, proc_id) == Remove) {
          local_stats.update_count++;
          if (do_atomic([&] {return tr.remove(all[j]);})) {
            local_stats.added_count--;
            local_stats.update_success_count++;
          }
        }

#ifdef Range_Search
        else if (get_operation(jj, proc_id) == Range) {
          K end = ((all[j] > max_key - range_gap)
                   ? max_key : all[j] + range_gap);
          local_stats.range_count++;
          bool use_speculative = true; //(range_size < 2048);
          local_stats.range_sum += verlib::with_snapshot([&] {
            long cnt=0;
            auto addf = [&] (K k, V v) {
              assert(cnt < range_keys.size());
              range_keys[cnt++] = k;
            };
            tr.range_(addf, all[j], end);
#ifdef LazyStamp
            if (verlib::aborted) {local_stats.retry_count++;}
#endif
            return cnt;}, use_speculative);
        }
#endif
        else { // multifind
          local_stats.mfind_count++;
          local_stats.query_total_value += verlib::with_snapshot([&] {
            long loc_found = 0;
            long loc = j;
            for (long k = 0; k < range_size; k++) {
              auto val = tr.find_(all[loc]);
              loc += 1;
              if (loc >= end) loc -= start; // wrap around if needed
              if(val.has_value()) loc_found += val.value()[0];
#ifdef LazyStamp
              if (verlib::aborted) {
                local_stats.retry_count++;
                return 0l;
              }
#endif
            }
            return loc_found;});
          j += range_size;
          if (j >= end) j = start;
          jj += range_size;
          if (jj >= end) jj = start+range_size;
          cnt += range_size;
          local_stats.total += range_size;
          continue;
        }
#ifdef Latency
        if (latency > 0)
          if (time_diff(start_op_time) < ((double) latency))
            within_latency++;
#endif
        if (++j >= end) j = start;
        if (++jj >= end) jj = start+7;
        cnt++;
        local_stats.total++;
      }
    };

    auto run_allocator_test = [&] (long start, long end, long proc_id) {
#ifndef TRACE
      std::cerr << "TRACE not defined!" << std::endl;
      exit(-1);
#else
      statistics local_stats;
      auto start_time = get_time();
      auto start_op_time = start_time;

      using trace_t = parlay::internal::alloc_log_t;
      using entry_t = trace_t::metadata_t;

      parlay::ThreadSpecific<trace_t>& trace = parlay::internal::my_log;

      std::set<entry_t::sz_t> size_classes_set = trace->get_size_classes();
      std::vector<size_t> size_classes(size_classes_set.begin(), size_classes_set.end());
      parlay::internal::pool_allocator pl_allctr = parlay::internal::pool_allocator(size_classes);

      //Store freelist (elements that will be freed in the future) stack
      // block_allocator correspondent to a size class
      std::vector<std::pair<
        std::unique_ptr<std::stack<void*>>,
        parlay::internal::block_allocator&>> allocator_freelist;
      size_t n_sz_classes = size_classes.size();

      for (const auto& sc : trace->get_size_classes()) {
        parlay::internal::block_allocator& blck_allctr = pl_allctr.get_block_allocator(sc);
        std::unique_ptr<std::stack<void*>> freelist_p =
          std::unique_ptr<std::stack<void*>>(new std::stack<void*>);
        allocator_freelist.push_back({std::move(freelist_p), blck_allctr});
      }

      bool replaying = std::getenv("PARLAY_REPLAY") != nullptr;
      if(!replaying) {
        std::cerr << "Not performing replay. "
        << "Please set PARLAY_REPLAY environment variable" << std::endl;
        exit(-1);
      }

      for_each(trace->begin(), trace->end(),
        [&](entry_t t) {
          entry_t::ts_t ts = t.get_ts();
          entry_t::sz_t sz = t.get_alloc_size();
          trace_t::ALLOC_T alloc_t = t.get_alloc_type();

          size_t sz_i = 0;
          while(size_classes[sz_i] < sz) {sz_i++;} //Find size class index

          std::pair<std::unique_ptr<std::stack<void*>>,
          parlay::internal::block_allocator&>& tup = allocator_freelist[sz_i];
          std::unique_ptr<std::stack<void*>>& freelist = tup.first;
          parlay::internal::block_allocator& allocator = tup.second;

          if(alloc_t == trace_t::ALLOC_T::ALLOC) { //allocation
            void *p = allocator.alloc();
            freelist->push(p);
            local_stats.num_allocs++;
          } else { //free

            if(!freelist->empty()) {
              allocator.free(freelist->top());
              freelist->pop();
              local_stats.num_frees++;
            } else { //No element of this size class to free
              //We approximate frees by freeing a node from the non-empty size
              //  class with the closest size
              //Assumes there is at least 2 or more size classes
              int32_t invalid = std::numeric_limits<int32_t>::max();
              int32_t chosen_index = invalid;
              int32_t sz_i_2_l = sz_i - 1;
              int32_t sz_2_l, dist_l;
              int32_t sz_i_2_r = sz_i + 1;
              int32_t sz_2_r, dist_r;
repeat:
              sz_i_2_l--;
              sz_i_2_r++;
              if(sz_i_2_l >= 0 && sz_i_2_l < n_sz_classes) {
                sz_2_l = size_classes[sz_i_2_l];
              } else {
                sz_i_2_l = invalid;
              }
              if(sz_i_2_r < n_sz_classes) {
                sz_2_r = size_classes[sz_i_2_r];
              } else {
                sz_i_2_r = invalid;
              }
              if(sz_i_2_l != invalid && sz_i_2_r != invalid) {
                //Both valid, check which is closest
                dist_l = sz - sz_2_l;
                dist_r = sz_2_r - sz;
                bool left_or_right = dist_l <= dist_r;
                std::unique_ptr<std::stack<void*>>& freelist2 = left_or_right ? 
                  allocator_freelist[sz_i_2_l].first :
                  allocator_freelist[sz_i_2_r].first;

                if(freelist2->empty()) {
                  //Check other
                  left_or_right = !left_or_right;
                  std::unique_ptr<std::stack<void*>>& freelist3 = left_or_right ? 
                    allocator_freelist[sz_i_2_l].first :
                    allocator_freelist[sz_i_2_r].first;
                  if(freelist3->empty())
                    goto repeat;
                }
                //Not empty!
                chosen_index = left_or_right ? sz_i_2_l : sz_i_2_r;

              } else if(sz_i_2_l != invalid) {
                //right is invalid
                std::unique_ptr<std::stack<void*>>& freelist2 = allocator_freelist[sz_i_2_l].first;
                if(freelist2->empty())
                  goto repeat;
                chosen_index = sz_i_2_l;

              } else if(sz_i_2_r != invalid) {
                //left is invalid
                std::unique_ptr<std::stack<void*>>& freelist2 = allocator_freelist[sz_i_2_r].first;
                if(freelist2->empty())
                  goto repeat;
                chosen_index = sz_i_2_r;

              } else { //Nothing to free 
                local_stats.num_failed_frees++;
                return; //equivalent to continue; in for_each
              }
              assert(chosen_index != invalid);
              auto& tup_chosen = allocator_freelist[chosen_index];
              std::unique_ptr<std::stack<void*>>& freelist_chosen = tup_chosen.first;
              parlay::internal::block_allocator& allocator_chosen = tup_chosen.second;
              allocator_chosen.free(freelist_chosen->top());
              freelist_chosen->pop();
              local_stats.num_approx_frees++;
            }
          }
          local_stats.total++;
      });
      processor_stats[proc_id] = local_stats;
#endif
    };

    parlay::internal::start_tracing();
    // loop across the processors
    parlay::parallel_for(0, p, [&] (size_t proc_id) {
        long start = proc_id * mp;
        long end = (proc_id + 1) * mp;
        if (test_allocator) {
          run_allocator_test(start, end, proc_id);
        } else if (trans_size > 0) 
          run_transactions(start,end,proc_id);
        else
          run_operations(start,end,proc_id);
    }, 1);
    parlay::internal::stop_tracing();


    double duration = t.stop();
    if (duration > 1.1 * trial_time)
      std::cout << "duration: " << duration << std::endl;

    if (test_allocator && clear) {
      parlay::type_allocator<size_t> allocator;
      allocator.clear();
    }

    statistics overall_stats = parlay::reduce(processor_stats, parlay::binary_op(adds, statistics()));
    
    if (i != 0 || skip_warmup_round) { // don't report zeroth round -- warmup
      if (finish && (duration < trial_time/4))
        std::cout << "warning out of samples, finished in "
                  << duration << " seconds" << std::endl;

      size_t num_ops = overall_stats.total;
      if (latency > 0) {
        std::cout << "percent within latency = " << (100.0*overall_stats.within_latency)/num_ops << std::endl;
      }

      double percent_aborts = (overall_stats.transactions == 0 ?
                               0.0 :
                               overall_stats.aborts * 100.0 / overall_stats.transactions);
      
      if (range_query_threads == 0) {
        std::cout << std::setprecision(4)
                  << P.commandName() << ","
                  << update_percent << (use_upsert ? "%upsert," : "%update,")
                  << range_percent << "%range,"
                  << (multifind ? "mfind," : "find,") 
                  << "rs=" << range_size << ","
                  << "trans=" << trans_size << ","
                  << "n=" << n << ","
                  << "p=" << p << ","
                  << "z=" << zipfian_param << ","
#ifdef TransStats
		  << "%aborts=" << percent_aborts << ","
#endif
#ifdef REPORT_BYTES
                  //get the total number of bytes allocated
                  //<< "mem=" << getMemoryUsageBytes() << ","
                  << "mem=" << getMemoryUsageBytes()/(1024*1024) << "MiB,"
#endif
                  << (!use_thread_perc ? num_ops
                        //dont count unsuccessful updates when using thread_perc
                        //  some allocators have a very unbalanced successfull/unsuccessfull count
                        : (num_ops - (overall_stats.update_count - overall_stats.update_success_count)))
                          / (duration * 1e6) << std::endl;
      } else {
        std::cout << std::setprecision(4)
                  << P.commandName() << ","
                  << update_percent << "%update,"
                  << range_percent << "%range,"
                  << "rs=" << range_size << ","
                  << "trans=" << trans_size << ","
                  << "n=" << n << ","
                  << "p=" << p << ","
                  << "rq_threads=" << range_query_threads << ","
                  << "z=" << zipfian_param << ","
#ifdef REPORT_BYTES
                  //get the total number of bytes allocated
                  << "mem=" << getMemoryUsageBytes() << ","
                  << "mem=" << getMemoryUsageBytes()/(1024*1024) << "MiB,"
#endif
                  << "rq_throughput=" << overall_stats.range_count / (duration * 1e6) << ","
                  << "update_throughput=" << overall_stats.update_count / (duration * 1e6) << std::endl;
      }

      if (do_check) {
        size_t final_cnt = tr.check();
        long total_added = overall_stats.added_count;
        long total_updates = overall_stats.update_count;
        double update_successes = overall_stats.update_success_count;
        double update_success_ratio = update_successes / total_updates;
        long total_mfind = overall_stats.mfind_count;
        long total_queries = overall_stats.query_count + total_mfind * range_size;
        double total_found = overall_stats.query_total_value / default_value[0];
        double success_ratio = total_found / total_queries;
        if (verbose) {
          std::cout << "total updates =     " << total_updates 
                    << "\nsuccesful updates = " << (long) update_successes
                    << "\ntotal queries =     " << total_queries 
                    << "\nsuccesful queries = " << (long) total_found
                    << "\ninitial size =      " << initial_size
                    << "\ntotal added =       " << total_added
                    << "\nfinal size =        " << final_cnt 
                    << std::endl;
        }

	if (use_upsert) {
	  if (success_ratio != 1 && update_percent != 100)
	    std::cout << "bad query success ratio, should be 1 is "
		      << success_ratio << std::endl;
	  if (update_success_ratio != 0 && total_updates != 0 && !warmup)
	    std::cout << "bad upsert success ratio, should be 0 is "
		      << update_success_ratio << std::endl;
	} else {
	  if (success_ratio < .45 || success_ratio > .55)
	    std::cout << "bad query success ratio, should be .5 is "
		      << success_ratio << std::endl;
	  if (update_success_ratio < .3 || update_success_ratio > .55)
	    std::cout << "bad update success ratio, should be .5 is "
		      << update_success_ratio << std::endl;
	}
        if (multifind && verbose) {
          long mfind_sum = overall_stats.mfind_count;
          long retry_sum = overall_stats.retry_count;
#ifdef LazyStamp
          std::cout << "retry percent = " << (double) 100 * retry_sum / mfind_sum
                    << std::endl;
#endif
        }
        if (range_percent > 0 || range_query_threads > 0) {
          long range_sum = overall_stats.range_sum;
          long retry_sum = overall_stats.retry_count;
          long num_queries = overall_stats.range_count;
          long av_range_size = ((float) range_sum) / num_queries;
          if (av_range_size > 1.05 * range_size || av_range_size < .95 * range_size)
            std::cout << "bad average range size: "
                      << av_range_size << std::endl;
          if (verbose)
            std::cout << "average range size = " << ((float) range_sum) / num_queries
#ifdef LazyStamp
                      << ", retry percent = " << (double) 100 * retry_sum / num_queries 
#endif
                      << std::endl;
        }

        if (initial_size + total_added != final_cnt) {
          std::cout << "bad size: initial size = " << initial_size 
                    << ", added " << total_added
                    << ", final size = " << final_cnt 
                    << std::endl;
        }
      }
      // if (stats) {
      //   descriptor_pool.stats();
      //   log_array_pool.stats();
      //   os.stats();
      // }
    }
    parlay::parallel_for(0, nn, [&] (size_t i) {
      do_atomic([&] {tr.remove(unique[i]);});
    });
    if (stats) {
      SetType::print_stats();
      if(test_allocator) {
        std::cout
        << "#allocs: " << overall_stats.num_allocs << std::endl
        << "#frees: " << overall_stats.num_frees << std::endl
        << "#approx frees: " << overall_stats.num_approx_frees << std::endl
        << "#failed frees: " << overall_stats.num_failed_frees << std::endl;
      }
    }
    } //end of forced scope
    if (clear) {
      bool cleared = SetType::clear();
      if(!cleared)
        std::cerr << "Some memory pools were not cleared!" << std::endl;
    }
  }
  SetType::clear();
} //run_mixed_operations

template<typename SetType, typename K>
void run_insert_find_remove(const parlay::sequence<K>& all,
                            long unique_count,
                            commandLine P) {
  long m = all.size();
  bool clear = P.getOption("-clear");
  int rounds = P.getOptionIntValue("-r", 1);
  long buckets = P.getOptionLongValue("-bu", unique_count);
  bool verbose = P.getOption("-verbose");
  bool do_check = ! P.getOption("-no_check");
  bool stats = P.getOption("-stats");
  
  for (int i = 1; i < rounds+1; i++) {
    auto tr = SetType(buckets);
    if (verbose) std::cout << "round " << i << std::endl;
    parlay::internal::timer t;

    // millions of operations per second
    auto mops = [=] (double time) -> float {return m / (time * 1e6);};
    long len = parlay::remove_duplicates(all).size();
    parlay::sequence<bool> flags(m);
    using V = typename SetType::V;
    V default_value;
    default_value[0] = 123;
    
    t.start();
    parlay::parallel_for(0, m, [&] (size_t i) {
        flags[i] = tr.insert(all[i], default_value);
        //std::cout << flags[i] << ", " << all[i] << ", " << tr.check() << std::endl;
                               });

    std::cout << "insert," << m << "," << mops(t.stop()) << std::endl;

    if (do_check) {
      long total = parlay::count(flags, true);
      long lenc = tr.check();
      if (lenc != len || total != len) {
        std::cout << "incorrect size after insert: distinct keys inserted = "
                  << len << " succeeded = " << total << " found = " << lenc
                  << std::endl;
      }
    }
    if (stats) {
      //descriptor_pool.stats();
      //log_array_pool.stats();
      SetType::print_stats();
    }

    auto search_seq = parlay::random_shuffle(all, parlay::random(1));
    t.start();
    parlay::parallel_for(0, m, [&] (size_t i) {
        auto x = tr.find(search_seq[i]);                                  
        if (!x.has_value()) {
          std::cout << "key not found, i = " << i << ", key = " << search_seq[i] << std::endl;
          abort();
        } else if (*x != default_value) {
          std::cout << "bad value " << (*x)[0] << " at key " << search_seq[i] << std::endl;
          abort();
        }
        });
    std::cout << "find," << m << "," << mops(t.stop()) << std::endl;

    auto delete_seq = parlay::random_shuffle(all, parlay::random(2));
    t.start();
    parlay::parallel_for(0, m, [&] (size_t i) {
                                 bool x = tr.remove(delete_seq[i]);
      });

    std::cout << "remove," << m << "," << mops(t.stop()) << std::endl;
    if (do_check) {
      len = tr.check();
      if (len != 0) {
        std::cout << "BAD LENGTH = " << len << std::endl;
        tr.print();
      } else if(verbose) {
        std::cout << "CHECK PASSED" << std::endl;
      }
    }
    //if (stats) os.stats();

  }
  if (clear) {
    SetType::clear();
  }
  if (stats) {
    if (clear) std::cout << "the following should be zero if no memory leak" << std::endl;
    SetType::print_stats();
  }
}
