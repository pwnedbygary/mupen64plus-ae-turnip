package paulscode.android.mupen64plusae.netplay.room;

import org.junit.Test;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.UnknownHostException;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

public class NetplayCodeTest {

    // The value of a code is (address << 16) | port, written as 10 Crockford base 32 digits,
    // most significant first: 0-9 are 0-9, then A=10 ... H=17 J=18 K=19 M=20 N=21 P=22 ... T=26
    // V=27 ... Z=31 (no I, L, O or U). Then come 2 digits for the check, value mod 1021.
    // As 2^10 = 1024 = 1021 + 3, mod 1021 2^10 is 3. The addresses are documentation addresses.

    private static final String CROCKFORD_DIGITS = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

    // 203.0.113.5:43821, from encodesTypicalHost
    private static final String TYPICAL_CODE = "6B01RGBASDWS";

    @Test
    public void encodesSmallestAddressAndPort() throws Exception {
        // 0.0.0.1:1 -> 1 * 2^16 + 1 = 65537 = 2*32^3 + 1 -> 0000002001;
        // 65537 = 64*1021 + 193 and 193 = 6*32 + 1 -> check 61
        assertCode("000000200161", address(0, 0, 0, 1), 1);
    }

    @Test
    public void encodesLargestAddressAndPort() throws Exception {
        // 255.255.255.255:65535 -> 2^48 - 1 = 7*32^9 + (32^9 - 1), as 32^9 = 2^45
        // -> 7 then nine 31s -> 7ZZZZZZZZZ;
        // mod 1021 2^48 = (2^10)^4 * 2^8 is 3^4 * 256 = 20736 = 20*1021 + 316, so 2^48 - 1
        // is 315 = 9*32 + 27 -> check 9V
        assertCode("7ZZZZZZZZZ9V", address(255, 255, 255, 255), 65535);
    }

    @Test
    public void encodesTypicalHost() throws Exception {
        // 203.0.113.5 = 0xCB007105 and 43821 = 0xAB2D -> 0xCB007105AB2D = 223202756635437;
        // 5-bit groups 00110 01011 00000 00001 11000 10000 01011 01010 11001 01101
        // = 6 11 0 1 24 16 11 10 25 13 -> 6B01RGBASD;
        // mod 1021 2^16 is 3*64 = 192, 2^24 is 192*256 = 49152 = 48*1021 + 144,
        // 203.0.113.5 is 203*144 + 113*256 + 5 = 58165 = 56*1021 + 989, and the value is
        // 989*192 + 43821 = 233709 = 228*1021 + 921; 921 = 28*32 + 25 -> check WS
        assertCode(TYPICAL_CODE, address(203, 0, 113, 5), 43821);
    }

    @Test
    public void lettersSkipILOAndU() throws Exception {
        // 0.0.0.0 with port 32 + v -> value 1*32 + v, below 1021, so the check is the value
        // itself: 000000001 and the character for v, then 1 and that character again
        Inet4Address zero = address(0, 0, 0, 0);
        assertCode("000000001A1A", zero, 32 + 10);
        assertCode("000000001H1H", zero, 32 + 17);
        assertCode("000000001J1J", zero, 32 + 18);
        assertCode("000000001K1K", zero, 32 + 19);
        assertCode("000000001M1M", zero, 32 + 20);
        assertCode("000000001N1N", zero, 32 + 21);
        assertCode("000000001P1P", zero, 32 + 22);
        assertCode("000000001T1T", zero, 32 + 26);
        assertCode("000000001V1V", zero, 32 + 27);
        assertCode("000000001Z1Z", zero, 32 + 31);
    }

    @Test
    public void decodesWhatItEncodes() throws Exception {
        Inet4Address[] addresses = {address(0, 0, 0, 1), address(198, 51, 100, 200),
                address(203, 0, 113, 5), address(255, 255, 255, 255)};
        int[] ports = {1, 1024, 43821, 65535};

        for (Inet4Address address : addresses) {
            for (int port : ports) {
                String code = NetplayCode.encode(address, port);
                assertHost(NetplayCode.decode(code), address, port);
                assertHost(NetplayCode.decode(NetplayCode.toDisplayForm(code)), address, port);
            }
        }
    }

    @Test
    public void displayFormHasThreeGroupsOfFour() {
        assertEquals("6B01-RGBA-SDWS", NetplayCode.toDisplayForm(TYPICAL_CODE));
        assertEquals("0000-0020-0161", NetplayCode.toDisplayForm("000000200161"));
    }

    @Test
    public void ignoresCaseSpacesAndHyphens() throws Exception {
        Inet4Address host = address(203, 0, 113, 5);
        assertHost(NetplayCode.decode("6B01-RGBA-SDWS"), host, 43821);
        assertHost(NetplayCode.decode("6b01-rgba-sdws"), host, 43821);
        assertHost(NetplayCode.decode("6b01RgBaSdWs"), host, 43821);
        assertHost(NetplayCode.decode(" 6B01 RGBA SDWS "), host, 43821);
        assertHost(NetplayCode.decode("6B01 - RGBA - SDWS"), host, 43821);
        assertHost(NetplayCode.decode("6-B-0-1-R-G-B-A-S-D-W-S"), host, 43821);
        assertHost(NetplayCode.decode("\t6B01\u00A0RGBA SDWS\n"), host, 43821);
    }

    @Test
    public void ignoresZeroWidthCharactersAndNoBreakSpacesInCodes() throws Exception {
        // Zero-width space U+200B, zero-width non-joiner and joiner U+200C and U+200D,
        // left-to-right and right-to-left marks U+200E and U+200F, word joiner U+2060, byte
        // order mark U+FEFF. U+00A0 is a no-break space. Before, inside and after the code
        Inet4Address host = address(203, 0, 113, 5);
        assertHost(NetplayCode.decode("\u200B6B01-RGBA-SDWS"), host, 43821);
        assertHost(NetplayCode.decode("6B01\u200ERGBA\u2060SDWS"), host, 43821);
        assertHost(NetplayCode.decode("6B01-RGBA-SDWS\uFEFF"), host, 43821);
        assertHost(NetplayCode.decode("\u200F6B01\u00A0RGBA-\u200BSDWS\u00A0\u200E"), host, 43821);
        assertHost(NetplayCode.decode("6B01\u200CRGBA\u200DSDWS"), host, 43821);
    }

    @Test
    public void ignoresZeroWidthCharactersAndNoBreakSpacesInAddresses() throws Exception {
        // The same characters before, inside and after an address and port
        Inet4Address host = address(203, 0, 113, 5);
        assertHost(NetplayCode.decode("\uFEFF203.0.113.5:43821"), host, 43821);
        assertHost(NetplayCode.decode("203.0\u200B.113.5:\u206043821"), host, 43821);
        assertHost(NetplayCode.decode("203.0.113.5:43821\u200F"), host, 43821);
        assertHost(NetplayCode.decode("\u00A0\u200E203.0.113.5\u00A0:43821\u200B\u00A0"), host, 43821);
    }

    @Test
    public void refusesTextOfOnlyZeroWidthCharacters() {
        assertNull(NetplayCode.decode("\u200B"));
        assertNull(NetplayCode.decode("\u200B\u200E\u200F\u2060\uFEFF"));
    }

    @Test
    public void refusesOtherInvisibleCharacters() {
        // Right-to-left override U+202E, left-to-right embedding U+202A, left-to-right isolate
        // U+2066, pop directional isolate U+2069, Arabic letter mark U+061C, soft hyphen U+00AD,
        // and function application U+2061, next to the word joiner. Each is a format character
        assertNull(NetplayCode.decode("\u202E6B01-RGBA-SDWS"));
        assertNull(NetplayCode.decode("6B01-\u202ARGBA-SDWS"));
        assertNull(NetplayCode.decode("6B01-RGBA-\u2066SDWS\u2069"));
        assertNull(NetplayCode.decode("6B01\u061CRGBASDWS"));
        assertNull(NetplayCode.decode("6B01\u00ADRGBA-SDWS"));
        assertNull(NetplayCode.decode("6B01\u2061RGBA-SDWS"));
        assertNull(NetplayCode.decode("\u202E203.0.113.5:43821"));
        assertNull(NetplayCode.decode("203.0.113.5\u2066:43821\u2069"));
        assertNull(NetplayCode.decode("203.0.113.5:43821\u00AD"));
    }

    @Test
    public void readsOAsZeroAndIAndLAsOne() throws Exception {
        // The third and fourth characters of 6B01RGBASDWS are 0 and 1
        Inet4Address host = address(203, 0, 113, 5);
        assertHost(NetplayCode.decode("6BOIRGBASDWS"), host, 43821);
        assertHost(NetplayCode.decode("6BOLRGBASDWS"), host, 43821);
        assertHost(NetplayCode.decode("6boirgbasdws"), host, 43821);
        assertHost(NetplayCode.decode("6bolrgbasdws"), host, 43821);
    }

    @Test
    public void refusesWrongLengths() {
        assertNull(NetplayCode.decode(""));
        assertNull(NetplayCode.decode("   "));
        assertNull(NetplayCode.decode("-----"));
        assertNull(NetplayCode.decode("6B01RGBASDW"));
        assertNull(NetplayCode.decode("6B01-RGBA-SDW"));
        assertNull(NetplayCode.decode("6B01RGBASDWS0"));
        assertNull(NetplayCode.decode("6B01-RGBA-SDWS-0"));
    }

    @Test
    public void refusesOldNumericCodes() {
        // Codes from np.zurita.me were numbers of at most 10 digits
        assertNull(NetplayCode.decode("1234567890"));
        assertNull(NetplayCode.decode("4294967295"));
        assertNull(NetplayCode.decode("12345"));
    }

    @Test
    public void refusesCodesWithoutCheck() {
        // The 10-character form of round 1, which was never released
        assertNull(NetplayCode.decode("6B01RGBASD"));
        assertNull(NetplayCode.decode("6B01R-GBASD"));
        assertNull(NetplayCode.decode("0000002001"));
        assertNull(NetplayCode.decode("7ZZZZZZZZZ"));
    }

    @Test
    public void refusesCheckOffByOne() {
        // The check of 6B01RGBASD is 921 = 28*32 + 25 (WS); 922 = 28*32 + 26 is WT and
        // 920 = 28*32 + 24 is WR
        assertNull(NetplayCode.decode("6B01RGBASDWT"));
        assertNull(NetplayCode.decode("6B01RGBASDWR"));
    }

    @Test
    public void refusesEverySingleChangedCharacter() throws Exception {
        assertHost(NetplayCode.decode(TYPICAL_CODE), address(203, 0, 113, 5), 43821);

        int changes = 0;
        for (int index = 0; index < TYPICAL_CODE.length(); ++index) {
            for (char replacement : CROCKFORD_DIGITS.toCharArray()) {
                if (replacement != TYPICAL_CODE.charAt(index)) {
                    String changed = TYPICAL_CODE.substring(0, index) + replacement
                            + TYPICAL_CODE.substring(index + 1);
                    assertNull(changed, NetplayCode.decode(changed));
                    ++changes;
                }
            }
        }
        // 12 positions, 31 other characters each
        assertEquals(12 * 31, changes);
    }

    @Test
    public void refusesEverySwapOfNeighboringCharacters() throws Exception {
        assertHost(NetplayCode.decode(TYPICAL_CODE), address(203, 0, 113, 5), 43821);

        int swaps = 0;
        for (int index = 0; index + 1 < TYPICAL_CODE.length(); ++index) {
            char first = TYPICAL_CODE.charAt(index);
            char second = TYPICAL_CODE.charAt(index + 1);
            if (first != second) {
                String swapped = TYPICAL_CODE.substring(0, index) + second + first
                        + TYPICAL_CODE.substring(index + 2);
                assertNull(swapped, NetplayCode.decode(swapped));
                ++swaps;
            }
        }
        // No two neighboring characters of 6B01RGBASDWS are the same: 11 swaps
        assertEquals(11, swaps);
    }

    @Test
    public void refusesCharactersOutsideTheAlphabet() {
        assertNull(NetplayCode.decode("6B01RGBASDWU"));
        assertNull(NetplayCode.decode("6b01rgbasdwu"));
        assertNull(NetplayCode.decode("6B01RGBASDW!"));
        assertNull(NetplayCode.decode("6B01_RGBA_SDWS"));
        assertNull(NetplayCode.decode("6B01.RGBA.SDWS"));
        assertNull(NetplayCode.decode("6B01RGBASDW\uFF33"));
        // A control character is not ignored
        assertNull(NetplayCode.decode("6B01RGBASD\u0007WS"));
    }

    @Test
    public void refusesValuesOfTwoToThe48OrMore() {
        // 8000000000 = 8*32^9 = 2^3 * 2^45 = 2^48, which mod 1021 is 316 = 9*32 + 28
        // (encodesLargestAddressAndPort), so its check 9W is right
        assertNull(NetplayCode.decode("80000000009W"));
        // ZZZZZZZZZZ = 32^10 - 1 = 2^50 - 1; mod 1021 2^50 is 316*4 = 1264 = 1021 + 243, so
        // 2^50 - 1 is 242 = 7*32 + 18, and its check 7J is right
        assertNull(NetplayCode.decode("ZZZZZZZZZZ7J"));
    }

    @Test
    public void refusesCodesWithPortZero() {
        // 0000020000 = 2*32^4 = 2^21 = 32 * 2^16 + 0 -> 0.0.0.32 with port 0;
        // mod 1021 2^21 = (2^10)^2 * 2 is 9*2 = 18 = 0*32 + 18, so its check 0J is right
        assertNull(NetplayCode.decode("00000200000J"));
        // 6B01RGA000: groups 6 11 0 1 24 16 10 0 0 0 = 00110 01011 00000 00001 11000 10000
        // 01010 00000 00000 00000 = 0xCB0071050000 -> 203.0.113.5 with port 0; mod 1021 this
        // is 989*192 = 189888 = 185*1021 + 1003 (encodesTypicalHost), and 1003 = 31*32 + 11,
        // so its check ZB is right
        assertNull(NetplayCode.decode("6B01RGA000ZB"));
    }

    @Test
    public void readsAddressAndPort() throws Exception {
        assertHost(NetplayCode.decode("203.0.113.5:43821"), address(203, 0, 113, 5), 43821);
        assertHost(NetplayCode.decode(" 203.0.113.5:43821 "), address(203, 0, 113, 5), 43821);
        assertHost(NetplayCode.decode("198.51.100.200:1"), address(198, 51, 100, 200), 1);
        assertHost(NetplayCode.decode("255.255.255.255:65535"), address(255, 255, 255, 255), 65535);
        assertHost(NetplayCode.decode("192.168.1.20:43821"), address(192, 168, 1, 20), 43821);
    }

    @Test
    public void refusesBadPorts() {
        assertNull(NetplayCode.decode("203.0.113.5"));
        assertNull(NetplayCode.decode("203.0.113.5:"));
        assertNull(NetplayCode.decode("203.0.113.5:0"));
        assertNull(NetplayCode.decode("203.0.113.5:65536"));
        assertNull(NetplayCode.decode("203.0.113.5:100000"));
        assertNull(NetplayCode.decode("203.0.113.5:-1"));
        assertNull(NetplayCode.decode("203.0.113.5:4382a"));
        assertNull(NetplayCode.decode("203.0.113.5:43821:1"));
    }

    @Test
    public void refusesBadAddresses() {
        assertNull(NetplayCode.decode(":43821"));
        assertNull(NetplayCode.decode("203.0.113:43821"));
        assertNull(NetplayCode.decode("203.0.113.5.7:43821"));
        assertNull(NetplayCode.decode("203.0..5:43821"));
        assertNull(NetplayCode.decode("203.0.113.256:43821"));
        assertNull(NetplayCode.decode("203.0.113.05:43821"));
        assertNull(NetplayCode.decode("203.0.113.x:43821"));
        assertNull(NetplayCode.decode("host.example:43821"));
        assertNull(NetplayCode.decode("[::1]:43821"));
        assertNull(NetplayCode.decode("203.0.113.5:4382\u00071"));
    }

    @Test
    public void parsesOnlyDottedAddresses() throws Exception {
        assertEquals(address(203, 0, 113, 5), NetplayCode.parseAddress("203.0.113.5"));
        assertEquals(address(0, 0, 0, 0), NetplayCode.parseAddress("0.0.0.0"));
        assertEquals(address(255, 255, 255, 255), NetplayCode.parseAddress("255.255.255.255"));
        assertNull(NetplayCode.parseAddress(""));
        assertNull(NetplayCode.parseAddress("203.0.113"));
        assertNull(NetplayCode.parseAddress("203.0.113.5 "));
        assertNull(NetplayCode.parseAddress("203.0.113.5:43821"));
        assertNull(NetplayCode.parseAddress("-1.0.0.0"));
        assertNull(NetplayCode.parseAddress("host.example"));
    }

    @Test
    public void refusesToEncodePortsOutOfRange() throws Exception {
        Inet4Address host = address(203, 0, 113, 5);
        assertThrows(IllegalArgumentException.class, () -> NetplayCode.encode(host, 0));
        assertThrows(IllegalArgumentException.class, () -> NetplayCode.encode(host, 65536));
    }

    @Test
    public void networksTheInternetCannotReachAreNotPublic() throws Exception {
        // First and last address of each network
        assertNotPublic(0, 0, 0, 0);
        assertNotPublic(0, 255, 255, 255);
        assertNotPublic(10, 0, 0, 0);
        assertNotPublic(10, 255, 255, 255);
        assertNotPublic(100, 64, 0, 0);
        assertNotPublic(100, 127, 255, 255);
        assertNotPublic(127, 0, 0, 0);
        assertNotPublic(127, 255, 255, 255);
        assertNotPublic(169, 254, 0, 0);
        assertNotPublic(169, 254, 255, 255);
        assertNotPublic(172, 16, 0, 0);
        assertNotPublic(172, 31, 255, 255);
        assertNotPublic(192, 0, 0, 0);
        assertNotPublic(192, 0, 0, 255);
        assertNotPublic(192, 168, 0, 0);
        assertNotPublic(192, 168, 255, 255);
        assertNotPublic(198, 18, 0, 0);
        assertNotPublic(198, 19, 255, 255);
        assertNotPublic(224, 0, 0, 0);
        assertNotPublic(255, 255, 255, 255);
    }

    @Test
    public void addressesJustOutsideThoseNetworksArePublic() throws Exception {
        assertPublic(1, 0, 0, 0);
        assertPublic(9, 255, 255, 255);
        assertPublic(11, 0, 0, 0);
        assertPublic(100, 63, 255, 255);
        assertPublic(100, 128, 0, 0);
        assertPublic(126, 255, 255, 255);
        assertPublic(128, 0, 0, 0);
        assertPublic(169, 253, 255, 255);
        assertPublic(169, 255, 0, 0);
        assertPublic(172, 15, 255, 255);
        assertPublic(172, 32, 0, 0);
        assertPublic(191, 255, 255, 255);
        assertPublic(192, 0, 1, 0);
        assertPublic(192, 167, 255, 255);
        assertPublic(192, 169, 0, 0);
        assertPublic(198, 17, 255, 255);
        assertPublic(198, 20, 0, 0);
        assertPublic(223, 255, 255, 255);
        assertPublic(203, 0, 113, 5);
    }

    private static Inet4Address address(int a, int b, int c, int d) throws UnknownHostException {
        return (Inet4Address) InetAddress.getByAddress(new byte[] {(byte) a, (byte) b, (byte) c, (byte) d});
    }

    private static void assertCode(String code, Inet4Address address, int port) {
        assertEquals(code, NetplayCode.encode(address, port));
        assertHost(NetplayCode.decode(code), address, port);
    }

    private static void assertHost(InetSocketAddress host, Inet4Address address, int port) {
        assertNotNull(host);
        assertEquals(address, host.getAddress());
        assertEquals(port, host.getPort());
    }

    private static void assertPublic(int a, int b, int c, int d) throws UnknownHostException {
        assertTrue(a + "." + b + "." + c + "." + d, NetplayCode.isPublic(address(a, b, c, d)));
    }

    private static void assertNotPublic(int a, int b, int c, int d) throws UnknownHostException {
        assertFalse(a + "." + b + "." + c + "." + d, NetplayCode.isPublic(address(a, b, c, d)));
    }
}
