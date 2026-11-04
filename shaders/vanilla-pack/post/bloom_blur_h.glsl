// Bloom, pass 2 of 4: blurs the bright parts (horizontal half of a Gaussian blur).

const float RADIUS = 2.0;               // blur width, in pixels per tap

void main()
{
    const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec2 offset = vec2(1.0, 0.0) * RADIUS / uResolution;
    vec3 sum = texture(uScene, vUV).rgb * weights[0];
    for(int i = 1; i < 5; i++)
    {
        sum += texture(uScene, vUV + offset * float(i)).rgb * weights[i];
        sum += texture(uScene, vUV - offset * float(i)).rgb * weights[i];
    }
    fragColour = vec4(sum, 1.0);
}
