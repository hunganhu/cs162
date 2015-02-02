package kvstore;

import static kvstore.KVConstants.*;

import java.io.IOException;
import java.net.Socket;

/**
 * This NetworkHandler will asynchronously handle the socket connections.
 * It uses a threadPool to ensure that none of it's methods are blocking.
 */
public class TPCClientHandler implements NetworkHandler {

    public TPCMaster tpcMaster;
    public ThreadPool threadPool;

    /**
     * Constructs a TPCClientHandler with ThreadPool of a single thread.
     *
     * @param tpcMaster TPCMaster to carry out requests
     */
    public TPCClientHandler(TPCMaster tpcMaster) {
        this(tpcMaster, 1);
    }

    /**
     * Constructs a TPCClientHandler with ThreadPool of a single thread.
     *
     * @param tpcMaster TPCMaster to carry out requests
     * @param connections number of threads in threadPool to service requests
     */
    public TPCClientHandler(TPCMaster tpcMaster, int connections) {
        // implement me
    	this.tpcMaster = tpcMaster;
    	threadPool = new ThreadPool(connections);	

    }

    /**
     * Creates a job to service the request on a socket and enqueues that job
     * in the thread pool. Ignore InterruptedExceptions.
     *
     * @param client Socket connected to the client with the request
     */
    @Override
    public void handle(Socket client) {
        // implement me
    	Runnable r = new ClientHandler (this.tpcMaster, client);
    	try {
    		threadPool.addJob(r);    		
    	} catch (InterruptedException ie) {
    		return;
    	}
    }
    
    // implement me
    private class ClientHandler implements Runnable {
    	private TPCMaster master = null;
    	private Socket client = null;

    	@Override
    	public void run() {
    		// Implement Me!
			KVMessage request;
			KVMessage response;
     		try {
    			request = new KVMessage(client);
    			response = new KVMessage(RESP);
    			if (request.getMsgType().equals(GET_REQ)) {
    				String value = master.handleGet(request);
    				response.setKey(request.getKey());
       				response.setValue(value);
    			} else if (request.getMsgType().equals(PUT_REQ)) {
    				master.handleTPCRequest(request, true);
    				response.setMessage(SUCCESS);        				
    			} else if (request.getMsgType().equals(DEL_REQ)) {
    				master.handleTPCRequest(request, false);
    				response.setMessage(SUCCESS);
    			} else {
    				response.setMessage("Data Error: Invalid Message Type");
    			}
    		} catch (KVException kve) {
    			response = kve.getKVMessage();
    		}
    		
    		try {
        		response.sendMessage(client);
    		} catch (KVException kve) {
    			kve.printStackTrace();
    		}
    		
    	}

    	public ClientHandler(TPCMaster master, Socket client) {
    		this.master = master;
    		this.client = client;
    	}
    }


}
