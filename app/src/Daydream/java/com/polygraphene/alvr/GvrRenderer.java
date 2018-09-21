package com.polygraphene.alvr;

import android.content.Context;
import android.content.res.AssetManager;
import android.graphics.Point;
import android.graphics.RectF;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.opengl.Matrix;
import android.util.Log;

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
    static {
        System.loadLibrary("native-lib");
    }

    private final GvrActivity mActivity;
    private final GvrApi mApi;

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

    private final LoadingTexture mLoadingTexture = new LoadingTexture();

    // Native pointer to GvrRenderer
    private long nativeHandle;

    public GvrRenderer(GvrActivity activity, GvrApi api) {
        System.loadLibrary("native-lib");
        this.mActivity = activity;
        this.mApi = api;
    }

    public void start() {
        // Initialize native objects. It's important to call .shutdown to release resources.
        mViewportList = mApi.createBufferViewportList();
        mTmpViewport = mApi.createBufferViewport();
        nativeHandle = createNative(mActivity.getAssets());
    }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
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
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
    }

    private final RectF mEyeUv = new RectF();

    @Override
    public void onDrawFrame(GL10 gl) {
        // Take a frame from the queue and direct all our GL commands to it. This will app rendering
        // thread to sleep until the compositor wakes it up.
        Frame frame = mSwapChain.acquireFrame();

        mApi.getHeadSpaceFromStartSpaceTransform(mHeadFromWorld, System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(50));

        // Get the parts of each frame associated with each eye in world space. The list is
        // {left eye, right eye}.
        mApi.getRecommendedBufferViewports(mViewportList);

        // Bind to the only layer used in this frame.
        frame.bindBuffer(0);

        for (int eye = 0; eye < 2; ++eye) {
            mViewportList.get(eye, mTmpViewport);
            mTmpViewport.getSourceFov(mEyeFov);
            float l = (float) -Math.tan(Math.toRadians(mEyeFov.left)) * 1f;
            float r = (float) Math.tan(Math.toRadians(mEyeFov.right)) * 1f;
            float b = (float) -Math.tan(Math.toRadians(mEyeFov.bottom)) * 1f;
            float t = (float) Math.tan(Math.toRadians(mEyeFov.top)) * 1f;
            Matrix.frustumM(mEyePerspective, 0, l, r, b, t, 1f, 10f);

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

            Log.i("ALVR", Arrays.toString(eye == 0 ? mLeftMvp : mRightMvp));
        }

        mLoadingTexture.drawMessage(mActivity.getVersionName() + "\nLoading...");
        
        renderNative(nativeHandle, mLeftMvp, mRightMvp, mLeftViewport, mRightViewport);

        // Send the frame to the compositor
        frame.unbind();
        frame.submit(mViewportList, mHeadFromWorld);
    }

    public void shutdown() {
        mViewportList.shutdown();
        mViewportList = null;
        mTmpViewport.shutdown();
        mTmpViewport = null;
        mSwapChain.shutdown();
        mSwapChain = null;
        destroyNative(nativeHandle);
        nativeHandle = 0;
    }

    private native long createNative(AssetManager assetManager);
    private native void glInitNative(long nativeHandle, int targetWidth, int TargetHeight);
    private native int getLoadingTexture(long nativeHandle);
    private native void renderNative(long nativeHandle, float[] leftMvp, float[] rightMvp, int[] leftViewport, int[] rightViewport);
    private native void destroyNative(long nativeHandle);
}