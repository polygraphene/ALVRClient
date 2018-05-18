package com.polygraphene.remoteglass;

import android.app.Activity;
import android.graphics.SurfaceTexture;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.os.Bundle;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.Window;
import android.view.WindowManager;

import java.io.IOException;
import java.io.InputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;

public class MainActivity extends Activity {
    private final static String TAG = "MainActivity";
    private SurfaceView mSurfaceView;
    private SurfaceHolder mHolder;
    private final SurfaceHolder.Callback mCallback = new VideoSurface();
    private List<MediaCodecInfo> AvcCodecInfoes = new ArrayList<>();


    private boolean surfaceCreated = false;
    private boolean stopped = false;
    NALParser nalParser;
    StatisticsCounter counter = new StatisticsCounter();

    private DecoderThread decoderThread;
    //private ReceiverThread receiverThread = new ReceiverThread();
    private UdpReceiverThread receiverThread;

    SurfaceTexture surfaceTexture;
    Surface surface;

    boolean rendered = false;
    boolean renderRequesed = false;
    long frameIndex = 0;
    Object waiter = new Object();

    private VrAPI vrAPI = new VrAPI();

    public Surface getSurface() {
        return surface;
    }

    public boolean isStopped() {
        return stopped;
    }

    public int renderIf(MediaCodec codec, int queuedOutputBuffer, long frameIndex) {
        //Log.v(TAG, "renderIf " + queuedOutputBuffer);
        synchronized (waiter) {
            if (!renderRequesed) {
                return queuedOutputBuffer;
            }
        }

        if(queuedOutputBuffer == -1) {
            return queuedOutputBuffer;
        }

        Log.v(TAG, "releaseOutputBuffer " + frameIndex);
        codec.releaseOutputBuffer(queuedOutputBuffer, true);
        synchronized (waiter) {
            //rendered = true;
            this.frameIndex = frameIndex;
            //waiter.notifyAll();
        }
        return -1;
    }

    public void onChangeSettings(int enableTestMode) {
        Log.v(TAG, "onChangeSettings " + enableTestMode);
        vrAPI.onChangeSettings(enableTestMode);
    }

    public interface VrFrameCallback {
        void onSendTracking(byte[] buf, int len, long frame);
        long waitFrame();
    }

    private class VideoSurface implements SurfaceHolder.Callback {

        @Override
        public void surfaceCreated(final SurfaceHolder holder) {
            Thread th = new Thread() {
                @Override
                public void run() {
                    setName("VR-Thread");

                    vrAPI.onSurfaceCreated(holder.getSurface(), MainActivity.this);
                    surfaceTexture = new SurfaceTexture(vrAPI.getSurfaceTextureID());
                    surfaceTexture.setOnFrameAvailableListener(new SurfaceTexture.OnFrameAvailableListener() {
                        @Override
                        public void onFrameAvailable(SurfaceTexture surfaceTexture) {
                            Log.v(TAG, "onFrameAvailable " + frameIndex);

                            synchronized (waiter) {
                                renderRequesed = false;
                                rendered = true;
                                waiter.notifyAll();
                            }
                        }
                    });
                    surface = new Surface(surfaceTexture);
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {

                            try {
                                decoderThread.start();
                                receiverThread.start();
                            } catch (IllegalArgumentException | IllegalStateException | SecurityException e) {
                                e.printStackTrace();
                            }
                        }
                    });

                    while (true) {
                        vrAPI.render(new VrFrameCallback() {
                            @Override
                            public void onSendTracking(byte[] buf, int len, long frame) {
                                Log.v(TAG, "sending " + len + " fr:" + frame);
                                receiverThread.send(buf, len);
                            }

                            @Override
                            public long waitFrame(){
                                synchronized (waiter) {
                                    if(rendered) {
                                        Log.v(TAG, "updateTexImage(discard)");
                                        surfaceTexture.updateTexImage();
                                    }
                                    Log.v(TAG, "waitFrame Enter");
                                    renderRequesed = true;
                                    rendered = false;
                                }
                                while(true){
                                    synchronized (waiter){
                                        if(rendered){
                                            Log.v(TAG, "waited:" + frameIndex);
                                            surfaceTexture.updateTexImage();
                                            break;
                                        }
                                        try {
                                            Log.v(TAG, "waiting");
                                            waiter.wait();
                                        } catch (InterruptedException e) {
                                            e.printStackTrace();
                                        }
                                    }
                                }

                                return frameIndex;
                            }
                        });
                    }
                }
            };
            th.start();

        }

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {

        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            vrAPI.onSurfaceDestroyed();
        }

    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);


        // Force the screen to stay on, rather than letting it dim and shut off
        // while the user is watching a movie.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        requestWindowFeature(Window.FEATURE_NO_TITLE);

        setContentView(R.layout.activity_main);
        mSurfaceView = findViewById(R.id.surfaceview);

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
        if (decoderThread != null) {
            decoderThread.interrupt();
            try {
                decoderThread.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
        if (receiverThread != null) {
            receiverThread.interrupt();
            try {
                receiverThread.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
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

        nalParser = new NALParser();


        //receiverThread = new ReceiverThread(nalParser, counter, this);
        receiverThread = new UdpReceiverThread(nalParser, counter, this);
        //receiverThread = new ReplayReceiverThread();
        receiverThread.setHost("10.1.0.2", 9944);
        decoderThread = new DecoderThread(this, receiverThread, AvcCodecInfoes.get(0));

        if (surfaceCreated) {
            try {
                decoderThread.start();
                receiverThread.start();
            } catch (IllegalArgumentException | IllegalStateException | SecurityException e) {
                e.printStackTrace();
            }
        }
    }


    static class ReceiverThread extends Thread {
        NALParser nalParser;
        StatisticsCounter counter;
        String host;
        int port;
        MainActivity mainActivity;

        ReceiverThread(NALParser nalParser, StatisticsCounter counter, MainActivity activity) {
            this.nalParser = nalParser;
            this.counter = counter;
            this.mainActivity = activity;
        }

        public void setHost(String host, int port) {
            this.host = host;
            this.port = port;
        }

        @Override
        public void run() {
            try {
                DatagramSocket socket = new DatagramSocket();

                final int PACKET_BUFFER = 3000;
                final int MAX_PACKET_LENGTH = 2000;

                byte[][] buffers = new byte[PACKET_BUFFER][];
                DatagramPacket[] packets = new DatagramPacket[PACKET_BUFFER];

                for (int i = 0; i < PACKET_BUFFER; i++) {
                    buffers[i] = new byte[MAX_PACKET_LENGTH];
                    packets[i] = new DatagramPacket(buffers[i], buffers[i].length);
                }

                InetSocketAddress serverAddress = new InetSocketAddress(host, port);

                socket.send(new DatagramPacket(new byte[1], 1, serverAddress));

                int previousSequence = -1;
                for (int i = 0; ; i++) {
                    if (mainActivity.isStopped()) {
                        break;
                    }

                    byte[] packetBuffer = buffers[i % PACKET_BUFFER];
                    DatagramPacket packet = packets[i % PACKET_BUFFER];
                    socket.receive(packet);

                    int sequence = ByteBuffer.wrap(packetBuffer, 0, 4)
                            .order(ByteOrder.LITTLE_ENDIAN)
                            .getInt();

                    if (previousSequence + 1 != sequence) {
                        Log.v(TAG, "packet Dropped from " + previousSequence + " to " + sequence + " (" + (sequence - (previousSequence + 1)) + ")");
                    }
                    previousSequence = sequence;

                    counter.countPacket(packet.getLength() - 4);

                    int queueSize = nalParser.queuePacket(ByteBuffer.wrap(packetBuffer, 4, packet.getLength() - 4));
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
        ReplayReceiverThread(NALParser nalParser, StatisticsCounter counter, MainActivity activity) {
            super(nalParser, counter, activity);
        }

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
                    counter.countPacket(ret);

                    int queueSize = 0;
                    nalParser.queuePacket(ByteBuffer.wrap(packetBuffer, 0, ret));

                    if (queueSize > 2000) {
                        Log.e(TAG, "sleep packetQueue");
                        while (nalParser.getQueueSize() > 2000) {
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
