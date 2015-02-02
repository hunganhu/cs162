package kvstore;

import static kvstore.KVConstants.*;

import java.net.Socket;
import java.util.*;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

public class TPCMaster {

	public int numSlaves;
	public KVCache masterCache;
	public final SortedMap<Long, TPCSlaveInfo> slaves;
	private Lock lock;
    final Lock lockSlave = new ReentrantLock();
    final Condition Full = lockSlave.newCondition(); 

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
		slaves = new TreeMap<Long, TPCSlaveInfo>();
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
		System.err.println("[RegisterSlave] Slave info:["+ slave.getSlaveID() +"]"+slave.getHostname() +":" + slave.getPort());
		lockSlave.lock();
		try {
			if (slaves.containsKey(key)) {
				System.err.println("[RegisterSlave] Update:["+ slave.getSlaveID() +"]"+slave.getHostname() +":" + slave.getPort());
				slaves.remove(key);
				slaves.put(key, slave);				
			} else {
				if (getNumRegisteredSlaves() < numSlaves) {
					System.err.println("[RegisterSlave] Insert:["+ slave.getSlaveID() +"]"+slave.getHostname() +":" + slave.getPort());
					slaves.put(key, slave);					
				} else {
					Full.signalAll();
				}
			}
		} finally {
			lockSlave.unlock();
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

		lockSlave.lock();
		try {
			while (getNumRegisteredSlaves() < numSlaves) {
				Full.await();
			}
		} catch (InterruptedException ie) {
			// ignore
		} finally {
			lockSlave.unlock();
		}
		
		String value = null;
		String key = msg.getKey();
		long hashkey = hashTo64bit(key);
		TPCSlaveInfo slave1 = findFirstReplica(Long.toString(hashkey));
		TPCSlaveInfo slave2 = findSuccessor(slave1);
		Socket socket1 = null;
		Socket socket2 = null;
		KVMessage response1 = null;
		KVMessage response2 = null;
		KVMessage decision = null;
		System.err.println("[Master] Message:" + msg.toString());

		lock = masterCache.getLock(key);
		try {
			lock.lock();
			if (isPutReq) {
				value = msg.getValue();
			}
			// phase 1 begins
			// send message to slave1
			try {
				socket1 = slave1.connectHost(TIMEOUT);
				System.err.println("[Master] Send msg to slave1:" + slave1.toString());
				msg.sendMessage(socket1);
				response1 = new KVMessage(socket1, TIMEOUT);
				System.err.println("[Master] Response from slave1:" + response1.toString());
			} catch (KVException kve) { //back store miss				
				System.err.println("[Master-1] KVException:"+kve.getKVMessage().getMessage());
				response1 = new KVMessage(ABORT, kve.getKVMessage().getMessage());
			} catch (Exception ex) { //back store miss
				System.err.println("[Master-1] Exception:"+ex.getMessage());
				response1 = new KVMessage(ABORT, ex.getMessage());
			} finally {
				if (socket1 != null)
					slave1.closeHost(socket1);
			}
			// send message to slave2
			try {
				socket2 = slave2.connectHost(TIMEOUT);
				System.err.println("[Master] Send msg to slave2:" + slave2.toString());
				msg.sendMessage(socket2);
				response2 = new KVMessage(socket2, TIMEOUT);
				System.err.println("[Master] Response from slave2:" + response2.toString());
			} catch (KVException kve) { //back store miss
				System.err.println("[Master-2] KVException:"+kve.getKVMessage().getMessage());
				response2 = new KVMessage(ABORT, kve.getKVMessage().getMessage());
			} catch (Exception ex) { //back store miss
				System.err.println("[Master-2] Exception:"+ex.getMessage());
				response2 = new KVMessage(ABORT, ex.getMessage());
			} finally {
				if (socket2 != null)
					slave2.closeHost(socket2);
			}
			if (response1.getMsgType().equals(READY) &&
					response2.getMsgType().equals(READY)) {
				decision = new KVMessage(COMMIT);
			} else if (!response1.getMsgType().equals(READY)){
				decision = response1;
			} else {
				decision = response2;
			}

			// phase 2 begins
			// acknowledge slave1
			do {
				try {
					socket1 = slave1.connectHost(TIMEOUT);
					System.err.println("[Master] Acknowledge slave1 with " + decision.getMsgType());
					decision.sendMessage(socket1);
					response1 = new KVMessage(socket1, TIMEOUT);
					System.err.println("[Master] Response from slave1:" + response1.toString());
				} catch (KVException kve) { //back store miss
					System.err.println("[Master-1] KVException:"+kve.getKVMessage().getMessage());
					slave1 = findFirstReplica(Long.toString(hashkey));
				} catch (Exception ex) { //back store miss
					System.err.println("[Master-1] Exception:"+ex.getMessage());
					slave1 = findFirstReplica(Long.toString(hashkey));
				} finally {   		
					if (socket1 != null)
						slave1.closeHost(socket1);
				}
			} while (!response1.getMsgType().equals(ACK));
			// acknowledge slave2
			do {
				try {
					System.err.println("[Master-2] Slave2 info:"+ slave2.getHostname() +":" + slave2.getPort());
					socket2 = slave2.connectHost(TIMEOUT);
					System.err.println("[Master] Acknowledge slave2 with " + decision.getMsgType());
					decision.sendMessage(socket2);
					response2 = new KVMessage(socket2, TIMEOUT);
					System.err.println("[Master] Response from slave2:" + response1.toString());
				} catch (KVException kve) { //back store miss
					System.err.println("[Master-2] KVException:"+kve.getKVMessage().getMessage());
					slave2 = findSuccessor(slave1);
					System.err.println("[Master-2] refresh Slave2 info:"+ slave2.getHostname() +":" + slave2.getPort());
				} catch (Exception ex) { //back store miss
					System.err.println("[Master-2] Exception:"+ex.getMessage());
					slave2 = findSuccessor(slave1);
					System.err.println("[Master-2] refresh Slave2 info:"+ slave2.getHostname() +":" + slave2.getPort());
				} finally {   		
					if (socket2 != null)
						slave2.closeHost(socket2);
				}
			} while (!response2.getMsgType().equals(ACK));

			if (response1.getMsgType().equals(ACK) &&
					response2.getMsgType().equals(ACK)) {
				if (decision.getMsgType().equals(COMMIT)) {
					if (isPutReq) {
						System.err.println("[Master] Cache put key[" + key +"], value["+ value+"]");
						masterCache.put(key, value);
					} else {
						System.err.println("[Master] Cache del key[" + key +"]");
						masterCache.del(key);
					}
				} else {
					throw new KVException(decision);
				}
			} else {
				System.err.println("throw abort exception.");
				throw new KVException(ERROR_INVALID_FORMAT);
			}
		} finally {
			lock.unlock();
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
		lockSlave.lock();
		try {
			while (getNumRegisteredSlaves() < numSlaves) {
				Full.await();
			}
		} catch (InterruptedException ie) {
			// ignore
		} finally {
			lockSlave.unlock();
		}
		KVMessage response;
		Socket socket1 = null;
		Socket socket2 = null;
		TPCSlaveInfo slave1 = null;
		TPCSlaveInfo slave2 = null;

		String value = null;
		String key = msg.getKey();
		lock = masterCache.getLock(key);
		try {        // try to get from cache
			lock.lock();
			value = masterCache.get(key);
			if (value == null) { // cache miss, try to get from first replica
				long hashkey = hashTo64bit(key);
				slave1 = findFirstReplica(Long.toString(hashkey));
				socket1 = slave1.connectHost(TIMEOUT);
				msg.sendMessage(socket1);
				response = new KVMessage(socket1, TIMEOUT);
				if (response == null) { // first failed, try to get from secondary replica
					slave2 = findSuccessor(slave1);
					socket2 = slave2.connectHost(TIMEOUT);
					msg.sendMessage(socket2);
					response = new KVMessage(socket2, TIMEOUT);
					if (response == null) { // secondary failed
						throw new KVException(KVConstants.ERROR_NO_SUCH_KEY);            		
					} else {
						value = response.getValue();
						if (!response.getMsgType().equalsIgnoreCase(RESP) ||
								value == null) { // invalid message
							throw new KVException(KVConstants.ERROR_NO_SUCH_KEY);
						} else {
							masterCache.put(key, value); // update cache if the key exists in store
						}
					}
				}
				value = response.getValue();
				if (!response.getMsgType().equalsIgnoreCase(RESP) ||
						value == null) { // invalid message
					throw new KVException(KVConstants.ERROR_NO_SUCH_KEY);
				} else {
					masterCache.put(key, value); // update cache if the key exists in store
				}
			}
		} catch (KVException kve) { //back store miss
			System.err.println(kve.getKVMessage().getMessage());
			//			throw kve;
		} finally {
			lock.unlock();
			if (socket1 != null)
				slave1.closeHost(socket1);
			if (socket2 != null)
				slave2.closeHost(socket2);
		}
		return value;
	}

}
