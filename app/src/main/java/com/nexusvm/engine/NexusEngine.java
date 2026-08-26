package com.nexusvm.engine;

import android.view.Surface;

/**
 * NexusEngine — Java-side bridge to the C++ container engine.
 * 
 * This class provides the Java API for controlling the engine.
 * All heavy lifting is done in native C++ (libNexusVM.so).
 */
public class NexusEngine {

    // Engine states (must match C++ EngineState enum)
    public static final int STATE_UNINITIALIZED = 0;
    public static final int STATE_INITIALIZED   = 1;
    public static final int STATE_ROM_LOADED    = 2;
    public static final int STATE_STARTING      = 3;
    public static final int STATE_RUNNING       = 4;
    public static final int STATE_PAUSED        = 5;
    public static final int STATE_STOPPING      = 6;
    public static final int STATE_ERROR         = -1;

    // Load native library
    static {
        System.loadLibrary("NexusVM");
    }

    // Singleton
    private static NexusEngine sInstance;

    public static synchronized NexusEngine getInstance() {
        if (sInstance == null) {
            sInstance = new NexusEngine();
        }
        return sInstance;
    }

    private NexusEngine() {}

    // ========================================================================
    // Public API
    // ========================================================================

    /**
     * Initialize the engine with the given configuration.
     */
    public int initialize(String romPath, String dataPath, String rootfsPath,
                          String deviceModel, String deviceBrand,
                          int displayWidth, int displayHeight, int displayDpi,
                          boolean gpuPassthrough, boolean debugLogging) {
        return nativeInitialize(romPath, dataPath, rootfsPath,
                deviceModel, deviceBrand,
                displayWidth, displayHeight, displayDpi,
                gpuPassthrough, debugLogging);
    }

    /**
     * Load a GSI ROM image.
     */
    public int loadRom(String romPath) {
        return nativeLoadRom(romPath);
    }

    /**
     * Start the guest Android OS.
     */
    public int start() {
        return nativeStart();
    }

    /**
     * Pause the guest OS.
     */
    public int pause() {
        return nativePause();
    }

    /**
     * Resume the guest OS.
     */
    public int resume() {
        return nativeResume();
    }

    /**
     * Stop the guest OS.
     */
    public int stop() {
        return nativeStop();
    }

    /**
     * Destroy the engine.
     */
    public void destroy() {
        nativeDestroy();
    }

    /**
     * Get the current engine state.
     */
    public int getState() {
        return nativeGetState();
    }

    /**
     * Get a human-readable state string.
     */
    public String getStateString() {
        switch (getState()) {
            case STATE_UNINITIALIZED: return "Uninitialized";
            case STATE_INITIALIZED:   return "Initialized";
            case STATE_ROM_LOADED:    return "ROM Loaded";
            case STATE_STARTING:      return "Starting";
            case STATE_RUNNING:       return "Running";
            case STATE_PAUSED:        return "Paused";
            case STATE_STOPPING:      return "Stopping";
            case STATE_ERROR:         return "Error";
            default:                  return "Unknown";
        }
    }

    /**
     * Set the rendering surface.
     */
    public void setSurface(Surface surface) {
        nativeSetSurface(surface);
    }

    /**
     * Send a touch event to the guest.
     */
    public void sendTouchEvent(int action, float x, float y, int pointerId) {
        nativeSendTouch(action, x, y, pointerId);
    }

    /**
     * Send a key event to the guest.
     */
    public void sendKeyEvent(int action, int keycode) {
        nativeSendKey(action, keycode);
    }

    /**
     * Set a system property in the guest.
     */
    public int setProperty(String key, String value) {
        return nativeSetProperty(key, value);
    }

    /**
     * Get a system property from the guest.
     */
    public String getProperty(String key) {
        return nativeGetProperty(key);
    }

    // ========================================================================
    // Native Methods
    // ========================================================================

    private native int nativeInitialize(String romPath, String dataPath, String rootfsPath,
                                        String deviceModel, String deviceBrand,
                                        int displayWidth, int displayHeight, int displayDpi,
                                        boolean gpuPassthrough, boolean debugLogging);
    private native int nativeLoadRom(String romPath);
    private native int nativeStart();
    private native int nativePause();
    private native int nativeResume();
    private native int nativeStop();
    private native void nativeDestroy();
    private native int nativeGetState();
    private native void nativeSetSurface(Surface surface);
    private native void nativeSendTouch(int action, float x, float y, int pointerId);
    private native void nativeSendKey(int action, int keycode);
    private native int nativeSetProperty(String key, String value);
    private native String nativeGetProperty(String key);
}
