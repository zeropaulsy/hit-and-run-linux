// FXAA: smooths jagged edges (the game has no other anti-aliasing). Based on the idea of Timothy Lottes' FXAA:
// find edges by luma contrast, then blend along them.

const float EDGE_THRESHOLD = 0.125;     // minimum local contrast to count as an edge (lower = more edges)
const float EDGE_THRESHOLD_MIN = 0.0312;
const float SPAN_MAX = 8.0;             // longest blend, in pixels

float Luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main()
{
    vec2 px = 1.0 / uResolution;
    vec3 rgbM = texture(uScene, vUV).rgb;
    float lumaNW = Luma(texture(uScene, vUV + vec2(-1.0, -1.0) * px).rgb);
    float lumaNE = Luma(texture(uScene, vUV + vec2( 1.0, -1.0) * px).rgb);
    float lumaSW = Luma(texture(uScene, vUV + vec2(-1.0,  1.0) * px).rgb);
    float lumaSE = Luma(texture(uScene, vUV + vec2( 1.0,  1.0) * px).rgb);
    float lumaM = Luma(rgbM);
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    if(lumaMax - lumaMin < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD))
    {
        fragColour = vec4(rgbM, 1.0);
        return;
    }

    vec2 dir = vec2(-((lumaNW + lumaNE) - (lumaSW + lumaSE)), (lumaNW + lumaSW) - (lumaNE + lumaSE));
    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125, 1.0 / 128.0);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-SPAN_MAX), vec2(SPAN_MAX)) * px;

    vec3 rgbA = 0.5 * (texture(uScene, vUV + dir * (1.0 / 3.0 - 0.5)).rgb +
                       texture(uScene, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(uScene, vUV + dir * -0.5).rgb +
                                     texture(uScene, vUV + dir * 0.5).rgb);
    float lumaB = Luma(rgbB);
    fragColour = vec4((lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB, 1.0);
}
