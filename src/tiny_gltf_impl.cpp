// Image decoding is left to texture.cppm's own stb_image-based Texture
// class (same as the OBJ path), so tinygltf's own image loading is disabled
// via the TINYGLTF_NO_STB_IMAGE/_WRITE/_EXTERNAL_IMAGE macros (set globally
// in CMakeLists.txt, not just here — see the comment there for why) —
// model.cppm reads Image::uri itself and resolves it the same way
// loadModel() resolves OBJ texture paths.
#define TINYGLTF_IMPLEMENTATION
#include "tiny_gltf.h"
