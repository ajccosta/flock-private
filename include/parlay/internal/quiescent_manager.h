#pragma once

#include <cstring>
#include <cassert>

#define INVALID_TID UINT32_MAX
__thread uint32_t tid = INVALID_TID;


//We want to overload the virtual function with ellipsis, but that generates warnings
//#pragma GCC diagnostic ignored "-Woverloaded-virtual"

class i_quiescent_manager {
  public:
    i_quiescent_manager(){}
    virtual ~i_quiescent_manager(){}
    virtual bool quiescence_check() = 0; //Check if grace period occurred since last check
    //TODO: would like it so that interface is variadic or uses ellipsis
    //	so that an implementation that does not use tail announcement doesn't need this
    virtual inline void set_quiescent(uint64_t tail_ann) = 0; //Say that we are quiescent
    virtual inline void unset_quiescent(uint64_t tail_ann) = 0; //Say that we are not quiescent
};


class ebr_qm : public i_quiescent_manager {
  private:
    unsigned int num_threads;
    
    struct alignas(64) quiescent_t {
      std::atomic<uint64_t> qui; //announcement variable AND quiescent bit
    };
    
    struct quiescent_t* quiescence_vars;
    unsigned int last_q_check = 0;
    uint64_t *tail_announcements_seen;
    
    inline bool check_thread_quiescence(uint32_t thread_id) {
      uint64_t qui, announcement, bit;
      qui = quiescence_vars[thread_id].qui.load(std::memory_order_relaxed);
      announcement = (qui >> 1);
      if(announcement > tail_announcements_seen[thread_id]) {
        //Has thread announced tail since last grace period?
        //  This only works because threads announce tail AFTER stealing
        tail_announcements_seen[thread_id] = announcement;
        return true;
      }
      //Is thread quiescent "now"?
      bit = (qui & 0x1);
      return bit == 1;
    }
  
  public:
    ebr_qm() {
      num_threads = parlay::internal::init_num_workers(); //Assume number of thread does not change
      quiescence_vars = new quiescent_t[num_threads](); //Initialized to 0
      tail_announcements_seen = new uint64_t[num_threads](); //Initialized to 0
    }
    
    ~ebr_qm() {
      delete quiescence_vars;
      delete tail_announcements_seen;
    }
    
    /*
       The only requirement is modification order consistency,
       which relaxed memory order guarantees.
    */
    inline void set_quiescent(uint64_t tail_ann) {
      quiescence_vars[parlay::my_thread_id()].qui.store((tail_ann << 1) | 0x1, std::memory_order_relaxed);
    }
    
    inline void unset_quiescent(uint64_t tail_ann) {
      quiescence_vars[parlay::my_thread_id()].qui.store((tail_ann << 1) | 0x0, std::memory_order_relaxed);
    }
    
    inline bool quiescence_check() {
      auto my_tid = parlay::my_thread_id();
      for(; last_q_check < num_threads &&
            (my_tid == last_q_check || check_thread_quiescence(last_q_check));
            last_q_check++);
      if(last_q_check == num_threads) {
        last_q_check = 0;
        return true;
      } else {
        return false;
      }
    }
};


class faa_qm : public i_quiescent_manager {
  private:
    //Number of in-flight threads
    std::atomic_uint_fast64_t n_inflight_threads{0};
  public:

    faa_qm(){}
    ~faa_qm(){}
    
    inline void set_quiescent([[maybe_unused]] uint64_t tail_ann) {
      //Ignore tail_ann but we must have same prototype
      n_inflight_threads.fetch_sub(1, std::memory_order_seq_cst);
    }
    
    inline void unset_quiescent([[maybe_unused]] uint64_t tail_ann) {
      //Ignore tail_ann but we must have same prototype
      n_inflight_threads.fetch_add(1, std::memory_order_seq_cst);
    }
    
    inline bool quiescence_check() {
      //mem order relaxed seems to improve performance
      return n_inflight_threads.load(std::memory_order_relaxed) == 0;
    }
};


//TODO: do this in a more standard way.
__thread uint64_t last_tail_ann;

class faa_dist_qm : public i_quiescent_manager {
//Inspired by Pedro Ramalhete and Andrea Correia's LeftRight distributed counter.
//  ConcurrencyFreaks with link to paper: http://concurrencyfreaks.blogspot.com/2013/12/left-right-concurrency-control.html
//  CppCon 2015 Talk: https://youtu.be/FtaD0maxwec?t=2179
//  Example implementation: https://github.com/mmcshane/leftright/blob/master/include/mpm/leftright.h
  private:
    static inline constexpr size_t num_slots = 32; 
    struct alignas(64) slot {
      std::atomic<uint64_t> in_flight; //announcement variable AND quiescent bit
      void inc(std::memory_order mo) { in_flight.fetch_add(1, mo); }
      void dec(std::memory_order mo) { in_flight.fetch_sub(1, mo); }
      uint64_t load(std::memory_order mo) { return in_flight.load(mo); }
    };
    slot slots[num_slots];

    static_assert(num_slots > 0);
    
  public:
    faa_dist_qm(){}
    ~faa_dist_qm(){}
    
    inline void set_quiescent([[maybe_unused]] uint64_t tail_ann) {
      //Use tail_ann as our "source of randomness"
      //Use last_tail_ann because semantics do not enforce 2 set/unset_quiescent pairs
      //  to have the same tail_ann and that worsens implementation as quiescence_check
      //  would always have to read every slot.
      slots[last_tail_ann % num_slots].dec(std::memory_order_seq_cst);
    }
    
    inline void unset_quiescent(uint64_t tail_ann) {
      //Use tail_ann as our "source of randomness"
      last_tail_ann = tail_ann;
      slots[tail_ann % num_slots].inc(std::memory_order_seq_cst);
    }
    
    inline bool quiescence_check() { //TODO: can these barriers be acquire/release
      if(slots[0].load(std::memory_order_seq_cst) > 0) //num_slots >= 1
        return false;
      for(size_t i = 1; i < num_slots; i++) {
        if(slots[i].load(std::memory_order_relaxed) > 0) {
          std::atomic_thread_fence(std::memory_order_seq_cst);
          return false;
        }
      }
      std::atomic_thread_fence(std::memory_order_seq_cst);
      return true;
    }
};
