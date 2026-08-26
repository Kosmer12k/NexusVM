package com.nexusvm.engine;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.Button;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import java.io.File;

/**
 * NexusVM Main Activity
 * 
 * Simple UI with:
 * - Load ROM button (file picker)
 * - Start / Stop / Pause buttons
 * - SurfaceView for guest display
 * - Status indicator
 */
public class MainActivity extends AppCompatActivity implements SurfaceHolder.Callback {

    private static final String TAG = "NexusVM";
    private static final int REQUEST_ROM_FILE = 1001;
    private static final int REQUEST_PERMISSIONS = 1002;
    private static final int REQUEST_MANAGE_STORAGE = 1003;

    // UI elements
    private SurfaceView surfaceView;
    private Button btnLoadRom;
    private Button btnStart;
    private Button btnStop;
    private Button btnPause;
    private TextView tvStatus;
    private TextView tvRomPath;
    private ProgressBar progressBar;

    // Engine
    private NexusEngine engine;
    private String currentRomPath = null;
    private boolean surfaceReady = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // Initialize engine
        engine = NexusEngine.getInstance();

        // Bind UI
        surfaceView = findViewById(R.id.surfaceView);
        btnLoadRom  = findViewById(R.id.btnLoadRom);
        btnStart    = findViewById(R.id.btnStart);
        btnStop     = findViewById(R.id.btnStop);
        btnPause    = findViewById(R.id.btnPause);
        tvStatus    = findViewById(R.id.tvStatus);
        tvRomPath   = findViewById(R.id.tvRomPath);
        progressBar = findViewById(R.id.progressBar);

        // Setup surface
        surfaceView.getHolder().addCallback(this);

        // Button handlers
        btnLoadRom.setOnClickListener(v -> openFilePicker());
        btnStart.setOnClickListener(v -> startEngine());
        btnStop.setOnClickListener(v -> stopEngine());
        btnPause.setOnClickListener(v -> togglePause());

        // Request permissions
        checkPermissions();

        updateUI();
    }

    // ========================================================================
    // Permissions
    // ========================================================================

    private void checkPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                intent.setData(Uri.parse("package:" + getPackageName()));
                startActivityForResult(intent, REQUEST_MANAGE_STORAGE);
            }
        } else {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE)
                    != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(this,
                        new String[]{
                            Manifest.permission.READ_EXTERNAL_STORAGE,
                            Manifest.permission.WRITE_EXTERNAL_STORAGE
                        }, REQUEST_PERMISSIONS);
            }
        }
    }

    // ========================================================================
    // File Picker
    // ========================================================================

    private void openFilePicker() {
        Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
        intent.setType("*/*");
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        startActivityForResult(Intent.createChooser(intent, "Select GSI ROM (system.img)"), REQUEST_ROM_FILE);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode == REQUEST_ROM_FILE && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            if (uri != null) {
                // Copy URI to a local file path the engine can access
                currentRomPath = copyUriToLocal(uri);
                if (currentRomPath != null) {
                    tvRomPath.setText("ROM: " + new File(currentRomPath).getName());
                    initializeEngine();
                }
            }
        }
    }

    private String copyUriToLocal(Uri uri) {
        // For content:// URIs, we need to copy to our app's data directory
        // For file:// URIs, we can use the path directly
        String path = uri.getPath();
        if (path != null && new File(path).exists()) {
            return path;
        }

        // Copy from content URI
        try {
            File destFile = new File(getFilesDir(), "system.img");
            java.io.InputStream in = getContentResolver().openInputStream(uri);
            java.io.FileOutputStream out = new java.io.FileOutputStream(destFile);
            byte[] buf = new byte[65536];
            int len;
            long total = 0;
            while ((len = in.read(buf)) > 0) {
                out.write(buf, 0, len);
                total += len;
            }
            in.close();
            out.close();
            Log.i(TAG, "Copied ROM to: " + destFile.getAbsolutePath() + " (" + total + " bytes)");
            return destFile.getAbsolutePath();
        } catch (Exception e) {
            Log.e(TAG, "Failed to copy ROM: " + e.getMessage());
            Toast.makeText(this, "Failed to copy ROM file", Toast.LENGTH_SHORT).show();
            return null;
        }
    }

    // ========================================================================
    // Engine Control
    // ========================================================================

    private void initializeEngine() {
        tvStatus.setText("Status: Initializing...");
        progressBar.setVisibility(View.VISIBLE);

        new Thread(() -> {
            DisplayMetrics dm = getResources().getDisplayMetrics();
            String dataPath = getFilesDir().getAbsolutePath();
            String rootfsPath = dataPath + "/rootfs";

            int ret = engine.initialize(
                    currentRomPath,
                    dataPath,
                    rootfsPath,
                    "Pixel 7 Pro",  // Default device model
                    "Google",       // Default brand
                    dm.widthPixels,
                    dm.heightPixels,
                    dm.densityDpi,
                    true,   // GPU passthrough
                    true    // Debug logging
            );

            if (ret == 0) {
                // Load ROM
                ret = engine.loadRom(currentRomPath);
            }

            final int result = ret;
            runOnUiThread(() -> {
                progressBar.setVisibility(View.GONE);
                if (result == 0) {
                    Toast.makeText(this, "ROM loaded successfully!", Toast.LENGTH_SHORT).show();
                } else {
                    Toast.makeText(this, "Failed to load ROM: error " + result, Toast.LENGTH_LONG).show();
                }
                updateUI();
            });
        }).start();
    }

    private void startEngine() {
        tvStatus.setText("Status: Starting guest OS...");

        // Pass surface to engine
        if (surfaceReady) {
            engine.setSurface(surfaceView.getHolder().getSurface());
        }

        new Thread(() -> {
            int ret = engine.start();
            runOnUiThread(() -> {
                if (ret != 0) {
                    Toast.makeText(this, "Start failed: error " + ret, Toast.LENGTH_LONG).show();
                }
                updateUI();
            });
        }).start();
    }

    private void stopEngine() {
        new Thread(() -> {
            engine.stop();
            runOnUiThread(this::updateUI);
        }).start();
    }

    private void togglePause() {
        int state = engine.getState();
        if (state == NexusEngine.STATE_RUNNING) {
            engine.pause();
        } else if (state == NexusEngine.STATE_PAUSED) {
            engine.resume();
        }
        updateUI();
    }

    // ========================================================================
    // UI Updates
    // ========================================================================

    private void updateUI() {
        int state = engine.getState();
        tvStatus.setText("Status: " + engine.getStateString());

        btnLoadRom.setEnabled(state == NexusEngine.STATE_UNINITIALIZED || 
                              state == NexusEngine.STATE_INITIALIZED);
        btnStart.setEnabled(state == NexusEngine.STATE_ROM_LOADED);
        btnStop.setEnabled(state == NexusEngine.STATE_RUNNING || 
                           state == NexusEngine.STATE_PAUSED);
        btnPause.setEnabled(state == NexusEngine.STATE_RUNNING || 
                            state == NexusEngine.STATE_PAUSED);

        if (state == NexusEngine.STATE_PAUSED) {
            btnPause.setText("Resume");
        } else {
            btnPause.setText("Pause");
        }
    }

    // ========================================================================
    // SurfaceHolder.Callback — Rendering Surface for Guest Display
    // ========================================================================

    @Override
    public void surfaceCreated(@NonNull SurfaceHolder holder) {
        Log.i(TAG, "Surface created");
        surfaceReady = true;
        if (engine.getState() == NexusEngine.STATE_RUNNING) {
            engine.setSurface(holder.getSurface());
        }
    }

    @Override
    public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int width, int height) {
        Log.i(TAG, "Surface changed: " + width + "x" + height);
    }

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
        Log.i(TAG, "Surface destroyed");
        surfaceReady = false;
        engine.setSurface(null);
    }

    // ========================================================================
    // Touch Input Forwarding
    // ========================================================================

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (engine.getState() != NexusEngine.STATE_RUNNING) {
            return super.onTouchEvent(event);
        }

        int action = event.getActionMasked();
        int pointerIndex = event.getActionIndex();
        int pointerId = event.getPointerId(pointerIndex);

        engine.sendTouchEvent(action, event.getX(pointerIndex), 
                              event.getY(pointerIndex), pointerId);
        return true;
    }

    // ========================================================================
    // Lifecycle
    // ========================================================================

    @Override
    protected void onPause() {
        super.onPause();
        if (engine.getState() == NexusEngine.STATE_RUNNING) {
            engine.pause();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (engine.getState() == NexusEngine.STATE_PAUSED) {
            engine.resume();
        }
        updateUI();
    }

    @Override
    protected void onDestroy() {
        engine.stop();
        engine.destroy();
        super.onDestroy();
    }
}
