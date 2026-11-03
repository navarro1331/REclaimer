package com.guiltyspark343.android;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/** Small unauthenticated readiness probe; never logs a server address, response, or code. */
public final class ServerProbe {
    private volatile HttpURLConnection connection;
    private volatile boolean cancelled;

    public void cancel() {
        cancelled = true;
        HttpURLConnection active = connection;
        if (active != null) active.disconnect();
    }

    public void check(ConnectionAddress address) throws IOException {
        HttpURLConnection request = (HttpURLConnection) new URL(address.url()).openConnection();
        connection = request;
        try {
            if (cancelled) throw new IOException("cancelled");
            request.setConnectTimeout(8000);
            request.setReadTimeout(8000);
            request.setInstanceFollowRedirects(false);
            request.setUseCaches(false);
            request.setRequestProperty("Accept", "text/html");
            request.setRequestProperty("User-Agent", "GuiltySpark343-Android/0.1.0");
            int status = request.getResponseCode();
            if (status >= 300 && status < 400) {
                throw new ServerException("The address redirects elsewhere. Enter Guilty Spark’s direct server address.");
            }
            if (status != 200) throw new ServerException("The server responded with HTTP " + status + ". Check its address and port.");
            String type = request.getContentType();
            if (type == null || !type.toLowerCase(Locale.ROOT).contains("text/html")) {
                throw new ServerException("That address responded, but it does not appear to be a Guilty Spark workspace.");
            }
            ByteArrayOutputStream output = new ByteArrayOutputStream();
            try (InputStream input = request.getInputStream()) {
                byte[] buffer = new byte[4096];
                int read;
                while ((read = input.read(buffer)) >= 0) {
                    if (cancelled || Thread.currentThread().isInterrupted()) throw new IOException("cancelled");
                    if (output.size() + read > 65536) throw new ServerException("The response does not match Guilty Spark’s connection page.");
                    output.write(buffer, 0, read);
                }
            }
            String page = output.toString(StandardCharsets.UTF_8.name());
            if (!page.contains("Guilty Spark 343")) {
                throw new ServerException("That server is reachable, but its page does not identify Guilty Spark 343.");
            }
        } finally { request.disconnect(); connection = null; }
    }

    public static final class ServerException extends IOException {
        public ServerException(String message) { super(message); }
    }
}
