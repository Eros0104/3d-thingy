$input v_normal, v_texcoord0, v_worldPos

#include <bgfx_shader.sh>

SAMPLER2D(s_albedo, 0);

uniform vec4 u_baseColor;
uniform vec4 u_lightPos[8];
uniform vec4 u_lightColor[8];
uniform vec4 u_lightParams;
uniform vec4 u_ambient;
uniform vec4 u_wallSegs[128];
uniform vec4 u_wallParams;

bool wall_blocks(vec2 p, vec2 q, vec2 a, vec2 b) {
	vec2 r = q - p;
	vec2 s = b - a;
	float denom = r.x * s.y - r.y * s.x;
	if (abs(denom) < 0.0001) return false;
	vec2 ap = a - p;
	float t = (ap.x * s.y - ap.y * s.x) / denom;
	float u = (ap.x * r.y - ap.y * r.x) / denom;
	return t > 1e-5 && t < 1.0 && u >= 0.0 && u <= 1.0;
}

void main()
{
	vec3 N = normalize(v_normal);
	vec4 tex = texture2D(s_albedo, v_texcoord0);
	vec3 albedo = tex.xyz * u_baseColor.xyz;
	vec3 ambient = u_ambient.xyz * albedo;
	vec3 diffuseAccum = vec3(0.0, 0.0, 0.0);

	vec2 fragXZ = vec2(v_worldPos.x, v_worldPos.z);
	int nWalls = int(u_wallParams.x);

	for (int i = 0; i < 8; i++)
	{
		if (float(i) >= u_lightParams.x) {
			break;
		}

		vec3 toLight = u_lightPos[i].xyz - v_worldPos;
		float dist = length(toLight);
		float r = max(u_lightPos[i].w, 0.001);
		if (dist >= r) continue;
		float falloff = 1.0 - dist / r;
		float atten = falloff * falloff;

		vec2 lightXZ = vec2(u_lightPos[i].x, u_lightPos[i].z);
		bool occluded = false;
		for (int w = 0; w < 128; w++) {
			if (w >= nWalls) break;
			if (wall_blocks(fragXZ, lightXZ, u_wallSegs[w].xy, u_wallSegs[w].zw)) {
				occluded = true;
				break;
			}
		}
		if (occluded) continue;

		vec3 L = normalize(toLight);
		float ndl = max(dot(N, L), 0.0);
		diffuseAccum += ndl * u_lightColor[i].xyz * albedo * atten;
	}

	gl_FragColor = vec4(ambient + diffuseAccum, tex.w * u_baseColor.w);
}
