package kvstore;

import static org.junit.Assert.*;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.Assert;
import org.junit.runner.RunWith;
import static org.powermock.api.mockito.PowerMockito.whenNew;
import org.powermock.core.classloader.annotations.PrepareForTest;
import org.powermock.modules.junit4.PowerMockRunner;

import static org.mockito.Matchers.*;
import static org.mockito.Mockito.*;
import java.io.IOException;
import java.net.Socket;
import java.net.SocketException;
import java.net.SocketTimeoutException;
import java.net.UnknownHostException;

@RunWith(PowerMockRunner.class)
@PrepareForTest(TPCSlaveInfo.class)
public class TPCSlaveInfoTest {
    static final long SLAVE1 = 4611686018427387903L;  // Long.MAX_VALUE/2
    static final long SLAVE2 = 9223372036854775807L;  // Long.MAX_VALUE
    static final long SLAVE3 = -4611686018427387903L; // Long.MIN_VALUE/2
    static final long SLAVE4 = -0000000000000000001;  // Long.MIN_VALUE
    static final long SLAVE5 = 6230492013836775123L;  // Arbitrary long value
	TPCSlaveInfo mockSlave = mock(TPCSlaveInfo.class);

	@Before
	public void setUp() throws Exception {
		TPCSlaveInfo mockSlave = mock(TPCSlaveInfo.class);
	}

	@After
	public void tearDown() throws Exception {
	}

	@Test
	public final void testTPCSlaveInfoInvalidFormat() {
		TPCSlaveInfo mySlave;
		
		try {
			mySlave = new TPCSlaveInfo("");
		} catch (KVException kve) {
			String errMsg = kve.getKVMessage().getMessage();
			assertTrue("Empty Info", errMsg.equals(KVConstants.ERROR_INVALID_FORMAT));
		}

		try {
			mySlave = new TPCSlaveInfo(SLAVE1+"@11.11.11.11::1234");
		} catch (KVException kve) {
			String errMsg = kve.getKVMessage().getMessage();
			assertTrue("Empty host", errMsg.equals(KVConstants.ERROR_INVALID_FORMAT));
		}

		try {
			mySlave = new TPCSlaveInfo(SLAVE2+"@:1234");
		} catch (KVException kve) {
			String errMsg = kve.getKVMessage().getMessage();
			assertTrue("Empty host", errMsg.equals(KVConstants.ERROR_INVALID_FORMAT));
		}
		
		try {
			mySlave = new TPCSlaveInfo("@11.11.11.11:1234");
		} catch (KVException kve) {
			String errMsg = kve.getKVMessage().getMessage();
			assertTrue("Empty SlaveId", errMsg.equals(KVConstants.ERROR_INVALID_FORMAT));
		}
		try {
			mySlave = new TPCSlaveInfo(SLAVE3+"@11.11.11.11");
		} catch (KVException kve) {
			String errMsg = kve.getKVMessage().getMessage();
			assertTrue("Empty port", errMsg.equals(KVConstants.ERROR_INVALID_FORMAT));
		}

		try {
			mySlave = new TPCSlaveInfo(SLAVE4+"@11.11.11.11:0xab");
		} catch (KVException kve) {
			String errMsg = kve.getKVMessage().getMessage();
			assertTrue("Invalid port", errMsg.equals(KVConstants.ERROR_INVALID_FORMAT));
		}
	}
	@Test
	public final void testTPCSlaveInfoValidFormat() {
		TPCSlaveInfo mySlave;
		
		try {
			mySlave = new TPCSlaveInfo(SLAVE5+"@11.11.11.11:1234");
			assertEquals("Invalid SlaveID", mySlave.getSlaveID(), SLAVE5);
			assertEquals("Invalid host", mySlave.getHostname(), "11.11.11.11");
			assertEquals("Invalid port", mySlave.getPort(), 1234);
		} catch (KVException kve) {
			String errMsg = kve.getKVMessage().getMessage();
			assertTrue("Empty Info", errMsg.equals(KVConstants.ERROR_INVALID_FORMAT));
		}
		
	}
	@Test
	public final void testGetSlaveID() {
		when(mockSlave.getSlaveID()).thenReturn(SLAVE1);
		assertEquals(SLAVE1, mockSlave.getSlaveID());
	}

	@Test
	public final void testGetHostname() {
		when(mockSlave.getHostname()).thenReturn("111.222.1.2");
		assertEquals("111.222.1.2", mockSlave.getHostname());
	}

	@Test
	public final void testGetPort() {
		when(mockSlave.getPort()).thenReturn(8080);
		assertEquals(8080, mockSlave.getPort());
	}

	@Test
	public final void testConnectHostSocketTimeout() throws Exception {
		setupSocketTimeoutException();
		try {
        	mockSlave.connectHost(3000);
        } catch (KVException kve) {
            String errMsg = kve.getKVMessage().getMessage();
            assertTrue(errMsg.equals(KVConstants.ERROR_SOCKET_TIMEOUT));
        } catch (Exception e) {
			fail("Unexpected exception thrown!");        	
        }
	}

	@Test
	public final void testConnectHostUnknownHost() throws Exception {
		setupSocketHostException();
        try {
        	mockSlave.connectHost(3000);
        } catch (KVException kve) {
            String errMsg = kve.getKVMessage().getMessage();
            assertTrue(errMsg.equals(KVConstants.ERROR_COULD_NOT_CREATE_SOCKET));
        } catch (Exception e) {
			fail("Unexpected exception thrown!");        	
        }
	}

	@Test
	public final void testConnectHostConnectFail() throws Exception {
        setupSocketIOException();
        try {
        	mockSlave.connectHost(3000);
        } catch (KVException kve) {
            String errMsg = kve.getKVMessage().getMessage();
            assertTrue(errMsg.equals(KVConstants.ERROR_COULD_NOT_CONNECT) ||
                errMsg.equals(KVConstants.ERROR_COULD_NOT_CREATE_SOCKET));
        } catch (Exception e) {
			fail("Unexpected exception thrown!");        	
        }
	}

	@Test
	public final void testCloseHost() throws Exception {
		Socket mockSock = mock(Socket.class);

		doThrow(new IOException()).when(mockSock).close();
		try {
			mockSlave.closeHost(mockSock);
			// IOException will be ignored by closeHost()
		} catch (Exception ex) {
			fail("Unexpected exception thrown!");
		}
		verify(mockSlave).closeHost(mockSock);
	}

	/* ----------------------- BEGIN HELPER METHODS ------------------------ */
    private void setupSocketHostException() throws Exception {
        whenNew(Socket.class).withArguments(anyString(), anyInt())
            .thenThrow(new UnknownHostException());
    }

    private void setupSocketIOException() throws Exception {
        whenNew(Socket.class).withArguments(anyString(), anyInt())
            .thenThrow(new IOException());
    }

    private void setupSocketTimeoutException() throws Exception {
        whenNew(Socket.class).withArguments(anyString(), anyInt())
            .thenThrow(new SocketTimeoutException());
    }
}
