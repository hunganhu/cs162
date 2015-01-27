package kvstore;

import static kvstore.KVConstants.*;

import java.io.IOException;
import java.net.Socket;

/**
 * This NetworkHandler will asynchronously handle the socket connections.
 * Uses a thread pool to ensure that none of its methods are blocking.
 */
public class TPCRegistrationHandler implements NetworkHandler {

    private TPCMaster master;
    private ThreadPool threadpool;

    /**
     * Constructs a TPCRegistrationHandler with a ThreadPool of a single thread.
     *
     * @param master TPCMaster to register slave with
     */
    public TPCRegistrationHandler(TPCMaster master) {
        this(master, 1);
    }

    /**
     * Constructs a TPCRegistrationHandler with ThreadPool of thread equal to the
     * number given as connections.
     *
     * @param master TPCMaster to carry out requests
     * @param connections number of threads in threadPool to service requests
     */
    public TPCRegistrationHandler(TPCMaster master, int connections) {
        this.threadpool = new ThreadPool(connections);
        this.master = master;
    }

    /**
     * Creates a job to service the request on a socket and enqueues that job
     * in the thread pool. Ignore any InterruptedExceptions.
     *
     * @param slave Socket connected to the slave with the request
     */
    @Override
    public void handle(Socket slave) {
        // implement me
    	Runnable r = new RegisterHandler (this.master, slave);
    	try {
    		threadpool.addJob(r);    		
    	} catch (InterruptedException ie) {
    		return;
    	}

    }
    
    // implement me
    private class RegisterHandler implements Runnable {
    	private TPCMaster master = null;
    	private Socket slave = null;

    	@Override
    	public void run() {
    		// Implement Me!
			KVMessage request;
			KVMessage response;
     		try {
    			request = new KVMessage(slave);
    			response = new KVMessage(RESP);
    			if (request.getMsgType().equals(REGISTER)) {
    				String slaveInfo = request.getMessage().trim();
    				master.registerSlave(new TPCSlaveInfo(slaveInfo));
    				response.setMessage("Successful registered " + slaveInfo);
    			} else {
    				response.setMessage("Data Error: Invalid Message Type");
    			}
     		} catch (KVException kve) {
    			response = kve.getKVMessage();
     		}
    	
    		try {
        		response.sendMessage(slave);
    		} catch (KVException kve) {
    			kve.printStackTrace();
    		}
    		
    	}

    	public RegisterHandler(TPCMaster master, Socket slave) {
    		this.master = master;
    		this.slave = slave;
    	}
    }

}
