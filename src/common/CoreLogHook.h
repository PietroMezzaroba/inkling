#pragma once

/* Force-included into every firmware translation unit (platformio.ini):
 * routes InklingCore's optional INKLING_LOG diagnostics into the device log
 * (DeviceLog.cpp). Plain C so vendored C sources accept it too. */
#ifdef __cplusplus
extern "C" {
#endif
void inkling_core_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#ifdef __cplusplus
}
#endif

#define INKLING_LOG(...) inkling_core_log(__VA_ARGS__)
