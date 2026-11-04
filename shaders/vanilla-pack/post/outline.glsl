// Outlines: dark lines round objects and along sharp corners, from the depth buffer alone (no change to the materials).
// Put it after the tone mapping. With the Vulkan renderer and anti-aliasing on, the lines are anti-aliased with it (each
// sample of a pixel decides for itself, the pixel gets the share that are on a line); with OpenGL or anti-aliasing off
// they are one pixel hard, and fxaa.glsl after this pass smooths them.
// Two kinds of line:
// - silhouettes: 1/distance changes linearly across a flat surface, so a pair of opposite neighbours that doesn't
//   average to this pixel's value is a step in depth. Only the nearer side is drawn: the line stays on the object.
// - creases: the surface's direction differs either side of this pixel (corners of buildings, kerbs).
#pragma shar_depth_samples

const vec3 LINE_COLOUR = vec3(0.05, 0.04, 0.06);
const float LINE_OPACITY = 0.9;
const float LINE_WIDTH = 1.0;           // in pixels at 1080p, whole pixels after following the window's height
const float SILHOUETTE_LOW = 0.02;      // depth step, as a share of the distance: no line below this ..
const float SILHOUETTE_HIGH = 0.06;     // .. full line above it
const float HEIGHT_LOW = 0.08;          // height above the surface behind it, in game units: no line below this ..
const float HEIGHT_HIGH = 0.2;          // .. full line above it (seams in the ground look like big steps at low angles)
const float HEIGHT_RANGE = 12.0;        // both grow by themselves again every this far from the camera
const float CREASE_LOW = 0.55;          // cosine of the angle across a corner: no line above this (57 degrees) ..
const float CREASE_HIGH = 0.3;          // .. full line below it (73 degrees); CREASE_LOW = 2.0 turns creases off
const float FADE_START = 60.0;          // lines fade out with distance, in game units (about metres) ..
const float FADE_END = 220.0;           // .. and are gone here: far away they would only be noise

int depthSample = 0;                    // the sample being looked at: neighbours are compared sample for sample

float RawDepth(ivec2 p)
{
#ifdef SHAR_DEPTH_SAMPLES
    return DepthSample(p, depthSample);
#else
    return texelFetch(uDepth, clamp(p, ivec2(0), ivec2(uResolution) - 1), 0).r;
#endif
}

float Distance(ivec2 p)
{
    float z = RawDepth(p) * 2.0 - 1.0;
    return max(2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear)), 0.001);
}

vec3 Position(ivec2 p)
{
    vec2 uv = (vec2(p) + 0.5) / uResolution;
    vec4 q = uInverseProjection * vec4(uv * 2.0 - 1.0, RawDepth(p) * 2.0 - 1.0, 1.0);
    return q.xyz / q.w;
}

// the surface's direction at p, taken on the side with the smaller depth step (as ssao.glsl)
vec3 SurfaceNormal(ivec2 p)
{
    vec3 c = Position(p);
    vec3 right = Position(p + ivec2(1, 0)) - c;
    vec3 left  = c - Position(p - ivec2(1, 0));
    vec3 up    = Position(p + ivec2(0, 1)) - c;
    vec3 down  = c - Position(p - ivec2(0, 1));
    vec3 dx = abs(right.z) < abs(left.z) ? right : left;
    vec3 dy = abs(up.z) < abs(down.z) ? up : down;
    return normalize(cross(dx, dy));
}

// 0 .. 1: p sits in front of what lies on either side of it along step
float Silhouette(ivec2 p, ivec2 step, float centre)
{
    float a = 1.0 / Distance(p + step), b = 1.0 / Distance(p - step);
    float depthStep = (2.0 * centre - a - b) / centre;
    if(depthStep <= SILHOUETTE_LOW)
        return 0.0;
    // how far p stands off the plane of the surface behind it
    ivec2 behind = a < b ? p + step : p - step;
    float height = abs(dot(Position(p) - Position(behind), SurfaceNormal(behind)));
    float scale = 1.0 + 1.0 / (centre * HEIGHT_RANGE);
    return smoothstep(SILHOUETTE_LOW, SILHOUETTE_HIGH, depthStep) * smoothstep(HEIGHT_LOW * scale, HEIGHT_HIGH * scale, height);
}

float Crease(ivec2 p, ivec2 step)
{
    // a step in depth is the silhouette's business: the two sides are different objects
    float a = Distance(p + step), b = Distance(p - step);
    if(abs(a - b) > 0.1 * min(a, b))
        return 0.0;
    float angle = abs(dot(SurfaceNormal(p + step), SurfaceNormal(p - step)));
    return smoothstep(CREASE_LOW, CREASE_HIGH, angle);
}

// 0 .. 1: how much line this sample of p has
float Line(ivec2 p, int width)
{
    if(RawDepth(p) >= 1.0)
        return 0.0;
    float distance = Distance(p);
    float fade = 1.0 - smoothstep(FADE_START, FADE_END, distance);
    if(fade <= 0.0)
        return 0.0;
    float centre = 1.0 / distance;
    float line = max(max(Silhouette(p, ivec2(width, 0), centre), Silhouette(p, ivec2(0, width), centre)),
                     max(Silhouette(p, ivec2(width, width), centre), Silhouette(p, ivec2(width, -width), centre)));
    if(CREASE_LOW <= 1.0 && line < 1.0)
        line = max(line, max(Crease(p, ivec2(width, 0)), Crease(p, ivec2(0, width))));
    return line * fade;
}

#ifdef SHAR_DEPTH_SAMPLES
// is there anything here for the samples to look at? From sample 0 of a one pixel wider neighbourhood and this pixel's
// own samples: most of the screen stops here, at a fraction of the cost of looking at every sample
bool NeedsSamples(ivec2 p, int width)
{
    float centre = 1.0 / Distance(p);
    for(int s = 1; s < SHAR_DEPTH_SAMPLES; s++)
    {
        depthSample = s;
        if(abs(1.0 / Distance(p) - centre) > 0.5 * SILHOUETTE_LOW * centre)
            return true;
    }
    depthSample = 0;
    ivec2 wide[4] = ivec2[4](ivec2(width, 0), ivec2(0, width), ivec2(width, width), ivec2(width, -width));
    for(int i = 0; i < 4; i++)
    {
        float a = 1.0 / Distance(p + wide[i]), b = 1.0 / Distance(p - wide[i]);
        if(abs(2.0 * centre - a - b) > 0.5 * SILHOUETTE_LOW * centre)
            return true;
    }
    return CREASE_LOW <= 1.0 && max(Crease(p, wide[0]), Crease(p, wide[1])) > 0.0;
}
#endif

void main()
{
    vec3 colour = texture(uScene, vUV).rgb;
    fragColour = vec4(colour, 1.0);
    if(uFar <= 0.0)
        return;

    ivec2 p = ivec2(vUV * uResolution);     // not gl_FragCoord: the last pass's counts from the top
    int width = max(int(LINE_WIDTH * uResolution.y / 1080.0 + 0.5), 1);
    float line = 0.0;
#ifdef SHAR_DEPTH_SAMPLES
    if(NeedsSamples(p, width + 1))
    {
        for(int s = 0; s < SHAR_DEPTH_SAMPLES; s++)
        {
            depthSample = s;
            line += Line(p, width);
        }
        line /= float(SHAR_DEPTH_SAMPLES);
    }
#else
    line = Line(p, width);
#endif
    fragColour.rgb = mix(colour, LINE_COLOUR, line * LINE_OPACITY);
}
