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
#ifdef Validate
	  validate_ptr_log.clear();
	  validate_val_log.clear();
#endif	  
	  auto x = f();
	  TS ts;
	  bool passed = x.has_value();
	  if (passed) {
	    current_transaction->time_stamp = tbs;
#ifdef Validate
	    ts = global_stamp.get_write_stamp();
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
	      for (int i=0; i < validate_val_log.cnt; i++) {
		auto [loc, expected] = validate_val_log.vals[i];
		if (loc->load() != expected) {
		  passed = false;
		  break;
		}
	      }
	    TS oldts = tbs;
	    TS newts = passed ? ts : failed;
	    current_transaction->time_stamp.compare_exchange_strong(oldts, newts);
	    passed = current_transaction->time_stamp.load() != failed;
#else
	    ts = set_transaction_stamp(current_transaction);
#endif
	  } 
	  if (passed) {
	    // set stamps on logged writes
	    for (int i=0; i < write_log.cnt; i++) {
	      versioned* ver = strip_indirect(write_log.vals[i]->load());
	      assert(ver != nullptr);
	      ver->clear_current_trans(ts);
	    }
#ifdef TransUndo
	    // undo the retires on the logged allocates
	    for (int i=0; i < allocated_log.cnt; i++)
	      *allocated_log.vals[i] = true;
#endif
	  } else {
#ifdef TransUndo
	    current_transaction->time_stamp = failed;

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
	      undo_versioned_write(write_log.vals[i]);
#else
	    std::cout << "error: undo requested, but turned off" << std::endl;
	    abort(); 
#endif
	  }
	  // unlock all acquired locks
	  for (int i=0; i < lock_log.cnt; i++) {
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

							   
