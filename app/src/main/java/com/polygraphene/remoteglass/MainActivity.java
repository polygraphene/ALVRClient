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

import java.util.ArrayList;
import java.util.List;

public class MainActivity extends Activity {
    private final static String TAG = "MainActivity";

    private SurfaceView mSurfaceView;
    private SurfaceHolder mHolder;
    private final SurfaceHolder.Callback mCallback = new VideoSurface();
    private List<MediaCodecInfo> mAvcCodecInfoes = new ArrayList<>();

    private boolean mSurfaceCreated = false;
    private boolean mStopped = false;
    StatisticsCounter mCounter = new StatisticsCounter();

    private DecoderThread mDecoderThread;
    private UdpReceiverThread mReceiverThread;
    private TrackingThread mTrackingThread;

    SurfaceTexture surfaceTexture;
    Surface surface;

    boolean rendered = false;
    boolean renderRequested = false;
    long frameIndex = 0;
    final Object waiter = new Object();

    private VrAPI vrAPI = new VrAPI();

    public Surface getSurface() {
        return surface;
    }

    public boolean isStopped() {
        return mStopped;
    }

    public int renderIf(MediaCodec codec, int queuedOutputBuffer, long frameIndex) {
        //Log.v(TAG, "renderIf " + queuedOutputBuffer);
        synchronized (waiter) {
            if (!renderRequested) {
                return queuedOutputBuffer;
            }
        }

        if (queuedOutputBuffer == -1) {
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

    public void onChangeSettings(int enableTestMode, int suspend) {
        Log.v(TAG, "onChangeSettings " + enableTestMode + " suspend:" + suspend);
        vrAPI.onChangeSettings(enableTestMode, suspend);
    }

    public interface OnSendTrackingCallback {
        void onSendTracking(byte[] buf, int len, long frame);
    }

    public interface VrFrameCallback {
        long waitFrame();
    }

    private class VideoSurface implements SurfaceHolder.Callback {

        @Override
        public void surfaceCreated(final SurfaceHolder holder) {
            VrThread vrThread = new VrThread();
            // Start rendering.
            vrThread.start();

            mSurfaceCreated = true;
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
                /*
                Log.v(TAG, info.getName());
                for (MediaCodecInfo.CodecProfileLevel profile : capabilitiesForType.profileLevels) {
                    Log.v(TAG, "profile:" + profile.profile + " level:" + profile.level);
                }*/

                mAvcCodecInfoes.add(info);
            }
        }

    }

    @Override
    protected void onStop() {
        super.onStop();

        mStopped = true;
        if (mReceiverThread != null) {
            mReceiverThread.interrupt();
            try {
                mReceiverThread.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
        if (mDecoderThread != null) {
            mDecoderThread.interrupt();
            try {
                mDecoderThread.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
        if (mTrackingThread != null) {
            mTrackingThread.interrupt();
            try {
                mTrackingThread.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
        mReceiverThread = null;
        mDecoderThread = null;
        mTrackingThread = null;
    }

    @Override
    protected void onResume() {
        super.onResume();

        if (mReceiverThread != null) {
            mReceiverThread.interrupt();
        }
        if (mDecoderThread != null) {
            mDecoderThread.interrupt();
        }
        if (mTrackingThread != null) {
            mTrackingThread.interrupt();
        }

        mReceiverThread = new UdpReceiverThread(mCounter, this);
        mReceiverThread.setHost("10.1.0.2", 9944);
        mDecoderThread = new DecoderThread(this, mReceiverThread, mAvcCodecInfoes.get(0), mCounter);
        mTrackingThread = new TrackingThread();

        if (mSurfaceCreated) {
            try {
                mDecoderThread.start();
                mReceiverThread.start();
            } catch (IllegalArgumentException | IllegalStateException | SecurityException e) {
                e.printStackTrace();
            }
        }
    }

    class VrThread extends Thread {
        @Override
        public void run() {
            setName("VR-Thread");

            vrAPI.onSurfaceCreated(mHolder.getSurface(), MainActivity.this);

            surfaceTexture = new SurfaceTexture(vrAPI.getSurfaceTextureID());
            surfaceTexture.setOnFrameAvailableListener(new SurfaceTexture.OnFrameAvailableListener() {
                @Override
                public void onFrameAvailable(SurfaceTexture surfaceTexture) {
                    Log.v(TAG, "onFrameAvailable " + frameIndex);

                    synchronized (waiter) {
                        renderRequested = false;
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
                        mDecoderThread.start();
                        if(!mReceiverThread.start()) {
                            Log.e(TAG, "FATAL: Initialization of ReceiverThread failed.");
                            return;
                        }
                        // TrackingThread relies on ReceiverThread.
                        mTrackingThread.start();
                    } catch (IllegalArgumentException | IllegalStateException | SecurityException e) {
                        e.printStackTrace();
                    }
                }
            });

            while (!isStopped()) {
                vrAPI.render(new VrFrameCallback() {


                    @Override
                    public long waitFrame() {
                        synchronized (waiter) {
                            if (rendered) {
                                Log.v(TAG, "updateTexImage(discard)");
                                surfaceTexture.updateTexImage();
                            }
                            Log.v(TAG, "waitFrame Enter");
                            renderRequested = true;
                            rendered = false;
                        }
                        while (true) {
                            synchronized (waiter) {
                                if (rendered) {
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
    }

    class TrackingThread extends Thread {
        @Override
        public void run() {
            while(!isStopped()) {
                vrAPI.fetchTrackingInfo(new OnSendTrackingCallback() {
                    @Override
                    public void onSendTracking(byte[] buf, int len, long frame) {
                        Log.v(TAG, "sending " + len + " fr:" + frame);
                        mReceiverThread.send(buf, len);
                    }
                });
                try {
                    Thread.sleep(16);
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }
    }
}
