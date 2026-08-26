/*
 * NexusVM — JNI Bridge
 * 
 * Connects the Java Android app to the C++ engine.
 * All engine operations are called through this bridge.
 */

#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>

#include "engine/nexus_engine.h"
#include "engine/utils/logger.h"

using namespace nexusvm;

// ============================================================================
// Helper: Convert jstring to std::string
// ============================================================================

static std::string jstringToString(JNIEnv* env, jstring str) {
    if (!str) return "";
    const char* chars = env->GetStringUTFChars(str, nullptr);
    std::string result(chars);
    env->ReleaseStringUTFChars(str, chars);
    return result;
}

// ============================================================================
// JNI: Engine Lifecycle
// ============================================================================

extern "C" JNIEXPORT jint JNICALL
Java_com_nexusvm_engine_NexusEngine_nativeInitialize(
        JNIEnv* env, jobject /* this */,
        jstring romPath, jstring dataPath, jstring rootfsPath,
        jstring deviceModel, jstring deviceBrand,
        jint displayWidth, jint displayHeight, jint displayDpi,
        jboolean gpuPassthrough, jboolean debugLogging) {
    
    EngineConfig config;
    config.rom_path       = jstringToString(env, romPath);
    config.data_path      = jstringToString(env, dataPath);
    config.rootfs_path    = jstringToString(env, rootfsPath);
    config.device_model   = jstringToString(env, deviceModel);
    config.device_brand   = jstringToString(env, deviceBrand);
    config.display_width  = displayWidth;
    config.display_height = displayHeight;
    config.display_dpi    = displayDpi;
    config.gpu_passthrough = gpuPassthrough;
    config.debug_logging  = debugLogging;
    
    return NexusEngine::getInstance().initialize(config);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_nexusvm_engine_NexusEngine_nativeLoadRom(
        JNIEnv* env, jobject /* this */, jstring romPath) {
    return NexusEngine::getInstance().loadRom(jstringToString(env, romPath));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_nexusvm_engine_NexusEngine_nativeStart(
        JNIEnv* /* env */, jobject /* this */) {
    return NexusEngine::getInstance().start();
}

extern "C" JNIEXPORT jint JNICALL
Java_com_nexusvm_engine_NexusEngine_nativePause(
        JNIEnv* /* env */, jobject /* this */) {
    return NexusEngine::getInstance().pause();
}

extern "C" JNIEXPORT jint JNICALL
Java_com_nexusvm_engine_NexusEngine_nativeResume(
        JNIEnv* /* env */, jobject /* this */) {
    return NexusEngine::getInstance().resume();
}

extern "C" JNIEXPORT jint JNICALL
Java_com_nexusvm_engine_NexusEngine_nativeStop(
        JNIEnv* /* env */, jobject /* this */) {
    return NexusEngine::getInstance().stop();
}

extern "C" JNIEXPORT void JNICALL
Java_com_nexusvm_engine_NexusEngine_nativeDestroy(
        JNIEnv* /* env */, jobject /* this */) {
    NexusEngine::getInstance().destroy();
}

// ============================================================================
// JNI: Runtime
// ============================================================================

extern "C" JNIEXPORT jint JNICALL
Java_com_nexusvm_engine_NexusEngine_nativeGetState(
        JNIEnv* /* env */, jobject /* this */) {
    return static_cast<jint>(NexusEngine::getInstance().getState());
}

extern "C" JNIEXPORT void JNICALL
Java_com_nexusvm_engine_NexusEngine_nativeSetSurface(
        JNIEnv* env, jobject /* this */, jobject surface) {
    ANativeWindow* window = nullptr;
    if (surface) {
        window = ANativeWindow_fromSurface(env, surface);
    }
    NexusEngine::getInstance().setNativeWindow(window);
}

extern "C" JNIEXPORT void JNICALL
Java_com_nexusvm_engine_NexusEngine_nativeSendTouch(
        JNIEnv* /* env */, jobject /* this */,
        jint action, jfloat x, jfloat y, jint pointerId) {
    NexusEngine::getInstance().sendTouchEvent(action, x, y, pointerId);
}

extern "C" JNIEXPORT void JNICALL
Java_com_nexusvm_engine_NexusEngine_nativeSendKey(
        JNIEnv* /* env */, jobject /* this */,
        jint action, jint keycode) {
    NexusEngine::getInstance().sendKeyEvent(action, keycode);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_nexusvm_engine_NexusEngine_nativeSetProperty(
        JNIEnv* env, jobject /* this */,
        jstring key, jstring value) {
    return NexusEngine::getInstance().setGuestProperty(
        jstringToString(env, key), jstringToString(env, value));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_nexusvm_engine_NexusEngine_nativeGetProperty(
        JNIEnv* env, jobject /* this */, jstring key) {
    std::string value = NexusEngine::getInstance().getGuestProperty(
        jstringToString(env, key));
    return env->NewStringUTF(value.c_str());
}
