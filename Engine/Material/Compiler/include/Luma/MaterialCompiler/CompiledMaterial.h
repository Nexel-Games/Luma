#pragma once

#include <string>
#include <vector>

#include "Luma/MaterialCompiler/CodeChunk.h"
#include "Luma/MaterialCompiler/Parameters.h"

// The backend-neutral result of compiling a material graph. Later stages add
// the parameter block, preshader program, texture table (Task 7) and the
// per-pass GLSL assembled from templates (Task 10).

namespace Luma::MaterialCompiler {

enum class Pass : u8 { GBuffer, DepthOnly, SelectionMask, Forward, PostProcess, Count };

struct CompileError {
    MaterialGraph::NodeId node = 0;  // 0 = the material as a whole
    i32 pin = -1;                    // input pin on `node`, -1 = the node
    std::string message;
    bool warning = false;
};

// One Material Output input the material uses, as a GLSL expression of the
// pin's type, valid after its stage's body has run.
struct MaterialOutputValue {
    std::string pin;  // Material Output pin name ("BaseColor", "WorldPositionOffset", ...)
    Stage stage = Stage::Pixel;
    MaterialGraph::ValueType type = MaterialGraph::ValueType::Float1;
    std::string code;
};

// A value computed in the vertex shader and read by the pixel shader
// (Vertex Interpolator). The pixel stage reads it as `lumaInterp<index>`.
struct Interpolator {
    MaterialGraph::ValueType type = MaterialGraph::ValueType::Float1;
    std::string vertexCode;  // GLSL expression valid after the vertex body
};

struct CompiledMaterial {
    // Generated helper functions (Custom nodes), before the stage bodies.
    std::string functions;
    std::vector<std::string> includes;  // extra engine shader includes (Custom nodes)
    // Local variable declarations, in emission order, for each stage.
    std::string vertexBody;
    std::string pixelBody;
    std::vector<Interpolator> interpolators;
    std::vector<ParameterInfo> parameters;  // in declaration order
    ParameterBlockLayout layout;
    std::vector<MaterialOutputValue> outputs;  // Material Output pin order
    std::vector<CompileError> errors;
    std::vector<CompileError> warnings;
    std::string staticSwitchKey;  // canonical "name=0|name=1" of the permutation

    bool Ok() const { return errors.empty(); }
    // Stable hash of everything generated (equal for identical graphs).
    std::string Fingerprint() const;
};

}  // namespace Luma::MaterialCompiler
