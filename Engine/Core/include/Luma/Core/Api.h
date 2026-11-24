#pragma once

// Per-library export macros for modular (DLL) builds. STATIC builds expand to
// nothing. WINDOWS_EXPORT_ALL_SYMBOLS still exports unannotated symbols; these
// macros mark the types plugins and other DLLs are specified to share.

#if defined(LUMA_MODULAR) && defined(_WIN32)
#define LUMA_API_EXPORT __declspec(dllexport)
#define LUMA_API_IMPORT __declspec(dllimport)
#else
#define LUMA_API_EXPORT
#define LUMA_API_IMPORT
#endif

#ifndef LUMA_CORE_API
#ifdef LUMA_CORE_BUILDING
#define LUMA_CORE_API LUMA_API_EXPORT
#else
#define LUMA_CORE_API LUMA_API_IMPORT
#endif
#endif
