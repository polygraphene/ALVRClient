package com.polygraphene.alvr;

public class ThreadBase {
    private Thread mThread;
    private boolean mStopped = false;

    public void start() {
        mThread = new MyThread();
        mStopped = false;
        mThread.start();
    }

    public void stopAndWait() {
        interrupt();
        while (mThread.isAlive()) {
            try {
                mThread.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
    }

    public void interrupt() {
        mStopped = true;
    }

    public void run() {

    }

    public boolean isStopped() {
        return mStopped;
    }

    private class MyThread extends Thread {
        @Override
        public void run() {
            setName(ThreadBase.this.getClass().getName());

            ThreadBase.this.run();
        }
    }
}
