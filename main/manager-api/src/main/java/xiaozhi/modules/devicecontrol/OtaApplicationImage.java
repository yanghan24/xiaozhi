package xiaozhi.modules.devicecontrol;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.HexFormat;

/** P4 application image, not a merged flash image or bootloader. */
record OtaApplicationImage(String version, String elfSha256, String sha256) {
    static OtaApplicationImage inspect(byte[] bytes, String expectedSha256) {
        if (bytes.length < 288 || bytes.length > GithubReleaseSource.MAX_IMAGE)
            throw new ControlFailure(400, "INVALID_APPLICATION_IMAGE");
        var header = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        if ((bytes[0] & 255) != 0xe9 || (header.getShort(12) & 65535) != 0x12
                || header.getInt(32) != 0xabcd5432)
            throw new ControlFailure(400, "INVALID_P4_APPLICATION_IMAGE");
        int segments = bytes[1] & 255;
        if (segments < 1 || segments > 16) throw new ControlFailure(400, "INVALID_APPLICATION_IMAGE");
        long offset = 24;
        int checksum = 0xef;
        for (int segment = 0; segment < segments; segment++) {
            if (offset + 8 > bytes.length) throw new ControlFailure(400, "INVALID_APPLICATION_IMAGE");
            long length = Integer.toUnsignedLong(header.getInt((int) offset + 4));
            if (segment == 0 && length < 256) throw new ControlFailure(400, "INVALID_APPLICATION_IMAGE");
            long end = offset + 8 + length;
            if (end > bytes.length) throw new ControlFailure(400, "INVALID_APPLICATION_IMAGE");
            for (int position = (int) offset + 8; position < end; position++) checksum ^= bytes[position] & 255;
            offset = end;
        }
        int checksumEnd = ((int) offset + 16) & ~15;
        int hashSize = bytes[23] == 1 ? 32 : 0;
        if ((bytes[23] != 0 && bytes[23] != 1) || checksumEnd + hashSize != bytes.length
                || (bytes[checksumEnd - 1] & 255) != checksum)
            throw new ControlFailure(400, "INVALID_APPLICATION_IMAGE");
        if (hashSize != 0 && !MessageDigest.isEqual(digest(Arrays.copyOf(bytes, checksumEnd)),
                Arrays.copyOfRange(bytes, checksumEnd, bytes.length)))
            throw new ControlFailure(400, "INVALID_APPLICATION_IMAGE");
        String version = cString(bytes, 48, 32);
        String elf = HexFormat.of().formatHex(Arrays.copyOfRange(bytes, 176, 208));
        String sha = HexFormat.of().formatHex(digest(bytes));
        if (!sha.equals(expectedSha256)) throw new ControlFailure(400, "CHECKSUM_MISMATCH");
        if (version.isEmpty() || !version.matches("[A-Za-z0-9_.+\\-]{1,31}") || elf.equals("0".repeat(64)))
            throw new ControlFailure(400, "INVALID_APPLICATION_IMAGE");
        return new OtaApplicationImage(version, elf, sha);
    }

    private static byte[] digest(byte[] bytes) {
        try { return MessageDigest.getInstance("SHA-256").digest(bytes); }
        catch (java.security.NoSuchAlgorithmException impossible) { throw new IllegalStateException(impossible); }
    }

    private static String cString(byte[] bytes, int offset, int capacity) {
        int end = offset;
        while (end < offset + capacity && bytes[end] != 0) end++;
        return new String(bytes, offset, end - offset, StandardCharsets.US_ASCII);
    }
}
