package com.polygraphene.alvr;

import android.app.Activity;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.graphics.Point;
import android.graphics.RectF;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.view.View;

import com.google.vr.ndk.base.BufferSpec;
import com.google.vr.ndk.base.BufferViewport;
import com.google.vr.ndk.base.BufferViewportList;
import com.google.vr.ndk.base.Frame;
import com.google.vr.ndk.base.GvrApi;
import com.google.vr.ndk.base.GvrLayout;
import com.google.vr.ndk.base.SwapChain;

import java.util.concurrent.TimeUnit;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Activity used when running on Daydream
 */
public class GvrActivity extends Activity {
    private GvrLayout mGvrLayout;
    private GLSurfaceView mSurfaceView;
    private GvrRenderer mRenderer;

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
        mSurfaceView.setRenderer(mRenderer);
    }

    // Notify components when there are lifecycle changes.
    @Override
    protected void onResume() {
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
    }

    @Override
    protected void onPause() {
        super.onPause();
        mGvrLayout.onPause();
        mSurfaceView.onPause();
    }

    @Override
    protected void onStart() {
        super.onStart();
        mRenderer.start();
    }

    @Override
    protected void onStop() {
        super.onStop();
        mRenderer.shutdown();
    }

    public String getVersionName(){
        try {
            PackageInfo pInfo = getPackageManager().getPackageInfo(getPackageName(), 0);
            String version = pInfo.versionName;
            return getString(R.string.app_name) + " v" + version;
        } catch (PackageManager.NameNotFoundException e) {
            e.printStackTrace();
            return getString(R.string.app_name) + " Unknown version";
        }
    }
}