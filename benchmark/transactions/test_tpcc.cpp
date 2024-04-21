#include <chrono>

#include "../parse_command_line.h"

#define MAP_TYPE RMap

#include "tpcc/TPCC.hpp"
#include "tpcc/TestConfig.hpp"
#include "verlib/verlib.h"
#include "parlay/parallel.h"
#include "parlay/primitives.h"

int main(int argc, char* argv[]) {
  commandLine P(argc,argv,"");
  double time = P.getOptionDoubleValue("-t", 1.0);
  int threads = P.getOptionIntValue("-p", parlay::num_workers());
  int rounds = P.getOptionIntValue("-r", 1);
  int warmup = !P.getOption("-nowarmup");
  bool verbose = P.getOption("-v");
  int new_order = P.getOptionIntValue("-new_order", 50);
  int payment = 100 - new_order;

  for (int i=0; i < rounds + warmup; i++) {
    tpcc::TPCC tpcc_struct(new_order, payment, 0, 0, 0);
    auto gtc = new GlobalTestConfig();
    gtc->task_num = threads;
    gtc->interval = time;
    gtc->verbose = verbose;
    tpcc_struct.init(gtc);

    if (verbose) tpcc_struct.table_sizes();
    parlay::sequence<long> ops(threads);
    parlay::sequence<long> aborts(threads);
  
    auto startTime = std::chrono::high_resolution_clock::now();
    gtc->finish = startTime + std::chrono::seconds{(uint64_t)gtc->interval};
    parlay::parallel_for(0, threads, [&] (long i) {
      auto ltc = new LocalTestConfig();
      ltc->tid=i;
      ltc->seed=2*i;
      long aborts_start = tlf_internal::num_aborts;
      ops[i] = tpcc_struct.execute(gtc,ltc);
      aborts[i] = tlf_internal::num_aborts - aborts_start;
    }, 1, true);

    long total_ops = parlay::reduce(ops);
    long total_aborts = parlay::reduce(aborts);
  
    if (verbose) tpcc_struct.table_sizes();
    if (i >= warmup)
      std::cout << P.commandName()
		<< std::setprecision(4) 
		<< ": mops/sec = " << total_ops/( time * 1e6)
		<< ", %aborts = " << total_aborts * 100.0 / total_ops
		<< std::endl;
    tpcc_struct.cleanup(gtc);
  }
}
