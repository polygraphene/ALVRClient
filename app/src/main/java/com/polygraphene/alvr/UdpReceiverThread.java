package com.polygraphene.alvr;

import android.util.Log;

import java.net.InetAddress;
import java.net.InterfaceAddress;
import java.net.NetworkInterface;
import java.net.SocketException;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.List;

class UdpReceiverThread implements NALParser {
    private static final String TAG = "UdpReceiverThread";

    static {
        System.loadLibrary("native-lib");
    }

    private static final String BROADCAST_ADDRESS = "255.255.255.255";

    private Thread mThread;
    private StatisticsCounter mCounter;
    private LatencyCollector mLatencyCollector;
    private int mPort;
    private boolean mInitialized = false;
    private boolean mInitializeFailed = false;

    private String mPreviousServerAddress;
    private int mPreviousServerPort;

    public native boolean isConnected();

    interface Callback {
        void onConnected(int width, int height, int codec);
        void onChangeSettings(int enableTestMode, int suspend);
    }
    private Callback mCallback;

    UdpReceiverThread(StatisticsCounter counter, Callback callback, LatencyCollector latencyCollector) {
        mCounter = counter;
        mCallback = callback;
        mLatencyCollector = latencyCollector;
    }

    public void setPort(int port) {
        mPort = port;
    }

    private String getDeviceName() {
        String manufacturer = android.os.Build.MANUFACTURER;
        String model = android.os.Build.MODEL;
        if (model.toLowerCase().startsWith(manufacturer.toLowerCase())) {
            return model;
        } else {
            return manufacturer + " " + model;
        }
    }

    public void recoverConnectionState(String serverAddress, int serverPort){
        mPreviousServerAddress = serverAddress;
        mPreviousServerPort = serverPort;
    }

    public boolean start() {
        mThread = new Thread() {
            @Override
            public void run() {
                runThread();
            }
        };
        mThread.start();

        synchronized (this) {
            while (!mInitialized && !mInitializeFailed) {
                try {
                    wait();
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }

        return !mInitializeFailed;
    }

    private void runThread() {
        mThread.setName(UdpReceiverThread.class.getName());

        try {
            String[] broadcastList = getBroadcastAddressList();
            int ret = initializeSocket(mPort, getDeviceName(), broadcastList);
            if (ret != 0) {
                Log.e(TAG, "Error on initializing socket. Code=" + ret + ".");
                synchronized (this) {
                    mInitializeFailed = true;
                    notifyAll();
                }
                return;
            }
            synchronized (this) {
                mInitialized = true;
                notifyAll();
            }
            Log.v(TAG, "UdpReceiverThread initialized.");

            runLoop(mLatencyCollector, mPreviousServerAddress, mPreviousServerPort);
        } finally {
            closeSocket();
        }

        Log.v(TAG, "UdpReceiverThread stopped.");
    }

    // List broadcast address from all interfaces except for mobile network.
    // We should send all broadcast address to use USB tethering or VPN.
    private String[] getBroadcastAddressList() {
        List<String> ret = new ArrayList<>();
        try {
            Enumeration<NetworkInterface> networkInterfaces = NetworkInterface.getNetworkInterfaces();
            while(networkInterfaces.hasMoreElements()){
                NetworkInterface networkInterface = networkInterfaces.nextElement();

                if(networkInterface.getName().startsWith("rmnet")) {
                    // Ignore mobile network interfaces.
                    Log.v(TAG, "Ignore interface. Name=" + networkInterface.getName());
                    continue;
                }

                List<InterfaceAddress> interfaceAddresses = networkInterface.getInterfaceAddresses();
                String address = "";
                for(InterfaceAddress interfaceAddress : interfaceAddresses) {
                    address += interfaceAddress.toString() + ", ";
                    // getBroadcast() return non-null only when ipv4.
                    if(interfaceAddress.getBroadcast() != null) {
                        ret.add(interfaceAddress.getBroadcast().getHostAddress());
                    }
                }
                Log.v(TAG, "Interface: Name=" + networkInterface.getName() + " Address=" + address + " 2=" + address);
            }
            Log.v(TAG, ret.size() + " broadcast addresses were found.");
            for(String address : ret) {
                Log.v(TAG, address);
            }
        } catch (SocketException e) {
            e.printStackTrace();
        }
        if(ret.size() == 0) {
            ret.add(BROADCAST_ADDRESS);
        }
        return ret.toArray(new String[]{});
    }

    public void join() throws InterruptedException {
        mThread.join();
    }

    // called from native
    @SuppressWarnings("unused")
    public void onConnected(int width, int height, int codec) {
        mCallback.onConnected(width, height, codec);
    }
    @SuppressWarnings("unused")
    public void onChangeSettings(int EnableTestMode, int suspend) {
        mCallback.onChangeSettings(EnableTestMode, suspend);
    }

    native int initializeSocket(int port, String deviceName, String[] broadcastAddrList);

    native void closeSocket();

    native void runLoop(LatencyCollector latencyCollector, String serverAddress, int serverPort);

    native void interrupt();

    native int send(byte[] buf, int length);

    native void set72Hz(boolean is72Hz);

    native String getServerAddress();
    native int getServerPort();

    //
    // NALParser interface
    //

    @Override
    public native int getNalListSize();
    @Override
    public native NAL waitNal();
    @Override
    public native NAL getNal();
    @Override
    public native void recycleNal(NAL nal);
    @Override
    public native void flushNALList();
    @Override
    public native void notifyWaitingThread();
    @Override
    public native void clearStopped();
}