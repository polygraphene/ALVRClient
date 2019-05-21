package com.polygraphene.alvr;

import android.content.Context;
import android.media.AudioManager;
import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.Window;
import android.view.WindowManager;

public class OvrActivity extends BaseActivity {
    private final static String TAG = "OvrActivity";

    private OvrThread mOvrThread = null;

    private final SurfaceHolder.Callback mCallback = new SurfaceHolder.Callback() {
        @Override
        public void surfaceCreated(final SurfaceHolder holder) {
            mOvrThread.onSurfaceCreated(holder.getSurface());
        }

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            mOvrThread.onSurfaceChanged(holder.getSurface());
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            mOvrThread.onSurfaceDestroyed();
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        requestWindowFeature(Window.FEATURE_NO_TITLE);

        setContentView(R.layout.activity_main);
        SurfaceView surfaceView = findViewById(R.id.surfaceview);

        SurfaceHolder holder = surfaceView.getHolder();
        holder.addCallback(mCallback);

        Log.v(TAG, "onCreate: Starting OvrThread");
        mOvrThread = new OvrThread(this);
    }

    @Override
    protected void onResume() {
        super.onResume();

        if(mOvrThread != null) {
            mOvrThread.onResume();
        }
    }

    @Override
    protected void onPause() {
        super.onPause();

        if(mOvrThread != null) {
            mOvrThread.onPause();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        Log.v(TAG, "onDestroy: Stopping OvrThread.");
        if(mOvrThread != null) {
            mOvrThread.quit();
            mOvrThread = null;
        }
        Log.v(TAG, "onDestroy: OvrThread has stopped.");
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
}
