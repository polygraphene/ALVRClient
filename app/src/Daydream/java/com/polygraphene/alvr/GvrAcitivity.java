package com.polygraphene.alvr;

import android.app.Activity;
import android.app.Activity;
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
 * Minimal app using GvrLayout
 */
public class GvrAcitivity extends Activity {
    private GvrLayout gvrLayout;
    private GLSurfaceView surfaceView;
    private Renderer renderer;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // surfaceView is what actually performs the rendering.
        surfaceView = new GLSurfaceView(this);
        surfaceView.setEGLContextClientVersion(2);
        surfaceView.setEGLConfigChooser(8, 8, 8, 0, 0, 0);

        // This is the interface to the GVR APIs. It also draws the X & Gear icons.
        gvrLayout = new GvrLayout(this);
        gvrLayout.setPresentationView(surfaceView);
        gvrLayout.setAsyncReprojectionEnabled(true);
        setContentView(gvrLayout);

        // Bind a standard Android Renderer.
        renderer = new Renderer(gvrLayout.getGvrApi());
        surfaceView.setRenderer(renderer);

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

    // Notify components when there are lifecycle changes.
    @Override
    protected void onResume() {
        super.onResume();
        gvrLayout.onResume();
        surfaceView.onResume();
    }

    @Override
    protected void onPause() {
        super.onPause();
        gvrLayout.onPause();
        surfaceView.onPause();
    }

    @Override
    protected void onStop() {
        super.onStop();
        renderer.shutdown();
        finish();  // Finish the sample aggressively to keep it simple.
    }

    private class Renderer implements GLSurfaceView.Renderer {
        private final GvrApi api;

        // Manages a queue of frames that our app renders to.
        private SwapChain swapChain;
        // Represents the 2D & 3D aspects of each frame that we care about when rendering.
        private final BufferViewportList viewportList;
        // Temp used while rendering
        private final BufferViewport tmpViewport;
        // Size of the frame that our app renders to.
        private final Point targetSize = new Point();
        // Not used in this sample.
        private final float[] headFromWorld = new float[16];

        public Renderer(GvrApi api) {
            this.api = api;
            // Initialize native objects. It's important to call .shutdown to release resources.
            viewportList = api.createBufferViewportList();
            tmpViewport = api.createBufferViewport();
        }

        @Override
        public void onSurfaceCreated(GL10 gl, EGLConfig config) {
            api.initializeGl();

            // This tends to be 50% taller & wider than the device's screen so it has 2.4x as many pixels
            // as the screen.
            api.getMaximumEffectiveRenderTargetSize(targetSize);

            // Configure the pixel buffers that the app renders to. There is only one in this sample.
            BufferSpec bufferSpec = api.createBufferSpec();
            bufferSpec.setSize(targetSize);

            // Create the queue of frames with a given spec.
            BufferSpec[] specList = {bufferSpec};
            swapChain = api.createSwapChain(specList);

            // Free this early since we no longer need it.
            bufferSpec.shutdown();
        }

        @Override
        public void onSurfaceChanged(GL10 gl, int width, int height) {
        }

        private final RectF eyeUv = new RectF();

        @Override
        public void onDrawFrame(GL10 gl) {
            // Take a frame from the queue and direct all our GL commands to it. This will app rendering
            // thread to sleep until the compositor wakes it up.
            Frame frame = swapChain.acquireFrame();

            // We don't care about head pose in this sample, but GVR needs a valid matrix.
            api.getHeadSpaceFromStartSpaceTransform(headFromWorld, System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(50));

            // Get the parts of each frame associated with each eye in world space. The list is
            // {left eye, right eye}.
            api.getRecommendedBufferViewports(viewportList);

            // Bind to the only layer used in this frame.
            frame.bindBuffer(0);

            // For this sample, we draw different colors for each eye by scissoring the two halves of the
            // frame.
            gl.glEnable(GLES20.GL_SCISSOR_TEST);
            for (int eye = 0; eye < 2; eye++) {
                // Determine the size of each eye's part of the frame in the range [0, 1]
                viewportList.get(eye, tmpViewport);
                tmpViewport.getSourceUv(eyeUv);

                // Convert the eye's box to pixel ranges.
                gl.glScissor(
                        (int) (eyeUv.left * targetSize.x),
                        (int) (eyeUv.bottom * targetSize.y),
                        (int) (eyeUv.width() * targetSize.x),
                        (int) (-eyeUv.height() * targetSize.y));

                // Draw the color.
                if (eye == 0) {
                    GLES20.glClearColor(1.f, 0.0f, 0.0f, 1.0f);
                } else {
                    GLES20.glClearColor(0.f, 0.0f, 1.0f, 1.0f);
                }
                GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
            }

            // Send the frame to the compositor
            frame.unbind();
            frame.submit(viewportList, headFromWorld);
        }

        public void shutdown() {
            viewportList.shutdown();
            tmpViewport.shutdown();
            swapChain.shutdown();
        }
    }
}