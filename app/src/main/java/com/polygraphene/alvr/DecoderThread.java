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

    private NAL mBuf = null;

    private boolean mWaitNextIDR = false;
    private long mFrameNumber = 0;
    private boolean mStopped = false;

    @SuppressWarnings("unused")
    private MainActivity mMainActivity = null;

    private boolean mDebugIDRFrame = false;

    private class FramePresentationTime {
        public long frameIndex;
        public long presentationTime;
        public long inputTime;
    }

    private final List<FramePresentationTime> mFrameBuf = new LinkedList<>();

    private NAL mSPSBuffer = null;
    private NAL mPPSBuffer = null;

    private List<Integer> mAvailableInputs = new LinkedList<>();

    public interface RenderCallback {
        Surface getSurface();

        int renderIf(MediaCodec codec, int queuedOutputBuffer, long frameIndex);
    }

    private RenderCallback mRenderCallback;

    DecoderThread(NALParser nalParser, MediaCodecInfo codecInfo, StatisticsCounter counter, RenderCallback renderCallback, MainActivity mainActivity) {
        mNalParser = nalParser;
        mCodecInfo = codecInfo;
        mCounter = counter;
        mRenderCallback = renderCallback;
        mMainActivity = mainActivity;
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
            while (!mStopped) {
                NAL packet = mNalParser.getNal();
                if (packet == null) {
                    Thread.sleep(10);
                    continue;
                }

                int NALType = packet.buf[4] & 0x1F;

                if (NALType == 7) {
                    mSPSBuffer = packet;
                } else if (NALType == 8) {
                    mPPSBuffer = packet;
                    if (mSPSBuffer != null)
                        break;
                }
            }
            if (mStopped) {
                return;
            }

            int nalQueueMax = 10;

            int width = mNalParser.getWidth();
            int height = mNalParser.getHeight();
            Log.v(TAG, "Video geometry is " + width + "x" + height);

            String videoFormat = "video/avc";
            MediaFormat format = MediaFormat.createVideoFormat(videoFormat, width, height);
            format.setString("KEY_MIME", videoFormat);
            format.setByteBuffer("csd-0", ByteBuffer.wrap(mSPSBuffer.buf, 0, mSPSBuffer.len));
            format.setByteBuffer("csd-1", ByteBuffer.wrap(mPPSBuffer.buf, 0, mPPSBuffer.len));

            String codecName = mCodecInfo.getName();
            Log.v(TAG, "Create codec " + codecName);
            mDecoder = MediaCodec.createByCodecName(codecName);

            mDecoder.setVideoScalingMode(MediaCodec.VIDEO_SCALING_MODE_SCALE_TO_FIT);
            mDecoder.setCallback(new Callback());
            mDecoder.configure(format, mRenderCallback.getSurface(), null, 0);
            mDecoder.start();

            while (!mStopped) {
                frameLog("Waiting NALU");
                mBuf = mNalParser.waitNal();
                if (mStopped) {
                    break;
                }

                int NALType = mBuf.buf[4] & 0x1F;
                frameLog("Got NAL TYPE " + NALType + " Len " + mBuf.len + "  q:" + mNalParser.getNalListSize());

                if (mFrameNumber > 500) {
                    //return;
                }
                int index;
                while (true) {
                    synchronized (mAvailableInputs) {
                        if (mAvailableInputs.size() > 0) {
                            index = mAvailableInputs.get(0);
                            mAvailableInputs.remove(0);
                            break;
                        }
                        mAvailableInputs.wait();
                    }
                }
                frameLog("Uses input index=" + index + " NAL Queue Size=" + mNalParser.getNalListSize() + " (Max:" + nalQueueMax + (mNalParser.getNalListSize() > nalQueueMax ? " discard)" : ")"));

                ByteBuffer buffer = mDecoder.getInputBuffer(index);

                if (mNalParser.getNalListSize() > nalQueueMax) {
                    mNalParser.flushNALList();
                }

                if (NALType == 7) {
                    // SPS
                    mSPSBuffer = mBuf;

                    mBuf = null;
                    mDecoder.queueInputBuffer(index, 0, 0, 0, 0);
                } else if (NALType == 8) {
                    // PPS
                    mPPSBuffer = mBuf;

                    mBuf = null;
                    if (mWaitNextIDR) {
                        if (mSPSBuffer != null)
                            buffer.put(mSPSBuffer.buf, 0, mSPSBuffer.len);
                        buffer.put(mBuf.buf, 0, mBuf.len);

                        mWaitNextIDR = false;
                        frameLog("Sending Codec Config. Size: " + buffer.position());
                        mDecoder.queueInputBuffer(index, 0, buffer.position(), 0, MediaCodec.BUFFER_FLAG_CODEC_CONFIG);
                    } else {
                        mDecoder.queueInputBuffer(index, 0, buffer.position(), 0, 0);
                    }
                } else if (NALType == 5) {
                    // IDR
                    frameLog("Feed IDR SPS:" + mSPSBuffer.len + " PPS:" + mPPSBuffer.len + " IDR:" + mBuf.len);

                    buffer.put(mBuf.buf, 0, mBuf.len);
                    mFrameNumber++;

                    debugIDRFrame(mBuf, mSPSBuffer, mPPSBuffer);

                    pushFramePresentationMap(mBuf);

                    mDecoder.queueInputBuffer(index, 0, buffer.position(), mBuf.presentationTime, 0);
                    mBuf = null;
                    //mDecoder.queueInputBuffer(inIndex, 0, buffer.position(), startTimestamp, 0);
                } else {
                    buffer.put(mBuf.buf, 0, mBuf.len);

                    if (NALType == 1) {
                        // PFrame
                        mCounter.countPFrame();
                    }
                    mFrameNumber++;

                    if (mWaitNextIDR) {
                        // Ignore P-Frame until next I-Frame
                        frameLog("Ignoring P-Frame");
                        mDecoder.queueInputBuffer(index, 0, 0, mBuf.presentationTime, 0);
                        mBuf = null;
                    } else {
                        pushFramePresentationMap(mBuf);

                        frameLog("Feed P-Frame " + buffer.position() + " bytes NALType=" + String.format("%02X", NALType) + " FrameNumber=" + mFrameNumber + " FrameIndex=" + mBuf.frameIndex + " PresentationTime=" + mBuf.presentationTime);
                        mDecoder.queueInputBuffer(index, 0, buffer.position(), mBuf.presentationTime, 0);
                        mBuf = null;
                    }
                }
            }

        } catch (IOException | InterruptedException | IllegalStateException e) {
            e.printStackTrace();
            Log.v(TAG, "DecoderThread stopped by Exception.");
        } finally {
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

    class Callback extends MediaCodec.Callback {
        @Override
        public void onInputBufferAvailable(@NonNull MediaCodec codec, int index) {
            frameLog("onInputBufferAvailable " + index + " " + mBuf);

            synchronized (mAvailableInputs) {
                mAvailableInputs.add(index);
                mAvailableInputs.notifyAll();
            }
        }

        @Override
        public void onOutputBufferAvailable(@NonNull MediaCodec codec, int index, @NonNull MediaCodec.BufferInfo info) {
            mCounter.countOutputFrame(1);

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

            long decodeLatency = System.nanoTime() / 1000 - inputTime;
            frameLog("Render frame " + "fr:" + foundFrameIndex + " pres:" + info.presentationTimeUs + " decode latency=" + decodeLatency + " us");

            mQueuedOutputBuffer = mRenderCallback.renderIf(mDecoder, mQueuedOutputBuffer, foundFrameIndex);
            if (mQueuedOutputBuffer == -1) {
                frameLog("consumed");
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
}
