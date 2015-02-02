package kvstore;

import static org.junit.Assert.*;

import org.junit.Test;

public class TPCEndToEndTest extends TPCEndToEndTemplate {

    @Test(timeout = 150000)
    public void testPutGet() throws KVException {
    	for (int i = 0; i < 100; i++) {
    		client.put("foo" + i, "bar");
    		assertEquals("get failed", client.get("foo" + i), "bar");
    	}
        client.put("foo", "bar");
    
        try {
        	client.put("foo1000", "bar1000");
        	client.get("foo1000");
        	client.del("foo999");
        } catch (KVException e){
        	System.out.println("kvexception: " + e.getKVMessage().getMessage());
        } catch (Exception e) {
        	System.out.println("other exception");
        }
    }

}
