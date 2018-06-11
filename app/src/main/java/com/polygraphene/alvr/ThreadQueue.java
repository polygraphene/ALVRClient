package com.polygraphene.alvr;

import java.util.LinkedList;

public class ThreadQueue {
    private LinkedList<Runnable> mQueue = new LinkedList<>();
    private boolean mStopped = false;

    synchronized public void post(Runnable runnable) {
        if(mStopped) {
            return;
        }
        mQueue.addLast(runnable);
        notifyAll();
    }

    // Post runnable and wait completion.
    public void send(Runnable runnable) {
        synchronized (this) {
            if(mStopped) {
                return;
            }
            mQueue.addLast(runnable);
            notifyAll();

            while(mQueue.contains(runnable)) {
                try {
                    wait();
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }
    }

    public boolean waitIdle() {
        Runnable runnable;
        while ((runnable = next()) != null) {
            if (mStopped) {
                return false;
            }
            runnable.run();
        }
        if(mStopped) {
            return false;
        }
        return true;
    }

    public void interrupt() {
        mStopped = true;
        post(null);
    }

    synchronized private Runnable next() {
        if (mQueue.size() == 0) {
            return null;
        }
        // Notify queue change for threads waiting completion of "send" method.
        notifyAll();
        return mQueue.removeFirst();
    }

    synchronized public void waitNext() {
        while(mQueue.size() == 0){
            try {
                wait();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
    }
}
