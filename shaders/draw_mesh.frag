uniform sampler2D tex0, tex1;
varying vec4 epos;
varying vec3 dlpos;
varying vec3 normal; // world space
varying vec3 eye_norm;

void main()
{
	vec4 lit_color = gl_Color * gl_LightModel.ambient;
	if (enable_light0) lit_color += add_light_comp_pos_smap_light0(eye_norm, epos);
	if (enable_light1) lit_color += add_light_comp_pos_smap_light1(eye_norm, epos);
	lit_color = clamp(lit_color, 0.0, 1.0);
	if (enable_dlights) lit_color.rgb += add_dlights(dlpos, normalize(normal), gl_ModelViewMatrixInverse[3].xyz, vec3(1.0)); // dynamic lighting

	lit_color *= texture2D(tex0, gl_TexCoord[0].st);
#ifdef HAVE_DETAIL_TEXTURE
	lit_color *= texture2D(tex1, gl_TexCoord[1].st);
#endif
	vec4 color = vec4(lit_color.rgb, gl_Color.a);
#ifndef NO_FOG
	color = apply_fog(color);
#endif
	gl_FragColor = color;
}
