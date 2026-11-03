package com.guiltyspark343.android;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Insets;
import android.net.Uri;
import android.net.http.SslError;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.webkit.CookieManager;
import android.webkit.GeolocationPermissions;
import android.webkit.JsResult;
import android.webkit.PermissionRequest;
import android.webkit.RenderProcessGoneDetail;
import android.webkit.SslErrorHandler;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.PopupMenu;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import javax.net.ssl.SSLException;

public final class MainActivity extends Activity {
    private final ExecutorService network = Executors.newSingleThreadExecutor();
    private SharedPreferences preferences;
    private EditText addressInput;
    private TextView status;
    private Button connectButton;
    private View setup, workspace, saveButton, connectionProgress;
    private ProgressBar webProgress;
    private FrameLayout webContainer;
    private WebView web;
    private volatile ConnectionAddress activeAddress;
    private ServerProbe probe;
    private int requestGeneration;
    private boolean connecting, closed;
    private ValueCallback<Uri[]> fileCallback;
    private static final int PICK_FILE = 343;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        preferences = getSharedPreferences("connection", MODE_PRIVATE);
        setContentView(R.layout.activity_main);
        View root = findViewById(R.id.root);
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                Insets edge = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout() | WindowInsets.Type.ime());
                view.setPadding(edge.left, edge.top, edge.right, edge.bottom);
            } else {
                view.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(), insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            }
            return insets;
        });
        setup = findViewById(R.id.setup_scroll);
        workspace = findViewById(R.id.workspace_container);
        addressInput = findViewById(R.id.server_address);
        status = findViewById(R.id.connection_status);
        connectButton = findViewById(R.id.connect_button);
        saveButton = findViewById(R.id.save_button);
        connectionProgress = findViewById(R.id.connect_progress);
        webProgress = findViewById(R.id.web_progress);
        webContainer = findViewById(R.id.web_container);
        addressInput.setText(state == null ? preferences.getString("server", "") : state.getString("draft", ""));
        connectButton.setOnClickListener(view -> { if (connecting) cancelConnection(); else connect(); });
        saveButton.setOnClickListener(view -> saveForLater());
        addressInput.setOnEditorActionListener((view, action, event) -> {
            if (action == EditorInfo.IME_ACTION_GO) { connect(); return true; }
            return false;
        });
        findViewById(R.id.workspace_menu).setOnClickListener(this::showWorkspaceMenu);
        if (Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT,
                    () -> { if (!handleBack()) finish(); });
        }
    }

    private ConnectionAddress readAddress() {
        try { addressInput.setError(null); return ConnectionAddress.parse(addressInput.getText().toString()); }
        catch (IllegalArgumentException error) {
            addressInput.setError(error.getMessage());
            message(error.getMessage(), true);
            return null;
        }
    }

    private void saveForLater() {
        ConnectionAddress address = readAddress();
        if (address == null) return;
        preferences.edit().putString("server", address.url()).apply();
        addressInput.setText(address.url());
        hideKeyboard();
        message(getString(R.string.saved), false);
    }

    private void connect() {
        if (connecting) return;
        ConnectionAddress address = readAddress();
        if (address == null) return;
        hideKeyboard();
        setConnecting(true);
        message(getString(R.string.checking), false);
        int generation = ++requestGeneration;
        ServerProbe currentProbe = new ServerProbe();
        probe = currentProbe;
        network.execute(() -> {
            String error = null;
            try { currentProbe.check(address); }
            catch (SSLException ignored) { error = getString(R.string.ssl_error); }
            catch (ServerProbe.ServerException failure) { error = failure.getMessage(); }
            catch (IOException | RuntimeException ignored) { error = getString(R.string.offline_error); }
            final String result = error;
            runOnUiThread(() -> {
                if (closed || generation != requestGeneration) return;
                probe = null;
                setConnecting(false);
                if (result != null) { message(result, true); return; }
                preferences.edit().putString("server", address.url()).apply();
                addressInput.setText(address.url());
                openWorkspace(address);
            });
        });
    }

    private void cancelConnection() {
        requestGeneration++;
        if (probe != null) probe.cancel();
        probe = null;
        setConnecting(false);
        status.setVisibility(View.GONE);
    }

    private void setConnecting(boolean value) {
        connecting = value;
        connectButton.setText(value ? R.string.cancel : R.string.connect);
        addressInput.setEnabled(!value);
        saveButton.setEnabled(!value);
        connectionProgress.setVisibility(value ? View.VISIBLE : View.GONE);
    }

    private void message(String text, boolean error) {
        status.setText(text);
        status.setTextColor(getColor(error ? R.color.error : R.color.accent));
        status.setVisibility(View.VISIBLE);
    }

    @SuppressLint("SetJavaScriptEnabled")
    private void openWorkspace(ConnectionAddress address) {
        destroyWeb();
        activeAddress = address;
        setup.setVisibility(View.GONE);
        workspace.setVisibility(View.VISIBLE);
        ((TextView) findViewById(R.id.workspace_host)).setText(address.displayHost());
        web = new WebView(this);
        web.setId(View.generateViewId());
        web.setContentDescription("Guilty Spark workspace");
        web.setBackgroundColor(getColor(R.color.background));
        WebSettings settings = web.getSettings();
        settings.setJavaScriptEnabled(true); // Required by the selected Guilty Spark web workspace.
        settings.setDomStorageEnabled(true);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setGeolocationEnabled(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        settings.setMediaPlaybackRequiresUserGesture(true);
        settings.setSupportMultipleWindows(false);
        settings.setJavaScriptCanOpenWindowsAutomatically(false);
        settings.setSaveFormData(false);
        settings.setSafeBrowsingEnabled(true);
        settings.setUserAgentString(settings.getUserAgentString() + " GuiltySpark343-Android/0.1.0");
        CookieManager.getInstance().setAcceptCookie(true);
        CookieManager.getInstance().setAcceptThirdPartyCookies(web, false);
        web.setWebViewClient(new WorkspaceClient());
        web.setWebChromeClient(new WorkspaceChrome());
        web.setDownloadListener((url, agent, disposition, type, length) -> Toast.makeText(this, R.string.download_help, Toast.LENGTH_LONG).show());
        webContainer.addView(web, new FrameLayout.LayoutParams(-1, -1));
        web.loadUrl(address.url());
    }

    private final class WorkspaceClient extends WebViewClient {
        @Override public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
            String target = request.getUrl().toString();
            if (activeAddress != null && activeAddress.sameOrigin(target)) return false;
            if (request.isForMainFrame() && request.hasGesture() && isWebLink(target)) {
                new AlertDialog.Builder(MainActivity.this).setTitle(R.string.open_external_question).setMessage(target)
                        .setPositiveButton(R.string.open_browser, (dialog, which) -> openBrowser(target))
                        .setNegativeButton(R.string.cancel, null).show();
            }
            return true;
        }
        @Override public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
            String target = request.getUrl().toString();
            ConnectionAddress address = activeAddress;
            if (address != null && address.sameOrigin(target)) return null;
            String scheme = request.getUrl().getScheme();
            if ("data".equals(scheme) || "blob".equals(scheme) || "about".equals(scheme)) return null;
            return new WebResourceResponse("text/plain", "UTF-8", 403, "Outside workspace", null, new ByteArrayInputStream(new byte[0]));
        }
        @Override public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
            if (request.isForMainFrame() && view == web) showSetup(getString(R.string.offline_error));
        }
        @Override public void onReceivedHttpError(WebView view, WebResourceRequest request, WebResourceResponse response) {
            if (request.isForMainFrame() && response.getStatusCode() >= 400 && view == web) showSetup(getString(R.string.web_error));
        }
        @Override public void onReceivedSslError(WebView view, SslErrorHandler handler, SslError error) {
            handler.cancel();
            if (view == web) showSetup(getString(R.string.ssl_error));
        }
        @Override public boolean onRenderProcessGone(WebView view, RenderProcessGoneDetail detail) {
            if (view == web) showSetup(getString(R.string.renderer_error));
            return true;
        }
    }

    private final class WorkspaceChrome extends WebChromeClient {
        @Override public void onProgressChanged(WebView view, int progress) {
            if (view != web) return;
            webProgress.setProgress(progress);
            webProgress.setVisibility(progress == 100 ? View.GONE : View.VISIBLE);
        }
        @Override public void onPermissionRequest(PermissionRequest request) { request.deny(); }
        @Override public void onGeolocationPermissionsShowPrompt(String origin, GeolocationPermissions.Callback callback) { callback.invoke(origin, false, false); }
        @Override public boolean onJsAlert(WebView view, String url, String text, JsResult result) {
            new AlertDialog.Builder(MainActivity.this).setMessage(text).setPositiveButton(R.string.ok, (dialog, which) -> result.confirm())
                    .setOnCancelListener(dialog -> result.cancel()).show();
            return true;
        }
        @Override public boolean onJsConfirm(WebView view, String url, String text, JsResult result) {
            new AlertDialog.Builder(MainActivity.this).setMessage(text).setPositiveButton(R.string.ok, (dialog, which) -> result.confirm())
                    .setNegativeButton(R.string.cancel, (dialog, which) -> result.cancel()).setOnCancelListener(dialog -> result.cancel()).show();
            return true;
        }
        @Override public boolean onShowFileChooser(WebView view, ValueCallback<Uri[]> callback, FileChooserParams params) {
            if (fileCallback != null) fileCallback.onReceiveValue(null);
            fileCallback = callback;
            Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
            picker.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, params.getMode() == FileChooserParams.MODE_OPEN_MULTIPLE);
            try { startActivityForResult(Intent.createChooser(picker, getString(R.string.choose_file)), PICK_FILE); }
            catch (ActivityNotFoundException error) { fileCallback.onReceiveValue(null); fileCallback = null; }
            return true;
        }
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_FILE || fileCallback == null) return;
        Uri[] files = null;
        if (result == RESULT_OK && data != null) {
            if (data.getClipData() != null) {
                int size = Math.min(data.getClipData().getItemCount(), 20);
                files = new Uri[size];
                for (int i = 0; i < size; i++) files[i] = data.getClipData().getItemAt(i).getUri();
            } else if (data.getData() != null) files = new Uri[] {data.getData()};
        }
        fileCallback.onReceiveValue(files);
        fileCallback = null;
    }

    private void showWorkspaceMenu(View anchor) {
        PopupMenu menu = new PopupMenu(this, anchor);
        menu.getMenu().add(0, 1, 0, R.string.reload);
        menu.getMenu().add(0, 2, 1, R.string.connection_settings);
        menu.getMenu().add(0, 3, 2, R.string.open_browser);
        menu.getMenu().add(0, 4, 3, R.string.sign_out);
        menu.setOnMenuItemClickListener(item -> {
            switch (item.getItemId()) {
                case 1: if (web != null) web.reload(); break;
                case 2: confirmLeave(); break;
                case 3: if (activeAddress != null) openBrowser(activeAddress.url()); break;
                case 4:
                    new AlertDialog.Builder(this).setMessage(R.string.sign_out_question)
                            .setPositiveButton(R.string.sign_out, (dialog, which) -> {
                                destroyWeb();
                                CookieManager.getInstance().removeAllCookies(done -> CookieManager.getInstance().flush());
                                showSetup(null);
                            }).setNegativeButton(R.string.cancel, null).show();
                    break;
                default: return false;
            }
            return true;
        });
        menu.show();
    }

    private void confirmLeave() {
        new AlertDialog.Builder(this).setMessage(R.string.leave_workspace)
                .setPositiveButton(R.string.connection_settings, (dialog, which) -> showSetup(null))
                .setNegativeButton(R.string.cancel, null).show();
    }

    private static boolean isWebLink(String url) { return url.startsWith("https://") || url.startsWith("http://"); }
    private void openBrowser(String target) {
        try { startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(target)).addCategory(Intent.CATEGORY_BROWSABLE)); }
        catch (ActivityNotFoundException error) { Toast.makeText(this, R.string.no_browser, Toast.LENGTH_LONG).show(); }
    }
    private void showSetup(String error) {
        destroyWeb();
        workspace.setVisibility(View.GONE);
        setup.setVisibility(View.VISIBLE);
        if (error == null) status.setVisibility(View.GONE); else message(error, true);
    }
    private void destroyWeb() {
        if (fileCallback != null) { fileCallback.onReceiveValue(null); fileCallback = null; }
        if (web != null) {
            WebView old = web;
            web = null;
            webContainer.removeView(old);
            old.stopLoading();
            old.destroy();
        }
        activeAddress = null;
    }
    private void hideKeyboard() {
        ((InputMethodManager) getSystemService(INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(addressInput.getWindowToken(), 0);
        addressInput.clearFocus();
    }
    // Android 13+ uses the platform OnBackInvokedCallback registered in onCreate.
    // This override is retained only for Android 8-12, without an AndroidX dependency.
    @SuppressLint("GestureBackNavigation")
    @Override public void onBackPressed() {
        if (handleBack()) return;
        super.onBackPressed();
    }
    private boolean handleBack() {
        if (connecting) { cancelConnection(); return true; }
        if (web != null) { if (web.canGoBack()) web.goBack(); else confirmLeave(); return true; }
        return false;
    }
    @Override protected void onSaveInstanceState(Bundle state) {
        state.putString("draft", addressInput.getText().toString());
        super.onSaveInstanceState(state);
    }
    @Override protected void onPause() { if (web != null) web.onPause(); super.onPause(); }
    @Override protected void onResume() { super.onResume(); if (web != null) web.onResume(); }
    @Override protected void onDestroy() {
        closed = true;
        requestGeneration++;
        if (probe != null) probe.cancel();
        network.shutdownNow();
        destroyWeb();
        super.onDestroy();
    }
}
