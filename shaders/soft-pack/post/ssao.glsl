// Ambient occlusion (SSAO), pass 1 of 2: optimized implementation
#pragma shar_original                  
#pragma shar_scale 1                  

const int SAMPLES = 8;                  
const float RADIUS = 0.25;               
const float BIAS = 0.04;                
const float FADE_START = 40.0;          
const float FADE_END = 90.0;            

const int ROTATION[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);

vec4 Output(float ao, float distance)
{
    float t = clamp(log(max(distance, uNear) / uNear) / log(uFar / uNear), 0.0, 1.0);
    float v = floor(t * 65535.0 + 0.5);
    return vec4(ao, floor(v / 256.0) / 255.0, mod(v, 256.0) / 255.0, 1.0);
}

void main()
{
    float depth = texture(uDepth, vUV).r;
    if(uFar <= 0.0 || depth >= 1.0)
    {
        fragColour = vec4(1.0);         
        return;
    }

    vec3 p = ViewPosition(vUV);
    float distance = -p.z;

    vec2 pixel = 1.0 / uResolution;
    vec3 right = ViewPosition(vUV + vec2(pixel.x, 0.0)) - p;
    vec3 left  = p - ViewPosition(vUV - vec2(pixel.x, 0.0));
    vec3 up    = ViewPosition(vUV + vec2(0.0, pixel.y)) - p;
    vec3 down  = p - ViewPosition(vUV - vec2(0.0, pixel.y));
    
    vec3 dx = mix(left, right, step(abs(right.z), abs(left.z)));
    vec3 dy = mix(down, up, step(abs(up.z), abs(down.z)));
    vec3 n  = normalize(cross(dx, dy));
    
    n *= sign(dot(n, -p));

    // Fall back to clean bitwise 1D indexing layout compatible with engine parsing
    ivec2 cell = ivec2(gl_FragCoord.xy) & 3;
    float noise = (float(ROTATION[(cell.y << 2) + cell.x]) + 0.5) / 16.0;
    
    vec3 helper = mix(vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), step(abs(n.z), 0.9));
    vec3 t = normalize(cross(helper, n));
    vec3 b = cross(n, t);

    float occlusion = 0.0;
    for(int i = 0; i < SAMPLES; i++)
    {
        float f = (float(i) + 0.5) / float(SAMPLES);
        float angle = float(i) * 2.39996 + noise * 6.28318;
        
        float cosTheta = sqrt(1.0 - f);
        float sinTheta = sqrt(f);
        
        vec3 direction = t * (cos(angle) * sinTheta) + b * (sin(angle) * sinTheta) + n * cosTheta;
        float scale = mix(0.15, 1.0, f * f) * RADIUS;
        vec3 s = p + direction * scale;

        vec4 clip = uProjection * vec4(s, 1.0);
        vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
        
        float insideUV = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);
        vec3 q = ViewPosition(uv);

        vec3 toOccluder = q - p;
        float distance3D = length(toOccluder);
        
        float inFront = step(s.z + BIAS, q.z);
        float above = step(BIAS * distance3D, dot(toOccluder, n));
        float range = 1.0 - smoothstep(0.5 * RADIUS, RADIUS, distance3D);
        
        occlusion += (inFront * above * range) * insideUV;
    }

    float ao = 1.0 - occlusion / float(SAMPLES);
    ao = mix(ao, 1.0, smoothstep(FADE_START, FADE_END, distance));
    fragColour = Output(ao, distance);
}
