package com.polygraphene.remoteglass;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.support.annotation.NonNull;
import android.util.Log;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Date;
import java.util.LinkedList;
import java.util.List;

class DecoderThread extends Thread {
    private static final String TAG = "DecoderThread";
    private final MediaCodecInfo codecInfo;
    private MainActivity mainActivity;
    private UdpReceiverThread nalParser;
    private MediaCodec decoder = null;
    private int queuedOutputBuffer = -1;

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
    }

    private List<FramePresentationTime> frameBuf = new LinkedList<>();

    private NAL IDRBuffer = null;
    private NAL SPSBuffer = null;
    private NAL PPSBuffer = null;

    private List<Integer> availableInputs = new LinkedList<>();

    DecoderThread(MainActivity mainActivity, UdpReceiverThread nalParser, MediaCodecInfo codecInfo) {
        this.mainActivity = mainActivity;
        this.nalParser = nalParser;
        this.codecInfo = codecInfo;
    }

    void frameLog(String s) {
        Log.v(TAG, s);
    }

    @Override
    public void run() {
        setName(DecoderThread.class.getName());

        try {
            while (true) {
                NAL packet = nalParser.getNal();
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
                buf = nalParser.waitNal();

                int NALType = buf.buf[4] & 0x1F;
                frameLog("Got NAL TYPE " + NALType + " Len " + buf.len + "  q:" + nalParser.getNalListSize());

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
                frameLog("Uses input index=" + index);
                consecutiveStalls = 0;

                ByteBuffer buffer = decoder.getInputBuffer(index);

                if (nalParser.getNalListSize() > 1) {
                    nalParser.flushNALList();
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

                    decoder.queueInputBuffer(index, 0, buffer.position(), buf.presentationTime, 0);
                    buf = null;
                    //decoder.queueInputBuffer(inIndex, 0, buffer.position(), startTimestamp, 0);
                } else {
                    buffer.put(buf.buf, 0, buf.len);

                    if (NALType == 1) {
                        // PFrame
                        mainActivity.counter.countPFrame();
                    }
                    frameNumber++;

                    if (waitNextIDR) {
                        // Ignore P-Frame until next I-Frame
                        frameLog("Ignoring P-Frame");
                        decoder.queueInputBuffer(index, 0, 0, buf.presentationTime, 0);
                        buf = null;
                    } else {
                        FramePresentationTime f = new FramePresentationTime();
                        f.frameIndex = buf.frameIndex;
                        f.presentationTime = buf.presentationTime;

                        synchronized (frameBuf) {
                            frameBuf.add(f);
                            if (frameBuf.size() > 100) {
                                frameBuf.remove(0);
                            }
                        }
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
            mainActivity.counter.countOutputFrame(1);

            if (queuedOutputBuffer != -1) {
                decoder.releaseOutputBuffer(queuedOutputBuffer, false);
                queuedOutputBuffer = -1;
            }
            queuedOutputBuffer = index;
            foundFrameIndex = 0;

            synchronized (frameBuf) {
                for (FramePresentationTime f : frameBuf) {
                    if (f.presentationTime == info.presentationTimeUs) {
                        foundFrameIndex = f.frameIndex;
                    }
                }
            }

            frameLog("render frame " + "fr:" + foundFrameIndex + " pres:" + info.presentationTimeUs);

            queuedOutputBuffer = mainActivity.renderIf(decoder, queuedOutputBuffer, foundFrameIndex);
            if (queuedOutputBuffer == -1) {
                frameLog("consumed");
            } else {
                frameLog("not ready");
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
