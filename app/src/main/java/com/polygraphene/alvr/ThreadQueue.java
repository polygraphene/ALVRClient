package com.polygraphene.alvr;

import java.util.LinkedList;

public class ThreadQueue {
    private LinkedList<Runnable> mQueue = new LinkedList<>();
    private boolean mStopped = false;

    synchronized public void post(Runnable runnable) {
        mQueue.addLast(runnable);
        notifyAll();
    }

    public boolean waitIdle() {
        Runnable runnable;
        while ((runnable = next()) != null) {
            if (mStopped) {
                return false;
            }
            if (runnable != null) {
                runnable.run();
            }
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
