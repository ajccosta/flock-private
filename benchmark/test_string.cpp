#include <parlay/io.h>
using K = parlay::chars;
using V = unsigned long;

#include <verlib/verlib.h>
#include "set.h"
#include "test_string.h"

int main(int argc, char* argv[]) {
  commandLine P(argc,argv,"");
  Set<K,V> lst;
  size_t default_size = 100000;
  test_sets(lst, default_size, P);
}
