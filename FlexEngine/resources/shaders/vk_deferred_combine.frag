#version 450

#include "vk_misc.glsl"

// Deferred PBR Combine

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 fragColour;

layout (binding = 0) uniform UBOConstant
{
	vec4 camPos;
	mat4 invView;
	mat4 invProj;
	DirectionalLight dirLight;
	PointLight pointLights[NUM_POINT_LIGHTS];
	SpotLight spotLights[NUM_SPOT_LIGHTS];
	AreaLight areaLights[NUM_AREA_LIGHTS];
	SkyboxData skyboxData;
	ShadowSamplingData shadowSamplingData;
	SSAOSamplingData ssaoData;
	float zNear;
	float zFar;
} uboConstant;

mat3 identity33()
{
    return mat3(1);
}

mat3 mat3_from_columns(vec3 c0, vec3 c1, vec3 c2)
{
    mat3 m = mat3(c0, c1, c2);
    return m;
}

mat3 mat3_from_rows(vec3 c0, vec3 c1, vec3 c2)
{
    mat3 m = mat3(c0, c1, c2);
    m = transpose(m);
    return m;
}

vec2 LTC_Coords(float cosTheta, float roughness)
{
    float theta = acos(cosTheta);
    vec2 coords = vec2(roughness, theta / (0.5 * PI));

    const float LUT_SIZE = 32.0;
    const float LUT_BIAS = 0.5 / LUT_SIZE;
    // Scale and bias coordinates, for correct filtered lookup
    coords *= (LUT_SIZE - 1.0) / LUT_SIZE + LUT_BIAS;

    return coords;
}

mat3 LTC_Matrix(sampler2D texLSDMat, vec2 coord)
{
    // load inverse matrix
    vec4 t = texture(texLSDMat, coord);
    mat3 Minv = mat3_from_columns(
        vec3(1.0, 0.0, t.y),
        vec3(0.0, t.z, 0.0),
        vec3(t.w, 0.0, t.x)
    );

    return Minv;
}

float IntegrateEdge2(vec3 v1, vec3 v2)
{
    float x = dot(v1, v2);
    float y = abs(x);

    float a = 0.8543985 + (0.4965155 + 0.0145206*y)*y;
    float b = 3.4175940 + (4.1616724 + y)*y;
    float v = a / b;

    float theta_sintheta = (x > 0.0) ? v : 0.5*inversesqrt(max(1.0 - x*x, 1e-7)) - v;

    return cross(v1, v2).z*theta_sintheta;
}

float IntegrateEdge(vec3 v1, vec3 v2)
{
    float cosTheta = dot(v1, v2);
    cosTheta = clamp(cosTheta, -0.9999, 0.9999);

    float theta = acos(cosTheta);    
    float res = cross(v1, v2).z * theta / sin(theta);

    return res;
}

void ClipQuadToHorizon(inout vec3 L[5], out int n)
{
    // detect clipping config
    int config = 0;
    if (L[0].z > 0.0) config += 1;
    if (L[1].z > 0.0) config += 2;
    if (L[2].z > 0.0) config += 4;
    if (L[3].z > 0.0) config += 8;

    // clip
    n = 0;

    if (config == 0)
    {
        // clip all
    }
    else if (config == 1) // V1 clip V2 V3 V4
    {
        n = 3;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[3].z * L[0] + L[0].z * L[3];
    }
    else if (config == 2) // V2 clip V1 V3 V4
    {
        n = 3;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    }
    else if (config == 3) // V1 V2 clip V3 V4
    {
        n = 4;
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
        L[3] = -L[3].z * L[0] + L[0].z * L[3];
    }
    else if (config == 4) // V3 clip V1 V2 V4
    {
        n = 3;
        L[0] = -L[3].z * L[2] + L[2].z * L[3];
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
    }
    else if (config == 5) // V1 V3 clip V2 V4) impossible
    {
        n = 0;
    }
    else if (config == 6) // V2 V3 clip V1 V4
    {
        n = 4;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    }
    else if (config == 7) // V1 V2 V3 clip V4
    {
        n = 5;
        L[4] = -L[3].z * L[0] + L[0].z * L[3];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    }
    else if (config == 8) // V4 clip V1 V2 V3
    {
        n = 3;
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
        L[1] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] =  L[3];
    }
    else if (config == 9) // V1 V4 clip V2 V3
    {
        n = 4;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[2].z * L[3] + L[3].z * L[2];
    }
    else if (config == 10) // V2 V4 clip V1 V3) impossible
    {
        n = 0;
    }
    else if (config == 11) // V1 V2 V4 clip V3
    {
        n = 5;
        L[4] = L[3];
        L[3] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    }
    else if (config == 12) // V3 V4 clip V1 V2
    {
        n = 4;
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
    }
    else if (config == 13) // V1 V3 V4 clip V2
    {
        n = 5;
        L[4] = L[3];
        L[3] = L[2];
        L[2] = -L[1].z * L[2] + L[2].z * L[1];
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
    }
    else if (config == 14) // V2 V3 V4 clip V1
    {
        n = 5;
        L[4] = -L[0].z * L[3] + L[3].z * L[0];
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
    }
    else if (config == 15) // V1 V2 V3 V4
    {
        n = 4;
    }
    
    if (n == 3)
    {
    	L[3] = L[0];
	}
    if (n == 4)
    {
    	L[4] = L[0];
    }
}

vec3 LTC_Evaluate(vec3 N, vec3 V, vec3 P, mat3 Minv, vec4 points[4], bool twoSided)
{
    // construct orthonormal basis around N
    vec3 T1, T2;
    T1 = normalize(V - N * dot(V, N));
    T2 = cross(N, T1);

    // rotate area light in (T1, T2, R) basis
    Minv = Minv * mat3_from_rows(T1, T2, N);

    // polygon (allocate 5 vertices for clipping)
    vec3 L[5];
    L[0] = Minv * (points[0].xyz - P);
    L[1] = Minv * (points[1].xyz - P);
    L[2] = Minv * (points[2].xyz - P);
    L[3] = Minv * (points[3].xyz - P);
    L[4] = L[3]; // avoid warning

    int n;
    ClipQuadToHorizon(L, n);
    
    if (n == 0)
    {
    	return vec3(0, 0, 0);
    }

    // project onto sphere
    L[0] = normalize(L[0]);
    L[1] = normalize(L[1]);
    L[2] = normalize(L[2]);
    L[3] = normalize(L[3]);
    L[4] = normalize(L[4]);

    // integrate
    float sum = 0.0;

    sum += IntegrateEdge(L[0], L[1]);
    sum += IntegrateEdge(L[1], L[2]);
    sum += IntegrateEdge(L[2], L[3]);
    if (n >= 4)
    {
    	sum += IntegrateEdge(L[3], L[4]);
    }
    if (n == 5)
    {
    	sum += IntegrateEdge(L[4], L[0]);
    }

    // note: negated due to winding order
    sum = twoSided ? abs(sum) : max(0.0, -sum);

    vec3 Lo_i = vec3(sum, sum, sum);

    // scale by filtered light color
    //Lo_i *= textureLight;

    return Lo_i;
}

float FrostbiteAnalyicAreaLight(AreaLight areaLight, vec3 posWS, vec3 normalWS)
{
	vec3 Lunormalized = areaLight.points[0].xyz - posWS;
	vec3 L = normalize(Lunormalized);
	float sqrDist = dot(Lunormalized, Lunormalized);

	float lightRadius = 2.0;

	//#define WITHOUT_CORRECT_HORIZON
	#if WITHOUT_CORRECT_HORIZON // Analytical solution above horizon

	// Patch to Sphere frontal equation ( Quilez version )
	float sqrLightRadius = lightRadius * lightRadius;
	// Do not allow object to penetrate the light ( max )
	// Form factor equation include a (1 / FB_PI ) that need to be cancel
	// thus the " FB_PI *"
	float illuminance = PI * (sqrLightRadius / (max(sqrLightRadius, sqrDist))) * clamp(dot(normalWS, L), 0.0, 1.0);

	# else // Analytical solution with horizon

	// Tilted patch to sphere equation
	float Beta = acos(dot(normalWS, L));
	float H = sqrt (sqrDist);
	float h = H / lightRadius;
	float x = sqrt(h * h - 1);
	float y = -x * (1 / tan(Beta));

	float illuminance = 0;
	if (h * cos(Beta) > 1)
	{
		illuminance = cos(Beta) / (h * h);
	}
	else
	{
		illuminance = (1 / (PI * h * h)) *
			(cos(Beta) * acos(y) - x * sin(Beta) * sqrt (1 - y * y)) +
			(1 / PI ) * atan(sin(Beta) * sqrt (1 - y * y) / x);
	}

	illuminance *= PI;

	# endif

	return illuminance;
}

layout (binding = 1) uniform sampler2D brdfLUT;
layout (binding = 2) uniform samplerCube irradianceSampler;
layout (binding = 3) uniform samplerCube prefilterMap;
layout (binding = 4) uniform sampler2D depthBuffer;
layout (binding = 5) uniform sampler2D ssaoBuffer;
layout (binding = 6) uniform sampler2DArray shadowMaps;

layout (binding = 7) uniform sampler2D normalRoughnessTex;
layout (binding = 8) uniform sampler2D albedoMetallicTex;
layout (binding = 9) uniform sampler2D ltcMatricesTex;
layout (binding = 10) uniform sampler2D ltcAmplitudesTex;

void main()
{
    vec3 N = texture(normalRoughnessTex, ex_TexCoord).rgb;
    N = mat3(uboConstant.invView) * N; // view space -> world space
    //  TODO: Compile out as variant
    if (!gl_FrontFacing) N *= -1.0;
    float roughness = texture(normalRoughnessTex, ex_TexCoord).a;

    float depth = texture(depthBuffer, ex_TexCoord).r;
    vec3 posVS = ReconstructVSPosFromDepth(uboConstant.invProj, ex_TexCoord, depth);
    vec3 posWS = ReconstructWSPosFromDepth(uboConstant.invProj, uboConstant.invView, ex_TexCoord, depth);
    float depthN = posVS.z*(1/48.0);
	
    float invDist = 1.0f/(uboConstant.zFar-uboConstant.zNear);

	float linDepth = (posVS.z-uboConstant.zNear)*invDist;
	// fragColour = vec4(vec3(linDepth), 1); return;

    vec3 albedo = texture(albedoMetallicTex, ex_TexCoord).rgb;	
    float metallic = texture(albedoMetallicTex, ex_TexCoord).a;

    float ssao = (uboConstant.ssaoData.enabled == 1 ? texture(ssaoBuffer, ex_TexCoord).r : 1.0f);
	ssao = pow(ssao, uboConstant.ssaoData.powExp);

	// fragColour = vec4(vec3(pow(ssao, uboConstant.ssaoPowExp)), 1); return;

	uint cascadeIndex = 0;
	for (uint i = 0; i < NUM_CASCADES; ++i)
	{
		if (linDepth > uboConstant.shadowSamplingData.cascadeDepthSplits[i])
		{
			cascadeIndex = i + 1;
		}
	}

	float fogDist = clamp(length(uboConstant.camPos.xyz - posWS)*0.0003 - 0.1,0.0,1.0);
	vec3 V = normalize(uboConstant.camPos.xyz - posWS);
	vec3 R = reflect(-V, N);

	float NoV = max(dot(N, V), 0.0);

	// If diaelectric, F0 should be 0.04, if metal it should be the albedo colour
	vec3 F0 = vec3(0.04);
	F0 = mix(F0, albedo, metallic);

	// Reflectance equation
	vec3 Lo = vec3(0.0);
	for (int i = 0; i < NUM_POINT_LIGHTS; ++i)
	{
		if (uboConstant.pointLights[i].enabled == 0)
		{
			continue;
		}

		float distance = length(uboConstant.pointLights[i].position - posWS);

		if (distance > 125)
		{
			// TODO: Define radius on point lights individually
			continue;
		}

		// Pretend point lights have a radius of 1cm to avoid division by 0
		float attenuation = 1.0 / max((distance * distance), 0.001);
		vec3 L = normalize(uboConstant.pointLights[i].position - posWS);
		vec3 radiance = uboConstant.pointLights[i].colour.rgb * attenuation * uboConstant.pointLights[i].brightness;
		float NoL = max(dot(N, L), 0.0);

		Lo += DoLighting(radiance, N, V, L, NoV, NoL, roughness, metallic, F0, albedo);
	}

	for (int i = 0; i < NUM_SPOT_LIGHTS; ++i)
	{
		if (uboConstant.spotLights[i].enabled == 0)
		{
			continue;
		}

		float distance = length(uboConstant.spotLights[i].position - posWS);

		if (distance > 125)
		{
			// TODO: Define radius on spot lights individually
			continue;
		}

		// Pretend point lights have a radius of 1cm to avoid division by 0
		vec3 pointToLight = normalize(uboConstant.spotLights[i].position - posWS);
		vec3 L = normalize(uboConstant.spotLights[i].direction);
		float attenuation = 1.0 / max((distance * distance), 0.001);
		//attenuation *= max(dot(L, pointToLight), 0.0);
		vec3 radiance = uboConstant.spotLights[i].colour.rgb * attenuation * uboConstant.spotLights[i].brightness;
		float NoL = max(dot(N, pointToLight), 0.0);
		float LoPointToLight = max(dot(L, pointToLight), 0.0);
		float inCone = LoPointToLight - uboConstant.spotLights[i].angle;
		float blendThreshold = 0.1;
		if (inCone < 0.0)
		{
			radiance = vec3(0.0);
		}
		else if (inCone < blendThreshold)
		{
			radiance *= clamp(inCone / blendThreshold, 0.001, 1.0);
		}

		Lo += DoLighting(radiance, N, V, L, NoV, LoPointToLight, roughness, metallic, F0, albedo);
	}

	for (int i = 0; i < NUM_AREA_LIGHTS; ++i)
	{
		AreaLight areaLight = uboConstant.areaLights[i];

		if (areaLight.enabled == 0)
		{
			continue;
		}

//		float distance = length(areaLight.position - posWS);
//		if (distance > 125)
//		{
//			// TODO: Define radius on lights individually
//			continue;
//		}

		float angleOfIncidence = clamp(dot(N, V), 0.01, 0.99);
    	vec2 lookupUV = LTC_Coords(angleOfIncidence, roughness);

		mat3 Minv = LTC_Matrix(ltcMatricesTex, lookupUV);
    	
		vec4 points[4];
	    points[0] = areaLight.points[0];
	    points[1] = areaLight.points[1];
	    points[2] = areaLight.points[2];
	    points[3] = areaLight.points[3];
		bool twoSided = false;
		vec3 Lo_i_diff = LTC_Evaluate(N, V, posWS, identity33(), points, twoSided);
		vec3 Lo_i_spec = LTC_Evaluate(N, V, posWS, Minv, points, twoSided);

		Lo_i_diff *= albedo;

		// Specular
    	vec2 schlick = texture(ltcAmplitudesTex, lookupUV).xy;
    	vec3 specColour = metallic * albedo;
	    Lo_i_spec *= specColour * schlick.x + (1.0 - specColour) * schlick.y;

	    float dist = length(areaLight.points[0].xyz - posWS) + 0.001;
	    vec3 radiance = areaLight.brightness * pow(areaLight.colour, vec3(2.2));
		Lo += radiance * (Lo_i_diff + Lo_i_spec) / (2.0 * PI);
	}

	float dirLightShadowOpacity = 1.0;
	if (uboConstant.dirLight.enabled != 0)
	{
		vec3 L = normalize(uboConstant.dirLight.direction);
		vec3 radiance = uboConstant.dirLight.colour.rgb * uboConstant.dirLight.brightness;
		float NoL = max(dot(N, L), 0.0);

		dirLightShadowOpacity = DoShadowMapping(uboConstant.dirLight, uboConstant.shadowSamplingData, posWS, cascadeIndex, shadowMaps, NoL);

		Lo += DoLighting(radiance, N, V, L, NoV, NoL, roughness, metallic, F0, albedo) * dirLightShadowOpacity;
	}

	vec3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

	vec3 skyColour = mix(uboConstant.skyboxData.colourTop.rgb, uboConstant.skyboxData.colourMid.rgb, 1.0-max(dot(N, vec3(0,1,0)), 0.0));
	skyColour = mix(skyColour, uboConstant.skyboxData.colourBtm.rgb, -min(dot(N, vec3(0,-1,0)), 0.0));
	skyColour *= dirLightShadowOpacity;

	// Diffse ambient term (IBL)
	vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;	  
    vec3 irradiance = mix(texture(irradianceSampler, N).rgb, skyColour, 0.2);
    vec3 diffuse = irradiance * albedo;

	// Specular ambient term (IBL)
	const float MAX_REFLECTION_LOAD = 5.0;
	vec3 prefilteredColour = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOAD).rgb;
	prefilteredColour += skyColour * 0.1;
	prefilteredColour = vec3(0.0);
	vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
	vec3 specular = prefilteredColour * (F * brdf.x + brdf.y) * (0.75 * dirLightShadowOpacity + 0.25);
	// Dampen specular on downward facing normals
	//specular *= dot(N, vec3(0, 1, 0)) * 0.5 + 0.5;

	vec3 ambient = (kD * diffuse + specular);

	// TODO: Apply SSAO to ambient term
	vec3 colour = ambient + Lo * ssao;
	colour = mix(colour, uboConstant.skyboxData.colourFog.rgb, fogDist);

	colour = colour / (colour + vec3(1.0f)); // Reinhard tone-mapping
	colour = pow(colour, vec3(1.0f / 2.2f)); // Gamma correction

	fragColour = vec4(colour, 1.0);

	// fragColour = vec4(F, 1);

	// Visualize world pos:
	// fragColour = vec4(posWS*0.1, 1); return;

	// Visualize normals:
	// fragColour = vec4(N*0.5+0.5, 1); return;

	// Visualize screen coords:
	//fragColour = vec4(ex_TexCoord, 0, 1); return;

	// Visualize metallic:
	//fragColour = vec4(metallic, metallic, metallic, 1); return;

    DrawDebugOverlay(albedo, N, roughness, metallic, diffuse, specular, ex_TexCoord,
     linDepth, dirLightShadowOpacity, ssao, /* inout */ fragColour);
}
