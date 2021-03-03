#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

struct DirectionalLight 
{
	vec3 direction;
	int enabled;
	vec3 colour;
	float brightness;
	int castShadows;
	float shadowDarkness;
	vec2 _pad;
};

struct PointLight 
{
	vec3 position;
	int enabled;
	vec3 colour;
	float brightness;
};

// Updated once per frame
layout (binding = 0) uniform UBOConstant
{
	mat4 view;
	mat4 viewProjection;
	float time;
	// vec4 screenSize; // (w, h, 1/w, 1/h)
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;
} uboDynamic;

layout (location = 0) in vec4 ex_PositionOS;

layout (location = 0) out vec4 outColour;

float sdf(vec3 p)
{
	p /=  uboDynamic.model[0][0];
	p.xy *= sin(p.z * 15.0 + uboConstant.time * 4.5) * 0.09 + 0.85;
	p.y *= cos((p.y+p.z) * 12.3 + uboConstant.time * 3.6) * 0.07 + 0.85;
	p.x *= cos((p.x) * 9.3 + uboConstant.time * 2.6) * 0.02 + 0.98;
	p.z = mix(p.z, p.z * p.y, 0.3);

	float radius = 0.5;
	float dist = length(p) - radius;

	return dist;
}

// Use finite differences to compute surface normal
vec3 calcNormal(vec3 pos)
{
	float center = sdf(pos);
	vec2 epsillon_zero = vec2(0.001, 0.0);
	return normalize(vec3(
		sdf(pos + epsillon_zero.xyy),
		sdf(pos + epsillon_zero.yxy),
		sdf(pos + epsillon_zero.yyx)) - center);
}

vec4 raymarch(vec3 rayOrigin, vec3 rayDir)
{
	vec4 result = vec4(0);

	vec3 lightDir = normalize(vec3(1.0, -0.9, -0.2));

	const int stepCount = 256;
	float tmin = 0.0;
	float tmax = 10000.0;
	float t = tmin;
	for (int i = 0; i < stepCount; ++i)
	{
		float dist = sdf(rayOrigin + t * rayDir);
		result.xyz = dist.xxx;
		// result.w = 1.0;
		// break;
		if (dist < 0.001 * t)
		{
			vec3 N = calcNormal(rayOrigin + t * rayDir);
			//result.xyz = (N) * 0.5 + 0.5;
			float d = dot(N, -lightDir);
			float light = d * 0.5 + 0.5;
			vec3 diffuse = mix(max(d, 0.0), light, 0.05) * vec3(0.05, 0.64, 0.15);
			vec3 sky = (dot(N, vec3(0, 1, 0)) * 0.5 + 0.5) * vec3(0.02, 0.06, 0.3);
			result.xyz = diffuse + sky * 0.5;
			result.w = 1.0;
			break;
		}

		t += dist;

		if (t > tmax)
		{
			break;
		}
	}

	return result;
}

void main()
{
	vec4 res = vec4(1842, 1057, 1.0/1842, 1.0/1057);
	// Get screen coord in range [-1, 1]
	vec2 screenCoordN = (2.0 * gl_FragCoord.xy - res.xy) * res.zw;// uboConstant.screenSize.zw;
	screenCoordN.y = -screenCoordN.y;

	//outColour = vec4(0.9, 0.1, 0.1, 1.0);
	// outColour = vec4(abs(ex_PositionOS.xyz), 1.0);

	vec3 rayDirWS = vec3(inverse(mat3(uboConstant.view)) * vec3(screenCoordN, 1.0));
	//outColour = vec4((rayDirWS.z > 0.0 ? 1.0 : 0.0).xxx, 1.0);
	 // outColour = vec4(max(rayDirWS, 0.0), 1);
	 // return;
	vec4 col = raymarch(ex_PositionOS.xyz, rayDirWS);

    // Contrast
    float constrast = 0.3;
    outColour = mix(outColour, smoothstep(0.0, 1.0, outColour), constrast);
    
    // Colour mapping
    outColour *= vec4(0.90,0.96,1.1,1.0);

    // Gamma correction
	col = vec4(pow(col.xyz, vec3(0.4545)), col.w);
	outColour = col;
}
