package com.polygraphene.alvr;

import android.content.Context;
import android.content.res.AssetManager;
import android.graphics.Point;
import android.graphics.RectF;
import android.graphics.SurfaceTexture;
import android.opengl.EGL14;
import android.opengl.EGLContext;
import android.opengl.GLES20;
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
import java.util.HashMap;
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
    private GvrTracking mGvrTracking;
    private UdpReceiverThread mReceiverThread;
    private DecoderThread mDecoderThread;
    private Object mWaiter = new Object();
    private boolean mFrameAvailable = false;

    // Manages a queue of frames that our app renders to.
    private SwapChain mSwapChain;
    // Represents the 2D & 3D aspects of each frame that we care about when rendering.
    private BufferViewportList mViewportList;
    // Temp used while rendering
    private BufferViewport mTmpViewport;
    // Size of the frame that our app renders to.
    private final Point mTargetSize = new Point();

    private final float[] mHeadFromWorld = new float[16];
    private final float[] mEyeFromHead = new float[16];
    private final float[] mEyeFromWorld = new float[16];
    private final RectF mEyeFov = new RectF();
    private final float[] mEyePerspective = new float[16];
    private final float[] mLeftMvp = new float[16];
    private final float[] mRightMvp = new float[16];
    private final int[] mLeftViewport = new int[4];
    private final int[] mRightViewport = new int[4];


    private final float[] mTmpLeftEyePerspective = new float[16];

    private final float[] invRotationMatrix = new float[16];
    private final float[] translationMatrix = new float[16];

    private final float[] mTmpHeadPosition = new float[3];
    private final float[] mTmpHeadOrientation = new float[4];
    private long currentFrameIndex = 0;
    private LongSparseArray<float[]> mFrameMap = new LongSparseArray<>();

    private final LoadingTexture mLoadingTexture = new LoadingTexture();

    private SurfaceTexture mSurfaceTexture;
    private Surface mSurface;

    private Runnable mSurfaceCreatedListener;

    // Native pointer to GvrRenderer
    private long nativeHandle;

    public GvrRenderer(GvrActivity activity, GvrApi api) {
        this.mActivity = activity;
        this.mApi = api;

        mGvrTracking = new GvrTracking(mApi.getNativeGvrContext());
    }

    public void setOnSurfaceCreatedListener(Runnable runnable) {
        mSurfaceCreatedListener = runnable;
    }

    public void start() {
        // Initialize native objects. It's important to call .shutdown to release resources.
        mViewportList = mApi.createBufferViewportList();
        mTmpViewport = mApi.createBufferViewport();
        nativeHandle = createNative(mActivity.getAssets());
    }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        Log.v(TAG, "onSurfaceCreated");
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

        mSurfaceTexture = new SurfaceTexture(getSurfaceTexture(nativeHandle));
        mSurfaceTexture.setOnFrameAvailableListener(new SurfaceTexture.OnFrameAvailableListener() {
            @Override
            public void onFrameAvailable(SurfaceTexture surfaceTexture) {
                Log.v(TAG, "onFrameAvailable");
                synchronized (mWaiter) {
                    mFrameAvailable = true;
                    mWaiter.notifyAll();
                }
            }
        });
        mSurface = new Surface(mSurfaceTexture);

        mSurfaceCreatedListener.run();

        Log.v(TAG, "onSurfaceCreated: end");
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
    }

    public void onResume(UdpReceiverThread receiverThread, DecoderThread decoderThread) {
        mReceiverThread = receiverThread;
        mDecoderThread = decoderThread;
    }

    public void onPause() {
        mReceiverThread = null;
        mDecoderThread = null;
    }

    private final RectF mEyeUv = new RectF();

    @Override
    public void onDrawFrame(GL10 gl) {
        // Take a frame from the queue and direct all our GL commands to it. This will app rendering
        // thread to sleep until the compositor wakes it up.
        Frame frame = mSwapChain.acquireFrame();

        mApi.getHeadSpaceFromStartSpaceTransform(mHeadFromWorld, System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(50));

        // Send tracking info to the server.
        if(mReceiverThread != null && mReceiverThread.isConnected()) {
            sendTracking(mHeadFromWorld);
        }

        // Get the parts of each frame associated with each eye in world space. The list is
        // {left eye, right eye}.
        mApi.getRecommendedBufferViewports(mViewportList);

        // Bind to the only layer used in this frame.
        frame.bindBuffer(0);

        for (int eye = 0; eye < 2; ++eye) {
            mViewportList.get(eye, mTmpViewport);
            mTmpViewport.getSourceFov(mEyeFov);
            //Log.v(TAG, "EyeFov[" + eye + "] l:" + mEyeFov.left + ", r:" + mEyeFov.right + ", b:" + mEyeFov.bottom + ", t:" + mEyeFov.top);
            float l = (float) -Math.tan(Math.toRadians(mEyeFov.left)) * 1f;
            float r = (float) Math.tan(Math.toRadians(mEyeFov.right)) * 1f;
            float b = (float) -Math.tan(Math.toRadians(mEyeFov.bottom)) * 1f;
            float t = (float) Math.tan(Math.toRadians(mEyeFov.top)) * 1f;
            Matrix.frustumM(mEyePerspective, 0, l, r, b, t, 0.1f, 30f);

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

        if(mReceiverThread != null && mReceiverThread.isConnected()) {
            while(mReceiverThread != null && mReceiverThread.isConnected()) {
                long frameIndex = waitFrame(5);
                if (frameIndex != -1) {
                    float[] matrix = mFrameMap.get(frameIndex);
                    if (matrix != null) {
                        System.arraycopy(matrix, 0, mHeadFromWorld, 0, matrix.length);
                    }
                    renderNative(nativeHandle, mLeftMvp, mRightMvp, mLeftViewport, mRightViewport, false, frameIndex);
                    break;
                } else {
                    // Send tracking info to the server.
                    if(mReceiverThread != null && mReceiverThread.isConnected()) {
                        mApi.getHeadSpaceFromStartSpaceTransform(mHeadFromWorld, System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(50));
                        sendTracking(mHeadFromWorld);
                    }
                }
            }
        } else {
            mLoadingTexture.drawMessage(Utils.getVersionName(mActivity) + "\nLoading...");

            renderNative(nativeHandle, mLeftMvp, mRightMvp, mLeftViewport, mRightViewport, true, 0);
        }

        // Send the frame to the compositor
        frame.unbind();
        frame.submit(mViewportList, mHeadFromWorld);
    }

    public void shutdown() {
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
    }

    private long waitFrame(int waitMs) {
        synchronized (mWaiter) {
            mFrameAvailable = false;

            long frameIndex = mDecoderThread.render(waitMs);
            if (frameIndex < 0) {
                return frameIndex;
            }

            while (!mFrameAvailable) {
                try {
                    mWaiter.wait();
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }

            mSurfaceTexture.updateTexImage();
            return frameIndex;
        }
    }

    float[] mTmpMVPMatrix = new float[16];
    float[] mTmpEyeFromHead = new float[16];
    float[] mTmpEyeFromWorld = new float[16];

    private void sendTracking(float[] headFromWorld) {
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
        mGvrTracking.sendTrackingInfo(mReceiverThread.getPointer(), currentFrameIndex, mTmpHeadOrientation, mTmpHeadPosition);
        //Log.e("XXX", "saving frame " + z + " " + Arrays.toString(m) + Math.sqrt(x * x + y * y + z * z + w * w));

    }

    public EGLContext getEGLContext() {
        return EGL14.eglGetCurrentContext();
    }

    public Surface getSurface() {
        return mSurface;
    }

    private native long createNative(AssetManager assetManager);
    private native void glInitNative(long nativeHandle, int targetWidth, int TargetHeight);
    private native int getLoadingTexture(long nativeHandle);
    private native int getSurfaceTexture(long nativeHandle);
    private native void renderNative(long nativeHandle, float[] leftMvp, float[] rightMvp, int[] leftViewport, int[] rightViewport, boolean loading, long frameIndex);
    private native void destroyNative(long nativeHandle);
}