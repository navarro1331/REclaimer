package com.guiltyspark343.android;

import android.app.Instrumentation;
import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.webkit.CookieManager;
import android.webkit.WebView;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.TextView;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BooleanSupplier;

/** Self-contained Android tests using only platform APIs and a synthetic loopback server. */
public final class SmokeInstrumentation extends Instrumentation {
    private MainActivity activity;
    private String fixture;
    private int checks;

    @Override public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        fixture = arguments.getString("fixture", "http://127.0.0.1:34344/");
        start();
    }
    @Override public void onStart() {
        Bundle result = new Bundle();
        try {
            getTargetContext().getSharedPreferences("connection", 0).edit().clear().commit();
            launch();
            check(text(R.id.server_address).isEmpty(), "Fresh install starts without an invented server");
            check(visible(R.id.setup_scroll), "Native setup available without any server");
            enter("http://example.com"); click(R.id.connect_button);
            waitFor(() -> text(R.id.connection_status).contains("private IP"));
            check(visible(R.id.setup_scroll), "Public HTTP rejected before network access");
            enter("http://10.42.0.2:34343"); click(R.id.save_button);
            waitFor(() -> text(R.id.connection_status).contains("Address saved"));
            check(getTargetContext().getSharedPreferences("connection", 0).getString("server", "").equals("http://10.42.0.2:34343/"), "Save for later persists normalized address");
            runOnMainSync(() -> activity.finish());
            launch();
            check(text(R.id.server_address).equals("http://10.42.0.2:34343/"), "Saved address survives activity restart");
            enter("http://127.0.0.1:1"); click(R.id.connect_button);
            waitFor(() -> text(R.id.connection_status).contains("out of reach"));
            check(visible(R.id.setup_scroll), "Unreachable server gives a recoverable native error");
            enter(fixture); click(R.id.connect_button);
            waitFor(() -> visible(R.id.workspace_container));
            waitJs("Boolean(document.querySelector('#code'))", "true");
            check(true, "Server login page loaded inside Android workspace");
            evaluate("document.querySelector('#code').value='wrong-code';document.querySelector('#login').click();true");
            waitJs("document.querySelector('#error')?.textContent || ''", "\"Access code was not accepted.\"");
            check(true, "Wrong access code stays rejected by the server");
            evaluate("document.querySelector('#code').value='fixture-only';document.querySelector('#login').click();true");
            waitJs("Boolean(document.querySelector('#model'))", "true");
            check(true, "Access-code login receives the server session");
            evaluate("document.querySelector('#model').click();true");
            waitJs("Boolean(document.querySelector('#prompt'))", "true");
            evaluate("document.querySelector('#prompt').value='Synthetic connection check';document.querySelector('#send').click();true");
            waitJs("document.querySelector('#reply')?.textContent || ''", "\"Synthetic reply received on Android.\"");
            check(true, "Authenticated synthetic chat request and reply work in Android");
            evaluate("window.externalProbe='pending';fetch('http://127.0.0.1:1/').then(()=>window.externalProbe='allowed').catch(()=>window.externalProbe='blocked');true");
            waitJs("window.externalProbe", "\"blocked\"");
            check(true, "Out-of-origin network resources are blocked");
            final String[] url = new String[1];
            runOnMainSync(() -> url[0] = web().getUrl());
            check(url[0].contains("/m/fixture/chat.html"), "Model workspace stays on selected origin");
            runOnMainSync(() -> web().loadUrl(fixture + "unavailable"));
            waitFor(() -> visible(R.id.setup_scroll) && text(R.id.connection_status).contains("could not load"));
            check(true, "Server failure returns to recoverable connection settings");
            click(R.id.connect_button);
            waitFor(() -> visible(R.id.workspace_container));
            waitJs("Boolean(document.querySelector('#model'))", "true");
            check(true, "Reconnect restores authenticated model selection");
            // Destroy this synthetic session so the final install opens an empty connection screen.
            CountDownLatch cookies = new CountDownLatch(1);
            runOnMainSync(() -> CookieManager.getInstance().removeAllCookies(done -> cookies.countDown()));
            cookies.await(5, TimeUnit.SECONDS);
            runOnMainSync(() -> activity.finish());
            getTargetContext().getSharedPreferences("connection", 0).edit().clear().commit();
            result.putString("stream", "\nPassed " + checks + " Android integration checks with a synthetic server.\n");
            finish(-1, result);
        } catch (Throwable failure) {
            result.putString("stream", "\nFAILED after " + checks + " checks: " + failure + "\n");
            finish(1, result);
        }
    }
    private void launch() {
        activity = (MainActivity) startActivitySync(new Intent(getTargetContext(), MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        waitForIdleSync();
    }
    private void enter(String value) { runOnMainSync(() -> ((EditText) activity.findViewById(R.id.server_address)).setText(value)); }
    private void click(int id) { runOnMainSync(() -> activity.findViewById(id).performClick()); }
    private String text(int id) {
        AtomicReference<String> result = new AtomicReference<>("");
        runOnMainSync(() -> result.set(((TextView) activity.findViewById(id)).getText().toString()));
        return result.get();
    }
    private boolean visible(int id) {
        boolean[] result = new boolean[1];
        runOnMainSync(() -> result[0] = activity.findViewById(id).getVisibility() == View.VISIBLE);
        return result[0];
    }
    private WebView web() { return (WebView) ((FrameLayout) activity.findViewById(R.id.web_container)).getChildAt(0); }
    private String evaluate(String script) throws Exception {
        CountDownLatch latch = new CountDownLatch(1);
        AtomicReference<String> result = new AtomicReference<>("");
        runOnMainSync(() -> web().evaluateJavascript(script, value -> { result.set(value); latch.countDown(); }));
        if (!latch.await(5, TimeUnit.SECONDS)) throw new AssertionError("JavaScript callback timeout");
        return result.get();
    }
    private void waitJs(String script, String expected) throws Exception {
        long until = System.currentTimeMillis() + 20000;
        do { if (expected.equals(evaluate(script))) return; Thread.sleep(150); } while (System.currentTimeMillis() < until);
        throw new AssertionError("Workspace state did not arrive: " + script);
    }
    private void waitFor(BooleanSupplier condition) throws Exception {
        long until = System.currentTimeMillis() + 20000;
        do { if (condition.getAsBoolean()) return; Thread.sleep(100); } while (System.currentTimeMillis() < until);
        throw new AssertionError("UI state did not arrive");
    }
    private void check(boolean condition, String name) {
        if (!condition) throw new AssertionError(name);
        checks++;
        Bundle progress = new Bundle(); progress.putString("stream", "PASS: " + name + "\n"); sendStatus(0, progress);
    }
}
