#pragma once

#include "Luma/Core/Api.h"

#ifndef LUMA_SCENE_API
#ifdef LUMA_SCENE_BUILDING
#define LUMA_SCENE_API LUMA_API_EXPORT
#else
#define LUMA_SCENE_API LUMA_API_IMPORT
#endif
#endif
