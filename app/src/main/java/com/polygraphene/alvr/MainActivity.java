package com.polygraphene.alvr;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.media.AudioManager;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.os.Bundle;
import android.support.annotation.NonNull;
import android.util.Log;
import android.view.KeyEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.Window;
import android.view.WindowManager;

import java.util.ArrayList;
import java.util.List;

public class MainActivity extends Activity {
    private final static String TAG = "MainActivity";

    static {
        System.loadLibrary("native-lib");
    }

    private List<MediaCodecInfo> mAvcDecoderList;

    private VrThread mVrThread = null;

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
        if(mAvcDecoderList.size() == 0) {
            // TODO: Show error message for a user. How to?
            Log.e(TAG, "Suitable codec is not found.");
            finish();
            return;
        }

        Log.v(TAG, "onCreate: Starting VrThread");
        mVrThread = new VrThread(this);
        mVrThread.start();

        ArThread.requestPermissions(this);
    }

    @Override
    protected void onResume() {
        super.onResume();

        if(mVrThread != null) {
            mVrThread.onResume();
        }
    }

    @Override
    protected void onPause() {
        super.onPause();

        if(mVrThread != null) {
            mVrThread.onPause();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        Log.v(TAG, "onDestroy: Stopping VrThread.");
        if(mVrThread != null) {
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

    private List<MediaCodecInfo> findAvcDecoder(){
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

    public String getVersionName(){
        try {
            PackageInfo pInfo = this.getPackageManager().getPackageInfo(getPackageName(), 0);
            String version = pInfo.versionName;
            return getString(R.string.app_name) + " v" + version;
        } catch (PackageManager.NameNotFoundException e) {
            e.printStackTrace();
            return getString(R.string.app_name) + " Unknown version";
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        if (!mVrThread.onRequestPermissionsResult(this)) {
            finish();
        }
    }
}
