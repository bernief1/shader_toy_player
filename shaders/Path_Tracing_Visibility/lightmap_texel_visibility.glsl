//$OUTPUT:texelIDs
//$INPUT:[KEYBOARD2]
//$INPUT:texelIDs
//$IMAGE:lightmapvis, width=256, height=256, format=R32_SINT, write=TRUE, filter=FALSE

void mainImage(out uvec4 fragColor, in vec2 fragCoord)
{
#if LIGHTMAP
	bool update = IS_KEY_TOGGLED(KEY_X) && IS_KEY_TOGGLED(KEY_L);
	if (update) {
		uvec4 texelID = texelFetch(texelIDs, ivec2(fragCoord), 0);
		if (texelID.w > 0U)
			imageStore(lightmapvis_IMAGE, ivec2(texelID.xy), ivec4(iFrame + 1));
	}
#endif // LIGHTMAP
	discard;
}