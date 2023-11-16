/*

Copyright 2017 University of Rochester

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. 

*/


#ifndef RMAP_HPP
#define RMAP_HPP

#include <string>
#include "Rideable.hpp"

#include "optional.hpp"

#ifdef TPCC_STATS
    const int TPCC_MAX_TH = 1000;
    const int TPCC_PADDING = 64;
    int tpcc_padding[16];
    uint64_t tpcc_aborts[TPCC_MAX_TH*TPCC_PADDING];
    uint64_t tpcc_puts[TPCC_MAX_TH*TPCC_PADDING];
    uint64_t tpcc_inserts[TPCC_MAX_TH*TPCC_PADDING];
    uint64_t tpcc_removes[TPCC_MAX_TH*TPCC_PADDING];
    uint64_t tpcc_gets[TPCC_MAX_TH*TPCC_PADDING];

    void reset_tpcc_stats() {
        for(int i = 0; i < TPCC_MAX_TH; i++) {
            tpcc_aborts[i*TPCC_PADDING] = 0;
            tpcc_puts[i*TPCC_PADDING] = 0;
            tpcc_gets[i*TPCC_PADDING] = 0;
            tpcc_removes[i*TPCC_PADDING] = 0;
            tpcc_inserts[i*TPCC_PADDING] = 0;
        }
    }

    uint64_t num_tpcc_puts() {
        uint64_t sum = 0;
        for(int i = 0; i < TPCC_MAX_TH; i++)
            sum += tpcc_puts[i*TPCC_PADDING];
        return sum;
    }

    uint64_t num_tpcc_gets() {
        uint64_t sum = 0;
        for(int i = 0; i < TPCC_MAX_TH; i++)
            sum += tpcc_gets[i*TPCC_PADDING];
        return sum;
    }

    uint64_t num_tpcc_removes() {
        uint64_t sum = 0;
        for(int i = 0; i < TPCC_MAX_TH; i++)
            sum += tpcc_removes[i*TPCC_PADDING];
        return sum;
    }

    uint64_t num_tpcc_inserts() {
        uint64_t sum = 0;
        for(int i = 0; i < TPCC_MAX_TH; i++)
            sum += tpcc_inserts[i*TPCC_PADDING];
        return sum;
    }

    uint64_t num_tpcc_aborts() {
        uint64_t sum = 0;
        for(int i = 0; i < TPCC_MAX_TH; i++)
            sum += tpcc_aborts[i*TPCC_PADDING];
        return sum;
    }
#endif

template <class K, class V> class RMap : public virtual Rideable{
public:

    // Gets value corresponding to a key
    // returns : the most recent value set for that key
    virtual optional<V> get(K key, int tid)=0;

    // Puts a new key/value pair into the map   
    // returns : the previous value for this key,
    // or NULL if no such value exists
    virtual optional<V> put(K key, V val, int tid)=0;

    // Inserts a new key/value pair into the map
    // if the key is not already present
    // returns : true if the insert is successful, false otherwise
    virtual bool insert(K key, V val, int tid)=0;

    // Removes a value corresponding to a key
    // returns : the removed value
    virtual optional<V> remove(K key, int tid)=0;

    // Replaces the value corresponding to a key
    // if the key is already present in the map
    // returns : the replaced value, or NULL if replace was unsuccessful
    virtual optional<V> replace(K key, V val, int tid)=0;

    virtual size_t check() { return 0; };

    virtual void stats() {};
};

#endif   
