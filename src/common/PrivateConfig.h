#pragma once

#if __has_include("PrivateConfig.local.h")
#include "PrivateConfig.local.h"
#endif

#ifndef INKLING_DEFAULT_SSID_1
#define INKLING_DEFAULT_SSID_1 ""
#endif
#ifndef INKLING_DEFAULT_PASS_1
#define INKLING_DEFAULT_PASS_1 ""
#endif

#ifndef INKLING_DEFAULT_SSID_2
#define INKLING_DEFAULT_SSID_2 ""
#endif
#ifndef INKLING_DEFAULT_PASS_2
#define INKLING_DEFAULT_PASS_2 ""
#endif

// Token the updater accepts uploads with when /.inkling/wifi.txt has none
// (it must match .ota-token on the computer). Empty: uploads are refused
// until the card names one.
#ifndef INKLING_DEFAULT_TOKEN
#define INKLING_DEFAULT_TOKEN ""
#endif
