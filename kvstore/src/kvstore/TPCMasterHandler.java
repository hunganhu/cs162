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
			KVMessage request;
			KVMessage response;
			boolean isGetRequest = true;
    		try {
    			request = new KVMessage(master, TIMEOUT);
    			if (!request.getMsgType().equals(GET_REQ)) {
    				isGetRequest = false;
    				tpcLog.appendAndFlush(request);
    			}
    			
    			if (request.getMsgType().equals(GET_REQ)) {
    				response = new KVMessage(RESP);
    				String value = kvServer.get(request.getKey());
    				response.setKey(request.getKey());
    				response.setValue(value);
    			} else if (request.getMsgType().equals(PUT_REQ)) {
    				kvServer.put(request.getKey(), request.getValue());
    				response = new KVMessage(READY);
    			} else if (request.getMsgType().equals(DEL_REQ)) {
    				kvServer.del(request.getKey());
    				response = new KVMessage(READY);
    			} else if (request.getMsgType().equals(COMMIT)) {
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
    		}
        		
    	}

    	public MasterHandler(KVServer kvServer, Socket master) {
    		this.kvServer = kvServer;
    		this.master = master;
    	}
    }

}
