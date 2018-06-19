package com.polygraphene.alvr;

public interface ArThread {
    void start(MainActivity activity);
    void interrupt();
    void join() throws InterruptedException;

    void initialize(MainActivity activity);

    boolean onRequestPermissionsResult(MainActivity activity);
    void setCameraTexture(int texture);

    float[] getOrientation();
    float[] getPosition();

    String getErrorMessage();

}
