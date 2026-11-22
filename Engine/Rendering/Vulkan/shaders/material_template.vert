// material_template.vert - vertex stage of every material pass. Included by
// generated sources after LumaMaterialVertex(), which computes World Position
// Offset, Custom UVs and Vertex Interpolators.
//
// Surface passes transform the engine vertex format (MeshVertex: position,
// normal, tangent + handedness, uv0); the post-process pass draws one
// fullscreen triangle (no vertex buffer).

#ifdef LUMA_PASS_POSTPROCESS

layout(location = 0) out vec2 vScreenUV;

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vScreenUV = uv;
    vec4 clip = vec4(uv * 2.0 - 1.0, 0.0, 1.0);

    P.screenUV = uv;
    P.pixelPos = uv * lumaFrame.viewport.xy;
    P.worldPos = LumaWorldFromDepth(uv, 1.0);
    P.worldPosNoOffset = P.worldPos;
    P.cameraVector = normalize(lumaFrame.camPos.xyz - P.worldPos);
    P.worldNormal = P.cameraVector;
    P.worldTangent = vec3(1.0, 0.0, 0.0);
    P.worldBitangent = vec3(0.0, 1.0, 0.0);
    P.pixelNormal = P.worldNormal;
    for (int i = 0; i < 8; ++i) P.texcoords[i] = uv;
    P.vertexColor = vec4(1.0);
    P.pixelDepth = 0.0;
    P.objectPos = vec3(0.0);
    P.objectRadius = 0.0;
    P.objectBounds = vec3(0.0);
    P.localToWorld = mat4(1.0);
    P.worldToLocal = mat4(1.0);

    MaterialVertex V = LumaDefaultVertex();
    LumaMaterialVertex(V);
    gl_Position = clip;
}

#else

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV;

layout(location = LUMA_VARYING_WORLD_POS) out vec3 vWorldPos;
layout(location = LUMA_VARYING_WORLD_POS_NO_OFFSET) out vec3 vWorldPosNoOffset;
layout(location = LUMA_VARYING_NORMAL) out vec3 vNormal;
layout(location = LUMA_VARYING_TANGENT) out vec4 vTangent;
layout(location = LUMA_VARYING_UV) out vec4 vUV[4];
layout(location = LUMA_VARYING_COLOR) out vec4 vColor;

void main() {
    const mat4 model = lumaPush.model;
    const mat3 normalMatrix =
        mat3(lumaPush.normalMatrix[0].xyz, lumaPush.normalMatrix[1].xyz, lumaPush.normalMatrix[2].xyz);
    const LumaObject object = lumaObjects[lumaPush.objectId];

    vec3 worldPos = (model * vec4(inPosition, 1.0)).xyz;
    // Normals use the inverse-transpose (correct under non-uniform scale);
    // tangents lie in the surface and transform with the model matrix.
    vec3 N = normalize(normalMatrix * inNormal);
    vec3 T = normalize(mat3(model) * inTangent.xyz);
    T = normalize(T - N * dot(N, T));

    P.worldPos = worldPos;  // World Position Offset sees the undisplaced position
    P.worldPosNoOffset = worldPos;
    P.worldNormal = N;
    P.worldTangent = T;
    P.worldBitangent = cross(N, T) * (inTangent.w < 0.0 ? -1.0 : 1.0);
    P.pixelNormal = N;
    P.texcoords[0] = inUV;
    for (int i = 1; i < 8; ++i) P.texcoords[i] = vec2(0.0);  // the engine mesh format has one UV set
    P.vertexColor = vec4(1.0);
    P.cameraVector = normalize(lumaFrame.camPos.xyz - worldPos);
    vec4 clip = lumaFrame.viewProj * vec4(worldPos, 1.0);
    P.screenUV = (clip.xy / clip.w) * 0.5 + 0.5;
    P.pixelPos = P.screenUV * lumaFrame.viewport.xy;
    P.pixelDepth = -(lumaFrame.view * vec4(worldPos, 1.0)).z;
    P.objectPos = model[3].xyz;
    P.objectRadius = object.boundsCenterRadius.w;
    P.objectBounds = object.boundsExtent.xyz;
    P.localToWorld = model;
    P.worldToLocal = object.worldToLocal;

    MaterialVertex V = LumaDefaultVertex();
    LumaMaterialVertex(V);

    vec3 displaced = worldPos + V.worldPositionOffset;
    vWorldPos = displaced;
    vWorldPosNoOffset = worldPos;
    vNormal = N;
    vTangent = vec4(T, inTangent.w);
    vec2 uv[8];
    for (int i = 0; i < 8; ++i) uv[i] = (V.customUVMask & (1u << i)) != 0u ? V.customUV[i] : P.texcoords[i];
    for (int i = 0; i < 4; ++i) vUV[i] = vec4(uv[2 * i], uv[2 * i + 1]);
    vColor = P.vertexColor;
    gl_Position = lumaFrame.viewProj * vec4(displaced, 1.0);
}

#endif
