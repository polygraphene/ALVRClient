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

    private static final int CODEC_H264 = 0;
    private static final int CODEC_H265 = 1;
    private int mCodec = CODEC_H265;

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

    private NAL mSPSBuffer = null;
    private NAL mPPSBuffer = null;

    private NAL mVPSBuffer = null;

    // Dummy SPS/PPS for some decoders which crashes on not set csd-0/csd-1. (e.g. Galaxy S6 Exynos decoder)
    byte[] DummySPS = new byte[]{ (byte)0x00, (byte)0x00, (byte)0x00, (byte)0x01, (byte)0x67, (byte)0x64, (byte)0x00, (byte)0x20, (byte)0xac, (byte)0x2b, (byte)0x40, (byte)0x20,
            0x02, (byte)0x0d, (byte)0x80, (byte)0x88, (byte)0x00, (byte)0x00, (byte)0x1f, (byte)0x40, (byte)0x00, (byte)0x0e, (byte)0xa6, (byte)0x04,
            0x7a, (byte)0x55};
    byte[] DummyPPS = new byte[]{ (byte)0x00, (byte)0x00, (byte)0x00, (byte)0x01, (byte)0x68, (byte)0xee, (byte)0x3c, (byte)0xb0};
    int DummyWidth = 1024;
    int DummyHeight = 512;

    byte[] DummyCSD_H265 = new byte[] {
            (byte)0x00, (byte)0x00, (byte)0x00, (byte)0x01, (byte)0x40, (byte)0x01, (byte)0x0c, (byte)0x01, (byte)0xff, (byte)0xff, (byte)0x21, (byte)0x40,
            (byte)0x00, (byte)0x00, (byte)0x03, (byte)0x00, (byte)0x00, (byte)0x03, (byte)0x00, (byte)0x00, (byte)0x03, (byte)0x00, (byte)0x00, (byte)0x03,
            (byte)0x00, (byte)0x78, (byte)0xac, (byte)0x09, (byte)0x00, (byte)0x00, (byte)0x00, (byte)0x01, (byte)0x42, (byte)0x01, (byte)0x01, (byte)0x21,
            (byte)0x40, (byte)0x00, (byte)0x00, (byte)0x03, (byte)0x00, (byte)0x00, (byte)0x03, (byte)0x00, (byte)0x00, (byte)0x03, (byte)0x00, (byte)0x00,
            (byte)0x03, (byte)0x00, (byte)0x78, (byte)0xa0, (byte)0x02, (byte)0x00, (byte)0x80, (byte)0x20, (byte)0x16, (byte)0x5a, (byte)0xd2, (byte)0x90,
            (byte)0x96, (byte)0x4b, (byte)0x8c, (byte)0x04, (byte)0x04, (byte)0x00, (byte)0x00, (byte)0x03, (byte)0x00, (byte)0x04, (byte)0x00, (byte)0x00,
            (byte)0x03, (byte)0x00, (byte)0xf0, (byte)0x20, (byte)0x00, (byte)0x00, (byte)0x00, (byte)0x01, (byte)0x44, (byte)0x01, (byte)0xc0, (byte)0xf7,
            (byte)0xc0, (byte)0xcc, (byte)0x90
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

    public void stopAndWait() {
        interrupt();
        while(isAlive()){
            try {
                join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
    }

    public void interrupt() {
        mStopped = true;
        mNalParser.notifyWaitingThread();
    }

    @Override
    public void run() {
        setName(DecoderThread.class.getName());

        try {
            if(mCodec == CODEC_H264) {
                decodeLoopH264();
            }else if(mCodec == CODEC_H265) {
                decodeLoopH265();
            }
        } catch (IOException | InterruptedException | IllegalStateException e) {
            e.printStackTrace();
            Log.v(TAG, "DecoderThread stopped by Exception.");
        } finally {
            if(mSPSBuffer != null) {
                mNalParser.recycleNal(mSPSBuffer);
                mSPSBuffer = null;
            }
            if(mPPSBuffer != null) {
                mNalParser.recycleNal(mPPSBuffer);
                mPPSBuffer = null;
            }
            if(mVPSBuffer != null) {
                mNalParser.recycleNal(mVPSBuffer);
                mVPSBuffer = null;
            }
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

    private void decodeLoopH264() throws InterruptedException, IOException {
        String videoFormat = "video/avc";
        MediaFormat format = MediaFormat.createVideoFormat(videoFormat, DummyWidth, DummyHeight);
        format.setString("KEY_MIME", videoFormat);
        format.setByteBuffer("csd-0", ByteBuffer.wrap(DummySPS, 0, DummySPS.length));
        format.setByteBuffer("csd-1", ByteBuffer.wrap(DummyPPS, 0, DummyPPS.length));

        mDecoder = MediaCodec.createDecoderByType(videoFormat);

        mDecoder.setVideoScalingMode(MediaCodec.VIDEO_SCALING_MODE_SCALE_TO_FIT);
        mDecoder.setCallback(new Callback());
        mDecoder.configure(format, mRenderCallback.getSurface(), null, 0);
        mDecoder.start();

        Log.v(TAG, "Codec created. Type=" + videoFormat + " Name=" + mDecoder.getCodecInfo().getName());

        // We don't need to wait IDR first time.
        mWaitNextIDR = true;

        while (!mStopped) {
            NAL nal = mNalParser.waitNal();
            if(nal == null) {
                break;
            }
            if (mStopped) {
                mNalParser.recycleNal(nal);
                break;
            }

            int NALType = nal.buf[4] & 0x1F;
            Utils.frameLog(nal.frameIndex,"Got NAL Type=" + NALType + " Length=" + nal.length + " QueueSize=" + mNalParser.getNalListSize());

            if (NALType == NAL_TYPE_SPS) {
                // SPS
                if(mSPSBuffer != null) {
                    mNalParser.recycleNal(mSPSBuffer);
                }
                mSPSBuffer = nal;
            } else if (NALType == NAL_TYPE_PPS) {
                // PPS
                if(mPPSBuffer != null) {
                    mNalParser.recycleNal(mPPSBuffer);
                }
                mPPSBuffer = nal;

                if (mWaitNextIDR && mSPSBuffer != null) {
                    frameLog("Feed codec config. SPS Size=" + mSPSBuffer.length + " PPS Size=" + mPPSBuffer.length);

                    mWaitNextIDR = false;

                    ByteBuffer buffer = getInputBuffer(nal);

                    buffer.put(mSPSBuffer.buf, 0, mSPSBuffer.length);
                    buffer.put(mPPSBuffer.buf, 0, mPPSBuffer.length);

                    sendInputBuffer(buffer, 0, MediaCodec.BUFFER_FLAG_CODEC_CONFIG);
                }
            } else if (NALType == NAL_TYPE_IDR) {
                // IDR-Frame
                Utils.frameLog(nal.frameIndex,"Feed IDR-Frame. Size=" + nal.length + " PresentationTime=" + nal.presentationTime);

                debugIDRFrame(nal, mSPSBuffer, mPPSBuffer);

                mLatencyCollector.DecoderInput(nal.frameIndex);

                pushFramePresentationMap(nal);

                ByteBuffer buffer = getInputBuffer(nal);
                buffer.put(nal.buf, 0, nal.length);
                sendInputBuffer(buffer, nal.presentationTime, 0);

                mNalParser.recycleNal(nal);
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
                    Utils.frameLog(nal.frameIndex,"Feed P-Frame. Size=" + nal.length + " PresentationTime=" + nal.presentationTime);

                    pushFramePresentationMap(nal);

                    ByteBuffer buffer = getInputBuffer(nal);
                    buffer.put(nal.buf, 0, nal.length);
                    sendInputBuffer(buffer, nal.presentationTime, 0);
                }
                mNalParser.recycleNal(nal);
            }
        }

    }

    private void decodeLoopH265() throws IOException, InterruptedException {
        String videoFormat = "video/hevc";
        MediaFormat format = MediaFormat.createVideoFormat(videoFormat, DummyWidth, DummyHeight);
        format.setString("KEY_MIME", videoFormat);
        format.setByteBuffer("csd-0", ByteBuffer.wrap(DummyCSD_H265, 0, DummyCSD_H265.length));

        mDecoder = MediaCodec.createDecoderByType(videoFormat);

        mDecoder.setVideoScalingMode(MediaCodec.VIDEO_SCALING_MODE_SCALE_TO_FIT);
        mDecoder.setCallback(new Callback());
        mDecoder.configure(format, mRenderCallback.getSurface(), null, 0);
        mDecoder.start();

        Log.v(TAG, "Codec created. Type=" + videoFormat + " Name=" + mDecoder.getCodecInfo().getName());

        // We don't need to wait IDR first time.
        mWaitNextIDR = true;

        while (!mStopped) {
            NAL nal = mNalParser.waitNal();
            if(nal == null) {
                break;
            }
            if (mStopped) {
                mNalParser.recycleNal(nal);
                break;
            }

            int NALType = (nal.buf[4] >> 1) & 0x3F;
            Utils.frameLog(nal.frameIndex,"Got NAL Type=" + NALType + " Length=" + nal.length + " QueueSize=" + mNalParser.getNalListSize());

            if (NALType == H265_NAL_TYPE_VPS) {
                // VPS + SPS + PPS
                if(mVPSBuffer != null) {
                    mNalParser.recycleNal(mVPSBuffer);
                }
                mVPSBuffer = nal;

                if (mWaitNextIDR && mVPSBuffer != null) {
                    frameLog("Feed codec config. VPS+SPS+PPS Size=" + mVPSBuffer.length);

                    mWaitNextIDR = false;

                    ByteBuffer buffer = getInputBuffer(nal);

                    buffer.put(mVPSBuffer.buf, 0, mVPSBuffer.length);

                    sendInputBuffer(buffer, 0, MediaCodec.BUFFER_FLAG_CODEC_CONFIG);
                }
            } else if (NALType == H265_NAL_TYPE_IDR_W_RADL) {
                // IDR-Frame
                Utils.frameLog(nal.frameIndex,"Feed IDR-Frame. Size=" + nal.length + " PresentationTime=" + nal.presentationTime);

                mLatencyCollector.DecoderInput(nal.frameIndex);

                pushFramePresentationMap(nal);

                ByteBuffer buffer = getInputBuffer(nal);
                buffer.put(nal.buf, 0, nal.length);
                sendInputBuffer(buffer, nal.presentationTime, 0);

                mNalParser.recycleNal(nal);
            } else {
                // PFrame
                mCounter.countPFrame();

                mLatencyCollector.DecoderInput(nal.frameIndex);

                if (mWaitNextIDR) {
                    // Ignore P-Frame until next I-Frame
                    Utils.frameLog(nal.frameIndex,"Ignoring P-Frame");
                } else {
                    // P-Frame
                    Utils.frameLog(nal.frameIndex,"Feed P-Frame. Size=" + nal.length + " PresentationTime=" + nal.presentationTime);

                    pushFramePresentationMap(nal);

                    ByteBuffer buffer = getInputBuffer(nal);
                    buffer.put(nal.buf, 0, nal.length);
                    sendInputBuffer(buffer, nal.presentationTime, 0);
                }
                mNalParser.recycleNal(nal);
            }
        }

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

    public void notifyCodecChange(int codec) {
        if(codec != mCodec) {
            stopAndWait();
            mCodec = codec;
            start();
        }
        mWaitNextIDR = true;
    }

}
