$input a_position, a_normal, a_texcoord0, a_indices, a_weight
$output v_normal, v_texcoord0

#include <bgfx_shader.sh>

uniform mat4 u_bones[120];

void main()
{
	// a_indices arrives as 0..1 floats (Uint8 attribute is normalized); rescale to 0..255.
	int i0 = int(a_indices.x * 255.0 + 0.5);
	int i1 = int(a_indices.y * 255.0 + 0.5);
	int i2 = int(a_indices.z * 255.0 + 0.5);
	int i3 = int(a_indices.w * 255.0 + 0.5);

	mat4 skin = u_bones[i0] * a_weight.x
	          + u_bones[i1] * a_weight.y
	          + u_bones[i2] * a_weight.z
	          + u_bones[i3] * a_weight.w;

	vec3 lpos = mul(skin, vec4(a_position, 1.0)).xyz;
	vec3 wpos = mul(u_model[0], vec4(lpos, 1.0)).xyz;
	gl_Position = mul(u_viewProj, vec4(wpos, 1.0));

	vec3 lnrm = mul(skin, vec4(a_normal, 0.0)).xyz;
	v_normal = normalize(mul(u_model[0], vec4(lnrm, 0.0)).xyz);
	v_texcoord0 = a_texcoord0;
}
