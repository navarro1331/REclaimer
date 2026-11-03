package com.guiltyspark343.android;

import java.net.URI;
import java.net.URISyntaxException;
import java.util.Locale;

/** Validates the operator-selected server origin without DNS lookups. */
public final class ConnectionAddress {
    private final URI origin;

    private ConnectionAddress(URI origin) { this.origin = origin; }

    public static ConnectionAddress parse(String input) {
        if (input == null || input.trim().isEmpty()) {
            throw new IllegalArgumentException("Add your server address first. You can leave setup until your Nebula connection is ready.");
        }
        String value = input.trim();
        if (value.length() > 2048 || value.indexOf('\\') >= 0) {
            throw new IllegalArgumentException("Use a complete HTTP or HTTPS server address.");
        }
        try {
            URI uri = new URI(value);
            String scheme = uri.getScheme() == null ? "" : uri.getScheme().toLowerCase(Locale.ROOT);
            String host = uri.getHost();
            if ((!scheme.equals("http") && !scheme.equals("https")) || host == null || host.isEmpty()) {
                throw new IllegalArgumentException("Start the address with http:// or https:// and include your server’s host.");
            }
            if (uri.getRawUserInfo() != null || uri.getRawQuery() != null || uri.getRawFragment() != null
                    || (uri.getRawPath() != null && !uri.getRawPath().isEmpty() && !uri.getRawPath().equals("/"))) {
                throw new IllegalArgumentException("Use only the server address and port. Enter the access code on its sign-in screen.");
            }
            int port = uri.getPort();
            if (port == 0 || port > 65535 || port < -1 || uri.getRawAuthority().endsWith(":")) {
                throw new IllegalArgumentException("The server port must be between 1 and 65535.");
            }
            if (scheme.equals("http") && !isPrivateLiteral(host)) {
                throw new IllegalArgumentException("For HTTP, use your server’s private IP address on Nebula or Wi-Fi. Use HTTPS for a public host.");
            }
            if ((scheme.equals("http") && port == 80) || (scheme.equals("https") && port == 443)) port = -1;
            return new ConnectionAddress(new URI(scheme, null, host.toLowerCase(Locale.ROOT), port, "/", null, null));
        } catch (URISyntaxException e) {
            throw new IllegalArgumentException("Check the server address. Example: http://10.42.0.2:34343");
        }
    }

    public String url() { return origin.toASCIIString(); }
    public String displayHost() { return origin.getRawAuthority(); }

    public boolean sameOrigin(String candidate) {
        try {
            URI other = new URI(candidate);
            return other.getRawUserInfo() == null && origin.getScheme().equalsIgnoreCase(other.getScheme())
                    && origin.getHost().equalsIgnoreCase(other.getHost()) && effectivePort(origin) == effectivePort(other);
        } catch (Exception e) { return false; }
    }

    private static int effectivePort(URI uri) {
        return uri.getPort() >= 0 ? uri.getPort() : ("https".equalsIgnoreCase(uri.getScheme()) ? 443 : 80);
    }

    private static boolean isPrivateLiteral(String host) {
        String ip = host.toLowerCase(Locale.ROOT);
        if (ip.startsWith("[") && ip.endsWith("]")) {
            ip = ip.substring(1, ip.length() - 1);
            if (!ip.matches("[0-9a-f:]+")) return false;
            return ip.equals("::1") || ip.matches("f[cd][0-9a-f]{2}:.*");
        }
        String[] parts = ip.split("\\.", -1);
        if (parts.length != 4) return false;
        int[] bytes = new int[4];
        for (int i = 0; i < 4; i++) {
            if (!parts[i].matches("0|[1-9][0-9]{0,2}")) return false;
            bytes[i] = Integer.parseInt(parts[i]);
            if (bytes[i] > 255) return false;
        }
        return bytes[0] == 10 || bytes[0] == 127
                || (bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31)
                || (bytes[0] == 192 && bytes[1] == 168)
                || (bytes[0] == 100 && bytes[1] >= 64 && bytes[1] <= 127);
    }
}
