package kvstore;

import java.util.LinkedList;
import java.util.concurrent.locks.*;

public class ThreadPool {

    /* Array of threads in the threadpool */
    public Thread threads[];
    public LinkedList<Runnable> queue = new LinkedList<Runnable>();
    final Lock lock = new ReentrantLock();
    final Condition notEmpty = lock.newCondition(); 

    /**
     * Constructs a Threadpool with a certain number of threads.
     *
     * @param size number of threads in the thread pool
     */
    public ThreadPool(int size) {
        threads = new Thread[size];
         // implement me
        for (int i = 0; i < size; i++) {
        	threads[i] = new WorkerThread(this);
        	threads[i].start();
        }
    }

    /**
     * Add a job to the queue of jobs that have to be executed. As soon as a
     * thread is available, the thread will retrieve a job from this queue if
     * if one exists and start processing it.
     *
     * @param r job that has to be executed
     * @throws InterruptedException if thread is interrupted while in blocked
     *         state. Your implementation may or may not actually throw this.
     */
    public void addJob(Runnable r) throws InterruptedException {
        // implement me
    	lock.lock();
    	try {
    		queue.addLast(r);
    		notEmpty.signal();
    	} finally {
    		lock.unlock();
    	}
    }

    /**
     * Block until a job is present in the queue and retrieve the job
     * @return A runnable task that has to be executed
     * @throws InterruptedException if thread is interrupted while in blocked
     *         state. Your implementation may or may not actually throw this.
     */
    public Runnable getJob() throws InterruptedException {
        // implement me
    	lock.lock();
    	try {
    		while (queue.isEmpty()) {
    			notEmpty.await();
    		}
    		Runnable r = queue.removeFirst();
    		return r;
    	} finally {
    		lock.unlock();
    	}
    }

    /**
     * A thread in the thread pool.
     */
    public class WorkerThread extends Thread {

        public ThreadPool threadPool;

        /**
         * Constructs a thread for this particular ThreadPool.
         *
         * @param pool the ThreadPool containing this thread
         */
        public WorkerThread(ThreadPool pool) {
            threadPool = pool;
        }

        /**
         * Scan for and execute tasks.
         */
        @Override
        public void run() {
            // implement me
        	while (true) {
        		try {
        			Runnable task = (Runnable) threadPool.getJob();
        			task.run();
        		} catch (Exception e) {
        			e.printStackTrace();
        		}
        	}
        }
    }
}
