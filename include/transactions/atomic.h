// Main code for atomic region
// Includes:
//   atomic(F f), which calls
//   commit(transaction_descriptor* descriptor), which calls
//   validate(transaction_descriptor* descriptor)

#ifndef TLF_ATOMIC_H_
#define TLF_ATOMIC_H_

namespace verlib {

#ifdef TPCC_STATS
#include<Medley/src/RMap.hpp>
#endif

thread_local TS start_stamp = 0;

// validate the versioned pointers that were read
void validate(transaction_descriptor* descriptor) {
  auto &log = descriptor->validate_ptr_log;
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

void commit (transaction_descriptor* descriptor) {
  if (is_helping()) trans_descriptor_pool.acquire(descriptor);
  flck::skip_if_done([&] {
  assert(descriptor != nullptr);
  auto& write_log = descriptor->write_ptr_log;
  // Install writes at head of version lists
  std::pair<versioned*,versioned*> pointers[write_log.size()];

  for (int i=0; i < write_log.size(); i++) {
    auto [location, value] = write_log[i];
    pointers[i] = install_store(location, value, descriptor);
  }

  // Set timestamp to go into validate mode
  descriptor->time_stamp = tbs; // logged load 

  // Validate the reads (not idempotent)
  flck::non_idempotent([=] {validate(descriptor);}); 
  TS ts = descriptor->time_stamp.load();

  // finalize logged writes (whether passed or failed)
  for (int i=0; i < write_log.size(); i++) {
    auto [location, old_value] = write_log[i];
    finalize_store(location, pointers[i], ts);
  }
  });  
}

// wrapper for a transaction
template <typename F>
auto atomic(F f) { 
  try_count = 0;
  int delay = 0;
  int max_delay = 0;
  while (true) {
    auto r = flck::with_epoch([&] {
      in_lock = 0;
      start_stamp = global_stamp.get_read_stamp();
      old_version_read = false;
      transaction_descriptor* descriptor = trans_descriptor_pool.New();
      current_transaction = descriptor;
      auto x = f();
      current_transaction = nullptr;
      bool passed = x.has_value(); 
      if (passed && descriptor->write_ptr_log.size() > 0) {
        // if transactions has stores and an old version was read, then abort
        if (old_version_read) passed = false;
        else { // otherwise acquire locks in sorted order, and commit
          bool use_help = (try_count % 100) == 99;
          std::sort(descriptor->lock_log.data(),
                    descriptor->lock_log.data() + descriptor->lock_log.size());
          passed = run_with_locks_rec(0, descriptor, [=] {
               commit(descriptor); return true;}, use_help);
          passed = passed && (descriptor->time_stamp.load() != failed);
        }
      }
      assert(current_transaction == nullptr);
      if (passed) {
        trans_descriptor_pool.Retire(descriptor);
        return x;
      } else {
#ifdef TPCC_STATS
        tpcc_aborts[flck::internal::worker_id()*TPCC_PADDING]++;
#endif
        auto& retired_log = descriptor->retired_log;

        // undo the logged retires
        for (int i=0; i < retired_log.size(); i++)
          epoch::internal::undo_Retire(retired_log[i]);

        // undo logged allocates
        auto& allocated_log = descriptor->allocated_log;
        for (int i=0; i < allocated_log.size(); i++)
          epoch::internal::undo_Allocate(allocated_log[i]);

        if (delay == 0) {
#ifdef Validate
          delay = 5 * (1  + 4 * descriptor->lock_log.size());
          max_delay = 16 * delay;
#else
          delay = 100 * (1  + 4 * descriptor->lock_log.size());
          max_delay = 16 * delay;
#endif
        } else delay = std::min((int) std::ceil(1.2 * delay), max_delay);
        for (volatile int i=0; i < delay; i++);

        if (try_count++ > 1000000000l/max_delay) {
          std::cout << "looks like an infinite retry loop" << std::endl;
          abort();
        }

        trans_descriptor_pool.Retire(descriptor);
        using RT = decltype(x);
        return RT();
      }});
    if (r.has_value()) return *r;
  }
}

template <typename F>
auto do_now(F f) {
  //transaction_descriptor* tmp = current_transaction;
  //current_transaction = nullptr;
  auto x = f();
  //current_transaction = tmp;
  return x;
}

}

#endif // TLF_ATOMIC_H_
