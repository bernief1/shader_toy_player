//$OUTPUT:lightmapvis
//$INPUT:[KEYBOARD2]
//$INPUT:lightmap
//$INPUT:lightmapvis

// ==============================================================
// TODO -- we actually don't need to do 3x3 dilation, we could do
// 2x2 dilation (with a single textureGather) and offset the
// lightmapvis texture by 1/2 texel ..
// ==============================================================

void mainImage(out ivec4 fragColor, in vec2 fragCoord)
{
	int visibleFrame = 0;
#if LIGHTMAP
	visibleFrame = texelFetch(lightmapvis, ivec2(fragCoord), 0).x;
	bool update = IS_KEY_TOGGLED(KEY_X) && IS_KEY_TOGGLED(KEY_L) && IS_KEY_NOT_TOGGLED(KEY_F);
	if (update) {
		ivec4 currentFrame = ivec4(iFrame + 1);
		if (visibleFrame != currentFrame.x && texelFetch(lightmap, ivec2(fragCoord), 0).w != 0.0) {
			ivec4 f0 = ivec4(
				texelFetchOffset(lightmapvis, ivec2(fragCoord), 0, ivec2(-1,-1)).x,
				texelFetchOffset(lightmapvis, ivec2(fragCoord), 0, ivec2( 0,-1)).x,
				texelFetchOffset(lightmapvis, ivec2(fragCoord), 0, ivec2(+1,-1)).x,
				texelFetchOffset(lightmapvis, ivec2(fragCoord), 0, ivec2(-1, 0)).x);
			ivec4 f1 = ivec4(
				texelFetchOffset(lightmapvis, ivec2(fragCoord), 0, ivec2(+1, 0)).x,
				texelFetchOffset(lightmapvis, ivec2(fragCoord), 0, ivec2(-1,+1)).x,
				texelFetchOffset(lightmapvis, ivec2(fragCoord), 0, ivec2( 0,+1)).x,
				texelFetchOffset(lightmapvis, ivec2(fragCoord), 0, ivec2(+1,+1)).x);
			if (any(equal(f0, currentFrame)) ||
				any(equal(f1, currentFrame)))
				visibleFrame = currentFrame.x;
		}
	}
#endif // LIGHTMAP
	fragColor = ivec4(visibleFrame);
}