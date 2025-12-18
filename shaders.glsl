static const char* SIMPLE_VERT = R"(#version 410 core
layout(location=0) in vec3 aPos;
uniform mat4 uModel, uView, uProj;
void main(){ gl_Position = uProj * uView * uModel * vec4(aPos,1.0); }
)";

static const char* SIMPLE_FRAG = R"(#version 410 core
out vec4 FragColor; uniform vec3 uColor;
void main(){ FragColor = vec4(uColor,1.0); }
)";

static const char* DEPTH_VERT = R"(#version 410 core
layout(location=0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uLightVP;
void main() {
    gl_Position = uLightVP * uModel * vec4(aPos, 1.0);
}
)";
static const char* DEPTH_FRAG = R"(#version 410 core
void main() { }
)";

static const char* FOG_VERT = R"(#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;

uniform mat4 uModel, uView, uProj;

out vec3 vPos;
out vec3 vNormal;

void main(){
    vec4 world = uModel * vec4(aPos, 1.0);
    vPos = world.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    gl_Position = uProj * uView * world;
}
)";

static const char* FOG_FRAG = R"(#version 410 core
out vec4 FragColor;
in vec3 vPos;
in vec3 vNormal;

uniform vec3 uColor;
uniform vec3 uViewPos;
uniform vec3 uLightPos;
uniform vec3 uLightColor;
uniform float uLightPower;

uniform sampler2D uShadowMap;
uniform mat4 uLightVP;
uniform float uShadowBias;

uniform float uFogDensity;        
uniform float uExtinction;       
uniform int uNumSamples;

// Ambient Dimmer
uniform float uFogAmbient;

uniform float uConeAngleInner;    
uniform float uConeAngleOuter;    
uniform vec3 uWindowCenter;       

// Research Toggles
uniform bool uDither;
uniform int uShowMapMode; // 0=Normal, 1=Transmission, 2=Depth

// heatmap(Blue -> Green -> Red) ---
vec3 jet(float t) {
    return clamp(vec3(1.5) - abs(4.0 * vec3(t) + vec3(-3, -2, -1)), 0.0, 1.0);
}

// Interleaved Gradient Noise
float ign(vec2 uv) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(uv, magic.xy)));
}

float attenuate(float dist) {
    return 1.0 / (1.0 + 0.1 * dist + 0.05 * dist * dist);
}

float shadowAtPoint(vec3 p) {
    vec4 lp = uLightVP * vec4(p,1.0);
    lp /= lp.w;
    vec2 uv = lp.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    float sampleDepth = lp.z * 0.5 + 0.5;
    float depthTex = texture(uShadowMap, uv).r;
    return (sampleDepth - uShadowBias > depthTex) ? 0.0 : 1.0;
}

vec3 march(vec3 rayStart, vec3 rayDir, float rayLen) {
    vec3 scatteredLight = vec3(0.0);
    float stepSize = rayLen / float(max(uNumSamples,1));
    float currentAttenuation = 1.0;
    vec3 coneAxis = normalize(uWindowCenter - uLightPos);

    // Dither Calculation
    float offset = 0.5;
    if (uDither) {
        offset = ign(gl_FragCoord.xy);
    }

    for (int i = 0; i < uNumSamples; ++i) {
        if (i >= uNumSamples) break; 
        
        float t = stepSize * (float(i) + offset);
        vec3 p = rayStart + rayDir * t;
        
        // 1. Ambient Fog (Global Dimmer)
        vec3 ambientLight = uLightColor * uFogAmbient;
        
        // 2. Direct Spotlight
        vec3 directLight = vec3(0.0);
        vec3 dirFromLight = normalize(p - uLightPos);
        float coneDot = dot(dirFromLight, coneAxis);
        float directLightFactor = smoothstep(uConeAngleOuter, uConeAngleInner, coneDot);

        if (directLightFactor > 0.0) {
            float vis = shadowAtPoint(p); 
            float lightDistance = length(uLightPos - p);
            float atten = attenuate(lightDistance);
            directLight = uLightColor * uLightPower * atten * directLightFactor * vis;
        }

        // 3. Accumulate
        vec3 totalStepLight = (ambientLight + directLight);
        float scattering = uFogDensity * stepSize;
        scatteredLight += totalStepLight * scattering * currentAttenuation;
        
        currentAttenuation *= exp(-scattering);
    }
    return scatteredLight;
}

void main() {
    // 1. Surface Lighting (Ambient term increases with global dimmer)
    vec3 ambient = (0.1 + uFogAmbient) * uColor; 
    vec3 norm = normalize(vNormal);
    vec3 lightDir = normalize(uLightPos - vPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor * uColor;
    vec3 surfaceColor = ambient + diffuse;

    // 2. Volumetric Pass
    vec3 rd = normalize(vPos - uViewPos);
    float rayLen = length(vPos - uViewPos); // 'd(x)'
    vec3 fog = march(uViewPos, rd, rayLen);

    // 3. Transmission 't(x)'
    float transmission = exp(-rayLen * uExtinction);

    // 4. Composite
    vec3 finalColor = surfaceColor * transmission + fog;
    
    // --- VISUALIZATION OUTPUT ---
    if (uShowMapMode == 1) {
        // [TRANSMISSION MAP]
        // Stretch contrast using smoothstep to make cubes clearly visible vs walls
        // Low threshold 0.4, High threshold 1.0
        float contrastT = smoothstep(0.4, 1.0, transmission); 
        FragColor = vec4(vec3(contrastT), 1.0);
    } 
    else if (uShowMapMode == 2) {
        // [DEPTH MAP]
        // Rainbow Heatmap Visualization
        float normalizedDepth = clamp(rayLen / 15.0, 0.0, 1.0);
        vec3 heatmap = jet(normalizedDepth);
        FragColor = vec4(heatmap, 1.0);
    } 
    else {
        // Normal Mode
        FragColor = vec4(finalColor, 1.0);
    }
}
)";
