// material_postprocess.frag - a post-process material as a fullscreen pass:
// Emissive Color is the pass output. Scene Texture reads the scene buffers
// (Post Process Input 0 = the post chain's current colour).

layout(location = 0) in vec2 vScreenUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = vScreenUV;
    float deviceDepth = texture(lumaSceneTextures[kLumaSceneDepth], uv).r;
    P.screenUV = uv;
    P.pixelPos = gl_FragCoord.xy;
    P.worldPos = LumaWorldFromDepth(uv, deviceDepth);
    P.worldPosNoOffset = P.worldPos;
    P.cameraVector = normalize(lumaFrame.camPos.xyz - P.worldPos);
    P.worldNormal = normalize(texture(lumaSceneTextures[kLumaGBufferB], uv).rgb * 2.0 - 1.0);
    P.worldTangent = vec3(1.0, 0.0, 0.0);
    P.worldBitangent = vec3(0.0, 1.0, 0.0);
    P.pixelNormal = P.worldNormal;
    for (int i = 0; i < 8; ++i) P.texcoords[i] = uv;
    P.vertexColor = vec4(1.0);
    P.pixelDepth = LumaLinearDepth(uv, deviceDepth);
    P.objectPos = vec3(0.0);
    P.objectRadius = 0.0;
    P.objectBounds = vec3(0.0);
    P.localToWorld = mat4(1.0);
    P.worldToLocal = mat4(1.0);

    MaterialPixel m = LumaDefaultPixel();
    LumaMaterialPixel(m);
    outColor = vec4(m.emissive, 1.0);
}
