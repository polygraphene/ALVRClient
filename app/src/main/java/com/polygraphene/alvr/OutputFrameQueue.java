package com.polygraphene.alvr;

import android.media.MediaCodec;
import android.support.annotation.NonNull;
import android.util.Log;

import java.util.LinkedList;
import java.util.Queue;

public class OutputFrameQueue {
    private static final String TAG = "OutputFrameQueue";

    private boolean mStopped = false;

    private class Element {
        public int index;
        public long frameIndex;
    }

    private Queue<Element> mQueue = new LinkedList<>();
    private Queue<Element> mUnusedList = new LinkedList<>();
    private MediaCodec mCodec;
    private FrameMap mFrameMap = new FrameMap();
    private final int mQueueSize = 1;
    private Element mRendering = null;
    private Element mAvailable = null;

    OutputFrameQueue() {
        for(int i = 0; i < mQueueSize; i++) {
            mUnusedList.add(new Element());
        }
    }

    public void setCodec(MediaCodec codec) {
        mCodec = codec;
    }

    public void pushInputBuffer(long presentationTimeUs, long frameIndex) {
        mFrameMap.put(presentationTimeUs, frameIndex);
    }

    synchronized public void pushOutputBuffer(int index, @NonNull MediaCodec.BufferInfo info) {
        long foundFrameIndex = mFrameMap.find(info.presentationTimeUs);

        if (foundFrameIndex < 0) {
            Log.e(TAG, "Ignore output buffer because unknown frameIndex. index=" + index);
            mCodec.releaseOutputBuffer(index, false);
            return;
        }

        Element elem = mUnusedList.poll();
        if(elem == null){
            Log.e(TAG, "FrameQueue is full. Discard old frame.");

            elem = mQueue.poll();
            mCodec.releaseOutputBuffer(elem.index, false);
        }
        elem.index = index;
        elem.frameIndex = foundFrameIndex;
        mQueue.add(elem);

        LatencyCollector.DecoderOutput(foundFrameIndex);
        Utils.frameLog(foundFrameIndex, "Current queue state=" + mQueue.size() + "/" + mQueueSize + " pushed index=" + index);
        notifyAll();
    }

    synchronized public long render(boolean render) {
        if(mStopped) {
            return -1;
        }
        if (mRendering != null || mAvailable != null) {
            // It will conflict with current rendering frame.
            // Defer processing until current frame is rendered.
            return -1;
        }
        Element elem = mQueue.poll();
        if(elem == null) {
            return -1;
        }

        mRendering = elem;
        mCodec.releaseOutputBuffer(elem.index, render);
        return elem.frameIndex;
    }

    synchronized public void onFrameAvailable() {
        if(mStopped) {
            return;
        }
        if(mRendering == null) {
            return;
        }
        if(mAvailable != null) {
            return;
        }
        mAvailable = mRendering;
        mRendering = null;
    }

    synchronized public long clearAvailable() {
        if(mStopped) {
            return -1;
        }
        if(mAvailable == null){
            return -1;
        }
        long frameIndex = mAvailable.frameIndex;
        mUnusedList.add(mAvailable);
        mAvailable = null;

        // Render deferred frame.
        render(true);

        return frameIndex;
    }

    synchronized public void stop() {
        mStopped = true;
        mQueue.clear();
        notifyAll();
    }

    synchronized public void reset() {
        mStopped = false;
        mQueue.clear();
        notifyAll();
    }
}
