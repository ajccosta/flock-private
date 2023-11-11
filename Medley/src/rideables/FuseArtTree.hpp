
#ifndef _FUSE_HASH_TABLE_H_
#define _FUSE_HASH_TABLE_H_

#include <string>
#include <algorithm>
#include <utility>
#include <functional>

#include <structures/arttree/ordered_map.h>

/**
 *
 */
template<typename K, typename V>
class FuseArtTree : public RMap<K,V> {
private:
    ordered_map<K, V> arttree;

public:
    FuseArtTree(GlobalTestConfig* gtc) {}

    ~FuseArtTree() {}


    static std::string className() { return "FuseArtTree"; }

    //
    // Map methods for running the usual tests and benchmarks
    //

    // not used in TPCC
    optional<V> replace(K key, V val, int tid) {
        bool b = arttree.insert_(key, val);
        if(b) return optional<V>(val);
        else return {};
    }

    optional<V> put(K key, V val, int tid) {
        bool b = arttree.put_(key, val);
        if(b) return optional<V>(val); //TPCC never makes use of return value
        else return {};
    }

    bool insert(K key, V val, int tid) {
        return arttree.insert_(key, val);
    }

    optional<V> remove(K key, int tid) {
        bool b = arttree.remove_(key);
        if(b) return optional<V>(); //TPCC never makes use of return value
        else return {};
    }

    optional<V> get(K key, int tid) {
        return arttree.find(key);
    }
};

template <class T> 
class FuseArttreeFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new FuseArtTree<T,T>(gtc);
    }
};


#endif /* _FUSE_HASH_TABLE_H_ */
