// Texture and coordinate expressions.

#include "../ExpressionBuilder.h"

namespace Luma::MaterialGraph::Detail {

void RegisterTexture(ExpressionRegistry& r) {
    {
        Expr e("TextureSample", "Texture Sample", "Texture",
               "Samples a texture (2D or cube) set on the node or wired into Tex.");
        e.In("UVs", VT::Any, F(0.0), "Float2 for 2D, Float3 direction for cube; default UV 0.")
            .In("Tex", VT::Any, F(0.0), "Optional texture object overriding the node's texture.")
            .In("MipValue", VT::Float1, F(0.0), "Bias or level, per MIP Value Mode.")
            .In("DDX", VT::Float2, F(0.0), "Derivatives mode only.")
            .In("DDY", VT::Float2, F(0.0), "Derivatives mode only.")
            .Out("RGB", VT::Float3)
            .Out("R", VT::Float1)
            .Out("G", VT::Float1)
            .Out("B", VT::Float1)
            .Out("A", VT::Float1)
            .Out("RGBA", VT::Float4)
            .Asset("texture", "Texture", "Texture");
        SamplerProps(e);
        r.Add(e.Keywords({"texture", "sample", "tex"}).Build());
    }
    r.Add(Expr("TextureObject", "Texture Object", "Texture",
               "A texture reference passed to functions or a Texture Sample's Tex input.")
              .Out("", VT::Texture2D)
              .Asset("texture", "Texture", "Texture")
              .Build());
    r.Add(Expr("TextureProperty", "Texture Property", "Texture",
               "Size or texel size of a texture object.")
              .In("Texture", VT::Texture2D, F(0.0), "", false)
              .Out("", VT::Float2)
              .Enum("property", "Property", {"TextureSize", "TexelSize"}, 0)
              .Build());
}

void RegisterCoordinates(ExpressionRegistry& r) {
    r.Add(Expr("TextureCoordinate", "Texture Coordinate", "Coordinates", "A mesh UV channel, tiled.")
              .Out("", VT::Float2)
              .Prop("index", "Coordinate Index", PK::Int, I(0), "", 0, 7)
              .Prop("uTiling", "U Tiling", PK::Float, F(1.0))
              .Prop("vTiling", "V Tiling", PK::Float, F(1.0))
              .Prop("unMirrorU", "Un Mirror U", PK::Bool, B(false))
              .Prop("unMirrorV", "Un Mirror V", PK::Bool, B(false))
              .Keywords({"uv", "texcoord"})
              .Build());
    r.Add(Expr("Panner", "Panner", "Coordinates", "Scrolls coordinates over time.")
              .In("Coordinate", VT::Float2, F(0.0), "Defaults to UV 0.")
              .In("Time", VT::Float1, F(0.0), "Defaults to scene time.")
              .In("Speed", VT::Float2, F(0.0), "Overrides Speed X/Y when connected.")
              .Out("", VT::Float2)
              .Prop("speedX", "Speed X", PK::Float, F(0.0))
              .Prop("speedY", "Speed Y", PK::Float, F(0.0))
              .Prop("fractionalPart", "Fractional Part", PK::Bool, B(false))
              .Keywords({"scroll", "pan", "animate"})
              .Build());
    r.Add(Expr("Rotator", "Rotator", "Coordinates", "Rotates coordinates around a centre over time.")
              .In("Coordinate", VT::Float2, F(0.0))
              .In("Time", VT::Float1, F(0.0))
              .Out("", VT::Float2)
              .Prop("centerX", "Center X", PK::Float, F(0.5))
              .Prop("centerY", "Center Y", PK::Float, F(0.5))
              .Prop("speed", "Speed", PK::Float, F(0.25), "Rotation speed in radians per second.")
              .Keywords({"rotate", "spin"})
              .Build());
    r.Add(Expr("WorldPosition", "Absolute World Position", "Coordinates",
               "World-space position of the pixel / vertex.")
              .Out("", VT::Float3)
              .Enum("offsets", "Shader Offsets", {"IncludingOffsets", "ExcludingOffsets"}, 0,
                    "Whether World Position Offset is included.")
              .Keywords({"world", "position", "location"})
              .Build());
    r.Add(Expr("ObjectPosition", "Object Position", "Coordinates", "World position of the object's origin.")
              .Out("", VT::Float3)
              .Build());
    r.Add(Expr("ObjectRadius", "Object Radius", "Coordinates", "Radius of the object's bounding sphere.")
              .Out("", VT::Float1)
              .Build());
    r.Add(Expr("ObjectBounds", "Object Bounds", "Coordinates", "Size of the object's bounding box.")
              .Out("", VT::Float3)
              .Build());
    r.Add(Expr("ActorPosition", "Actor Position", "Coordinates", "World position of the owning actor.")
              .Out("", VT::Float3)
              .Build());
    r.Add(Expr("CameraPositionWS", "Camera Position", "Coordinates", "World-space camera position.")
              .Out("", VT::Float3)
              .Keywords({"eye", "view"})
              .Build());
    r.Add(Expr("CameraVectorWS", "Camera Vector", "Coordinates",
               "Normalised direction from the surface to the camera.")
              .Out("", VT::Float3)
              .Keywords({"view", "vector", "eye"})
              .Build());
    r.Add(Expr("VertexNormalWS", "Vertex Normal", "Coordinates", "World-space interpolated vertex normal.")
              .Out("", VT::Float3)
              .Build());
    r.Add(Expr("PixelNormalWS", "Pixel Normal", "Coordinates",
               "World-space normal after the Normal input (normal maps) is applied.")
              .Out("", VT::Float3)
              .PixelOnly()
              .Build());
    r.Add(Expr("VertexTangentWS", "Vertex Tangent", "Coordinates", "World-space vertex tangent.")
              .Out("", VT::Float3)
              .Build());
    r.Add(Expr("ReflectionVectorWS", "Reflection Vector", "Coordinates",
               "Camera vector reflected about the (vertex) normal.")
              .Out("", VT::Float3)
              .Build());
    r.Add(Expr("ScreenPosition", "Screen Position", "Coordinates", "Pixel position on screen.")
              .Out("ViewportUV", VT::Float2)
              .Out("PixelPosition", VT::Float2)
              .PixelOnly()
              .Build());
    r.Add(Expr("ViewSize", "View Size", "Coordinates", "Viewport size in pixels.")
              .Out("", VT::Float2)
              .Build());
    r.Add(Expr("PixelDepth", "Pixel Depth", "Coordinates", "Linear view depth of this pixel.")
              .Out("", VT::Float1)
              .PixelOnly()
              .Build());
    r.Add(Expr("SceneDepth", "Scene Depth", "Coordinates",
               "Linear depth of opaque geometry behind this pixel (translucent / post process).")
              .In("UVs", VT::Float2, F(0.0), "Defaults to this pixel.")
              .Out("", VT::Float1)
              .PixelOnly()
              .Build());
    r.Add(Expr("Time", "Time", "Coordinates", "Scene time in seconds.")
              .Out("", VT::Float1)
              .Prop("period", "Period", PK::Float, F(0.0), "Wraps time every Period seconds (0 = never).")
              .Keywords({"animate", "seconds"})
              .Build());
    r.Add(Expr("DeltaTime", "Delta Time", "Coordinates", "Seconds since the previous frame.")
              .Out("", VT::Float1)
              .Build());
    r.Add(Expr("VertexColor", "Vertex Color", "Coordinates", "The mesh's per-vertex colour.")
              .Out("RGB", VT::Float3)
              .Out("R", VT::Float1)
              .Out("G", VT::Float1)
              .Out("B", VT::Float1)
              .Out("A", VT::Float1)
              .Out("RGBA", VT::Float4)
              .Build());
}

}  // namespace Luma::MaterialGraph::Detail
