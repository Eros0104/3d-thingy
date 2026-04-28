$input v_normal, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_albedo, 0);
uniform vec4 u_baseColor;

void main()
{
	vec3 N = normalize(v_normal);
	vec4 tex = texture2D(s_albedo, v_texcoord0);
	vec3 albedo = tex.xyz * u_baseColor.xyz;

	// Two-light rim setup so the viewmodel reads well in any scene without
	// pulling from world lights. Key from upper-front-right, fill from below-left.
	vec3 keyDir = normalize(vec3(0.4, 0.8, -0.5));
	vec3 fillDir = normalize(vec3(-0.6, -0.2, 0.3));
	float key = max(dot(N, keyDir), 0.0);
	float fill = max(dot(N, fillDir), 0.0);
	float wrap = 0.5 + 0.5 * key;
	vec3 lit = albedo * (0.35 + 0.85 * wrap + 0.25 * fill);
	gl_FragColor = vec4(lit, tex.w * u_baseColor.w);
}
