package com.polygraphene.alvr;

import android.media.MediaCodec;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.support.annotation.MainThread;
import android.util.Log;
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
        mRenderer = new GvrRenderer(this, mGvrLayout.getGvrApi(), mSurfaceView);
        mRenderer.setRendererCallback(mRendererCallback);
        mSurfaceView.setRenderer(mRenderer);
    }

    // Notify components when there are lifecycle changes.
    @Override
    protected void onResume() {
        Log.v(TAG, "onResume: enter");

        mResumed = true;

        super.onResume();
        mGvrLayout.onResume();
        mSurfaceView.onResume();
        mRenderer.onResume();

        if (mSurfaceCreated) {
            startWorkerThreads();
        }

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
    }

    @MainThread
    private void startWorkerThreads() {
        Log.v(TAG, "startWorkerThreads: enter.");
        mReceiverThread = new UdpReceiverThread(mUdpReceiverCallback);

        ConnectionStateHolder.ConnectionState connectionState = new ConnectionStateHolder.ConnectionState();
        ConnectionStateHolder.loadConnectionState(GvrActivity.this, connectionState);

        if(connectionState.serverAddr != null && connectionState.serverPort != 0) {
            Log.v(TAG, "Load connection state: " + connectionState.serverAddr + " " + connectionState.serverPort);
            mReceiverThread.recoverConnectionState(connectionState.serverAddr, connectionState.serverPort);
        }

        mDecoderThread = new DecoderThread(mReceiverThread, mRenderer.getSurface(), GvrActivity.this, mDecoderCallback);

        try {
            mDecoderThread.start();

            if (!mReceiverThread.start(mRenderer.getEGLContext(), GvrActivity.this, mRenderer.getDeviceDescriptor(), 0, mDecoderThread)) {
                Log.e(TAG, "FATAL: Initialization of ReceiverThread failed.");
                return;
            }
        } catch (IllegalArgumentException | IllegalStateException | SecurityException e) {
            e.printStackTrace();
        }

        mRenderer.setThreads(mReceiverThread, mDecoderThread);

        Log.v(TAG, "startWorkerThreads: Done.");
    }

    @Override
    protected void onPause() {
        Log.v(TAG, "onPause: enter. waitForSurfacePrepared.");

        super.onPause();

        mResumed = false;

        mGvrLayout.onPause();

        // Stop worker threads.
        Log.v(TAG, "VrThread.onPause: Stopping worker threads.");
        // DecoderThread must be stopped before ReceiverThread and setting mResumed=false.
        if (mDecoderThread != null) {
            Log.v(TAG, "VrThread.onPause: Stopping DecoderThread.");
            mDecoderThread.stopAndWait();
            mDecoderThread = null;
        }
        if (mReceiverThread != null) {
            Log.v(TAG, "VrThread.onPause: Stopping ReceiverThread.");
            mReceiverThread.stopAndWait();
            mReceiverThread = null;
        }

        // Must call Renderer.onPause() before SurfaceView.onPause().
        // Because SurfaceView.onPause() wait for the exit of Renderer.onDraw() call.
        mRenderer.onPause();

        Log.v(TAG, "Call mSurfaceView.onPause");
        mSurfaceView.onPause();

        Log.v(TAG, "onPause: exit");
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
        Log.v(TAG, "onStop: exit");
    }

    private GvrRenderer.RendererCallback mRendererCallback = new GvrRenderer.RendererCallback() {
        @Override
        public void onSurfaceCreated() {
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    mSurfaceCreated = true;
                    Log.v(TAG, "startWorkerThreads");
                    if (!mResumed) {
                        return;
                    }

                    startWorkerThreads();
                }
            });
        }

        @Override
        public void onSurfaceDestroyed() {
            mSurfaceCreated = false;
        }
    };

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
                    if(mDecoderThread != null) {
                        mDecoderThread.onConnect(codec, frameQueueSize);
                    }
                }
            });
        }

        @Override
        public void onChangeSettings(int enableTestMode, int suspend, int frameQueueSize) {
        }

        @Override
        public void onShutdown(String serverAddr, int serverPort) {
            ConnectionStateHolder.saveConnectionState(GvrActivity.this, serverAddr, serverPort);
        }

        @Override
        public void onDisconnect() {
            if(mDecoderThread != null) {
                mDecoderThread.onDisconnect();
            }
        }

        @Override
        public void onTracking(float[] position, float[] orientation) {
        }
    };

    private DecoderThread.DecoderCallback mDecoderCallback = new DecoderThread.DecoderCallback() {
        @Override
        public void onPrepared() {
            mReceiverThread.setSinkPrepared(true);
        }

        @Override
        public void onDestroy() {
            mReceiverThread.setSinkPrepared(false);
        }

        @Override
        public void onFrameDecoded() {
            mRenderer.onFrameDecoded(mSurfaceView);
        }
    };
}