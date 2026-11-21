// Single translation unit for tinyexr.
//
// ThirdParty/miniz.h declares the API and then includes miniz_common.h,
// miniz_tdef.h, miniz_tinfl.h and miniz_zip.h, none of which are in the tree,
// and there is no miniz.c. TINYEXR_USE_MINIZ is therefore off. Compression
// uses stb, which is already vendored under ThirdParty/stb.
//
// tinyexr declares stbi_zlib_* as extern "C", so the stb implementation is
// included after tinyexr.h (STB_*_STATIC would contradict that linkage).
#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
