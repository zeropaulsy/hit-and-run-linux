// Darkens the corners of the picture.

const float STRENGTH = 0.35;            // how dark the corners get
const float SIZE = 0.75;                // where the darkening starts (distance from the centre, 0-1.4)

void main()
{
    vec3 c = texture(uScene, vUV).rgb;
    vec2 d = (vUV - 0.5) * vec2(uResolution.x / uResolution.y, 1.0);
    float v = smoothstep(SIZE, SIZE + 0.6, length(d) * 1.4);
    fragColour = vec4(c * (1.0 - STRENGTH * v), 1.0);
}
