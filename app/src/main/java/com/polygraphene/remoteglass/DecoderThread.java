package com.polygraphene.remoteglass;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.util.Log;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Date;

class DecoderThread extends Thread {
    private static final String TAG = "DecoderThread";
    private final MediaCodecInfo codecInfo;
    private MainActivity mainActivity;
    private SrtReceiverThread nalParser;

    private NAL IDRBuffer = null;
    private NAL SPSBuffer = null;
    private NAL PPSBuffer = null;

    long time0;

    DecoderThread(MainActivity mainActivity, SrtReceiverThread nalParser, MediaCodecInfo codecInfo) {
        this.mainActivity = mainActivity;
        this.nalParser = nalParser;
        this.codecInfo = codecInfo;
    }

    @Override
    public void run() {
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
            time0 = System.nanoTime() / 1000;


            int width = 1920;
            int height = 1080;
            String videoFormat = "video/avc";
            MediaFormat format = MediaFormat.createVideoFormat(videoFormat, width, height);
            format.setString("KEY_MIME", videoFormat);
            format.setByteBuffer("csd-0", ByteBuffer.wrap(SPSBuffer.buf, 0, SPSBuffer.len));
            format.setByteBuffer("csd-1", ByteBuffer.wrap(PPSBuffer.buf, 0, PPSBuffer.len));

            String codecName = codecInfo.getName();
            Log.v(TAG, "Create codec " + codecName);
            final MediaCodec decoder = MediaCodec.createByCodecName(codecName);

            decoder.configure(format, mainActivity.getSurface(), null, 0);
            decoder.setVideoScalingMode(MediaCodec.VIDEO_SCALING_MODE_SCALE_TO_FIT);
            decoder.start();

            boolean waitNextIDR = false;
            long consecutiveStalls = 0;
            long frameNumber = 0;
            long prevResetTimestamp = System.nanoTime() / 1000;

            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();

            long prevTimestamp = -1;
            while (true) {
                if (mainActivity.isStopped()) {
                    break;
                }
                long timestamp = System.nanoTime() / 1000;
                if (prevTimestamp == timestamp) {
                    timestamp++;
                }

                int inIndex = decoder.dequeueInputBuffer(1000);
                //Log.v(TAG, "dequeueInputBuffer " + inIndex);
                if (inIndex >= 0) {
                    consecutiveStalls = 0;

                    ByteBuffer buffer = decoder.getInputBuffer(inIndex);

                    NAL buf = nalParser.getNal();
                    if (buf != null) {
                        int NALType = buf.buf[4] & 0x1F;
                        Log.v(TAG, "Got NAL TYPE " + NALType + " Len " + buf.len + "  q:" + nalParser.getNalListSize());

                        if (NALType == 7) {
                            // SPS
                            SPSBuffer = buf;

                            decoder.queueInputBuffer(inIndex, 0, 0, 0, 0);
                        } else if (NALType == 8) {
                            // PPS
                            PPSBuffer = buf;

                            if (waitNextIDR) {
                                if (SPSBuffer != null)
                                    buffer.put(SPSBuffer.buf, 0, SPSBuffer.len);
                                buffer.put(buf.buf, 0, buf.len);

                                waitNextIDR = false;
                                Log.v(TAG, "Sending Codec Config. Size: " + buffer.position());
                                decoder.queueInputBuffer(inIndex, 0, buffer.position(), 0, MediaCodec.BUFFER_FLAG_CODEC_CONFIG);
                            } else {
                                decoder.queueInputBuffer(inIndex, 0, buffer.position(), 0, 0);
                            }
                        } else if (NALType == 5) {
                            // IDR
                            Log.v(TAG, "Sending IDR SPS:" + SPSBuffer.len + " PPS:" + PPSBuffer.len + " IDR:" + buf.len);

                            buffer.put(buf.buf, 0, buf.len);
                            frameNumber++;

                            decoder.queueInputBuffer(inIndex, 0, buffer.position(), buf.presentationTime, 0);
                            //decoder.queueInputBuffer(inIndex, 0, buffer.position(), startTimestamp, 0);
                            prevTimestamp = timestamp;
                        } else {
                            buffer.put(buf.buf, 0, buf.len);

                            if (NALType == 1) {
                                // PFrame
                                mainActivity.counter.countPFrame();
                            }
                            frameNumber++;

                            if (waitNextIDR) {
                                // Ignore P-Frame until next I-Frame
                                Log.v(TAG, "Ignoring P-Frame");
                                decoder.queueInputBuffer(inIndex, 0, 0, buf.presentationTime, 0);
                            } else {
                                Log.v(TAG, "Feed " + buffer.position() + " bytes " + String.format("%02X", NALType) + " frame " + frameNumber + " " + calcDiff(buf.presentationTime));
                                decoder.queueInputBuffer(inIndex, 0, buffer.position(), buf.presentationTime, 0);
                            }
                            prevTimestamp = timestamp;
                        }
                    } else {
                        // We shouldn't stop the playback at this point, just pass the EOS
                        // flag to decoder, we will get it again from the
                        // dequeueOutputBuffer
                        //Log.d("DecodeActivity", "InputBuffer BUFFER_FLAG_END_OF_STREAM");
                        //decoder.queueInputBuffer(inIndex, 0, 0, timestamp, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                        decoder.queueInputBuffer(inIndex, 0, 0, timestamp, 0);
                        prevTimestamp = timestamp;
                    }
                } else {
                    mainActivity.counter.countStall();
                    consecutiveStalls++;
                    //Log.v(TAG, "stalled " + consecutiveStalls);
                    if (consecutiveStalls > 100 && prevResetTimestamp < System.nanoTime() / 1000 - 1000 * 1000 * 10) {
                        // Codec input stalled. Try reset.
                        Log.v(TAG, "Codec input stalled. Try reset.");
                        waitNextIDR = true;
                        nalParser.flushNALList();

                        prevResetTimestamp = System.nanoTime() / 1000;
                        if (false) {
                            /*
                            decoder.reset();

                            nalParser.flushNALList();
                            while (true) {
                                byte[] buf = nalParser.getNal();
                                if (buf != null) {
                                    int NALType = buf[4] & 0x1F;
                                    if (NALType == 7) {
                                        // SPS
                                        Log.v(TAG, "Found next SPS");
                                        SPSBuffer = nalParser.recvNextNAL();
                                    } else if (NALType == 8) {
                                        format = MediaFormat.createVideoFormat(videoFormat, width, height);
                                        format.setString("KEY_MIME", videoFormat);
                                        format.setByteBuffer("csd-0", ByteBuffer.wrap(SPSBuffer));
                                        format.setByteBuffer("csd-1", ByteBuffer.wrap(buf));
                                        nalParser.recvNextNAL();

                                        decoder.configure(format, mainActivity.getSurface(), null, 0);
                                        decoder.setVideoScalingMode(MediaCodec.VIDEO_SCALING_MODE_SCALE_TO_FIT);
                                        decoder.start();
                                        break;
                                    } else {
                                        // Drop NAL
                                        nalParser.recvNextNAL();
                                    }
                                }
                            }*/
                        } else {
                            decoder.flush();
                        }
                    }
                }


                int lastIndex = -1;
                while (true) {
                    int outIndex = decoder.dequeueOutputBuffer(info, 0);
                    switch (outIndex) {
                        case MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED:
                            break;
                        case MediaCodec.INFO_OUTPUT_FORMAT_CHANGED:
                            Log.d("DecodeActivity", "New format " + decoder.getOutputFormat());
                            break;
                        case MediaCodec.INFO_TRY_AGAIN_LATER:
                            //Log.d("DecodeActivity", "dequeueOutputBuffer timed out!");
                            break;
                        default:
                            //Log.d("DecodeActivity", "Output buffer " + outIndex);
                            mainActivity.counter.countOutputFrame(1);

                            // We use a very simple clock to keep the video FPS, or the video
                            // playback will be too fast
                                        /*while (info.presentationTimeUs / 1000 > System.currentTimeMillis() - startMs) {
                                            try {
                                                sleep(10);
                                            } catch (InterruptedException e) {
                                                e.printStackTrace();
                                                break;
                                            }
                                        }*/
                            if (lastIndex >= 0) {
                                decoder.releaseOutputBuffer(lastIndex, 0);
                            }

                            lastIndex = outIndex;
                            continue;
                    }

                    if (lastIndex >= 0) {
                        Log.v(TAG, "render frame " + info.presentationTimeUs + " (" + (info.presentationTimeUs - timestamp) + ")" + " " + calcDiff(info.presentationTimeUs));
                        decoder.releaseOutputBuffer(lastIndex, true);
                    }
                    break;
                }
            }

        } catch (IOException | InterruptedException e) {
            e.printStackTrace();
            Log.v(TAG, "DecoderThread stopped by Exception.");
        }
        Log.v(TAG, "DecoderThread stopped.");
    }

    String calcDiff(long us) {
        long ms = (us-11644473600000000L)/1000;
        return "" + (ms - System.currentTimeMillis()) + " ms";
    }
}
