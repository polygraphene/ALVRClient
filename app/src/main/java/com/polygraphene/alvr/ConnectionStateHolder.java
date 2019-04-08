package com.polygraphene.alvr;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * Hold previous connection state to recover connection after resume app.
 */
public class ConnectionStateHolder {

    private static final String KEY_SERVER_ADDRESS = "serverAddress";
    private static final String KEY_SERVER_PORT = "serverPort";

    public static class ConnectionState {
        public String serverAddr;
        public int serverPort;
    }

    public static void saveConnectionState(Context context, String serverAddr, int serverPort) {
        SharedPreferences pref = context.getSharedPreferences("pref", Context.MODE_PRIVATE);
        SharedPreferences.Editor edit = pref.edit();
        // If server address is NULL, it means no preserved connection.
        edit.putString(KEY_SERVER_ADDRESS, serverAddr);
        edit.putInt(KEY_SERVER_PORT, serverPort);
        edit.apply();
    }

    public static void loadConnectionState(Context context, ConnectionState connectionState) {
        SharedPreferences pref = context.getSharedPreferences("pref", Context.MODE_PRIVATE);
        connectionState.serverAddr = pref.getString(KEY_SERVER_ADDRESS, null);
        connectionState.serverPort = pref.getInt(KEY_SERVER_PORT, 0);

        saveConnectionState(context, null, 0);
    }
}
