package kvstore;

import static kvstore.KVConstants.*;

import java.io.IOException;
import java.net.*;
import java.util.regex.*;

/**
 * Data structure to maintain information about SlaveServers
 */
public class TPCSlaveInfo {

    public long slaveID;
    public String hostname;
    public int port;

    /**
     * Construct a TPCSlaveInfo to represent a slave server.
     *
     * @param info as "SlaveServerID@Hostname:Port"
     * @throws KVException ERROR_INVALID_FORMAT if info string is invalid
     */
    public TPCSlaveInfo(String info) throws KVException {
        // implement me
    	String [] part1;   // split to SlaveServerID and host:port
    	String [] part2; // split to host and port

    	try {
    		if (info.isEmpty())
           		throw new KVException(KVConstants.ERROR_INVALID_FORMAT);   			
    		else 
    			part1 = info.split("@");
    		
    		if (part1.length != 2)    			
           		throw new KVException(KVConstants.ERROR_INVALID_FORMAT);   			
    		else
    			part2 = part1[1].split(":");

    		if (part2.length != 2) {    			
           		throw new KVException(KVConstants.ERROR_INVALID_FORMAT);   			
    		}
    		
    		slaveID = Long.parseLong(part1[0].trim());
    		hostname = part2[0].trim();
    		port = Integer.parseInt(part2[1].trim());
        	if (hostname.length() == 0) {
        		throw new KVException(KVConstants.ERROR_INVALID_FORMAT);
        	}
    	} catch (NumberFormatException nfe) {
       		throw new KVException(KVConstants.ERROR_INVALID_FORMAT);   		
    	}
    }

    public long getSlaveID() {
        return slaveID;
    }

    public String getHostname() {
        return hostname;
    }

    public int getPort() {
        return port;
    }

    /**
     * Create and connect a socket within a certain timeout.
     *
     * @return Socket object connected to SlaveServer, with timeout set
     * @throws KVException ERROR_SOCKET_TIMEOUT, ERROR_COULD_NOT_CREATE_SOCKET,
     *         or ERROR_COULD_NOT_CONNECT
     */
    public Socket connectHost(int timeout) throws KVException {
        // implement me
    	try {
    		Socket sock = new Socket(hostname, port);
    		sock.setSoTimeout(timeout);
    		return sock;
    	} catch (SocketTimeoutException ste) {
    		throw new KVException(ERROR_SOCKET_TIMEOUT);
    	} catch (IllegalArgumentException iae) {
       		throw new KVException(ERROR_COULD_NOT_CREATE_SOCKET);    		
    	} catch (UnknownHostException uhe) {
       		throw new KVException(ERROR_COULD_NOT_CREATE_SOCKET);    		
    	} catch (IOException ioe) {
    		throw new KVException(ERROR_COULD_NOT_CONNECT);
    	}
    }

    /**
     * Closes a socket.
     * Best effort, ignores error since the response has already been received.
     *
     * @param sock Socket to be closed
     */
    public void closeHost(Socket sock) {
        // implement me
    	try {
    		sock.close();
    	} catch (IOException ex) {
    		// ignore error
    	}
    }
    
    public String toString() {
    	return new Long(slaveID).toString()+"@"+hostname+":"+port;
    }
}
