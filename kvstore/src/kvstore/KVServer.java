package kvstore;

import static kvstore.KVConstants.*;

import java.util.concurrent.locks.*;

/**
 * This class services all storage logic for an individual key-value server.
 * All KVServer request on keys from different sets must be parallel while
 * requests on keys from the same set should be serial. A write-through
 * policy should be followed when a put request is made.
 */
public class KVServer implements KeyValueInterface {

    private KVStore dataStore;
    private KVCache dataCache;
    private ReentrantLock lock;

    private static final int MAX_KEY_SIZE = 256;
    private static final int MAX_VAL_SIZE = 256 * 1024;

    /**
     * Constructs a KVServer backed by a KVCache and KVStore.
     *
     * @param numSets the number of sets in the data cache
     * @param maxElemsPerSet the size of each set in the data cache
     */

    public KVServer(int numSets, int maxElemsPerSet) {
        this.dataCache = new KVCache(numSets, maxElemsPerSet);
        this.dataStore = new KVStore();
    }

    /**
     * Performs put request on cache and store.
     *
     * @param  key String key
     * @param  value String value
     * @throws KVException if key or value is too long
     */
    @Override
    public void put(String key, String value) throws KVException {
        // implement me
    	if (key == null || key.length() == 0) 
    		throw new KVException(ERROR_INVALID_KEY);
    	else if (key.length() > MAX_KEY_SIZE) 
    		throw new KVException(ERROR_OVERSIZED_KEY);
    	
    	if (value == null || value.length() == 0) 
    		throw new KVException(ERROR_INVALID_VALUE);
    	if (value.length() > MAX_VAL_SIZE) 
    		throw new KVException(ERROR_OVERSIZED_VALUE);

    	lock = (ReentrantLock) dataCache.getLock(key);
    	try {
        	lock.lock();
    		dataCache.put(key, value);
    		dataStore.put(key, value);
    	} finally {
    		if (lock.isHeldByCurrentThread())
    			lock.unlock();
    	}
    }

    /**
     * Performs get request.
     * Checks cache first. Updates cache if not in cache but located in store.
     *
     * @param  key String key
     * @return String value associated with key
     * @throws KVException with ERROR_NO_SUCH_KEY if key does not exist in store
     */
    @Override
    public String get(String key) throws KVException {
        // implement me
    	if (key == null || key.length() == 0) 
    		throw new KVException(ERROR_INVALID_KEY);
    	else if (key.length() > MAX_KEY_SIZE) 
    		throw new KVException(ERROR_OVERSIZED_KEY);
    	if (!hasKey(key)) 
    		throw new KVException(ERROR_NO_SUCH_KEY);
    	
    	String value = null;
    	lock = (ReentrantLock) dataCache.getLock(key);
    	try {        // try to get in cache
        	lock.lock();
    		value = dataCache.get(key);
        	if (value == null) { // cache miss
        		value = dataStore.get(key);
        	}
        	dataCache.put(key, value); // update cache if the key exists in store
    	} catch (KVException kve) { //back store miss
    		throw kve;
    	} finally {
    		if (lock.isHeldByCurrentThread())
    			lock.unlock();
    	}
    	return value;
    }


    /**
     * Performs del request.
     *
     * @param  key String key
     * @throws KVException with ERROR_NO_SUCH_KEY if key does not exist in store
     */
    @Override
    public void del(String key) throws KVException {
        // implement me
    	if (key == null || key.length() == 0) 
    		throw new KVException(ERROR_INVALID_KEY);
    	else if (key.length() > MAX_KEY_SIZE) 
    		throw new KVException(ERROR_OVERSIZED_KEY);
    	
    	if (!hasKey(key)) 
    		throw new KVException(ERROR_NO_SUCH_KEY);
    	
    	lock = (ReentrantLock) dataCache.getLock(key);
    	try {
    		lock.lock();
    		dataCache.del(key);
    		dataStore.del(key);
    	} catch (KVException kve) {
    		throw kve;
    	} finally {
    		if (lock.isHeldByCurrentThread())
    			lock.unlock();
    	}

    }

    /**
     * Check if the server has a given key. This is used for TPC operations
     * that need to check whether or not a transaction can be performed but
     * you don't want to modify the state of the cache by calling get(). You
     * are allowed to call dataStore.get() for this method.
     *
     * @param key key to check for membership in store
     */
    public boolean hasKey(String key) {
        // implement me
    	try {
    		if (dataStore.get(key) != null)
    			return true;
    	} catch (KVException kve) {
    		return false;
    	}
    	return false;
    }

    /** This method is purely for convenience and will not be tested. */
    @Override
    public String toString() {
        return dataStore.toString() + dataCache.toString();
    }

}
