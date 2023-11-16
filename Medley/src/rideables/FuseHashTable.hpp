
#ifndef _FUSE_HASH_TABLE_H_
#define _FUSE_HASH_TABLE_H_

#include <string>
#include <algorithm>
#include <utility>
#include <functional>

namespace FuseHash {
    #include <structures/hash_block/unordered_map.h>
};


/**
 *
 */
template<typename K, typename V>
class alignas(128) FuseHashTable : public RMap<K,V> {
private:
    FuseHash::unordered_map<K, V> hashtable;

public:
    FuseHashTable(GlobalTestConfig* gtc) : hashtable(gtc->size == 0 ? 100000 : gtc->size) {}

    ~FuseHashTable() {}


    static std::string className() { return "FuseHashTable"; }

    //
    // Map methods for running the usual tests and benchmarks
    //

    // not used in TPCC
    optional<V> replace(K key, V val, int tid) {
        bool b = hashtable.insert(key, val);
        if(b) return optional<V>(val);
        else return {};
    }

    optional<V> put(K key, V val, int tid) {
#ifdef TPCC_STATS
        tpcc_puts[tid*TPCC_PADDING]++;
#endif
        bool b = hashtable.upsert(key, [&] (auto x) { return val; });
        if(b) return optional<V>(val); //TPCC never makes use of return value
        else return {};
    }

    bool insert(K key, V val, int tid) {
#ifdef TPCC_STATS
        tpcc_inserts[tid*TPCC_PADDING]++;
#endif
        return hashtable.insert(key, val);
    }

    optional<V> remove(K key, int tid) {
#ifdef TPCC_STATS
        tpcc_removes[tid*TPCC_PADDING]++;
#endif
        bool b = hashtable.remove(key);
        if(b) return optional<V>(); //TPCC never makes use of return value
        else return {};
    }

    optional<V> get(K key, int tid) {
#ifdef TPCC_STATS
        tpcc_gets[tid*TPCC_PADDING]++;
#endif
        return hashtable.find(key);
    }
};

template <class T> 
class FuseHashTableFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new FuseHashTable<T,T>(gtc);
    }
};


#endif /* _FUSE_HASH_TABLE_H_ */
