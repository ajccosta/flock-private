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
#ifdef TransUndo
	  allocated_log.clear();
	  retired_log.clear();
	  write_once_log.clear();
#endif
	  assert(current_transaction->time_stamp == tbd);
	  auto x = f();
	  TS ts;
	  bool passed = x.has_value();
	  if (passed) {
	    current_transaction->time_stamp = tbs;
	    current_transaction->atomic_set_current_stamp();
	    ts = current_transaction->time_stamp.load();
	  } 
	  if (passed) {
	    // set stamps on logged writes
	    for (int i=0; i < write_log.cnt; i++) 
	      finalize_versioned_store(write_log.vals[i], ts);

#ifdef TransUndo
	    // undo the retires on the logged allocates
	    for (int i=0; i < allocated_log.cnt; i++)
	      *allocated_log.vals[i] = true;
#endif
	  } else {
#ifdef TransUndo
	    current_transaction->time_stamp = failed;
	    //std::cout << write_log.cnt << "+";

	    // if count goes too high uses lock for validation on next try
	    try_count++;
	    
	    // undo the logged retires
	    for (int i=0; i < retired_log.cnt; i++)
	      *retired_log.vals[i] = true; 

	    // undo logged write onces
	    for (int i=0; i < write_once_log.cnt; i++) {
	      auto [ptr,val] = write_once_log.vals[i];
	      *ptr = val;
	    }

	    // undo logged writes to versioned pointers
	    for (int i=0; i < write_log.cnt; i++)
	      finalize_versioned_store(write_log.vals[i], failed);
#else
	    std::cout << "error: undo requested, but turned off" << std::endl;
	    abort(); 
#endif
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

							   
