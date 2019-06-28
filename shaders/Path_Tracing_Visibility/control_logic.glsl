//$OUTPUT:control, width=4, height=1, format=R32G32B32_FLOAT
//$INPUT:[KEYBOARD2]
//$INPUT:control

/*
no direct light sampling at all
	noisy far from light

direct light sampling (uniform over light surface)
	nasty fireflies near light where light touches receivers

direct light sampling only when far from light surface
	my own little experiment, before i had MIS working ..
	overall looks decent, but makes "band" around the light before it converges
	hard to tune - "distance to light surface" doesn't directly relate to variance
	i've removed this codepath for now

multiple importance sampling: uniform over light surface (D1) + cosine-weighted hemisphere (D2)
	default is 50-50 split between sampling distributions (D1 = D2 = N/2)
	note that when D1=N, D2=0 we have direct light sampling (fireflies near light),
	and when D1=0, D2=N we have cosine hemisphere sampling (noisy far from light)
	can also be weighted by distance to light surface, e.g.:
	D1 = N*(A + B*sat((dist - dmin)/(dmax - dmin))), D2 = N - D1

weighted direct light sampling multiplied by analytic (LTC) solid angle
	converges instantly (with just 1 sample) when light is unshadowed and untextured
	problematic .. result is correct when infinite number of samples are taken at once, but heavily biased when only taking a few samples
	problematic .. multiple accumulation passes DO NOT converge to the correct solution
	i've removed this codepath for now
	
spherical rectangle sampling (weighted to PDF over light surface)
	gorgeous!
	problem solved, at least for quad lights.
*/

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
	INIT_SCENE();

	bvec2 frameReset = bvec2(false);
	frameReset.y = IS_KEY_DOWN(KEY_SPACE) || SLIDER_CHANGED;
	if (IS_KEY_PRESSED(KEY_E) || IS_KEY_PRESSED(KEY_H) || IS_KEY_PRESSED(KEY_U))
		frameReset.y = true;
	if (IS_KEY_PRESSED(KEY_I) || IS_KEY_PRESSED(KEY_O))
		frameReset.y = true;
	if (IS_KEY_PRESSED(KEY_P))
		frameReset.y = true;
	if (IS_KEY_PRESSED(KEY_R))
		frameReset.y = true;
#if LIGHTMAP
	if (IS_KEY_PRESSED(KEY_L))
		frameReset.y = true;
	if (lightmapEnabled) {
		if (IS_KEY_PRESSED(KEY_C) || IS_KEY_PRESSED(KEY_N))
			frameReset.y = true;
		if (IS_KEY_PRESSED(KEY_F))
			frameReset.x = true;
	}
#endif // LIGHTMAP

	mat4 camera;
	vec2 frames;
	CAMERA_HANDLER(camera, _KEYBOARD2_, control, frames, frameReset, 1.0);
}