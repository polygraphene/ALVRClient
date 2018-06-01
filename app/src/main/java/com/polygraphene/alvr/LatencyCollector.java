package com.polygraphene.alvr;

import android.util.Log;

import java.util.LinkedList;
import java.util.TreeMap;

public class LatencyCollector {
    private final static String TAG = "LatencyCollector";

    class FrameTimestamp {
        // Timestamp in microsec.
        public long tracking;
        public long estimatedSent;
        public long receivedFirst;
        public long receivedLast;
        public long decoderInput;
        public long decoderOutput;
        public long rendered1;
        public long rendered2;
        public long submit;
    }
    private TreeMap<Long, FrameTimestamp> mFrameList = new TreeMap<>();
    public static final int MAX_FRAMES = 1000;

    private long mStatisticsTime;
    private long mPacketsLostTotal = 0;
    private long mPacketsLostInSecond = 0;

    // Total/Transport/Decode latency
    // Total/Max/Min/Count
    private long[][] mLatency = new long[3][4];

    private long[][] mPreviousLatency = new long[3][4];

    public LatencyCollector(){
        mStatisticsTime = currentSec();
    }

    private long currentSec(){
        return System.nanoTime() / (1000 * 1000 * 1000);
    }

    private long current(){
        return System.nanoTime() / 1000;
    }

    private FrameTimestamp newFrameTimestamp(){
        if(mFrameList.size() == MAX_FRAMES) {
            FrameTimestamp timestamp = mFrameList.get(mFrameList.firstKey());
            mFrameList.remove(mFrameList.firstKey());
            timestamp.tracking = timestamp.estimatedSent = timestamp.receivedFirst = timestamp.receivedLast
                    = timestamp.decoderInput = timestamp.decoderOutput = timestamp.rendered1 = timestamp.rendered2
                    = timestamp.submit = 0;
            return timestamp;
        }
        return new FrameTimestamp();
    }
    private FrameTimestamp getFrame(long frameIndex){
        FrameTimestamp timestamp = mFrameList.get(frameIndex);
        if(timestamp == null) {
            timestamp = newFrameTimestamp();
            mFrameList.put(frameIndex, timestamp);
        }
        return timestamp;
    }

    synchronized public void Tracking(long frameIndex) {
        getFrame(frameIndex).tracking = current();
    }
    synchronized public void EstimatedSent(long frameIndex, long time) {
        getFrame(frameIndex).estimatedSent = time;
    }
    synchronized public void ReceivedFirst(long frameIndex) {
        getFrame(frameIndex).receivedFirst = current();
    }
    synchronized public void ReceivedLast(long frameIndex) {
        getFrame(frameIndex).receivedLast = current();
    }
    synchronized public void DecoderInput(long frameIndex) {
        getFrame(frameIndex).decoderInput = current();
    }
    synchronized public void DecoderOutput(long frameIndex) {
        getFrame(frameIndex).decoderOutput = current();
    }
    synchronized public void Rendered1(long frameIndex) {
        getFrame(frameIndex).rendered1 = current();
    }
    synchronized public void Rendered2(long frameIndex) {
        getFrame(frameIndex).rendered2 = current();
    }
    synchronized public void Submit(long frameIndex) {
        FrameTimestamp timestamp = getFrame(frameIndex);
        timestamp.submit = current();

        long[] latency = new long[3];
        latency[0] = timestamp.submit - timestamp.tracking;
        latency[1] = timestamp.receivedLast - timestamp.estimatedSent;
        latency[2] = timestamp.decoderOutput - timestamp.decoderInput;

        Latency(latency);

        Utils.frameLog(frameIndex, "totalLatency=" + latency[0] + " transportLatency=" + latency[1] + " decodeLatency=" + latency[2] + " (" + timestamp.decoderInput + " " + timestamp.decoderOutput + ")");
    }

    synchronized public void ResetAll() {
        mPacketsLostTotal = 0;
        mPacketsLostInSecond = 0;
        mStatisticsTime = currentSec();

        for(int i = 0; i < 3; i++) {
            for(int j = 0; j < 4; j++) {
                mLatency[i][j] = 0;
                mPreviousLatency[i][j] = 0;
            }
        }
    }

    synchronized public void ResetSecond(){
        long[][] tmp = mPreviousLatency;
        mPreviousLatency = mLatency;
        mLatency = tmp;

        for(int i = 0; i < 3; i++) {
            for(int j = 0; j < 4; j++) {
                mLatency[i][j] = 0;
            }
        }

        mPacketsLostInSecond = 0;
    }

    synchronized public void PacketLoss(long count) {
        long current = currentSec();
        if(mStatisticsTime != current){
            mStatisticsTime = current;
            ResetSecond();
        }
        mPacketsLostTotal += count;
        mPacketsLostInSecond += count;
    }

    synchronized public void Latency(long[] latency) {
        long current = currentSec();
        if(mStatisticsTime != current){
            mStatisticsTime = current;
            ResetSecond();
        }
        for(int i = 0; i < 3; i++) {
            mLatency[i][0] += latency[i];
            mLatency[i][1] = Math.max(mLatency[i][1], latency[i]);
            mLatency[i][2] = Math.min(mLatency[i][2], latency[i]);
            mLatency[i][3]++;
        }
    }

    synchronized public long GetPacketsLostTotal(){
        return mPacketsLostTotal;
    }

    synchronized public long GetPacketsLostInSecond(){
        return mPacketsLostInSecond;
    }

    synchronized public long GetLatency(int i, int j){
        if(j == 1 || j == 2) {
            return mPreviousLatency[i][j];
        }
        if(mPreviousLatency[i][3] == 0) {
            return 0;
        }
        return mPreviousLatency[i][0] / mPreviousLatency[i][3];
    }
}
