package com.polygraphene.alvr;

import com.google.gson.Gson;
import com.koushikdutta.async.AsyncNetworkSocket;
import com.koushikdutta.async.AsyncServer;
import com.koushikdutta.async.AsyncServerSocket;
import com.koushikdutta.async.AsyncSocket;
import com.koushikdutta.async.ByteBufferList;
import com.koushikdutta.async.DataEmitter;
import com.koushikdutta.async.callback.DataCallback;
import com.koushikdutta.async.callback.ListenCallback;

import java.net.InetAddress;
import java.net.UnknownHostException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

public class LauncherSocket {
    private static final String TAG = "LauncherSocket";
    private boolean mConnected = false;
    private AsyncSocket mSocket;
    private AsyncServerSocket mServerSocket;
    private Gson mGson = new Gson();
    private int mRequestId = 1;
    private int mReadState = 0;
    private int mLength = 0;
    private int mRemaining = 0;
    byte[] mReadBuffer;

    private class Command {
        public int requestId;
        public String command;

        Command(int requestId, String command) {
            this.requestId = requestId;
            this.command = command;
        }
    }

    public interface LauncherSocketCallback {
        void onConnect();
    }

    LauncherSocketCallback mCallback;

    public LauncherSocket(LauncherSocketCallback callback) {
        mCallback = callback;
    }

    public void listen() {
        try {
            AsyncServer.getDefault().listen(InetAddress.getByName("0.0.0.0"), 9944, new ListenCallback() {
                @Override
                public void onAccepted(AsyncSocket socket) {
                    AsyncNetworkSocket networkSocket = (AsyncNetworkSocket) socket;
                    if (mConnected) {
                        Utils.logi(TAG, () -> "Ignored connection request while connected. Address=" + networkSocket.getRemoteAddress().toString());
                        return;
                    }
                    Utils.logi(TAG, () -> "Connected. Address=" + networkSocket.getRemoteAddress().toString());
                    mConnected = true;
                    mSocket = socket;
                    socket.setDataCallback(LauncherSocket.this::onDataAvailable);
                    mCallback.onConnect();
                }

                @Override
                public void onListening(AsyncServerSocket socket) {
                    mServerSocket = socket;
                }

                @Override
                public void onCompleted(Exception ex) {

                }
            });
        } catch (UnknownHostException ignored) {
        }
    }

    public void close() {
        Utils.logi(TAG, () -> "Close.");
        if (mServerSocket != null) {
            mServerSocket.stop();
            mServerSocket = null;
        }
        if (mSocket != null) {
            sendCommand("Close");
            mSocket.end();
            mSocket.close();
            mSocket = null;
        }
    }

    public void sendCommand(String commandName) {
        String json = mGson.toJson(new Command(mRequestId, commandName));
        ByteBufferList byteBufferList = new ByteBufferList();

        byte[] buffer = json.getBytes(StandardCharsets.UTF_8);
        byte[] length = new byte[] {(byte)buffer.length, (byte)(buffer.length >> 8), (byte)(buffer.length >> 16), (byte)(buffer.length >> 24)};

        byteBufferList.add(ByteBuffer.wrap(length));
        byteBufferList.add(ByteBuffer.wrap(buffer));
        mSocket.write(byteBufferList);
    }

    public boolean isConnected() {
        return mConnected;
    }

    private void onDataAvailable(DataEmitter emitter, ByteBufferList bb) {
        bb.order(ByteOrder.LITTLE_ENDIAN);

        while (bb.remaining() > 0) {
            if (mReadState == 0) {
                mRemaining = mLength = bb.getInt();
                mReadBuffer = new byte[mLength];

                mReadState = 1;
            } else if (mReadState == 1) {
                int r = bb.remaining();
                if (r < mRemaining) {
                    // No sufficient buffer.
                    bb.get(mReadBuffer, mLength - mRemaining, r);
                    mRemaining -= r;
                    return;
                }
                bb.get(mReadBuffer, mLength - mRemaining, mRemaining);

                onReceive();
                mReadState = 0;
            }
        }
    }

    private void onReceive() {
        Command command = mGson.fromJson(new String(mReadBuffer, StandardCharsets.UTF_8), Command.class);
        if(command.command.equals("Close")) {
            Utils.logi(TAG, () -> "Connection closed by server.");
            if(mSocket != null) {
                mSocket.end();
                mSocket.close();
                mSocket = null;
            }
            mConnected = false;
        } else {
            Utils.loge(TAG, () -> "Unknown command received. command=" + command.command + " requestId=" + command.requestId);
        }
    }
}
