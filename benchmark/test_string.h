#include <string>
#include <iostream>
#include <sstream>
#include <limits>

#include <parlay/primitives.h>
#include <parlay/random.h>
#include <parlay/io.h>
#include <parlay/internal/get_time.h>
#include <parlay/internal/group_by.h>
#include "parse_command_line.h"

enum op_type : char {Find, Insert, Remove, Range, MultiFind};

template <typename SetType>
void test_sets(SetType& os, size_t default_size, commandLine P) {

  auto filename = P.getArgument(0);
  
  // number of threads
  int p = P.getOptionIntValue("-threads", parlay::num_workers());  
  int rounds = P.getOptionIntValue("-r", 1);

  // do fixed time experiment
  double trial_time = P.getOptionDoubleValue("-tt", 1.0);

  int range_size = P.getOptionIntValue("-rs",16);
  int range_percent = P.getOptionIntValue("-range",0);
  int multifind_percent = P.getOptionIntValue("-mfind",0);
  
#ifndef Range_Search
  if (range_percent > 0) {
    std::cout << "range search not implemented for this structure" << std::endl;
    return;
  }
#endif
  
  // shuffles the memory pool to break sharing of cache lines
  // by neighboring list/tree nodes
  bool shuffle = P.getOption("-shuffle");

  // verbose
  bool verbose = P.getOption("-v");

  // clear the memory pool between rounds
  bool clear = P.getOption("-clear");

  // check consistency, on by default
  bool do_check = ! P.getOption("-no_check");

  // print memory usage statistics
  bool stats = P.getOption("-stats");

  // for mixed update/query, the percent that are updates
  int update_percent = P.getOptionIntValue("-u", 20); 

  int range_query_threads = P.getOptionIntValue("-rqthreads", 0); 

  auto str = parlay::chars_from_file(filename);
  auto a = parlay::tokens(str, [] (char c) {return c == ' ';});
  long m = a.size();
  long nn = parlay::remove_duplicates(a).size();
  long n = nn/2;
  std::cout << "total strings = " << m << " unique strings = " << n << std::endl;

  // initially set to all finds
  parlay::sequence<op_type> op_types(m, Find);

  op_types = parlay::tabulate(m, [&] (size_t i) -> op_type {
      auto h = parlay::hash64(m+i)%200;
      if (h < update_percent) return Insert; 
      else if (h < 2*update_percent) return Remove;
      else if (h < 2*update_percent + 2*range_percent) return Range;
      else if (h < 2*update_percent + 2*range_percent + 2*multifind_percent)
	return MultiFind;
      else return Find; });
    
  parlay::internal::timer t;
  //if (shuffle) os.shuffle(n);

  for (int i = 1; i < rounds+1; i++) {
    auto tr = os.empty(n);
    if (verbose) std::cout << "round " << i << std::endl;

    std::cout << "before" << std::endl;
    parlay::parallel_for(0, m, [&] (size_t i) {
	auto k = a[parlay::hash64(i) % m];
	if(i%2==0) os.insert(tr, k, 123); 
	else os.remove(tr, k); }, 10, true);
    std::cout << "after" << std::endl;
    
    size_t initial_size = os.check(tr);

    size_t mp = m/p;
    // set some number of threads to only do range queries
    parlay::parallel_for(0, range_query_threads, [&] (size_t i) {
	for(int j = i*mp; j < (i+1)*mp; j++) op_types[j] = Range;
      }, 1);

    parlay::sequence<size_t> totals(p);
    parlay::sequence<long> addeds(p);
    parlay::sequence<long> range_counts(p);
    parlay::sequence<long> range_sums(p);
    parlay::sequence<long> mfind_counts(p);
    parlay::sequence<long> retry_counts(p);
    parlay::sequence<long> update_counts(p);
    parlay::sequence<long> query_counts(p);

    t.start();
    auto start = std::chrono::system_clock::now();
    std::atomic<bool> finish = false;
    auto range_buffers = parlay::tabulate(p, [&] (size_t i) {
	return std::vector<V>(100ul + 2*range_size);});

    std::cout << "start" << std::endl;
    parlay::parallel_for(0, p, [&] (size_t i) {
	int cnt = 0;
	size_t j = i*mp;
	size_t total = 0;
	long added = 0;
	long range_count = 0;
	long range_sum = 0;
	long mfind_count = 0;
	long retry_count = 0;
	long update_count = 0;
	long query_count = 0;
	volatile long keysum = 0;
	//std::vector<K>& range_keys = range_buffers[i];
	while (true) {
	  // every once in a while check if time is over
	  if (cnt >= 100 || finish) { 
	    cnt = 0;
	    auto current = std::chrono::system_clock::now();
	    double duration = std::chrono::duration_cast<std::chrono::milliseconds>(current - start).count();
	    if (duration > 1000*trial_time || finish) {
	      if(!finish) finish.store(true);
	      totals[i] = total;
	      addeds[i] = added;
	      range_counts[i] = range_count;
	      range_sums[i] = range_sum;
	      mfind_counts[i] = mfind_count;
	      retry_counts[i] = retry_count;
	      update_counts[i] = update_count;
	      query_counts[i] = query_count;
	      return;
	    }
	  }
	  // check that no overflow
	  if (j >= (i+1)*mp) abort();

	  if (op_types[j] == Find) {
	    query_count++;
	    auto val = os.find(tr, a[j]);
	    if(val.has_value()) keysum += val.value();
	  }
	  else if (op_types[j] == Insert) {
	    update_count++;
	    if (os.insert(tr, a[j], 123)) added++;}
	  else if (op_types[j] == Remove) {
	    update_count++;
	    if (os.remove(tr, a[j])) added--;
	  } else { // multifind
	    mfind_count++;
	    keysum += verlib::with_snapshot([&] {
		long tmp_sum = 0;
		long loc = j;
		for (long k = 0; k < range_size; k++) {
		  auto val = os.find_(tr, a[loc]);
		  loc += 1;
		  if (loc >= (i+1)*mp) loc -= mp;
		  if(val.has_value()) tmp_sum += val.value();
#ifdef LazyStamp
		  if (verlib::aborted) {
		    retry_count++;
		    return 0l;
		  }
#endif
		}
		return tmp_sum;});
	    j += range_size;
	    if (j >= (i+1)*mp) j -= mp;
	    cnt += range_size;
	    total += range_size;
	    continue;
	  }
	  if (++j >= (i+1)*mp) j -= mp;
	  cnt++;
	  total++;
	}
      }, 1);
    double duration = t.stop();
    std::cout << "duration: " << duration << std::endl;

    if (i != 0) { // don't report zeroth round -- warmup
      if (finish && (duration < trial_time/4))
	std::cout << "warning out of samples, finished in "
		  << duration << " seconds" << std::endl;

      size_t num_ops = parlay::reduce(totals);

      if(range_query_threads == 0) {
	std::cout << std::setprecision(4)
		  << P.commandName() << ","
		  << update_percent << "%update,"
		  << range_percent << "%range,"
		  << multifind_percent << "%mfind,"
		  << "rs=" << range_size << ","
		  << "n=" << n << ","
		  << "p=" << p << ","
		  << num_ops / (duration * 1e6) << std::endl;
      } else {
	std::cout << std::setprecision(4)
		  << P.commandName() << ","
		  << update_percent << "%update,"
		  << range_percent << "%range,"
		  << multifind_percent << "%mfind,"
		  << "rs=" << range_size << ","
		  << "n=" << n << ","
		  << "p=" << p << ","
		  << "rq_threads=" << range_query_threads << ","
		  << "rq_throughput=" << parlay::reduce(range_counts) / (duration * 1e6) << ","
		  << "update_throughput=" << parlay::reduce(update_counts) / (duration * 1e6) << std::endl;
      }

      if (do_check) {
	size_t final_cnt = os.check(tr);
	long updates = parlay::reduce(addeds);
	if (multifind_percent > 0) {
	  long mfind_sum = parlay::reduce(mfind_counts);
	  long retry_sum = parlay::reduce(retry_counts);
#ifdef LazyStamp
	  std::cout << "retry percent = " << (double) 100 * retry_sum / mfind_sum
		    << std::endl;
#endif
	}

	if (initial_size + updates != final_cnt) {
	  std::cout << "bad size: intial size = " << initial_size 
		    << ", added " << updates
		    << ", final size = " << final_cnt 
		    << std::endl;
	} else if(verbose) {
	  std::cout << "CHECK PASSED" << std::endl;
	}
      }
    }

    parlay::parallel_for(0, m, [&] (size_t i) { os.remove(tr, a[i]); });

    os.retire(tr); // free the ord_set (should be empty, but not with arttree)
    if (clear) {
      os.clear();
      //descriptor_pool.clear();
      //log_array_pool.clear();
    }
    if (stats) {
      if (clear) std::cout << "the following should be zero if no memory leak" << std::endl;
      //descriptor_pool.stats();
      //log_array_pool.stats();
      os.stats();
    }
  }
}


