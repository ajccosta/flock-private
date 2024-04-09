#ifndef TPCC_GLUE_HPP
#define TPCC_GLUE_HPP

#include "set.h"

template <typename K>
struct StructString {
  static constexpr int num_ints = sizeof(K)/4;
  static int length(K key) { return sizeof(K);}
  static int get_byte(K key, int pos) {
    int* kptr = (int*) &key;
    int byte = 3 - (pos & 3);
    return kptr[pos/4] >> byte;
  }
};

template <typename Map>
struct RMap_ {
  using V = typename Map::V;
  using K = typename Map::K;
  
  Map m;
  RMap_(long n) : m(Map(n)) {}
  ~RMap_() {
    //std::cout << "destructing map: " << std::endl;
    //m.stats();
  }
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

template <typename K, typename V>
#if defined(HASH)
using RMap = RMap_<unordered_map<K,V>>;
#elif defined(RADIX)
using RMap = RMap_<ordered_map<K,V,StructString<K>>>;
#else
using RMap = RMap_<ordered_map<K,V>>;
#endif

#endif // TPCC_GLUE_HPP
