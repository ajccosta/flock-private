#ifndef TPCC_GLUE_HPP
#define TPCC_GLUE_HPP

#include "set.h"

template <typename K, typename V>
struct RMap {

#ifdef HASH
  using Map = unordered_map<K,V>;
#else
  using Map = ordered_map<K,V>;
#endif

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
