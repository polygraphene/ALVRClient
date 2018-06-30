package com.polygraphene.alvr;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.support.annotation.NonNull;
import android.util.Log;
import android.view.Surface;

import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.LinkedList;
import java.util.List;

class DecoderThread {
    private static final String TAG = "DecoderThread";

    private static final int CODEC_H264 = 0;
    private static final int CODEC_H265 = 1;
    private int mCodec = CODEC_H265;

    private static final String VIDEO_FORMAT_H264 = "video/avc";
    private static final String VIDEO_FORMAT_H265 = "video/hevc";
    private String mFormat = VIDEO_FORMAT_H265;

    private Thread mThread;
    private final MediaCodecInfo mCodecInfo;
    private NALParser mNalParser;
    private MediaCodec mDecoder = null;
    private int mQueuedOutputBuffer = -1;
    private StatisticsCounter mCounter;
    private LatencyCollector mLatencyCollector;

    private boolean mWaitNextIDR = false;
    private boolean mStopped = false;
    private boolean mIsFrameAvailable = false;

    @SuppressWarnings("unused")
    private MainActivity mMainActivity = null;

    private boolean mDebugIDRFrame = false;

    private int mBufferIndex = -1;

    private class FramePresentationTime {
        public long frameIndex;
        public long presentationTime;
        public long inputTime;
    }

    private final List<FramePresentationTime> mFrameBuf = new LinkedList<>();

    private static final int NAL_TYPE_SPS = 7;
    private static final int NAL_TYPE_PPS = 8;
    private static final int NAL_TYPE_IDR = 5;
    private static final int NAL_TYPE_P = 1;

    private static final int H265_NAL_TYPE_TRAIL_R = 1;
    private static final int H265_NAL_TYPE_IDR_W_RADL = 19;
    private static final int H265_NAL_TYPE_VPS = 32;
    private static final int H265_NAL_TYPE_SPS = 33;
    private static final int H265_NAL_TYPE_PPS = 34;

    // Dummy SPS/PPS for some decoders which crashes on not set csd-0/csd-1. (e.g. Galaxy S6 Exynos decoder)
    byte[] DummySPS = new byte[]{(byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x01, (byte) 0x67, (byte) 0x64, (byte) 0x00, (byte) 0x20, (byte) 0xac, (byte) 0x2b, (byte) 0x40, (byte) 0x20,
            0x02, (byte) 0x0d, (byte) 0x80, (byte) 0x88, (byte) 0x00, (byte) 0x00, (byte) 0x1f, (byte) 0x40, (byte) 0x00, (byte) 0x0e, (byte) 0xa6, (byte) 0x04,
            0x7a, (byte) 0x55};
    byte[] DummyPPS = new byte[]{(byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x01, (byte) 0x68, (byte) 0xee, (byte) 0x3c, (byte) 0xb0};
    int DummyWidth = 1024;
    int DummyHeight = 512;

    byte[] DummyCSD_H265 = new byte[]{
            (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x01, (byte) 0x40, (byte) 0x01, (byte) 0x0c, (byte) 0x01, (byte) 0xff, (byte) 0xff, (byte) 0x21, (byte) 0x40,
            (byte) 0x00, (byte) 0x00, (byte) 0x03, (byte) 0x00, (byte) 0x00, (byte) 0x03, (byte) 0x00, (byte) 0x00, (byte) 0x03, (byte) 0x00, (byte) 0x00, (byte) 0x03,
            (byte) 0x00, (byte) 0x78, (byte) 0xac, (byte) 0x09, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x01, (byte) 0x42, (byte) 0x01, (byte) 0x01, (byte) 0x21,
            (byte) 0x40, (byte) 0x00, (byte) 0x00, (byte) 0x03, (byte) 0x00, (byte) 0x00, (byte) 0x03, (byte) 0x00, (byte) 0x00, (byte) 0x03, (byte) 0x00, (byte) 0x00,
            (byte) 0x03, (byte) 0x00, (byte) 0x78, (byte) 0xa0, (byte) 0x02, (byte) 0x00, (byte) 0x80, (byte) 0x20, (byte) 0x16, (byte) 0x5a, (byte) 0xd2, (byte) 0x90,
            (byte) 0x96, (byte) 0x4b, (byte) 0x8c, (byte) 0x04, (byte) 0x04, (byte) 0x00, (byte) 0x00, (byte) 0x03, (byte) 0x00, (byte) 0x04, (byte) 0x00, (byte) 0x00,
            (byte) 0x03, (byte) 0x00, (byte) 0xf0, (byte) 0x20, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x01, (byte) 0x44, (byte) 0x01, (byte) 0xc0, (byte) 0xf7,
            (byte) 0xc0, (byte) 0xcc, (byte) 0x90
    };

    private final List<Integer> mAvailableInputs = new LinkedList<>();

    public interface RenderCallback {
        Surface getSurface();

        int renderIf(MediaCodec codec, int queuedOutputBuffer, long frameIndex);
    }

    private RenderCallback mRenderCallback;

    DecoderThread(NALParser nalParser, MediaCodecInfo codecInfo, StatisticsCounter counter
            , RenderCallback renderCallback, MainActivity mainActivity, LatencyCollector latencyCollector) {
        mNalParser = nalParser;
        mCodecInfo = codecInfo;
        mCounter = counter;
        mRenderCallback = renderCallback;
        mMainActivity = mainActivity;
        mLatencyCollector = latencyCollector;
    }

    private void frameLog(String s) {
        Log.v(TAG, s);
    }

    private void pushFramePresentationMap(NAL buf, long presentationTime) {
        FramePresentationTime f = new FramePresentationTime();
        f.frameIndex = buf.frameIndex;
        f.presentationTime = presentationTime;
        f.inputTime = System.nanoTime() / 1000;

        synchronized (mFrameBuf) {
            mFrameBuf.add(f);
            if (mFrameBuf.size() > 100) {
                mFrameBuf.remove(0);
            }
        }
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

    public void start() {
        mThread = new MyThread();
        mStopped = false;
        synchronized (mAvailableInputs) {
            mAvailableInputs.clear();
        }
        mThread.start();
    }

    public void interrupt() {
        mStopped = true;
        mNalParser.notifyWaitingThread();
    }

    public void run() {
        mThread.setName(DecoderThread.class.getName());

        try {
            decodeLoop();
        } catch (IOException | InterruptedException | IllegalStateException e) {
            e.printStackTrace();
            Log.v(TAG, "DecoderThread stopped by Exception.");
        } finally {
            Log.v(TAG, "Stopping decoder.");
            if (mDecoder != null) {
                try {
                    mDecoder.stop();
                    mDecoder.release();
                } catch (IllegalStateException e) {
                }
            }
        }
        Log.v(TAG, "DecoderThread stopped.");
    }

    private void decodeLoop() throws InterruptedException, IOException {
        MediaFormat format = MediaFormat.createVideoFormat(mFormat, DummyWidth, DummyHeight);
        format.setString("KEY_MIME", mFormat);
        if (mCodec == CODEC_H264) {
            format.setByteBuffer("csd-0", ByteBuffer.wrap(DummySPS, 0, DummySPS.length));
            format.setByteBuffer("csd-1", ByteBuffer.wrap(DummyPPS, 0, DummyPPS.length));
        } else {
            format.setByteBuffer("csd-0", ByteBuffer.wrap(DummyCSD_H265, 0, DummyCSD_H265.length));
        }

        mDecoder = MediaCodec.createDecoderByType(mFormat);

        mDecoder.setVideoScalingMode(MediaCodec.VIDEO_SCALING_MODE_SCALE_TO_FIT);
        mDecoder.setCallback(new Callback());
        mDecoder.configure(format, mRenderCallback.getSurface(), null, 0);
        mDecoder.start();

        Log.v(TAG, "Codec created. Type=" + mFormat + " Name=" + mDecoder.getCodecInfo().getName());

        mWaitNextIDR = true;

        while (!mStopped) {
            NAL nal = mNalParser.waitNal();
            if (nal == null) {
                Log.v(TAG, "decodeLoop Stopped. nal==null.");
                break;
            }
            if (mStopped) {
                Log.v(TAG, "decodeLoop Stopped. mStopped==true.");
                mNalParser.recycleNal(nal);
                break;
            }

            int NALType;

            if (mCodec == CODEC_H264) {
                NALType = nal.buf[4] & 0x1F;
            } else {
                NALType = (nal.buf[4] >> 1) & 0x3F;
            }
            Utils.frameLog(nal.frameIndex, "Got NAL Type=" + NALType + " Length=" + nal.length + " QueueSize=" + mNalParser.getNalListSize());

            long presentationTime = System.nanoTime() / 1000;

            if (mCodec == CODEC_H264 && NALType == NAL_TYPE_SPS) {
                // SPS + PPS
                if (mWaitNextIDR) {
                    Utils.frameLog(nal.frameIndex, "Feed codec config. SPS+PPS Size=" + nal.length);

                    mWaitNextIDR = false;

                    sendInputBuffer(nal, 0, MediaCodec.BUFFER_FLAG_CODEC_CONFIG);
                }
                mNalParser.recycleNal(nal);
            } else if (mCodec == CODEC_H264 && NALType == NAL_TYPE_IDR) {
                // IDR-Frame
                Utils.frameLog(nal.frameIndex, "Feed IDR-Frame. Size=" + nal.length + " PresentationTime=" + presentationTime);

                //debugIDRFrame(nal, mSPSBuffer, mPPSBuffer);

                mLatencyCollector.DecoderInput(nal.frameIndex);

                sendInputBuffer(nal, presentationTime, 0);

                mNalParser.recycleNal(nal);
            } else if (mCodec == CODEC_H265 && NALType == H265_NAL_TYPE_VPS) {
                // VPS + SPS + PPS
                if (mWaitNextIDR) {
                    frameLog("Feed codec config. VPS+SPS+PPS Size=" + nal.length);

                    mWaitNextIDR = false;

                    sendInputBuffer(nal, 0, MediaCodec.BUFFER_FLAG_CODEC_CONFIG);
                }
                mNalParser.recycleNal(nal);
            } else if (mCodec == CODEC_H265 && NALType == H265_NAL_TYPE_IDR_W_RADL) {
                // IDR-Frame
                Utils.frameLog(nal.frameIndex, "Feed IDR-Frame. Size=" + nal.length + " PresentationTime=" + presentationTime);

                mLatencyCollector.DecoderInput(nal.frameIndex);

                sendInputBuffer(nal, presentationTime, 0);

                mNalParser.recycleNal(nal);
            } else {
                // PFrame
                mCounter.countPFrame();

                mLatencyCollector.DecoderInput(nal.frameIndex);

                if (mWaitNextIDR) {
                    // Ignore P-Frame until next I-Frame
                    Utils.frameLog(nal.frameIndex, "Ignoring P-Frame");
                } else {
                    // P-Frame
                    Utils.frameLog(nal.frameIndex, "Feed P-Frame. Size=" + nal.length + " PresentationTime=" + presentationTime);

                    sendInputBuffer(nal, presentationTime, 0);
                }
                mNalParser.recycleNal(nal);
            }
        }

    }

    // Output IDR frame in external media dir for debugging. (/sdcard/Android/media/...)
    private void debugIDRFrame(NAL buf, NAL spsBuffer, NAL ppsBuffer) {
        if (spsBuffer == null || ppsBuffer == null) {
            return;
        }
        if (mDebugIDRFrame) {
            try {
                String path = mMainActivity.getExternalMediaDirs()[0].getAbsolutePath() + "/" + buf.frameIndex + ".h264";
                FileOutputStream stream = new FileOutputStream(path);
                stream.write(spsBuffer.buf, 0, spsBuffer.length);
                stream.write(ppsBuffer.buf, 0, ppsBuffer.length);
                stream.write(buf.buf, 0, buf.length);
                stream.close();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }

    private ByteBuffer getInputBuffer(NAL nal) throws InterruptedException {
        Utils.frameLog(nal.frameIndex, "Wait next input buffer.");
        while (true) {
            synchronized (mAvailableInputs) {
                if (mAvailableInputs.size() > 0) {
                    mBufferIndex = mAvailableInputs.get(0);
                    mAvailableInputs.remove(0);
                    break;
                }
                mAvailableInputs.wait();
            }
        }
        Utils.frameLog(nal.frameIndex, "Uses input index=" + mBufferIndex + " NAL QueueSize=" + mNalParser.getNalListSize());
        return mDecoder.getInputBuffer(mBufferIndex);
    }

    private void sendInputBuffer(NAL nal, long presentationTimeUs, int flags) throws InterruptedException {
        if(presentationTimeUs != 0) {
            pushFramePresentationMap(nal, presentationTimeUs);
        }

        ByteBuffer buffer = getInputBuffer(nal);
        buffer.put(nal.buf, 0, nal.length);
        mDecoder.queueInputBuffer(mBufferIndex, 0, buffer.position(), presentationTimeUs, flags);
        mBufferIndex = -1;
    }

    class Callback extends MediaCodec.Callback {
        @Override
        public void onInputBufferAvailable(@NonNull MediaCodec codec, int index) {
            //frameLog("onInputBufferAvailable " + index + " " + mBuf);

            synchronized (mAvailableInputs) {
                mAvailableInputs.add(index);
                mAvailableInputs.notifyAll();
            }
        }

        @Override
        public void onOutputBufferAvailable(@NonNull MediaCodec codec, int index, @NonNull MediaCodec.BufferInfo info) {
            mCounter.countOutputFrame(1);

            mIsFrameAvailable = true;

            if (mQueuedOutputBuffer != -1) {
                mDecoder.releaseOutputBuffer(mQueuedOutputBuffer, false);
                mQueuedOutputBuffer = -1;
            }
            mQueuedOutputBuffer = index;
            long foundFrameIndex = 0;
            long inputTime = 0;

            synchronized (mFrameBuf) {
                for (FramePresentationTime f : mFrameBuf) {
                    if (f.presentationTime == info.presentationTimeUs) {
                        foundFrameIndex = f.frameIndex;
                        inputTime = f.inputTime;
                        break;
                    }
                }
            }
            mLatencyCollector.DecoderOutput(foundFrameIndex);

            long decodeLatency = System.nanoTime() / 1000 - inputTime;
            Utils.frameLog(foundFrameIndex, "Render frame " + " presentationTimeUs:" + info.presentationTimeUs + " decodeLatency=" + decodeLatency + " us");

            mQueuedOutputBuffer = mRenderCallback.renderIf(mDecoder, mQueuedOutputBuffer, foundFrameIndex);
            if (mQueuedOutputBuffer == -1) {
                //frameLog("consumed");
            } else {
                frameLog("not ready. discard.");
                mDecoder.releaseOutputBuffer(mQueuedOutputBuffer, false);
                mQueuedOutputBuffer = -1;
            }
        }

        @Override
        public void onError(@NonNull MediaCodec codec, @NonNull MediaCodec.CodecException e) {
            Log.e("DecodeActivity", "Codec Error: " + e.getMessage() + "\n" + e.getDiagnosticInfo());
        }

        @Override
        public void onOutputFormatChanged(@NonNull MediaCodec codec, @NonNull MediaFormat format) {
            Log.d("DecodeActivity", "New format " + mDecoder.getOutputFormat());
        }
    }

    public boolean isFrameAvailable() {
        return mIsFrameAvailable;
    }

    public void notifyCodecChange(int codec) {
        if (codec != mCodec) {
            stopAndWait();
            mCodec = codec;
            if (mCodec == CODEC_H264) {
                mFormat = VIDEO_FORMAT_H264;
            } else {
                mFormat = VIDEO_FORMAT_H265;
            }
            mNalParser.clearStopped();
            start();
        } else {
            mWaitNextIDR = true;
        }
    }

    private class MyThread extends Thread {
        @Override
        public void run() {
            DecoderThread.this.run();
        }
    }
}
