// Ambient occlusion (SSAO), pass 2a of 2: Horizontal Bilateral Blur

const float EDGE_SHARPNESS = 15.0; 
const int KERNEL_RADIUS = 4;       
const float BLUR_STRIDE = 2.0;    

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
    // Use standard texture lookup for the center pixel to ensure scale consistency
    vec4 centerTexel = texture(uScene, vUV);
    
    float logScale = log(uFar / uNear);
    float centre = log(max(LinearDepth(vUV), uNear) / uNear) / logScale;

    float centerGauss = GetGaussWeight(0);
    float sum = centerTexel.r * centerGauss;
    float weights = centerGauss;

    // Calculate the size of a single pixel in 0.0 -> 1.0 UV space
    vec2 texelSize = 1.0 / vec2(textureSize(uScene, 0));

    for(int i = -KERNEL_RADIUS; i <= KERNEL_RADIUS; i++)
    {
        if(i == 0) continue; 
        
        // Calculate offset in UV space instead of integers
        float offset = float(i) * BLUR_STRIDE;
        vec2 sampleUV = vUV + vec2(offset * texelSize.x, 0.0);
        
        // Clamp UV coordinates safely to screen boundaries
        sampleUV = clamp(sampleUV, 0.0, 1.0);
        vec4 texel = texture(uScene, sampleUV);
        
        float diff = abs(UnpackedLogDepth(texel) - centre);
        float depthSimilarity = clamp(1.0 - (diff * EDGE_SHARPNESS), 0.0, 1.0);
        
        float w = GetGaussWeight(abs(i)) * depthSimilarity;
        
        sum += texel.r * w;
        weights += w;
    }

    float ao = sum / max(weights, 0.001);
    fragColour = vec4(ao, centerTexel.g, centerTexel.b, 1.0);
}
