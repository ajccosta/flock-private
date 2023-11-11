#include <iostream>
#include <string>
#include <thread>
#include <random>
#include <time.h>
#include <sys/time.h>

#include "ConcurrentPrimitives.hpp"
#include "HarnessUtils.hpp"
#include "CustomTypes.hpp"

#include "LockfreeHashTable.hpp"
#include "NVMLockfreeHashTable.hpp"

#include "txMontageLfHashTable.hpp"
#include "MedleyLfHashTable.hpp"

#include "TxnBoostingLfHashTable.hpp"

#include "OneFileHashTable.hpp"
// #include "POneFileHashTable.hpp"

#include "FraserSkipList.hpp"
#include "NVMFraserSkipList.hpp"

#include "txMontageFraserSkipList.hpp"
#include "MedleyFraserSkipList.hpp"

#include "TxnBoostingFraserSkipList.hpp"

#include "OneFileSkipList.hpp"
// #include "POneFileSkipList.hpp"

#include "TDSLSkipList.hpp"
#include "LFTTSkipList.hpp"

// #include "FuseLeafTree.hpp"
#include "FuseBTree.hpp"
#include "FuseHashTable.hpp"

#include "MapChurnTest.hpp"
#include "TxnMapChurnTest.hpp"
#include "TxnVerify.hpp"
#include "TPCC.hpp"

using namespace std;


int main(int argc, char *argv[])
{
	GlobalTestConfig gtc;

	/* hash tables */
	gtc.addRideableOption(new LockfreeHashTableFactory<uint64_t>(), "LfHashTable");
	// gtc.addRideableOption(new NVMLockfreeHashTableFactory<uint64_t>(), "NVMLockfreeHashTable");
	gtc.addRideableOption(new MedleyLfHashTableFactory<uint64_t>(), "MedleyLfHashTable");
	// gtc.addRideableOption(new txMontageLfHashTableFactory<uint64_t>(), "txMontageLfHashTable");
	gtc.addRideableOption(new TxnBoostingLfHashTableFactory<uint64_t>(), "TxnBoostingLfHashTable");
	gtc.addRideableOption(new OneFileHashTableFactory<uint64_t>(), "OneFileHashTable");
	// gtc.addRideableOption(new POneFileHashTableFactory<uint64_t>(), "POneFileHashTable");

	/* skiplists */
	gtc.addRideableOption(new FraserSkipListFactory<uint64_t>(), "FraserSkipList");
	// gtc.addRideableOption(new NVMFraserSkipListFactory<uint64_t>(), "NVMFraserSkipList");
	gtc.addRideableOption(new MedleyFraserSkipListFactory<uint64_t>(), "MedleyFraserSkipList");
	// gtc.addRideableOption(new txMontageFraserSkipListFactory<uint64_t>(), "txMontageFraserSkipList");
	gtc.addRideableOption(new TxnBoostingFraserSkipListFactory<uint64_t>(), "TxnBoostingFraserSkipList");
	gtc.addRideableOption(new OneFileSkipListFactory<uint64_t>(), "OneFileSkipList");
	// gtc.addRideableOption(new POneFileSkipListFactory<uint64_t>(), "POneFileSkipList");
	gtc.addRideableOption(new TDSLSkipListFactory<uint64_t>(), "TDSLSkipList");
	gtc.addRideableOption(new LFTTSkipListFactory(), "LFTTSkipList");
	// gtc.addRideableOption(new FuseLeafTreeFactory<uint64_t>(), "FuseLeafTree");
	gtc.addRideableOption(new FuseBTreeFactory<uint64_t>(), "FuseBTree");
	gtc.addRideableOption(new FuseHashTableFactory<uint64_t>(), "FuseHashTable");

	/* non-transactional microbenchmark */
	// gtc.addTestOption(new MapChurnTest<uint64_t,uint64_t>(50, 0, 25, 25, 1000000, 500000), "MapChurnTest<uint64_t>:g50p0i25rm25:range=100000:prefill=500000");

	/* transactional TPCC benchmark */
	gtc.addTestOption(new tpcc::TPCC<TxnType::NBTC>(50,50,0,0,0),"TPCC<NBTC>");
	gtc.addTestOption(new tpcc::TPCC<TxnType::TDSL>(50,50,0,0,0),"TPCC<TDSL>");
	gtc.addTestOption(new tpcc::TPCC<TxnType::OneFile>(50,50,0,0,0),"TPCC<OneFile>");
	gtc.addTestOption(new tpcc::TPCC<TxnType::Fuse>(50,50,0,0,0),"TPCC<Fuse>");

	std::vector<int> sizes {100000, 10000000};
	std::vector<double> zipfs {0, 0.75, 0.9, 0.99};
	std::vector<int> updates {0, 10, 50, 100};
	std::vector<int> trans_sizes {16};
	bool fixed_size_trans = true;

	/* transactional microbenchmark */
	for(int size : sizes)
		for(double zipf : zipfs)
			for(int up : updates)
				for(int trans_size : trans_sizes)
				{
					stringstream ss;
					ss << ":txn=" << trans_size << ":update=" << up << ":size=" << size << ":zipf=" << zipf;
					std::string postfix = ss.str();
					gtc.addTestOption(new TxnMapChurnTest<uint64_t,uint64_t,TxnType::None>(fixed_size_trans, trans_size, 100-up, 0, up/2, up/2, size*2, size, zipf), "None"+postfix);
					gtc.addTestOption(new TxnMapChurnTest<uint64_t,uint64_t,TxnType::NBTC>(fixed_size_trans, trans_size, 100-up, 0, up/2, up/2, size*2, size, zipf), "NBTC"+postfix); // Uses Medley/txMontage framework
					gtc.addTestOption(new TxnMapChurnTest<uint64_t,uint64_t,TxnType::TDSL>(fixed_size_trans, trans_size, 100-up, 0, up/2, up/2, size*2, size, zipf), "TDSL"+postfix);
					gtc.addTestOption(new TxnMapChurnTest<uint64_t,uint64_t,TxnType::LFTT>(fixed_size_trans, trans_size, 100-up, 0, up/2, up/2, size*2, size, zipf), "LFTT"+postfix);
					gtc.addTestOption(new TxnMapChurnTest<uint64_t,uint64_t,TxnType::OneFile>(fixed_size_trans, trans_size, 100-up, 0, up/2, up/2, size*2, size, zipf), "OneFile"+postfix);
					gtc.addTestOption(new TxnMapChurnTest<uint64_t,uint64_t,TxnType::Fuse>(fixed_size_trans, trans_size, 100-up, 0, up/2, up/2, size*2, size, zipf), "Fuse"+postfix);
				}

	// gtc.addTestOption(new TxnVerify<uint64_t, uint64_t>(30, 14,14, 14, 14,14, 500000,10), "TxnVerify<uint64_t>:g30:wa14:rb14:wb14:rc14:wc14:range=500000");
	

	gtc.parseCommandLine(argc, argv);
        omp_set_num_threads(gtc.task_num);
	gtc.runTest();

	// print out results
	if(gtc.verbose){
		printf("Operations/sec: %ld\n",(uint64_t)(gtc.total_operations/gtc.interval));
	}
	else{
		string rideable_name = gtc.getRideableName().c_str();
		if(gtc.getEnv("PersistStrat") == "No") {
			rideable_name = "NoPersist"+rideable_name;
		}
		if(gtc.getEnv("Liveness") == "Nonblocking") {
			rideable_name = "nb"+rideable_name;
		}
		printf("%d,%ld,%s,%s\n",gtc.task_num,(uint64_t)(gtc.total_operations/gtc.interval),rideable_name.c_str(),gtc.getTestName().c_str());
	}
}
