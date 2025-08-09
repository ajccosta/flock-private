#include <array>
#include <verlib/verlib.h>
#include "set.h"
#include "test_sets.h"
#include "parse_command_line.h"

using K = unsigned long;
using V = std::array<unsigned long, 1>;

// if use this definition then enries are 100 bytes (8 + 23*4)
//using V = std::array<unsigned int, 23>;

int main(int argc, char* argv[]) {
  commandLine P(argc,argv,R"(
[-n <size>]  : size of the map
[-r <rounds>]  : number of rounds to run the test
[-p <num-threads>] : number of threads for tests
[-z <zipfian-parameter>] : real parameter in range [0:1), 0 is uniform, .99 is highly skewed
[-u <update-percent>] : percent of operations that are updates (1/2 inserts, 1/2 deletes)
[-mfind <multifind-percent>] : percent of operations that are atomic multifinds
[-range <range-query-percent>] : percent of operations that are range queries
[-rs <range-query-size>] : size of the range query or multifind 
[-upsert] : do upserts instead of inserts and deletes
[-t <time>] : time to run each test in seconds
[-dense] : use keys in range [0:n)
[-simple_test] : sanity check
[-insert_find_delete] : just insert, then find, then delete
[-verbose] : print some info
[-stats] : print some memory statistics 
[-clear] : clear the memory pool between rounds
[-warmup] : perform random insertions and deletions before starting experiment
[-test_allocator] : replay an allocation trace
[-no_check] : do not check correctness of answers
[-rqthreads <num range query threads>] : number of dedicated range-query threads
[-trans <ops-per-transaction>] : size of transaction in operations (0 means no transaction)
[-latency <latency-cutoff-in-usecs>] : measure latency instead of throughput
[-no_warmup_round] : do not perform warmup round (the first round is usually ignored)
[-thread-perc "...;..."] : fine-grained thread update percentages
    E.g., -thread-perc "80,20;40,60;0,100" will have a 1/3 of threads do 80% followed by 20% inserts;
      another 1/3 will do 40% inserts for half the time, then 60%;
      and the final 1/3 will switch between doing only removes and doing only inserts
[-prefill] : how many elements to prefill the data structure with, default = n/2
[-nscale] : how many unique keys will exist relative to -n (#unique keys = -n * -nscale), default = 2
[-remote-free-perc] : how many remote frees will to occur (approximately)
    Requires compilation with CHECK_REMOTE_FREES and REMOTE_FREE_MANIPULATION
    With -stats, or -stats 2 additionall information is printed
)");

#ifdef HASH
  struct IntHash {
    std::size_t operator()(K const& k) const noexcept {
      return k * UINT64_C(0xbf58476d1ce4e5b9);}
  };
  using SetType = unordered_map<K,V,IntHash>;
#else
  using SetType = ordered_map<K,V>;
#endif
  
  size_t default_size = 100000;

  test_sets<SetType>(default_size, P);
}
