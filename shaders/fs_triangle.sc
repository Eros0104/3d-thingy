$input v_color0, v_normal, v_texcoord0, v_worldPos

#include <bgfx_shader.sh>

SAMPLER2D(s_albedo, 0);

uniform vec4 u_lightPos;
uniform vec4 u_lightColor;
uniform vec4 u_ambient;

void main()
{
	vec3 N = normalize(v_normal);
	vec3 toLight = u_lightPos.xyz - v_worldPos;
	float distSq = dot(toLight, toLight);
	vec3 L = normalize(toLight);
	float ndl = max(dot(N, L), 0.0);
	float atten = 1.0 / (1.0 + 0.15 * distSq);
	vec3 texRgb = texture2D(s_albedo, v_texcoord0).xyz;
	vec3 albedo = texRgb * v_color0.xyz;
	vec3 ambient = u_ambient.xyz * albedo;
	vec3 diffuse = ndl * u_lightColor.xyz * albedo * atten;
	gl_FragColor = vec4(ambient + diffuse, v_color0.w);
}
