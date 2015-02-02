package kvstore;

import static kvstore.KVConstants.*;
import java.io.IOException;
import java.net.Socket;
/**
 * Implements NetworkHandler to handle 2PC operation requests from the Master/
 * Coordinator Server
 */
public class TPCMasterHandler implements NetworkHandler {

	public long slaveID;
	public KVServer kvServer;
	public TPCLog tpcLog;
	public ThreadPool threadpool;

	// implement me
	public static final int REGISTERPORT = 9090;
	public static final int TIMEOUT = 3000;

	/**
	 * Constructs a TPCMasterHandler with one connection in its ThreadPool
	 *
	 * @param slaveID the ID for this slave server
	 * @param kvServer KVServer for this slave
	 * @param log the log for this slave
	 */
	public TPCMasterHandler(long slaveID, KVServer kvServer, TPCLog log) {
		this(slaveID, kvServer, log, 1);
	}

	/**
	 * Constructs a TPCMasterHandler with a variable number of connections
	 * in its ThreadPool
	 *
	 * @param slaveID the ID for this slave server
	 * @param kvServer KVServer for this slave
	 * @param log the log for this slave
	 * @param connections the number of connections in this slave's ThreadPool
	 */
	public TPCMasterHandler(long slaveID, KVServer kvServer, TPCLog log, int connections) {
		this.slaveID = slaveID;
		this.kvServer = kvServer;
		this.tpcLog = log;
		this.threadpool = new ThreadPool(connections);
	}

	/**
	 * Registers this slave server with the master.
	 *
	 * @param masterHostname
	 * @param server SocketServer used by this slave server (which contains the
	 *               hostname and port this slave is listening for requests on
	 * @throws KVException with ERROR_INVALID_FORMAT if the response from the
	 *         master is received and parsed but does not correspond to a
	 *         success as defined in the spec OR any other KVException such
	 *         as those expected in KVClient in project 3 if unable to receive
	 *         and/or parse message
	 */
	public void registerWithMaster(String masterHostname, SocketServer server)
			throws KVException {
		// implement me
		try {
			Socket sock = new Socket (masterHostname, REGISTERPORT);
			String slaveInfo = Long.toString(slaveID) + "@" + 
					server.getHostname() + ":" + server.getPort();
			KVMessage request = new KVMessage(KVConstants.REGISTER, slaveInfo);
			request.sendMessage(sock);
			KVMessage response = new KVMessage(sock);
			if (!response.getMsgType().equalsIgnoreCase(RESP) ||
					!response.getMessage().equals("Successful registered " + slaveInfo))
				throw new KVException("Register error.");
		} catch (IOException ioe) {
			throw new KVException(ERROR_COULD_NOT_CREATE_SOCKET);
		} catch (KVException kve) {
			throw kve;
		}

	}

	/**
	 * Creates a job to service the request on a socket and enqueues that job
	 * in the thread pool. Ignore any InterruptedExceptions.
	 *
	 * @param master Socket connected to the master with the request
	 */
	@Override
	public void handle(Socket master) {
		// implement me
		Runnable r = new MasterHandler (this.kvServer, master);
		try {
			threadpool.addJob(r);    		
		} catch (InterruptedException ie) {
			return;
		}
	}
	private class MasterHandler implements Runnable {
		private KVServer kvServer = null;
		private Socket master = null;

		@Override
		public void run() {
			// Implement Me!
			KVMessage request = null;
			KVMessage response = null;
			KVMessage phase1 = null;

			boolean isGetRequest = true;
			try {
				request = new KVMessage(master, TIMEOUT);
				if (!request.getMsgType().equals(GET_REQ)) {
					isGetRequest = false;
					phase1 = tpcLog.getLastEntry();
					tpcLog.appendAndFlush(request);
				}

				if (request.getMsgType().equals(GET_REQ)) {
					response = new KVMessage(RESP);
					String value = kvServer.get(request.getKey());
					response.setKey(request.getKey());
					response.setValue(value);
				} else if (request.getMsgType().equals(PUT_REQ)) {
					if (request.getKey() == null || request.getKey().length() > 256)
						response = new KVMessage(ABORT);
					else
						response = new KVMessage(READY);
				} else if (request.getMsgType().equals(DEL_REQ)) {
					if (request.getKey() == null || request.getKey().length() > 256)  // invalid key
						response = new KVMessage(ABORT);
					else if (!kvServer.hasKey(request.getKey()))  // key not exist
						response = new KVMessage(ABORT, ERROR_NO_SUCH_KEY);						
					else
						response = new KVMessage(READY);
				} else if (request.getMsgType().equals(COMMIT)) {
					if (phase1.getMsgType().equals(KVConstants.PUT_REQ)) {
						kvServer.put(phase1.getKey(), phase1.getValue());						
					} else if (phase1.getMsgType().equals(KVConstants.DEL_REQ)) {
						kvServer.del(phase1.getKey());
					}					
					response = new KVMessage(ACK);
				} else if (request.getMsgType().equals(ABORT)) {
					response = new KVMessage(ACK);
				} else {
					response = new KVMessage(RESP, "Data Error: Invalid Message Type");
				}
			} catch (KVException kve) {
				if (isGetRequest)
					response = new KVMessage(RESP, kve.getMessage());
				else
					response = new KVMessage(ABORT, kve.getMessage());
			}


			try {
				response.sendMessage(master);
			} catch (KVException kve) {
				kve.printStackTrace();
			} finally {
				try {
					master.close();
				} catch (Exception e) {
					// ignore
				}
			}

		}

		public MasterHandler(KVServer kvServer, Socket master) {
			this.kvServer = kvServer;
			this.master = master;
		}
	}

}
