// An old TV: curved screen, scanlines and a shadow mask. Put it in [final] so the HUD is on the TV too.

const float CURVATURE = 0.08;           // 0 = flat
const float SCANLINES = 0.35;           // scanline darkness
const float PIXELS_PER_LINE = 3.0;      // scanline spacing in screen pixels (whole numbers avoid moire)
const float MASK = 0.15;                // strength of the RGB shadow mask

void main()
{
    vec2 uv = vUV * 2.0 - 1.0;
    uv *= 1.0 + CURVATURE * dot(uv.yx, uv.yx);
    uv = uv * 0.5 + 0.5;
    if(uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    {
        fragColour = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 c = texture(uScene, uv).rgb;
    float line = 0.5 + 0.5 * cos(uv.y * uResolution.y / PIXELS_PER_LINE * 6.2831853);
    c *= 1.0 - SCANLINES * line;
    int column = int(mod(gl_FragCoord.x, 3.0));
    vec3 mask = vec3(column == 0 ? 1.0 : 1.0 - MASK, column == 1 ? 1.0 : 1.0 - MASK, column == 2 ? 1.0 : 1.0 - MASK);
    c *= mask * (1.0 + MASK * 0.7);
    fragColour = vec4(clamp(c, 0.0, 1.0), 1.0);
}
