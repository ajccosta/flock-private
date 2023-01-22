#include <parlay/io.h>

#define CHARS

#ifdef CHARS
using K = parlay::chars;
#else
using K = std::string;
#endif

template <typename K>
struct RadixString {
  static int length(K key) { return key.size()+1;}
  static int get_byte(K key, int pos) {
    return (pos >= key.size()) ? 0 : key[pos];}
};

using V = unsigned long;

#include <verlib/verlib.h>
#include "set.h"
#include "test_string.h"

int main(int argc, char* argv[]) {
  commandLine P(argc,argv,"");
#ifdef RADIX
  Set<K,V,RadixString<K>> lst;
#elif HASH
  Set<K,V,parlay::hash<K>> lst;
#else // Comparison
  Set<K,V> lst;
#endif
  size_t default_size = 100000;
  test_sets(lst, default_size, P);
}
