$input a_position, a_normal, a_texcoord0, a_color0
$output v_color0, v_normal, v_texcoord0, v_worldPos

#include <bgfx_shader.sh>

void main()
{
	vec3 wpos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
	gl_Position = mul(u_viewProj, vec4(wpos, 1.0));
	v_worldPos = wpos;
	v_normal = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
	v_texcoord0 = a_texcoord0;
	v_color0 = a_color0;
}
