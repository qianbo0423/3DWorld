varying vec3 dlpos, normal; // world space
varying vec3 eye_norm;

void main()
{
	gl_TexCoord[0]  = gl_MultiTexCoord0;
	vec3 gwdelta    = get_grass_wind_delta(gl_Vertex.xyz, gl_MultiTexCoord0.s);
	eye_norm        = length(gl_Normal) * (gl_NormalMatrix * normalize(normalize(gl_Normal) + gwdelta/height)); // eye space (not normalized), height comes from wind.part
	vec4 vertex     = gl_Vertex + vec4(gwdelta, 0.0);
	gl_Position     = gl_ModelViewProjectionMatrix * vertex;
	normal          = normalize(gl_Normal);
	dlpos           = vertex.xyz;
	gl_FrontColor   = gl_Color;
#ifndef NO_FOG
	gl_FogFragCoord = length((gl_ModelViewMatrix * vertex).xyz);
#endif
} 
