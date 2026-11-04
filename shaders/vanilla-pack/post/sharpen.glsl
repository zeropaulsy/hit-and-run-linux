// Contrast-adaptive sharpening: sharpens more where the picture is flat, less at strong edges (no halos).

const float SHARPNESS = 0.5;            // 0 = off, 1 = strong

void main()
{
    vec2 px = 1.0 / uResolution;
    vec3 c = texture(uScene, vUV).rgb;
    vec3 n = texture(uScene, vUV + vec2(0.0, -px.y)).rgb;
    vec3 s = texture(uScene, vUV + vec2(0.0,  px.y)).rgb;
    vec3 w = texture(uScene, vUV + vec2(-px.x, 0.0)).rgb;
    vec3 e = texture(uScene, vUV + vec2( px.x, 0.0)).rgb;

    vec3 mn = min(c, min(min(n, s), min(w, e)));
    vec3 mx = max(c, max(max(n, s), max(w, e)));
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, 1e-4), 0.0, 1.0));
    vec3 weight = -amp * mix(0.125, 0.2, SHARPNESS);

    vec3 result = (c + (n + s + w + e) * weight) / (1.0 + 4.0 * weight);
    fragColour = vec4(clamp(result, 0.0, 1.0), 1.0);
}
