package com.polygraphene.alvr;

import android.util.LongSparseArray;

import java.util.LinkedList;
import java.util.List;

// Stores mapping of presentationTime to frameIndex for tracking frameIndex on decoding.
public class FrameMap {
    private static final int MAX_FRAMES = 50;

    private List<Long> mFrameHistory = new LinkedList<>();
    private LongSparseArray<Long> mFrameHashMap = new LongSparseArray<>();

    public synchronized void put(long presentationTime, long frameIndex) {
        mFrameHistory.add(presentationTime);
        mFrameHashMap.put(presentationTime, frameIndex);
        if (mFrameHistory.size() > MAX_FRAMES) {
            Long key = mFrameHistory.remove(0);
            mFrameHashMap.remove(key);
        }
    }

    public synchronized long find(long presentationTime) {
        Long f = mFrameHashMap.get(presentationTime);
        return f != null ? f : -1;
    }
}
