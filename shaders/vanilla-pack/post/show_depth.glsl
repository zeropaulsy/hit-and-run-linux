// Shows the depth buffer: near = black, far = white. An example of uDepth and LinearDepth (scene stage only;
// in the final stage the depth buffer also holds whatever the HUD and menus drew).

const float RANGE = 100.0;              // distance (game units, ~metres) shown as white

void main()
{
    float distance = LinearDepth(vUV);
    fragColour = vec4(vec3(clamp(distance / RANGE, 0.0, 1.0)), 1.0);
}
