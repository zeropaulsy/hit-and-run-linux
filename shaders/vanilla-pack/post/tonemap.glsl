// Tone mapping: a filmic response curve, as films and modern games use, for softer highlights, richer midtones and
// deeper shadows. The game's picture has no brightness beyond white, so it is first widened into a range above white
// (the brightest parts become brighter than white), then run through the curve, which brings it back in gently.
// Mid grey stays where it was. Put it in [scene] (the HUD stays as it is), before grade.glsl and lut.glsl.

const int CURVE = 1;                    // 0 = ACES (punchy, as films), 1 = Hable / Uncharted 2 (gentle), 2 = Reinhard
const float EXPOSURE = 1.0;             // brightness before the curve (1 = mid grey unchanged)
const float AMOUNT = 1.0;               // 0 = off, 1 = full curve (ACES at full strength lifts bright walls a lot)
const float SATURATION = 0.5;           // the curve's colour boost: 0 = keep the original saturation, 1 = all of it

const float WIDEN = 0.6;                // how far the brightest parts go beyond white (fixed: the curves are set for it)

vec3 Aces(vec3 x)       { return x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14); }
vec3 Hable(vec3 x)      { return (x * (0.15 * x + 0.05) + 0.004) / (x * (0.15 * x + 0.5) + 0.06) - 0.0667; }
vec3 Reinhard(vec3 x)   { return x / (1.0 + x); }

vec3 Curve(vec3 x)
{
    if(CURVE == 1) return Hable(x);
    if(CURVE == 2) return Reinhard(x);
    return Aces(x);
}

void main()
{
    vec3 c = texture(uScene, vUV).rgb;
    vec3 linear = pow(c, vec3(2.2));

    // widen (inverse Reinhard-like: white becomes 1 / (1 - WIDEN)), then the curve with white mapped back to white;
    // each curve's built-in exposure keeps mid grey (0.18) at about 0.2
    vec3 wide = linear / max(1.0 - WIDEN * linear, 0.05);
    float white = 1.0 / (1.0 - WIDEN);
    float exposure = (CURVE == 1 ? 2.389 : CURVE == 2 ? 0.732 : 0.663) * EXPOSURE;
    vec3 mapped = Curve(wide * exposure) / Curve(vec3(white * exposure));

    // a curve applied to each channel pulls them apart (more saturation); the same brightness change applied to the
    // colour as a whole keeps its saturation: blend between the two
    const vec3 lumaWeights = vec3(0.2126, 0.7152, 0.0722);
    float lumaIn = dot(linear, lumaWeights);
    float lumaOut = dot(mapped, lumaWeights);
    vec3 keepSaturation = linear * (lumaOut / max(lumaIn, 1e-5));
    mapped = mix(keepSaturation, mapped, SATURATION);

    vec3 result = pow(clamp(mapped, 0.0, 1.0), vec3(1.0 / 2.2));
    fragColour = vec4(mix(c, result, AMOUNT), 1.0);
}
