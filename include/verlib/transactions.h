// Code for transactions
// One allocated for every transaction

// set the stamp on the transaction descriptor
TS set_transaction_stamp(trans_descriptor* desc) {
  TS t = global_stamp.get_write_stamp();
  TS old = tbs;
  if (desc->time_stamp.load() == tbs)
    desc->time_stamp.compare_exchange_strong(old, t);
  return desc->time_stamp.load();
}

// wrapper for a transaction
template <typename F>
auto with_transaction(F f) { //, bool retry=true) {
  auto x = trans_descriptor_pool.new_obj();
  auto r = flck::with_epoch([&] {
      current_transaction = x;
      auto r = flck::try_loop ([&] {
 	  lock_log.clear();
	  write_log.clear();
#ifdef TransUndo
	  allocated_log.clear();
	  retired_log.clear();
	  write_once_log.clear();
#endif
	  auto x = f();
	  TS ts;
	  bool passed = true;
	  if (x.has_value()) {
	    current_transaction->time_stamp = tbs;
	    ts = set_transaction_stamp(current_transaction);
	    // set stamps on logged writes
	    for (int i=0; i < write_log.cnt; i++) {
	      versioned* ver = strip_indirect(write_log.vals[i]->load());
	      ver->clear_current_trans(ts);
	    }
#ifdef TransUndo
	    // undo the retires on the logged allocates
	    for (int i=0; i < allocated_log.cnt; i++)
	      *allocated_log.vals[i] = true;
#endif
	  } else {
#ifdef TransUndo
	    passed = false;
	    //std::cout << "+" << std::endl;
	    
	    // undo the logged retires
	    for (int i=0; i < retired_log.cnt; i++)
	      *retired_log.vals[i] = true;
	    // undo logged writes
	    for (int i=0; i < write_once_log.cnt; i++) 
	      *write_once_log.vals[i] = false;
	    for (int i=0; i < write_log.cnt; i++)
	      undo_versioned_write(write_log.vals[i]);
#else
	    std::cout << "error: undo requested, but turned off" << std::endl;
	    abort(); 
#endif
	  }
	  for (int i=0; i < lock_log.cnt; i++) {
	    lock_log.vals[i]->unlock();
	  }
	  using RT = decltype(x);
	  if (passed) return x;
	  else return RT();});
	trans_descriptor* tmp = current_transaction;
	current_transaction = nullptr;
	trans_descriptor_pool.retire(tmp);
      return r;
    });
  return r;
}

							   
