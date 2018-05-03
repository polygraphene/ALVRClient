package com.polygraphene.remoteglass;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaFormat;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import java.io.ByteArrayOutputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.nio.BufferOverflowException;
import java.nio.BufferUnderflowException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.LinkedList;
import java.util.List;

public class MainActivity extends AppCompatActivity {
    private final static String TAG = "MainActivity";
    private SurfaceView mSurfaceView;
    private SurfaceHolder mHolder;
    private final SurfaceHolder.Callback mCallback = new VideoSurface();
    private List<MediaCodecInfo> AvcCodecInfoes = new ArrayList<>();
    private byte[] IDRBuffer;
    private boolean stopped = false;

    enum h264ByteFormatState {
        ZERO_0,
        ZERO_1,
        ZERO_2,
        ZERO_3,
        NAL
    }

    h264ByteFormatState state = h264ByteFormatState.ZERO_0;
    ByteArrayOutputStream NALBuffer = new ByteArrayOutputStream();
    byte[] SPSBuffer = null;
    byte[] PPSBuffer = null;
    byte[] AUDBuffer = null;
    long parsedNALCoutner = 0;

    private DecoderThread decoderThread;
    //private ReceiverThread receiverThread = new ReceiverThread();
    private ReceiverThread receiverThread;

    private List<ByteBuffer> packetQueue = new LinkedList<>();
    private List<byte[]> NALList = new LinkedList<>();

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

    private int findNALEnd(byte[] buf, int offset, int length) {
        for (int i = offset; i < offset + length; i++) {
            if (i + 3 < offset + length &&
                    buf[i] == 0x00 && buf[i + 1] == 0x00 && buf[i + 2] == 0x00 && buf[i + 3] == 0x01) {
                return i;
                //} else if (i + 2 < offset + length &&
                //        buf[i] == 0x00 && buf[i + 1] == 0x00 && buf[i + 2] == 0x01) {
                //    return i;
            }
        }
        return -1;
    }

    private void replaceNAL3To4(ByteBuffer dest, ByteBuffer src){
        int state = 0;
        dest.put(src.get());
        dest.put(src.get());
        try {
            while (true) {
                byte c = src.get();
                if (state == 0 && c == 0) {
                    state = 1;
                    dest.put((byte) 0);
                } else if (state == 1 && c == 0) {
                    state = 2;
                    dest.put((byte) 0);
                } else if (state == 2 && c == 1) {
                    state = 0;
                    dest.put((byte) 0);
                    dest.put((byte) 1);
                } else {
                    dest.put(c);
                    state = 0;
                }
            }
        }catch (BufferUnderflowException e){

        }
    }

    private boolean parseNAL(ByteBuffer buffer) {
        try {
            while (true) {
                if (state == h264ByteFormatState.ZERO_0) {
                    if (findZero(buffer)) {
                        state = h264ByteFormatState.ZERO_1;
                        NALBuffer.reset();
                        NALBuffer.write(0);
                    } else {
                        NALBuffer.reset();
                        return false;
                    }
                }
                if (state == h264ByteFormatState.ZERO_1) {
                    byte c = buffer.get();
                    if (c == 0x00) {
                        NALBuffer.write(c);
                        state = h264ByteFormatState.ZERO_2;
                    } else {
                        state = h264ByteFormatState.ZERO_0;

                        continue;
                    }
                }
                if (state == h264ByteFormatState.ZERO_2) {
                    byte c = buffer.get();
                    if (c == 0x00) {
                        NALBuffer.write(c);
                        state = h264ByteFormatState.ZERO_3;
                    } else if (c == 0x01) {
                        NALBuffer.reset();
                        Log.v(TAG, "None Frame NAL has appeared!!");
                        //throw new IllegalStateException();
                        state = h264ByteFormatState.ZERO_0;
                        //state = h264ByteFormatState.NAL;
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
                        NALBuffer.write(c);
                        state = h264ByteFormatState.NAL;
                    } else {
                        state = h264ByteFormatState.ZERO_0;

                        continue;
                    }
                }
                if (state == h264ByteFormatState.NAL) {
                    int end = findNALEnd(buffer.array(), buffer.position(), buffer.remaining());
                    if (end == -1) {
                        NALBuffer.write(buffer.array(), buffer.position(), buffer.remaining());
                        return false;
                    }

                    byte[] buf = new byte[end - buffer.position() + NALBuffer.size()];
                    System.arraycopy(NALBuffer.toByteArray(), 0, buf, 0, NALBuffer.size());
                    buffer.get(buf, NALBuffer.size(), end - buffer.position());
                    NALList.add(buf);

                    int NALType = buf[4] & 0x1F;

                    parsedNALCoutner++;
                    Log.v(TAG, parsedNALCoutner + " NAL " + NALType + " " + buf.length);

/*
                    try {
                        String path = MainActivity.this.getExternalMediaDirs()[0].getAbsolutePath() + "/file-" + NALList.size();
                        FileOutputStream fileOutputStream = new FileOutputStream(path);
                        fileOutputStream.write(buf);
                        fileOutputStream.close();
                    }catch (IOException e){}*/
                    NALBuffer.reset();

                    state = h264ByteFormatState.ZERO_0;
                }
            }
        } catch (BufferUnderflowException e) {
            return false;
        }
    }

    long prev = 0;
    long counter = 0;
    long sizeCounter = 0;
    long frameCounter = 0;
    long PFrameCounter = 0;
    long StallCounter = 0;

    synchronized private void resetCounterIf() {
        long current = System.currentTimeMillis() / 1000;
        if (prev != 0 && prev != current) {
            Log.v(TAG, counter + " Packets/s " + ((float) sizeCounter) / 1000 * 8 + " kb/s " + frameCounter + " fps" + " fed PFrames " + PFrameCounter + " " + StallCounter + " stalled");
            counter = 0;
            sizeCounter = 0;
            frameCounter = 0;
            PFrameCounter = 0;
            StallCounter = 0;
        }
        prev = current;
    }

    private void countPacket(int size, int frame) {
        resetCounterIf();
        if (size != 0) counter++;
        frameCounter += frame;
        sizeCounter += size;
    }

    private void countPFrame() {
        resetCounterIf();
        PFrameCounter++;
    }

    private void countStall() {
        resetCounterIf();
        StallCounter++;
    }


    private class VideoSurface implements SurfaceHolder.Callback {

        @Override
        public void surfaceCreated(final SurfaceHolder holder) {
            try {
                decoderThread.start();
                receiverThread.start();
            } catch (IllegalArgumentException | IllegalStateException | SecurityException e) {
                e.printStackTrace();
            }
        }

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
        }

    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        mSurfaceView = (SurfaceView) findViewById(R.id.surfaceview);

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


    @Override
    protected void onStop() {
        super.onStop();

        stopped = true;
        decoderThread.interrupt();
        receiverThread.interrupt();
        decoderThread = null;
        receiverThread = null;
    }

    @Override
    protected void onResume() {
        super.onResume();

        if (decoderThread != null) {
            decoderThread.interrupt();
        }
        if (receiverThread != null) {
            receiverThread.interrupt();
        }

        decoderThread = new DecoderThread();
        receiverThread = new ReceiverThread();
        //receiverThread = new ReplayReceiverThread();
    }

    ByteBuffer getPacket() throws InterruptedException {
        while (true) {
            synchronized (packetQueue) {
                if (packetQueue.size() > 0) {
                    ByteBuffer packet = packetQueue.get(0);
                    packetQueue.remove(0);
                    return packet;
                }
            }
            Thread.sleep(1);
        }
    }

    ByteBuffer peekPacket() {
        synchronized (packetQueue) {
            if (packetQueue.size() > 0) {
                ByteBuffer packet = packetQueue.get(0);
                packetQueue.remove(0);
                return packet;
            }
        }
        return null;
    }

    void flushPacketQueue() {
        synchronized (packetQueue) {
            packetQueue.clear();
        }
    }

    void flushNALQueue() {
        NALList.clear();
    }

    byte[] peekNextNAL() {
        while (true) {
            if (!NALList.isEmpty()) {
                return NALList.get(0);
            }
            ByteBuffer packet = peekPacket();
            if (packet == null) {
                return null;
            }

            parseNAL(packet);
        }
    }

    byte[] recvNextNAL() {
        byte[] buf = peekNextNAL();
        if (buf == null) {
            return null;
        }
        NALList.remove(0);
        return buf;
    }

    private class DecoderThread extends Thread {
        @Override
        public void run() {
            try {

                if (true) {
                    for (int i = 0; ; i++) {
                        ByteBuffer packet = getPacket();
                        parseNAL(packet);

                        for (byte[] buf : NALList) {
                            int NALType = buf[4] & 0x1F;
                            //Log.v(TAG, "nal " + NALType);
                            if (NALType == 5) {
                                // First I-Frame

                                SPSBuffer = NALList.get(0);
                                PPSBuffer = NALList.get(1);
                                IDRBuffer = NALList.get(2);
                                break;
                            }
                        }
                        if (SPSBuffer != null) {
                            break;
                        }
                    }

                } else {
                    for (int i = 0; ; i++) {
                        ByteBuffer packet = getPacket();
                        //Log.v(TAG, "Packet received " + packet.getLength());
                        parseNAL(packet);
                        NALList.clear();
                    }
                }

                int width = 1920;
                int height = 1080;
                String videoFormat = "video/avc";
                MediaFormat format = MediaFormat.createVideoFormat(videoFormat, width, height);
                format.setString("KEY_MIME", videoFormat);
                format.setByteBuffer("csd-0", ByteBuffer.wrap(SPSBuffer));
                format.setByteBuffer("csd-1", ByteBuffer.wrap(PPSBuffer));

                String codecName = AvcCodecInfoes.get(0).getName();
                Log.v(TAG, "Create codec " + codecName);
                final MediaCodec decoder = MediaCodec.createByCodecName(codecName);

                decoder.configure(format, mHolder.getSurface(), null, 0);
                decoder.setVideoScalingMode(MediaCodec.VIDEO_SCALING_MODE_SCALE_TO_FIT);
                decoder.start();

                boolean waitNextIDR = false;
                long consecutiveStalls = 0;
                boolean firstFrame = true;
                long startTimestamp = 0;
                long frameNumber = 0;
                long prevResetTimestamp = System.nanoTime() / 1000;

                MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();

                long prevTimestamp = -1;
                while (true) {
                    if (stopped) {
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

                        byte[] buf = recvNextNAL();
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

                                if(waitNextIDR) {
                                    if (SPSBuffer != null)
                                        buffer.put(SPSBuffer);
                                    buffer.put(buf);

                                    waitNextIDR = false;
                                    Log.v(TAG, "Sending Codec Config. Size: " + buffer.position());
                                    decoder.queueInputBuffer(inIndex, 0, buffer.position(), 0, MediaCodec.BUFFER_FLAG_CODEC_CONFIG);
                                }else {
                                    decoder.queueInputBuffer(inIndex, 0, buffer.position(), 0, 0);
                                }
                            } else if (NALType == 5) {
                                // IDR
                                Log.v(TAG, "Sending IDR SPS:" + SPSBuffer.length + " PPS:" + PPSBuffer.length + " IDR:" + buf.length);

                                //buffer.put(buf);
                                replaceNAL3To4(buffer, ByteBuffer.wrap(buf));
                                if (firstFrame) {
                                    firstFrame = false;
                                    startTimestamp = timestamp;
                                }

                                frameNumber++;


                                decoder.queueInputBuffer(inIndex, 0, buffer.position(), timestamp, 0);
                                //decoder.queueInputBuffer(inIndex, 0, buffer.position(), startTimestamp, 0);
                                prevTimestamp = timestamp;
                            } else {
                                //buffer.put(buf);
                                replaceNAL3To4(buffer, ByteBuffer.wrap(buf));
                                sampleSize += buffer.position();

                                if ((buf[4] & 0x1F) == 1) {
                                    // PFrame
                                    countPFrame();
                                }
                                startTimestamp += 16666;
                                frameNumber++;

                                if(waitNextIDR) {
                                    // Ignore P-Frame until next I-Frame
                                    Log.v(TAG, "Ignoring P-Frame");
                                    decoder.queueInputBuffer(inIndex, 0, 0, timestamp, 0);
                                }else {
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
                        countStall();
                        consecutiveStalls++;
                        //Log.v(TAG, "stalled " + consecutiveStalls);
                        if (consecutiveStalls > 100 && prevResetTimestamp < System.nanoTime() / 1000 - 1000 * 1000 * 10) {
                            // Codec input stalled. Try reset.
                            Log.v(TAG, "Codec input stalled. Try reset.");
                            waitNextIDR = true;
                            flushPacketQueue();
                            flushNALQueue();

                            prevResetTimestamp = System.nanoTime() / 1000;
                            if(false) {
                                decoder.reset();

                                flushPacketQueue();
                                flushNALQueue();
                                while (true) {
                                    byte[] buf = peekNextNAL();
                                    if (buf != null) {
                                        int NALType = buf[4] & 0x1F;
                                        if (NALType == 7) {
                                            // SPS
                                            Log.v(TAG, "Found next SPS");
                                            SPSBuffer = recvNextNAL();
                                        } else if (NALType == 8) {
                                            format = MediaFormat.createVideoFormat(videoFormat, width, height);
                                            format.setString("KEY_MIME", videoFormat);
                                            format.setByteBuffer("csd-0", ByteBuffer.wrap(SPSBuffer));
                                            format.setByteBuffer("csd-1", ByteBuffer.wrap(buf));
                                            recvNextNAL();

                                            decoder.configure(format, mHolder.getSurface(), null, 0);
                                            decoder.setVideoScalingMode(MediaCodec.VIDEO_SCALING_MODE_SCALE_TO_FIT);
                                            decoder.start();
                                            break;
                                        } else {
                                            // Drop NAL
                                            recvNextNAL();
                                        }
                                    }
                                }
                            }else{
                                decoder.flush();
                                //decoder.start();
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
                                countPacket(0, 1);

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
                            //try {                                Thread.sleep(10000);                            } catch (InterruptedException e) {                                e.printStackTrace();                            }
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

    private void findSPSPPS() {
        for (byte[] buf : NALList) {
            if (buf.length >= 5) {
                int NALType = buf[4] & 0x1F;
                if (NALType == 7) {
                    // SPS
                    SPSBuffer = buf;
                    if (PPSBuffer != null) {
                        return;
                    }
                } else if (NALType == 8) {
                    PPSBuffer = buf;
                    if (SPSBuffer != null) {
                        return;
                    }
                }
            }
        }
        NALList.clear();
    }

    private class ReceiverThread extends Thread {
        @Override
        public void run() {
            try {
                DatagramSocket socket = new DatagramSocket(9944);


                byte[][] buffers = new byte[3000][];
                for (int i = 0; i < 3000; i++) {
                    buffers[i] = new byte[2000];
                }

                int prevCounter = -1;
                for (int i = 0; ; i++) {
                    if (stopped) {
                        break;
                    }

                    byte[] packetBuffer = buffers[i % 3000];
                    DatagramPacket packet = new DatagramPacket(packetBuffer, packetBuffer.length);
                    socket.receive(packet);

                    int counter = ByteBuffer.wrap(packetBuffer, 0, 4).order(ByteOrder.LITTLE_ENDIAN).getInt();

//                    int counter = packetBuffer[0] | packetBuffer[1] << 8 | packetBuffer[2] << 16 | packetBuffer[3] << 24;
                    if(prevCounter + 1 != counter) {
                        Log.v(TAG, "packet Dropped from " + prevCounter + " to " + counter + " (" + (counter - (prevCounter + 1)) + ")");
                    }
                    prevCounter = counter;

                    countPacket(packet.getLength() - 4, 0);

                    int queueSize = 0;
                    synchronized (packetQueue) {
                        if (packetQueue.size() < 2003) {
                            ByteBuffer queueBuffer = ByteBuffer.wrap(packetBuffer, 4, packet.getLength() - 4);
                            packetQueue.add(queueBuffer);
                        } else {
                            Log.e(TAG, "packetQueue too long!");
                            packetQueue.clear();
                        }
                        queueSize = packetQueue.size();
                    }
                    if (queueSize % 1000 == 0) {
                        Log.v(TAG, "queueSize " + queueSize);
                    }
                }

            } catch (IOException e) {
            }

            Log.v(TAG, "ReceiverThread stopped.");
        }
    }

    private class ReplayReceiverThread extends ReceiverThread {
        @Override
        public void run() {
            ServerSocket socket = null;
            try {
                socket = new ServerSocket(9944);
                socket.setSoTimeout(100);
                socket.setReuseAddress(true);

                Socket client;
                while (true) {
                    if (stopped) {
                        throw new InterruptedException();
                    }
                    try {
                        client = socket.accept();
                        break;
                    } catch (SocketTimeoutException e) {
                    }
                }
                socket.close();
                socket = null;

                Log.v(TAG, "Accept Replay Socket");
                InputStream inputStream = client.getInputStream();

                byte[][] buffers = new byte[3000][];
                for (int i = 0; i < 3000; i++) {
                    buffers[i] = new byte[2000];
                }
                for (int i = 0; ; i++) {
                    if (stopped) {
                        break;
                    }

                    byte[] packetBuffer = buffers[i % 3000];

                    int ret = inputStream.read(packetBuffer);
                    if (ret == -1) {
                        break;
                    }
                    countPacket(ret, 0);

                    int queueSize = 0;
                    synchronized (packetQueue) {
                        if (packetQueue.size() < 2103) {
                            packetQueue.add(ByteBuffer.wrap(packetBuffer, 0, ret));
                        } else {
                            Log.e(TAG, "packetQueue too long!");
                            //packetQueue.clear();
                        }
                        queueSize = packetQueue.size();
                    }

                    if (queueSize > 2000) {
                        Log.e(TAG, "sleep packetQueue");
                        while (packetQueue.size() > 2000) {
                            if (stopped) {
                                break;
                            }
                            Thread.sleep(5);
                        }
                    }
                    if (queueSize % 100 == 0) {
                        //Log.v(TAG, "queueSize " + queueSize);
                    }
                }

            } catch (IOException | InterruptedException e) {
                e.printStackTrace();
            }

            if (socket != null) {
                try {
                    socket.close();
                } catch (IOException e) {
                    e.printStackTrace();
                }
            }

            Log.v(TAG, "ReplayReceiverThread stopped.");
        }
    }
}
