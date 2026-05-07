$input v_color0, v_normal, v_texcoord0, v_worldPos

#include <bgfx_shader.sh>

SAMPLER2D(s_albedo,    0);
SAMPLER2D(s_normal,    1);
SAMPLER2D(s_roughness, 2);

uniform vec4 u_lightPos[8];
uniform vec4 u_lightColor[8];
uniform vec4 u_lightParams;
uniform vec4 u_ambient;
uniform vec4 u_cameraPos;
uniform vec4 u_wallSegs[128]; // each: (ax, az, bx, bz) in XZ plane
uniform vec4 u_wallParams;    // x = number of active wall segments

// Returns true if the segment P->Q is blocked by wall segment A->B (XZ plane).
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
	vec2 uv = v_texcoord0;

	// --- Tangent frame derived from geometry normal ---
	// Works for flat walls (N horizontal) and floors/ceilings (N vertical).
	// Matches the UV layout: floor u=world_x, v=world_z; wall u=along_wall, v=world_y.
	vec3 geoN = normalize(v_normal);
	vec3 T, B;
	if (abs(geoN.y) > 0.5) {
		// Floor / ceiling
		T = vec3(1.0, 0.0, 0.0);
		B = vec3(0.0, 0.0, 1.0);
	} else {
		// Wall: T along the wall direction, B up
		T = vec3(geoN.z, 0.0, -geoN.x);
		B = vec3(0.0, 1.0, 0.0);
	}
	mat3 TBN = mat3(T, B, geoN);

	// Sample normal map and bring into world space
	vec3 tsNormal = texture2D(s_normal, uv).xyz * 2.0 - 1.0;
	vec3 N = normalize(mul(TBN, tsNormal));

	// Roughness (0 = mirror, 1 = fully diffuse)
	float roughness = texture2D(s_roughness, uv).r;
	float shininess = (1.0 - roughness) * (1.0 - roughness) * 64.0 + 1.0;

	vec3 texRgb = texture2D(s_albedo, uv).xyz;
	vec3 albedo = texRgb * v_color0.xyz;
	vec3 ambient = u_ambient.xyz * albedo;
	vec3 diffuseAccum = vec3(0.0, 0.0, 0.0);

	vec2 fragXZ = vec2(v_worldPos.x, v_worldPos.z);
	vec3 V = normalize(u_cameraPos.xyz - v_worldPos);
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

		// 2D wall occlusion test in the XZ plane
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

		// Blinn-Phong specular modulated by roughness
		vec3 H = normalize(L + V);
		float spec = pow(max(dot(N, H), 0.0), shininess) * (1.0 - roughness) * 0.4;

		diffuseAccum += (ndl * albedo + spec) * u_lightColor[i].xyz * atten;
	}

	gl_FragColor = vec4(ambient + diffuseAccum, texture2D(s_albedo, uv).a * v_color0.w);
}
