// Ambient occlusion (SSAO), pass 2b of 2: Vertical Bilateral Blur & Composition

const float STRENGTH = 1.0;             
const float EDGE_SHARPNESS = 15.0;      
const bool SHOW_AO = false;             
const int KERNEL_RADIUS = 4;
const float BLUR_STRIDE = 1.0;    

float UnpackedLogDepth(vec4 texel)
{
    float g = floor(texel.g * 255.0 + 0.5);
    float b = floor(texel.b * 255.0 + 0.5);
    return (g * 256.0 + b) / 65535.0;
}

float GetGaussWeight(int idx)
{
    if (idx == 0) return 0.22;
    if (idx == 1) return 0.18;
    if (idx == 2) return 0.12;
    if (idx == 3) return 0.07;
    return 0.02;
}

void main()
{
    vec3 colour = texture(uOriginal, vUV).rgb;
    if(uFar <= 0.0)
    {
        fragColour = vec4(SHOW_AO ? vec3(1.0) : colour, 1.0);
        return;
    }

    // Read center from previous pass using upsampled UVs
    vec4 centerTexel = texture(uScene, vUV);
    
    float logScale = log(uFar / uNear);
    float centre = log(max(LinearDepth(vUV), uNear) / uNear) / logScale;

    float centerGauss = GetGaussWeight(0);
    float sum = centerTexel.r * centerGauss;
    float weights = centerGauss;

    vec2 texelSize = 1.0 / vec2(textureSize(uScene, 0));

    for(int i = -KERNEL_RADIUS; i <= KERNEL_RADIUS; i++)
    {
        if(i == 0) continue;
        
        float offset = float(i) * BLUR_STRIDE;
        vec2 sampleUV = vUV + vec2(0.0, offset * texelSize.y);
        
        sampleUV = clamp(sampleUV, 0.0, 1.0);
        vec4 texel = texture(uScene, sampleUV);
        
        float diff = abs(UnpackedLogDepth(texel) - centre);
        float depthSimilarity = clamp(1.0 - (diff * EDGE_SHARPNESS), 0.0, 1.0);
        
        float w = GetGaussWeight(abs(i)) * depthSimilarity;
        
        sum += texel.r * w;
        weights += w;
    }

    float ao = sum / max(weights, 0.001);
    ao = mix(1.0, clamp(1.0 - (1.0 - ao) * STRENGTH, 0.0, 1.0), step(0.001, weights));

    if (SHOW_AO) {
        fragColour = vec4(vec3(ao), 1.0);
    } else {
        fragColour = vec4(colour * ao, 1.0);
    }
}
