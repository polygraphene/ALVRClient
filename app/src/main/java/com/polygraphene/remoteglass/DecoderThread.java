package com.polygraphene.remoteglass;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.util.Log;

import java.io.IOException;
import java.nio.ByteBuffer;

class DecoderThread extends Thread {
    private static final String TAG = "DecoderThread";
    private final MediaCodecInfo codecInfo;
    private MainActivity mainActivity;
    private NALParser nalParser;

    private byte[] IDRBuffer = null;
    private byte[] SPSBuffer = null;
    private byte[] PPSBuffer = null;

    DecoderThread(MainActivity mainActivity, NALParser nalParser, MediaCodecInfo codecInfo){
        this.mainActivity = mainActivity;
        this.nalParser = nalParser;
        this.codecInfo = codecInfo;
    }

    @Override
    public void run() {
        try {

            if (true) {
                for (int i = 0; ; i++) {
                    ByteBuffer packet = nalParser.getPacket();
                    nalParser.parseNAL(packet);

                    for (byte[] buf : nalParser.getNALList()) {
                        int NALType = buf[4] & 0x1F;
                        //Log.v(TAG, "nal " + NALType);
                        if (NALType == 5) {
                            // First I-Frame

                            SPSBuffer = nalParser.getNALList().get(0);
                            PPSBuffer = nalParser.getNALList().get(1);
                            IDRBuffer = nalParser.getNALList().get(2);
                            break;
                        }
                    }
                    if (SPSBuffer != null) {
                        break;
                    }
                }

            } else {
                for (int i = 0; ; i++) {
                    ByteBuffer packet = nalParser.getPacket();
                    //Log.v(TAG, "Packet received " + packet.getLength());
                    nalParser.parseNAL(packet);
                    nalParser.getNALList().clear();
                }
            }

            int width = 1920;
            int height = 1080;
            String videoFormat = "video/avc";
            MediaFormat format = MediaFormat.createVideoFormat(videoFormat, width, height);
            format.setString("KEY_MIME", videoFormat);
            format.setByteBuffer("csd-0", ByteBuffer.wrap(SPSBuffer));
            format.setByteBuffer("csd-1", ByteBuffer.wrap(PPSBuffer));

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

                    int sampleSize = 0;

                    byte[] buf = nalParser.recvNextNAL();
                    if (buf != null) {
                        int NALType = buf[4] & 0x1F;
                        Log.v(TAG, "Got NAL TYPE " + NALType + " Len " + buf.length);

                        if (NALType == 7) {
                            // SPS
                            SPSBuffer = buf;

                            decoder.queueInputBuffer(inIndex, 0, 0, 0, 0);
                        } else if (NALType == 8) {
                            // PPS
                            PPSBuffer = buf;

                            if (waitNextIDR) {
                                if (SPSBuffer != null)
                                    buffer.put(SPSBuffer);
                                buffer.put(buf);

                                waitNextIDR = false;
                                Log.v(TAG, "Sending Codec Config. Size: " + buffer.position());
                                decoder.queueInputBuffer(inIndex, 0, buffer.position(), 0, MediaCodec.BUFFER_FLAG_CODEC_CONFIG);
                            } else {
                                decoder.queueInputBuffer(inIndex, 0, buffer.position(), 0, 0);
                            }
                        } else if (NALType == 5) {
                            // IDR
                            Log.v(TAG, "Sending IDR SPS:" + SPSBuffer.length + " PPS:" + PPSBuffer.length + " IDR:" + buf.length);

                            //buffer.put(buf);
                            nalParser.replaceNAL3To4(buffer, ByteBuffer.wrap(buf));

                            frameNumber++;

                            decoder.queueInputBuffer(inIndex, 0, buffer.position(), timestamp, 0);
                            //decoder.queueInputBuffer(inIndex, 0, buffer.position(), startTimestamp, 0);
                            prevTimestamp = timestamp;
                        } else {
                            //buffer.put(buf);
                            nalParser.replaceNAL3To4(buffer, ByteBuffer.wrap(buf));
                            sampleSize += buffer.position();

                            if ((buf[4] & 0x1F) == 1) {
                                // PFrame
                                mainActivity.counter.countPFrame();
                            }
                            frameNumber++;

                            if (waitNextIDR) {
                                // Ignore P-Frame until next I-Frame
                                Log.v(TAG, "Ignoring P-Frame");
                                decoder.queueInputBuffer(inIndex, 0, 0, timestamp, 0);
                            } else {
                                Log.v(TAG, "Feed " + sampleSize + " bytes " + String.format("%02X", (buf[4] & 0x1F)) + " frame " + frameNumber);
                                decoder.queueInputBuffer(inIndex, 0, sampleSize, timestamp, 0);
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
                        nalParser.flushPacketQueue();
                        nalParser.flushNALQueue();

                        prevResetTimestamp = System.nanoTime() / 1000;
                        if (false) {
                            decoder.reset();

                            nalParser.flushPacketQueue();
                            nalParser.flushNALQueue();
                            while (true) {
                                byte[] buf = nalParser.peekNextNAL();
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
                            }
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
                        Log.v(TAG, "render frame " + info.presentationTimeUs + " (" + (info.presentationTimeUs - timestamp) + ")");
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
}
