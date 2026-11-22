// Constants and parameters.

#include "../ExpressionBuilder.h"

namespace Luma::MaterialGraph::Detail {

void RegisterConstants(ExpressionRegistry& r) {
    r.Add(Expr("Constant", "Constant", "Constants", "A single float value.")
              .Out("", VT::Float1)
              .Prop("value", "Value", PK::Float, F(0.0))
              .Keywords({"1", "scalar", "float"})
              .Build());
    r.Add(Expr("Constant2Vector", "Constant2Vector", "Constants", "A two-component float value.")
              .Out("", VT::Float2)
              .Prop("value", "Value", PK::Vec4, V(0, 0, 0, 0))
              .Keywords({"2", "float2", "vector"})
              .Build());
    r.Add(Expr("Constant3Vector", "Constant3Vector", "Constants",
               "A three-component float value (often a colour).")
              .Out("", VT::Float3)
              .Prop("value", "Value", PK::Vec4, V(0, 0, 0, 0))
              .Keywords({"3", "float3", "color", "colour", "rgb"})
              .Build());
    r.Add(Expr("Constant4Vector", "Constant4Vector", "Constants", "A four-component float value.")
              .Out("", VT::Float4)
              .Prop("value", "Value", PK::Vec4, V(0, 0, 0, 0))
              .Keywords({"4", "float4", "rgba"})
              .Build());
    r.Add(Expr("StaticBool", "Static Bool", "Constants",
               "A compile-time boolean, for Static Switch inputs.")
              .Out("", VT::Bool)
              .Prop("value", "Value", PK::Bool, B(false))
              .Build());
}

void RegisterParameters(ExpressionRegistry& r) {
    r.Add(Expr("ScalarParameter", "Scalar Parameter", "Parameters",
               "A float that Material Instances can override without recompiling.")
              .Out("", VT::Float1)
              .ParamMeta("Param")
              .Prop("default", "Default Value", PK::Float, F(0.0))
              .Prop("sliderMin", "Slider Min", PK::Float, F(0.0))
              .Prop("sliderMax", "Slider Max", PK::Float, F(0.0), "Equal min/max = no slider.")
              .Keywords({"param", "scalar", "float"})
              .Build());
    r.Add(Expr("VectorParameter", "Vector Parameter", "Parameters",
               "A colour / float4 that Material Instances can override.")
              .Out("RGB", VT::Float3)
              .Out("R", VT::Float1)
              .Out("G", VT::Float1)
              .Out("B", VT::Float1)
              .Out("A", VT::Float1)
              .Out("RGBA", VT::Float4)
              .ParamMeta("Color")
              .Prop("default", "Default Value", PK::Vec4, V(1, 1, 1, 1))
              .Keywords({"param", "color", "colour", "vector"})
              .Build());
    {
        Expr e("TextureSampleParameter2D", "Texture Sample Parameter 2D", "Parameters",
               "Samples a 2D texture that Material Instances can replace.");
        e.In("UVs", VT::Float2, F(0.0), "Defaults to texture coordinate 0.")
            .Out("RGB", VT::Float3)
            .Out("R", VT::Float1)
            .Out("G", VT::Float1)
            .Out("B", VT::Float1)
            .Out("A", VT::Float1)
            .Out("RGBA", VT::Float4)
            .ParamMeta("Texture")
            .Asset("texture", "Texture", "Texture");
        SamplerProps(e);
        r.Add(e.Keywords({"param", "texture", "sample"}).Build());
    }
    {
        Expr e("TextureSampleParameterCube", "Texture Sample Parameter Cube", "Parameters",
               "Samples a cube texture that Material Instances can replace.");
        e.In("UVs", VT::Float3, F(0.0), "Direction; defaults to the reflection vector.")
            .Out("RGB", VT::Float3)
            .Out("R", VT::Float1)
            .Out("G", VT::Float1)
            .Out("B", VT::Float1)
            .Out("A", VT::Float1)
            .Out("RGBA", VT::Float4)
            .ParamMeta("Cubemap")
            .Asset("texture", "Texture", "TextureCube");
        SamplerProps(e);
        r.Add(e.Keywords({"param", "cube", "cubemap"}).Build());
    }
    r.Add(Expr("TextureObjectParameter", "Texture Object Parameter", "Parameters",
               "A replaceable texture passed to other nodes (functions, Texture Sample).")
              .Out("", VT::Texture2D)
              .ParamMeta("Texture")
              .Asset("texture", "Texture", "Texture")
              .Build());
    r.Add(Expr("StaticSwitchParameter", "Static Switch Parameter", "Parameters",
               "Chooses True or False at compile time; instances overriding it compile their own "
               "shader permutation.")
              .In("True", VT::Any, F(0.0))
              .In("False", VT::Any, F(0.0))
              .Out("", VT::Any)
              .ParamMeta("Switch")
              .Prop("default", "Default Value", PK::Bool, B(false))
              .Keywords({"static", "switch", "permutation"})
              .Build());
    r.Add(Expr("StaticBoolParameter", "Static Bool Parameter", "Parameters",
               "A compile-time boolean parameter for Static Switch inputs.")
              .Out("", VT::Bool)
              .ParamMeta("Bool")
              .Prop("default", "Default Value", PK::Bool, B(false))
              .Build());
    r.Add(Expr("CollectionParameter", "Collection Parameter", "Parameters",
               "Reads a global value from a Material Parameter Collection.")
              .Out("", VT::Float4)
              .Asset("collection", "Collection", "ParameterCollection")
              .Prop("parameter", "Parameter Name", PK::String, S(""))
              .Keywords({"mpc", "global", "collection"})
              .Build());
}

}  // namespace Luma::MaterialGraph::Detail
