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

class DecoderThread extends Thread {
    private static final String TAG = "DecoderThread";

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

    private NAL mSPSBuffer = null;
    private NAL mPPSBuffer = null;

    // Dummy SPS/PPS for some decoders which crashes on not set csd-0/csd-1. (e.g. Galaxy S6 Exynos decoder)
    byte[] DummySPS = new byte[]{ (byte)0x00, (byte)0x00, (byte)0x00, (byte)0x01, (byte)0x67, (byte)0x64, (byte)0x00, (byte)0x20, (byte)0xac, (byte)0x2b, (byte)0x40, (byte)0x20,
            0x02, (byte)0x0d, (byte)0x80, (byte)0x88, (byte)0x00, (byte)0x00, (byte)0x1f, (byte)0x40, (byte)0x00, (byte)0x0e, (byte)0xa6, (byte)0x04,
            0x7a, (byte)0x55};
    byte[] DummyPPS = new byte[]{ (byte)0x00, (byte)0x00, (byte)0x00, (byte)0x01, (byte)0x68, (byte)0xee, (byte)0x3c, (byte)0xb0};

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

    private void pushFramePresentationMap(NAL buf) {
        FramePresentationTime f = new FramePresentationTime();
        f.frameIndex = buf.frameIndex;
        f.presentationTime = buf.presentationTime;
        f.inputTime = System.nanoTime() / 1000;

        synchronized (mFrameBuf) {
            mFrameBuf.add(f);
            if (mFrameBuf.size() > 100) {
                mFrameBuf.remove(0);
            }
        }
    }

    public void interrupt(){
        mStopped = true;
        mNalParser.notifyWaitingThread();
    }

    @Override
    public void run() {
        setName(DecoderThread.class.getName());

        try {
            String videoFormat = "video/avc";
            MediaFormat format = MediaFormat.createVideoFormat(videoFormat, 0, 0);
            format.setString("KEY_MIME", videoFormat);
            format.setByteBuffer("csd-0", ByteBuffer.wrap(DummySPS, 0, DummySPS.length));
            format.setByteBuffer("csd-1", ByteBuffer.wrap(DummyPPS, 0, DummyPPS.length));

            String codecName = mCodecInfo.getName();
            Log.v(TAG, "Create codec " + codecName);
            mDecoder = MediaCodec.createByCodecName(codecName);

            mDecoder.setVideoScalingMode(MediaCodec.VIDEO_SCALING_MODE_SCALE_TO_FIT);
            mDecoder.setCallback(new Callback());
            mDecoder.configure(format, mRenderCallback.getSurface(), null, 0);
            mDecoder.start();

            // We don't need to wait IDR first time.
            mWaitNextIDR = true;

            while (!mStopped) {
                NAL nal = mNalParser.waitNal();
                if (mStopped || nal == null) {
                    break;
                }

                int NALType = nal.buf[4] & 0x1F;
                Utils.frameLog(nal.frameIndex,"Got NAL Type=" + NALType + " Length=" + nal.len + " QueueSize=" + mNalParser.getNalListSize());

                if (NALType == NAL_TYPE_SPS) {
                    // SPS
                    mSPSBuffer = nal;
                } else if (NALType == NAL_TYPE_PPS) {
                    // PPS
                    mPPSBuffer = nal;

                    if (mWaitNextIDR && mSPSBuffer != null) {
                        frameLog("Feed codec config. SPS Size=" + mSPSBuffer.len + " PPS Size=" + mPPSBuffer.len);

                        mWaitNextIDR = false;

                        ByteBuffer buffer = getInputBuffer(nal);

                        buffer.put(mSPSBuffer.buf, 0, mSPSBuffer.len);
                        buffer.put(mPPSBuffer.buf, 0, mPPSBuffer.len);

                        sendInputBuffer(buffer, 0, MediaCodec.BUFFER_FLAG_CODEC_CONFIG);
                    }
                } else if (NALType == NAL_TYPE_IDR) {
                    // IDR-Frame
                    Utils.frameLog(nal.frameIndex,"Feed IDR-Frame. Size=" + nal.len + " PresentationTime=" + nal.presentationTime);

                    debugIDRFrame(nal, mSPSBuffer, mPPSBuffer);

                    mLatencyCollector.DecoderInput(nal.frameIndex);

                    pushFramePresentationMap(nal);

                    ByteBuffer buffer = getInputBuffer(nal);
                    buffer.put(nal.buf, 0, nal.len);
                    sendInputBuffer(buffer, nal.presentationTime, 0);
                } else {
                    if (NALType == NAL_TYPE_P) {
                        // PFrame
                        mCounter.countPFrame();
                    }

                    mLatencyCollector.DecoderInput(nal.frameIndex);

                    if (mWaitNextIDR) {
                        // Ignore P-Frame until next I-Frame
                        Utils.frameLog(nal.frameIndex,"Ignoring P-Frame");
                    } else {
                        // P-Frame
                        Utils.frameLog(nal.frameIndex,"Feed P-Frame. Size=" + nal.len + " PresentationTime=" + nal.presentationTime);

                        pushFramePresentationMap(nal);

                        ByteBuffer buffer = getInputBuffer(nal);
                        buffer.put(nal.buf, 0, nal.len);
                        sendInputBuffer(buffer, nal.presentationTime, 0);
                    }
                }
            }

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

    // Output IDR frame in external media dir for debugging. (/sdcard/Android/media/...)
    private void debugIDRFrame(NAL buf, NAL spsBuffer, NAL ppsBuffer) {
        if(spsBuffer == null || ppsBuffer == null) {
            return;
        }
        if(mDebugIDRFrame) {
            try {
                String path = mMainActivity.getExternalMediaDirs()[0].getAbsolutePath() + "/" + buf.frameIndex + ".h264";
                FileOutputStream stream = new FileOutputStream(path);
                stream.write(spsBuffer.buf, 0, spsBuffer.len);
                stream.write(ppsBuffer.buf, 0, ppsBuffer.len);
                stream.write(buf.buf, 0, buf.len);
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

    private void sendInputBuffer(ByteBuffer buffer, long presentationTimeUs, int flags) {
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

    public void notifyGeometryChange() {
        mWaitNextIDR = true;
    }

}
