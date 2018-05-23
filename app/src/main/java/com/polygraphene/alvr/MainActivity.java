package com.polygraphene.alvr;

import android.app.Activity;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.os.Bundle;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.Window;
import android.view.WindowManager;

import java.util.ArrayList;
import java.util.List;

public class MainActivity extends Activity {
    private final static String TAG = "MainActivity";

    static {
        System.loadLibrary("srt");
        System.loadLibrary("native-lib");
    }

    private List<MediaCodecInfo> mAvcCodecInfoes = new ArrayList<>();

    private VrThread mVrThread = null;

    public List<MediaCodecInfo> getAvcCodecInfoes() {
        return mAvcCodecInfoes;
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

        Log.v(TAG, "onCreate: Starting VrThread");
        mVrThread = new VrThread(this);
        mVrThread.start();
    }

    @Override
    protected void onResume() {
        super.onResume();

        mVrThread.onResume();
    }

    @Override
    protected void onPause() {
        super.onPause();

        mVrThread.onPause();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        Log.v(TAG, "onDestroy: Stopping VrThread.");
        mVrThread.interrupt();
        try {
            mVrThread.join();
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
        Log.v(TAG, "onDestroy: VrThread has stopped.");
    }
}
