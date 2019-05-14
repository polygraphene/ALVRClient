package com.polygraphene.alvr.test;

import com.polygraphene.alvr.NAL;
import com.polygraphene.alvr.NALParser;

import java.util.LinkedList;
import java.util.List;

public class DummyNALParser implements NALParser {
    public List<NAL> mNalList = new LinkedList<>();
    private Object mWaiter = new Object();

    @Override
    public int getNalListSize() {
        return mNalList.size();
    }

    @Override
    public NAL waitNal() {
        return getNal();
    }

    @Override
    public NAL getNal() {
        if(mNalList.size() == 0){
            synchronized (mWaiter) {
                try {
                    mWaiter.wait();
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
            return null;
        }
        NAL elem = mNalList.get(0);
        mNalList.remove(0);
        return elem;
    }

    @Override
    public void recycleNal(NAL nal) {
        // Ignore
    }

    @Override
    public void flushNALList() {
        mNalList.clear();
    }

    @Override
    public void notifyWaitingThread() {
        synchronized (mWaiter) {
            mWaiter.notifyAll();
        }
    }

    @Override
    public void clearStoppedAndPrepared() {
        // Ignore
    }

    @Override
    public void setSinkPrepared(boolean prepared) {

    }
}
