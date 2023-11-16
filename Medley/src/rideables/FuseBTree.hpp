
#ifndef _FUSE_BTREE_H_
#define _FUSE_BTREE_H_

#include <string>
#include <algorithm>
#include <utility>
#include <functional>
    
#include <structures/btree/ordered_map.h>


/**
 *
 */
template<typename K, typename V>
class alignas(128) FuseBTree : public RMap<K,V> {
private:
    ordered_map<K, V> btree;

public:
    FuseBTree(GlobalTestConfig* gtc) {}

    ~FuseBTree() {}


    static std::string className() { return "FuseBTree"; }

    //
    // Map methods for running the usual tests and benchmarks
    //

    // not used in TPCC
    optional<V> replace(K key, V val, int tid) {
        bool b = btree.insert(key, val);
        if(b) return optional<V>(val);
        else return {};
    }

    optional<V> put(K key, V val, int tid) {
#ifdef TPCC_STATS
        tpcc_puts[tid*TPCC_PADDING]++;
#endif
        bool b = btree.upsert(key, val);
        if(b) return optional<V>(val); //TPCC never makes use of return value
        else return {};
    }

    bool insert(K key, V val, int tid) {
#ifdef TPCC_STATS
        tpcc_inserts[tid*TPCC_PADDING]++;
#endif
        return btree.insert(key, val);
    }

    optional<V> remove(K key, int tid) {
#ifdef TPCC_STATS
        tpcc_removes[tid*TPCC_PADDING]++;
#endif
        bool b = btree.remove(key);
        if(b) return optional<V>(); //TPCC never makes use of return value
        else return {};
    }

    optional<V> get(K key, int tid) {
#ifdef TPCC_STATS
        tpcc_gets[tid*TPCC_PADDING]++;
#endif
        return btree.find(key);
    }

    size_t check() { return btree.check(); }
};

template <class T> 
class FuseBTreeFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new FuseBTree<T,T>(gtc);
    }
};


#endif /* _FUSE_BTREE_H_ */
