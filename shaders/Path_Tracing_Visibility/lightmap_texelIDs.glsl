//$OUTPUT:texelIDs, relative_width=0.25, relative_height=0.25, format=R16G16B16A16_UINT
//$INPUT:[KEYBOARD2]
//$INPUT:control
//$INPUT:lightmap, width=256, height=256, format=R32G32B32A32_FLOAT, filter=TRUE

void mainImage(out uvec4 fragColor, in vec2 fragCoord)
{
#if LIGHTMAP
	bool update = IS_KEY_TOGGLED(KEY_X) && IS_KEY_TOGGLED(KEY_L);
	if (update) {
		INIT_SCENE();

		ivec2 controlSamplerRes = textureSize(control, 0);
	//	ivec2 frameControlCoord = controlSamplerRes - ivec2(1,1);
		ivec2 mouseControlCoord = controlSamplerRes - ivec2(2,1);
		ivec2 camPosCoord       = controlSamplerRes - ivec2(3,1);
		ivec2 camRotCoord       = controlSamplerRes - ivec2(4,1);

	//	vec3 frameControl = texelFetch(control, frameControlCoord, 0).xyz;
	//	vec3 mouseControl = texelFetch(control, mouseControlCoord, 0).xyz;
		vec3 camPos       = texelFetch(control, camPosCoord, 0).xyz;
		vec3 camRot       = texelFetch(control, camRotCoord, 0).xyz; // yaw,pitch,unused
		mat3 camBasis     = CreateCameraBasis(camRot);
	
	//#if SCREEN_JITTER
	//	vec2 screenJitter = fract(iTime*vec2(23.75310853, 21.95340893)) - 0.5; // [-0.5..0.5], from https://www.shadertoy.com/view/MdKyRK
	//#else
		vec2 screenJitter = vec2(0);
	//#endif
		vec2 sceneRes = iOutputResolution.xy; // resolution of this buffer
		vec2 screenPos = 2.0*(fragCoord + screenJitter)/sceneRes - 1.0; // [-1..1]
		Ray cameraRay;
		cameraRay.origin = camPos + vec3(50.0, 40.8, 139.0);
		cameraRay.dir = camBasis*CreateScreenRay(screenPos, sceneRes.x/sceneRes.y);

		float t;
		Object obj;
		int objId = IntersectScene(cameraRay, -1, t, obj);
		if (objId != -1 && obj.lightmapBounds != vec4(0)) {
			vec3 P = cameraRay.origin + t*cameraRay.dir;
			vec2 lightmapSize = vec2(textureSize(lightmap, 0));
			vec2 lightmapUV = ComputeLightmapUV(obj, P - obj.pos, lightmapSize);
			fragColor.xy = uvec2(lightmapUV*lightmapSize);
			fragColor.z = uint(objId);
		} else
			fragColor.xyz = uvec3(0);
		fragColor.w = 1U;
	} else
#endif // LIGHTMAP
		fragColor = uvec4(0);
}