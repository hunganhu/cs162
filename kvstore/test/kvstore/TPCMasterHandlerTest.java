package kvstore;

import static org.junit.Assert.*;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.experimental.categories.Category;
import org.junit.runner.RunWith;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.Socket;

import org.powermock.core.classloader.annotations.PrepareForTest;
import org.powermock.modules.junit4.PowerMockRunner;

import static org.mockito.Matchers.anyInt;
import static org.mockito.Mockito.*;

import static kvstore.KVConstants.*;
import static autograder.TestUtils.kTimeoutQuick;
import autograder.AGCategories.AGTestDetails;
import autograder.AGCategories.AG_PROJ4_CODE;

@RunWith(PowerMockRunner.class)
@PrepareForTest({Socket.class, KVMessage.class, TPCMasterHandler.class, TPCSlaveInfo.class})
public class TPCMasterHandlerTest {

    private KVServer server;
    private TPCMasterHandler masterHandler;
    private Socket sock1;
    private Socket sock2;
    private Socket sock3;

    private static final String LOG_PATH = "TPCMasterHandlerTest.log";
 
	@Before
	public void setupTPCMasterHandler() throws Exception {
        server = new KVServer(10, 10);
        TPCLog log = new TPCLog(LOG_PATH, server);
        Utils.setupMockThreadPool();
        masterHandler = new TPCMasterHandler( 1L, server, log);
	}

	@After
	public void tearDown() throws Exception {
        server = null;
        masterHandler = null;
        sock1 = null;
        sock2 = null;
        sock3 = null;
        File log = new File(LOG_PATH);

        if (log.exists() && !log.delete()) { // true iff delete failed.
            System.err.printf("deleting log-file at %s failed.\n", log.getAbsolutePath());
        }
	}

/*
 	@Test
	public final void testRegisterWithMaster() {
		fail("Not yet implemented");
	}
*/	
	
    @Test(timeout = kTimeoutQuick)
    @Category(AG_PROJ4_CODE.class)
    @AGTestDetails(points = 1,
        desc = "Test if they can fail to get one value using Handler")
    public void testFailureGet() throws KVException {
        setupSocketSuccess();
        InputStream getreqFile = getClass().getClassLoader().getResourceAsStream("getreq.txt");
        ByteArrayOutputStream tempOut = new ByteArrayOutputStream();
        try {
            doNothing().when(sock1).setSoTimeout(anyInt());
            when(sock1.getInputStream()).thenReturn(getreqFile);
            when(sock1.getOutputStream()).thenReturn(tempOut);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }

        masterHandler.handle(sock1);

        try {
            doNothing().when(sock3).setSoTimeout(anyInt());
            when(sock3.getInputStream()).thenReturn(new ByteArrayInputStream(tempOut.toByteArray()));
        } catch (IOException e) {
            throw new RuntimeException(e);
        }

        KVMessage check = new KVMessage(sock3);
        assertEquals(RESP, check.getMsgType());
        assertEquals(ERROR_NO_SUCH_KEY, check.getMessage());
    }

//	@Test(timeout = kTimeoutQuick)
	@Test(timeout = 3000000)
    @Category(AG_PROJ4_CODE.class)
    @AGTestDetails(points = 1,
        desc = "Test 2PC if they can put one value using Handler")
	public void testSuccessPut() throws KVException {
        setupSocketSuccess();
        // send put request
        InputStream putreqFile = getClass().getClassLoader().getResourceAsStream("putreq.txt");
        ByteArrayOutputStream tempOut = new ByteArrayOutputStream();
        ByteArrayOutputStream tempAck = new ByteArrayOutputStream();
        try {
            doNothing().when(sock1).setSoTimeout(anyInt());
            when(sock1.getInputStream()).thenReturn(putreqFile);
            when(sock1.getOutputStream()).thenReturn(tempOut);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }

        masterHandler.handle(sock1);

        try {
            doNothing().when(sock3).setSoTimeout(anyInt());
            when(sock3.getInputStream()).thenReturn(new ByteArrayInputStream(tempOut.toByteArray()));
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
        // get ready response
        KVMessage check = new KVMessage(sock3);
        assertEquals(READY, check.getMsgType());

        // send commit
        InputStream commitFile = getClass().getClassLoader().getResourceAsStream("commit.txt");
        try {
            doNothing().when(sock2).setSoTimeout(anyInt());
            when(sock2.getInputStream()).thenReturn(commitFile);
            when(sock2.getOutputStream()).thenReturn(tempAck);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }

        masterHandler.handle(sock2);

        try {
            doNothing().when(sock3).setSoTimeout(anyInt());
            when(sock3.getInputStream()).thenReturn(new ByteArrayInputStream(tempAck.toByteArray()));
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
        // get ack response
        check = new KVMessage(sock3);
        assertEquals(ACK, check.getMsgType());
        
        assertTrue(server.hasKey("key"));
        assertEquals("value", server.get("key"));
        
    }

//	@Test(timeout = kTimeoutQuick)
	@Test(timeout = 3000000)
    @Category(AG_PROJ4_CODE.class)
    @AGTestDetails(points = 1,
        desc = "Test 2PC if they can delete one value using Handler")
	public void testFailureDel() throws KVException {
        setupSocketSuccess();
        // send put request
        InputStream putreqFile = getClass().getClassLoader().getResourceAsStream("delreq.txt");
        ByteArrayOutputStream tempOut = new ByteArrayOutputStream();
        ByteArrayOutputStream tempAck = new ByteArrayOutputStream();
        try {
            doNothing().when(sock1).setSoTimeout(anyInt());
            when(sock1.getInputStream()).thenReturn(putreqFile);
            when(sock1.getOutputStream()).thenReturn(tempOut);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }

        masterHandler.handle(sock1);

        try {
            doNothing().when(sock3).setSoTimeout(anyInt());
            when(sock3.getInputStream()).thenReturn(new ByteArrayInputStream(tempOut.toByteArray()));
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
        // get ready response
        KVMessage check = new KVMessage(sock3);
        assertEquals(ABORT, check.getMsgType());
        assertEquals(ERROR_NO_SUCH_KEY, check.getMessage());

        // send abort
        InputStream commitFile = getClass().getClassLoader().getResourceAsStream("abort.txt");
        try {
            doNothing().when(sock2).setSoTimeout(anyInt());
            when(sock2.getInputStream()).thenReturn(commitFile);
            when(sock2.getOutputStream()).thenReturn(tempAck);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }

        masterHandler.handle(sock2);

        try {
            doNothing().when(sock3).setSoTimeout(anyInt());
            when(sock3.getInputStream()).thenReturn(new ByteArrayInputStream(tempAck.toByteArray()));
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
        // get ack response
        check = new KVMessage(sock3);
        assertEquals(ACK, check.getMsgType());
        
        assertFalse(server.hasKey("key"));
        
    }
    
    /* begin helper methods. */
    private void setupSocketSuccess() {
        sock1 = mock(Socket.class);
        sock2 = mock(Socket.class);
        sock3 = mock(Socket.class);
    }

}
