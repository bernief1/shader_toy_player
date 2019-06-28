
/* =============================================================================
2019/06/27 06:39PM
Based on https://www.shadertoy.com/view/XdcfRr (fjavifabre)
Which was based on https://www.shadertoy.com/view/4sfGDB (Zavie)
Original code http://www.kevinbeason.com/smallpt/

Note: this project is NOT intended to look great, it is intended to resolve
discrepancies with the ground truth path tracer (currently I'm using Mitsuba).
At this point I've resolved discrepancies with diffuse material only, but not
specular or refractive.

NOTABLE FEATURES:
- multi-bounce global illumination using path tracing
- direct light sampling, MIS
- textured area lights
- lightmapping
- lightmap texel visibility

CONTROLS:
- mouse controls camera (hold 'T' for translation)
- 'WASD' camera fly controls
- 'E' toggle direct light sampling (next event estimation / importance sampling for lights)
- 'H' toggle Halton sampling for first bounce (and first direct light sampling)
- 'L' toggle lightmaps
- 'V' toggle lightmap visualization
- 'F' toggle lightmap filtering
- 'I' toggle indirect illumination only
- 'O' toggle direct illumination only
- 'C' colorize lightmap sphere faces
- 'Z' toggle variance display (non-lightmap only)

TODO:
- support textured quad lights
- add GGX and metallic materials (low priority)
- firefly suppression
- support non-convex objects
============================================================================== */

#define SLIDER_VAR_CONST(type,name,init,rangemin,rangemax) const type name = type(init)
#ifndef SLIDER_VAR
#define SLIDER_VAR SLIDER_VAR_CONST
#define SLIDER_CHANGED false
#endif

// this defines the order of the passes
//$PASS:control_logic.glsl
//$PASS:lightmap_texelIDs.glsl
//$PASS:lightmap_texel_visibility.glsl
//$PASS:lightmap_texel_visibility_dilation.glsl
//$PASS:lightmap.glsl
//$PASS:scene.glsl
//$PASS:variance.glsl
//$PASS:final.glsl

//$BUFFER:noise, path=textures\shadertoy\RGBA_NOISE_MEDIUM.png
//$BUFFER:passionflower, path=textures\passionflower.jpg, format=R8G8B8A8_UNORM_SRGB, mips=-1

// ================================================
// === BEGIN CAMERA CODE ==========================
// ================================================

#define PI 3.14159265359

mat3 CreateCameraBasis(vec3 camRot)
{
	float sinYaw = sin(camRot.x);
	float cosYaw = cos(camRot.x);
	float sinPitch = sin(camRot.y);
	float cosPitch = cos(camRot.y);
	mat3 basis;
	basis[0] = vec3(+cosYaw, 0, +sinYaw);
	basis[1] = vec3(-sinYaw*sinPitch, cosPitch, +cosYaw*sinPitch);
	basis[2] = vec3(+sinYaw*cosPitch, sinPitch, -cosYaw*cosPitch);
	return basis;
}

vec3 CreateScreenRay(vec2 screenPos, float screenAspect)
{
	float VFOV = 80.0;
	float tanVFOV = tan(VFOV*0.5*PI/180.0); // could precompute this
	return normalize(vec3(screenPos*vec2(screenAspect, 1)*tanVFOV, 1));
}

#define CAMERA_HANDLER(camera, keyboardSampler, controlSampler, frame, frameReset, translationScale) \
	CameraHandler(camera, fragColor, fragCoord, keyboardSampler, controlSampler, iMouse, frames, iFrame, frameReset, iFrameRate, translationScale)

// this is my current attempt at a "generic" camera controller for shadertoy ..
bool CameraHandler(
	out mat4 camera,
	out vec4 fragColor,
	vec2 fragCoord,
	sampler2D keyboardSampler_not_used,
	sampler2D controlSampler,
	vec4 mouse, // <-- iMouse
	out vec2 frames,
	int frameIndex, // <-- iFrame
	bvec2 frameReset,
	float frameRate, // <-- iFrameRate
	float translationScale)
{
#define MOUSE_CONTROL_NONE      0
#define MOUSE_CONTROL_ROTATE    1
#define MOUSE_CONTROL_TRANSLATE 2
#define MOUSE_CONTROL frameControl.z // i need xyz for translation mouse control, so mode needs to be stored elsewhere

	float camSpeedRotate = 0.003;
	float camSpeedTranslate = 0.25*translationScale;
	float camSpeedTranslateWASD = 60.0*translationScale/frameRate;

	ivec2 controlSamplerRes = textureSize(controlSampler, 0);
	ivec2 frameControlCoord = controlSamplerRes - ivec2(1,1);
	ivec2 mouseControlCoord = controlSamplerRes - ivec2(2,1);
	ivec2 camPosCoord       = controlSamplerRes - ivec2(3,1);
	ivec2 camRotCoord       = controlSamplerRes - ivec2(4,1);

	vec3 frameControl = texelFetch(controlSampler, frameControlCoord, 0).xyz;
	vec3 mouseControl = texelFetch(controlSampler, mouseControlCoord, 0).xyz;
	vec3 camPos       = texelFetch(controlSampler, camPosCoord, 0).xyz;
	vec3 camRot       = texelFetch(controlSampler, camRotCoord, 0).xyz; // yaw,pitch,unused
	mat3 camBasis     = CreateCameraBasis(camRot);

	bool mouseDownPrev = MOUSE_CONTROL != float(MOUSE_CONTROL_NONE);
	bool mouseDownCurr = mouse.z > 0.0;
	if (mouseDownCurr) {
		if (!mouseDownPrev) {
			if (IS_KEY_DOWN(KEY_T)) {
				mouseControl = camBasis*camPos;
				MOUSE_CONTROL = float(MOUSE_CONTROL_TRANSLATE);
			} else {
				mouseControl.xy = camRot.xy;
				mouseControl.z = 0.0;
				MOUSE_CONTROL = float(MOUSE_CONTROL_ROTATE);
			}
		}
		if (MOUSE_CONTROL == float(MOUSE_CONTROL_TRANSLATE)) {
			vec3 delta = vec3((mouse.zw - mouse.xy)*camSpeedTranslate, 0);
			camPos = camBasis*(mouseControl + delta);
		} else if (MOUSE_CONTROL == float(MOUSE_CONTROL_ROTATE))
			camRot.xy = mod(mouseControl.xy + (mouse.zw - mouse.xy)*camSpeedRotate, 2.0*PI);
		frameReset.x = true;
	} else
		MOUSE_CONTROL = float(MOUSE_CONTROL_NONE);

	// WASD controls
	if (IS_KEY_DOWN(KEY_A)) { camPos += camBasis*vec3(-1,0,0)*camSpeedTranslateWASD; frameReset.x = true; }
	if (IS_KEY_DOWN(KEY_D)) { camPos += camBasis*vec3(+1,0,0)*camSpeedTranslateWASD; frameReset.x = true; }
	if (IS_KEY_DOWN(KEY_S)) { camPos += camBasis*vec3(0,0,-1)*camSpeedTranslateWASD; frameReset.x = true; }
	if (IS_KEY_DOWN(KEY_W)) { camPos += camBasis*vec3(0,0,+1)*camSpeedTranslateWASD; frameReset.x = true; }

	camera = mat4(camBasis);
	camera[3].xyz = camPos;
	
	frames = max(vec2(0), vec2(frameIndex) - frameControl.xy);
	if (frameReset.x) frames.x = 0.0; // resets scene only
	if (frameReset.y) frames.xy = vec2(0); // resets both scene and lightmap accum
	if (frames.x == 0.0) frameControl.x = float(frameIndex);
	if (frames.y == 0.0) frameControl.y = float(frameIndex);
	ivec2 coord = ivec2(fragCoord.xy);
	if      (coord == frameControlCoord) fragColor = vec4(frameControl, 1);
	else if (coord == mouseControlCoord) fragColor = vec4(mouseControl, 1);
	else if (coord == camPosCoord) fragColor = vec4(camPos, 1);
	else if (coord == camRotCoord) fragColor = vec4(camRot, 1);
	else return true;

	return false;
}

// ================================================
// === END CAMERA CODE ============================
// ================================================

#define NUM_PRIMARY_RAY_SAMPLES    16 // set these as high as you can for best quality
#define NUM_DIRECT_LIGHT_SAMPLES   16
#define NUM_INDIRECT_LIGHT_SAMPLES 2 // better to have this be > 1, when using MIS ..
#define MIN_DEPTH_INDIRECT         2 // normally 2 (or 3 for the third bounce, etc.)
#define MAX_DEPTH                  2 // 1=direct lighting only, normally 2 or 3
#define LAST_FRAME                 32768 // stop early if you want to ..

#define OBJ_ID_NONE         -1
#define OBJ_ID_LEFT_WALL     0
#define OBJ_ID_RIGHT_WALL    1
#define OBJ_ID_FLOOR         2
#define OBJ_ID_CEILING       3
#define OBJ_ID_FRONT_WALL    4
#define OBJ_ID_BACK_WALL     5
#define OBJ_ID_LEFT_SPHERE   6
#define OBJ_ID_RIGHT_SPHERE  7
#define OBJ_ID_CEILING_LIGHT 8
#define NUM_OBJECTS          9

#define LIGHT_IS_QUAD (1)
#if __VERSION__ >= 400 && defined(_GPU_SHADER_5_) // not my laptop, and certainly not WebGL :/
#define LIGHT_TEXTURED (1 && LIGHT_IS_QUAD) // currently requires bindless texture extension so that i can pass samplers around as uint64_t
#else
#define LIGHT_TEXTURED (0)
#endif

#define LIGHTMAP (1)
#define LIGHTMAP_PADDING 1
#define LIGHTMAP_DOWNSAMPLE 2 // <-- change to 1 if running full resolution
#define LIGHTMAP_QUAD_INSET 0.0625
#define LIGHTMAP_QUAD_RES_U (164/LIGHTMAP_DOWNSAMPLE)
#define LIGHTMAP_QUAD_RES_V (96/LIGHTMAP_DOWNSAMPLE)
#define LIGHTMAP_SPHERE_ATLAS (1 && LIGHTMAP)
#define LIGHTMAP_SPHERE_FACE_RES_1 (64/LIGHTMAP_DOWNSAMPLE)
#define LIGHTMAP_SPHERE_FACE_RES_2 (52/LIGHTMAP_DOWNSAMPLE)
#define LIGHTMAP_SPHERE_FACE_INSET 0.5

#define SCREEN_JITTER  (0) // simple and cheap antialiasing
#define DIFFUSE_ONLY   (1) // make the whole scene diffuse, for easier reference comparison
#define TEST_FIREFLIES (1) // note: lots of fireflies with next event estimation (but not without)

#define MATERIAL_TYPE_DIFFUSE    0
#define MATERIAL_TYPE_SPECULAR   1
#define MATERIAL_TYPE_REFRACTIVE 2

// TODO -- maybe try improving the random function
// see https://www.shadertoy.com/view/4djSRW
#define USE_XORSHIFT_RNG (1) // xorshift seems slightly better in some situations ..
#if USE_XORSHIFT_RNG
#define RAND_SEED_TYPE uint
RAND_SEED_TYPE seed = 0U;
RAND_SEED_TYPE InitRandom(vec2 fragCoord, vec2 resolution, int frame, float time)
{
	uint pixelIndex = uint(dot(fragCoord - vec2(0.5), vec2(1, resolution.x))); // [0..w*h-1]
	uint seed = pixelIndex + uint(frame*frame);
	seed *= seed; // improves a bit more
	return seed;
}
float rand(inout RAND_SEED_TYPE seed)
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return float(seed)*(1.0/4294967295.0); // [0..1]
}
#else
#define RAND_SEED_TYPE float
RAND_SEED_TYPE seed = 0.0;
RAND_SEED_TYPE InitRandom(vec2 fragCoord, vec2 resolution, int frame, float time)
{
	vec2 screenPos = 2.0*fragCoord.xy/resolution.xy - 1.0;
	return resolution.y*screenPos.x + screenPos.y + fract(time);
}
float rand(inout RAND_SEED_TYPE seed)
{
	return fract(sin(seed++)*43758.5453123);
}
#endif
vec2 rand2(inout RAND_SEED_TYPE seed) { return vec2(rand(seed), rand(seed)); }
vec3 rand3(inout RAND_SEED_TYPE seed) { return vec3(rand2(seed), rand(seed)); }
vec4 rand4(inout RAND_SEED_TYPE seed) { return vec4(rand3(seed), rand(seed)); }

mat3 transpose3x3(mat3 v)
{
	mat3 tmp;
	tmp[0] = vec3(v[0].x, v[1].x, v[2].x);
	tmp[1] = vec3(v[0].y, v[1].y, v[2].y);
	tmp[2] = vec3(v[0].z, v[1].z, v[2].z);
	return tmp;
}

struct Ray
{
	vec3 origin;
	vec3 dir;
};

struct Object
{
	vec3 pos;
	float radius; // if zero, then it is a quad
	vec3 quadNormal;
	vec3 quadBasisX; // divided by extent
	vec3 quadBasisY; // divided by extent
#if LIGHT_TEXTURED
	uint64_t quadLightTexture;
#endif // LIGHT_TEXTURED
#if LIGHTMAP
	vec4 lightmapBounds;
#endif // LIGHTMAP
	vec3 albedo;
	vec3 emissive;
	int materialType;
};
Object objects[NUM_OBJECTS];

Object MakeQuad(vec3 pos, vec3 normal, vec3 basisX, float extentX, vec3 basisY, float extentY, vec3 albedo, vec3 emissive, int materialType)
{
	Object quad;
	quad.pos = pos;
	quad.radius = 0.0;
	quad.quadNormal = normal;
	quad.quadBasisX = basisX/extentX;
	quad.quadBasisY = basisY/extentY;
#if LIGHT_TEXTURED
	quad.quadLightTexture = 0U;
#endif // LIGHT_TEXTURED
#if LIGHTMAP
	quad.lightmapBounds = vec4(0);
#endif // LIGHTMAP
	quad.albedo = albedo;
	quad.emissive = emissive;
	quad.materialType = materialType;
	return quad;
}

Object MakeSphere(vec3 pos, float radius, vec3 albedo, vec3 emissive, int materialType)
{
	Object sphere;
	sphere.pos = pos;
	sphere.radius = radius;
	sphere.quadNormal = vec3(0);
	sphere.quadBasisX = vec3(0);
	sphere.quadBasisY = vec3(0);
#if LIGHT_TEXTURED
	sphere.quadLightTexture = 0U;
#endif // LIGHT_TEXTURED
#if LIGHTMAP
	sphere.lightmapBounds = vec4(0);
#endif // LIGHTMAP
	sphere.albedo = albedo;
	sphere.emissive = emissive;
	sphere.materialType = materialType;
	return sphere;
}

bool IsDiffuse(Object obj)
{
#if DIFFUSE_ONLY
	return true; // always!
#else
	return obj.materialType == MATERIAL_TYPE_DIFFUSE;
#endif
}

bool IsLight(Object obj)
{
	return max(max(obj.emissive.x, obj.emissive.y), obj.emissive.z) > 0.0;
}

bool IsQuad(Object obj)
{
	return obj.radius == 0.0;
}

bool IsSphere(Object obj)
{
	return obj.radius > 0.0;
}

#if LIGHTMAP
vec4 PackRect(inout vec2 p, vec2 dims, float atlasWidth, inout float heightMax, float padding, float inset)
{
	if (p.x + dims.x + padding > atlasWidth) {
		p.y += heightMax + padding;
		p.x = padding;
		heightMax = 0.0;
	} else
		heightMax = max(dims.y, heightMax);
	vec4 rect = vec4(p, p + dims) + vec4(1,1,-1,-1)*inset;
	p.x += dims.x + padding;
	return rect;
}

#if LIGHTMAP_SPHERE_ATLAS
float GetLightmapSphereFaceRes(Object obj)
{
	// assume 3x2 grid ..
	return (obj.lightmapBounds.w - obj.lightmapBounds.y)*0.5;
}
#endif // LIGHTMAP_SPHERE_ATLAS
#endif // LIGHTMAP

SLIDER_VAR(vec3,obj_0_offset,0,-100,100);
SLIDER_VAR(vec3,obj_1_offset,0,-100,100);
SLIDER_VAR(vec3,light_offset,0,-100,100);

void InitObjects(float lightmapAtlasWidth)
{
	// Cornell Box is (smallpt version):
	// x = 1 (left) to 99 (right)
	// y = 0 (bottom) to 81.6 (top)
	// z = 0 (front) to 170 (back)
	// light is centered at 50,81.6,81.6 with radius 20

	const float lightRadiusScale = 1.0;
	const float lightRadius = 20.0*lightRadiusScale;
	const float lightIntensity = (bool(TEST_FIREFLIES) ? 1.0 : 12.0)/(lightRadiusScale*lightRadiusScale);

#if 1 // use quads for falls
	objects[OBJ_ID_LEFT_WALL ] = MakeQuad(vec3( 1.0,40.8, 85.0), vec3(+1,0,0), vec3(0,0,1), 85.0, vec3(0,-1,0), 40.8, vec3(0.75,0.25,0.25), vec3(0), MATERIAL_TYPE_DIFFUSE);
	objects[OBJ_ID_RIGHT_WALL] = MakeQuad(vec3(99.0,40.8, 85.0), vec3(-1,0,0), vec3(0,0,1), 85.0, vec3(0,+1,0), 40.8, vec3(0.25,0.25,0.75), vec3(0), MATERIAL_TYPE_DIFFUSE);
	objects[OBJ_ID_FLOOR     ] = MakeQuad(vec3(50.0, 0.0, 85.0), vec3(0,+1,0), vec3(0,0,1), 85.0, vec3(+1,0,0), 49.0, vec3(0.75), vec3(0), MATERIAL_TYPE_DIFFUSE);
	objects[OBJ_ID_CEILING   ] = MakeQuad(vec3(50.0,81.6, 85.0), vec3(0,-1,0), vec3(0,0,1), 85.0, vec3(-1,0,0), 49.0, vec3(0.75), vec3(0), MATERIAL_TYPE_DIFFUSE);
	objects[OBJ_ID_FRONT_WALL] = MakeQuad(vec3(50.0,40.8,  0.0), vec3(0,0,+1), vec3(1,0,0), 49.0, vec3(0,+1,0), 40.8, vec3(0.75), vec3(0), MATERIAL_TYPE_DIFFUSE);
	objects[OBJ_ID_BACK_WALL ] = MakeQuad(vec3(50.0,40.8,170.0), vec3(0,0,-1), vec3(1,0,0), 49.0, vec3(0,-1,0), 40.8, vec3(0.00), vec3(0), MATERIAL_TYPE_DIFFUSE);
#else // use spheres for walls (original)
	const float r = 1e5;
	objects[OBJ_ID_LEFT_WALL ] = MakeSphere(vec3( 1.0 - r, 40.8, 81.6),  r, vec3(0.75,0.25,0.25), vec3(0), MATERIAL_TYPE_DIFFUSE);
	objects[OBJ_ID_RIGHT_WALL] = MakeSphere(vec3(99.0 + r, 40.8, 81.6),  r, vec3(0.25,0.25,0.75), vec3(0), MATERIAL_TYPE_DIFFUSE);
	objects[OBJ_ID_FLOOR     ] = MakeSphere(vec3(50.0, -r, 81.6),        r, vec3(0.75), vec3(0), MATERIAL_TYPE_DIFFUSE);
	objects[OBJ_ID_CEILING   ] = MakeSphere(vec3(50.0, 81.6 + r, 81.6),  r, vec3(0.75), vec3(0), MATERIAL_TYPE_DIFFUSE);
	objects[OBJ_ID_FRONT_WALL] = MakeSphere(vec3(50.0, 40.8, -r),        r, vec3(0.75), vec3(0), MATERIAL_TYPE_DIFFUSE);
	objects[OBJ_ID_BACK_WALL ] = MakeSphere(vec3(50.0, 40.8, 170.0 + r), r, vec3(0.0), vec3(0), MATERIAL_TYPE_DIFFUSE);
#endif
	objects[OBJ_ID_LEFT_SPHERE ] = MakeSphere(vec3(27.0, 16.5, 47.0), 16.5, vec3(0.9),         vec3(0), bool(DIFFUSE_ONLY) ? MATERIAL_TYPE_DIFFUSE : MATERIAL_TYPE_SPECULAR);
	objects[OBJ_ID_RIGHT_SPHERE] = MakeSphere(vec3(73.0, 16.5, 78.0), 16.5, vec3(0.7,0.9,0.9), vec3(0), bool(DIFFUSE_ONLY) ? MATERIAL_TYPE_DIFFUSE : MATERIAL_TYPE_REFRACTIVE);
#if LIGHT_IS_QUAD
	const float epsilon = 1e-3;
#if TEST_FIREFLIES
	//objects[OBJ_ID_CEILING_LIGHT] = MakeQuad(vec3(1.0 + epsilon, lightRadius - 37.0, lightRadius + 10.0), vec3(1,0,0), vec3(0,1,0), lightRadius, vec3(0,0,1), lightRadius, vec3(0.0), vec3(lightIntensity), MATERIAL_TYPE_DIFFUSE); // wall light
	objects[OBJ_ID_CEILING_LIGHT] = MakeQuad(vec3(20.0, lightRadius, 78.0), vec3(1,0,0), vec3(0,0,-1), lightRadius*640.0/480.0, vec3(0,1,0), lightRadius, vec3(0.0), vec3(lightIntensity), MATERIAL_TYPE_DIFFUSE);
#else
	objects[OBJ_ID_CEILING_LIGHT] = MakeQuad(vec3(50.0, 81.6 - epsilon, 81.6), vec3(0,-1,0), vec3(1,0,0), lightRadius, vec3(0,0,1), lightRadius, vec3(0.0), vec3(lightIntensity), MATERIAL_TYPE_DIFFUSE);
#endif
#else
	objects[OBJ_ID_CEILING_LIGHT] = MakeSphere(vec3(50.0, 81.6, 81.6), lightRadius, vec3(0.0), vec3(lightIntensity), MATERIAL_TYPE_DIFFUSE);
#endif

#if LIGHTMAP
	if (lightmapAtlasWidth > 0.0) {
		const float padding = float(LIGHTMAP_PADDING);
		vec2 p = vec2(padding);
		float hmax = 0.0;
		const float quadInset = float(LIGHTMAP_QUAD_INSET);
		vec2 quadSize = vec2(LIGHTMAP_QUAD_RES_U, LIGHTMAP_QUAD_RES_V);
		objects[OBJ_ID_LEFT_WALL   ].lightmapBounds = PackRect(p, quadSize,    lightmapAtlasWidth, hmax, padding, quadInset);
		objects[OBJ_ID_FLOOR       ].lightmapBounds = PackRect(p, quadSize,    lightmapAtlasWidth, hmax, padding, quadInset);
		objects[OBJ_ID_FRONT_WALL  ].lightmapBounds = PackRect(p, quadSize.yy, lightmapAtlasWidth, hmax, padding, quadInset);
		objects[OBJ_ID_RIGHT_WALL  ].lightmapBounds = PackRect(p, quadSize,    lightmapAtlasWidth, hmax, padding, quadInset);
		objects[OBJ_ID_CEILING     ].lightmapBounds = PackRect(p, quadSize,    lightmapAtlasWidth, hmax, padding, quadInset);
		objects[OBJ_ID_BACK_WALL   ].lightmapBounds = vec4(0);
		objects[OBJ_ID_LEFT_SPHERE ].lightmapBounds = PackRect(p, vec2(3,2)*float(LIGHTMAP_SPHERE_FACE_RES_1), lightmapAtlasWidth, hmax, padding, 0.0);
		objects[OBJ_ID_RIGHT_SPHERE].lightmapBounds = PackRect(p, vec2(3,2)*float(LIGHTMAP_SPHERE_FACE_RES_2), lightmapAtlasWidth, hmax, padding, 0.0);
	}
#endif // LIGHTMAP

	objects[OBJ_ID_LEFT_SPHERE].pos += obj_0_offset;
	objects[OBJ_ID_RIGHT_SPHERE].pos += obj_1_offset;
	objects[OBJ_ID_CEILING_LIGHT].pos += light_offset;

#if LIGHT_TEXTURED && defined(passionflower)
	objects[OBJ_ID_CEILING_LIGHT].quadLightTexture = uint64_t(passionflower);
#endif // LIGHT_TEXTURED && defined(passionflower)
}

#if defined(lightmap)
#define LIGHTMAP_ATLAS_WIDTH() float(textureSize(lightmap, 0).x)
#else
#define LIGHTMAP_ATLAS_WIDTH() 0
#endif

#define INIT_SCENE() \
	bool diffuseUniformSampling = IS_KEY_TOGGLED    (KEY_U);\
	bool directLightSampling    = IS_KEY_NOT_TOGGLED(KEY_E);\
	bool lightmapEnabled        = IS_KEY_TOGGLED    (KEY_L);\
	int  lightmapDepth          = lightmapEnabled ? 0 : -1;\
	int  minDepth               = IS_KEY_TOGGLED(KEY_I) ? MIN_DEPTH_INDIRECT : 0;\
	int  maxDepth               = IS_KEY_TOGGLED(KEY_O) ? 1 : max(MAX_DEPTH, MIN_DEPTH_INDIRECT);\
	bool haltonEnabled          = IS_KEY_NOT_TOGGLED(KEY_H); \
	seed = InitRandom(fragCoord, iResolution.xy, iFrame, iTime);\
	InitObjects(lightmapEnabled ? LIGHTMAP_ATLAS_WIDTH() : 0.0)

// ============================================================================

vec3 GetQuadBasisExtentX(Object quad) { return quad.quadBasisX/dot(quad.quadBasisX, quad.quadBasisX); }
vec3 GetQuadBasisExtentY(Object quad) { return quad.quadBasisY/dot(quad.quadBasisY, quad.quadBasisY); }

vec3 QuadLocalToWorld(vec2 P, Object quad) // P is [-1..1]
{
	return quad.pos + GetQuadBasisExtentX(quad)*P.x + GetQuadBasisExtentY(quad)*P.y;
}

float GetQuadArea(Object quad)
{
	float x = dot(quad.quadBasisX, quad.quadBasisX);
	float y = dot(quad.quadBasisY, quad.quadBasisY);
	return 4.0/sqrt(x*y); // could precompute this ..
}

float GetSphereArea(Object sphere)
{
	return 4.0*PI*sphere.radius*sphere.radius;
}

// ============================================================================

float DistanceToQuadOrPlane(vec3 P, Object quad, bool isInfinitePlane)
{
	vec3 V = P - quad.pos;
	float vz = abs(dot(V, quad.quadNormal));
	if (!isInfinitePlane) {
		float ex = 1.0/length(quad.quadBasisX);
		float ey = 1.0/length(quad.quadBasisY);
		float vx = ex*max(0.0, abs(dot(V, quad.quadBasisX)) - 1.0);
		float vy = ey*max(0.0, abs(dot(V, quad.quadBasisY)) - 1.0);
		vz = length(vec3(vx, vy, vz));
	}
	return vz;
}

float DistanceToQuad(vec3 P, Object quad)
{
	return DistanceToQuadOrPlane(P, quad, false);
}

float DistanceToSphere(vec3 P, Object sphere)
{
	vec3 V = P - sphere.pos;
	return length(V) - sphere.radius;
}

float DistanceToObject(vec3 P, Object obj)
{
	if (IsQuad(obj))
		return DistanceToQuad(P, obj);
	else
		return DistanceToSphere(P, obj);
}

// ============================================================================

vec3 ClosestPointOnQuadOrPlane(vec3 P, Object quad, bool isInfinitePlane)
{
	vec3 V = P - quad.pos;
	float vz = dot(V, quad.quadNormal);
	if (!isInfinitePlane) {
		float vx = dot(V, quad.quadBasisX); // note quadBasis is divided by extent
		float vy = dot(V, quad.quadBasisY);
		vx = clamp(vx, -1.0, 1.0);
		vy = clamp(vy, -1.0, 1.0);
		return QuadLocalToWorld(vec2(vx, vy), quad);
	} else
		return P - quad.quadNormal*vz;
}

vec3 ClosestPointOnQuad(vec3 P, Object quad)
{
	return ClosestPointOnQuadOrPlane(P, quad, false);
}

vec3 ClosestPointOnSphere(vec3 P, Object sphere)
{
	vec3 V = P - sphere.pos;
	return sphere.pos + normalize(V)*sphere.radius;
}

vec3 ClosestPointOnObject(vec3 P, Object obj)
{
	if (IsQuad(obj))
		return ClosestPointOnQuad(P, obj);
	else
		return ClosestPointOnSphere(P, obj);
}

// ============================================================================

float IntersectQuadOrPlane(Ray ray, Object quad, bool isInfinitePlane, bool twoSided)
{
	vec3 V = quad.pos - ray.origin;
	float t = 0.0;
	float d = dot(ray.dir, quad.quadNormal);
	if (d < 0.0 || twoSided) {
		t = max(0.0, dot(V, quad.quadNormal)/d);
		if (t > 0.0 && !isInfinitePlane) {
			V -= t*ray.dir;
			float vx = dot(V, quad.quadBasisX); // note quadBasis is divided by extent
			float vy = dot(V, quad.quadBasisY);
			if (max(abs(vx), abs(vy)) > 1.0)
				t = 0.0;
		}
	}
	return t;
}

float IntersectQuad(Ray ray, Object quad)
{
	bool twoSided = false;//IsLight(quad);
	return IntersectQuadOrPlane(ray, quad, false, twoSided);
}

float IntersectSphere(Ray ray, Object sphere)
{
	vec3 V = sphere.pos - ray.origin;
	float r = sphere.radius;
	float b = dot(V, ray.dir);
	float det = b*b - dot(V, V) + r*r;
	if (det < 0.0)
		return 0.0;
	else {
		det = sqrt(det);
		float epsilon = 1e-3;
		float t;
		return (t = b - det) > epsilon ? t : ((t = b + det) > epsilon ? t : 0.0);
	}
}

float IntersectObject(Ray ray, Object obj)
{
	if (IsQuad(obj))
		return IntersectQuad(ray, obj);
	else
		return IntersectSphere(ray, obj);
}

int IntersectScene(Ray ray, int ignoreObjId, out float out_t, out Object out_obj)
{
	int id = OBJ_ID_NONE;
	out_t = 1e5;
	out_obj = objects[0]; // none
	for (int i = 0; i < NUM_OBJECTS; i++) {
		Object obj = objects[i];
		float t = IntersectObject(ray, obj);
		if (i != ignoreObjId && t != 0.0 && t < out_t) {
			id = i;
			out_t = t;
			out_obj = obj;
		}
	}
	return id;
}

int IntersectScene(Ray ray, int ignoreObjId)
{
	float t_unused;
	Object obj_unused;
	return IntersectScene(ray, ignoreObjId, t_unused, obj_unused);
}

// ============================================================================

vec3 GetSurfaceNormal(vec3 hitPos, Object obj)
{
	if (IsQuad(obj))
		return obj.quadNormal;
	else
		return normalize(hitPos - obj.pos);
}

vec2 Weyl(int i)
{
	// http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
	// https://www.shadertoy.com/view/4dtBWH
	//return fract(float(n)*vec2(0.754877669, 0.569840296));
	return fract(vec2(i*ivec2(12664745, 9560333))/exp2(24.0)); // integer mul to avoid round-off
}

float Halton(int b, int i)
{
	float r = 0.0;
	float f = 1.0;
	while (i > 0) {
		f = f / float(b);
		r = r + f * float(i % b);
		i = int(float(i) / float(b));
	}
	return r;
}

float Halton2(int i)
{
#if __VERSION__ >= 400
	return float(bitfieldReverse(uint(i)))/4294967296.0;
#else
	return Halton(2, i);
#endif
}

vec2 Halton23(int i)
{
	return vec2(Halton2(i), Halton(3, i));
}

vec2 Hammersley(int i, int n)
{
	float y = float(i)/float(n); // [0..1)
	return vec2(Halton2(i), y);
}

mat3 MakeOrthoBasis(vec3 N)
{
	vec3 basisX = normalize(cross(N.yzx, N));
	vec3 basisY = cross(N, basisX);
	return mat3(basisX, basisY, N);
}

vec3 SampleSpherical(float sinTheta, float cosTheta, float phi)
{
	float sinPhi = sin(phi);
	float cosPhi = cos(phi);
	return vec3(vec2(cosPhi, sinPhi)*sinTheta, cosTheta);
}

vec3 SampleHemisphere(vec3 N, float sinTheta, float cosTheta, float s_x)
{
	float phi = 2.0*PI*s_x;
	return MakeOrthoBasis(N)*SampleSpherical(sinTheta, cosTheta, phi);
}

vec3 SampleHemisphereCosineWeighted(vec3 N, vec2 s)
{
	float sinThetaSqr = s.y;
	float sinTheta = sqrt(sinThetaSqr);
	float cosTheta = sqrt(1.0 - sinThetaSqr);
	return SampleHemisphere(N, sinTheta, cosTheta, s.x);
}

vec3 SampleHemisphereUniformCone(vec3 N, vec2 s, float halfConeAngleInRadians)
{
	float cosTheta = s.y;
	float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
	float phi = 2.0*PI*s.x;
	vec3 V = SampleSpherical(sinTheta, cosTheta, phi);
	float NdotV = dot(N, V);
	if (NdotV < 0.0) { // necessary if N != {0,0,1}
		V = -V;
		NdotV = -NdotV;
	}
	if (halfConeAngleInRadians != PI*0.5) {
		float c2 = cos(acos(NdotV)*halfConeAngleInRadians); c2 *= c2;
		float s2 = 1.0 - c2;
		float z = sqrt((c2/s2)*(1.0 - NdotV*NdotV)) - NdotV;
		V = normalize(V + N*z);
	}
	return V;
}

vec3 SampleHemisphereUniform(vec3 N, vec2 s)
{
	return SampleHemisphereUniformCone(N, s, PI*0.5);
}

float LTC_IntegrateEdge(vec3 v1, vec3 v2)
{
	float theta = acos(dot(v1, v2));
#if 0 // from https://blog.selfshadow.com/sandbox/ltc.html
	return (v1.x*v2.y - v1.y*v2.x)*((theta > 0.001) ? theta/sin(theta) : 1.0);
#else // from http://jsfiddle.net/hh74z2ft -- appears to be the same
	return normalize(cross(v1, v2)).z*theta;
#endif
}

int LTC_ClipQuadToHorizonPlane(inout vec3 L[5], in float d[4])
{
	// detect clipping config
	int config = 0;
	if (d[0] > 0.0) config += 1;
	if (d[1] > 0.0) config += 2;
	if (d[2] > 0.0) config += 4;
	if (d[3] > 0.0) config += 8;

	if (config == 1) { // L0 clip L1 L2 L3
		L[1] = -d[1] * L[0] + d[0] * L[1]; // L1 = clip(L1,L0)
		L[2] = -d[3] * L[0] + d[0] * L[3]; // L2 = clip(L3,L0)
		return 3;
	} else if (config == 2) { // L1 clip L0 L2 L3
		L[0] = -d[0] * L[1] + d[1] * L[0]; // L0 = clip(L0,L1)
		L[2] = -d[2] * L[1] + d[1] * L[2]; // L2 = clip(L2,L1)
		return 3;
	} else if (config == 3) { // L0 L1 clip L2 L3
		L[2] = -d[2] * L[1] + d[1] * L[2]; // L2 = clip(L2,L1)
		L[3] = -d[3] * L[0] + d[0] * L[3]; // L3 = clip(L3,L0)
		return 4;
	} else if (config == 4) { // L2 clip L0 L1 L3
		L[0] = -d[3] * L[2] + d[2] * L[3]; // L0 = clip(L3,L2)
		L[1] = -d[1] * L[2] + d[2] * L[1]; // L1 = clip(L1,L2)
		return 3;
//  } else if (config == 5) { // L0 L2 clip L1 L3  (impossible)
	} else if (config == 6) { // L1 L2 clip L0 L3
		L[0] = -d[0] * L[1] + d[1] * L[0]; // L0 = clip(L0,L1)
		L[3] = -d[3] * L[2] + d[2] * L[3]; // L3 = clip(L3,L2)
		return 4;
	} else if (config == 7) { // L0 L1 L2 clip L3
		L[4] = -d[3] * L[0] + d[0] * L[3]; // L4 = clip(L3,L0)
		L[3] = -d[3] * L[2] + d[2] * L[3]; // L3 = clip(L3,L2)
		return 5;
	} else if (config == 8) { // L3 clip L0 L1 L2
		L[0] = -d[0] * L[3] + d[3] * L[0]; // L0 = clip(L0,L3)
		L[1] = -d[2] * L[3] + d[3] * L[2]; // L1 = clip(L2,L3)
		L[2] =  L[3];
		return 3;
	} else if (config == 9) { // L0 L3 clip L1 L2
		L[1] = -d[1] * L[0] + d[0] * L[1]; // L1 = clip(L1,L0)
		L[2] = -d[2] * L[3] + d[3] * L[2]; // L2 = clip(L2,L3)
		return 4;
//	} else if (config == 10) { // L1 L3 clip L0 L2 (impossible)
	} else if (config == 11) { // L0 L1 L3 clip L2
		L[4] =  L[3];
		L[3] = -d[2] * L[3] + d[3] * L[2]; // L3 = clip(L2,L3)
		L[2] = -d[2] * L[1] + d[1] * L[2]; // L2 = clip(L2,L1)
		return 5;
	} else if (config == 12) { // L2 L3 clip L0 L1
		L[1] = -d[1] * L[2] + d[2] * L[1]; // L1 = clip(L1,L2)
		L[0] = -d[0] * L[3] + d[3] * L[0]; // L0 = clip(L0,L3)
		return 4;
	} else if (config == 13) { // L0 L2 L3 clip L1
		L[4] =  L[3];
		L[3] =  L[2];
		L[2] = -d[1] * L[2] + d[2] * L[1]; // L2 = clip(L2,L1)
		L[1] = -d[1] * L[0] + d[0] * L[1]; // L1 = clip(L1,L0)
		return 5;
	} else if (config == 14) { // L1 L2 L3 clip L0
		L[4] = -d[0] * L[3] + d[3] * L[0]; // L4 = clip(L0,L3)
		L[0] = -d[0] * L[1] + d[1] * L[0]; // L0 = clip(L0,L1)
		return 5;
	} else if (config == 15) // L0 L1 L2 L3 (no clipping)
		return 4;
	else
		return 0; // all points clipped
}

float LTC_Evaluate(vec3 P, vec3 N, vec3 V, mat3 Minv, vec3 corners[4], bool clipToHorizon, bool twoSided)
{
	// construct orthonormal basis around N
	vec3 T1, T2;
	T1 = normalize(V - N*dot(V, N));
	T2 = cross(N, T1);

	// rotate area light in (T1, T2, N) basis
	Minv = Minv*transpose3x3(mat3(T1, T2, N));

	// polygon (allocate 5 vertices for clipping)
	vec3 L[5];
	L[0] = Minv*(corners[0] - P);
	L[1] = Minv*(corners[1] - P);
	L[2] = Minv*(corners[2] - P);
	L[3] = Minv*(corners[3] - P);
	float d[4]; // distances to clipping plane
	d[0] = L[0].z;
	d[1] = L[1].z;
	d[2] = L[2].z;
	d[3] = L[3].z;
	int n = 4;
	if (clipToHorizon)
		n = LTC_ClipQuadToHorizonPlane(L, d);

	// integrate
	float sum = 0.0;
	if (n > 0) {
		L[0] = normalize(L[0]);
		L[1] = normalize(L[1]);
		L[2] = normalize(L[2]);
		sum += LTC_IntegrateEdge(L[0], L[1]);
		sum += LTC_IntegrateEdge(L[1], L[2]);
	#if 0 // generalized for N-sided polygon
		int i = 3;
		while (i < n) {
			L[i] = normalize(L[i]);
			sum += LTC_IntegrateEdge(L[i - 1], L[i]);
			i++;
		}
		sum += LTC_IntegrateEdge(L[i - 1], L[0]);
	#else
		if (n == 3)
			sum += LTC_IntegrateEdge(L[2], L[0]);
		else { // n >= 4
			L[3] = normalize(L[3]);
			sum += LTC_IntegrateEdge(L[2], L[3]);
			if (n == 4)
				sum += LTC_IntegrateEdge(L[3], L[0]);
			else { // n >= 5
				L[4] = normalize(L[4]);
				sum += LTC_IntegrateEdge(L[3], L[4]);
				sum += LTC_IntegrateEdge(L[4], L[0]);
			}
		}
	#endif
	}
	sum *= 0.5/PI;
	return twoSided ? abs(sum) : max(0.0, sum);
}

float LTC_EvaluateDiffuse(vec3 P, vec3 N, Object light)
{
	if (IsQuad(light)) {
		vec3 V = light.pos - P;
		vec3 bx = GetQuadBasisExtentX(light);
		vec3 by = GetQuadBasisExtentY(light);
		vec3 corners[4] = vec3[4](
			light.pos - bx + by,
			light.pos + bx + by,
			light.pos + bx - by,
			light.pos - bx - by
		);
		const bool clipToHorizon = true;
		const bool twoSided = false;
		return LTC_Evaluate(P, N, V, mat3(1), corners, clipToHorizon, twoSided);
	} else
		return 0.0; // not implemented for sphere
}

SLIDER_VAR(float,light_intensity,1.5,0,10);
#if LIGHT_TEXTURED
SLIDER_VAR(float,light_texture_LOD,1,0,10);
#endif // LIGHT_TEXTURED

// =============================================================
// set MAX = 0 to always direct sample light
// set MIN = MAX > 0 to direct sample light beyond this distance
// set MIN < MAX to make it probabilistic
// =============================================================
SLIDER_VAR(bool,direct_light_dist_enabled,true,x,x);
SLIDER_VAR(bool,direct_light_dist_dbg,false,x,x);
SLIDER_VAR(float,direct_light_dist_min,0,0,100);
SLIDER_VAR(float,direct_light_dist_max,0,0,100);

SLIDER_VAR(bool,MIS_enabled,true,x,x);
SLIDER_VAR(float,MIS_ratio_default,0.5,0,1);
#define MIS_USE_POWER (0) // doesnt seem to make any difference
#if MIS_USE_POWER
SLIDER_VAR_CONST(float,MIS_power_b,2,1,16);
#endif // MIS_USE_POWER

SLIDER_VAR(bool,MIS_light_dist_enabled,true,x,x);  // these control the ratio of direct samples vs hemisphere samples for MIS based on distance
SLIDER_VAR(bool,MIS_light_dist_dbg,false,x,x);
SLIDER_VAR(float,MIS_light_dist_min,0,0,100);
SLIDER_VAR(float,MIS_light_dist_max,0,0,100);

bool ShouldSampleLight(vec3 P, vec3 N, Object light, inout vec3 dbg)
{
	if (IsLight(light)) {
		if (direct_light_dist_enabled && direct_light_dist_max <= 0.0)
			return true;
		else {
			float dmax = direct_light_dist_max;
			float dmin = min(dmax - 0.0001, direct_light_dist_min);
			float ds = 1.0/(dmin - dmax);
			float d0 = -ds*dmax;
			float q = clamp(DistanceToObject(P, light)*ds + d0, 0.0, 1.0); // q=1 @ min dist, q=0 @ max dist
			if (direct_light_dist_dbg) {
				dbg += vec3(q*q);
				return true;
			} else if (q < rand(seed))
				return true;
		}
	}
	return false;
}

vec3 SampleLightColor(vec3 P, Object light)
{
	vec3 color = light.emissive*light_intensity;
#if LIGHT_TEXTURED
	if (light.quadLightTexture != 0U) {
		vec3 V = P - light.pos;
		float vx = dot(V, light.quadBasisX); // note quadBasis is divided by extent
		float vy = dot(V, light.quadBasisY);
		vec2 uv = vec2(vx, -vy)*0.5 + vec2(0.5); // [0..1]
		color *= textureLod(sampler2D(light.quadLightTexture), uv, light_texture_LOD).rgb;
	}
#endif // LIGHT_TEXTURED
	//if (IsSphere(light)) {
	//	vec3 V = normalize(P - light.pos);
	//	vec3 A = abs(V);
	//	color *= mix(vec3(0.1), V*0.5 + vec3(0.5), pow(max(max(A.x, A.y), A.z), 8.0));
	//}
	return color;
}

// An Area-Preserving Parametrization for Spherical Rectangles
// https://www.solidangle.com/research/egsr2013_spherical_rectangle.pdf
// https://www.shadertoy.com/view/ltsyzl
struct SphQuad
{
	vec3 o, x, y, z; // local reference system R
	float z0, z0sq; //
	float x0, y0, y0sq; // rectangle coords in R
	float x1, y1, y1sq; //
	float b0, b1, b0sq, k; // misc precomputed constants
	float S; // solid angle of Q
};

SphQuad SphQuadInit(vec3 P, Object lightQuad)
{
	// the original paper defines these variables a little differently than how i would ..
	vec3 ex = GetQuadBasisExtentX(lightQuad)*2.0;
	vec3 ey = GetQuadBasisExtentY(lightQuad)*2.0;
	vec3 s = lightQuad.pos - (ex + ey)*0.5;
	vec3 o = P;

	// from here on, the variables match the original paper
	SphQuad squad;
	squad.o = o;
	float exl = length(ex);
	float eyl = length(ey);
	// compute local reference system R
	squad.x = ex/exl;
	squad.y = ey/eyl;
	squad.z = cross(squad.x, squad.y);
	// compute rectangle coords in local reference system
	vec3 d = s - o;
	squad.z0 = dot(d, squad.z);
	// flip z to make it point against Q
	if (squad.z0 > 0.0) {
		squad.z *= -1.0;
		squad.z0 *= -1.0;
	}
	squad.z0sq = squad.z0*squad.z0;
	squad.x0 = dot(d, squad.x);
	squad.y0 = dot(d, squad.y);
	squad.x1 = squad.x0 + exl;
	squad.y1 = squad.y0 + eyl;
	squad.y0sq = squad.y0*squad.y0;
	squad.y1sq = squad.y1*squad.y1;
	// create vectors to four vertices
	vec3 v00 = vec3(squad.x0, squad.y0, squad.z0);
	vec3 v01 = vec3(squad.x0, squad.y1, squad.z0);
	vec3 v10 = vec3(squad.x1, squad.y0, squad.z0);
	vec3 v11 = vec3(squad.x1, squad.y1, squad.z0);
	// compute normals to edges
	vec3 n0 = normalize(cross(v00, v10));
	vec3 n1 = normalize(cross(v10, v11));
	vec3 n2 = normalize(cross(v11, v01));
	vec3 n3 = normalize(cross(v01, v00));
	// compute internal angles (gamma_i)
	float g0 = acos(-dot(n0, n1));
	float g1 = acos(-dot(n1, n2));
	float g2 = acos(-dot(n2, n3));
	float g3 = acos(-dot(n3, n0));
	// compute predefined constants
	squad.b0 = n0.z;
	squad.b1 = n2.z;
	squad.b0sq = squad.b0*squad.b0;
	squad.k = 2.0*PI - g2 - g3;
	// compute solid angle from internal angles
	squad.S = g0 + g1 - squad.k;
	return squad;
}

vec3 SphQuadSample(SphQuad squad, float u, float v)
{
	// 1. compute cu
	float au = u*squad.S + squad.k;
	float fu = (cos(au)*squad.b0 - squad.b1)/sin(au);
	float cu = 1.0/sqrt(fu*fu + squad.b0sq)*(fu > 0.0 ? 1.0 : -1.0);
	cu = clamp(cu, -1.0, 1.0); // avoid NaNs
	// 2. compute xu
	float xu = -(cu*squad.z0)/sqrt(1.0 - cu*cu);
	xu = clamp(xu, squad.x0, squad.x1); // avoid Infs
	// 3. compute yv
	float d = sqrt(xu*xu + squad.z0sq);
	float h0 = squad.y0/sqrt(d*d + squad.y0sq);
	float h1 = squad.y1/sqrt(d*d + squad.y1sq);
	float hv = h0 + v*(h1 - h0), hv2 = hv*hv;
	const float epsilon = 0.0001;
	float yv = (hv2 < 1.0 - epsilon) ? (hv*d)/sqrt(1.0 - hv2) : squad.y1;
	// 4. transform (xu,yv,z0) to world coords
	return squad.o + xu*squad.x + yv*squad.y + squad.z0*squad.z;
}

vec3 SampleLight(Object light, int lightId, int ignoreObjId, vec3 P, vec3 N, vec2 s, bool sphericalLightIsTextured)
{
	vec3 V;
	vec3 L;
	float inversePDF_d;
	if (IsQuad(light)) {
		bool sampleSphericalRectangle = IS_KEY_NOT_TOGGLED(KEY_R);
		if (sampleSphericalRectangle) {
			SphQuad squad = SphQuadInit(P, light);
			V = SphQuadSample(squad, s.x, s.y) - P;
			L = normalize(V);
			inversePDF_d = squad.S;
		} else {
			V = QuadLocalToWorld(s*2.0 - vec2(1.0), light) - P;
			L = normalize(V);
			float distSqr = dot(V, V);
			inversePDF_d = GetQuadArea(light)*max(0.0, -dot(light.quadNormal, L))/distSqr;
		}
	} else {
		// http://www.pbr-book.org/3ed-2018/Light_Transport_I_Surface_Reflection/Sampling_Light_Sources.html
		vec3 pointToLight = light.pos - P;
		float radiusSqr = light.radius*light.radius;
		float sinThetaMaxSqr = radiusSqr/dot(pointToLight, pointToLight);
		float cosThetaMax = sqrt(1.0 - sinThetaMaxSqr);
		float cosTheta = cosThetaMax + (1.0 - cosThetaMax)*s.y;
		float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
		if (sphericalLightIsTextured) {
			float dc = length(pointToLight);
			float ds = dc*cosTheta - sqrt(max(0.0, radiusSqr - dc*dc*sinTheta*sinTheta));
			float cosAlpha = (radiusSqr + dc*dc - ds*ds)/(2.0*dc*light.radius);
			float sinAlpha = sqrt(max(0.0, 1.0 - cosAlpha*cosAlpha));
			V = light.pos + light.radius*SampleHemisphere(-normalize(pointToLight), sinAlpha, cosAlpha, s.x) - P;
			L = normalize(V);
		} else {
			V = vec3(0); // V will be incorrect in this codepath, so SampleLightColor below won't work if the light is textured
			L = SampleHemisphere(normalize(pointToLight), sinTheta, cosTheta, s.x);
		}
		inversePDF_d = 2.0*PI*(1.0 - cosThetaMax);
	}
	inversePDF_d *= max(0.0, dot(N, L))/PI;
	if (inversePDF_d > 0.0 && IntersectScene(Ray(P, L), ignoreObjId) == lightId)
		return SampleLightColor(P + V, light)*inversePDF_d;
	else
		return vec3(0);
}

// sample MIS direct light distribution
vec3 SampleLightMIS_d(Object light, int lightId, int ignoreObjId, vec3 P, vec3 N, vec2 s, float N_d, float N_h, bool sphericalLightIsTextured)
{
	vec3 V;
	vec3 L;
	float inversePDF_d;
	if (IsQuad(light)) {
		V = QuadLocalToWorld(s*2.0 - vec2(1.0), light) - P;
		L = normalize(V);
		float distSqr = dot(V, V);
		inversePDF_d = GetQuadArea(light)*max(0.0, -dot(light.quadNormal, L))/distSqr;
	} else {
		// http://www.pbr-book.org/3ed-2018/Light_Transport_I_Surface_Reflection/Sampling_Light_Sources.html
		vec3 pointToLight = light.pos - P;
		float radiusSqr = light.radius*light.radius;
		float sinThetaMaxSqr = radiusSqr/dot(pointToLight, pointToLight);
		float cosThetaMax = sqrt(1.0 - sinThetaMaxSqr);
		float cosTheta = cosThetaMax + (1.0 - cosThetaMax)*s.y;
		float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
		if (sphericalLightIsTextured) {
			float dc = length(pointToLight);
			float ds = dc*cosTheta - sqrt(max(0.0, radiusSqr - dc*dc*sinTheta*sinTheta));
			float cosAlpha = (radiusSqr + dc*dc - ds*ds)/(2.0*dc*light.radius);
			float sinAlpha = sqrt(max(0.0, 1.0 - cosAlpha*cosAlpha));
			V = light.pos + light.radius*SampleHemisphere(-normalize(pointToLight), sinAlpha, cosAlpha, s.x) - P;
			L = normalize(V);
		} else {
			V = vec3(0);
			L = SampleHemisphere(normalize(pointToLight), sinTheta, cosTheta, s.x);
		}
		inversePDF_d = 2.0*PI*(1.0 - cosThetaMax);
	}
	inversePDF_d *= max(0.0, dot(N, L))/PI;
	if (inversePDF_d > 0.0 && IntersectScene(Ray(P, L), ignoreObjId) == lightId) {
		float PDF_d = 1.0/inversePDF_d;
		float PDF_h = 1.0;
	#if MIS_USE_POWER
		float b = MIS_power_b;
		return SampleLightColor(P + V, light)*pow(N_d*PDF_d, b - 1.0)/(pow(N_d*PDF_d, b) + pow(N_h*PDF_h, b));
	#else
		return SampleLightColor(P + V, light)/(N_d*PDF_d + N_h*PDF_h);
	#endif
	}
	return vec3(0);
}

// sample MIS hemisphere distribution
vec3 SampleLightMIS_h(Object light, int lightId, int ignoreObjId, vec3 P, vec3 N, vec2 s, float N_d, float N_h)
{
	vec3 L = SampleHemisphereCosineWeighted(N, s);
	float t;
	Object unused;
	if (IntersectScene(Ray(P, L), ignoreObjId, t, unused) == lightId) {
		vec3 V = L*t;
		float inversePDF_d;
		if (IsQuad(light)) {
			float distSqr = t*t; // same as dot(V, V)
			inversePDF_d = GetQuadArea(light)*max(0.0, -dot(light.quadNormal, L))/distSqr;
		} else {
			vec3 pointToLight = light.pos - P;
			float radiusSqr = light.radius*light.radius;
			float sinThetaMaxSqr = radiusSqr/dot(pointToLight, pointToLight);
			float cosThetaMax = sqrt(1.0 - sinThetaMaxSqr);
			inversePDF_d = 2.0*PI*(1.0 - cosThetaMax);
		}
		inversePDF_d *= max(0.0, dot(N, L))/PI;
		if (inversePDF_d > 0.0) {
			float PDF_d = 1.0/inversePDF_d;
			float PDF_h = 1.0;
		#if MIS_USE_POWER
			float b = MIS_power_b;
			return SampleLightColor(P + V, light)*pow(N_h*PDF_h, b - 1.0)/(pow(N_d*PDF_d, b) + pow(N_h*PDF_h, b));
		#else
			return SampleLightColor(P + V, light)/(N_d*PDF_d + N_h*PDF_h);
		#endif
		}
	}
	return vec3(0);
}

vec3 SampleLightsInScene(
	vec3 P,
	vec3 N,
	bool haltonEnabled,
	vec2 offset,
	int objId,
	int frameIndex, // iFrame
	int numLightSamples,
	inout uint wasSampled)
{
	bool sphericalLightIsTextured = false; // enable this if SampleLightColor needs position for spherical lights
	vec3 e = vec3(0);
	for (int lightId = 0; lightId < NO_UNROLL_(NUM_OBJECTS, objId); lightId++) {
		Object light = objects[lightId];
		if (IsQuad(light) && dot(light.pos - P, light.quadNormal) >= 0.0) { // facing away?
			wasSampled |= (1U << lightId); // might as well mark this light as sampled, we won't hit it in the next bounce
			continue;
		}
		if (ShouldSampleLight(P, N, light, e)) {
			vec3 l = vec3(0);
			if (MIS_enabled) {
				float q = MIS_ratio_default; // controls ratio N_h / N_d (hemisphere samples to direct light samples)
				if (MIS_light_dist_enabled && MIS_light_dist_max > 0.0) {
					float dmax = MIS_light_dist_max;
					float dmin = min(dmax - 0.0001, MIS_light_dist_min);
					float ds = 1.0/(dmin - dmax);
					float d0 = -ds*dmax;
					q = clamp(DistanceToObject(P, light)*ds + d0, 0.0, 1.0);
				}
				if (MIS_light_dist_dbg)
					e += vec3(q*q);
				int N_h = int(0.5 + float(numLightSamples)*q); // [0..numLightSamples]
				int N_d = numLightSamples - N_h; // [0..numLightSamples]
				for (int i = 0; i < N_d; i++) {
					vec2 s = haltonEnabled ? fract(offset + Halton23(i + frameIndex*N_d)) : rand2(seed);
					l += SampleLightMIS_d(light, lightId, objId, P, N, s, float(N_d), float(N_h), sphericalLightIsTextured);
				}
				for (int i = 0; i < N_h; i++) {
					vec2 s = haltonEnabled ? fract(offset + Halton23(i + frameIndex*N_h)) : rand2(seed);
					l += SampleLightMIS_h(light, lightId, objId, P, N, s, float(N_d), float(N_h));
				}
			} else {
				for (int i = 0; i < NO_UNROLL_(numLightSamples, objId); i++) {
					vec2 s = haltonEnabled ? fract(offset + Halton23(i + frameIndex*numLightSamples)) : rand2(seed);
					l += SampleLight(light, lightId, objId, P, N, s, sphericalLightIsTextured);
				}
				l /= float(numLightSamples);
			}
			e += l;
			wasSampled |= (1U << lightId);
		}
	}
	return e;
}

#if LIGHTMAP
vec2 ComputeLightmapUV(Object obj, vec3 V, vec2 lightmapSize)
{
	vec2 lightmapSizeInv = 1.0/lightmapSize; // we could just pass this in ..
	vec2 lightmapUV = vec2(-1);
	if (IsQuad(obj)) {
		vec2 st;
		st.x = dot(V, obj.quadBasisX);
		st.y = dot(V, obj.quadBasisY);
		vec2 uv = st*0.5 + vec2(0.5); // [0..1]
		vec4 atlasBounds = obj.lightmapBounds;
		atlasBounds.zw -= atlasBounds.xy; // width, height
		if (float(LIGHTMAP_QUAD_INSET) < 0.5) { // don't sample outside the lightmap bounds (if we are filtering)
			vec2 uvmin = vec2(0.5)/atlasBounds.zw;
			vec2 uvmax = vec2(1) - uvmin;
			uv = clamp(uv, uvmin, uvmax);
		}
		atlasBounds *= lightmapSizeInv.xyxy;
		lightmapUV = atlasBounds.xy + atlasBounds.zw*uv;
	} else {
		int faceRow;
		int faceCol;
		vec2 facePos;
		vec3 Va = abs(V);
		float Vamax = max(max(Va.x, Va.y), Va.z);
		if (Vamax == Va.x) {
			faceCol = 0;
			faceRow = V.x < 0.0 ? 1 : 0;
			facePos = V.yz/Va.x;
		} else if (Vamax == Va.y) {
			faceCol = 1;
			faceRow = V.y < 0.0 ? 1 : 0;
			facePos = V.zx/Va.y;
		} else { // Vamax == Va.z
			faceCol = 2;
			faceRow = V.z < 0.0 ? 1 : 0;
			facePos = V.xy/Va.z;
		}
		vec2 faceUV = facePos*0.5 + vec2(0.5); // [0..1]
		float faceRes = GetLightmapSphereFaceRes(obj);
		vec2 faceBoundsMin = vec2(faceCol + 0, faceRow + 0)*faceRes + vec2(LIGHTMAP_SPHERE_FACE_INSET);
		vec2 faceBoundsMax = vec2(faceCol + 1, faceRow + 1)*faceRes - vec2(LIGHTMAP_SPHERE_FACE_INSET);
		vec2 uv = obj.lightmapBounds.xy + faceBoundsMin + (faceBoundsMax - faceBoundsMin)*faceUV;
		lightmapUV = uv*lightmapSizeInv;
	}
	return lightmapUV;
}
#endif // LIGHTMAP

vec3 ComputeRadiance(
	Ray ray,
	int objId,
	int depth,
	int depthMin,
	int depthMax,
	bool diffuseUniformSampling,
#if LIGHTMAP
	int lightmapDepth,
	sampler2D lightmapSampler,
#endif // LIGHTMAP
	bool directLightSampling,
	uint wasSampled)
{
	vec3 acc = vec3(0);
	vec3 mask = vec3(1);
	for (; depth <= NO_UNROLL_(depthMax, objId); depth++) {
		float t;
		Object obj;
		if ((objId = IntersectScene(ray, objId, t, obj)) < 0)
			break;
		vec3 P = ray.origin + t*ray.dir;
	#if LIGHTMAP
		if (depth == lightmapDepth && obj.lightmapBounds != vec4(0) && IsDiffuse(obj)) {
			vec2 lightmapSize = vec2(textureSize(lightmapSampler, 0));
			vec2 lightmapUV = ComputeLightmapUV(obj, P - obj.pos, lightmapSize);
			if (lightmapUV.x != -1.0) {
				acc += mask*texture(lightmapSampler, lightmapUV).rgb;
				break;
			}
		}
	#endif // LIGHTMAP
		vec3 N = GetSurfaceNormal(P, obj);
		if ((wasSampled & (1U << objId)) == 0U && depth >= depthMin && dot(N, ray.dir) < 0.0)
			acc += mask*SampleLightColor(P, obj);
		wasSampled = 0U;
		if (IsDiffuse(obj)) {
			mask *= obj.albedo;
			int depthNext = depth + 1;
			if (directLightSampling && depthNext <= depthMax && depthNext >= depthMin)
				acc += mask*SampleLightsInScene(
					P,
					N,
					false, // haltonEnabled
					vec2(0), // offset
					objId,
					0, // frameIndex
					NUM_INDIRECT_LIGHT_SAMPLES, // numLightSamples
					wasSampled);
			vec3 rayDir;
			vec2 s = rand2(seed);
			if (diffuseUniformSampling) {
				rayDir = SampleHemisphereUniform(N, s);
				mask *= dot(N, rayDir)*2.0; // why 2.0?
			} else
				rayDir = SampleHemisphereCosineWeighted(N, s);
			ray = Ray(P, rayDir);
		}
	#if !DIFFUSE_ONLY
		else if (obj.materialType == MATERIAL_TYPE_SPECULAR) {
			mask *= obj.albedo;
			ray = Ray(P, reflect(ray.dir, N));
		} else { // MATERIAL_TYPE_REFRACTIVE
			float a = dot(N, ray.dir);
			float ddn = abs(a);
			float nc = 1.0;
			float nt = 1.5;
			float nnt = mix(nc/nt, nt/nc, float(a > 0.0));
			float cos2t = 1.0 - nnt*nnt*(1.0 - ddn*ddn);
			ray = Ray(P, reflect(ray.dir, N));
			if (cos2t > 0.0) {
				vec3 tdir = normalize(ray.dir*nnt + sign(a)*N*(ddn*nnt + sqrt(cos2t)));
				float R0 = (nt - nc)/(nt + nc);
				R0 *= R0;
				float c = 1.0 - mix(ddn, dot(tdir, N), float(a > 0.0));
				float Re = R0 + (1.0 - R0)*c*c*c*c*c;
				float _P = 0.25 + 0.5*Re;
				float RP = Re/_P;
				float TP = (1.0 - Re)/(1.0 - _P);
				if (rand(seed) < _P)
					mask *= RP;
				else {
					mask *= obj.albedo*TP;
					ray = Ray(P, tdir);
				}
			}
		}
	#endif // !DIFFUSE_ONLY
	}
	return acc;
}