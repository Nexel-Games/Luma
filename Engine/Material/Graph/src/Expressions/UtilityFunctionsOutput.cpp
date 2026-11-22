// Utility, functions, post-process and the Material Output node.

#include "../ExpressionBuilder.h"

namespace Luma::MaterialGraph::Detail {

void RegisterUtility(ExpressionRegistry& r) {
    r.Add(Expr("Fresnel", "Fresnel", "Utility",
               "Schlick Fresnel: 1 at grazing angles, Base Reflect Fraction facing the camera.")
              .In("ExponentIn", VT::Float1, F(5.0))
              .In("BaseReflectFractionIn", VT::Float1, F(0.04))
              .In("Normal", VT::Float3, F(0.0), "Defaults to the pixel normal.")
              .Out("", VT::Float1)
              .Keywords({"rim", "edge", "facing"})
              .Build());
    r.Add(Expr("BumpOffset", "Bump Offset", "Utility",
               "Cheap parallax: offsets UVs along the camera vector by height.")
              .In("Coordinate", VT::Float2, F(0.0))
              .In("Height", VT::Float1, F(0.0), "", false)
              .In("HeightRatioInput", VT::Float1, F(0.05))
              .Out("", VT::Float2)
              .Prop("referencePlane", "Reference Plane", PK::Float, F(0.5))
              .Keywords({"parallax"})
              .Build());
    r.Add(Expr("ParallaxOcclusionMapping", "Parallax Occlusion Mapping", "Utility",
               "Ray-marches a height map for depth; feed Parallax UVs into every texture sample.")
              .In("HeightmapTexture", VT::Texture2D, F(0.0), "", false)
              .In("HeightRatio", VT::Float1, F(0.05))
              .In("MinSteps", VT::Float1, F(8.0))
              .In("MaxSteps", VT::Float1, F(32.0))
              .In("UVs", VT::Float2, F(0.0))
              .Out("ParallaxUVs", VT::Float2)
              .Out("PixelDepthOffset", VT::Float1)
              .Enum("channel", "Heightmap Channel", {"R", "G", "B", "A"}, 0)
              .PixelOnly()
              .Keywords({"pom", "parallax", "height", "displacement"})
              .Build());
    r.Add(Expr("NormalFromHeight", "Normal From Heightmap", "Utility",
               "Tangent-space normal from a height texture's per-texel slope.")
              .In("HeightMap", VT::Texture2D, F(0.0), "", false)
              .In("Intensity", VT::Float1, F(1.0))
              .In("UVs", VT::Float2, F(0.0))
              .Out("", VT::Float3)
              .PixelOnly()
              .Keywords({"bump", "height", "normal"})
              .Build());
    r.Add(Expr("BlendAngleCorrectedNormals", "Blend Angle Corrected Normals", "Utility",
               "Reoriented normal mapping: layers a detail normal onto a base normal.")
              .In("BaseNormal", VT::Float3, V(0, 0, 1, 0))
              .In("AdditionalNormal", VT::Float3, V(0, 0, 1, 0))
              .Out("", VT::Float3)
              .Keywords({"detail", "normal", "blend"})
              .Build());
    r.Add(Expr("Transform", "Transform Vector", "Utility", "Transforms a direction between spaces.")
              .In("Input", VT::Float3, F(0.0), "", false)
              .Out("", VT::Float3)
              .Enum("source", "Source", {"Tangent", "Local", "World", "View"}, 0)
              .Enum("destination", "Destination", {"Tangent", "Local", "World", "View"}, 2)
              .Build());
    r.Add(Expr("TransformPosition", "Transform Position", "Utility",
               "Transforms a position between spaces.")
              .In("Input", VT::Float3, F(0.0), "", false)
              .Out("", VT::Float3)
              .Enum("source", "Source", {"Local", "World", "View"}, 0)
              .Enum("destination", "Destination", {"Local", "World", "View"}, 1)
              .Build());
    r.Add(Expr("DDX", "DDX", "Utility", "Screen-space X derivative.")
              .In("Value", VT::Any, F(0.0))
              .Out("", VT::Any)
              .PixelOnly()
              .Build());
    r.Add(Expr("DDY", "DDY", "Utility", "Screen-space Y derivative.")
              .In("Value", VT::Any, F(0.0))
              .Out("", VT::Any)
              .PixelOnly()
              .Build());
    r.Add(Expr("Custom", "Custom", "Utility",
               "Hand-written GLSL. Inputs are named in 'inputs' (comma-separated); the code "
               "returns the output and may write extra outputs declared in 'outputs'.")
              .Out("", VT::Any)
              .Prop("code", "Code", PK::Code, S("return vec3(1.0, 0.0, 1.0);"))
              .Enum("outputType", "Output Type", {"Float1", "Float2", "Float3", "Float4"}, 2)
              .Prop("inputs", "Inputs", PK::String, S(""), "Comma-separated input names.")
              .Prop("outputs", "Additional Outputs", PK::String, S(""),
                    "Comma-separated Name:Type entries, e.g. 'Mask:Float1'.")
              .Prop("includes", "Include Files", PK::String, S(""),
                    "Comma-separated engine shader includes.")
              .Dynamic()
              .Keywords({"code", "glsl", "shader"})
              .Build());
    r.Add(Expr("VertexInterpolator", "Vertex Interpolator", "Utility",
               "Computes a value in the vertex shader and interpolates it to pixels.")
              .In("VS", VT::Any, F(0.0), "", false)
              .Out("PS", VT::Any)
              .Keywords({"interpolate", "custom uv", "vertex"})
              .Build());
    r.Add(Expr("NamedRerouteDeclaration", "Named Reroute Declaration", "Utility",
               "Names a value so Named Reroute Usage nodes can read it anywhere.")
              .In("Input", VT::Any, F(0.0))
              .Out("", VT::Any)
              .Prop("name", "Name", PK::String, S("Reroute"))
              .Build());
    r.Add(Expr("NamedRerouteUsage", "Named Reroute Usage", "Utility", "Reads a Named Reroute Declaration.")
              .Out("", VT::Any)
              .Prop("name", "Name", PK::String, S("Reroute"))
              .Build());
    r.Add(Expr("Reroute", "Reroute", "Utility", "Passes its input through (wire organisation).")
              .In("Input", VT::Any, F(0.0))
              .Out("", VT::Any)
              .Build());
}

void RegisterFunctions(ExpressionRegistry& r) {
    r.Add(Expr("MaterialFunctionCall", "Material Function Call", "Functions",
               "Calls a Material Function; its inputs and outputs come from the function asset.")
              .Out("", VT::Any)
              .Asset("function", "Material Function", "MaterialFunction")
              .Dynamic()
              .Keywords({"function", "call", "mf"})
              .Build());
    r.Add(Expr("FunctionInput", "Function Input", "Functions", "An input of a Material Function.")
              .In("Preview", VT::Any, F(0.0), "Value used when the caller leaves the input unconnected "
                                              "(with Use Preview Value As Default) or for previews.")
              .Out("", VT::Any)
              .Prop("name", "Input Name", PK::String, S("In"))
              .Enum("inputType", "Input Type",
                    {"Float1", "Float2", "Float3", "Float4", "Texture2D", "TextureCube", "StaticBool"}, 2)
              .Prop("sortPriority", "Sort Priority", PK::Int, I(0))
              .Prop("usePreviewAsDefault", "Use Preview Value As Default", PK::Bool, B(true))
              .Prop("description", "Description", PK::String, S(""))
              .Hidden()
              .Build());
    r.Add(Expr("FunctionOutput", "Function Output", "Functions", "An output of a Material Function.")
              .In("Output", VT::Any, F(0.0))
              .Prop("name", "Output Name", PK::String, S("Result"))
              .Prop("sortPriority", "Sort Priority", PK::Int, I(0))
              .Prop("description", "Description", PK::String, S(""))
              .Hidden()
              .Build());
}

void RegisterPostProcess(ExpressionRegistry& r) {
    r.Add(Expr("SceneTexture", "Scene Texture", "Post Process",
               "Reads a scene buffer (post-process materials; Scene Color/Depth also in translucency).")
              .In("UVs", VT::Float2, F(0.0), "Defaults to this pixel.")
              .Out("Color", VT::Float4)
              .Out("Size", VT::Float2)
              .Out("InvSize", VT::Float2)
              .Enum("id", "Scene Texture Id",
                    {"SceneColor", "SceneDepth", "WorldNormal", "BaseColor", "Roughness", "Metallic",
                     "Specular", "CustomData", "PostProcessInput0"},
                    0)
              .PixelOnly()
              .Keywords({"scene", "buffer", "gbuffer", "post"})
              .Build());
    r.Add(Expr("SceneColor", "Scene Color", "Post Process",
               "Opaque scene colour behind a translucent pixel (refraction-style lookups).")
              .In("OffsetFraction", VT::Float2, F(0.0), "Screen-space offset (fraction of the view).")
              .Out("", VT::Float3)
              .PixelOnly()
              .Build());
}

void RegisterOutput(ExpressionRegistry& r) {
    Expr e("MaterialOutput", "Material Output", "Output",
           "The material's result. Which inputs are used depends on the domain, blend mode and "
           "shading model.");
    e.In("BaseColor", VT::Float3, F(0.0))
        .In("Metallic", VT::Float1, F(0.0))
        .In("Specular", VT::Float1, F(0.5))
        .In("Roughness", VT::Float1, F(0.5))
        .In("Anisotropy", VT::Float1, F(0.0))
        .In("EmissiveColor", VT::Float3, F(0.0))
        .In("Opacity", VT::Float1, F(1.0))
        .In("OpacityMask", VT::Float1, F(1.0))
        .In("Normal", VT::Float3, V(0, 0, 1, 0), "Tangent-space normal.")
        .In("Tangent", VT::Float3, V(1, 0, 0, 0), "Tangent-space tangent (anisotropy).")
        .In("WorldPositionOffset", VT::Float3, F(0.0), "Vertex offset in world units.")
        .In("SubsurfaceColor", VT::Float3, V(1, 1, 1, 0), "Subsurface / Two-Sided Foliage colour.")
        .In("CustomData0", VT::Float1, F(0.0), "Clear Coat amount.")
        .In("CustomData1", VT::Float1, F(0.0), "Clear Coat roughness.")
        .In("AmbientOcclusion", VT::Float1, F(1.0))
        .In("Refraction", VT::Float1, F(1.0), "Index of refraction / pixel offset (translucency).")
        .In("PixelDepthOffset", VT::Float1, F(0.0));
    for (const char* uv : {"CustomUV0", "CustomUV1", "CustomUV2", "CustomUV3", "CustomUV4",
                           "CustomUV5", "CustomUV6", "CustomUV7"}) {
        e.In(uv, VT::Float2, F(0.0), "Computed in the vertex shader; read via Texture Coordinate.");
    }
    r.Add(e.Hidden().Build());
}

}  // namespace Luma::MaterialGraph::Detail
