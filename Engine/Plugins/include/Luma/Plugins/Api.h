#pragma once

#include "Luma/Core/Api.h"

#ifndef LUMA_PLUGINS_API
#ifdef LUMA_PLUGINS_BUILDING
#define LUMA_PLUGINS_API LUMA_API_EXPORT
#else
#define LUMA_PLUGINS_API LUMA_API_IMPORT
#endif
#endif

#if defined(LUMA_MODULE_DLL) && defined(_WIN32)
#define LUMA_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define LUMA_MODULE_EXPORT extern "C"
#endif
