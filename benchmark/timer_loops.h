// Contains main timer loops
//
// run_mixed_operation:
// runs on given keys for a fixed amount of time specified by -tt with
// a mix of operations, specified as follows
//   -u <update percent>
//   -range <range query percent>
//   -mfind <multifind percent>
// any left over percent goes to finds.
//   -rs <size>
// specifies the size of the range query or multifind.
// If
//   -range_query_threads <num threads>
// is specified then that percent of threads are dedicated to range
// queries and the rest use the mix above.
// Reports throughput.
//
// run_insert_find_remove:
// inserts, then finds, then removes the given keys
//
template<typename K, typename SetType>
void run_mixed_operations(SetType& os,
			  parlay::sequence<K>& all,
			  const parlay::sequence<K>& unique,
			  float zipfian_param,
			  commandLine P) {
  int m = all.size();
  int nn = unique.size();
  int n = nn/2;

  int p = P.getOptionIntValue("-p", parlay::num_workers());  

  int rounds = P.getOptionIntValue("-r", 1);

  double trial_time = P.getOptionDoubleValue("-tt", 1.0);

  // Mix of operations
  int update_percent = P.getOptionIntValue("-u", 20); 
  int range_size = P.getOptionIntValue("-rs",16);
  int range_percent = P.getOptionIntValue("-range",0);
  int multifind_percent = P.getOptionIntValue("-mfind",0);
  int trans_size = P.getOptionIntValue("-trans", 0);
  int trans_percent = P.getOptionIntValue("-tp", 0);

  // mix of threads
  int range_query_threads = P.getOptionIntValue("-rqthreads", 0);
  
#ifdef Range_Search
  K max_key = ~0ul;
  K range_gap = (max_key/n)*range_size;

#else
  if (range_percent > 0) {
    std::cout << "range search not implemented for this structure" << std::endl;
    return;
  }
#endif

  // number of hash buckets for hash tables
  int buckets = P.getOptionIntValue("-bu", n);

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

  enum op_type : char {Find, Insert, Remove, Range, MultiFind, Trans};
 
  // initially set to all finds
  parlay::sequence<op_type> op_types(m, Find);

  op_types = parlay::tabulate(m, [&] (size_t i) -> op_type {
      auto h = parlay::hash64(m+i)%200;
      if (h < update_percent) return Insert; 
      else if (h < 2*update_percent) return Remove;
      else if (h < 2*(update_percent + range_percent)) return Range;
      else if (h < 2*(update_percent + range_percent + multifind_percent))
	return MultiFind;
      else if (h < 2*(update_percent + range_percent + multifind_percent + trans_percent))
	return Trans;
      else return Find; });
    
  parlay::internal::timer t;
  if (shuffle) os.shuffle(n);

  for (int i = 0; i < rounds+1; i++) {
    auto tr = os.empty(buckets);
    auto tr2 = os.empty(buckets);
    long len;
    if (do_check) {
      size_t len = os.check(tr);
      if (len != 0) {
	std::cout << "BAD LENGTH = " << len << std::endl;
      } else if(verbose) {
	std::cout << "CHECK PASSED" << std::endl;
      }
    }
      
    if (verbose) std::cout << "round " << i << std::endl;
    size_t initial_size;

    // insert n distinct elements
    parlay::parallel_for(0, n, [&] (size_t i) {
	os.insert(tr, unique[i], 123); }, 10, true);
    
    // insert and delete for a while
    parlay::parallel_for(0, 4*n, [&] (size_t i) {
	auto k = unique[parlay::hash64(i + i) % nn];
	if(i%2==0) os.insert(tr, k, 123); 
	else os.remove(tr, k); }, 10, true);

    if (do_check) {
      initial_size = os.check(tr);
      if (verbose) std::cout << "initial size = " << initial_size << std::endl;
    }

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
	   return std::vector<K>(100ul + 2*std::max(range_size, trans_size));});
					     
    parlay::parallel_for(0, p, [&] (size_t i) {
	int cnt = 0;
	int current_id = flck::internal::current_id;
	size_t j = i*mp;
	size_t total = 0;
	long added = 0;
	long range_count = 0;
	long range_sum = 0;
	long mfind_count = 0;
	long retry_count = 0;
	long update_count = 0;
	long query_count = 0;
	bool i_or_d = false;
	volatile long keysum = 0;
	std::vector<K>& range_keys = range_buffers[i];
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
	  assert(j < (i+1)*mp);

#ifdef Transactional
	  if (trans_size > 0) {
	    long tsize = std::min(j + trans_size, (i+1)*mp) - j;
	    if (tsize > range_keys.size()) {
	      std::cout << "transaction size too large" << std::endl;
	      abort();
	    }
	    for (int k=0; k < tsize; k++) range_keys[k] = all[j+k];
	    int loc_keysum;
		       
	    //std::sort(range_keys.begin(), range_keys.begin() + tsize);
	    added += verlib::with_transaction([&] {
               int loc_added = 0;
	       loc_keysum = 0;
	       for (int k=0; k < tsize; k++) {
#ifdef TransUndo
		 if (op_types[j+k] == Find) {
		   auto x = os.try_find(tr, range_keys[k]);
		   if (!x.has_value()) return std::optional<int>();
		   loc_keysum += (*x).has_value();
		 } else if (op_types[j+k] == Insert) {
		   auto x = os.try_insert(tr, range_keys[k], 123);
		   //if (!x.has_value() || verlib::trans_aborted) return std::optional<int>();
		   if (!x.has_value()) return std::optional<int>();
		   if (*x) loc_added++;
		 } else if (op_types[j+k] == Remove) {
		   auto x = os.try_remove(tr, range_keys[k]);
		   //if (!x.has_value()  || verlib::trans_aborted) return std::optional<int>();
		   if (!x.has_value()) return std::optional<int>();
		   if (*x) loc_added--;
		 }
#else
		 if (op_types[j+k] == Find) {
		   loc_keysum += os.find_locked(tr, range_keys[k]).has_value();
		 } else if (op_types[j+k] == Insert) {
		   if (os.insert_(tr, range_keys[k], 123)) loc_added++;
		 } else if (op_types[j+k] == Remove) {
		   if (os.remove_(tr, range_keys[k])) loc_added--;
		 }
#endif
	       }
	       return std::optional<int>(loc_added);});
	    keysum += loc_keysum;
	    i_or_d = !i_or_d;
	    j += tsize;
	    if (j >= (i+1)*mp) j -= mp;
	    cnt += tsize;
	    total += tsize;
	    continue;
	  } else if (op_types[j] == Trans) {
	    if (j < (i+1)*mp -2) {
	      update_count += 2;
	      added += verlib::with_transaction([&] {
						  bool ins = (parlay::hash64(j + i) % 2) == 0;
				if (ins) { 
				  auto x = os.try_insert(tr, all[j], 123);
				  if (!x.has_value()) return std::optional<int>();
				  auto y = os.try_remove(tr2, all[j]);
				  if (!y.has_value()) return std::optional<int>();
				  return std::optional<int>(*x ? 1 : 0);
				} else {
				  auto x = os.try_remove(tr, all[j]);
				  if (!x.has_value()) return std::optional<int>();
				  auto y = os.try_insert(tr2, all[j], 123);
				  if (!y.has_value()) return std::optional<int>();
				  return std::optional<int>(*x ? -1 : 0);}});
	      cnt += 2;
	      total += 2;
	    }
	    j += 2;
	    if (j >= (i+1)*mp) j -= mp;
	    continue;
	  } else
#endif
	  if (op_types[j] == Find) {
	    query_count++;
	    auto val = os.find(tr, all[j]);
	    if(val.has_value()) keysum += val.value();
	  } else if (op_types[j] == Insert) {
	    update_count++;
	    if (os.insert(tr, all[j], 123)) added++;
	  }
	  else if (op_types[j] == Remove) {
	    update_count++;
	    if (os.remove(tr, all[j])) added--;
	  }
#ifdef Range_Search
	  else if (op_types[j] == Range) {
	    K end = ((all[j] > max_key - range_gap)
		     ? max_key : all[j] + range_gap);
	    range_count++;
	    bool use_speculative = true; //(range_size < 2048);
	    range_sum += verlib::with_snapshot([&] {
                long cnt=0;
                auto addf = [&] (K k, V v) {
                  assert(cnt < range_keys.size());
                  range_keys[cnt++] = k;
                };
                os.range_(tr, addf, all[j], end);
#ifdef LazyStamp
                if (verlib::aborted) {retry_count++;}
#endif
                return cnt;}, use_speculative);
	  }
#endif
	  else { // multifind
	    mfind_count++;
	    keysum += verlib::with_snapshot([&] {
                long tmp_sum = 0;
                long loc = j;
                for (long k = 0; k < range_size; k++) {
                  auto val = os.find_(tr, all[loc]);
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
    //os.print(tr);
    if (duration > 1.1 * trial_time)
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
		  << "trans=" << trans_size << ","
		  << "n=" << n << ","
		  << "p=" << p << ","
		  << "z=" << zipfian_param << ","
		  << num_ops / (duration * 1e6) << std::endl;
      } else {
	std::cout << std::setprecision(4)
		  << P.commandName() << ","
		  << update_percent << "%update,"
		  << range_percent << "%range,"
		  << multifind_percent << "%mfind,"
		  << "rs=" << range_size << ","
		  << "trans=" << trans_size << ","
		  << "n=" << n << ","
		  << "p=" << p << ","
		  << "rq_threads=" << range_query_threads << ","
		  << "z=" << zipfian_param << ","
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
	if (range_percent > 0 || range_query_threads > 0) {
	  long range_sum = parlay::reduce(range_sums);
	  long retry_sum = parlay::reduce(retry_counts);
	  long num_queries = parlay::reduce(range_counts);
	  std::cout << "average range size = " << ((float) range_sum) / num_queries
#ifdef LazyStamp
		    << ", retry percent = " << (double) 100 * retry_sum / num_queries 
#endif
		    << std::endl;
	}

	if (initial_size + updates != final_cnt) {
	  std::cout << "bad size: initial size = " << initial_size 
		    << ", added " << updates
		    << ", final size = " << final_cnt 
		    << std::endl;
	} else if(verbose) {
	  std::cout << "CHECK PASSED" << std::endl;
	}
      }
      // if (stats) {
      //   descriptor_pool.stats();
      //   log_array_pool.stats();
      //   os.stats();
      // }
    }
    parlay::parallel_for(0, nn, [&] (size_t i) {
	  os.remove(tr, unique[i]); });
    os.retire(tr);
    if (clear) os.clear();
  }
}

template<typename K, typename SetType>
void run_insert_find_remove(SetType& os,
			    const parlay::sequence<K>& all,
			    long unique_count,
			    commandLine P) {
  long m = all.size();
  bool clear = P.getOption("-clear");
  int rounds = P.getOptionIntValue("-r", 1);
  int buckets = P.getOptionIntValue("-bu", unique_count);
  bool verbose = P.getOption("-verbose");
  bool do_check = ! P.getOption("-no_check");
  bool stats = P.getOption("-stats");

  for (int i = 1; i < rounds+1; i++) {
    auto tr = os.empty(buckets);
    if (verbose) std::cout << "round " << i << std::endl;
    parlay::internal::timer t;

    // millions of operations per second
    auto mops = [=] (double time) -> float {return m / (time * 1e6);};
    long len = parlay::remove_duplicates(all).size();
    parlay::sequence<bool> flags(m);
  
    t.start();
    parlay::parallel_for(0, m, [&] (size_t i) {
	flags[i] = os.insert(tr, all[i], 123);
      });

    std::cout << "insert," << m << "," << mops(t.stop()) << std::endl;

    if (do_check) {
      long total = parlay::count(flags, true);
      long lenc = os.check(tr);
      if (lenc != len || total != len) {
	std::cout << "incorrect size after insert: inserted="
		  << len << " succeeded=" << total << " found=" << lenc
		  << std::endl;
      }
    }
    if (stats) {
      //descriptor_pool.stats();
      //log_array_pool.stats();
      os.stats();
    }

    auto search_seq = parlay::random_shuffle(all, parlay::random(1));
    t.start();
    parlay::parallel_for(0, m, [&] (size_t i) {
	if (!os.find(tr, search_seq[i]).has_value()) {
	  std::cout << "key not found, i = " << i << ", key = " << search_seq[i] << std::endl;
	  abort();
	}});
    std::cout << "find," << m << "," << mops(t.stop()) << std::endl;

    auto delete_seq = parlay::random_shuffle(all, parlay::random(2));
    t.start();
    parlay::parallel_for(0, m, [&] (size_t i) {
	os.remove(tr, delete_seq[i]);
      });

    std::cout << "remove," << m << "," << mops(t.stop()) << std::endl;
    if (do_check) {
      len = os.check(tr);
      if (len != 0) {
	std::cout << "BAD LENGTH = " << len << std::endl;
      } else if(verbose) {
	std::cout << "CHECK PASSED" << std::endl;
      }
    }
    //if (stats) os.stats();

    os.retire(tr); // free the ord_set (should be empty, but not with arttree)
    if (clear) {
      os.clear();
    }
    if (stats) {
      if (clear) std::cout << "the following should be zero if no memory leak" << std::endl;
      os.stats();
    }
  }

}
