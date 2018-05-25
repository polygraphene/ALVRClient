package com.polygraphene.alvr;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.support.annotation.NonNull;
import android.util.Log;
import android.view.Surface;

import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.LinkedList;
import java.util.List;

class DecoderThread extends Thread {
    private static final String TAG = "DecoderThread";
    private final MediaCodecInfo mCodecInfo;
    private NALParser mNalParser;
    private MediaCodec decoder = null;
    private int queuedOutputBuffer = -1;
    private StatisticsCounter mCounter;

    private NAL buf = null;

    private boolean waitNextIDR = false;
    private long consecutiveStalls = 0;
    private long frameNumber = 0;
    private boolean mStopped = false;

    private long foundFrameIndex = 0;

    private MainActivity mMainActivity = null;

    private class FramePresentationTime {
        public long frameIndex;
        public long presentationTime;
        public long inputTime;
    }

    private final List<FramePresentationTime> frameBuf = new LinkedList<>();

    private NAL IDRBuffer = null;
    private NAL SPSBuffer = null;
    private NAL PPSBuffer = null;

    private List<Integer> availableInputs = new LinkedList<>();

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

        synchronized (frameBuf) {
            frameBuf.add(f);
            if (frameBuf.size() > 100) {
                frameBuf.remove(0);
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
                    SPSBuffer = packet;
                } else if (NALType == 8) {
                    PPSBuffer = packet;
                    if (SPSBuffer != null)
                        break;
                }
            }
            if (mStopped) {
                return;
            }

            int nalQueueMax = 10;

            // TODO: Parse SPS to get geometry
            int width = 2048;
            int height = 1024;

            String videoFormat = "video/avc";
            MediaFormat format = MediaFormat.createVideoFormat(videoFormat, width, height);
            format.setString("KEY_MIME", videoFormat);
            format.setByteBuffer("csd-0", ByteBuffer.wrap(SPSBuffer.buf, 0, SPSBuffer.len));
            format.setByteBuffer("csd-1", ByteBuffer.wrap(PPSBuffer.buf, 0, PPSBuffer.len));

            String codecName = mCodecInfo.getName();
            Log.v(TAG, "Create codec " + codecName);
            decoder = MediaCodec.createByCodecName(codecName);

            decoder.setVideoScalingMode(MediaCodec.VIDEO_SCALING_MODE_SCALE_TO_FIT);
            decoder.setCallback(new Callback());
            decoder.configure(format, mRenderCallback.getSurface(), null, 0);
            decoder.start();

            while (!mStopped) {
                frameLog("Waiting NALU");
                buf = mNalParser.waitNal();
                if (mStopped) {
                    break;
                }

                int NALType = buf.buf[4] & 0x1F;
                frameLog("Got NAL TYPE " + NALType + " Len " + buf.len + "  q:" + mNalParser.getNalListSize());

                if (frameNumber > 500) {
                    //return;
                }
                int index;
                while (true) {
                    synchronized (availableInputs) {
                        if (availableInputs.size() > 0) {
                            index = availableInputs.get(0);
                            availableInputs.remove(0);
                            break;
                        }
                        availableInputs.wait();
                    }
                }
                frameLog("Uses input index=" + index + " NAL Queue Size=" + mNalParser.getNalListSize() + " (Max:" + nalQueueMax + (mNalParser.getNalListSize() > nalQueueMax ? " discard)" : ")"));
                consecutiveStalls = 0;

                ByteBuffer buffer = decoder.getInputBuffer(index);

                if (mNalParser.getNalListSize() > nalQueueMax) {
                    mNalParser.flushNALList();
                }

                if (NALType == 7) {
                    // SPS
                    SPSBuffer = buf;

                    buf = null;
                    decoder.queueInputBuffer(index, 0, 0, 0, 0);
                } else if (NALType == 8) {
                    // PPS
                    PPSBuffer = buf;

                    buf = null;
                    if (waitNextIDR) {
                        if (SPSBuffer != null)
                            buffer.put(SPSBuffer.buf, 0, SPSBuffer.len);
                        buffer.put(buf.buf, 0, buf.len);

                        waitNextIDR = false;
                        frameLog("Sending Codec Config. Size: " + buffer.position());
                        decoder.queueInputBuffer(index, 0, buffer.position(), 0, MediaCodec.BUFFER_FLAG_CODEC_CONFIG);
                    } else {
                        decoder.queueInputBuffer(index, 0, buffer.position(), 0, 0);
                    }
                } else if (NALType == 5) {
                    // IDR
                    frameLog("Sending IDR SPS:" + SPSBuffer.len + " PPS:" + PPSBuffer.len + " IDR:" + buf.len);

                    buffer.put(buf.buf, 0, buf.len);
                    frameNumber++;

                    debugIDRFrame(buf, SPSBuffer, PPSBuffer);

                    pushFramePresentationMap(buf);

                    decoder.queueInputBuffer(index, 0, buffer.position(), buf.presentationTime, 0);
                    buf = null;
                    //decoder.queueInputBuffer(inIndex, 0, buffer.position(), startTimestamp, 0);
                } else {
                    buffer.put(buf.buf, 0, buf.len);

                    if (NALType == 1) {
                        // PFrame
                        mCounter.countPFrame();
                    }
                    frameNumber++;

                    if (waitNextIDR) {
                        // Ignore P-Frame until next I-Frame
                        frameLog("Ignoring P-Frame");
                        decoder.queueInputBuffer(index, 0, 0, buf.presentationTime, 0);
                        buf = null;
                    } else {
                        pushFramePresentationMap(buf);

                        frameLog("Feed " + buffer.position() + " bytes " + String.format("%02X", NALType) + " frame " + frameNumber + " fr:" + buf.frameIndex + " pres:" + buf.presentationTime);
                        decoder.queueInputBuffer(index, 0, buffer.position(), buf.presentationTime, 0);
                        buf = null;
                    }
                }
            }

        } catch (IOException | InterruptedException | IllegalStateException e) {
            e.printStackTrace();
            Log.v(TAG, "DecoderThread stopped by Exception.");
        } finally {
            if (decoder != null) {
                try {
                    decoder.stop();
                    decoder.release();
                } catch (IllegalStateException e) {
                }
            }
        }
        Log.v(TAG, "DecoderThread stopped.");
    }

    private void debugIDRFrame(NAL buf, NAL spsBuffer, NAL ppsBuffer) {
        /*
        try {
            FileOutputStream stream = new FileOutputStream(mMainActivity.getExternalMediaDirs()[0].getAbsolutePath() + "/" + buf.frameIndex + ".h264");
            stream.write(spsBuffer.buf, 0, spsBuffer.len);
            stream.write(ppsBuffer.buf, 0, ppsBuffer.len);
            stream.write(buf.buf, 0, buf.len);
            stream.close();
        } catch (IOException e) {
            e.printStackTrace();
        }*/
    }

    class Callback extends MediaCodec.Callback {
        @Override
        public void onInputBufferAvailable(@NonNull MediaCodec codec, int index) {
            frameLog("onInputBufferAvailable " + index + " " + buf);

            synchronized (availableInputs) {
                availableInputs.add(index);
                availableInputs.notifyAll();
            }
        }

        @Override
        public void onOutputBufferAvailable(@NonNull MediaCodec codec, int index, @NonNull MediaCodec.BufferInfo info) {
            mCounter.countOutputFrame(1);

            if (queuedOutputBuffer != -1) {
                decoder.releaseOutputBuffer(queuedOutputBuffer, false);
                queuedOutputBuffer = -1;
            }
            queuedOutputBuffer = index;
            foundFrameIndex = 0;
            long inputTime = 0;

            synchronized (frameBuf) {
                for (FramePresentationTime f : frameBuf) {
                    if (f.presentationTime == info.presentationTimeUs) {
                        foundFrameIndex = f.frameIndex;
                        inputTime = f.inputTime;
                        break;
                    }
                }
            }

            long decodeLatency = System.nanoTime() / 1000 - inputTime;
            frameLog("Render frame " + "fr:" + foundFrameIndex + " pres:" + info.presentationTimeUs + " decode latency=" + decodeLatency + " us");

            queuedOutputBuffer = mRenderCallback.renderIf(decoder, queuedOutputBuffer, foundFrameIndex);
            if (queuedOutputBuffer == -1) {
                frameLog("consumed");
            } else {
                frameLog("not ready. discard.");
                decoder.releaseOutputBuffer(queuedOutputBuffer, false);
                queuedOutputBuffer = -1;
            }
        }

        @Override
        public void onError(@NonNull MediaCodec codec, @NonNull MediaCodec.CodecException e) {
            Log.e("DecodeActivity", "Codec Error: " + e.getMessage() + "\n" + e.getDiagnosticInfo());
        }

        @Override
        public void onOutputFormatChanged(@NonNull MediaCodec codec, @NonNull MediaFormat format) {
            Log.d("DecodeActivity", "New format " + decoder.getOutputFormat());
        }
    }
}
