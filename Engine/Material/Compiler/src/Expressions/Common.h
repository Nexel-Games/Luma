#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>

#include "../ExpressionCompilers.h"

// Shared helpers for the per-category expression compilers.
//
// GLSL contract with the pass templates (Task 10): generated code reads the
// global `MaterialParameters P` (worldPos, worldPosNoOffset, worldNormal,
// worldTangent, worldBitangent, pixelNormal, texcoords[8], vertexColor,
// cameraVector, screenUV, pixelPos, pixelDepth, objectPos, objectRadius,
// objectBounds, localToWorld, worldToLocal), frame data through
// MaterialTime() / MaterialDeltaTime() / MaterialCameraPosition() /
// MaterialViewSize() / MaterialViewMatrix(), parameter-block slots
// through MP(slot), textures through MSample2D* / MSampleCube* / MTexSize on
// their slot, scene buffers through MSceneTexture / MSceneDepth /
// MSceneColor, and the Luma* helpers in material_functions.glsl.

namespace Luma::MaterialCompiler::Detail {

using Table = std::unordered_map<std::string_view, CompileFn>;

void RegisterConstantsParameters(Table& table);
void RegisterMath(Table& table);
void RegisterTextureCoordinates(Table& table);
void RegisterUtility(Table& table);

// --- Properties ---------------------------------------------------------------
f64 PropF(const MaterialGraph::ExpressionDesc& d, const MaterialGraph::Node& n, std::string_view key);
i64 PropI(const MaterialGraph::ExpressionDesc& d, const MaterialGraph::Node& n, std::string_view key);
bool PropB(const MaterialGraph::ExpressionDesc& d, const MaterialGraph::Node& n, std::string_view key);
std::string PropS(const MaterialGraph::ExpressionDesc& d, const MaterialGraph::Node& n, std::string_view key);
Math::Vec4 PropV(const MaterialGraph::ExpressionDesc& d, const MaterialGraph::Node& n, std::string_view key);

// --- Inputs ---------------------------------------------------------------------
// Compiles input `pin` at the translator's current stage.
i32 In(Translator& t, const MaterialGraph::Node& n, u32 pin);
bool Linked(const Translator& t, const MaterialGraph::Node& n, u32 pin);
// Input `pin`, or `fallback` (a chunk) when it is unlinked.
i32 InOr(Translator& t, const MaterialGraph::Node& n, u32 pin, i32 fallback);
// Mesh texture coordinate 0.
i32 DefaultUV(Translator& t);

// --- Typed math with folding ------------------------------------------------------
using Lane1 = f32 (*)(f32);
using Lane2 = f32 (*)(f32, f32);
using Lane3 = f32 (*)(f32, f32, f32);

// Broadcasts Float1 operands to the widest; 0 (and an error) when two
// non-scalar widths differ.
u32 Unify(Translator& t, MaterialGraph::NodeId at, std::initializer_list<i32*> chunks);
// `pattern` uses $0 $1 $2 for the operands. Folds lane-wise when every
// operand is a constant and `fold` is given. Result type = unified type.
i32 Map1(Translator& t, MaterialGraph::NodeId at, i32 a, std::string_view pattern, Lane1 fold);
i32 Map2(Translator& t, MaterialGraph::NodeId at, i32 a, i32 b, std::string_view pattern, Lane2 fold);
i32 Map3(Translator& t, MaterialGraph::NodeId at, i32 a, i32 b, i32 c, std::string_view pattern, Lane3 fold);
// Picks components ("x", "xy", "zx"...) of a value; folds constants.
i32 Swizzle(Translator& t, i32 value, std::string_view components);
// vecN(a, b, ...) from Float1..4 parts; folds constants.
i32 Concat(Translator& t, MaterialGraph::NodeId at, std::initializer_list<i32> parts);

// --- Textures -----------------------------------------------------------------------
// Sampler settings from a node's Sampler Type / Address / Filter properties.
TextureSlot SlotFromProperties(const MaterialGraph::ExpressionDesc& d, const MaterialGraph::Node& n);
// Common parameter metadata (name, group, sort priority, description).
ParameterInfo ParamFromProperties(const MaterialGraph::ExpressionDesc& d, const MaterialGraph::Node& n,
                                  ParameterInfo::Kind kind);
struct SampleInputs {
    i32 uvPin = 0;        // UVs input
    i32 mipValuePin = -1; // -1 = the node has no MIP inputs
    i32 ddxPin = -1, ddyPin = -1;
};
// Samples `texture` (a Texture2D/TextureCube chunk) once per node and returns
// output pin `outputPin` (RGB, R, G, B, A, RGBA).
i32 SampleOutput(Translator& t, const MaterialGraph::Node& n, const MaterialGraph::ExpressionDesc& d,
                 u32 outputPin, i32 texture, SamplerType type, const SampleInputs& inputs);
// RGB/R/G/B/A/RGBA output of a Float4 chunk.
i32 ColorOutput(Translator& t, i32 rgba, u32 outputPin);

}  // namespace Luma::MaterialCompiler::Detail
