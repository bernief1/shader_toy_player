//$OUTPUT:scene, relative_width=0.25, relative_height=0.25, format=R32G32B32_FLOAT
//$INPUT:[KEYBOARD2]
//$INPUT:control
//$INPUT:lightmap
//$INPUT:lightmapvis
//$INPUT:scene
//$INPUT:noise
//$INPUT:passionflower

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
	INIT_SCENE();

	ivec2 controlSamplerRes = textureSize(control, 0);
	ivec2 frameControlCoord = controlSamplerRes - ivec2(1,1);
	ivec2 mouseControlCoord = controlSamplerRes - ivec2(2,1);
	ivec2 camPosCoord       = controlSamplerRes - ivec2(3,1);
	ivec2 camRotCoord       = controlSamplerRes - ivec2(4,1);

	vec3 frameControl = texelFetch(control, frameControlCoord, 0).xyz;
	vec3 mouseControl = texelFetch(control, mouseControlCoord, 0).xyz;
	vec3 camPos       = texelFetch(control, camPosCoord, 0).xyz;
	vec3 camRot       = texelFetch(control, camRotCoord, 0).xyz; // yaw,pitch,unused
	mat3 camBasis     = CreateCameraBasis(camRot);
	
#if SCREEN_JITTER
	vec2 screenJitter = fract(iTime*vec2(23.75310853, 21.95340893)) - 0.5; // [-0.5..0.5], from https://www.shadertoy.com/view/MdKyRK
#else
	vec2 screenJitter = vec2(0);
#endif
	vec2 sceneRes = iOutputResolution.xy; // resolution of this buffer
	vec2 screenPos = 2.0*(fragCoord + screenJitter)/sceneRes - 1.0; // [-1..1]
	Ray cameraRay;
	cameraRay.origin = camPos + vec3(50.0, 40.8, 139.0);
	cameraRay.dir = camBasis*CreateScreenRay(screenPos, sceneRes.x/sceneRes.y);
	vec3 color = vec3(0);

#if LIGHTMAP
//	if (IS_KEY_TOGGLED(KEY_0)) { // TEST LIGHTMAP FINAL GATHER
//		float t;
//		Object obj;
//		int objId = IntersectScene(cameraRay, -1, t, obj);
//		if (objId != -1) {
//			vec3 P = cameraRay.origin + t*cameraRay.dir;
//			vec3 N = GetSurfaceNormal(P, obj);
//			vec3 acc = vec3(0);
//			const int numFinalGatherRays = 16;
//			for (int i = 0; i < numFinalGatherRays; i++) {
//				Ray ray = Ray(P, SampleHemisphereCosineWeighted(N, rand2(seed)));
//				Object obj2;
//				int objId2 = IntersectScene(ray, objId, t, obj2);
//				if (objId2 != -1) {
//					if (obj2.lightmapBounds != vec4(0) && IsDiffuse(obj2)) {
//						vec2 lightmapSize = vec2(textureSize(lightmap, 0));
//						vec2 lightmapUV = ComputeLightmapUV(obj2, ray.origin + t*ray.dir - obj2.pos, lightmapSize);
//						if (lightmapUV.x != -1.0)
//							acc += texture(lightmap, lightmapUV).rgb*obj2.albedo;
//					}
//					//acc += obj2.emissive;
//				}
//			}
//			color = acc/float(numFinalGatherRays);
//		}
//	} else // NOT FINAL GATHER
#endif // LIGHTMAP
	if (bool(DIFFUSE_ONLY)) {
		float t;
		Object obj;
		int objId = IntersectScene(cameraRay, -1, t, obj); // camera ray intersects scene only once
		if (objId != -1) {
			vec2 offset = texelFetch(noise, ivec2(fragCoord)&255, 0).xy;
			vec3 P = cameraRay.origin + t*cameraRay.dir;
			vec3 N = GetSurfaceNormal(P, obj);
		#if LIGHTMAP
			if (lightmapDepth == 0 && obj.lightmapBounds != vec4(0) && IsDiffuse(obj)) {
				vec2 lightmapSize = vec2(textureSize(lightmap, 0));
				vec2 lightmapUV = ComputeLightmapUV(obj, P - obj.pos, lightmapSize);
				//float texelCenterDot = 0.0;
				bool filterLightmap = IS_KEY_NOT_TOGGLED(KEY_F);
				if (!filterLightmap) { // emulate point-sampling
					vec2 lightmapSize = vec2(textureSize(lightmap, 0));
					lightmapUV = lightmapUV*lightmapSize;
			//	#if 0 && defined(_GPU_SHADER_5_)
			//	#if 1 // TODO -- better way to visualize the texel centers? this looks pretty bad ..
			//		vec2 f = fract(lightmapUV);
			//		f *= 4.0*(vec2(1) - f); // [0..1..0]
			//		f.x = min(f.x, f.y);
			//		f.y = fwidthFine(f.x);
			//		texelCenterDot = pow(f.x, 8.0/f.y);
			//	#elif 0
			//		vec2 f = fract(lightmapUV + vec2(0.5));
			//		f *= 4.0*(vec2(1) - f); // [0..1..0]
			//		f.x = max(f.x, f.y);
			//		f.y = fwidthFine(f.x);
			//		texelCenterDot = max(0.0, 1.0 - f.x/f.y);
			//	#endif
			//	#endif // defined(_GPU_SHADER_5_)
					lightmapUV = (floor(lightmapUV) + vec2(0.5))/lightmapSize;
				}
				vec4 lm = texture(lightmap, lightmapUV);
				color = lm.rgb;
				color *= obj.albedo;
				if (IS_KEY_TOGGLED(KEY_N)) {
				#if 0
					if (any(notEqual(textureGather(lightmapvis, lightmapUV, 0), ivec4(iFrame + 1))))
						color = vec3(1,0,0);
				#else
					float frameBeginFade = 3.0;
					float frameEndFade = 100.0;
					float fade = clamp((lm.w - frameBeginFade)/(frameEndFade - frameBeginFade), 0.0, 1.0);
					color = mix(color, vec3(1,0,0), pow(1.0 - fade, 10.0));
				#endif
				}
				//color += texelCenterDot;
			} else
		#endif // LIGHTMAP
			if (IS_KEY_TOGGLED(KEY_P)) { // for reference - analytic area light solution (direct light only, no shadows)
				vec3 e = vec3(0);
				for (int lightId = 0; lightId < NO_UNROLL(NUM_OBJECTS); lightId++) {
					Object light = objects[lightId];
					if (IsLight(light))
						e += LTC_EvaluateDiffuse(P, N, light)*SampleLightColor(light.pos, light);
				}
				color += e;
				color *= obj.albedo;
			} else {
				vec3 e = vec3(0);
				uint wasSampled = 0U;
				if (directLightSampling && minDepth <= 1 && IsDiffuse(obj))
					e = SampleLightsInScene(
						P,
						N,
						haltonEnabled,
						offset,
						objId,
						iFrame,
						NUM_DIRECT_LIGHT_SAMPLES,
						wasSampled);
				for (int i = 0; i < NO_UNROLL(NUM_PRIMARY_RAY_SAMPLES); i++) {
					vec3 rayDir;
					float mask = 1.0;
					if (IsDiffuse(obj)) {
						vec2 s = haltonEnabled ? fract(offset + Halton23(i + iFrame*NUM_PRIMARY_RAY_SAMPLES)) : rand2(seed);
						if (diffuseUniformSampling) {
							rayDir = SampleHemisphereUniform(N, s);
							mask = dot(N, rayDir)*2.0; // why 2.0?
						} else
							rayDir = SampleHemisphereCosineWeighted(N, s);
					}
				#if !DIFFUSE_ONLY
					else
						rayDir = reflect(cameraRay.dir, N);
				#endif // !DIFFUSE_ONLY
					color += mask*ComputeRadiance(
						Ray(P, rayDir),
						objId,
						1, // depth
						minDepth,
						maxDepth,
						diffuseUniformSampling,
					#if LIGHTMAP
						lightmapDepth,
						lightmap,
					#endif // LIGHTMAP
						directLightSampling,
						wasSampled);
				}
				color /= float(NUM_PRIMARY_RAY_SAMPLES);
				color += e;
				color *= obj.albedo;
			}
			if (minDepth == 0 && dot(N, cameraRay.dir) < 0.0)
				color += SampleLightColor(P, obj);
		}
	} else {
		// ==============================================================
		// less efficient codepath - keeping it around for comparison.
		// here we cast multiple rays from the camera in the *same*
		// direction, only diverging once we hit a surface and bounce.
		// the code above has been refactored so that it does one initial
		// scene trace from the camera and then multiple bounces on
		// the first hit.
		// ==============================================================
		for (int i = 0; i < NUM_PRIMARY_RAY_SAMPLES; i++)
			color += ComputeRadiance(
				cameraRay,
				OBJ_ID_NONE,
				0, // depth
				minDepth,
				maxDepth,
				diffuseUniformSampling,
			#if LIGHTMAP
				lightmapDepth,
				lightmap,
			#endif // LIGHTMAP
				directLightSampling,
				0U); // wasSampled
		color /= float(NUM_PRIMARY_RAY_SAMPLES);
	}

#if LIGHTMAP
	if (IS_KEY_TOGGLED(KEY_L)) {
		// can't accum scene if we are potentially updating only visible lightmap texels (?) .. just write it out directly
	} else
#endif // LIGHTMAP
	{
		vec2 frames = max(vec2(0), vec2(iFrame) - frameControl.xy);
		float f = frames.x;
		if (f > 0.0) {
			vec4 prev = texelFetch(scene, ivec2(fragCoord), 0);
			if (f < float(LAST_FRAME)) {
				float accum = 1.0/(f + 1.0);
				color = color*accum + prev.rgb*(1.0 - accum);
			} else
				color = prev.rgb;
		}
	}

	fragColor = vec4(color, 1.0);
}