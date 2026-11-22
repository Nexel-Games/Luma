// material_pixel.glsl - pixel-stage setup shared by the surface pass
// templates: reads material_template.vert's varyings into `P`, runs the
// generated LumaMaterialPixel() and applies the Masked clip and Pixel Depth
// Offset.

#ifndef LUMA_MATERIAL_PIXEL_GLSL
#define LUMA_MATERIAL_PIXEL_GLSL

layout(location = LUMA_VARYING_WORLD_POS) in vec3 vWorldPos;
layout(location = LUMA_VARYING_WORLD_POS_NO_OFFSET) in vec3 vWorldPosNoOffset;
layout(location = LUMA_VARYING_NORMAL) in vec3 vNormal;
layout(location = LUMA_VARYING_TANGENT) in vec4 vTangent;
layout(location = LUMA_VARYING_UV) in vec4 vUV[4];
layout(location = LUMA_VARYING_COLOR) in vec4 vColor;

void LumaSetupPixel() {
    // Back faces (two-sided materials) shade with the flipped normal.
    vec3 N = normalize(gl_FrontFacing ? vNormal : -vNormal);
    vec3 T = normalize(vTangent.xyz - N * dot(N, vTangent.xyz));
    const LumaObject object = lumaObjects[lumaPush.objectId];

    P.worldPos = vWorldPos;
    P.worldPosNoOffset = vWorldPosNoOffset;
    P.worldNormal = N;
    P.worldTangent = T;
    P.worldBitangent = cross(N, T) * (vTangent.w < 0.0 ? -1.0 : 1.0);
    P.pixelNormal = N;
    for (int i = 0; i < 4; ++i) {
        P.texcoords[2 * i] = vUV[i].xy;
        P.texcoords[2 * i + 1] = vUV[i].zw;
    }
    P.vertexColor = vColor;
    P.cameraVector = normalize(lumaFrame.camPos.xyz - vWorldPos);
    P.pixelPos = gl_FragCoord.xy;
    P.screenUV = gl_FragCoord.xy * lumaFrame.viewport.zw;
    P.pixelDepth = 1.0 / gl_FragCoord.w;  // perspective: clip w = view depth
    P.objectPos = lumaPush.model[3].xyz;
    P.objectRadius = object.boundsCenterRadius.w;
    P.objectBounds = object.boundsExtent.xyz;
    P.localToWorld = lumaPush.model;
    P.worldToLocal = object.worldToLocal;
}

// Runs the material; discards clipped Masked pixels.
MaterialPixel LumaShadePixel() {
    LumaSetupPixel();
    MaterialPixel m = LumaDefaultPixel();
    LumaMaterialPixel(m);
#ifdef LUMA_BLEND_MASKED
    if (m.opacityMask < lumaPush.opacityMaskClip) discard;
#endif
#ifdef LUMA_PIXEL_DEPTH_OFFSET
    // Pushes the pixel away from the camera along the view ray.
    vec3 pushed = P.worldPos - P.cameraVector * max(m.pixelDepthOffset, 0.0);
    vec4 clip = lumaFrame.viewProj * vec4(pushed, 1.0);
    gl_FragDepth = clip.z / clip.w;
#endif
    return m;
}

#endif  // LUMA_MATERIAL_PIXEL_GLSL
