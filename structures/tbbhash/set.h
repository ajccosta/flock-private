#define HASH 1

#include "tbb/concurrent_hash_map.h"

template <typename K,
	  typename V,
	  class Hash = std::hash<K>,
	  class KeyEqual = std::equal_to<K>>
struct Set {

  struct HashCompare {
    static size_t hash(const K& k) { return Hash{}(k);}
    static bool equal(const K& k1, const K& k2) { return KeyEqual{}(k1,k2);}
  };

  using Table = tbb::concurrent_hash_map<K, V, HashCompare>;

  std::optional<V> find(Table& table, const K& k) {
    typename Table::accessor a;
    if (table.find(a, k)) return a->second;
    else return std::optional<V>();
  }

  std::optional<V> find_(Table& table, const K& k) {
    return find(table, k);
  }

  bool insert(Table& table, const K& k, const V& v) {
    return table.insert(std::make_pair(k, v));    
  }

  bool remove(Table& table, const K& k) {
    return table.erase(k);
  }

  Table empty(size_t n) {
    Table map(n);
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
