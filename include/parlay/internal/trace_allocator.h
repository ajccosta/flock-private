#ifndef PARLAY_INTERNAL_TRACE_ALLOCATOR_H_
#define PARLAY_INTERNAL_TRACE_ALLOCATOR_H_

#include <iostream>
#include <fstream>
#include "get_time.h"

namespace parlay {
namespace internal {

#ifdef TRACE

struct alloc_log_t {
    typedef enum {ALLOC = 0, FREE = 1} ALLOC_T;

    struct alignas(uint64_t) metadata_t { //assumes sizeof(float) = 4
        using sz_t = uint32_t;
        using ts_t = float;
        static constexpr size_t metadata_t_sz = sizeof(sz_t) + sizeof(ts_t);

        sz_t alloc_t_size; //[31:1]=size, [1:0]=alloc type
        ts_t timestamp; //timestamp at which allocation was made

        static inline sz_t alloc_t_size_repr(sz_t alloc_sz, ALLOC_T at) {
          return ((sz_t) alloc_sz << 1) | at;
        }

        metadata_t() {}

      public:
        inline ALLOC_T get_alloc_type() {
          return (ALLOC_T) (alloc_t_size & ((sz_t) 0x1));
        }

        inline sz_t get_alloc_size() {
          return alloc_t_size >> 1;
        }

        inline ts_t get_ts() {
          return timestamp;
        }

        metadata_t(sz_t alloc_sz, ALLOC_T at, ts_t ts):
          alloc_t_size(alloc_t_size_repr(alloc_sz, at)),
          timestamp(ts) {}

        metadata_t& operator=(char buf[metadata_t_sz]) {
            memcpy(this, buf, metadata_t_sz);
            return *this;
        }
    };

  private:
    std::vector<metadata_t> allocations;
    std::set<metadata_t::sz_t> size_classes;
    std::string log_file_name = "alloc.log";

    parlay::internal::timer clock;
    metadata_t::ts_t current_ts;
    static const size_t take_time_step = 100;
    size_t time_steps_taken = take_time_step;

    parlay::thread_id_type tid;

    //Whether we are currently tracing (true) or not (false)
    bool tracing;
    //Whether we are producing a trace (true) or replaying a trace (false)
    bool tracing_or_replaying;

    std::string get_file_name() { return 't' + std::to_string(tid) + '_' + log_file_name; }

    void reset_file() {
      //Delete file
      std::remove(get_file_name().c_str());
    }

    //Load trace from file
    //Loads set with size classes will
    void load_trace() {
      std::ifstream f;
      f.open(get_file_name(), std::ios::in | std::ios::binary);
      metadata_t t;
      while(f.good()) {
        //Probably unsafe, but should work fine
        f.read(reinterpret_cast<char*>(&t), sizeof(metadata_t));
        allocations.push_back(t);
        size_classes.insert(t.get_alloc_size());
      }
      allocations.pop_back(); //Last read contains garbage
      f.close();
    }

  public:
    alloc_log_t() = delete;

    alloc_log_t(parlay::thread_id_type tid): tid(tid), tracing(false) {
      //Don't initialize traces that will not be used
      if(tid >= parlay::num_workers()) return;

      tracing_or_replaying = std::getenv("PARLAY_REPLAY") == nullptr;
      if(tracing_or_replaying) { //Trace
        reset_file();
      } else { //Replay
        load_trace();
      }
    }

    metadata_t::ts_t get_ts() {
      if(time_steps_taken++ == take_time_step) {
        current_ts = (metadata_t::ts_t) clock.total_time();
        time_steps_taken = 0;
      }
      return current_ts;
    }

    void log_alloc(ALLOC_T at, size_t alloc_sz) {
      if(!tracing_or_replaying || !tracing) return;
      metadata_t t = metadata_t(alloc_sz, at, get_ts());
      allocations.push_back(t);
    }

    void start() { tracing = true; }
    void stop() { tracing = false; }

    void write() {
      if(!tracing_or_replaying) return;
      std::ofstream f;
      f.open(get_file_name(), std::ios::app | std::ios::binary | std::ios::out);
      for_each(allocations.begin(), allocations.end(), [&] (metadata_t &t) {
        f.write(reinterpret_cast<char*>(&t), sizeof(metadata_t));
      });
      f.close();
    }

    auto begin() const { return allocations.begin(); }
    auto end() const { return allocations.end(); }
    std::set<metadata_t::sz_t> get_size_classes() const { return size_classes; }
};

ThreadSpecific<alloc_log_t> my_log([](parlay::thread_id_type tid) { return alloc_log_t(tid); });

//Start/Stop tracing for all threads
static void start_tracing() { my_log.for_each([](auto&& log) { log.start(); }); }
static void stop_tracing() { my_log.for_each([](auto&& log) { log.stop(); }); }
static void write_trace() {
  //Write acquired traces to a file
  my_log.for_each([](auto&& log) {
    log.write();
  });
}

#else //!defined(TRACE)

#include <limits>
#include <set>

//inlined to attempt to optimized out function calls
struct alloc_log_t {
  struct metadata_t { using sz_t = uint32_t; using ts_t = float; };
  typedef enum {ALLOC = 0, FREE = 1} ALLOC_T;
  inline void log_alloc(ALLOC_T at, size_t alloc_sz) { ; }
  inline metadata_t::ts_t get_ts() { return std::numeric_limits<metadata_t::ts_t>::min(); }
  inline void start() { ; }
  inline void stop() { ; }
  inline void write() { ; }
  inline void begin() const { ; }
  inline void end() const { ; }
  inline std::set<metadata_t::sz_t> get_size_classes() const { std::set<metadata_t::sz_t> s; return s; }
};
inline static void start_tracing(){ ; }
inline static void stop_tracing(){ ; }
inline static void write_trace() { ; }
static alloc_log_t *my_log;
#endif

}  // namespace internal
}  // namespace parlay

#endif // PARLAY_INTERNAL_TRACE_ALLOCATOR_H_