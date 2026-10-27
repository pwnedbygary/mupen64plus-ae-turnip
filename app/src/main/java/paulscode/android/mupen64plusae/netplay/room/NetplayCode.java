package paulscode.android.mupen64plusae.netplay.room;

import androidx.annotation.Nullable;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.UnknownHostException;

/**
 * Netplay codes, which give players the host's public IPv4 address and room port without a
 * server. A code is the 48-bit value (address << 16) | port written as 10 characters of
 * Crockford's base 32, most significant first, then 2 check characters for the value mod 1021,
 * so it never contains I, L, O or U.
 */
final class NetplayCode
{
    private static final String ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    private static final int LENGTH = 12;
    private static final int BITS_PER_CHARACTER = 5;
    private static final int CHECK_BITS = 2 * BITS_PER_CHARACTER;
    // The largest prime below 32^2, so the check fits in 2 characters. A prime above 33 divides
    // no power of 32 and no number from 1 to 33, so the check catches every single changed
    // character and every swap of two neighboring characters:
    // - in the value, a changed character changes it by d * 32^k with 0 < |d| < 32, and a swap
    //   by (a - b) * 31 * 32^k, so its remainder changes;
    // - a changed check character, or a swap of the two, changes the check but not the value;
    // - a swap of the last value character and the first check character changes the value by
    //   d and the check by -32 * d, and the new check matches only if 1021 divides 33 * d.
    private static final int CHECK_MODULUS = 1021;
    private static final long VALUE_LIMIT = 1L << 48;
    private static final int MAX_PORT = 65535;

    // Networks that the internet can't reach, as {address, prefix length}
    private static final int[][] NON_PUBLIC_NETWORKS = {
            {0x00000000, 8},  // 0.0.0.0/8
            {0x0A000000, 8},  // 10.0.0.0/8
            {0x64400000, 10}, // 100.64.0.0/10, carrier-grade NAT
            {0x7F000000, 8},  // 127.0.0.0/8
            {0xA9FE0000, 16}, // 169.254.0.0/16
            {0xAC100000, 12}, // 172.16.0.0/12
            {0xC0000000, 24}, // 192.0.0.0/24
            {0xC0A80000, 16}, // 192.168.0.0/16
            {0xC6120000, 15}, // 198.18.0.0/15
            {0xE0000000, 3},  // 224.0.0.0/3, multicast and reserved
    };

    private NetplayCode()
    {
    }

    /**
     * Makes the code for a host.
     *
     * @param address the host's public address
     * @param port the room port, 1 to 65535
     * @return the code, 12 characters
     */
    static String encode(Inet4Address address, int port)
    {
        if (port < 1 || port > MAX_PORT) {
            throw new IllegalArgumentException("Port out of range: " + port);
        }

        long value = (toInt(address) & 0xFFFFFFFFL) << 16 | port;
        long digits = value << CHECK_BITS | value % CHECK_MODULUS;
        char[] code = new char[LENGTH];
        for (int index = LENGTH - 1; index >= 0; --index) {
            code[index] = ALPHABET.charAt((int) (digits & 0x1F));
            digits >>>= BITS_PER_CHARACTER;
        }
        return new String(code);
    }

    /**
     * Writes a code from {@link #encode} as players see it, in three groups of four characters.
     */
    static String toDisplayForm(String code)
    {
        return code.substring(0, 4) + "-" + code.substring(4, 8) + "-" + code.substring(8);
    }

    /**
     * Reads what a player typed or pasted: a code, in any case and with or without spaces and
     * hyphens, or an address and port written as a.b.c.d:port. Spaces, no-break spaces included,
     * are ignored in both, and so are the zero-width spaces, joiners and direction marks that
     * some chat apps add to copied text (U+200B to U+200F, U+2060 and U+FEFF). Other invisible
     * characters, such as the controls that embed, override or isolate a direction of text,
     * make the text invalid.
     *
     * @return the host's address and room port, or null if the text is neither
     */
    @Nullable
    static InetSocketAddress decode(String text)
    {
        StringBuilder compact = new StringBuilder();
        for (int index = 0; index < text.length(); ++index) {
            char character = text.charAt(index);
            if (!isIgnored(character)) {
                compact.append(character);
            }
        }

        int colon = compact.lastIndexOf(":");
        if (colon >= 0) {
            Inet4Address address = parseAddress(compact.substring(0, colon));
            int port = parseNumber(compact.substring(colon + 1), 5);
            return address != null && port >= 1 && port <= MAX_PORT ?
                    new InetSocketAddress(address, port) : null;
        }

        long digits = 0;
        int length = 0;
        for (int index = 0; index < compact.length(); ++index) {
            char character = compact.charAt(index);
            if (character == '-') {
                continue;
            }

            int digit = digitValue(character);
            if (digit < 0 || ++length > LENGTH) {
                return null;
            }
            digits = digits << BITS_PER_CHARACTER | digit;
        }

        long value = digits >>> CHECK_BITS;
        long check = digits & ((1L << CHECK_BITS) - 1);
        int port = (int) (value & 0xFFFF);
        if (length != LENGTH || value >= VALUE_LIMIT || port == 0
                || check != value % CHECK_MODULUS) {
            return null;
        }
        return new InetSocketAddress(toAddress((int) (value >>> 16)), port);
    }

    /**
     * Reads an address written as a.b.c.d, without looking up a host name.
     *
     * @return the address, or null if the text is not one
     */
    @Nullable
    static Inet4Address parseAddress(String text)
    {
        String[] parts = text.split("\\.", -1);
        if (parts.length != 4) {
            return null;
        }

        int value = 0;
        for (String part : parts) {
            int number = parseNumber(part, 3);
            // Some programs read a number with a leading zero as octal
            if (number < 0 || number > 255 || (part.length() > 1 && part.charAt(0) == '0')) {
                return null;
            }
            value = value << 8 | number;
        }
        return toAddress(value);
    }

    /**
     * Tells whether players can reach an address from the internet.
     *
     * @return false for addresses in the networks that the internet can't reach, such as private
     *         networks and the shared network of carrier-grade NAT, true for the others
     */
    static boolean isPublic(Inet4Address address)
    {
        int value = toInt(address);
        for (int[] network : NON_PUBLIC_NETWORKS) {
            int mask = -1 << (32 - network[1]);
            if ((value & mask) == network[0]) {
                return false;
            }
        }
        return true;
    }

    // Returns the value of 1 to maxDigits ASCII digits, or -1 if the text is not that
    private static int parseNumber(String text, int maxDigits)
    {
        if (text.isEmpty() || text.length() > maxDigits) {
            return -1;
        }

        int number = 0;
        for (int index = 0; index < text.length(); ++index) {
            char character = text.charAt(index);
            if (character < '0' || character > '9') {
                return -1;
            }
            number = number * 10 + (character - '0');
        }
        return number;
    }

    // Returns whether decode skips the character: a space, or a zero-width character that chat
    // apps add to copied text
    private static boolean isIgnored(char character)
    {
        return Character.isWhitespace(character) || Character.isSpaceChar(character)
                || (character >= '\u200B' && character <= '\u200F')
                || character == '\u2060' || character == '\uFEFF';
    }

    // Returns the value of a code character, or -1 if it is not one
    private static int digitValue(char character)
    {
        char upper = character >= 'a' && character <= 'z' ? (char) (character - 'a' + 'A') : character;
        if (upper == 'O') {
            return 0;
        }
        if (upper == 'I' || upper == 'L') {
            return 1;
        }
        return ALPHABET.indexOf(upper);
    }

    private static int toInt(Inet4Address address)
    {
        byte[] bytes = address.getAddress();
        return (bytes[0] & 0xFF) << 24 | (bytes[1] & 0xFF) << 16 | (bytes[2] & 0xFF) << 8 | (bytes[3] & 0xFF);
    }

    private static Inet4Address toAddress(int value)
    {
        byte[] bytes = {(byte) (value >>> 24), (byte) (value >>> 16), (byte) (value >>> 8), (byte) value};
        try {
            return (Inet4Address) InetAddress.getByAddress(bytes);
        } catch (UnknownHostException e) {
            // Only thrown for an array that isn't 4 or 16 bytes long
            throw new IllegalStateException(e);
        }
    }
}
