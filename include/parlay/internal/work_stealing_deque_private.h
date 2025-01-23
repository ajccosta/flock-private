#pragma once

//Continuous array version of ABP work-stealing deque

#include <cassert>

#include <atomic>
#include <iostream>
#include <utility>

#include "continuous_array.h"
#include "quiescent_manager.h"

namespace parlay {
namespace internal {

//#include "stats.hh"
//extern struct stats *st;

// Deque based on "Dynamic Circular Work-Stealing Deque" from David Chase and Yossi Lev
//  (which was based on Arora, Blumofe, and Plaxton SPAA, 1998).
//
// Supports:
//
// push_bottom:   Only the owning thread may call this
// pop_bottom:    Only the owning thread may call this
// pop_top:    Non-owning threads may call this
//
template <typename V>
struct alignas(128) PrivateDeque {

  //The ordering of these variables matters due to cache contention
  continuous_array<V*> *deq;
  uint64_t N; //Number of items in the local portion
  int64_t anchor_drop; //At which num_pops value should we attempt to "drop the anchor"
  uint64_t last_anchor; //Where was last anchor dropped
  int64_t num_pops; //#pop ops until next quiescent check
  alignas(64) std::atomic<uint64_t> bot; //index of where owner is pushing/popping
  alignas(64) i_quiescent_manager *qm;
  alignas(64) std::atomic<uint64_t> top; //index of where thiefs are stealing

  PrivateDeque(uint64_t N, int64_t anchor_drop, i_quiescent_manager *qm)
    : N(N), anchor_drop(anchor_drop), num_pops(0), bot(0), qm(qm), top(0) {

    deq = new continuous_array<V*>();
  }

  ~PrivateDeque() {
    delete qm;
    delete deq;
  }

  // Adds a new val to the bottom of the queue. Only the owning
  // thread can push new items. This must not be called by any
  // other thread.
  bool push_bottom(V* val) {
    //STATS(st->push_totl_incr());
    auto b = bot.load(std::memory_order_relaxed); // atomic load
    deq->put_head(b, val);
    //std::atomic_thread_fence(std::memory_order_release);
    bot.store(b + 1, std::memory_order_seq_cst);  // shared store
    //TODO can we do this branchless?
    if (num_pops < (int64_t) N) { //Don't surpass maximum number of num_pops
        num_pops++; //We can do 1 more syncless pop because of this push
        //STATS(st->num_pop_incr());
    }
    last_anchor = b + 1;
    //STATS(st->push_succ_incr());
    return true; //We use this to count every successful push op. (which is all of them)
  }

  // Pop an item from the top of the queue, i.e., the end that is not
  // pushed onto. Threads other than the owner can use this function.
  //
  // Returns {val, empty}, where empty is true if val was the
  // only val on the queue, i.e., the queue is now empty
  std::pair<V*, bool> pop_top() {
    //STATS(st->steal_totl_incr());
    auto t = top.load(std::memory_order_acquire);    // atomic load
    std::atomic_thread_fence(std::memory_order_seq_cst);
    auto b = bot.load(std::memory_order_acquire);  // atomic load
    assert(b + 1 >= t); //Check invariant that bot never strays more than 1 from top
    if (b > t + N) {
      qm->unset_quiescent(t); //Announce that we are not quiescent
      if (top.compare_exchange_strong(t, t + 1)) {
        //STATS(st->steal_succ_incr());
        auto val = deq->get_tail(t);
        qm->set_quiescent(t + 1); //We updated top to t+1, announce it and quiesce.
        return {val, (b == t + 1 + N)};
      } else {
        //STATS(st->steal_cas_fail_incr());
        qm->set_quiescent(t); //Announce that we are quiescent again.
        return {nullptr, (b == t + 1 + N)};
      }
    }
    //STATS(st->steal_fail_incr());
    return {nullptr, true};
  }

  // Pop an item from the bottom of the queue. Only the owning
  // thread can pop from this end. This must not be called by any
  // other thread.
  V* pop_bottom() {
    uint64_t t;
    auto b = bot.load(std::memory_order_relaxed); // atomic load
    if (b == 0) {
      return nullptr;
    }
    //STATS(st->pop_totl_incr());
    b--;
    assert(b != UINT64_MAX); //Check for underflow
    bot.store(b, std::memory_order_relaxed); // shared store
    if (num_pops > 0) { //Safe to pop without synchronizing
      num_pops--;
      //STATS(st->num_pop_decr());
      //No extra checks needed. bot was guaranteed to be > 0
      if (num_pops <= anchor_drop) {
        //STATS(st->quiescence_check_totl_incr());
        if (qm->quiescence_check()) {
          //STATS(st->quiescence_check_succ_incr());
          std::atomic_thread_fence(std::memory_order_seq_cst);
          if(last_anchor != 0 && b < last_anchor) {
            //If b is greater than last_anchor the public portion does not grow downward
            auto old_num_pops = num_pops;
            t = top.load(std::memory_order_relaxed);
            //We can do more pops than before but don't allow invalid state
            num_pops = std::min(N - (last_anchor - b), b - t); 
            //STATS(st->quiescence_check_num_pop_incr(num_pops - old_num_pops));
            assert(b - num_pops >= t); //Check invariant that bot never strays more than 1 from top
          }
          last_anchor = b; //drop anchor at current bot value
        }
      }
      //STATS(st->pop_succ_incr());
      //STATS(st->pop_syncless_incr());
      return deq->get_head(b);
    }
    //TODO if we read top here, dont read again
    //STATS(st->quiescence_check_totl_incr());
    //if (qm->quiescence_check()) {
    //  std::atomic_thread_fence(std::memory_order_seq_cst);
    //  STATS(st->quiescence_check_succ_incr());
    //  if(last_anchor != 0 && b < last_anchor) {
    //    //If b is greater than last_anchor the public portion does not grow downward
    //    auto old_num_pops = num_pops;
    //    auto t = top.load(std::memory_order_relaxed);
    //    //We can do more pops than before but don't allow invalid state
    //    num_pops = MIN(N - (last_anchor - b), b - t); 
    //    STATS(st->quiescence_check_num_pop_incr(num_pops - old_num_pops));
    //  }
    //  last_anchor = b; //we performed a barrier and quiescence check, drop anchor
    //  if (num_pops > 0) { //try again to perform syncless pop
    //    num_pops--;
    //    STATS(st->num_pop_decr());
    //    STATS(st->pop_succ_incr());
    //    STATS(st->pop_syncless_incr());
    //    return deq->get_head(b);
    //  }
    //}
    std::atomic_thread_fence(std::memory_order_seq_cst);
    //STATS(st->pop_syncfull_incr());
    t = top.load(std::memory_order_relaxed); // atomic load
    V* val;
    if (t <= b) {
      val = deq->get_head(b);
      if (t == b) {
        //STATS(st->pop_syncfull_cas_incr());
        if(!top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
          val = nullptr;
          //STATS(st->pop_syncfull_cas_fail_incr());
        } else {
          //STATS(st->pop_succ_incr());
          //STATS(st->pop_syncfull_succ_incr());
        }
        bot.store(b + 1, std::memory_order_relaxed);
      } else {
        //STATS(st->pop_succ_incr());
        //STATS(st->pop_syncfull_succ_incr());
      }
    } else {
      val = nullptr;
      bot.store(b + 1, std::memory_order_relaxed);
    }
    return val;
  }

  size_t size() {
    return real_size() - N;
  }

  size_t real_size() {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    return bot.load(std::memory_order_seq_cst) - top.load(std::memory_order_seq_cst);
  }

  void clear() {
    while(real_size() != 0) {pop_bottom();}
  }
};

}  // namespace internal
}  // namespace parlay
