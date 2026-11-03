package com.guiltyspark343.android;

import com.sun.net.httpserver.HttpServer;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

public final class ConnectionTests {
    private static int checks;
    private static void check(boolean result, String label) {
        checks++;
        if (!result) throw new AssertionError(label);
    }
    public static void main(String[] args) throws Exception {
        String[] valid = {"http://10.77.0.4:34343", "http://192.168.1.2", "http://172.16.0.1", "http://172.31.255.254", "http://100.64.1.1", "http://100.127.255.254", "http://127.0.0.1:8080", "https://spark.example.com", "http://[fd12:3456::1]:34343", "http://[::1]:34343"};
        for (String url : valid) check(ConnectionAddress.parse(url).url().endsWith("/"), "Accepted origin: " + url);
        String[] invalid = {"", "10.77.0.4:34343", "file:///etc/passwd", "javascript:alert(1)", "http://8.8.8.8", "http://example.com", "http://172.15.0.1", "http://172.32.0.1", "http://100.128.0.1", "http://192.168.1.999", "http://127.1", "http://0177.0.0.1", "http://10.0.0.1:0", "http://10.0.0.1:65536", "http://10.0.0.1:", "http://user:secret@10.0.0.1", "http://10.0.0.1/?token=secret", "http://10.0.0.1/#token=secret", "http://10.0.0.1/auth", "http://10.0.0.1\\@example.com", "http://[2001:4860:4860::8888]"};
        for (String url : invalid) {
            boolean rejected = false;
            try { ConnectionAddress.parse(url); } catch (IllegalArgumentException expected) { rejected = true; }
            check(rejected, "Rejected invalid/public cleartext origin: " + url);
        }
        ConnectionAddress origin = ConnectionAddress.parse(" HTTPS://Spark.Example.com:443/ ");
        check(origin.url().equals("https://spark.example.com/"), "Normalization");
        check(origin.sameOrigin("https://spark.example.com:443/m/model/chat.html"), "Same origin model workspace");
        check(!origin.sameOrigin("http://spark.example.com/"), "No scheme downgrade");
        check(!origin.sameOrigin("https://spark.example.com.attacker.test/"), "No host suffix spoof");
        check(!origin.sameOrigin("https://spark.example.com:8443/"), "No port switch");
        check(!origin.sameOrigin("https://user@spark.example.com/"), "No credentials in navigation");
        AtomicInteger status = new AtomicInteger(200);
        AtomicReference<String> type = new AtomicReference<>("text/html");
        AtomicReference<String> page = new AtomicReference<>("<title>Guilty Spark 343</title>");
        HttpServer fixture = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
        fixture.createContext("/", exchange -> {
            byte[] data = page.get().getBytes(StandardCharsets.UTF_8);
            exchange.getResponseHeaders().set("Content-Type", type.get());
            exchange.getResponseHeaders().set("Location", "http://127.0.0.1:1/");
            exchange.sendResponseHeaders(status.get(), data.length);
            exchange.getResponseBody().write(data);
            exchange.close();
        });
        fixture.start();
        try {
            ConnectionAddress local = ConnectionAddress.parse("http://127.0.0.1:" + fixture.getAddress().getPort());
            new ServerProbe().check(local); check(true, "Correct connection page");
            status.set(302); expectProbeError(local, "redirects");
            status.set(503); expectProbeError(local, "HTTP 503");
            status.set(200); type.set("application/json"); expectProbeError(local, "does not appear");
            type.set("text/html"); page.set("<title>Different app</title>"); expectProbeError(local, "does not identify");
            page.set("Guilty Spark 343" + "x".repeat(65536)); expectProbeError(local, "does not match");
            ServerProbe cancelled = new ServerProbe(); cancelled.cancel();
            boolean rejected = false;
            try { cancelled.check(local); } catch (java.io.IOException expected) { rejected = true; }
            check(rejected, "Cancelled request stays cancelled");
        } finally { fixture.stop(0); }
        System.out.println("Passed " + checks + " connection and HTTP checks.");
    }
    private static void expectProbeError(ConnectionAddress address, String expected) throws Exception {
        try { new ServerProbe().check(address); throw new AssertionError("Expected probe failure: " + expected); }
        catch (ServerProbe.ServerException error) { check(error.getMessage().contains(expected), expected); }
    }
}
