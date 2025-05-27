// A concurrent allocator for blocks of a fixed size
//
// Keeps a local pool per thread, and grabs list_length elements from a
// global pool if empty, and returns list_length elements to the global
// pool when local pool=2*list_length.
//
// Keeps track of number of allocated elements. Much more efficient
// than a general purpose allocator.
//
// Not generally intended for users. Users should use "type_allocator"
// which is a convenient wrapper around block_allocator that helps
// to manage memory for a specific type

#ifndef PARLAY_INTERNAL_BLOCK_ALLOCATOR_H_
#define PARLAY_INTERNAL_BLOCK_ALLOCATOR_H_

#include <cassert>
#include <cstddef>

#include <algorithm>
#include <atomic>
#include <iostream>
#include <new>
#include <optional>

#include "../utilities.h"
#include "../thread_specific.h"

#include "concurrency/hazptr_stack.h"
#include "work_stealing_deque_private.h"
#include "../random_gen.h"

#include "memory_size.h"

#ifdef UseHugepages
#include <sys/mman.h>
#endif

//#include <jemalloc/jemalloc.h>
//size_t jemalloc_get_allocated() {
//    size_t epoch = 1;
//    size_t sz, allocated;
//    sz = sizeof(size_t);
//    mallctl("thread.tcache.flush", NULL, NULL, NULL, 0);
//    mallctl("epoch", NULL, NULL, &epoch, sizeof(epoch));
//    mallctl("stats.allocated", &allocated, &sz, NULL, 0);
//    return allocated;
//}

/*\ Whether to use a deque-based (define DEQALLOC undefine DISTACK),
stack-based (undefine DEQALLOC undefine DISTACK) global pool or to use
a distributed (undefine DEQALLOC define DISTACK) stack-based pool\*/
//#define DEQALLOC
//#define DISTACK

// IWYU pragma: no_include <array>
// IWYU pragma: no_include <vector>

/*\ If you want the next run to acquire a trace of allocations. \*/
//#define TRACE 
#include "trace_allocator.h"

namespace parlay {
namespace internal {

struct block_allocator {
 private:

#ifdef UseHugepages
  static inline constexpr size_t huge_page_size = (1ul << 21);
  static inline constexpr size_t default_list_bytes = huge_page_size - 64;  // in bytes
#else
  static inline constexpr size_t default_list_bytes = (1ul << 18) - 64;  // in bytes
#endif
  static inline constexpr size_t min_alignment = 128;  // for cache line padding

  struct block {
    block* next;
  };

  struct alignas(128) local_list {
    size_t sz;
    block* head;
    block* mid;
    local_list() : sz(0), head(nullptr), mid(nullptr) {}
  };

  hazptr_stack<std::byte*> allocated_buffers;
#if !defined(DEQALLOC) && !defined(DISTACK)
  hazptr_stack<block*> global_stack;
#elif defined(DEQALLOC)
  //Deque where each element is a filled local_list (i.e., all lists have list_length elements)
  ThreadSpecific<PrivateDeque<block>> my_deq;
#else //defined(DISTACK)
  ThreadSpecific<hazptr_stack<block*>> dist_stack;
#endif

  size_t max_steal_retries; // number of steal attempts
  //List that we are currently allocating from
  ThreadSpecific<local_list> my_local_list;
  //RNG for each thread
  ThreadSpecific<random> my_rand;

  /*\ Counters for statistical analysis \*/
  ThreadSpecific<size_t> num_allocs; //Number of alloc calls per-thread
  ThreadSpecific<size_t> num_frees;  //Number of free calls per-thread
  ThreadSpecific<size_t> num_global_pool_accesses;  //Number of times the global pool is accessed

  size_t block_size; //Size of each block
  std::align_val_t block_align;
  size_t list_length; //Size of each local list and used for the threshold to return blocks to global pool
  size_t max_blocks; //Maximum number of blocks accross all threads
  std::atomic<size_t> blocks_allocated; //Number of blocks allocated (accross all threads)

  block* get_block(std::byte* buffer, size_t i) const {
    // Since block is an aggregate type, it has implicit lifetime, so the following code
    // is defined behaviour in C++20 even if we haven't yet called a constructor of block.
    // It is still UB prior to C++20, but this is hard to avoid without losing performance.
    return from_bytes<block>(buffer + i * block_size);
  }

 public:
  block_allocator(const block_allocator&) = delete;
  block_allocator(block_allocator&&) = delete;
  block_allocator& operator=(const block_allocator&) = delete;
  block_allocator& operator=(block_allocator&&) = delete;

  size_t get_block_size() const { return block_size; }
  size_t num_allocated_blocks() const { return blocks_allocated.load(); }

  explicit block_allocator(size_t block_size_,
    size_t block_align_ = alignof(std::max_align_t),
    [[maybe_unused]] size_t reserved_blocks = 0,
    size_t list_length_ = 0,
    size_t max_blocks_ = 0) :
#ifdef DEQALLOC
      my_deq([]{
        auto qm = new faa_qm();
        return PrivateDeque<block>(5, 2, qm);
      }),
#endif
      my_local_list(),                                                               
      my_rand([](parlay::thread_id_type tid) { return random(tid); }),               // Each block needs to be at least
      block_size(std::max<size_t>(block_size_, sizeof(block))),    // <------------- // large enough to hold the struct
      block_align(std::align_val_t{std::max<size_t>(block_align_, min_alignment)}),  // representing a free block.
      max_blocks(max_blocks_ == 0 ? (3 * getMemorySize() / block_size) / 4 : max_blocks_),
      blocks_allocated(0) {

      //Get number of steal retries from env variable
      auto steal_retries_env = std::getenv("PARLAY_STEAL_RETRIES");
      max_steal_retries = steal_retries_env != nullptr ?
        std::stoi(steal_retries_env) :
        std::thread::hardware_concurrency();

      //Get number of list bytes from env variable
      auto list_bytes_env = std::getenv("PARLAY_LIST_BYTES");
      size_t list_bytes = list_bytes_env != nullptr ?
        std::stoll(list_bytes_env) :
        default_list_bytes;
      list_length = (list_bytes + block_size + 1) / block_size;
      if(list_length == 1) list_length++; //Don't allow local pools with less than 2 blocks
  }

  // Allocate a new list of list_length elements
  auto initialize_list(std::byte* buffer) const -> block* {
    //Allocate list into buffer
    //For each element, except the last, set the next ptr to the following element
    for (size_t i=0; i < list_length - 1; i++) {
      new (buffer + i * block_size) block{get_block(buffer, i+1)};
    }
    //The last element has next set to nullptr
    new (buffer + (list_length - 1) * block_size) block{nullptr};
    //Return first element
    return get_block(buffer, 0);
  }

  //Return the number of blocks requested by the user
  size_t num_used_blocks() {
    size_t free_blocks;
#if !defined(DEQALLOC) && !defined(DISTACK)
    free_blocks = global_stack.size()*list_length;
#elif defined(DEQALLOC)
    free_blocks = 0;
    //Sum the number of blocks in each thread's deque
    //  (ThreadSpecific allows iteration over all thread's objects)
    my_deq.for_each([&](auto&& deq) {
      free_blocks += deq.real_size() * list_length;
    });
#else //defined(DISTACK)
    free_blocks = 0;
    dist_stack.for_each([&](auto&& stack) {
      free_blocks += stack.size()*list_length;
    });
#endif
    //Sum the number blocks in each thread's local list
    my_local_list.for_each([&](auto&& list) {
      free_blocks += list.sz;
    });
    return blocks_allocated.load() - free_blocks;
  }

  auto allocate_blocks(size_t num_blocks) -> std::byte* {
    long total_bytes = num_blocks * block_size;
#ifdef UseHugepages
    auto buffer = static_cast<std::byte*>(::operator new(total_bytes, std::align_val_t{huge_page_size}));
    madvise(buffer, total_bytes, MADV_HUGEPAGE);
#else
    auto buffer = static_cast<std::byte*>(::operator new(total_bytes, block_align));
#endif
    assert(buffer != nullptr);

    blocks_allocated.fetch_add(num_blocks);
    assert(blocks_allocated.load() <= max_blocks);

    allocated_buffers.push(buffer); // keep track so can free later
    return buffer;
  }

  // Either grab a list from the global pool, or if there is none
  // then allocate a new list
  auto get_list() -> block* {
    num_global_pool_accesses++;
#if !defined(DEQALLOC) && !defined(DISTACK)
    std::optional<block*> rem = global_stack.pop();
    if (rem) return *rem;
#elif defined(DEQALLOC)
    //Try to get list from our deque
    block* rem = my_deq->pop_bottom();
    if (rem) return rem;

    //Our deque is empty, attempt to steal a list
    size_t attempts_left = max_steal_retries;
    auto num_threads = num_thread_ids();
    while(attempts_left-- > 0) {
      rem = my_deq[my_rand->rand() % num_threads].pop_top().first;
      if(rem)
        return rem; //Successfuly stole list from other thread's deque!
    }
#else //defined(DISTACK)
    std::optional<block*> rem = dist_stack->pop();
    if (rem) return *rem;

    size_t attempts_left = max_steal_retries;
    auto num_threads = num_thread_ids();
    while(attempts_left-- > 0) {
      rem = dist_stack[my_rand->rand() % num_threads].pop();
      if(rem) return *rem; //Successfuly stole list from other thread's deque!
    }
#endif

    //All else failed, allocate more memory
    std::byte* buffer = allocate_blocks(list_length);
    return initialize_list(buffer);
  }

  // Allocate n elements across however many lists are needed (rounded up)
  [[deprecated]] void reserve(size_t) { }

  void print_stats() {
    size_t used = num_used_blocks();
    size_t allocated = num_allocated_blocks();
    size_t sz = get_block_size();
    std::cout << "Used: " << used << ", allocated: " << allocated
        << ", total allocs: " << num_allocs.sum()
        << ", total frees: " << num_frees.sum()
        << ", global pool accesses: " << num_global_pool_accesses.sum()
	      << ", block size: " << sz
	      //<< ", jemalloc: " << jemalloc_get_allocated()
	      << ", bytes: " << sz*allocated << "\n";
  }

  std::pair<size_t,size_t> stats() {
    size_t used = num_used_blocks();
    size_t allocated = num_allocated_blocks();
    size_t sz = get_block_size();
    return {used*sz, allocated*sz};
  }

  // Clears all memory ever allocated by this allocator. All allocated blocks
  // must be returned before calling this function.
  //
  // This operation is not safe to perform concurrently with any other operation
  //
  // Returns false if there exists blocks that haven't been returned, in which case
  // the operation fails and nothing is cleared. Returns true if no lingering
  // blocks were not freed, in which case the operation is successful
  bool clear() {
    if (num_used_blocks() > 0) {
      return false;
    }
    else {
      // clear lists
      my_local_list.for_each([](auto&& list) {
        list.sz = 0;
      });

#if !defined(DEQALLOC) && !defined(DISTACK)
      global_stack.clear();
#elif defined(DEQALLOC)
      //Clear deques
      my_deq.for_each([](auto&& deq) {
        deq.clear();
      });
#else //defined(DISTACK)
      //Clear stacks
      dist_stack.for_each([](auto&& stack) {
        stack.clear();
      });
#endif
      // throw away all allocated memory
      std::optional<std::byte*> x;
      while ((x = allocated_buffers.pop())) ::operator delete(*x, block_align);
      
      blocks_allocated.store(0);
      return true;
    }
  }

  ~block_allocator() {
    [[maybe_unused]] auto cleared = clear();
#if !defined(NDEBUG) && !defined(PARLAY_ALLOC_ALLOW_LEAK)
    if (!cleared) {
      std::cerr << "There are un-freed blocks obtained from block_allocator. If this is intentional you may"
                        " suppress this messsage with -DPARLAY_ALLOC_ALLOW_LEAK\n";
    }
#endif
    write_trace();
  }

  void free(void* ptr) {
    my_log->log_alloc(alloc_log_t::ALLOC_T::FREE, block_size);
    num_frees++;
    if (my_local_list->sz == list_length+1) {
      //Record middle of the list
      my_local_list->mid = my_local_list->head;
    } else if (my_local_list->sz == 2*list_length) {
#if !defined(DEQALLOC) && !defined(DISTACK)
      global_stack.push(my_local_list->mid->next);
#elif defined(DEQALLOC)
      //Return half of the list to our deque
      my_deq->push_bottom(my_local_list->mid->next);
#else //defined(DISTACK)
      dist_stack->push(my_local_list->mid->next);
#endif
      my_local_list->mid->next = nullptr;
      my_local_list->sz = list_length;
      num_global_pool_accesses++;
    }

    //Insert freed note at the head of the list
    auto new_node = new (ptr) block{my_local_list->head};
    my_local_list->head = new_node;
    my_local_list->sz++;
  }

  inline void* alloc() {
    my_log->log_alloc(alloc_log_t::ALLOC_T::ALLOC, block_size);
    num_allocs++;
    if (my_local_list->sz == 0)  { //No blocks on the local list, get a new list
      auto new_list = get_list();

      // !! Critical problem !! If this task got stolen during get_list(), the
      // running thread may have changed, so we can't assume we are looking at
      // the same local list, so we have to double-check the (possibly different)
      // local list of the (possibly changed) thread

      if (my_local_list->sz == 0) {
        my_local_list->head = new_list;
        my_local_list->sz = list_length;
      }
      else {
        // Looks like the task got stolen and the new thread already had a
        // non-empty local list, so we can push the new one into out local
        // deque for use in the future
#if !defined(DEQALLOC) && !defined(DISTACK)
        global_stack.push(new_list);
#elif defined(DEQALLOC)
        my_deq->push_bottom(new_list);
#else //defined(DISTACK)
        dist_stack->push(new_list);
#endif
      }
    }

    //Pop block from head of local list
    block* p = my_local_list->head;
    my_local_list->head = my_local_list->head->next;
    my_local_list->sz--;

    // Note: block is trivial, so it is legal to not call its destructor
    // before returning it and allowing its storage to be reused
    return static_cast<void*>(p);
  }

};

}  // namespace internal
}  // namespace parlay

#endif  // PARLAY_INTERNAL_BLOCK_ALLOCATOR_H_
