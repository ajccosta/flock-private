#ifndef TPCC_GLUE_HPP
#define TPCC_GLUE_HPP

#include "ordered_map.h"

template <typename K,
	  typename V,
	  typename Compare = std::less<K>>
struct RMap {

  using Map = ordered_map<K,V,Compare>;
  Map m;
  RMap(long n) : m(Map(n)) {}
  long size() { return m.check();}

  std::optional<V> get(const K& k, long tid) { return m.find(k); }

  std::optional<V> get_(const K& k, long tid) { return m.find_locked(k); }

  bool insert(const K& key, const V& value, long tid) {
    return m.insert(key, value);
  }

  bool insert_(const K& key, const V& value, long tid) {
    return m.insert_(key, value);
  }

  bool put(const K& key, const V& value, long tid) {
    return m.upsert(key, value);
  }

  bool put_(const K& key, const V& value, long tid) {
    return m.upsert_(key, value);
  }
  
  bool remove(const K& k) {
    return m.remove(k);
  }

};

#endif // TPCC_GLUE_HPP
