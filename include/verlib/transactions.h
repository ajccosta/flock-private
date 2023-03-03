// Code for transactions
// One allocated for every transaction

thread_local int try_count;

bool commit_transaction (trans_descriptor* descriptor) {
  bool passed = true;

  // Install writes at head of version lists
  // current_transaction temporarility set so retire in
  // install_versioned_store is undone on abort
  current_transaction = descriptor;
  for (int i=0; i < write_ptr_log.cnt; i++) {
    auto [location, value] = write_ptr_log.vals[i];  
    write_ptr_log.vals[i].second = install_versioned_store(location, value, descriptor);
  }
  current_transaction = nullptr;

  for (int i=0; i < write_atomic_log.cnt; i++) {
    auto [location, value] = write_atomic_log.vals[i];  
    install_atomic_store(location, value, descriptor);

  }
  descriptor->time_stamp = tbs;

  TS ts = global_stamp.get_write_stamp();

  if (false) { //!need_to_validate) {
    descriptor->atomic_set_stamp(ts);
    ts = descriptor->time_stamp;
  } else {
    // validate versioned pointers
    for (int i=0; i < validate_ptr_log.cnt; i++) {
      auto [location, expected] = validate_ptr_log.vals[i];
      if (set_stamp(location->load(), true) != expected) {
	passed = false;
	break;
      }
    }
    if (passed)
      // validate values
      for (int i=0; i < validate_atomic_log.cnt; i++) {
	auto [location, expected] = validate_atomic_log.vals[i];
	if (get_value(*location, true) != expected) {
	  passed = false;
	  break;
	}
      }

    descriptor->atomic_set_stamp(passed ? ts : failed);
    ts = descriptor->time_stamp.load();
    passed = (ts != failed);
  }

  // finalize logged writes (whether passed or failed)
  for (int i=0; i < write_ptr_log.cnt; i++) {
    auto [location, value] = write_ptr_log.vals[i];
    finalize_versioned_store(location, value, ts);
  }

  for (int i=0; i < write_atomic_log.cnt; i++)
    finalize_atomic_store(write_atomic_log.vals[i].first, ts);

  
  return passed;
}

void clear_logs() {
  lock_log.clear();
  write_ptr_log.clear();
  write_atomic_log.clear();
  validate_ptr_log.clear();
  validate_atomic_log.clear();
  allocated_log.clear();
  retired_log.clear();
}

// wrapper for a transaction
template <typename F>
auto with_transaction(F f) { 
  try_count = 0;
  double time_taken = 200;
  int delay = 0;
  while (true) {
    auto r = flck::with_epoch([&] {
	  clear_logs();
	  auto start_time = std::chrono::system_clock::now();
	  in_lock = false;
	  trans_descriptor* descriptor = trans_descriptor_pool.new_obj();
	  current_transaction = descriptor;
	  auto x = f();
	  current_transaction = nullptr;
	  bool passed = x.has_value();

#ifdef Immediate
	  if (passed)
	    if (lock_log.cnt > 0)
	      passed = commit_transaction(descriptor);
	  // unlock all acquired locks
	  for (int i=0; i < lock_log.cnt; i++) {
	    assert(lock_log.vals[i]->is_self_locked());
	    lock_log.vals[i]->unlock();
	  }
#else
	  if (passed)
	    if (lock_log.cnt > 0)
	      passed = run_with_logged_locks([=] {
	         return commit_transaction(descriptor);}); //, try_count > 0);
#endif

	  assert(current_transaction == nullptr);
	  auto end_time = std::chrono::system_clock::now();
	  time_taken = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
	  if (passed) {
	    trans_descriptor_pool.retire(descriptor);
	    return x;
	  } else {
	    // undo the logged retires
	    for (int i=0; i < retired_log.cnt; i++)
	      *retired_log.vals[i] = true;
	    // undo logged allocates
	    for (int i=0; i < allocated_log.cnt; i++)
	      *allocated_log.vals[i] = false;

	    if (delay == 0) delay = 100 * (1  + 4 * lock_log.cnt);
	    int max_ratio = 8;
#ifdef Immediate
	    max_ratio = 16;
#endif
	    int wait = delay * std::min(max_ratio, 1 << try_count);
	    for (volatile int i=0; i < wait; i++);
	    if (try_count++ > 1000000000/(max_ratio*delay)) {
	      std::cout << "looks like an infinite retry loop" << std::endl;
	      abort();
	    }

	    trans_descriptor_pool.retire(descriptor);
	    using RT = decltype(x);
	    return RT();
	  }});
    if (r.has_value()) return *r;
  }
}

							   
