package com.polygraphene.alvr;

import android.graphics.SurfaceTexture;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.Surface;
import android.view.View;

import com.google.vr.ndk.base.GvrLayout;

/**
 * Activity used when running on Daydream
 */
public class GvrActivity extends BaseActivity {
    private static final String TAG = "GvrActivity";

    private GvrLayout mGvrLayout;
    private GLSurfaceView mSurfaceView;
    private GvrRenderer mRenderer;

    private DecoderThread mDecoderThread;
    private UdpReceiverThread mReceiverThread;

    private boolean mResumed = false;
    private boolean mThreadStarted = false;
    private boolean mSurfaceCreated = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // mSurfaceView is what actually performs the rendering.
        mSurfaceView = new GLSurfaceView(this);
        mSurfaceView.setEGLContextClientVersion(2);
        mSurfaceView.setEGLConfigChooser(8, 8, 8, 0, 0, 0);

        // This is the interface to the GVR APIs. It also draws the X & Gear icons.
        mGvrLayout = new GvrLayout(this);
        mGvrLayout.setPresentationView(mSurfaceView);
        mGvrLayout.setAsyncReprojectionEnabled(true);
        setContentView(mGvrLayout);

        // Bind a standard Android Renderer.
        mRenderer = new GvrRenderer(this, mGvrLayout.getGvrApi());
        mRenderer.setOnSurfaceCreatedListener(new Runnable() {
            @Override
            public void run() {
                mSurfaceCreated = true;
                resumeThreads();
            }
        });
        mSurfaceView.setRenderer(mRenderer);
    }

    // Notify components when there are lifecycle changes.
    @Override
    protected void onResume() {
        Log.v(TAG, "onResume");

        super.onResume();
        mGvrLayout.onResume();
        mSurfaceView.onResume();

        // Go fullscreen. Depending on the lifecycle of your app, you may need to do this in a different
        // place.
        getWindow()
            .getDecorView()
            .setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);

        mResumed = true;
        resumeThreads();
    }

    /**
     * Called from onResume or when surface has created. To feed surface to decoder, we must wait for surface prepared.
     */
    private void resumeThreads(){
        if(!mResumed || !mSurfaceCreated) {
            return;
        }
        if(mThreadStarted) {
            return;
        }
        Log.v(TAG, "resumeThreads");
        mThreadStarted = true;

        mReceiverThread = new UdpReceiverThread(mUdpReceiverCallback);

        ConnectionStateHolder.ConnectionState connectionState = new ConnectionStateHolder.ConnectionState();
        ConnectionStateHolder.loadConnectionState(this, connectionState);

        if(connectionState.serverAddr != null && connectionState.serverPort != 0) {
            Log.v(TAG, "load connection state: " + connectionState.serverAddr + " " + connectionState.serverPort);
            mReceiverThread.recoverConnectionState(connectionState.serverAddr, connectionState.serverPort);
        }

        mDecoderThread = new DecoderThread(mReceiverThread, mRenderer.getSurface(), this);

        try {
            mDecoderThread.start();

            int[] refreshRates = new int[] {0, 0, 0, 0};
            refreshRates[0] = getRefreshRate();

            if (!mReceiverThread.start(mRenderer.getEGLContext(), this, refreshRates, 0)) {
                Log.e(TAG, "FATAL: Initialization of ReceiverThread failed.");
                return;
            }
        } catch (IllegalArgumentException | IllegalStateException | SecurityException e) {
            e.printStackTrace();
        }

        mRenderer.onResume(mReceiverThread, mDecoderThread);

        Log.v(TAG, "resumeThreads: Resume done.");
    }

    /**
     * Detect supported refresh rate on the device.
     * @return refresh rate in Hz.
     */
    private int getRefreshRate() {
        Log.v(TAG, "Checking device model. MANUFACTURER=" + Build.MANUFACTURER + " MODEL=" + Build.MODEL);
        if(Build.MANUFACTURER.equals("Lenovo") && Build.MODEL.equals("VR-1541F")) {
            Log.v(TAG, "Lenovo Mirage Solo is detected. Assume refresh rate is 75Hz.");
            return 75;
        } else {
            Log.v(TAG, "General Daydream device is detected. Assume refresh rate is 60Hz.");
            return 60;
        }
    }

    @Override
    protected void onPause() {
        Log.v(TAG, "onPause");
        mResumed = false;
        mThreadStarted = false;

        super.onPause();
        mGvrLayout.onPause();
        // Must call Renderer.onPause() before SurfaceView.onPause().
        // Because SurfaceView.onPause() wait for the exit of Renderer.onDraw() call.
        mRenderer.onPause();
        Log.v(TAG, "Call mSurfaceView.onPause");
        mSurfaceView.onPause();

        // Stop worker threads.
        Log.v(TAG, "VrThread.onPause: Stopping worker threads.");
        // DecoderThread must be stopped before ReceiverThread and setting mResumed=false.
        if (mDecoderThread != null) {
            Log.v(TAG, "VrThread.onPause: Stopping DecoderThread.");
            mDecoderThread.stopAndWait();
        }
        if (mReceiverThread != null) {
            Log.v(TAG, "VrThread.onPause: Stopping ReceiverThread.");
            mReceiverThread.stopAndWait();
        }
    }

    @Override
    protected void onStart() {
        Log.v(TAG, "onStart");
        super.onStart();
        mRenderer.start();
    }

    @Override
    protected void onStop() {
        Log.v(TAG, "onStop");
        super.onStop();
        mRenderer.shutdown();
    }

    private UdpReceiverThread.Callback mUdpReceiverCallback = new UdpReceiverThread.Callback() {
        @Override
        public void onConnected(final int width, final int height, final int codec, final int frameQueueSize, final int refreshRate) {
            // We must wait completion of notifyGeometryChange
            // to ensure the first video frame arrives after notifyGeometryChange.
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    // TODO: Can we change refresh rate on Daydream?

                    //mRenderer.setFrameGeometry(width, height);
                    mDecoderThread.onConnect(codec, frameQueueSize);
                }
            });
        }

        @Override
        public void onChangeSettings(int enableTestMode, int suspend, int frameQueueSize) {
            mDecoderThread.setFrameQueueSize(frameQueueSize);
        }

        @Override
        public void onShutdown(String serverAddr, int serverPort) {
            ConnectionStateHolder.saveConnectionState(GvrActivity.this, serverAddr, serverPort);
        }

        @Override
        public void onDisconnect() {
            mDecoderThread.onDisconnect();
        }

        @Override
        public void onTracking(float[] position, float[] orientation) {
        }
    };
}