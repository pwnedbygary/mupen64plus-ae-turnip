package paulscode.android.mupen64plusae.netplay.room;

import android.content.Context;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.net.wifi.WifiManager;
import android.util.Log;

import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.ArrayList;
import java.util.concurrent.atomic.AtomicBoolean;

import paulscode.android.mupen64plusae.util.Utility;

public class NetplayRoomClient {

    static final String TAG = "NetplayRoomClient";

    public interface OnServerFound
    {
        /**
         * Called when a valid server is
         * @param netplayVersion Server netplay version
         * @param serverId Server ID
         * @param serverName Server name
         * @param romMd5 Rom MD5
         */
        void onValidServerFound(int netplayVersion, int serverId, String serverName, String romMd5);

        /**
         * Called when server responds with registration information
         * @param regId Registration id
         * @param player Player number
         * @param videoPlugin Video plugin
         * @param rspPlugin RSP plugin
         * @param address Host address
         * @param port Host port
         */
        void onServerRegistration(int regId, int player, String videoPlugin, String rspPlugin, InetAddress address, int port);

        /**
         * Called when the server starts the game
         */
        void onServerStart();
    }

    // Broadcast service through NSD
    private final NsdManager mNsdManager;

    // NSD discovery listener
    private NsdManager.DiscoveryListener mDiscoveryListener;

    // Cleared once room discovery is stopped
    private final AtomicBoolean mDiscovering = new AtomicBoolean(true);

    private final WifiManager.MulticastLock mMulticastLock;

    // Device name
    private final String mDeviceName;

    // List of found servers; connection threads add to it, so lock it to use it
    private final ArrayList<NetplayRoomServerHandler> mClients = new ArrayList<>();

    private final OnServerFound mOnServerData;

    class ResolveListener implements NsdManager.ResolveListener {

        @Override
        public void onResolveFailed(NsdServiceInfo serviceInfo, int errorCode) {
            // Called when the resolve fails. Use the error code to debug.
            Log.e(TAG, "Resolve failed: " + errorCode);
        }

        @Override
        public void onServiceResolved(NsdServiceInfo serviceInfo) {
            Log.i(TAG, "Resolve Succeeded. port=" + serviceInfo.getPort());
            connectToServer(serviceInfo.getHost(), serviceInfo.getPort());
        }
    }

    public void connectToServer(String hostname, int port)
    {
        // Must run on a separate thread, running network operation on the main
        // thread leads to NetworkOnMainThreadException exceptions
        Thread connectThread = new Thread(() -> {
            try {
                InetAddress address = InetAddress.getByName(hostname);
                connectToServer(address, port);
            } catch (UnknownHostException e) {
                // The exception's message names the host.
                Log.e(TAG, "Unable to resolve the host");
            }
        });
        connectThread.setDaemon(true);
        connectThread.start();
    }

    private synchronized void connectToServer(InetAddress address, int port)
    {
        if (port >= 65536) {
            return;
        }
        // Execute a ping command to wake up the local wifi interface, not sure why this is needed
        // but it works
        Utility.executeShellCommand( "ping", "-c", "-1", address.getHostAddress() );

        final int serverId;
        synchronized (mClients) {
            serverId = mClients.size();
        }
        NetplayRoomServerHandler roomClient = new NetplayRoomServerHandler(mDeviceName, address, port,
                new NetplayRoomServerHandler.OnServerRoomData() {
                    @Override
                    public void onServerRoomData(int netplayVersion, String serverName, String romMd5) {
                        mOnServerData.onValidServerFound(netplayVersion, serverId, serverName, romMd5);
                    }

                    @Override
                    public void onServerRegistration(int regId, int player, String videoPlugin, String rspPlugin,
                                                     InetAddress address, int port) {
                        mOnServerData.onServerRegistration(regId, player, videoPlugin, rspPlugin, address, port);
                    }

                    @Override
                    public void onServerStartGame() {
                        mOnServerData.onServerStart();
                        stopListening();
                    }
                });

        synchronized (mClients) {
            if (!mClients.contains(roomClient)) {
                roomClient.connectAsync();
                mClients.add(roomClient);
            }
        }
    }

    /**
     * Constructor
     */
    public NetplayRoomClient(Context context, String deviceName, OnServerFound onServerData)
    {
        // Context for creating NSD service
        mDeviceName = deviceName;
        mOnServerData = onServerData;

        mMulticastLock = MulticastLocks.acquire(
                (WifiManager) context.getApplicationContext().getSystemService(Context.WIFI_SERVICE));

        initializeDiscoveryListener();

        mNsdManager = (NsdManager) context.getSystemService(Context.NSD_SERVICE);
        mNsdManager.discoverServices(NetplayRoomServer.DEFAULT_SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, mDiscoveryListener);
    }

    private void initializeDiscoveryListener()
    {
        mDiscoveryListener = new NsdManager.DiscoveryListener() {

            // Called as soon as service discovery begins.
            @Override
            public void onDiscoveryStarted(String regType) {
                Log.i(TAG, "NSD Service discovery started");
            }

            @Override
            public void onServiceFound(NsdServiceInfo service) {
                Log.i(TAG, "NSD Service discovery success: " + service.getServiceName());
                if (!service.getServiceType().equals(NetplayRoomServer.DEFAULT_SERVICE_TYPE)) {
                    Log.i(TAG, "NSD Unknown Service Type: " + service.getServiceType());
                } else if (service.getServiceName().contains(NetplayRoomServer.DEFAULT_SERVICE_NAME)){
                    Log.i(TAG, "NSD Found service: " + service.getServiceName());
                    mNsdManager.resolveService(service, new ResolveListener());
                }
            }

            @Override
            public void onServiceLost(NsdServiceInfo service) {
                // When the network service is no longer available.
                // Internal bookkeeping code goes here.
                Log.e(TAG, "NSD service lost: " + service.getServiceName());
            }

            @Override
            public void onDiscoveryStopped(String serviceType) {
                Log.i(TAG, "NSD Discovery stopped: " + serviceType);
            }

            @Override
            public void onStartDiscoveryFailed(String serviceType, int errorCode) {
                Log.e(TAG, "NSD Discovery failed: Error code:" + errorCode);

                try {
                    mNsdManager.stopServiceDiscovery(this);
                } catch (IllegalArgumentException e) {
                    e.printStackTrace();
                }
            }

            @Override
            public void onStopDiscoveryFailed(String serviceType, int errorCode) {
                Log.e(TAG, "NSD Discovery failed: Error code:" + errorCode);

                try {
                    mNsdManager.stopServiceDiscovery(this);
                } catch (IllegalArgumentException e) {
                    e.printStackTrace();
                }
            }
        };
    }

    public void registerServer(int serverId)
    {
        synchronized (mClients) {
            mClients.get(serverId).registerToRoomAsync();
        }
    }

    public void stopListening()
    {
        stopDiscovery();
        MulticastLocks.release(mMulticastLock);
    }

    public void leaveServer() {
        synchronized (mClients) {
            for (NetplayRoomServerHandler serverHandler : mClients) {
                serverHandler.leaveRoomAsync();
            }
        }

        stopDiscovery();
        MulticastLocks.release(mMulticastLock);
    }

    private void stopDiscovery()
    {
        if (!mDiscovering.getAndSet(false)) {
            return;
        }

        try {
            mNsdManager.stopServiceDiscovery(mDiscoveryListener);
        } catch (IllegalArgumentException e) {
            // Discovery that failed to start has no listener registered
            e.printStackTrace();
        }
    }
}
