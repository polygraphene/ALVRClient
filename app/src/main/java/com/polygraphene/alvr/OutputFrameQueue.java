package com.polygraphene.alvr;

import android.media.MediaCodec;
import android.support.annotation.NonNull;
import android.util.Log;

import java.util.LinkedList;

public class OutputFrameQueue {
    private static final String TAG = "OutputFrameQueue";

    private boolean mStopped = false;

    private class Element {
        public int index;
        public long frameIndex;
    }

    private LinkedList<Element> mQueue = new LinkedList<>();
    private MediaCodec mCodec;
    private FrameMap mFrameMap = new FrameMap();
    private boolean mFrameAvailable;
    private int mQueueSize = 1;

    OutputFrameQueue() {
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

        Element elem = new Element();
        elem.index = index;
        elem.frameIndex = foundFrameIndex;
        mQueue.add(elem);

        LatencyCollector.DecoderOutput(foundFrameIndex);

        // Start threshold is half of mQueueSize
        if (mQueue.size() >= (mQueueSize + 1) / 2 && !mFrameAvailable) {
            mFrameAvailable = true;
            Log.e(TAG, "Start playing.");
        }
        if (mQueue.size() > mQueueSize) {
            Log.e(TAG, "FrameQueue is full. Discard all frame.");
            while (mQueue.size() >= 2) {
                Element removeElement = mQueue.removeFirst();
                mCodec.releaseOutputBuffer(removeElement.index, false);
            }
        }
        Utils.frameLog(foundFrameIndex, "Current queue state=" + mQueue.size() + "/" + mQueueSize + " pushed index=" + index);
        notifyAll();
    }

    synchronized public long render(int waitMs) {
        if (mStopped) {
            return -1;
        }
        if (mQueue.size() == 0) {
            try {
                wait(waitMs);
            } catch (InterruptedException e) {
            }
        }
        if (mStopped || mQueue.size() == 0) {
            return -1;
        }
        Element element = mQueue.removeFirst();
        Utils.frameLog(element.frameIndex, "Got frame from queue");
        mCodec.releaseOutputBuffer(element.index, true);
        return element.frameIndex;
    }

    synchronized public boolean isFrameAvailable() {
        return mFrameAvailable;
    }

    synchronized public void stop() {
        mStopped = true;
        mQueue.clear();
        mFrameAvailable = false;
        notifyAll();
    }

    synchronized public void reset() {
        mStopped = false;
        mQueue.clear();
        mFrameAvailable = false;
        notifyAll();
    }

    synchronized public void setQueueSize(int queueSize) {
        Log.e(TAG, "Queue size was changed. Size=" + queueSize);
        mQueueSize = queueSize;
    }
}
