
package adapters;

import pl.edu.put.concurrent.MultiversionNavigableMap;
import pl.edu.put.concurrent.jiffy.Jiffy;

import main.support.KSTNode;
import main.support.OperationListener;
import main.support.Random;
import main.support.SetInterface;

public class JiffyAdapter<K extends Comparable<? super K>> extends AbstractAdapter<K> implements SetInterface<K> {
    MultiversionNavigableMap<K, K> tree = new Jiffy<K,K>();

    public boolean contains(K key) {
        return tree.containsKey(key);
    }

    @Override
    public boolean add(K key, Random rng, final int[] metrics) {
        return tree.put(key, key) == null;
//        return tree.put(key, key) == null;
    }

    public boolean add(K key, Random rng) {
        return add(key, rng, null);
    }

    public K get(K key) {
        //return tree.get(key);
        return tree.get(key);
    }

    @Override
    public boolean remove(K key, Random rng, final int[] metrics) {
        return tree.remove(key) != null;
    }

    public boolean remove(K key, Random rng) {
        return remove(key, rng, null);
    }

    @Override
    public Object rangeQuery(K lo, K hi, int rangeSize, Random rng) {
        return tree.subMap(lo, true, hi, true).keySet().toArray();
    }

    // @Override
    // public long rangeSum(K lo, K hi, int rangeSize, Random rng) {
    //     return tree.rangeSum(lo, hi);
    // }

    public void addListener(OperationListener l) {}

    public int size() {
        return sequentialSize();
    }

    public KSTNode<K> getRoot() {
        return null;
    }

    public long getSumOfKeys() {
        return 0;
//        return tree.getSumOfKeys();
    }

    public boolean supportsKeysum() {
        return false;
    }

    public long getKeysum() {
        return 0;
//        return tree.getSumOfKeys();
    }

    public int getSumOfDepths() {
        return 0;
    }

    public int sequentialSize() {
        return tree.size();
    }

}
