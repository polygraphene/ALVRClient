package com.polygraphene.remoteglass;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaDataSource;
import android.media.MediaFormat;
import android.media.MediaPlayer;
import android.provider.ContactsContract;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;
import java.net.SocketException;
import java.nio.BufferOverflowException;
import java.nio.BufferUnderflowException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public class MainActivity extends AppCompatActivity {
    private final static String TAG = "MainActivity";
    private SurfaceView mSurfaceView;
    private SurfaceHolder mHolder;
    private final SurfaceHolder.Callback mCallback = new VideoSurface(this);
    private List<MediaCodecInfo> AvcCodecInfoes = new ArrayList<>();

    private static class VideoSurface implements SurfaceHolder.Callback {

        public VideoSurface(MainActivity mainActivity) {
            activity = mainActivity;
        }

        enum h264ByteFormatState {
            ZERO_0,
            ZERO_1,
            ZERO_2,
            ZERO_3,
            NAL_HEAD,
            NAL_BODY
        }

        h264ByteFormatState state = h264ByteFormatState.ZERO_0;
        ByteArrayOutputStream NALBuffer = new ByteArrayOutputStream();
        byte[] SPSBuffer = null;
        byte[] PPSBuffer = null;
        ByteBuffer PPSSPSTrailing = ByteBuffer.allocate(0);
        private MainActivity activity;

        private boolean findZero(ByteBuffer buffer) {
            try {
                while (true) {
                    if (buffer.get() == 0x00) {
                        return true;
                    }
                }
            } catch (BufferOverflowException e) {
                return false;
            }
        }

        private int findNALEnd(byte[] buf) {
            for (int i = 0; i < buf.length; i++) {
                if (i + 3 < buf.length &&
                        buf[i + 0] == 0x00 && buf[i + 1] == 0x00 && buf[i + 2] == 0x00 && buf[i + 3] == 0x01) {
                    return i;
                } else if (i + 2 < buf.length &&
                        buf[i + 0] == 0x00 && buf[i + 1] == 0x00 && buf[i + 2] == 0x01) {
                    return i;
                }
            }
            return -1;
        }

        private boolean checkParameters(ByteBuffer buffer) {
            try {
                while (true) {
                    if (state == h264ByteFormatState.ZERO_0) {
                        if (findZero(buffer)) {
                            state = h264ByteFormatState.ZERO_1;
                        } else {
                            return false;
                        }
                    }
                    if (state == h264ByteFormatState.ZERO_1) {
                        byte c = buffer.get();
                        if (c == 0x00) {
                            state = h264ByteFormatState.ZERO_2;
                        } else {
                            state = h264ByteFormatState.ZERO_0;

                            continue;
                        }
                    }
                    if (state == h264ByteFormatState.ZERO_2) {
                        byte c = buffer.get();
                        if (c == 0x00) {
                            state = h264ByteFormatState.ZERO_3;
                        } else if (c == 0x01) {
                            state = h264ByteFormatState.NAL_HEAD;
                        } else {
                            state = h264ByteFormatState.ZERO_0;

                            continue;
                        }
                    }
                    if (state == h264ByteFormatState.ZERO_3) {
                        byte c = buffer.get();
                        if (c == 0x00) {
                            // more than 3 zeroes
                            //state = h264ByteFormatState.ZERO_3;
                        } else if (c == 0x01) {
                            state = h264ByteFormatState.NAL_HEAD;
                        } else {
                            state = h264ByteFormatState.ZERO_0;

                            continue;
                        }
                    }
                    if (state == h264ByteFormatState.NAL_HEAD) {
                        NALBuffer.reset();
                        state = h264ByteFormatState.NAL_BODY;
                    }
                    if (state == h264ByteFormatState.NAL_BODY) {
                        NALBuffer.write(buffer.array(), buffer.position(), buffer.remaining());

                        byte[] buf = NALBuffer.toByteArray();
                        int end = findNALEnd(buf);
                        if (end == -1) {
                            // not enough NAL buffer
                            return false;
                        }
                        state = h264ByteFormatState.ZERO_0;

                        // write back for next read
                        buffer = ByteBuffer.wrap(buf, end, buf.length - end);

                        int nalHeader = buf[0] & 0x1F;
                        Log.v(TAG, "NAL Type " + nalHeader + " len:" + end);
                        if (nalHeader == 7) {
                            // SPS
                            SPSBuffer = new byte[4 + end];
                            SPSBuffer[0] = 0x00;
                            SPSBuffer[1] = 0x00;
                            SPSBuffer[2] = 0x00;
                            SPSBuffer[3] = 0x01;
                            System.arraycopy(buf, 0, SPSBuffer, 4, end);
                            if (PPSBuffer != null) {
                                PPSSPSTrailing = buffer;
                                return true;
                            }
                        } else if (nalHeader == 8) {
                            // PPS
                            PPSBuffer = new byte[4 + end];
                            PPSBuffer[0] = 0x00;
                            PPSBuffer[1] = 0x00;
                            PPSBuffer[2] = 0x00;
                            PPSBuffer[3] = 0x01;
                            System.arraycopy(buf, 0, PPSBuffer, 4, end);

                            if (SPSBuffer != null) {
                                PPSSPSTrailing = buffer;
                                return true;
                            }
                        }
                    }
                }
            } catch (BufferUnderflowException e) {
                return false;
            }
        }

        long prev = 0;
        long counter = 0;
        long sizeCounter = 0;

        private void countPacket(int size) {
            if (prev != 0 && prev != System.currentTimeMillis() / 1000) {
                Log.v(TAG, counter + " Packets/s " + ((float) sizeCounter) / 1000 * 8 + " kb/s");
                counter = 0;
                sizeCounter = 0;
            }
            counter++;
            sizeCounter += size;
            prev = System.currentTimeMillis() / 1000;
        }

        @Override
        public void surfaceCreated(final SurfaceHolder holder) {
            try {
                Thread th = new Thread() {
                    @Override
                    public void run() {
                        try {
                            DatagramSocket socket = new DatagramSocket(9944);
                            byte[] packetBuffer = new byte[2000];
                            DatagramPacket packet = new DatagramPacket(packetBuffer, packetBuffer.length);

                            if(false) {
                                for (int i = 0; ; i++) {
                                    socket.receive(packet);
                                    Log.v(TAG, "Packet received " + packet.getLength());
                                    if (checkParameters(ByteBuffer.wrap(packetBuffer))) {
                                        break;
                                    }
                                }
                            }else{
                                for (int i = 0; ; i++) {
                                    socket.receive(packet);
                                    //Log.v(TAG, "Packet received " + packet.getLength());
                                    countPacket(packet.getLength());
                                    checkParameters(ByteBuffer.wrap(packetBuffer));
                                }
                            }

                            int width = 1920;
                            int height = 1080;
                            String videoFormat = "video/avc";
                            MediaFormat format = MediaFormat.createVideoFormat(videoFormat, width, height);
                            format.setString("KEY_MIME", videoFormat);
                            format.setByteBuffer("csd-0", ByteBuffer.wrap(SPSBuffer));
                            format.setByteBuffer("csd-1", ByteBuffer.wrap(PPSBuffer));


                            String codecName = activity.AvcCodecInfoes.get(0).getName();
                            Log.v(TAG, "Create codec " + codecName);
                            final MediaCodec decoder = MediaCodec.createByCodecName(codecName);

                            decoder.configure(format, holder.getSurface(), null, 0);
                            decoder.setVideoScalingMode(MediaCodec.VIDEO_SCALING_MODE_SCALE_TO_FIT);
                            decoder.start();

                            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();

                            while (true) {
                                int inIndex = decoder.dequeueInputBuffer(1000);
                                //Log.v(TAG, "dequeueInputBuffer " + inIndex);
                                if (inIndex >= 0) {
                                    ByteBuffer buffer = decoder.getInputBuffer(inIndex);

                                    int sampleSize = PPSSPSTrailing.remaining();
                                    if (sampleSize > 0) {
                                        byte[] buf = new byte[sampleSize];
                                        PPSSPSTrailing.get(buf);

                                        Log.v(TAG, String.format("Trailing: %02X %02X %02X %02X %02X", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]));
                                    }

                                    for (int i = 0; i < 100; i++) {
                                        socket.receive(packet);
                                        //Log.v(TAG, "Packet received " + packet.getLength());
                                        buffer.put(packetBuffer, 0, packet.getLength());
                                        sampleSize += packet.getLength();
                                        countPacket(packet.getLength());
                                        if (false) break;
                                    }
                                    if (sampleSize < 0) {
                                        // We shouldn't stop the playback at this point, just pass the EOS
                                        // flag to decoder, we will get it again from the
                                        // dequeueOutputBuffer
                                        Log.d("DecodeActivity", "InputBuffer BUFFER_FLAG_END_OF_STREAM");
                                        decoder.queueInputBuffer(inIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                                        break;
                                    } else {
                                        Log.v(TAG, "Feed input buffer " + sampleSize + " bytes");
                                        decoder.queueInputBuffer(inIndex, 0, sampleSize, 0, 0);

                                    }
                                }


                                int lastIndex = -1;
                                while(true) {
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
                                            Log.d("DecodeActivity", "Output buffer " + outIndex);

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
                                            if(lastIndex >= 0) {
                                                decoder.releaseOutputBuffer(lastIndex, 0);
                                            }

                                            lastIndex = outIndex;
                                            continue;
                                    }

                                    if(lastIndex != -1)
                                        decoder.releaseOutputBuffer(lastIndex, true);
                                    break;
                                }


                            }

                        } catch (IOException e) {
                            e.printStackTrace();
                        }
                    }
                };
                th.start();
            } catch (IllegalArgumentException | IllegalStateException | SecurityException e) {
                e.printStackTrace();
            }
        }

        //
        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {

        }

        /**
         * SurfaceViewが終了した時に呼び出される
         */
        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
        }

    }

    ;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        mSurfaceView = (SurfaceView) findViewById(R.id.surfaceview);
        // SurfaceViewにコールバックを設定
        mHolder = mSurfaceView.getHolder();
        mHolder.addCallback(mCallback);

        MediaCodecList mcl = new MediaCodecList(MediaCodecList.REGULAR_CODECS);

        for (MediaCodecInfo info : mcl.getCodecInfos()) {
            boolean isAvc = false;

            for (String type : info.getSupportedTypes()) {
                if (type.equals("video/avc")) {
                    isAvc = true;
                    break;
                }
            }
            if (isAvc && !info.isEncoder()) {
                MediaCodecInfo.CodecCapabilities capabilitiesForType = info.getCapabilitiesForType("video/avc");
                Log.v(TAG, info.getName());
                for (MediaCodecInfo.CodecProfileLevel profile : capabilitiesForType.profileLevels) {
                    Log.v(TAG, "profile:" + profile.profile + " level:" + profile.level);
                }

                AvcCodecInfoes.add(info);
            }
        }

    }

    private class UdpMediaDataSource extends MediaDataSource {
        DatagramSocket socket;
        int consumed = -1;
        DatagramPacket packet;
        byte[] packetBuffer = new byte[2000];

        UdpMediaDataSource() {
            try {
                socket = new DatagramSocket(9944);
            } catch (SocketException e) {
                e.printStackTrace();
            }

            Log.v(TAG, "socket bound " + socket.isBound());
        }

        @Override
        public int readAt(long position, byte[] buffer, int offset, int size) throws IOException {
            if (consumed == -1) {
                packet = new DatagramPacket(packetBuffer, packetBuffer.length);

                socket.receive(packet);
                Log.v(TAG, "Packet received " + packet.getLength());

                consumed = 0;
            }
            if ((packet.getLength() - consumed) < size) {
                System.arraycopy(packetBuffer, consumed, buffer, offset, packet.getLength() - consumed);
                consumed = -1;
                return packet.getLength() - consumed;
            } else {
                System.arraycopy(packetBuffer, consumed, buffer, offset, size);
                consumed += size;
                return size;
            }
        }

        @Override
        public long getSize() throws IOException {
            return 0;
        }

        @Override
        public void close() throws IOException {
            socket.close();
        }
    }
}
