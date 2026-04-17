$input v_color0, v_normal, v_texcoord0, v_worldPos

#include <bgfx_shader.sh>

SAMPLER2D(s_albedo, 0);

uniform vec4 u_lightPos[8];
uniform vec4 u_lightColor[8];
uniform vec4 u_lightParams;
uniform vec4 u_ambient;

void main()
{
	vec3 N = normalize(v_normal);
	vec3 texRgb = texture2D(s_albedo, v_texcoord0).xyz;
	vec3 albedo = texRgb * v_color0.xyz;
	vec3 ambient = u_ambient.xyz * albedo;
	vec3 diffuseAccum = vec3(0.0, 0.0, 0.0);

	for (int i = 0; i < 8; i++)
	{
		if (float(i) >= u_lightParams.x) {
			break;
		}

		vec3 toLight = u_lightPos[i].xyz - v_worldPos;
		float distSq = dot(toLight, toLight);
		vec3 L = normalize(toLight);
		// Two-sided lighting: walls are zero-thickness so we light whichever face is nearer.
		float ndl = abs(dot(N, L));
		float atten = 1.0 / (1.0 + 0.15 * distSq);
		diffuseAccum += ndl * u_lightColor[i].xyz * albedo * atten;
	}

	gl_FragColor = vec4(ambient + diffuseAccum, v_color0.w);
}
