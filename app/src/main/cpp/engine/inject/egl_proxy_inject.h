/*
 * NexusVM — EGL Proxy Inject Library Header
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void __attribute__((constructor)) nexusvm_egl_proxy_init();
void __attribute__((destructor)) nexusvm_egl_proxy_cleanup();

#ifdef __cplusplus
}
#endif
