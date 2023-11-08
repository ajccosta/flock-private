
#ifndef _FUSE_HASH_TABLE_H_
#define _FUSE_HASH_TABLE_H_

#include <string>
#include <algorithm>
#include <utility>
#include <functional>

#include <structures/btree/ordered_map.h>

/**
 *
 */
template<typename K, typename V>
class FuseLeafTree : public RMap<K,V> {
private:
    ordered_map<K, V> leafTree;

public:
    FuseLeafTree(GlobalTestConfig* gtc) {}

    ~FuseLeafTree() {}


    static std::string className() { return "FuseLeafTree"; }

    //
    // Map methods for running the usual tests and benchmarks
    //

    // not used in TPCC
    optional<V> replace(K key, V val, int tid) {
        bool b = leafTree.insert_(key, val);
        if(b) return optional<V>(val);
        else return {};
    }

    optional<V> put(K key, V val, int tid) {
        bool b = leafTree.insert_(key, val);
        if(b) return optional<V>(val); //TPCC never makes use of return value
        else return {};
    }

    bool insert(K key, V val, int tid) {
        return leafTree.insert_(key, val);
    }

    optional<V> remove(K key, int tid) {
        bool b = leafTree.remove_(key);
        if(b) return optional<V>(); //TPCC never makes use of return value
        else return {};
    }

    optional<V> get(K key, int tid) {
        return leafTree.find(key);
    }
};

template <class T> 
class FuseLeafTreeFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new FuseLeafTree<T,T>(gtc);
    }
};


#endif /* _FUSE_HASH_TABLE_H_ */
