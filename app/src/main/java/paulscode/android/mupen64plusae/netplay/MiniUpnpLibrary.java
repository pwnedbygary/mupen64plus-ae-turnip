package paulscode.android.mupen64plusae.netplay;

import com.sun.jna.Callback;
import com.sun.jna.JNIEnv;
import com.sun.jna.Library;
import com.sun.jna.Pointer;

/**
 * Library used to interface with AE Vid Ext implementation
 */

@SuppressWarnings("unused")
public interface MiniUpnpLibrary extends Library {

    // Initialize library

    /**
     * Initialize library
     * @param timeout Timeout for initialization
     */
    void UPnPInit(final int timeout);

    /**
     * Shutdown library
     */
    void UPnPShutdown();

    /**
     * Add UPNP port forward
     * @param protocol Protocol, either "TCP" or "UDP"
     * @param description Port description
     * @param port External port
     * @param intport Internal port
     * @return true if success
     */
    boolean UPnP_Add(String protocol, String description, int port, int intport);

    /**
     * Remove UPNP port forward
     * @param protocol Protocol, either "TCP" or "UDP"
     * @param port External port
     */
    boolean UPnP_Remove(String protocol, int port);

    /**
     * Get the router's external IPv4 address through UPnP
     * @return Dotted address, or an empty string if UPnP is not initialized or the router reports none
     */
    String UPnP_GetExternalAddress();

    /**
     * Initialize NAT PMP library
     */
    void NATPMP_Init(int gatewayIp);

    /**
     * Shutdown NAT PMP library
     */
    void NATPMP_Shutdown();

    /**
     * Add NAT-PMP port forward
     * @param protocol Protocol, either "TCP" or "UDP"
     * @param port External port
     * @param intport Internal port
     * @return true if success
     */
    boolean NATPMP_Add(String protocol, int port, int intport);

    /**
     * Remove NAT-PMP port forward
     * @param protocol Protocol, either "TCP" or "UDP"
     * @param port External port
     */
    boolean NATPMP_Remove(String protocol, int port);

    /**
     * Get the public IPv4 address the router reported when NAT-PMP was initialized
     * @return Dotted address, or an empty string if none was received
     */
    String NATPMP_GetPublicAddress();
}