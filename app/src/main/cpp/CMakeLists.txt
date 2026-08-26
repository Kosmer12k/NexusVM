cmake_minimum_required(VERSION 3.22.1)
project("nexusvm" VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)

# ============================================================================
# NexusVM Engine — Android Container Engine (Non-Root)
# ============================================================================
#
# This builds libNexusVM.so — the core engine that runs guest Android OS
# inside a userspace container without root, QEMU, or pKVM.
#
# Architecture:
#   [Guest Android OS]
#        |
#   [C++ Engine: Hooking, Virtual Binder, EGL Proxy, etc.]
#        |
#   [Host Android OS + GPU Hardware]
# ============================================================================

# Common compiler flags
add_compile_options(
    -fvisibility=hidden          # Hide internal symbols
    -ffunction-sections          # Enable dead code elimination
    -fdata-sections
    -fno-exceptions              # We handle errors via return codes
    -fno-rtti                    # No RTTI overhead
    -Wno-unused-parameter        # Suppress for interface stubs
)

# Linker flags
add_link_options(
    -Wl,--gc-sections            # Remove unused sections
    -Wl,--hash-style=gnu
)

# ============================================================================
# Source Files — organized by module
# ============================================================================

set(ENGINE_CORE_SOURCES
    engine/nexus_engine.cpp
    engine/nexus_engine.h
)

set(UTILS_SOURCES
    engine/utils/logger.cpp
    engine/utils/logger.h
    engine/utils/memory_utils.cpp
    engine/utils/memory_utils.h
    engine/utils/file_utils.cpp
    engine/utils/file_utils.h
    engine/utils/elf_parser.cpp
    engine/utils/elf_parser.h
)

set(MODULE_CONTAINER_LOADER_SOURCES
    engine/modules/container_loader.cpp
    engine/modules/container_loader.h
)

set(MODULE_SYSCALL_INTERCEPTOR_SOURCES
    engine/modules/syscall_interceptor.cpp
    engine/modules/syscall_interceptor.h
)

set(MODULE_FAKE_ROOT_SOURCES
    engine/modules/fake_root.cpp
    engine/modules/fake_root.h
)

set(MODULE_VIRTUAL_BINDER_SOURCES
    engine/modules/virtual_binder.cpp
    engine/modules/virtual_binder.h
)

set(MODULE_PROPERTY_SPOOF_SOURCES
    engine/modules/property_spoof.cpp
    engine/modules/property_spoof.h
)

set(MODULE_EGL_PROXY_SOURCES
    engine/modules/egl_proxy.cpp
    engine/modules/egl_proxy.h
)

set(MODULE_PROCESS_MANAGER_SOURCES
    engine/modules/process_manager.cpp
    engine/modules/process_manager.h
)

set(MODULE_INPUT_BRIDGE_SOURCES
    engine/modules/input_bridge.cpp
    engine/modules/input_bridge.h
)

set(HOOKING_SOURCES
    engine/hooking/plt_hook.cpp
    engine/hooking/plt_hook.h
    engine/hooking/inline_hook.cpp
    engine/hooking/inline_hook.h
)

# ============================================================================
# Main Library: libNexusVM.so
# ============================================================================

add_library(NexusVM SHARED
    # JNI entry point
    nexusvm_jni.cpp

    # Engine core
    ${ENGINE_CORE_SOURCES}

    # Utilities
    ${UTILS_SOURCES}

    # Hooking framework
    ${HOOKING_SOURCES}

    # Modules
    ${MODULE_CONTAINER_LOADER_SOURCES}
    ${MODULE_SYSCALL_INTERCEPTOR_SOURCES}
    ${MODULE_FAKE_ROOT_SOURCES}
    ${MODULE_VIRTUAL_BINDER_SOURCES}
    ${MODULE_PROPERTY_SPOOF_SOURCES}
    ${MODULE_EGL_PROXY_SOURCES}
    ${MODULE_PROCESS_MANAGER_SOURCES}
    ${MODULE_INPUT_BRIDGE_SOURCES}
)

# Include directories
target_include_directories(NexusVM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/modules
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/hooking
)

# Link Android system libraries
target_link_libraries(NexusVM
    android           # ANativeWindow, AAssetManager
    log               # __android_log_print
    dl                # dlopen, dlsym (for hooking)
    EGL               # EGL functions (GPU proxy)
    GLESv3            # OpenGL ES 3.x
    z                 # zlib (for image extraction)
    m                 # math
)

# ============================================================================
# Injected Libraries (loaded into guest processes)
# ============================================================================

# libfakeroot.so — Injected into guest to hook uid/gid/capabilities
add_library(fakeroot SHARED
    engine/inject/fakeroot_inject.cpp
    engine/inject/fakeroot_inject.h
)
target_include_directories(fakeroot PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/utils
)
target_link_libraries(fakeroot log dl)
set_target_properties(fakeroot PROPERTIES OUTPUT_NAME "fakeroot")

# libegl_proxy.so — Injected into guest to redirect EGL/GLES calls to host
add_library(egl_proxy SHARED
    engine/inject/egl_proxy_inject.cpp
    engine/inject/egl_proxy_inject.h
)
target_include_directories(egl_proxy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/utils
)
target_link_libraries(egl_proxy log dl EGL GLESv3)
set_target_properties(egl_proxy PROPERTIES OUTPUT_NAME "egl_proxy")

# ============================================================================
# Print build info
# ============================================================================
message(STATUS "=== NexusVM Engine Build Configuration ===")
message(STATUS "  C++ Standard:     ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "  Android ABI:      ${ANDROID_ABI}")
message(STATUS "  Android Platform: ${ANDROID_PLATFORM}")
message(STATUS "  NDK Path:         ${ANDROID_NDK}")
message(STATUS "==========================================")
