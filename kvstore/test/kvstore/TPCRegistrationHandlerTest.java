package kvstore;

import static org.junit.Assert.*;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.experimental.categories.Category;
import static org.mockito.Matchers.anyInt;
import static org.mockito.Mockito.*;
import org.powermock.core.classloader.annotations.PrepareForTest;
import org.powermock.modules.junit4.PowerMockRunner;
import org.junit.runner.RunWith;

import java.io.*;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;
import java.net.Socket;

import static org.mockito.Mockito.*;
import static kvstore.KVConstants.*;
import static autograder.TestUtils.kTimeoutQuick;
import autograder.AGCategories.AGTestDetails;
import autograder.AGCategories.AG_PROJ4_CODE;

@RunWith(PowerMockRunner.class)
@PrepareForTest({Socket.class, KVMessage.class, TPCRegistrationHandler.class, TPCSlaveInfo.class})
public class TPCRegistrationHandlerTest {
	TPCRegistrationHandler registerHandler;
	TPCMaster master;
	KVCache masterCache;

    private Socket sock1;
    private Socket sock2;
    private Socket sock3;
	
	@Before
	public void setUpMaster() throws Exception {
//		masterCache = new KVCache(2, 2);
		masterCache = mock(KVCache.class);
		master = new TPCMaster(4, masterCache);
        Utils.setupMockThreadPool();
        registerHandler = new TPCRegistrationHandler(master);

	}

	@After
	public void tearDown() throws Exception {
		masterCache = null;
		master = null;
		registerHandler = null;
	}


//	@Test(timeout = kTimeoutQuick)
	@Test(timeout = 3000000)
    @Category(AG_PROJ4_CODE.class)
    @AGTestDetails(points = 1,
        desc = "Test successful registration using Handler")
	public void testSuccessRegistration() throws KVException {
        setupSocketSuccess();
        // send register request                
        InputStream registerFile = getClass().getClassLoader().getResourceAsStream("register.txt");
        ByteArrayOutputStream tempOut = new ByteArrayOutputStream();
        try {
            doNothing().when(sock1).setSoTimeout(anyInt());
            when(sock1.getInputStream()).thenReturn(registerFile);
            when(sock1.getOutputStream()).thenReturn(tempOut);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
        registerHandler.handle(sock1);

        try {
            doNothing().when(sock3).setSoTimeout(anyInt());
            when(sock3.getInputStream()).thenReturn(new ByteArrayInputStream(tempOut.toByteArray()));
        } catch (IOException e) {
            throw new RuntimeException(e);
        }

        KVMessage check = new KVMessage(sock3);
        assertEquals(RESP, check.getMsgType());
        assertEquals("Successful registered", check.getMessage().substring(0, 21));
        
    }

	@Test(timeout = 3000000)
    @Category(AG_PROJ4_CODE.class)
    @AGTestDetails(points = 1,
        desc = "Test invalid registration using Handler")
	public void testInvalidRegistration() throws KVException {
        setupSocketSuccess();
        // send register request                
        InputStream registerFile = getClass().getClassLoader().getResourceAsStream("putreq.txt");
        ByteArrayOutputStream tempOut = new ByteArrayOutputStream();
        try {
            doNothing().when(sock1).setSoTimeout(anyInt());
            when(sock1.getInputStream()).thenReturn(registerFile);
            when(sock1.getOutputStream()).thenReturn(tempOut);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
        registerHandler.handle(sock1);

        try {
            doNothing().when(sock3).setSoTimeout(anyInt());
            when(sock3.getInputStream()).thenReturn(new ByteArrayInputStream(tempOut.toByteArray()));
        } catch (IOException e) {
            throw new RuntimeException(e);
        }

        KVMessage check = new KVMessage(sock3);
        assertEquals(RESP, check.getMsgType());
        assertNotEquals("Successful registered", check.getMessage().substring(0, 21));
        
    }

	/* begin helper methods. */
    private void setupSocketSuccess() {
        sock1 = mock(Socket.class);
        sock2 = mock(Socket.class);
        sock3 = mock(Socket.class);
    }
}
