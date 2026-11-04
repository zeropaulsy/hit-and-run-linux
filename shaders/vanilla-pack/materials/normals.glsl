// Debug view: each surface coloured by its normal (eye space: red = right, green = up, blue = towards the camera).

vec4 ShadeMaterial()
{
    vec3 n = normalize(vNormal);
    return vec4(n * 0.5 + 0.5, StandardColour().a);
}
