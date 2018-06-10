package com.polygraphene.alvr;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.media.AudioManager;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.support.annotation.Nullable;
import android.support.v4.app.ActivityCompat;
import android.support.v4.content.ContextCompat;
import android.util.Log;

import android.view.KeyEvent;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.Toast;

import com.google.ar.core.ArCoreApk;
import com.google.ar.core.Config;
import com.google.ar.core.Frame;
import com.google.ar.core.Session;
import com.google.ar.core.TrackingState;
import com.google.ar.core.exceptions.CameraNotAvailableException;
import com.google.ar.core.exceptions.UnavailableApkTooOldException;
import com.google.ar.core.exceptions.UnavailableArcoreNotInstalledException;
import com.google.ar.core.exceptions.UnavailableDeviceNotCompatibleException;
import com.google.ar.core.exceptions.UnavailableException;
import com.google.ar.core.exceptions.UnavailableSdkTooOldException;
import com.google.ar.sceneform.ArSceneView;
import com.google.ar.sceneform.FrameTime;
import com.google.ar.sceneform.Scene;
import com.google.ar.sceneform.rendering.GLHelper;

import java.util.ArrayList;
import java.util.List;

public class MainActivity extends Activity {
    private final static String TAG = "MainActivity";

    static {
        System.loadLibrary("native-lib");
    }

    private List<MediaCodecInfo> mAvcDecoderList;

    private VrThread mVrThread = null;

    private static final int RC_PERMISSIONS = 0x123;
    private boolean mInstallRequested;
    private Session mSession = null;

    public MediaCodecInfo getAvcDecoder() {
        return mAvcDecoderList.get(0);
    }

    private final SurfaceHolder.Callback mCallback = new SurfaceHolder.Callback() {
        @Override
        public void surfaceCreated(final SurfaceHolder holder) {
            mVrThread.onSurfaceCreated(holder.getSurface());
        }

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            mVrThread.onSurfaceChanged(holder.getSurface());
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            mVrThread.onSurfaceDestroyed();
        }
    };

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        requestWindowFeature(Window.FEATURE_NO_TITLE);

        setContentView(R.layout.activity_main);
        SurfaceView surfaceView = findViewById(R.id.surfaceview);

        SurfaceHolder holder = surfaceView.getHolder();
        holder.addCallback(mCallback);

        mAvcDecoderList = findAvcDecoder();
        if (mAvcDecoderList.size() == 0) {
            // TODO: Show error message for a user. How to?
            Log.e(TAG, "Suitable codec is not found.");
            finish();
            return;
        }

        requestCameraPermission(RC_PERMISSIONS);

        surfaceView.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent event) {
                Log.v(TAG, "onTouch: " + event.getAction() + " " + event.getX() + " " + event.getY());
                return true;
            }
        });

        Log.v(TAG, "onCreate: Starting VrThread");
        mVrThread = new VrThread(this);
        mVrThread.start();
    }

    @Override
    protected void onResume() {
        super.onResume();

        if (mSession == null) {
            try {
                mSession = createArSession(mInstallRequested);

                int var3 = GLHelper.createCameraTexture();
                Log.v(TAG, "ArSession texture=" + var3);
                mSession.setCameraTextureName(var3);
            } catch (UnavailableException e) {
                handleSessionException(e);
            }
        }
        mVrThread.setArSession(mSession);

        if (mVrThread != null) {
            mVrThread.onResume();
        }
    }

    @Override
    protected void onPause() {
        super.onPause();

        if (mVrThread != null) {
            mVrThread.onPause();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        Log.v(TAG, "onDestroy: Stopping VrThread.");
        if (mVrThread != null) {
            mVrThread.interrupt();
            try {
                mVrThread.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
        Log.v(TAG, "onDestroy: VrThread has stopped.");
    }
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        //Log.v(TAG, "dispatchKeyEvent: " + event.getKeyCode());
        if(event.getAction() == KeyEvent.ACTION_DOWN || event.getAction() == KeyEvent.ACTION_UP) {
            if (event.getKeyCode() == KeyEvent.KEYCODE_VOLUME_UP)
            {
                adjustVolume(1);
                return true;
            }
            if (event.getKeyCode() == KeyEvent.KEYCODE_VOLUME_DOWN)
            {
                adjustVolume(-1);
                return true;
            }

            mVrThread.onKeyEvent(event.getKeyCode(), event.getAction());
            return true;
        }else{
            return super.dispatchKeyEvent(event);
        }
    }

    private void adjustVolume(int direction)
    {
        AudioManager audio = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        audio.adjustStreamVolume(AudioManager.STREAM_MUSIC, direction, 0);
    }

    private List<MediaCodecInfo> findAvcDecoder() {
        List<MediaCodecInfo> codecList = new ArrayList<>();

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
                //MediaCodecInfo.CodecCapabilities capabilitiesForType = info.getCapabilitiesForType("video/avc");

                //Log.v(TAG, info.getName());
                //for (MediaCodecInfo.CodecProfileLevel profile : capabilitiesForType.profileLevels) {
                //    Log.v(TAG, "profile:" + profile.profile + " level:" + profile.level);
                //}

                codecList.add(info);
            }
        }
        return codecList;
    }

    public String getVersionName() {
        try {
            PackageInfo pInfo = this.getPackageManager().getPackageInfo(getPackageName(), 0);
            String version = pInfo.versionName;
            return getString(R.string.app_name) + " v" + version;
        } catch (PackageManager.NameNotFoundException e) {
            e.printStackTrace();
            return getString(R.string.app_name) + " Unknown version";
        }
    }

    public void displayError(final String errorMsg, @Nullable final Throwable problem) {
        final String tag = getClass().getSimpleName();
        final String toastText;
        if (problem != null && problem.getMessage() != null) {
            Log.e(tag, errorMsg, problem);
            toastText = errorMsg + ": " + problem.getMessage();
        } else if (problem != null) {
            Log.e(tag, errorMsg, problem);
            toastText = errorMsg;
        } else {
            Log.e(tag, errorMsg);
            toastText = errorMsg;
        }

        new Handler(Looper.getMainLooper())
                .post(
                        () -> {
                            Toast toast = Toast.makeText(this, toastText, Toast.LENGTH_LONG);
                            toast.setGravity(Gravity.CENTER, 0, 0);
                            toast.show();
                        });
    }


    public Session createArSession(boolean installRequested)
            throws UnavailableException {
        Session session = null;
        // if we have the camera permission, create the session
        if (hasCameraPermission()) {
            switch (ArCoreApk.getInstance().requestInstall(this, !installRequested)) {
                case INSTALL_REQUESTED:
                    return null;
                case INSTALLED:
                    break;
            }
            session = new Session(this);
            // IMPORTANT!!!  ArSceneView needs to use the non-blocking update mode.
            Config config = new Config(session);
            config.setUpdateMode(Config.UpdateMode.LATEST_CAMERA_IMAGE);
            session.configure(config);
            Log.v(TAG, "ArSession configured");
        }
        return session;
    }

    /**
     * Check to see we have the necessary permissions for this app, and ask for them if we don't.
     */
    public void requestCameraPermission(int requestCode) {
        ActivityCompat.requestPermissions(
                this, new String[]{Manifest.permission.CAMERA}, requestCode);
    }

    /**
     * Check to see we have the necessary permissions for this app.
     */
    public boolean hasCameraPermission() {
        return ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
                == PackageManager.PERMISSION_GRANTED;
    }

    /**
     * Check to see if we need to show the rationale for this permission.
     */
    public boolean shouldShowRequestPermissionRationale(Activity activity) {
        return ActivityCompat.shouldShowRequestPermissionRationale(
                activity, Manifest.permission.CAMERA);
    }

    /**
     * Launch Application Setting to grant permission.
     */
    public void launchPermissionSettings(Activity activity) {
        Intent intent = new Intent();
        intent.setAction(Settings.ACTION_APPLICATION_DETAILS_SETTINGS);
        intent.setData(Uri.fromParts("package", activity.getPackageName(), null));
        activity.startActivity(intent);
    }

    public void handleSessionException(UnavailableException sessionException) {

        String message;
        if (sessionException instanceof UnavailableArcoreNotInstalledException) {
            message = "Please install ARCore";
        } else if (sessionException instanceof UnavailableApkTooOldException) {
            message = "Please update ARCore";
        } else if (sessionException instanceof UnavailableSdkTooOldException) {
            message = "Please update this app";
        } else if (sessionException instanceof UnavailableDeviceNotCompatibleException) {
            message = "This device does not support AR";
        } else {
            message = "Failed to create AR session";
            Log.e(TAG, "Exception: " + sessionException);
        }
        Toast.makeText(this, message, Toast.LENGTH_LONG).show();
    }
}
