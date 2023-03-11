// Code for transactions
// One allocated for every transaction

thread_local TS start_stamp = 0;

// validate the versioned pointers that were read
void validate_reads(trans_descriptor* descriptor) {
  auto &log = descriptor->validate_ptr_log;
  //auto log = &validate_ptr_log;
  bool passed = true;
  TS ts = global_stamp.get_write_stamp();
  for (int i=0; i < log.size(); i++) {
    if (descriptor->time_stamp.load() != tbs) return;
    auto [location, expected] = log[i];
    if (!validate_pointer(location, expected)) {
      passed = false; break;}
  }
  descriptor->atomic_set_stamp(passed ? ts : failed);
}

bool commit_transaction (trans_descriptor* descriptor) {
  bool passed = true;

  // Install writes at head of version lists
  current_transaction = descriptor; //(trans_descriptor*) 22; //descriptor;
  auto& write_log = descriptor->write_ptr_log;
  for (int i=0; i < write_log.size(); i++) {
    auto [location, value] = write_log[i];
    install_versioned_store(location, value, descriptor);
  }
  current_transaction = nullptr;

  descriptor->time_stamp = tbs; // logged load and cas (aba_free)
  validate_reads(descriptor);  // not idempotent
  TS ts = descriptor->time_stamp.load();
  passed = (ts != failed);

  // finalize logged writes (whether passed or failed)
  for (int i=0; i < write_log.size(); i++) {
    auto [location, old_value] = write_log[i];
    //finalize_versioned_store(location, old_value, ts);
    finalize_versioned_store(location, ts);
  }
  
  return passed;
}

void clear_logs() {
  lock_log.clear();
  //write_ptr_log.clear();
  //validate_ptr_log.clear();
  //allocated_log.clear();
  //retired_log.clear();
}

// wrapper for a transaction
template <typename F>
auto with_transaction(F f) { 
  try_count = 0;
  int delay = 0;
  int max_delay = 0;
  while (true) {
    auto r = flck::with_epoch([&] {
	  clear_logs();
	  in_lock = false;
	  start_stamp = global_stamp.get_read_stamp();
	  trans_aborted = false;
	  trans_descriptor* descriptor = trans_descriptor_pool.new_obj();
	  current_transaction = descriptor;
	  auto x = f();
	  current_transaction = nullptr;
	  bool passed = x.has_value() && !trans_aborted;

#ifdef Immediate
	  if (passed)
	    if (lock_log.size() > 0)
	      passed = commit_transaction(descriptor);
	  // unlock all acquired locks
	  for (int i=0; i < lock_log.size(); i++) {
	    assert(lock_log[i]->is_self_locked());
	    lock_log[i]->unlock();
	  }
#else
	  if (passed)
	    if (lock_log.size() > 0)
	      passed = run_with_logged_locks([=] {
	         return commit_transaction(descriptor);}); 
#endif

	  assert(current_transaction == nullptr);
	  if (passed) {
	    trans_descriptor_pool.retire(descriptor);
	    return x;
	  } else {
	    auto& retired_log = descriptor->retired_log;
	    // undo the logged retires
	    for (int i=0; i < retired_log.size(); i++)
	      flck::internal::undo_retire(retired_log[i]);
	    // undo logged allocates
	    auto& allocated_log = descriptor->allocated_log;
	    for (int i=0; i < allocated_log.size(); i++)
	      flck::internal::undo_allocate(allocated_log[i]);

	    if (delay == 0) {
	      delay = 100 * (1  + 4 * lock_log.size());
	      max_delay = 4 * delay;
#ifdef Immediate
	      max_delay = 16 * delay;
#endif
	    } else delay = std::min(2 * delay, max_delay);
	    for (volatile int i=0; i < delay; i++);
	    if (try_count++ > 1000000000l/max_delay) {
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

template <typename F>
auto do_now(F f) {
  //trans_descriptor* tmp = current_transaction;
  //current_transaction = nullptr;
  auto x = f();
  //current_transaction = tmp;
  return x;
}
