$input a_position, a_normal, a_color0
$output v_color0, v_normal, v_worldPos

#include <bgfx_shader.sh>

void main()
{
	vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_worldPos = worldPos.xyz;
	v_normal = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
	v_color0 = a_color0;
}
