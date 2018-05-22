package com.polygraphene.alvr;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.support.annotation.NonNull;
import android.util.Log;

import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.LinkedList;
import java.util.List;

class DecoderThread extends Thread {
    private static final String TAG = "DecoderThread";
    private final MediaCodecInfo codecInfo;
    private MainActivity mainActivity;
    private UdpReceiverThread mReceiverThread;
    private MediaCodec decoder = null;
    private int queuedOutputBuffer = -1;
    private StatisticsCounter mCounter;

    NAL buf = null;

    boolean waitNextIDR = false;
    long consecutiveStalls = 0;
    long frameNumber = 0;
    long prevResetTimestamp = System.nanoTime() / 1000;

    long prevTimestamp = -1;

    long foundFrameIndex = 0;

    class FramePresentationTime {
        public long frameIndex;
        public long presentationTime;
        public long inputTime;
    }

    private List<FramePresentationTime> frameBuf = new LinkedList<>();

    private NAL IDRBuffer = null;
    private NAL SPSBuffer = null;
    private NAL PPSBuffer = null;

    private List<Integer> availableInputs = new LinkedList<>();

    DecoderThread(MainActivity mainActivity, UdpReceiverThread receiverThread, MediaCodecInfo codecInfo, StatisticsCounter counter) {
        this.mainActivity = mainActivity;
        this.mReceiverThread = receiverThread;
        this.codecInfo = codecInfo;
        mCounter = counter;
    }

    void frameLog(String s) {
        Log.v(TAG, s);
    }

    void pushFramePresentationMap(NAL buf) {
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

    @Override
    public void run() {
        setName(DecoderThread.class.getName());

        try {
            while (true) {
                NAL packet = mReceiverThread.getNal();
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

            int nalQueueMax = 10;
            int width = 2048;
            int height = 1024;
            String videoFormat = "video/avc";
            MediaFormat format = MediaFormat.createVideoFormat(videoFormat, width, height);
            format.setString("KEY_MIME", videoFormat);
            format.setByteBuffer("csd-0", ByteBuffer.wrap(SPSBuffer.buf, 0, SPSBuffer.len));
            format.setByteBuffer("csd-1", ByteBuffer.wrap(PPSBuffer.buf, 0, PPSBuffer.len));

            String codecName = codecInfo.getName();
            Log.v(TAG, "Create codec " + codecName);
            decoder = MediaCodec.createByCodecName(codecName);

            decoder.setVideoScalingMode(MediaCodec.VIDEO_SCALING_MODE_SCALE_TO_FIT);
            decoder.setCallback(new Callback());
            decoder.configure(format, mainActivity.getSurface(), null, 0);
            decoder.start();

            while (true) {
                if (mainActivity.isStopped()) {
                    break;
                }
                frameLog("Waiting NALU");
                buf = mReceiverThread.waitNal();

                int NALType = buf.buf[4] & 0x1F;
                frameLog("Got NAL TYPE " + NALType + " Len " + buf.len + "  q:" + mReceiverThread.getNalListSize());

                if (frameNumber > 500) {
                    //return;
                }
                int index;
                while(true) {
                    synchronized (availableInputs) {
                        if (availableInputs.size() > 0) {
                            index = availableInputs.get(0);
                            availableInputs.remove(0);
                            break;
                        }
                        availableInputs.wait();
                    }
                }
                frameLog("Uses input index=" + index + " NAL Queue Size=" + mReceiverThread.getNalListSize() + " (Max:" + nalQueueMax + (mReceiverThread.getNalListSize() > nalQueueMax ? " discard)" : ")"));
                consecutiveStalls = 0;

                ByteBuffer buffer = decoder.getInputBuffer(index);

                if (mReceiverThread.getNalListSize() > nalQueueMax) {
                    mReceiverThread.flushNALList();
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

                    pushFramePresentationMap(buf);
                    FileOutputStream outputStream = new FileOutputStream(mainActivity.getExternalMediaDirs()[0].toString() + "/" + buf.frameIndex + ".h264");
                    outputStream.write(SPSBuffer.buf, 0, SPSBuffer.len);
                    outputStream.write(PPSBuffer.buf, 0, PPSBuffer.len);
                    outputStream.write(buf.buf, 0, buf.len);
                    outputStream.close();

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

    String calcDiff(long us) {
        long ms = (us - 11644473600000000L) / 1000;
        return "" + (ms - System.currentTimeMillis()) + " ms";
    }

    class Callback extends MediaCodec.Callback {
        @Override
        public void onInputBufferAvailable(@NonNull MediaCodec codec, int index) {
            frameLog("onInputBufferAvailable " + index + " " + buf);

            synchronized (availableInputs){
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

            queuedOutputBuffer = mainActivity.renderIf(decoder, queuedOutputBuffer, foundFrameIndex);
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

        }

        @Override
        public void onOutputFormatChanged(@NonNull MediaCodec codec, @NonNull MediaFormat format) {
            Log.d("DecodeActivity", "New format " + decoder.getOutputFormat());
        }
    }
}
