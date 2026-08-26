/*
 * NexusVM — Fakeroot Inject Library
 * 
 * This library is loaded via LD_PRELOAD into guest processes.
 * It intercepts libc calls for UID/GID spoofing and path redirection.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// These functions are automatically called by the dynamic linker
void __attribute__((constructor)) nexusvm_inject_init();
void __attribute__((destructor)) nexusvm_inject_cleanup();

#ifdef __cplusplus
}
#endif
