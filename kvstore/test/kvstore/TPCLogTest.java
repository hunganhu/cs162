package kvstore;

import static org.junit.Assert.*;

import java.io.File;
import java.util.ArrayList;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import org.junit.experimental.categories.Category;
import org.mockito.ArgumentMatcher;
import org.mockito.invocation.InvocationOnMock;
import org.mockito.stubbing.Answer;
import static org.mockito.Matchers.*;
import static org.mockito.Mockito.*;

public class TPCLogTest {
    KVServer mockServer;
    KVServer kvServer;
    File tempFile;
    private String logPath;
    TPCLog mockLog;
    TPCLog log;

	@Before
	public void setup() throws Exception {
		mockServer = mock(KVServer.class);
        tempFile = new File("TempLogMDK.txt");
        tempFile.deleteOnExit();
		logPath = tempFile.getPath();
		mockLog = new TPCLog(logPath, mockServer);
	}
	
	
	@After
	public void tearDown() throws Exception {
		tempFile.delete();
		mockServer = null;
		kvServer = null;
	}

	@Test
	public final void testTPCLog() {
		assertTrue (mockLog instanceof TPCLog);
	}
/*
	@Test
	public final void testAppendAndFlush() {
		fail("Not yet implemented");
	}

	@Test
	public final void testGetLastEntry() {
		fail("Not yet implemented");
	}

	@Test
	public final void testLoadFromDisk() {
		fail("Not yet implemented");
	}

	@Test
	public final void testFlushToDisk() {
		fail("Not yet implemented");
	}
*/
	@Test
	public final void testRebuildServerWith6Puts() {
		setupLog(mockLog);
		
		kvServer = new KVServer(10,10);
		try {
		log = new TPCLog(logPath, kvServer);
		} catch (KVException kve) {
			fail("TPCLog build error");
		}
		
        try{
            assertTrue("Key 'key1' not found", kvServer.get("key1").equals("value1"));
            assertTrue("Key 'key2' not found", kvServer.get("key2").equals("value2"));
            assertTrue("Key 'key3' not found", kvServer.get("key3").equals("value3"));
            assertTrue("Key 'key4' not found", kvServer.get("key4").equals("value4"));
            assertTrue("Key 'key5' not found", kvServer.get("key5").equals("value5"));
            assertTrue("Key 'key6' not found", kvServer.get("key6").equals("value6"));
        } catch (KVException e) {
    	   fail("Key not found on rebuild.");
        }
	}

	@Test
	public final void testRebuildServerWithAbort() {
		setupLogWithAbort(mockLog);
		
		kvServer = new KVServer(10,10);
		try {
		log = new TPCLog(logPath, kvServer);
		} catch (KVException kve) {
			fail("TPCLog build error");
		}
		
        try{
            assertFalse("Key 'key1' not deleted", kvServer.hasKey("key1"));
            assertTrue ("Key 'key2' not found", kvServer.get("key2").equals("value2"));
            assertFalse("Key 'key3' added", kvServer.hasKey("key3"));
        } catch (KVException e) {
    	   fail(e.getKVMessage().getMessage());
        }
	}

    public void setupLog(TPCLog log){
        KVMessage putKey1 = new KVMessage(KVConstants.PUT_REQ);
        putKey1.setKey("key1");
        putKey1.setValue("value1");
        KVMessage putKey2 = new KVMessage(KVConstants.PUT_REQ);
        putKey2.setKey("key2");
        putKey2.setValue("value2");
        KVMessage putKey3 = new KVMessage(KVConstants.PUT_REQ);
        putKey3.setKey("key3");
        putKey3.setValue("value3");
        KVMessage putKey4 = new KVMessage(KVConstants.PUT_REQ);
        putKey4.setKey("key4");
        putKey4.setValue("value4");
        KVMessage putKey5 = new KVMessage(KVConstants.PUT_REQ);
        putKey5.setKey("key5");
        putKey5.setValue("value5");
        KVMessage putKey6 = new KVMessage(KVConstants.PUT_REQ);
        putKey6.setKey("key6");
        putKey6.setValue("value6");

        KVMessage com = new KVMessage(KVConstants.COMMIT);

        log.appendAndFlush(putKey1);
        log.appendAndFlush(com);
        log.appendAndFlush(putKey2);
        log.appendAndFlush(com);
        log.appendAndFlush(putKey3);
        log.appendAndFlush(com);
        log.appendAndFlush(putKey4);
        log.appendAndFlush(com);
        log.appendAndFlush(putKey5);
        log.appendAndFlush(com);
        log.appendAndFlush(putKey6);
        log.appendAndFlush(com);

        //System.out.println("Finished flushing.");

    }
    public void setupLogWithAbort(TPCLog log){
        KVMessage putKey1 = new KVMessage(KVConstants.PUT_REQ);
        putKey1.setKey("key1");
        putKey1.setValue("value1");
        KVMessage putKey2 = new KVMessage(KVConstants.PUT_REQ);
        putKey2.setKey("key2");
        putKey2.setValue("value2");
        KVMessage putKey3 = new KVMessage(KVConstants.PUT_REQ);
        putKey3.setKey("key3");
        putKey3.setValue("value3");
 
        KVMessage delKey1 = new KVMessage(KVConstants.DEL_REQ);
        delKey1.setKey("key1");
        KVMessage delKey2 = new KVMessage(KVConstants.DEL_REQ);
        delKey2.setKey("key2");

        KVMessage com = new KVMessage(KVConstants.COMMIT);
        KVMessage abort = new KVMessage(KVConstants.ABORT);

        log.appendAndFlush(putKey1);
        log.appendAndFlush(com);
        log.appendAndFlush(putKey2);
        log.appendAndFlush(com);
        log.appendAndFlush(delKey1);
        log.appendAndFlush(com);          // key1 deleted
        log.appendAndFlush(delKey2);
        log.appendAndFlush(abort);        // key2 not deleted
        log.appendAndFlush(putKey3);
        log.appendAndFlush(abort);        // key3 not added

        //System.out.println("Finished flushing.");

    }


}

