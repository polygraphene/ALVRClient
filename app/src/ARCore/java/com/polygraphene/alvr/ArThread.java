package com.polygraphene.alvr;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES11Ext;
import android.opengl.GLES20;
import android.provider.Settings;
import android.support.v4.app.ActivityCompat;
import android.support.v4.content.ContextCompat;
import android.util.Log;
import android.widget.Toast;

import com.google.ar.core.ArCoreApk;
import com.google.ar.core.Config;
import com.google.ar.core.Frame;
import com.google.ar.core.Pose;
import com.google.ar.core.Session;
import com.google.ar.core.exceptions.CameraNotAvailableException;
import com.google.ar.core.exceptions.UnavailableApkTooOldException;
import com.google.ar.core.exceptions.UnavailableArcoreNotInstalledException;
import com.google.ar.core.exceptions.UnavailableDeviceNotCompatibleException;
import com.google.ar.core.exceptions.UnavailableException;
import com.google.ar.core.exceptions.UnavailableSdkTooOldException;

import java.io.FileOutputStream;
import java.nio.ByteBuffer;
import java.nio.IntBuffer;
import java.util.concurrent.TimeUnit;

public class ArThread extends ThreadBase {
    private static final String TAG = "ArThread";

    private static final int EGL_OPENGL_ES3_BIT = 64;
    private static final int RC_PERMISSIONS = 1;

    private boolean mConnected = false;
    private boolean mInstallRequested;
    private Session mSession = null;

    private int mArRefreshRate = 60;

    // Position got from ARCore
    private float[] mPosition = new float[3];
    private float[] mOrientation = new float[4];

    private String mErrorMessage = null;

    private int mCameraTexture = -1;
    EGLContext mEGLContext;

    ArThread(EGLContext EGLContext) {
        mEGLContext = EGLContext;
    }

    public void start() {
        if (mSession == null) {
            return;
        }
        super.startBase();
    }

    public static void requestPermissions(BaseActivity activity) {
        ActivityCompat.requestPermissions(
                activity, new String[]{Manifest.permission.CAMERA}, RC_PERMISSIONS);
    }

    public void setCameraTexture(int texture) {
        mCameraTexture = texture;
    }

    public float[] getOrientation() {
        return mOrientation;
    }

    public float[] getPosition() {
        return mPosition;
    }

    public String getErrorMessage() {
        return mErrorMessage;
    }

    public void initialize(BaseActivity activity) {
        try {
            Session session = createArSession(activity, mInstallRequested);
            if (session == null) {
                mInstallRequested = hasCameraPermission(activity);
                mErrorMessage = "Please install ARCore apk from store.";
                return;
            }
            mSession = session;
        } catch (UnavailableException e) {
            handleSessionException(e);
        }
    }

    public void onConnect() {
        mConnected = true;
    }

    public void onDisconnect() {
        mConnected = false;
    }

    public void run() {
        Log.v(TAG, "ArThread started.");
        if (mSession != null) {
            Log.v(TAG, "setCameraTextureName texture=" + mCameraTexture);
            makeContext(mEGLContext);
            mSession.setCameraTextureName(mCameraTexture);
            try {
                mSession.resume();
                Log.e(TAG, "ArSession resumed");
            } catch (CameraNotAvailableException e) {
                e.printStackTrace();
            }
        }
        Log.v(TAG, "ArThread initialized.");

        long previousFetchTime = System.nanoTime();
        while (!isStopped()) {
            if (mConnected) {
                if (mSession != null) {
                    try {
                        Log.v(TAG, "Update ArSession.");
                        Frame frame = mSession.update();
                        Pose pose = frame.getCamera().getDisplayOrientedPose();
                        System.arraycopy(pose.getTranslation(), 0
                                , mPosition, 0, 3);
                        pose.getRotationQuaternion(mOrientation, 0);
                        Log.v(TAG, "New position fed. Position=(" + mPosition[0] + ", " + mPosition[1] + ", " + mPosition[2] + ")");
                    } catch (CameraNotAvailableException e) {
                        e.printStackTrace();
                    }
                }
            }
            try {
                previousFetchTime += 1000 * 1000 * 1000 / mArRefreshRate;
                long next = previousFetchTime - System.nanoTime();
                if (next < 0) {
                    // Exceed time!
                    previousFetchTime = System.nanoTime();
                } else {
                    TimeUnit.NANOSECONDS.sleep(next);
                }
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
        Log.v(TAG, "ArThread has stopped.");
        if (mSession != null) {
            mSession.pause();
            Log.e(TAG, "ArSession paused");
        }
    }

    private Session createArSession(Activity activity, boolean installRequested)
            throws UnavailableException {
        Session session = null;
        // if we have the camera permission, create the session
        if (hasCameraPermission(activity)) {
            switch (ArCoreApk.getInstance().requestInstall(activity, !installRequested)) {
                case INSTALL_REQUESTED:
                    return null;
                case INSTALLED:
                    break;
            }
            session = new Session(activity);
            // IMPORTANT!!!  ArSceneView needs to use the non-blocking update mode.
            Config config = new Config(session);
            config.setUpdateMode(Config.UpdateMode.LATEST_CAMERA_IMAGE);
            session.configure(config);
            Log.v(TAG, "ArSession configured");
        }
        return session;
    }

    /**
     * Check to see we have the necessary permissions for this app.
     */
    private static boolean hasCameraPermission(Activity activity) {
        return ContextCompat.checkSelfPermission(activity, Manifest.permission.CAMERA)
                == PackageManager.PERMISSION_GRANTED;
    }

    /**
     * Check to see if we need to show the rationale for this permission.
     */
    private static boolean shouldShowRequestPermissionRationale(Activity activity) {
        return ActivityCompat.shouldShowRequestPermissionRationale(
                activity, Manifest.permission.CAMERA);
    }

    /**
     * Launch Application Setting to grant permission.
     */
    private void launchPermissionSettings(BaseActivity activity) {
        Intent intent = new Intent();
        intent.setAction(Settings.ACTION_APPLICATION_DETAILS_SETTINGS);
        intent.setData(Uri.fromParts("package", activity.getPackageName(), null));
        activity.startActivity(intent);
    }

    private void handleSessionException(UnavailableException sessionException) {
        if (sessionException instanceof UnavailableArcoreNotInstalledException) {
            mErrorMessage = "Please install ARCore";
        } else if (sessionException instanceof UnavailableApkTooOldException) {
            mErrorMessage = "Please update ARCore";
        } else if (sessionException instanceof UnavailableSdkTooOldException) {
            mErrorMessage = "Please update this app";
        } else if (sessionException instanceof UnavailableDeviceNotCompatibleException) {
            mErrorMessage = "This device does not support AR";
        } else {
            mErrorMessage = "Failed to create AR session";
            Log.e(TAG, "Exception: " + sessionException);
        }
    }

    public static boolean onRequestPermissionsResult(Activity activity) {
        if (!hasCameraPermission(activity)) {
            Toast.makeText(
                    activity, "Camera permission is needed to run this application", Toast.LENGTH_LONG)
                    .show();
            if (!shouldShowRequestPermissionRationale(activity)) {
                // Permission denied with checking "Do not ask again".
                //launchPermissionSettings(activity);
            } else {
            }
            return true;
        }
        return true;
    }

    private void makeContext(EGLContext shareContext) {
        EGLDisplay display = EGL14.eglGetDisplay(0);

        int[] version = new int[2];
        EGL14.eglInitialize(display, version, 0, version, 1);

        EGLConfig[] configs = new EGLConfig[1];
        int[] numConfig = new int[]{0};
        int[] attribs = new int[]{EGL14.EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL14.EGL_NONE};
        EGL14.eglChooseConfig(display, attribs, 0, configs, 0, 1, numConfig, 0);

        int[] contextAttribs = new int[]{EGL14.EGL_CONTEXT_CLIENT_VERSION, 3, EGL14.EGL_NONE};
        EGLContext context = EGL14.eglCreateContext(display, configs[0], shareContext, contextAttribs, 0);

        int[] surfaceAttribs = new int[]{EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE};
        EGLSurface surface = EGL14.eglCreatePbufferSurface(display, configs[0], surfaceAttribs, 0);

        if (!EGL14.eglMakeCurrent(display, surface, surface, context)) {
            throw new IllegalStateException("Error making GL context.");
        }
    }

    public void debugReadPixel(BaseActivity activity) {
        //Generate a new FBO. It will contain your texture.
        int fb[] = new int[1];
        GLES11Ext.glGenFramebuffersOES(1, IntBuffer.wrap(fb));
        GLES11Ext.glBindFramebufferOES(GLES11Ext.GL_FRAMEBUFFER_OES, fb[0]);

        GLES11Ext.glFramebufferTexture2DOES(GLES11Ext.GL_FRAMEBUFFER_OES, GLES11Ext.GL_COLOR_ATTACHMENT0_OES, GLES11Ext.GL_TEXTURE_EXTERNAL_OES, mCameraTexture, 0);


        GLES11Ext.glBindFramebufferOES(GLES11Ext.GL_FRAMEBUFFER_OES, fb[0]);
        GLES20.glViewport(0, 0, 500, 500);

        final int pixels[] = new int[500 * 500];
        final IntBuffer buffer = IntBuffer.wrap(pixels);
        buffer.position(0);

        GLES20.glReadPixels(0, 0, 500, 500, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, buffer);

        ByteBuffer byteBuffer = ByteBuffer.allocate(pixels.length * 4);
        IntBuffer intBuffer = byteBuffer.asIntBuffer();
        intBuffer.put(pixels);
        try {
            FileOutputStream fileOutputStream = new FileOutputStream(activity.getExternalMediaDirs()[0].getAbsolutePath() + "/test.binf");
            fileOutputStream.write(byteBuffer.array(), 0, pixels.length * 4);
            fileOutputStream.close();
        } catch (java.io.IOException e) {
            e.printStackTrace();
        }
    }
}
