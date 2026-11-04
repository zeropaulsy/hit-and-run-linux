// Colour grading: white balance, exposure, contrast, saturation and vibrance, lift / gamma / gain (shadows, midtones,
// highlights) and split toning. Every setting at its neutral value leaves the picture unchanged. Put it in [scene]
// (the HUD stays as it is), after tonemap.glsl if you use both.

const float EXPOSURE = 0.0;                     // in stops: +1 = twice as bright, -1 = half
const float TEMPERATURE = 0.05;                 // white balance: > 0 warmer (yellow), < 0 cooler (blue)
const float TINT = 0.0;                         // white balance: > 0 magenta, < 0 green
const float CONTRAST = 1.05;                    // 1 = unchanged
const float SATURATION = 1.1;                   // 1 = unchanged, 0 = black and white
const float VIBRANCE = 0.1;                     // saturates the dull colours more than the vivid ones (0 = off)

const vec3 LIFT = vec3(0.0, 0.0, 0.0);          // added to the shadows (per colour channel, around 0)
const vec3 GAMMA = vec3(1.0, 1.0, 1.0);         // midtones (> 1 brighter)
const vec3 GAIN = vec3(1.0, 1.0, 1.0);          // highlights (multiplier)

const vec3 SHADOW_TINT = vec3(0.0, 0.05, 0.1);  // split toning: colour pushed into the shadows ...
const vec3 HIGHLIGHT_TINT = vec3(0.1, 0.05, 0.0);   // ... and into the highlights
const float SPLIT_AMOUNT = 0.0;                 // 0 = no split toning, 1 = full
const float SPLIT_BALANCE = 0.5;                // where shadows end and highlights begin (brightness)

float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main()
{
    vec3 c = texture(uScene, vUV).rgb;

    // white balance and exposure, on linear light
    vec3 linear = pow(c, vec3(2.2));
    linear *= vec3(1.0 + TEMPERATURE * 0.3 + TINT * 0.1, 1.0 - TINT * 0.2, 1.0 - TEMPERATURE * 0.3 + TINT * 0.1);
    linear *= exp2(EXPOSURE);
    c = pow(max(linear, 0.0), vec3(1.0 / 2.2));

    // contrast round mid grey
    c = (c - 0.5) * CONTRAST + 0.5;

    // saturation and vibrance
    float grey = Luma(c);
    float chroma = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
    c = mix(vec3(grey), c, SATURATION * (1.0 + VIBRANCE * (1.0 - chroma)));

    // lift / gamma / gain
    c = clamp(c, 0.0, 1.0);
    c = GAIN * (c + LIFT * (1.0 - c));
    c = pow(max(c, 0.0), 1.0 / max(GAMMA, vec3(0.01)));

    // split toning
    float l = Luma(c);
    vec3 tint = mix(SHADOW_TINT, HIGHLIGHT_TINT, smoothstep(SPLIT_BALANCE - 0.3, SPLIT_BALANCE + 0.3, l));
    c += tint * SPLIT_AMOUNT * (1.0 - abs(l * 2.0 - 1.0));

    fragColour = vec4(clamp(c, 0.0, 1.0), 1.0);
}
