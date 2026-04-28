$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_albedo, 0);

void main()
{
	float a = texture2D(s_albedo, v_texcoord0).x;
	gl_FragColor = vec4(v_color0.xyz, v_color0.w * a);
}
