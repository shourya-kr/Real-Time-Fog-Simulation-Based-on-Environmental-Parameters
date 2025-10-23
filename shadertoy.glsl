//variable parameteres
const vec3 fcolnear = vec3(0.6, 0.8, 1.);  
const vec3 fcolfar = vec3(0.8, 0.9, 1.0);  
const float fden = 0.7;               
const float fhfall = 2.8;             
const float windspeed = 0.8;
const vec2 winddirn = normalize(vec2(1.0, 0.3));

const float kd = 1.0;
const float ka = 1.0;
const float id = 0.6;
const float ia = 0.2;
const vec3 lightdirn = normalize(vec3(1.0, 1.0, 0.0));

// 2D random function
float random(vec2 p) {
    return fract(sin(dot(p.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}

//3d noise
float noise(in vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = random(i.xy + i.z * vec2(5.0));
    float b = random(i.xy + vec2(1.0, 0.0) + i.z * vec2(5.0));
    float c = random(i.xy + vec2(0.0, 1.0) + i.z * vec2(5.0));
    float d = random(i.xy + vec2(1.0, 1.0) + i.z * vec2(5.0));

    return mix(mix(a,b,f.x), mix(c,d,f.x),f.y);
}

//fbm
float fbm(vec3 p) {
    float f = 0.0;
    mat3 m = mat3(0.00, 0.80, 0.60, -0.80, 0.36, -0.48, -0.60, -0.48, 0.64);
    f += 0.5000 * noise(p); p = m * p * 2.02;
    f += 0.2500 * noise(p); p = m * p * 2.03;
    f += 0.1250 * noise(p); p = m * p * 2.01;
    f += 0.0625 * noise(p);
    return f / 0.9375;
}

//fog calc
vec3 applyfog(vec3 col, vec3 hitpos, vec3 campos) {

    float dist = length(hitpos - campos);//ray dist
    vec3 windoffset = vec3(winddirn * windspeed * sin(iTime*0.5),iTime * 0.05);

    //heterogeneous fog
    float noisedensity = fbm(hitpos * 0.1 + windoffset);

    //fog height factor,decreaces with height
    float hfactor = exp(-hitpos.y * fhfall);

    //final fog amount based on Beer-Lambert law
    float fogamount = 1.0 - exp(-dist * fden * hfactor * noisedensity);
    fogamount = smoothstep(0.0, 1.0, fogamount); //prevent abrupt edges in the fog

    //actaul color of fog
    vec3 fcol = mix(fcolnear,fcolfar, smoothstep(0.0, 1.0, dist / 20.0));

    return mix(col, fcol, fogamount);
}

//terrain height generation
float terrainheight(float x,float z)
{
 return clamp(sin(x*2.)*sin(z)+sin(x)*sin(z*0.5),.0,2.);
}

//terrain normal(needed for lighting)
vec3 normalcalc(vec3 p)
{
 float dx = sin(p.z)*cos(p.x*2.)*2.+sin(p.z*0.5)*cos(p.x);
 float dz = sin(p.x)*cos(p.z)+sin(p.x)*cos(p.z*0.5)*0.5;
 return normalize(vec3(dx,1.,dz));
}

//simple diffuse and ambient lighting
vec3 lighting(vec3 p)
{
 float diffuse = max(dot(lightdirn,p),.0);
 float ambient = ka*ia;
 return vec3(ambient+kd*diffuse*id);
}

//raymarching to find intersection with terrain
float castray(vec3 campos,vec3 raydirn,float mint,float maxt,float stepsize)
{
 float prevheight = .0, prevY = .0;
 for (float t = mint;t<maxt;t += stepsize)
 {
  vec3 p = campos+raydirn*t;
  float h = terrainheight(p.x,p.z);
    if (p.y < h)
    return t - stepsize + stepsize * (prevheight - prevY)/(p.y - h + prevheight - prevY);
  prevheight = h;
  prevY = p.y;
 }
 return maxt;
}

//terrain shading with fog and texture 
vec3 terrain(vec3 p,vec3 r,vec3 campos)
{
 vec3 normal = normalcalc(p);
 vec3 light = lighting(normal);
 vec3 baseColor = texture(iChannel0,p.xz).rgb*.5;
 vec3 shaded = baseColor*light;
 return applyfog(shaded,p,campos);
}

void mainImage(out vec4 fragColor,in vec2 fragCoord)
{
 //normalizing uv coordinates
 vec2 uv = (fragCoord.xy/iResolution.xy)*2.-1.;
 uv.x *= iResolution.x/iResolution.y;

 //camera movement
 float camZ = iTime*1.;  //moves forward in z
 float camX = 1.0 + sin(iTime)*.3; //oscillations in x
 float camY = terrainheight(camX,camZ) + .3; //followws terrain height in y
 vec3 campos = vec3(camX,camY,camZ);

 //camera's target points
 float lookahead = 1.;
 float targetX = 1. + sin((iTime + lookahead)*.5) * 5.;
 float targetZ = camZ + lookahead;
 float targetY = terrainheight(targetX,targetZ) + 0.2;
 vec3 target = vec3(targetX,targetY,targetZ);

 //camera's orientation
 vec3 forward = normalize(target - campos);
 vec3 right = normalize(cross(vec3(0,1,0),forward));
 vec3 up = cross(forward,right);

 //ray dirn in space
 vec3 raydirn = normalize(forward + uv.x*right + uv.y*up);

 //find intersection with terrain using raymarching
 float t = castray(campos,raydirn,.1,20.,.2);
 vec3 pos = campos + raydirn*t;

 //computing color based on terrain intersection/sky
 vec3 color;
 if (t < 20.)
 {
  color = terrain(pos, raydirn, campos);
 }else {
        color = mix(fcolnear,fcolfar, smoothstep(0.0, 1.0, uv.y));
    }
    fragColor = vec4(color,1.);
}
