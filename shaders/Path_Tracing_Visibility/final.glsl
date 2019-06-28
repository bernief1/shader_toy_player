//$INPUT:[KEYBOARD2]
//$INPUT:control
//$INPUT:lightmap
//$INPUT:lightmapvis
//$INPUT:scene
//$INPUT:variance
//$INPUT:texelIDs

float LinearToSRGB(float value)
{
	if (value <= 0.0031308)
		return value*12.92;
	else
		return 1.055*pow(value, 1.0/2.4) - 0.055;
}

vec3 LinearToSRGB(vec3 rgb)
{
	return vec3(LinearToSRGB(rgb.r), LinearToSRGB(rgb.g), LinearToSRGB(rgb.b));
}

float SRGBtoLinear(float value)
{
	if (value <= 0.04045)
		return value/12.92;
	else
		return pow((value + 0.055)/1.055, 2.4);
}

vec3 SRGBtoLinear(vec3 rgb)
{
	return vec3(SRGBtoLinear(rgb.r), SRGBtoLinear(rgb.g), SRGBtoLinear(rgb.b));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
	vec3 col;
#if LIGHTMAP
	ivec2 lightmapUV = ivec2(fragCoord*0.5);
	ivec2 lightmapSize = textureSize(lightmap, 0);
	bool lightmapVisualize = all(lessThan(lightmapUV, vec2(lightmapSize)));
	if (IS_KEY_TOGGLED(KEY_V) && lightmapVisualize) // 'V' = visualize lightmap
		col = texelFetch(lightmap, lightmapUV, 0).rgb;
	else
#endif // LIGHTMAP
		col = texture(scene, fragCoord/iResolution.xy).rgb;

	if (IS_KEY_TOGGLED(KEY_Z)) {
		float variance = texture(variance, fragCoord/iResolution.xy).b;
		//col = 500.0*variance*vec3(1,0,0);
		if (variance > 0.0001)
			col = vec3(1,0,0);
	}

#if LIGHTMAP
	// visualize texel visibility
	if (IS_KEY_TOGGLED(KEY_B)) {
		if (IS_KEY_TOGGLED(KEY_V)) { // visible texels
			if (lightmapVisualize) {
				float intensity = 0.0;
				int visibleFrame = texelFetch(lightmapvis, lightmapUV, 0).x;
				int currentFrame = iFrame + 1;
				if (visibleFrame == currentFrame)
					col = vec3(1,0,0);
			}
		} else { // texelIDs
			uvec4 texelID = texelFetch(texelIDs, lightmapUV, 0);
			if (texelID.w > 0U)
				col = vec3(texelID.xyz)/vec3(lightmapSize, NUM_OBJECTS);
		}
	}
#endif // LIGHTMAP

	col = LinearToSRGB(col);

	// show some toggles ..
	float r = 10.0;
	float x = 24.0;
	if (IS_KEY_NOT_TOGGLED(KEY_H)) col = mix(col, vec3(1,0,0), max(0.0, 1.0 - length(fragCoord - iResolution.xy + vec2(x,24))/r)); x += 24.0;
	if (IS_KEY_NOT_TOGGLED(KEY_E)) col = mix(col, vec3(0,1,0), max(0.0, 1.0 - length(fragCoord - iResolution.xy + vec2(x,24))/r)); x += 24.0;
	if (IS_KEY_NOT_TOGGLED(KEY_U)) col = mix(col, vec3(0,0,1), max(0.0, 1.0 - length(fragCoord - iResolution.xy + vec2(x,24))/r)); x += 24.0;

	fragColor = vec4(col, 1.0);
}