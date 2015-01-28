package kvstore;

import static kvstore.KVConstants.*;

import java.net.Socket;
import java.util.*;
import java.util.concurrent.locks.Lock;

public class TPCMaster {

    public int numSlaves;
    public KVCache masterCache;
    public final SortedMap<Long, TPCSlaveInfo> slaves = 
    		new TreeMap<Long, TPCSlaveInfo>();
    private Lock lock;

    public static final int TIMEOUT = 3000;

    /**
     * Creates TPCMaster, expecting numSlaves slave servers to eventually register
     *
     * @param numSlaves number of slave servers expected to register
     * @param cache KVCache to cache results on master
     */
    public TPCMaster(int numSlaves, KVCache cache) {
        this.numSlaves = numSlaves;
        this.masterCache = cache;
        // implement me
    }

    /**
     * Registers a slave. Drop registration request if numSlaves already
     * registered. Note that a slave re-registers under the same slaveID when
     * it comes back online.
     *
     * @param slave the slaveInfo to be registered
     */
    public void registerSlave(TPCSlaveInfo slave) throws KVException {
    	// implement me
    	Long key = new Long(slave.getSlaveID());
    	if (getNumRegisteredSlaves() < numSlaves) {
    		if (!slaves.containsKey(key)) {
    			slaves.put(key, slave);
    		} else {
    			slaves.remove(key);
    			slaves.put(key, slave);
    		}
    	}
    }

    /**
     * Converts Strings to 64-bit longs. Borrowed from http://goo.gl/le1o0W,
     * adapted from String.hashCode().
     *
     * @param string String to hash to 64-bit
     * @return long hashcode
     */
    public static long hashTo64bit(String string) {
        long h = 1125899906842597L;
        int len = string.length();

        for (int i = 0; i < len; i++) {
            h = (31 * h) + string.charAt(i);
        }
        return h;
    }

    /**
     * Compares two longs as if they were unsigned (Java doesn't have unsigned
     * data types except for char). Borrowed from http://goo.gl/QyuI0V
     *
     * @param n1 First long
     * @param n2 Second long
     * @return is unsigned n1 less than unsigned n2
     */
    public static boolean isLessThanUnsigned(long n1, long n2) {
        return (n1 < n2) ^ ((n1 < 0) != (n2 < 0));
    }

    /**
     * Compares two longs as if they were unsigned, uses isLessThanUnsigned
     *
     * @param n1 First long
     * @param n2 Second long
     * @return is unsigned n1 less than or equal to unsigned n2
     */
    public static boolean isLessThanEqualUnsigned(long n1, long n2) {
        return isLessThanUnsigned(n1, n2) || (n1 == n2);
    }

    /**
     * Find primary replica for a given key.
     *
     * @param key String to map to a slave server replica
     * @return SlaveInfo of first replica
     */
    public TPCSlaveInfo findFirstReplica(String key) {
        // implement me
    	Long hashkey = new Long(hashTo64bit(key));
    	
        if (slaves.isEmpty()) {
            return null;
        }
        SortedMap<Long, TPCSlaveInfo> tailMap = slaves.tailMap(hashkey); // include hashkey
        hashkey = tailMap.isEmpty() ? slaves.firstKey() : tailMap.firstKey();
        return slaves.get(hashkey);
    }

    /**
     * Find the successor of firstReplica.
     *
     * @param firstReplica SlaveInfo of primary replica
     * @return SlaveInfo of successor replica
     */
    public TPCSlaveInfo findSuccessor(TPCSlaveInfo firstReplica) {
        // implement me
    	Long hashkey = new Long(firstReplica.getSlaveID());
        SortedMap<Long, TPCSlaveInfo> tailMap = 
        		((TreeMap <Long, TPCSlaveInfo>) slaves).tailMap(hashkey, false); //exclude hashkey
        hashkey = tailMap.isEmpty() ? slaves.firstKey() : tailMap.firstKey();
        return slaves.get(hashkey);
    }

    /**
     * @return The number of slaves currently registered.
     */
    public int getNumRegisteredSlaves() {
        // implement me
        return slaves.size();
    }

    /**
     * (For testing only) Attempt to get a registered slave's info by ID.
     * @return The requested TPCSlaveInfo if present, otherwise null.
     */
    public TPCSlaveInfo getSlave(long slaveId) {
        // implement me
    	return slaves.get(new Long(slaveId));
    }

    /**
     * Perform 2PC operations from the master node perspective. This method
     * contains the bulk of the two-phase commit logic. It performs phase 1
     * and phase 2 with appropriate timeouts and retries.
     *
     * See the spec for details on the expected behavior.
     *
     * @param msg KVMessage corresponding to the transaction for this TPC request
     * @param isPutReq boolean to distinguish put and del requests
     * @throws KVException if the operation cannot be carried out for any reason
     */
    public synchronized void handleTPCRequest(KVMessage msg, boolean isPutReq)
            throws KVException {
        // implement me
    	
    	String value = null;
    	String key = msg.getKey();
		long hashkey = hashTo64bit(key);
		TPCSlaveInfo slave1 = findFirstReplica(Long.toString(hashkey));
		TPCSlaveInfo slave2 = findSuccessor(slave1);
		Socket socket1 = null;
		Socket socket2 = null;
		KVMessage response1, response2;
		KVMessage phase2;
		System.err.println("[Master] Message:" + msg.toString());

		lock = masterCache.getLock(key);
    	try {
    		lock.lock();
    		if (isPutReq) {
    			value = msg.getValue();
    		}
    		// phase 1 begins
    		socket1 = slave1.connectHost(TIMEOUT);
    		socket2 = slave2.connectHost(TIMEOUT);
    		System.err.println("[Master] Send msg to slave1:" + slave1.toString());
    		msg.sendMessage(socket1);
    		System.err.println("[Master] Send msg to slave2:" + slave2.toString());
    		msg.sendMessage(socket2);

    		response1 = new KVMessage(socket1, TIMEOUT);
    		System.err.println("[Master] Response from slave1:" + response1.toString());
    		response2 = new KVMessage(socket2, TIMEOUT);
    		System.err.println("[Master] Response from slave2:" + response2.toString());

    		// phase 2 begins
    		socket1 = slave1.connectHost(TIMEOUT);
    		socket2 = slave2.connectHost(TIMEOUT);

    		if (response1.getMsgType().equals(READY) &&
    				response2.getMsgType().equals(READY)) {
    			phase2 = new KVMessage(COMMIT);
        		System.err.println("[Master] Send commit to slave1.");
    			phase2.sendMessage(socket1);
        		System.err.println("[Master] Send commit to slave2.");
    			phase2.sendMessage(socket2);
    		} else {
    			phase2 = new KVMessage(ABORT);
        		System.err.println("[Master] Send abort to slave1.");
    			phase2.sendMessage(socket1);
        		System.err.println("[Master] Send abort to slave2.");
    			phase2.sendMessage(socket2);    			
    		}
    		response1 = new KVMessage(socket1, TIMEOUT);
    		System.err.println("[Master] Response from slave1:" + response1.toString());
    		response2 = new KVMessage(socket2, TIMEOUT);
    		System.err.println("[Master] Response from slave2:" + response1.toString());
    		if (response1.getMsgType().equals(ACK) &&
    				response2.getMsgType().equals(ACK)) {   			
    			if (isPutReq) {
    	    		System.err.println("[Master] Cache put key[" + key +"], value["+ value+"]");
    				masterCache.put(key, value);
    			} else {
    	    		System.err.println("[Master] Cache del key[" + key +"]");
    				masterCache.del(key);
    			}
    		} 	
    	} catch (KVException kve) { //back store miss
    		throw kve;
    	} finally {
    		lock.unlock();
    		if (slave1 != null)
    			slave1.closeHost(socket1);
    		if (slave2 != null)
    			slave2.closeHost(socket2);

    	}
    }

    /**
     * Perform GET operation in the following manner:
     * - Try to GET from cache, return immediately if found
     * - Try to GET from first/primary replica
     * - If primary succeeded, return value
     * - If primary failed, try to GET from the other replica
     * - If secondary succeeded, return value
     * - If secondary failed, return KVExceptions from both replicas
     *
     * @param msg KVMessage containing key to get
     * @return value corresponding to the Key
     * @throws KVException with ERROR_NO_SUCH_KEY if unable to get
     *         the value from either slave for any reason
     */
    @SuppressWarnings("unused")
	public String handleGet(KVMessage msg) throws KVException {
    	// implement me
    	KVMessage response;
		Socket socket1 = null;
		Socket socket2 = null;
		TPCSlaveInfo slave1 = null;
		TPCSlaveInfo slave2 = null;
		
    	String value = null;
    	String key = msg.getKey();
    	lock = masterCache.getLock(key);
    	try {        // try to get in cache
    		lock.lock();
    		value = masterCache.get(key);
    		if (value == null) { // cache miss
    			long hashkey = hashTo64bit(key);
    			slave1 = findFirstReplica(Long.toString(hashkey));
    			socket1 = slave1.connectHost(TIMEOUT);
    			msg.sendMessage(socket1);
    			response = new KVMessage(socket1);
    			if (response == null) {
    				slave2 = findSuccessor(slave1);
        			socket2 = slave2.connectHost(TIMEOUT);
    				msg.sendMessage(socket2);
    				response = new KVMessage(socket2);
    				if (response == null) {
    					throw new KVException(KVConstants.ERROR_NO_SUCH_KEY);            		
    				}        		
    			}
				if (!response.getMsgType().equalsIgnoreCase(RESP) ||
						value == null) { // secondary failed
					value = response.getValue();
					masterCache.put(key, value); // update cache if the key exists in store
				}
    		}
    	} catch (KVException kve) { //back store miss
    		throw kve;
    	} finally {
    		lock.unlock();
    		if (slave1 != null)
    			slave1.closeHost(socket1);
    		if (slave2 != null)
    			slave2.closeHost(socket2);
    	}
    	return value;
    }

}
