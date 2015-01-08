package kvstore;

import static kvstore.KVConstants.DEL_REQ;
import static kvstore.KVConstants.ERROR_COULD_NOT_CONNECT;
import static kvstore.KVConstants.GET_REQ;
import static kvstore.KVConstants.PUT_REQ;
import static kvstore.KVConstants.RESP;
import static kvstore.KVConstants.SUCCESS;

import java.io.IOException;
import java.net.Socket;

/**
 * This NetworkHandler will asynchronously handle the socket connections.
 * Uses a thread pool to ensure that none of its methods are blocking.
 */
public class ServerClientHandler implements NetworkHandler {

    public KVServer kvServer;
    public ThreadPool threadPool;

    /**
     * Constructs a ServerClientHandler with ThreadPool of a single thread.
     *
     * @param kvServer KVServer to carry out requests
     */
    public ServerClientHandler(KVServer kvServer) {
        this(kvServer, 1);
    }

    /**
     * Constructs a ServerClientHandler with ThreadPool of thread equal to
     * the number passed in as connections.
     *
     * @param kvServer KVServer to carry out requests
     * @param connections number of threads in threadPool to service requests
     */
    public ServerClientHandler(KVServer kvServer, int connections) {
        // implement me
    	this.kvServer = kvServer;
    	threadPool = new ThreadPool(connections);	
    }

    /**
     * Creates a job to service the request for a socket and enqueues that job
     * in the thread pool. Ignore any InterruptedExceptions.
     *
     * @param client Socket connected to the client with the request
     */
    @Override
    public void handle(Socket client) {
        // implement me
    	Runnable r = new ClientHandler (this.kvServer, client);
    	try {
    		threadPool.addJob(r);    		
    	} catch (InterruptedException ie) {
    		return;
    	}
    }
    
    // implement me
    private class ClientHandler implements Runnable {
    	private KVServer kvServer = null;
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
    				String value = kvServer.get(request.getKey());
    				response.setKey(request.getKey());
       				response.setValue(value);
    			} else if (request.getMsgType().equals(PUT_REQ)) {
    				kvServer.put(request.getKey(), request.getValue());
    				response.setMessage(SUCCESS);        				
    			} else if (request.getMsgType().equals(DEL_REQ)) {
    				kvServer.del(request.getKey());
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

    	public ClientHandler(KVServer kvServer, Socket client) {
    		this.kvServer = kvServer;
    		this.client = client;
    	}
    }

}
