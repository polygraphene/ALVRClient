package com.polygraphene.alvr;

import android.content.Context;
import android.content.Intent;
import android.support.test.InstrumentationRegistry;
import android.support.test.rule.ActivityTestRule;
import android.support.test.runner.AndroidJUnit4;
import android.view.SurfaceView;

import com.polygraphene.alvr.test.DecoderTestActivity;

import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public class ExampleInstrumentedTest {

    @Rule
    public ActivityTestRule<DecoderTestActivity> rule  = new ActivityTestRule<>(DecoderTestActivity.class);

    @Test
    public void decoderTest() throws Exception {
        // Context of the app under test.
        Context appContext = InstrumentationRegistry.getTargetContext();
        DecoderTestActivity activity = rule.getActivity();
        SurfaceView surfaceView = activity.findViewById(R.id.surface);
        rule.launchActivity(new Intent());
        //InstrumentationRegistry.getInstrumentation().callActivityOnResume(activity);
        //Instrumentation.ActivityMonitor activityMonitor = new Instrumentation.ActivityMonitor();
        //InstrumentationRegistry.getInstrumentation().waitForMonitor(activityMonitor);

        Thread.sleep(5000);
    }
}
