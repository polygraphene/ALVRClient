package com.polygraphene.alvr;

import android.content.res.AssetManager;
import android.graphics.Point;
import android.graphics.RectF;
import android.graphics.SurfaceTexture;
import android.media.MediaCodec;
import android.opengl.EGL14;
import android.opengl.EGLContext;
import android.opengl.GLSurfaceView;
import android.opengl.Matrix;
import android.util.Log;
import android.util.LongSparseArray;
import android.view.Surface;

import com.google.vr.ndk.base.BufferSpec;
import com.google.vr.ndk.base.BufferViewport;
import com.google.vr.ndk.base.BufferViewportList;
import com.google.vr.ndk.base.Frame;
import com.google.vr.ndk.base.GvrApi;
import com.google.vr.ndk.base.SwapChain;

import java.util.Arrays;
import java.util.concurrent.TimeUnit;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Main render loop. This is a mix of Java and native code.
 */
public class GvrRenderer implements GLSurfaceView.Renderer {
    private static final String TAG = "GvrRenderer";
    static {
        System.loadLibrary("native-lib");
    }

    private final GvrActivity mActivity;
    private final GvrApi mApi;
    private final GLSurfaceView mSurfaceView;
    private GvrTracking mGvrTracking;

    private UdpReceiverThread mReceiverThread;
    private DecoderThread mDecoderThread;

    // Manages a queue of frames that our app renders to.
    private SwapChain mSwapChain;
    // Represents the 2D & 3D aspects of each frame that we care about when rendering.
    private BufferViewportList mViewportList;
    // Temp used while rendering
    private BufferViewport mTmpViewport;
    // Size of the frame that our app renders to.
    private final Point mTargetSize = new Point();

    // HMD device information which is sent to server.
    private DeviceDescriptor mDeviceDescriptor = new DeviceDescriptor();

    // Variables for onDrawFrame().
    private final float[] mHeadFromWorld = new float[16];
    private final float[] mEyeFromHead = new float[16];
    private final float[] mEyeFromWorld = new float[16];
    private final RectF mEyeFov = new RectF();
    private final float[] mEyePerspective = new float[16];
    private final float[] mLeftMvp = new float[16];
    private final float[] mRightMvp = new float[16];
    private final int[] mLeftViewport = new int[4];
    private final int[] mRightViewport = new int[4];

    private final RectF mEyeUv = new RectF();

    // Variables for sendTracking().
    private final float[] mTmpLeftEyePerspective = new float[16];

    private final float[] invRotationMatrix = new float[16];
    private final float[] translationMatrix = new float[16];

    private final float[] mTmpHeadPosition = new float[3];
    private final float[] mTmpHeadOrientation = new float[4];

    private final float[] mTmpMVPMatrix = new float[16];
    private final float[] mTmpEyeFromHead = new float[16];
    private final float[] mTmpEyeFromWorld = new float[16];
    private final float[] mHeadFromWorld2 = new float[16];

    private long currentFrameIndex = 0;
    private LongSparseArray<float[]> mFrameMap = new LongSparseArray<>();

    private final LoadingTexture mLoadingTexture = new LoadingTexture();

    private SurfaceTexture mSurfaceTexture;
    private Surface mSurface;
    private EGLContext mEglContext;

    public interface RendererCallback {
        void onSurfaceCreated();
        void onSurfaceDestroyed();
    }
    private RendererCallback mRendererCallback;

    private long mRenderedFrameIndex = -1;

    // Native pointer to GvrRenderer
    private long nativeHandle;

    public GvrRenderer(GvrActivity activity, GvrApi api, GLSurfaceView surfaceView) {
        this.mActivity = activity;
        this.mApi = api;
        this.mSurfaceView = surfaceView;

        mGvrTracking = new GvrTracking(mApi.getNativeGvrContext());
    }

    public void setRendererCallback(RendererCallback callback) {
        mRendererCallback = callback;
    }

    public void start() {
        mSurfaceView.queueEvent(new Runnable() {
            @Override
            public void run() {
                Utils.logi(TAG, "start");
                // Initialize native objects. It's important to call .shutdown to release resources.
                mViewportList = mApi.createBufferViewportList();
                mTmpViewport = mApi.createBufferViewport();
                nativeHandle = createNative(mActivity.getAssets());
            }
        });
    }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        // Called from GLThread
        Utils.logi(TAG, "onSurfaceCreated");

        initializeGlObjects();

        mRendererCallback.onSurfaceCreated();
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        Utils.logi(TAG, "onSurfaceChanged");
    }

    public void onResume() {
        mSurfaceView.queueEvent(new Runnable() {
            @Override
            public void run() {
                Utils.logi(TAG, "onResume");
                mRenderedFrameIndex = -1;
            }
        });
    }

    public void setThreads(final UdpReceiverThread receiverThread, final DecoderThread decoderThread) {
        mSurfaceView.queueEvent(new Runnable() {
            @Override
            public void run() {
                Utils.logi(TAG, "setThreads");
                mReceiverThread = receiverThread;
                mDecoderThread = decoderThread;
            }
        });
    }

    void onFrameDecoded() {
        mSurfaceView.queueEvent(onFrameDecodedRunnable);
    }

    public void onPause() {
        mSurfaceView.queueEvent(new Runnable() {
            @Override
            public void run() {
                Utils.logi(TAG, "onPause");
                mReceiverThread = null;
                mDecoderThread = null;
            }
        });
    }

    public void shutdown() {
        Utils.logi(TAG, "shutdown.");
        mViewportList.shutdown();
        mViewportList = null;
        mTmpViewport.shutdown();
        mTmpViewport = null;
        if(mSwapChain != null) {
            mSwapChain.shutdown();
        }
        mSwapChain = null;
        destroyNative(nativeHandle);
        nativeHandle = 0;

        mRendererCallback.onSurfaceDestroyed();
    }

    public EGLContext getEGLContext() {
        return mEglContext;
    }

    public Surface getSurface() {
        return mSurface;
    }

    public DeviceDescriptor getDeviceDescriptor() {
        return mDeviceDescriptor;
    }

    private boolean isConnected() {
        return mReceiverThread != null && mReceiverThread.isConnected() &&
                mDecoderThread != null && !mDecoderThread.isStopped();
    }

    private Runnable onFrameDecodedRunnable = new Runnable() {
        @Override
        public void run() {
            if (isConnected()) {
                mDecoderThread.releaseBuffer();
            }
        }
    };

    private Runnable onFrameAvailableRunnable = new Runnable() {
        @Override
        public void run() {
            if (isConnected()) {
                mDecoderThread.onFrameAvailable();
            }
        }
    };

    @Override
    public void onDrawFrame(GL10 gl) {
        // Take a frame from the queue and direct all our GL commands to it. This will app rendering
        // thread to sleep until the compositor wakes it up.
        Frame frame = mSwapChain.acquireFrame();

        mApi.getHeadSpaceFromStartSpaceTransform(mHeadFromWorld, System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(5));

        // Send tracking info to the server.
        sendTracking();

        // Get the parts of each frame associated with each eye in world space. The list is
        // {left eye, right eye}.
        mApi.getRecommendedBufferViewports(mViewportList);

        // Bind to the only layer used in this frame.
        frame.bindBuffer(0);

        for (int eye = 0; eye < 2; ++eye) {
            mViewportList.get(eye, mTmpViewport);
            mTmpViewport.getSourceFov(mEyeFov);
            //Log.v(TAG, "EyeFov[" + eye + "] l:" + mEyeFov.left + ", r:" + mEyeFov.right + ", b:" + mEyeFov.bottom + ", t:" + mEyeFov.top);
            float near = 0.1f;
            float l = (float) -Math.tan(Math.toRadians(mEyeFov.left)) * near;
            float r = (float) Math.tan(Math.toRadians(mEyeFov.right)) * near;
            float b = (float) -Math.tan(Math.toRadians(mEyeFov.bottom)) * near;
            float t = (float) Math.tan(Math.toRadians(mEyeFov.top)) * near;
            Matrix.frustumM(mEyePerspective, 0, l, r, b, t, near, 30f);

            if(eye == 0) {
                System.arraycopy(mEyePerspective, 0, mTmpLeftEyePerspective, 0, 16);
            }
            mApi.getEyeFromHeadMatrix(eye, mEyeFromHead);
            Matrix.multiplyMM(mEyeFromWorld, 0, mEyeFromHead, 0, mHeadFromWorld, 0);
            Matrix.translateM(mEyeFromWorld, 0, 0, -1.5f, 0);
            Matrix.multiplyMM(eye == 0 ? mLeftMvp : mRightMvp, 0, mEyePerspective, 0, mEyeFromWorld, 0);

            int[] viewport = eye == 0 ? mLeftViewport : mRightViewport;
            mTmpViewport.getSourceUv(mEyeUv);
            viewport[0] = (int) (mEyeUv.left * mTargetSize.x);
            viewport[1] = (int) (mEyeUv.bottom * mTargetSize.y);
            viewport[2] = (int) (mEyeUv.width() * mTargetSize.x);
            viewport[3] = (int) (-mEyeUv.height() * mTargetSize.y);
        }

        long frameIndex = -1;
        if (isConnected()) {
            frameIndex = mDecoderThread.clearAvailable(mSurfaceTexture);
            if (frameIndex != -1) {
                mRenderedFrameIndex = frameIndex;
            }
            if(mRenderedFrameIndex != -1) {
                float[] matrix = mFrameMap.get(mRenderedFrameIndex);
                if (matrix != null) {
                    System.arraycopy(matrix, 0, mHeadFromWorld, 0, matrix.length);
                }
                renderNative(nativeHandle, mLeftMvp, mRightMvp, mLeftViewport, mRightViewport, false, mRenderedFrameIndex);
            } else {
                mLoadingTexture.drawMessage(Utils.getVersionName(mActivity) + "\nConnected.\nPlease wait for start.");
                renderNative(nativeHandle, mLeftMvp, mRightMvp, mLeftViewport, mRightViewport, true, 0);
            }
        } else {
            mLoadingTexture.drawMessage(Utils.getVersionName(mActivity) + "\nLoading...\nPress connect on server.");
            renderNative(nativeHandle, mLeftMvp, mRightMvp, mLeftViewport, mRightViewport, true, 0);
        }

        // Send the frame to the compositor
        frame.unbind();
        frame.submit(mViewportList, mHeadFromWorld);

        if(frameIndex >= 0) {
            LatencyCollector.Submit(frameIndex);
        }
    }

    private void initializeGlObjects() {
        Log.v(TAG, "initializeGlObjects");

        mApi.initializeGl();

        // This tends to be 50% taller & wider than the device's screen so it has 2.4x as many pixels
        // as the screen.
        mApi.getMaximumEffectiveRenderTargetSize(mTargetSize);

        // Configure the pixel buffers that the app renders to. There is only one in this sample.
        BufferSpec bufferSpec = mApi.createBufferSpec();
        bufferSpec.setSize(mTargetSize);

        // Create the queue of frames with a given spec.
        BufferSpec[] specList = {bufferSpec};
        mSwapChain = mApi.createSwapChain(specList);

        // Free this early since we no longer need it.
        bufferSpec.shutdown();

        glInitNative(nativeHandle, mTargetSize.x, mTargetSize.y);

        mLoadingTexture.initializeMessageCanvas(getLoadingTexture(nativeHandle));
        mLoadingTexture.drawMessage(Utils.getVersionName(mActivity) + "\nLoading...");

        mSurfaceTexture = new SurfaceTexture(getSurfaceTexture(nativeHandle));
        mSurfaceTexture.setOnFrameAvailableListener(new SurfaceTexture.OnFrameAvailableListener() {
            @Override
            public void onFrameAvailable(SurfaceTexture surfaceTexture) {
                Utils.log(TAG, "onFrameAvailable");
                mSurfaceView.queueEvent(onFrameAvailableRunnable);
            }
        });
        mSurface = new Surface(mSurfaceTexture);
        mEglContext = EGL14.eglGetCurrentContext();

        buildDeviceDescriptor();
    }

    private void buildDeviceDescriptor() {
        Log.v(TAG, "Checking device model. Type=" + mApi.getViewerType() + " Vendor=" +
                mApi.getViewerVendor() + " Model=" + mApi.getViewerModel() +
                " Width=" + mTargetSize.x + " Height=" + mTargetSize.y);

        mDeviceDescriptor.mRenderWidth = mTargetSize.x;
        mDeviceDescriptor.mRenderHeight = mTargetSize.y;

        mApi.getRecommendedBufferViewports(mViewportList);
        for (int eye = 0; eye < 2; ++eye) {
            mViewportList.get(eye, mTmpViewport);
            mTmpViewport.getSourceFov(mEyeFov);

            mDeviceDescriptor.mFov[eye * 4 + 0] = mEyeFov.left;
            mDeviceDescriptor.mFov[eye * 4 + 1] = mEyeFov.right;
            mDeviceDescriptor.mFov[eye * 4 + 2] = mEyeFov.top;
            mDeviceDescriptor.mFov[eye * 4 + 3] = mEyeFov.bottom;

            Log.v(TAG, "EyeFov[" + eye + "] (l,r,t,b)=(" +
                    mEyeFov.left + "," + mEyeFov.right + "," +
                    mEyeFov.top + "," + mEyeFov.bottom + ")");
        }

        mDeviceDescriptor.mDeviceCapabilityFlags = 0;
        mDeviceDescriptor.mControllerCapabilityFlags = DeviceDescriptor.ALVR_CONTROLLER_CAPABILITY_FLAG_ONE_CONTROLLER;

        if(mApi.getViewerType() == 0) {
            // GVR_VIEWER_TYPE_CARDBOARD
            mDeviceDescriptor.mDeviceType = DeviceDescriptor.ALVR_DEVICE_TYPE_CARDBOARD;
            mDeviceDescriptor.mDeviceSubType = DeviceDescriptor.ALVR_DEVICE_SUBTYPE_CARDBOARD_GENERIC;
        }else{
            // GVR_VIEWER_TYPE_DAYDREAM
            mDeviceDescriptor.mDeviceType = DeviceDescriptor.ALVR_DEVICE_TYPE_DAYDREAM;
            mDeviceDescriptor.mDeviceSubType = DeviceDescriptor.ALVR_DEVICE_SUBTYPE_DAYDREAM_GENERIC;
        }

        if(mApi.getViewerVendor().equals("Lenovo") && mApi.getViewerModel().equals("Mirage Solo")) {
            Log.v(TAG, "Lenovo Mirage Solo is detected. Assume refresh rate is 75Hz.");
            mDeviceDescriptor.mRefreshRates[0] = 75;
            mDeviceDescriptor.mDeviceSubType = DeviceDescriptor.ALVR_DEVICE_SUBTYPE_DAYDREAM_MIRAGE_SOLO;
            mDeviceDescriptor.mDeviceCapabilityFlags |= DeviceDescriptor.ALVR_DEVICE_CAPABILITY_FLAG_HMD_6DOF;
        } else {
            Log.v(TAG, "General Daydream device is detected. Assume refresh rate is 60Hz.");
            mDeviceDescriptor.mRefreshRates[0] = 60;
        }
        mDeviceDescriptor.mRefreshRates[1]
                = mDeviceDescriptor.mRefreshRates[2]
                = mDeviceDescriptor.mRefreshRates[3] = 0;
    }

    private void sendTracking() {
        if (isConnected()) {
            mApi.getHeadSpaceFromStartSpaceTransform(mHeadFromWorld2, System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(50));
            sendTrackingLocked(mHeadFromWorld2);
        }
    }

    private void sendTrackingLocked(float[] headFromWorld) {
        // Extract quaternion since that's what steam expects.
        float[] m = headFromWorld;
        float t0 = m[0] + m[5] + m[10];
        float x, y, z, w;
        if (t0 >= 0) {
            float s = (float) Math.sqrt(t0 + 1);
            w = .5f * s;
            s = .5f / s;
            x = (m[9] - m[6]) * s;
            y = (m[2] - m[8]) * s;
            z = (m[4] - m[1]) * s;
        } else if (m[0] > m[5] && m[0] > m[10]) {
            float s = (float) Math.sqrt(1 + m[0] - m[5] - m[10]);
            x = s * .5f;
            s = .5f / s;
            y = (m[4] + m[1]) * s;
            z = (m[2] + m[8]) * s;
            w = (m[9] - m[6]) * s;
        } else if (m[5] > m[10]) {
            float s = (float) Math.sqrt(1 + m[5] - m[0] - m[10]);
            y = s * .5f;
            s = .5f / s;
            x = (m[4] + m[1]) * s;
            z = (m[9] + m[6]) * s;
            w = (m[2] - m[8]) * s;
        } else {
            float s = (float) Math.sqrt(1 + m[10] - m[0] - m[5]);
            z = s * .5f;
            s = .5f / s;
            x = (m[2] + m[8]) * s;
            y = (m[9] + m[6]) * s;
            w = (m[4] - m[1]) * s;
        }


        mApi.getEyeFromHeadMatrix(0, mTmpEyeFromHead);
        Matrix.multiplyMM(mTmpEyeFromWorld, 0, mTmpEyeFromHead, 0, headFromWorld, 0);
        Matrix.translateM(mTmpEyeFromWorld, 0, 0, -1.5f, 0);
        Matrix.multiplyMM(mTmpMVPMatrix, 0, mTmpLeftEyePerspective, 0, mTmpEyeFromWorld, 0);

        //Log.v(TAG, "[" + (currentFrameIndex + 1) + "] " + " Left P:\n" + matToString(mTmpLeftEyePerspective) + "\nLeft MVP:\n" + matToString(mTmpMVPMatrix));

        // Extract translation. But first undo the rotation.
        Matrix.transposeM(invRotationMatrix, 0, headFromWorld, 0);
        invRotationMatrix[3] = invRotationMatrix[7] = invRotationMatrix[11] = 0;
        Matrix.multiplyMM(translationMatrix, 0, invRotationMatrix, 0, headFromWorld, 0);
        //Log.e("XXX", Arrays.toString(translationMatrix));

        mTmpHeadPosition[0] = -translationMatrix[12];
        mTmpHeadPosition[1] = 1.8f - translationMatrix[13];
        mTmpHeadPosition[2] = -translationMatrix[14];

        mTmpHeadOrientation[0] = x;
        mTmpHeadOrientation[1] = y;
        mTmpHeadOrientation[2] = z;
        mTmpHeadOrientation[3] = w;

        currentFrameIndex++;
        mFrameMap.append(currentFrameIndex, Arrays.copyOf(headFromWorld, headFromWorld.length));
        if(mFrameMap.size() > 100){
            mFrameMap.removeAt(0);
        }

        // Set tracking and save the current head pose. The headFromWorld value is saved in frameTracker via a call to trackFrame by the TrackingThread.
        mGvrTracking.sendTrackingInfo(mReceiverThread, currentFrameIndex, mTmpHeadOrientation, mTmpHeadPosition);
        //Log.e("XXX", "saving frame " + z + " " + Arrays.toString(m) + Math.sqrt(x * x + y * y + z * z + w * w));
    }

    private String matToString(float[] m) {
        StringBuilder sb = new StringBuilder();
        for(int i = 0; i < 4; i++) {
            sb.append("[");
            for(int j = 0; j < 4; j++) {
                sb.append(String.format("%+.5f ", m[j * 4 + i]));
            }
            sb.append("]\n");
        }
        return sb.toString();
    }

    private native long createNative(AssetManager assetManager);
    private native void glInitNative(long nativeHandle, int targetWidth, int TargetHeight);
    private native int getLoadingTexture(long nativeHandle);
    private native int getSurfaceTexture(long nativeHandle);
    private native void renderNative(long nativeHandle, float[] leftMvp, float[] rightMvp, int[] leftViewport, int[] rightViewport, boolean loading, long frameIndex);
    private native void destroyNative(long nativeHandle);
}