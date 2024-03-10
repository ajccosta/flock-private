#include <chrono>

#include "../parse_command_line.h"

#define MAP_TYPE RMap

#include "tpcc/TPCC.hpp"
#include "tpcc/TestConfig.hpp"
#include "transactions/transactions.h"
#include "parlay/parallel.h"
#include "parlay/primitives.h"

int main(int argc, char* argv[]) {
  commandLine P(argc,argv,"");
  double time = P.getOptionDoubleValue("-t", 1.0);
  int threads = P.getOptionIntValue("-p", 1);
  bool verbose = P.getOption("-v");
  int new_order = P.getOptionIntValue("-new_order", 50);
  int payment = 100 - new_order;

  tpcc::TPCC tpcc_struct(new_order, payment, 0, 0, 0);
  auto gtc = new GlobalTestConfig();
  gtc->verbose = true;
  gtc->task_num = threads;
  gtc->interval = time;
  gtc->verbose = verbose;
  tpcc_struct.init(gtc);

  if (verbose) tpcc_struct.table_sizes();
  parlay::sequence<long> ops(threads);
  
  auto startTime = std::chrono::high_resolution_clock::now();
  gtc->finish = startTime + std::chrono::seconds{(uint64_t)gtc->interval};
  parlay::parallel_for(0, threads, [&] (long i) {
    auto ltc = new LocalTestConfig();
    ltc->tid=i;
    ltc->seed=2*i;
    ops[i] = tpcc_struct.execute(gtc,ltc);}, 1);

  long total_ops = parlay::reduce(ops);
  
  if (verbose) tpcc_struct.table_sizes();
  std::cout << "num ops: " << total_ops << ", ops/sec = " << total_ops/time << std::endl;
}
