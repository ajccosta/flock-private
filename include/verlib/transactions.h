// Code for transactions
// One allocated for every transaction

thread_local int try_count;

bool commit_transaction (trans_descriptor* descriptor) {
  bool passed = true;

  // current_transaction temporarility set so retire in
  // install_versioned_store is undone on abort
  // Install writes
  current_transaction = descriptor;
  for (int i=0; i < write_log.cnt; i++) {
    auto [location, value] = write_log.vals[i];  
    install_versioned_store(location, value, descriptor);
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
	if (get_value(location->load(), true) != expected) {
	  passed = false;
	  break;
	}
      }
    // for (int i=0; i < validate_val_log.cnt; i++) {
    //   auto [loc, expected] = validate_val_log.vals[i];
    //   if (loc->load() != expected) {
    //     passed = false;
    //     break;
    //   }
    descriptor->atomic_set_stamp(passed ? ts : failed);
    ts = descriptor->time_stamp.load();
    passed = (ts != failed);
  }

  // finalize logged writes (whether passed or failed)
  for (int i=0; i < write_log.cnt; i++) 
    finalize_versioned_store(write_log.vals[i].first, ts);

  for (int i=0; i < write_atomic_log.cnt; i++)
    finalize_atomic_store(write_atomic_log.vals[i].first, ts);

  if (passed) {
    // undo the retires on the logged allocates
    for (int i=0; i < allocated_log.cnt; i++)
      *allocated_log.vals[i] = true;
  }

  return passed;
}

void clear_logs() {
  lock_log.clear();
  write_log.clear();
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
  return flck::try_loop ([&] {
      return flck::with_epoch([&] {
	  clear_logs();
	  in_lock = false;
	  trans_descriptor* descriptor = trans_descriptor_pool.new_obj();
	  current_transaction = descriptor;
	  auto x = f();
	  current_transaction = nullptr;
	  bool passed = x.has_value();

#ifdef Immediate
	  if (passed)
	    passed = commit_transaction(descriptor);
	  // unlock all acquired locks
	  for (int i=0; i < lock_log.cnt; i++) {
	    assert(lock_log.vals[i]->is_self_locked());
	    lock_log.vals[i]->unlock();
	  }
#else
	  if (passed)
	    passed = run_with_logged_locks([=] {
	               return commit_transaction(descriptor);});
#endif
	  if (passed) {
	    trans_descriptor_pool.retire(descriptor);
	    return x;
	  } else {
	    // if count goes too high use lock for validation on next try
	    try_count++;

	    // undo the logged retires
	    for (int i=0; i < retired_log.cnt; i++)
	      *retired_log.vals[i] = true;

	    trans_descriptor_pool.retire(descriptor);
	    using RT = decltype(x);
	    return RT();
	  }});}, 1000, 10000); // start delay, max delay
}

							   
