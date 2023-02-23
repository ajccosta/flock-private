// Code for transactions
// One allocated for every transaction

thread_local int try_count;

// wrapper for a transaction
template <typename F>
auto with_transaction(F f) { 
  try_count = 0;
  auto r = flck::try_loop ([&] {
      return flck::with_epoch([&] {
	  current_transaction = trans_descriptor_pool.new_obj();
 	  lock_log.clear();
	  write_log.clear();
	  allocated_log.clear();
	  retired_log.clear();
	  //write_once_log.clear();
	  write_atomic_log.clear();
	  validate_ptr_log.clear();
	  validate_atomic_log.clear();
	  //validate_val_log.clear();
	  assert(current_transaction->time_stamp == tbd);
	  auto x = f();
	  TS ts;
	  bool passed = x.has_value();
	  //bool need_to_validate = (validate_ptr_log.cnt > 0 || validate_val_log.cnt > 0);
	  bool need_to_validate = false;
#ifdef Validate
	  need_to_validate = true;
#endif
	  if (passed) {
	    // install writes
	    for (int i=0; i < write_log.cnt; i++) 
	      install_versioned_store(write_log.vals[i].first, write_log.vals[i].second, true);
	    for (int i=0; i < write_atomic_log.cnt; i++) 
	      install_atomic_store(write_atomic_log.vals[i].first, write_atomic_log.vals[i].second);
	    current_transaction->time_stamp = tbs;
	    ts = global_stamp.get_write_stamp();
	    if (!need_to_validate) {
	      current_transaction->atomic_set_stamp(ts);
	      ts = current_transaction->time_stamp;
	    } else {
	      // validate versioned pointers
	      for (int i=0; i < validate_ptr_log.cnt; i++) {
		auto [loc, expected] = validate_ptr_log.vals[i];
		if (set_stamp(loc->load(), true) != expected) {
		  passed = false;
		  break;
		}
	      }
	      if (passed)
		// validate values
		for (int i=0; i < validate_atomic_log.cnt; i++) {
		  auto [loc, expected] = validate_atomic_log.vals[i];
		  if (get_value(loc->load(), true) != expected) {
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
	      current_transaction->atomic_set_stamp(passed ? ts : failed);
	      ts = current_transaction->time_stamp.load();
	      passed = ts != failed;
	    }
	    if (passed) {
	      // undo the retires on the logged allocates
	      for (int i=0; i < allocated_log.cnt; i++)
		*allocated_log.vals[i] = true;
	    }
	    // finalize logged writes (whether passed or failed)
	    for (int i=0; i < write_log.cnt; i++) 
	      finalize_versioned_store(write_log.vals[i].first, ts);

	    for (int i=0; i < write_atomic_log.cnt; i++)
	      finalize_atomic_store(write_atomic_log.vals[i].first);
	  }
	  if (!passed) {
	    //std::cout << write_log.cnt << "+";

	    // if count goes too high uses lock for validation on next try
	    try_count++;

	    // undo the logged retires
	    for (int i=0; i < retired_log.cnt; i++)
	      *retired_log.vals[i] = true; 

	    // undo logged write onces
	    // for (int i=0; i < write_once_log.cnt; i++) {
	    //   auto [ptr,val] = write_once_log.vals[i];
	    //   *ptr = val;
	    // }
	  }

	  // unlock all acquired locks
	  for (int i=0; i < lock_log.cnt; i++) {
	    assert(lock_log.vals[i]->is_self_locked());
	    lock_log.vals[i]->unlock();
	  }

	  // retire descripor
	  trans_descriptor* tmp = current_transaction;
	  current_transaction = nullptr;
	  trans_descriptor_pool.retire(tmp); 

	  // return optional value (empty if aborted)
	  using RT = decltype(x);
	  if (passed) return x;
	  else return RT();  }); });
  return r;
}

							   
