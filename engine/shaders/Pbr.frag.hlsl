#include "Geometry.hlsli"
#include "Material.hlsli"

[[vk::binding(17, 0)]] Texture2D<float4> MetallicRoughnessTexture : register(t1);
[[vk::binding(18, 0)]] Texture2D<float4> NormalTexture : register(t2);
[[vk::binding(19, 0)]] Texture2D<float4> OcclusionTexture : register(t3);
[[vk::binding(20, 0)]] Texture2D<float4> EmissiveTexture : register(t4);
[[vk::binding(21, 0)]] Texture2D<float> ShadowDepthTexture : register(t5);
[[vk::binding(22, 0)]] Texture2D<float> AmbientOcclusionTexture : register(t6);
[[vk::binding(23, 0)]] Texture2D<float4> EnvironmentTexture : register(t7);
[[vk::binding(24, 0)]] Texture2D<float4> IrradianceTexture : register(t8);
[[vk::binding(25, 0)]] Texture2D<float4> BrdfTexture : register(t9);
[[vk::binding(33, 0)]] SamplerState LinearClampSampler : register(s1);
[[vk::binding(34, 0)]] SamplerComparisonState ShadowSampler : register(s2);
[[vk::binding(35, 0)]] SamplerState EnvironmentSampler : register(s3);

float DistributionGgx(float nDotH, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float denominator = nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(Pi * denominator * denominator, Epsilon);
}

float GeometrySchlickGgx(float nDotDirection, float roughness) {
    float remapped = roughness + 1.0;
    float k = remapped * remapped * 0.125;
    return nDotDirection / max(nDotDirection * (1.0 - k) + k, Epsilon);
}

float3 FresnelSchlick(float cosine, float3 f0) {
    float falloff = pow(saturate(1.0 - cosine), 5.0);
    return f0 + (1.0 - f0) * falloff;
}

float2 EnvironmentUv(float3 direction) {
    direction = SafeNormalize(RotateEnvironment(direction), float3(0.0, 1.0, 0.0));
    return float2(atan2(direction.z, direction.x) * (0.5 / Pi) + 0.5,
                  acos(clamp(direction.y, -1.0, 1.0)) / Pi);
}

float3 SampleEnvironment(float3 direction, float mipLevel) {
    return EnvironmentTexture.SampleLevel(EnvironmentSampler, EnvironmentUv(direction), mipLevel).rgb;
}

float3 SurfaceNormal(SurfaceVertex input, bool frontFacing) {
    float3 normal = SafeNormalize(input.Normal, float3(0.0, 1.0, 0.0));
    if (TextureMaskAlphaMode.z != 0u && !frontFacing)
        normal = -normal;
    if ((TextureMaskAlphaMode.x & 4u) == 0u)
        return normal;

    float3 tangent = input.Tangent.xyz - normal * dot(normal, input.Tangent.xyz);
    if (dot(tangent, tangent) < Epsilon) {
        float3 axis = abs(normal.z) < 0.99 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
        tangent = cross(axis, normal);
    }
    tangent = normalize(tangent);
    float3 bitangent = cross(normal, tangent) * input.Tangent.w;
    float3 tangentNormal = NormalTexture.Sample(LinearWrapSampler, input.TexCoord).xyz * 2.0 - 1.0;
    tangentNormal.xy *= MaterialFactors.z;
    return SafeNormalize(tangent * tangentNormal.x + bitangent * tangentNormal.y + normal * tangentNormal.z, normal);
}

float ShadowVisibility(float3 worldPosition, float nDotL) {
    if (FeatureParameters.x == 0u || TextureMaskAlphaMode.w == 0u)
        return 1.0;
    float4 lightPosition = mul(LightViewProjection, float4(worldPosition, 1.0));
    if (lightPosition.w <= Epsilon)
        return 1.0;
    float3 projected = lightPosition.xyz / lightPosition.w;
    float2 uv = float2(projected.x * 0.5 + 0.5, 0.5 - projected.y * 0.5);
    if (any(uv < 0.0) || any(uv > 1.0) || projected.z <= 0.0 || projected.z >= 1.0)
        return 1.0;

    static const float2 poissonDisk[16] = {
        float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
        float2(-0.09418410, -0.92938870), float2(0.34495938, 0.29387760),
        float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464),
        float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
        float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420),
        float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
        float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590),
        float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790)
    };
    float referenceDepth = projected.z - ShadowParameters.z - ShadowParameters.w * (1.0 - nDotL);
    uint sampleCount = clamp(FeatureParameters.z, 1u, 16u);
    float visibility = 0.0;
    for (uint sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex) {
        float2 offset = poissonDisk[sampleIndex] * ShadowParameters.xy * 2.0;
        visibility += ShadowDepthTexture.SampleCmpLevelZero(ShadowSampler, uv + offset, referenceDepth);
    }
    return visibility / float(sampleCount);
}

float3 EvaluateDirectLight(float3 normal, float3 viewDirection, float3 lightDirection,
                           float3 baseColor, float metallic, float roughness, float3 radiance) {
    float nDotL = saturate(dot(normal, lightDirection));
    float nDotV = max(saturate(dot(normal, viewDirection)), Epsilon);
    float3 halfVector = SafeNormalize(viewDirection + lightDirection, normal);
    float3 f0 = lerp(0.04.xxx, baseColor, metallic);
    float3 fresnel = FresnelSchlick(saturate(dot(halfVector, viewDirection)), f0);
    float distribution = DistributionGgx(saturate(dot(normal, halfVector)), roughness);
    float geometry = GeometrySchlickGgx(nDotV, roughness) * GeometrySchlickGgx(nDotL, roughness);
    float3 specular = distribution * geometry * fresnel / max(4.0 * nDotV * nDotL, Epsilon);
    return ((1.0 - fresnel) * (1.0 - metallic) * baseColor / Pi + specular) * radiance * nDotL;
}

float4 main(SurfaceVertex input, bool frontFacing : SV_IsFrontFace) : SV_Target0 {
    float4 baseColor = ReadBaseColor(input.TexCoord);
    ApplyAlphaMask(baseColor.a);
    float metallic = MaterialFactors.x;
    float roughness = MaterialFactors.y;
    if ((TextureMaskAlphaMode.x & 2u) != 0u) {
        float4 metalRough = MetallicRoughnessTexture.Sample(LinearWrapSampler, input.TexCoord);
        metallic *= metalRough.b;
        roughness *= metalRough.g;
    }
    metallic = saturate(metallic);
    roughness = clamp(roughness, 0.045, 1.0);
    float3 normal = SurfaceNormal(input, frontFacing);
    float3 viewDirection = SafeNormalize(CameraPositionExposure.xyz - input.WorldPosition, normal);
    float3 lightDirection = SafeNormalize(-LightDirectionIntensity.xyz, normal);
    float nDotL = saturate(dot(normal, lightDirection));
    float nDotV = max(saturate(dot(normal, viewDirection)), Epsilon);
    float3 f0 = lerp(0.04.xxx, baseColor.rgb, metallic);
    float3 radiance = LightColor.rgb * max(LightDirectionIntensity.w, 0.0);
    float3 color = EvaluateDirectLight(normal, viewDirection, lightDirection, baseColor.rgb, metallic, roughness, radiance)
                   * ShadowVisibility(input.WorldPosition, nDotL);
    for (uint lightIndex = 0u; lightIndex < min(AdditionalLightCount.x, 32u); ++lightIndex) {
        LightConstants light = AdditionalLights[lightIndex];
        float3 direction = SafeNormalize(-light.DirectionOuterCone.xyz, normal);
        float attenuation = 1.0;
        if (light.Parameters.y > 0.5) {
            float3 toLight = light.PositionRange.xyz - input.WorldPosition;
            float distanceSquared = dot(toLight, toLight);
            direction = SafeNormalize(toLight, normal);
            float relativeDistance = sqrt(distanceSquared) / max(light.PositionRange.w, Epsilon);
            float cutoff = saturate(1.0 - pow(relativeDistance, 4.0));
            attenuation = cutoff * cutoff / max(distanceSquared, 0.01);
            if (light.Parameters.y > 1.5) {
                float coneCosine = dot(-direction, light.DirectionOuterCone.xyz);
                float coneWidth = light.Parameters.x - light.DirectionOuterCone.w;
                // Equal cones represent a hard spotlight boundary.
                float cone = coneWidth > Epsilon
                    ? saturate((coneCosine - light.DirectionOuterCone.w) / coneWidth)
                    : (coneCosine >= light.DirectionOuterCone.w ? 1.0 : 0.0);
                attenuation *= cone * cone;
            }
        }
        color += EvaluateDirectLight(normal, viewDirection, direction, baseColor.rgb, metallic, roughness,
                                     light.ColorIntensity.rgb * light.ColorIntensity.w * attenuation);
    }

    float occlusion = 1.0;
    if ((TextureMaskAlphaMode.x & 8u) != 0u)
        occlusion = lerp(1.0, OcclusionTexture.Sample(LinearWrapSampler, input.TexCoord).r, saturate(MaterialFactors.w));
    if (FeatureParameters.y != 0u) {
        float2 screenUv = input.Position.xy * ViewportSizeInverse.zw;
        occlusion *= AmbientOcclusionTexture.SampleLevel(LinearClampSampler, screenUv, 0.0);
    }

    if (EnvironmentParameters.w > 0.5) {
        float3 ambientFresnel = f0 + (max((1.0 - roughness).xxx, f0) - f0) * pow(1.0 - nDotV, 5.0);
        float3 ambientDiffuseWeight = (1.0 - ambientFresnel) * (1.0 - metallic);
        float3 reflected = reflect(-viewDirection, normal);
        float3 irradiance = IrradianceTexture.SampleLevel(EnvironmentSampler, EnvironmentUv(normal), 0.0).rgb;
        float maxMipLevel = max(EnvironmentParameters.z - 1.0, 0.0);
        float mipLevel = roughness * maxMipLevel;
        float3 prefiltered = SampleEnvironment(reflected, mipLevel);
        float2 integratedBrdf = BrdfTexture.SampleLevel(LinearClampSampler, float2(nDotV, roughness), 0.0).rg;
        float3 specularBrdf = f0 * integratedBrdf.x + integratedBrdf.y;
        color += (ambientDiffuseWeight * irradiance * baseColor.rgb + prefiltered * specularBrdf)
               * occlusion * max(EnvironmentParameters.x, 0.0);
    }

    float3 emissive = EmissiveAlphaCutoff.rgb;
    if ((TextureMaskAlphaMode.x & 16u) != 0u)
        emissive *= EmissiveTexture.Sample(LinearWrapSampler, input.TexCoord).rgb;
    color += emissive;
    return float4(max(color, 0.0), TextureMaskAlphaMode.y == 2u ? baseColor.a : 1.0);
}
