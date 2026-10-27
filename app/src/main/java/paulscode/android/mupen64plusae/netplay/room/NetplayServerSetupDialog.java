package paulscode.android.mupen64plusae.netplay.room;

import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.text.SpannableString;
import android.text.TextUtils;
import android.text.style.StyleSpan;
import android.text.style.UnderlineSpan;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.RelativeLayout;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.fragment.app.DialogFragment;

import paulscode.android.mupen64plusae.R;

import paulscode.android.mupen64plusae.dialog.ThemedAlertDialog;
import paulscode.android.mupen64plusae.ui.UiTheme;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.ArrayList;
import java.util.List;
import java.util.ListIterator;
import java.util.Locale;

import paulscode.android.mupen64plusae.persistent.AppData;
import paulscode.android.mupen64plusae.persistent.GlobalPrefs;
import paulscode.android.mupen64plusae.util.DeviceUtil;
import paulscode.android.mupen64plusae.util.DisplayWrapper;
import paulscode.android.mupen64plusae.util.Notifier;

public class NetplayServerSetupDialog extends DialogFragment
{
    static final String TAG = "NpServerSetupDialog";

    public interface OnClientDialogActionListener {
        /**
         * Called when asked to connet
         * @param regId Registration id
         * @param player Player number, 1-4
         * @param videoPlugin Video plugin
         * @param rspPlugin RSP plugin
         * @param address Server address
         * @param port Server port
         */
        void connect(int regId, int player, String videoPlugin, String rspPlugin, InetAddress address, int port);

        /**
         * Called when emulation is started
         */
        void start();

        /**
         * Called when netplay is cancelled
         */
        void cancel();

        /**
         * Called when we want to use port forwarding in the router to map the room port
         * as well as whatever port the netplay server is using
         * @param roomPort Port number being used by the room
         */
        void mapPorts(int roomPort);
    }

    private static final String ROM_MD5 = "ROM_MD5";
    private static final String VIDEO_PLUGIN = "VIDEO_PLUGIN";
    private static final String RSP_PLUGIN = "RSP_PLUGIN";
    private static final String SERVER_PORT = "SERVER_PORT";

    private ClientListAdapter mServerListAdapter = null;

    private final ArrayList<NetplayClient> mClients = new ArrayList<>();

    private NetplayRoomServer mNetplayRoomService = null;

    private String mNetplayCode = "";
    private String mNetplayAddress = "";
    private String mNetplayMessage = "";

    private Button mGetCodeButton;
    private Button mAdvancedButton;
    private LinearLayout mServerLayout;
    private EditText mCodeTextView;
    private TextView mCodeAddressView;
    private TextView mCodeMessageView;
    private RelativeLayout mCodeLayout;

    // Activity holding this fragment
    private Activity mActivity;

    private boolean mShowingAdvanced = false;
    private boolean mShowingCode = false;

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    /**
     *
     * @return A cheat dialog
     */
    public static NetplayServerSetupDialog newInstance(String romMd5, String videoPlugin, String rspPlugin, int serverPort)
    {
        NetplayServerSetupDialog frag = new NetplayServerSetupDialog();
        Bundle args = new Bundle();
        args.putString(ROM_MD5, romMd5);
        args.putString(VIDEO_PLUGIN, videoPlugin);
        args.putString(RSP_PLUGIN, rspPlugin);
        args.putInt(SERVER_PORT, serverPort);
        frag.setArguments(args);
        return frag;
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Keep this instance, and with it the room server, when the activity is recreated for a
        // configuration change: onCreateDialog() relies on it to build the server only once
        setRetainInstance(true);
        setStyle(DialogFragment.STYLE_NORMAL, R.style.Theme_Mupen64plusaeTheme);
    }

    @Override
    @NonNull
    public Dialog onCreateDialog(Bundle savedInstanceState)
    {
        Bundle args = getArguments();

        if (args == null) {
            args = new Bundle();
        }

        mActivity = requireActivity();

        final String romMd5 = args.getString(ROM_MD5);
        final String videoPlugin = args.getString(VIDEO_PLUGIN);
        final String rspPlugin = args.getString(RSP_PLUGIN);

        final int serverPort = getArguments().getInt(SERVER_PORT);

        View dialogView = View.inflate(mActivity, R.layout.netplay_server_setup_dialog, null);

        ListView serverListView = dialogView.findViewById(R.id.clientList);
        Button startButton = dialogView.findViewById(R.id.buttonStart);

        mServerListAdapter = new ClientListAdapter(mActivity, mClients);
        serverListView.setAdapter(mServerListAdapter);

        Button cancelButton = dialogView.findViewById(R.id.buttonCancel);
        mGetCodeButton = dialogView.findViewById(R.id.buttonGetCode);
        mAdvancedButton = dialogView.findViewById(R.id.buttonAdvanced);

        mServerLayout = dialogView.findViewById(R.id.serverLayout);
        mCodeLayout = dialogView.findViewById(R.id.codeLayout);
        mCodeTextView = dialogView.findViewById(R.id.textCodeValue);
        mCodeTextView.setInputType(InputType.TYPE_NULL);
        mCodeTextView.setTextIsSelectable(true);
        mCodeTextView.setKeyListener(null);
        mCodeAddressView = dialogView.findViewById(R.id.textCodeAddress);
        mCodeMessageView = dialogView.findViewById(R.id.textCodeMessage);

        mServerLayout.setVisibility(mShowingAdvanced ? View.VISIBLE : View.GONE);
        mAdvancedButton.setVisibility(mShowingAdvanced ? View.GONE : View.VISIBLE);
        showCode();

        TextView serverAddress = dialogView.findViewById(R.id.textHostAddress);
        TextView port1 = dialogView.findViewById(R.id.textPort1);
        TextView port2 = dialogView.findViewById(R.id.textPort2);

        //Time to create the dialog
        AlertDialog.Builder builder = ThemedAlertDialog.newBuilder(mActivity);
        builder.setNegativeButton(null, null);
        builder.setPositiveButton(null, null);
        builder.setView(dialogView);

        AlertDialog dialog = builder.create();
        dialog.setOnShowListener(d -> UiTheme.get(mActivity).applyToDialog(dialog));
        DisplayWrapper.setDialogToResizeWithKeyboard(dialog, dialogView);
        dialog.setCanceledOnTouchOutside(false);
        dialog.setCancelable(false);
        setCancelable(false);

        cancelButton.setOnClickListener(v -> {
            if (mNetplayRoomService != null) {
                mNetplayRoomService.stopServer();
            }

            if (mActivity instanceof OnClientDialogActionListener) {
                ((OnClientDialogActionListener) mActivity).cancel();
            } else {
                Log.e(TAG, "Invalid activity, expected OnClientDialogActionListener");
            }
        });

        InetAddress address = DeviceUtil.getIPAddress();

        if (address != null) {
            String serverInfoText = "" + address.getHostAddress();

            SpannableString spanString = new SpannableString(serverInfoText);
            spanString.setSpan(new UnderlineSpan(), 0, spanString.length(), 0);
            spanString.setSpan(new StyleSpan(Typeface.ITALIC), 0, spanString.length(), 0);

            serverAddress.setText(spanString);
        }

        String deviceName = DeviceUtil.getDeviceName(getContext(), mActivity.getContentResolver());

        // Add ourselves
        if (mClients.size() == 0) {
            mClients.add(new NetplayClient(1, deviceName));
            mServerListAdapter.notifyDataSetChanged();

            mNetplayRoomService = new NetplayRoomServer(mActivity.getApplicationContext(),
                    deviceName, romMd5, videoPlugin, rspPlugin, serverPort,
                    new NetplayRoomServer.OnClientFound() {
                        @Override
                        public void onClientRegistration(int playerNumber, String deviceName) {
                            try {
                                mActivity.runOnUiThread(() -> {
                                    mClients.add(new NetplayClient(playerNumber, deviceName));
                                    mServerListAdapter.notifyDataSetChanged();
                                });
                            } catch (IllegalStateException e) {
                                e.printStackTrace();
                            }
                        }

                        @Override
                        public void onClienLeave(int playerNumber) {
                            try {

                                mActivity.runOnUiThread(() -> {
                                    ListIterator<NetplayClient> iter = mClients.listIterator();
                                    while(iter.hasNext()){
                                        if(iter.next().mPlayerNumer == playerNumber){
                                            iter.remove();
                                        }
                                    }
                                    mServerListAdapter.notifyDataSetChanged();
                                });
                            } catch (IllegalStateException e) {
                                e.printStackTrace();
                            }
                        }
                    });

            int registrationId = mNetplayRoomService.registerPlayerOne();

            if (mActivity instanceof OnClientDialogActionListener) {

                if (address == null) {
                    byte[] ipAddr = new byte[]{127, 0, 0, 1};
                    try {
                        address = InetAddress.getByAddress(ipAddr);
                    } catch (UnknownHostException e) {
                        e.printStackTrace();
                    }
                }

                if (address != null) {
                    OnClientDialogActionListener listener = (OnClientDialogActionListener)mActivity;
                    listener.connect(registrationId, 1, videoPlugin, rspPlugin, address, serverPort);
                }
            } else {
                Log.e(TAG, "Invalid activity, expected OnClientDialogActionListener");
            }
        }

        if (mNetplayRoomService != null) {
            AppData appData = new AppData(mActivity);
            GlobalPrefs globalPrefs = new GlobalPrefs(mActivity, appData);

            int port = globalPrefs.useUpnpToMapNetplayPorts ? mNetplayRoomService.getServerPort() : globalPrefs.netplayRoomTcpPort;
            SpannableString port1SpanString = new SpannableString(String.format(Locale.getDefault(), "%d", port));
            port1SpanString.setSpan(new UnderlineSpan(), 0, port1SpanString.length(), 0);
            port1SpanString.setSpan(new StyleSpan(Typeface.ITALIC), 0, port1SpanString.length(), 0);

            port1.setText(port1SpanString);
        }

        SpannableString port2SpanString = new SpannableString(String.format(Locale.getDefault(), "%d", serverPort));
        port2SpanString.setSpan(new UnderlineSpan(), 0, port2SpanString.length(), 0);
        port2SpanString.setSpan(new StyleSpan(Typeface.ITALIC), 0, port2SpanString.length(), 0);
        port2.setText(port2SpanString);

        startButton.setOnClickListener(v -> {
            if (mActivity instanceof OnClientDialogActionListener) {
                OnClientDialogActionListener listener = (OnClientDialogActionListener)mActivity;
                mNetplayRoomService.start();
                listener.start();
            } else {
                Log.e(TAG, "Invalid activity, expected OnClientDialogActionListener");
            }
        });

        mGetCodeButton.setOnClickListener(v -> {
            if (mActivity instanceof OnClientDialogActionListener) {
                OnClientDialogActionListener listener = (OnClientDialogActionListener)mActivity;
                listener.mapPorts(mNetplayRoomService.getServerPort());

                // Show an error if it takes too long to get a code
                mHandler.postDelayed(() -> {
                    if (!mShowingCode) {
                        Notifier.showToast(mActivity, R.string.netplay_codeRetrieveFailure);
                    }
                }, 20000);

            } else {
                Log.e(TAG, "Invalid activity, expected OnClientDialogActionListener");
            }
        });

        mAdvancedButton.setOnClickListener(v -> {
            mShowingAdvanced = true;
            mServerLayout.setVisibility(View.VISIBLE);
            mAdvancedButton.setVisibility(View.GONE);
        });

        dialog.getWindow().requestFeature(Window.FEATURE_NO_TITLE);

        return dialog;
    }

    static class NetplayClient implements Comparable<NetplayClient>
    {
        private final int mPlayerNumer;
        private final String mPlayerName;

        NetplayClient(int playerNumber, String playerName)
        {
            mPlayerNumer = playerNumber;
            mPlayerName = playerName;
        }

        @Override
        public int compareTo(NetplayClient other) {
            return toString().compareTo(other.toString());
        }

        @Override
        @NonNull
        public String toString() {
            return "" + mPlayerNumer + ": " + mPlayerName;
        }
    }

    private static class ClientListAdapter extends ArrayAdapter<NetplayClient>
    {
        private static final int RESID = R.layout.list_single_text;

        ClientListAdapter(Context context, List<NetplayClient> clients)
        {
            super(context, RESID, clients);
        }

        @Override
        public @NonNull View getView(int position, @Nullable View convertView,
                                     @NonNull ViewGroup parent)
        {
            Context context = getContext();
            LayoutInflater inflater = (LayoutInflater) context
                    .getSystemService( Context.LAYOUT_INFLATER_SERVICE );
            View view = convertView;
            if( view == null )
                view = inflater.inflate( RESID, null );

            NetplayClient item = getItem( position );
            if( item != null )
            {
                TextView text1 = view.findViewById( R.id.text1 );
                String player = item.mPlayerNumer + ": " + item.mPlayerName;
                text1.setText(player);
            }
            return view;
        }
    }

    private void showCode()
    {
        mCodeTextView.setText(mNetplayCode);
        mCodeAddressView.setText(mNetplayAddress);
        mCodeMessageView.setText(mNetplayMessage);
        mCodeLayout.setVisibility(TextUtils.isEmpty(mNetplayCode) ? View.GONE : View.VISIBLE);
        mCodeMessageView.setVisibility(TextUtils.isEmpty(mNetplayMessage) ? View.GONE : View.VISIBLE);
        mGetCodeButton.setVisibility(mShowingCode ? View.GONE : View.VISIBLE);
    }

    /**
     * Callback when a UDP port has been mapped
     * @param tcpPort1 Port for room server
     * @param tcpPort2 Port for TCP netplay server
     * @param udpPort2 Port for UDP netplay server
     * @param publicAddress Public IPv4 address the router reported, empty if it reported none,
     *                      or null if the ports were not mapped automatically or mapping failed
     */
    public void onUpnpPortsObtained(int tcpPort1, int tcpPort2, int udpPort2, String publicAddress)
    {
        Log.i(TAG, "Mapped ports through UPnP: " + tcpPort1 + " " + tcpPort2 + " " + udpPort2);

        // -1 when mapping failed, 0 when the room server could not open its socket
        if (tcpPort1 <= 0)
        {
            mActivity.runOnUiThread(() -> Notifier.showToast(mActivity, R.string.netplay_codeRetrieveFailure));
            return;
        }

        mNetplayRoomService.updateServerPort(tcpPort2);

        Inet4Address routerAddress = publicAddress != null ? NetplayCode.parseAddress(publicAddress) : null;
        boolean addressIsPublic = routerAddress != null && NetplayCode.isPublic(routerAddress);

        if (publicAddress != null) {
            Log.i(TAG, routerAddress == null ? "Router reported no IPv4 address" :
                    "Router reported a public address: " + addressIsPublic);
        }

        mActivity.runOnUiThread(() -> {
            if (publicAddress == null) {
                mNetplayCode = "";
                mNetplayAddress = "";
                mNetplayMessage = mActivity.getString(R.string.netplay_sharePublicAddress, String.valueOf(tcpPort1));
            } else if (routerAddress == null) {
                mNetplayCode = "";
                mNetplayAddress = "";
                mNetplayMessage = mActivity.getString(R.string.netplay_noRouterAddress, String.valueOf(tcpPort1));
            } else if (addressIsPublic) {
                mNetplayCode = NetplayCode.toDisplayForm(NetplayCode.encode(routerAddress, tcpPort1));
                mNetplayAddress = routerAddress.getHostAddress() + ":" + tcpPort1;
                mNetplayMessage = "";
            } else {
                mNetplayCode = "";
                mNetplayAddress = "";
                mNetplayMessage = mActivity.getString(R.string.netplay_noPublicAddress);
            }
            mShowingCode = true;
            showCode();
        });
    }
}
