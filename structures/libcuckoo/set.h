#include <parlay/primitives.h>
#include <verlib/verlib.h>
#define Dense_Keys 1
#define HASH 1

#include "cuckoohash_map.hh"

template <typename K,
	  typename V,
	  class Hash = std::hash<K>,
	  class KeyEqual = std::equal_to<K>>
struct Set {

  using Table = libcuckoo::cuckoohash_map<K, V, Hash, KeyEqual>;

  std::optional<V> find(Table& table, const K& k) {
    V result;
    if (table.find(k, result)) return result;
    else return std::optional<V>();
  }

  std::optional<V> find_(Table& table, const K& k) {
    return find(table, k);
  }

  bool insert(Table& table, const K& k, const V& v) {
    return table.insert(k, v);
  }

  bool remove(Table& table, const K& k) {
    return table.erase(k);
  }

  Table empty(size_t n) {
    Table map;
    map.reserve(n);
    return map;
  }

  void print(Table& t) {}
  void retire(Table& t) {}
  long check(Table& t) {return t.size();}
  void clear() {}
  void stats() {}
  void reserve(size_t n) {}
  void shuffle(size_t n) {}
};
