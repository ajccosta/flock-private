using K = unsigned long;
using V = unsigned long;

#include <verlib/verlib.h>
#include "set.h"
#include "test_sets.h"
#include "parse_command_line.h"

int main(int argc, char* argv[]) {
  commandLine P(argc,argv,"[-n <size>] [-r <rounds>] [-threads <num threads>] [-z <zipfian_param>] [-u <update percent>] [-mfind <multifind percent>] [-range <range query percent>] [-rs <range query size>] [-dense] [-simple_test] [-insert_find_delete] [-verbose] [-shuffle] [-stats] [-no_check] [-rqthreads <num range query threads>]");

#ifdef HASH
  using SetType = unordered_map<K,V>;
#else
  using SetType = ordered_map<K,V>;
#endif

  size_t default_size = 100000;
  test_sets<SetType>(default_size, P);
}
