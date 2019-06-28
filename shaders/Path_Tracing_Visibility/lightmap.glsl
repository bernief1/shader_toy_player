//$OUTPUT:lightmap
//$INPUT:[KEYBOARD2]
//$INPUT:control
//$INPUT:lightmap
//$INPUT:lightmapvis
//$INPUT:noise
//$INPUT:passionflower

#if LIGHTMAP_SPHERE_ATLAS
vec4 ColorizeSphereFace(int faceRow, int faceCol)
{
	vec4 colorize = vec4(0,0,0,1);
	if (faceCol == 0)
		colorize.r = 1.0;
	else if (faceCol == 1)
		colorize.g = 1.0;
	else // faceCol == 2
		colorize.b = 1.0;
	if (faceRow == 1)
		colorize.rgb = vec3(1) - colorize.rgb; // cyan,magenta,yellow
	colorize.rgb += (vec3(1) - colorize.rgb)*0.2; // whiten
	return colorize;
}
#endif // LIGHTMAP_SPHERE_ATLAS

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
	vec3 color = vec3(0,0,0.25); // lightmap atlas background
	vec4 prev = texelFetch(lightmap, ivec2(fragCoord), 0);
	float currFrame = prev.a;
#if LIGHTMAP
	if (IS_KEY_TOGGLED(KEY_L)) {
		ivec2 controlSamplerRes = textureSize(control, 0);
		ivec2 frameControlCoord = controlSamplerRes - ivec2(1,1);
		vec3 frameControl = texelFetch(control, frameControlCoord, 0).xyz;
		vec2 frames = max(vec2(0), float(iFrame) - frameControl.xy);
		bool firstFrame = frames.y == 0.0;
		bool updateVisibleOnly = IS_KEY_TOGGLED(KEY_X);
		if (firstFrame)
			currFrame = 0.0;
		else if (updateVisibleOnly && texelFetch(lightmapvis, ivec2(fragCoord), 0).x != iFrame + 1)
			discard; // early-out texels that aren't visible

		INIT_SCENE();

		vec2 offset = texelFetch(noise, ivec2(fragCoord)&255, 0).xy;
		int objId = OBJ_ID_NONE;
		Object obj;
		for (int i = 0; i < NO_UNROLL(NUM_OBJECTS); i++) {
			obj = objects[i];
			if (fragCoord.x >= obj.lightmapBounds.x &&
				fragCoord.y >= obj.lightmapBounds.y &&
				fragCoord.x <= obj.lightmapBounds.z &&
				fragCoord.y <= obj.lightmapBounds.w) {
				objId = i;
				break;
			}
		}
		if (objId != OBJ_ID_NONE && IsDiffuse(obj)) {
			vec4 colorize = vec4(0);
			vec3 P;
			if (IsQuad(obj)) {
				vec3 bx = GetQuadBasisExtentX(obj);
				vec3 by = GetQuadBasisExtentY(obj);
				vec4 bounds = obj.lightmapBounds;
				vec2 uv = (fragCoord.xy - bounds.xy)/(bounds.zw - bounds.xy);
				vec2 st = uv*2.0 - vec2(1);
				P = obj.pos + st.x*bx + st.y*by;
			} else { 
				vec2 sphereAtlasUV = fragCoord.xy - obj.lightmapBounds.xy;
				float faceRes = GetLightmapSphereFaceRes(obj);
				int faceRow = int(sphereAtlasUV.y/float(faceRes));
				int faceCol = int(sphereAtlasUV.x/float(faceRes));
				vec2 faceBoundsMin = vec2(faceCol + 0, faceRow + 0)*faceRes + vec2(LIGHTMAP_SPHERE_FACE_INSET);
				vec2 faceBoundsMax = vec2(faceCol + 1, faceRow + 1)*faceRes - vec2(LIGHTMAP_SPHERE_FACE_INSET);
				vec2 faceUV = (sphereAtlasUV - faceBoundsMin)/(faceBoundsMax - faceBoundsMin);
				vec3 V = normalize(vec3((faceRow == 1) ? -1 : 1, faceUV*2.0 - vec2(1)));
				if (faceCol == 1)
					V = V.zxy;
				else if (faceCol == 2)
					V = V.yzx;
				P = obj.pos + V*obj.radius;
				if (IS_KEY_TOGGLED(KEY_C))
					colorize = ColorizeSphereFace(faceRow, faceCol);
			}
			vec3 N = GetSurfaceNormal(P, obj);
			vec3 e = vec3(0);
			uint wasSampled = 0U;
			if (directLightSampling && minDepth <= 1)
				e = SampleLightsInScene(
					P,
					N,
					haltonEnabled,
					offset,
					objId,
					iFrame,
					NUM_DIRECT_LIGHT_SAMPLES,
					wasSampled);
			color = vec3(0);
			for (int i = 0; i < NO_UNROLL(NUM_PRIMARY_RAY_SAMPLES); i++) {
				vec2 s = haltonEnabled ? fract(offset + Halton23(i + iFrame*NUM_PRIMARY_RAY_SAMPLES)) : rand2(seed);
				vec3 rayDir; // diffuse only!
				float mask = 1.0;
				if (diffuseUniformSampling) {
					rayDir = SampleHemisphereUniform(N, s);
					mask = dot(N, rayDir)*2.0; // why 2.0?
				} else
					rayDir = SampleHemisphereCosineWeighted(N, s);
				color += mask*ComputeRadiance(
					Ray(P, rayDir),
					objId,
					1, // depth (starts at 1)
					minDepth,
					maxDepth,
					diffuseUniformSampling,
				#if LIGHTMAP
					-1, // lightmapDepth
					lightmap,
				#endif // LIGHTMAP
					directLightSampling,
					wasSampled);
			}
			color /= float(NUM_PRIMARY_RAY_SAMPLES);
			color += e;
		#if LIGHTMAP_SPHERE_ATLAS
			color += (colorize.rgb - color)*colorize.a;
		#endif // LIGHTMAP_SPHERE_ATLAS

			if (currFrame > 0.0) {
				if (currFrame < float(LAST_FRAME)) {
					float accum = 1.0/(currFrame + 1.0);
					color = color*accum + prev.rgb*(1.0 - accum);
				} else
					color = prev.rgb;
			}
			currFrame += 1.0;
		}
	}
#endif // LIGHTMAP

	fragColor = vec4(color, currFrame);
}