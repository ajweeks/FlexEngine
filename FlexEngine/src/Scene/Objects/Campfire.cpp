#include "stdafx.hpp"

#include "Scene/Objects/Campfire.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtx/norm.hpp> // For distance2
#include <glm/gtx/quaternion.hpp>
IGNORE_WARNINGS_POP

#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "GameSounds.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/VertexBufferData.hpp"
#include "JSONTypes.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/Objects/Building.hpp" // For AppendBoxToVertexData
#include "Scene/Objects/Lights.hpp" // For FireFlicker
#include "Scene/Objects/Props.hpp" // For DroppedItem
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	std::vector<Campfire*> Campfire::s_Campfires;
	WoodcraftEffects* WoodcraftEffects::s_Instance = nullptr;

	// Felled plants (chopped down or dug out from under) topple over
	static struct FallingPlantHook
	{
		FallingPlantHook() { TerrainGenerator::s_OnPlantFelled = &FallingPlant::OnPlantFelled; }
	} s_FallingPlantHook;

	namespace
	{
		const u32 MAX_CAMPFIRE_LIGHTS = 3;
		const real CAMPFIRE_LIGHT_RANGE = 220.0f;
		// Fire effects (flames, embers & smoke) are only animated within this distance of the camera
		const real CAMPFIRE_EFFECTS_RANGE = 250.0f;
		// Linear (a warm white: saturated orange looks far too orange once lit through the colour pipeline)
		const glm::vec3 CAMPFIRE_LIGHT_COLOUR(1.0f, 0.74f, 0.5f);

		PointLightID s_CampfireLights[MAX_CAMPFIRE_LIGHTS] = { InvalidPointLightID, InvalidPointLightID, InvalidPointLightID };
		i32 s_CampfireLightsUpdatedFrame = -1;

		u32 HashU32(u32 x)
		{
			x ^= x >> 16;
			x *= 0x7feb352du;
			x ^= x >> 15;
			x *= 0x846ca68bu;
			x ^= x >> 16;
			return x;
		}

		real HashToUnit(u32 h)
		{
			return (real)(h & 0xFFFFFF) / (real)0x1000000;
		}

		// Cheap sequential random numbers for effects (not world generation)
		real NextRandom(u32& state)
		{
			state = HashU32(state + 0x9E3779B9u);
			return HashToUnit(state);
		}

		real RandomRange(u32& state, real lo, real hi)
		{
			return lo + (hi - lo) * NextRandom(state);
		}

		glm::vec3 RandomUnitVector(u32& state)
		{
			const real z = RandomRange(state, -1.0f, 1.0f);
			const real a = RandomRange(state, 0.0f, TWO_PI);
			const real r = glm::sqrt(glm::max(1.0f - z * z, 0.0f));
			return glm::vec3(r * std::cos(a), z, r * std::sin(a));
		}

		glm::vec3 NormalizeOr(const glm::vec3& v, const glm::vec3& fallback)
		{
			const real length = glm::length(v);
			return length > 1e-6f ? v / length : fallback;
		}

		// Flat shaded triangle into static vertex data
		void AppendTriangle(VertexBufferDataCreateInfo& vertexData, std::vector<u32>& indices, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
		{
			const glm::vec3 cross = glm::cross(b - a, c - a);
			const real length = glm::length(cross);
			if (length < 1e-10f)
			{
				return;
			}
			const glm::vec3 normal = cross / length;
			const glm::vec3 tangent = NormalizeOr(b - a, VEC3_RIGHT);
			const u32 base = (u32)vertexData.positions_3D.size();
			for (const glm::vec3* p : { &a, &b, &c })
			{
				vertexData.positions_3D.push_back(*p);
				vertexData.normals.push_back(normal);
				vertexData.tangents.push_back(tangent);
				vertexData.texCoords_UV.push_back(glm::vec2(p->x + p->y, p->z));
				vertexData.colours_R32G32B32A32.push_back(VEC4_ONE);
			}
			indices.insert(indices.end(), { base, base + 1, base + 2 });
		}

		// Adds static submeshes to object, one per (material, boxes/triangles) part
		struct StaticMeshBuilder
		{
			explicit StaticMeshBuilder(MaterialID materialID) :
				materialID(materialID)
			{
				vertexData.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(materialID)->shaderID)->vertexAttributes;
			}

			void Box(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation)
			{
				AppendBoxToVertexData(center, halfExtents, rotation, vertexData, indices);
			}

			void Triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
			{
				AppendTriangle(vertexData, indices, a, b, c);
			}

			void AddTo(GameObject* object)
			{
				if (indices.empty())
				{
					return;
				}
				Mesh* mesh = object->GetMesh();
				if (mesh == nullptr)
				{
					mesh = object->SetMesh(new Mesh(object));
				}
				MeshComponent::LoadFromMemory(mesh, vertexData, indices, materialID);
			}

			MaterialID materialID;
			VertexBufferDataCreateInfo vertexData;
			std::vector<u32> indices;
		};

		using LayoutBoxFunc = std::function<void(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation)>;

		// An unlit campfire's ring of stones & teepee of logs (local space), shared by its mesh & its placement preview.
		// groundHeight: local height of the ground below a local xz position.
		void BuildUnlitLayout(u32& h, const std::function<real(real, real)>& groundHeight, const LayoutBoxFunc& stone, const LayoutBoxFunc& log, const LayoutBoxFunc& logEnd)
		{
			const u32 stoneCount = 10;
			for (u32 i = 0; i < stoneCount; ++i)
			{
				const real angle = TWO_PI * ((real)i + RandomRange(h, -0.2f, 0.2f)) / (real)stoneCount;
				const real radius = RandomRange(h, 0.72f, 0.82f);
				const glm::vec3 halfExtents(RandomRange(h, 0.14f, 0.2f), RandomRange(h, 0.09f, 0.14f), RandomRange(h, 0.11f, 0.16f));
				const real x = std::cos(angle) * radius;
				const real z = std::sin(angle) * radius;
				const glm::quat rotation = glm::angleAxis(-angle + RandomRange(h, -0.3f, 0.3f), VEC3_UP) * glm::angleAxis(RandomRange(h, -0.2f, 0.2f), VEC3_RIGHT);
				stone(glm::vec3(x, groundHeight(x, z) + halfExtents.y * 0.6f, z), halfExtents, rotation);
			}

			const u32 logCount = 5;
			const real logHalfLength = 0.42f;
			const real tilt = 0.62f; // Radians from vertical
			for (u32 i = 0; i < logCount; ++i)
			{
				const real angle = TWO_PI * ((real)i + RandomRange(h, -0.15f, 0.15f)) / (real)logCount;
				const glm::vec3 out(std::cos(angle), 0.0f, std::sin(angle));
				const glm::vec3 axis = glm::normalize(glm::cross(VEC3_UP, out));
				// Leaning in, with the bottom end on the ground
				const glm::quat rotation = glm::angleAxis(-tilt, axis) * glm::angleAxis(RandomRange(h, 0.0f, TWO_PI), VEC3_UP);
				const glm::vec3 logUp = rotation * VEC3_UP;
				const real radius = RandomRange(h, 0.06f, 0.08f);
				const glm::vec3 bottom = out * 0.42f + glm::vec3(0.0f, groundHeight(out.x * 0.42f, out.z * 0.42f) + 0.02f, 0.0f);
				const glm::vec3 center = bottom + logUp * logHalfLength;
				log(center, glm::vec3(radius, logHalfLength, radius), rotation);
				// Lighter cut ends
				logEnd(center + logUp * (logHalfLength + 0.005f), glm::vec3(radius * 0.92f, 0.01f, radius * 0.92f), rotation);
			}
			// Kindling in the middle
			for (u32 i = 0; i < 4; ++i)
			{
				const glm::quat rotation = glm::angleAxis(RandomRange(h, 0.0f, TWO_PI), VEC3_UP);
				log(glm::vec3(RandomRange(h, -0.1f, 0.1f), groundHeight(0.0f, 0.0f) + 0.05f, RandomRange(h, -0.1f, 0.1f)), glm::vec3(0.03f, 0.03f, 0.22f), rotation);
			}
		}

		GameObject* CreateChildObject(GameObject* parent, const char* name)
		{
			GameObject* object = new GameObject(name, BaseObjectSID);
			object->SetSerializable(false);
			object->SetVisibleInSceneExplorer(false);
			parent->AddChild(object);
			return object;
		}
	} // anonymous namespace

	namespace woodcraft
	{
		MaterialID GetOrCreateMaterial(const char* name, const glm::vec4& albedo, real roughness, bool bDynamic)
		{
			MaterialID materialID = g_Renderer->GetMaterialID(name);
			if (materialID == InvalidMaterialID)
			{
				MaterialCreateInfo createInfo = {};
				createInfo.name = name;
				createInfo.shaderName = "pbr";
				// Colours are given in sRGB, materials take linear values
				createInfo.constAlbedo = glm::vec4(glm::pow(glm::vec3(albedo), glm::vec3(2.2f)), albedo.a);
				createInfo.constRoughness = roughness;
				createInfo.constMetallic = 0.0f;
				createInfo.bDynamic = bDynamic;
				createInfo.bSerializable = false;
				materialID = g_Renderer->InitializeMaterial(&createInfo);
			}
			return materialID;
		}

		MaterialID GetOrCreateGlowMaterial(const char* name, const glm::vec4& colour)
		{
			MaterialID materialID = g_Renderer->GetMaterialID(name);
			if (materialID == InvalidMaterialID)
			{
				MaterialCreateInfo createInfo = {};
				createInfo.name = name;
				createInfo.shaderName = "colour";
				createInfo.colourMultiplier = colour;
				createInfo.bDynamic = true;
				createInfo.bSerializable = false;
				materialID = g_Renderer->InitializeMaterial(&createInfo);
			}
			return materialID;
		}

		PrefabID GetWoodPrefabID()
		{
			return g_ResourceManager->GetPrefabID("wood");
		}

		i32 WoodYield(const terrain::PlantInfo& plant)
		{
			switch (plant.kind)
			{
			case terrain::PlantKind::CONIFER: return 3 + (i32)(plant.size01 * 2.99f);
			case terrain::PlantKind::DECIDUOUS: return 3 + (i32)(plant.size01 * 2.99f);
			case terrain::PlantKind::CACTUS: return 1 + (i32)(plant.size01 * 1.99f);
			// (Charcoal: burnt remains have less left in them)
			case terrain::PlantKind::SNAG: return 1 + (i32)(plant.size01 * 2.99f);
			default: return 0;
			}
		}

		//
		// DynamicMesh
		//

		DynamicMesh::~DynamicMesh()
		{
			delete m_VertexData;
		}

		void DynamicMesh::Create(GameObject* parent, const char* name, MaterialID materialID, u32 initialMaxVertexCount /* = 1024 */)
		{
			object = CreateChildObject(parent, name);

			delete m_VertexData;
			m_VertexData = new VertexBufferDataCreateInfo();
			m_VertexData->attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(materialID)->shaderID)->vertexAttributes;

			// Start with a tiny triangle, buffers can't be empty
			Begin();
			AddTriangle(VEC3_ZERO, glm::vec3(0.0f, 0.0f, 0.01f), glm::vec3(0.01f, 0.0f, 0.0f));

			RenderObjectCreateInfo renderObjectCreateInfo = {};
			renderObjectCreateInfo.bCastsShadow = false;

			Mesh* mesh = object->SetMesh(new Mesh(object));
			Mesh::CreateInfo meshCreateInfo = {};
			meshCreateInfo.bDynamic = true;
			meshCreateInfo.vertexBufferCreateInfo = m_VertexData;
			meshCreateInfo.indices = m_Indices;
			meshCreateInfo.materialIDs = { materialID };
			meshCreateInfo.initialMaxVertexCount = initialMaxVertexCount;
			meshCreateInfo.optionalRenderObjectCreateInfo = &renderObjectCreateInfo;
			mesh->LoadFromMemory(meshCreateInfo);
			object->SetVisible(false);
		}

		void DynamicMesh::Begin()
		{
			m_VertexData->positions_3D.clear();
			m_VertexData->texCoords_UV.clear();
			m_VertexData->colours_R32G32B32A32.clear();
			m_VertexData->normals.clear();
			m_VertexData->tangents.clear();
			m_Indices.clear();
		}

		void DynamicMesh::AddTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
		{
			AppendTriangle(*m_VertexData, m_Indices, a, b, c);
		}

		void DynamicMesh::AddBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation)
		{
			AppendBoxToVertexData(center, halfExtents, rotation, *m_VertexData, m_Indices);
		}

		void DynamicMesh::AddOctahedron(const glm::vec3& center, const glm::vec3& radii, const glm::quat& rotation)
		{
			const glm::vec3 axes[6] = { glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), glm::vec3(-1, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0), glm::vec3(0, -1, 0) };
			const u32 faces[8][3] = { { 0, 1, 4 }, { 1, 2, 4 }, { 2, 3, 4 }, { 3, 0, 4 }, { 1, 0, 5 }, { 2, 1, 5 }, { 3, 2, 5 }, { 0, 3, 5 } };
			glm::vec3 points[6];
			for (u32 i = 0; i < 6; ++i)
			{
				points[i] = center + rotation * (axes[i] * radii);
			}
			for (const auto& face : faces)
			{
				AddTriangle(points[face[0]], points[face[2]], points[face[1]]);
			}
		}

		void DynamicMesh::Upload()
		{
			if (object == nullptr)
			{
				return;
			}

			const bool bVisible = !m_Indices.empty();
			object->SetVisible(bVisible);
			if (!bVisible)
			{
				return;
			}

			const glm::mat4 worldToLocal = glm::inverse(object->GetTransform()->GetWorldTransform());
			const glm::mat3 normalWorldToLocal = glm::mat3(worldToLocal);
			for (u32 i = 0; i < (u32)m_VertexData->positions_3D.size(); ++i)
			{
				m_VertexData->positions_3D[i] = glm::vec3(worldToLocal * glm::vec4(m_VertexData->positions_3D[i], 1.0f));
				m_VertexData->normals[i] = NormalizeOr(normalWorldToLocal * m_VertexData->normals[i], VEC3_UP);
				m_VertexData->tangents[i] = NormalizeOr(normalWorldToLocal * m_VertexData->tangents[i], VEC3_RIGHT);
			}

			MeshComponent* meshComponent = object->GetMesh()->GetSubMesh(0);
			meshComponent->UpdateDynamicVertexData(*m_VertexData, m_Indices);
		}

		//
		// ParticleSet
		//

		void ParticleSet::Create(GameObject* parent, const char* name, MaterialID materialID, bool bInOctahedra)
		{
			bOctahedra = bInOctahedra;
			mesh.Create(parent, name, materialID);
		}

		void ParticleSet::Spawn(const Particle& particle)
		{
			// Bounded, so a long hitch can't pile up particles
			if (m_Particles.size() < 160)
			{
				m_Particles.push_back(particle);
			}
		}

		void ParticleSet::Update(real dt, real gravity, real drag, real growth)
		{
			const real dragFactor = glm::exp(-drag * dt);
			const real growthFactor = glm::pow(growth, dt);
			for (i32 i = (i32)m_Particles.size() - 1; i >= 0; --i)
			{
				Particle& p = m_Particles[i];
				p.age += dt;
				if (p.age >= p.life)
				{
					m_Particles[i] = m_Particles.back();
					m_Particles.pop_back();
					continue;
				}
				p.vel.y -= gravity * dt;
				p.vel *= dragFactor;
				p.pos += p.vel * dt;
				p.size *= growthFactor;
				const real spin = glm::length(p.spin);
				if (spin > 1e-4f)
				{
					p.rot = glm::angleAxis(spin * dt, p.spin / spin) * p.rot;
				}
			}

			if (mesh.object == nullptr)
			{
				return;
			}
			mesh.Begin();
			for (const Particle& p : m_Particles)
			{
				// Shrink away at the end of their lives
				const real t = p.age / p.life;
				const real size = p.size * (1.0f - glm::smoothstep(0.7f, 1.0f, t));
				if (size < 1e-3f)
				{
					continue;
				}
				if (bOctahedra)
				{
					mesh.AddOctahedron(p.pos, glm::vec3(size), p.rot);
				}
				else
				{
					mesh.AddBox(p.pos, glm::vec3(size), p.rot);
				}
			}
			mesh.Upload();
		}
	} // namespace woodcraft

	//
	// WoodcraftEffects
	//

	WoodcraftEffects::WoodcraftEffects() :
		GameObject("Woodcraft effects", BaseObjectSID)
	{
		SetSerializable(false);
		SetVisibleInSceneExplorer(false);
	}

	void WoodcraftEffects::Initialize()
	{
		using namespace woodcraft;
		m_Chips.Create(this, "Wood chips", GetOrCreateMaterial("Woodcraft chips", glm::vec4(0.72f, 0.56f, 0.36f, 1.0f), 0.85f, true), false);
		m_Leaves.Create(this, "Leaves", GetOrCreateMaterial("Woodcraft leaves", glm::vec4(0.25f, 0.4f, 0.14f, 1.0f), 0.8f, true), false);
		m_Dust.Create(this, "Dust", GetOrCreateMaterial("Woodcraft dust", glm::vec4(0.42f, 0.36f, 0.28f, 1.0f), 1.0f, true), true);
		m_Clods.Create(this, "Clods", GetOrCreateMaterial("Terrain clods", glm::vec4(0.47f, 0.33f, 0.2f, 1.0f), 0.95f, true), false);
		m_Rocks.Create(this, "Rocks", GetOrCreateMaterial("Terrain rocks", glm::vec4(0.55f, 0.53f, 0.5f, 1.0f), 0.8f, true), true);

		GameObject::Initialize();
	}

	void WoodcraftEffects::Destroy(bool bDetachFromParent /* = true */)
	{
		if (s_Instance == this)
		{
			s_Instance = nullptr;
		}
		GameObject::Destroy(bDetachFromParent);
	}

	WoodcraftEffects* WoodcraftEffects::Get()
	{
		if (s_Instance == nullptr && g_SceneManager->HasSceneLoaded())
		{
			s_Instance = new WoodcraftEffects();
			g_SceneManager->CurrentScene()->AddRootObject(s_Instance);
		}
		return s_Instance;
	}

	void WoodcraftEffects::Update()
	{
		const real dt = glm::min(g_DeltaTime, 0.1f);
		m_Chips.Update(dt, 9.8f, 0.6f, 1.0f);
		m_Leaves.Update(dt, 2.5f, 2.2f, 1.0f);
		m_Dust.Update(dt, -0.3f, 1.8f, 1.7f);
		m_Clods.Update(dt, 12.0f, 0.4f, 0.85f);
		m_Rocks.Update(dt, 12.0f, 0.4f, 0.9f);

		GameObject::Update();
	}

	void WoodcraftEffects::SpawnChips(const glm::vec3& pos, const glm::vec3& normal, u32 count, const glm::vec3& colour)
	{
		FLEX_UNUSED(colour);
		for (u32 i = 0; i < count; ++i)
		{
			woodcraft::Particle p = {};
			p.pos = pos;
			p.vel = NormalizeOr(normal * 1.4f + RandomUnitVector(m_Seed) + VEC3_UP * 0.6f, VEC3_UP) * RandomRange(m_Seed, 2.0f, 5.0f);
			p.rot = glm::angleAxis(RandomRange(m_Seed, 0.0f, TWO_PI), RandomUnitVector(m_Seed));
			p.spin = RandomUnitVector(m_Seed) * RandomRange(m_Seed, 5.0f, 15.0f);
			p.size = RandomRange(m_Seed, 0.025f, 0.055f);
			p.life = RandomRange(m_Seed, 0.6f, 1.1f);
			m_Chips.Spawn(p);
		}
	}

	void WoodcraftEffects::SpawnLeaves(const glm::vec3& center, real radius, u32 count)
	{
		for (u32 i = 0; i < count; ++i)
		{
			woodcraft::Particle p = {};
			p.pos = center + RandomUnitVector(m_Seed) * radius * RandomRange(m_Seed, 0.2f, 1.0f);
			p.vel = RandomUnitVector(m_Seed) * RandomRange(m_Seed, 1.0f, 3.5f) + VEC3_UP * 1.5f;
			p.rot = glm::angleAxis(RandomRange(m_Seed, 0.0f, TWO_PI), RandomUnitVector(m_Seed));
			p.spin = RandomUnitVector(m_Seed) * RandomRange(m_Seed, 2.0f, 6.0f);
			p.size = RandomRange(m_Seed, 0.06f, 0.12f);
			p.life = RandomRange(m_Seed, 1.5f, 3.0f);
			m_Leaves.Spawn(p);
		}
	}

	void WoodcraftEffects::SpawnRubble(const glm::vec3& center, const glm::vec3& normal, real halfWidth, u32 count, real hardness)
	{
		woodcraft::ParticleSet& set = hardness < 0.5f ? m_Clods : m_Rocks;
		const glm::vec3 side = NormalizeOr(glm::cross(normal, glm::abs(normal.y) < 0.9f ? VEC3_UP : VEC3_RIGHT), VEC3_RIGHT);
		const glm::vec3 up = glm::cross(side, normal);
		for (u32 i = 0; i < count; ++i)
		{
			woodcraft::Particle p = {};
			p.pos = center + (side * RandomRange(m_Seed, -1.0f, 1.0f) + up * RandomRange(m_Seed, -1.0f, 1.0f)) * halfWidth * 0.8f + normal * 0.15f;
			p.vel = NormalizeOr(normal * 1.6f + RandomUnitVector(m_Seed) * 0.8f + VEC3_UP * 0.8f, VEC3_UP) * RandomRange(m_Seed, 1.5f, 4.5f);
			p.rot = glm::angleAxis(RandomRange(m_Seed, 0.0f, TWO_PI), RandomUnitVector(m_Seed));
			p.spin = RandomUnitVector(m_Seed) * RandomRange(m_Seed, 3.0f, 10.0f);
			p.size = RandomRange(m_Seed, 0.06f, 0.16f);
			p.life = RandomRange(m_Seed, 0.8f, 1.4f);
			set.Spawn(p);
		}
		SpawnDust(center + normal * 0.3f, halfWidth * 0.6f, count / 4 + 1);
	}

	void WoodcraftEffects::SpawnDust(const glm::vec3& center, real radius, u32 count)
	{
		for (u32 i = 0; i < count; ++i)
		{
			woodcraft::Particle p = {};
			glm::vec3 offset = RandomUnitVector(m_Seed);
			offset.y = glm::abs(offset.y) * 0.3f;
			p.pos = center + offset * radius;
			p.vel = glm::vec3(offset.x, 0.4f, offset.z) * RandomRange(m_Seed, 1.5f, 3.0f);
			p.rot = glm::angleAxis(RandomRange(m_Seed, 0.0f, TWO_PI), RandomUnitVector(m_Seed));
			p.spin = RandomUnitVector(m_Seed) * RandomRange(m_Seed, 0.5f, 1.5f);
			p.size = RandomRange(m_Seed, 0.15f, 0.3f);
			p.life = RandomRange(m_Seed, 1.0f, 1.8f);
			m_Dust.Spawn(p);
		}
	}

	//
	// FallingPlant
	//

	FallingPlant::FallingPlant(const terrain::PlantInfo& plant, const glm::vec3& fallDirection) :
		GameObject("Falling plant", BaseObjectSID),
		m_Plant(plant)
	{
		SetSerializable(false);
		SetVisibleInSceneExplorer(false);

		glm::vec3 direction(fallDirection.x, 0.0f, fallDirection.z);
		if (glm::length(direction) < 1e-3f)
		{
			// Away from the camera (the player's chopping it)
			direction = plant.base - g_CameraManager->CurrentCamera()->position;
			direction.y = 0.0f;
		}
		m_FallDirection = NormalizeOr(direction, VEC3_RIGHT);
		m_Axis = NormalizeOr(glm::cross(VEC3_UP, m_FallDirection), VEC3_FORWARD);
		// Cacti are stiff & short: they tip over rather than crash down
		m_MaxAngle = plant.kind == terrain::PlantKind::CACTUS ? 1.35f : 1.48f;
		// The last hit gives it a push
		m_AngularVelocity = 0.35f;
	}

	FallingPlant::~FallingPlant()
	{
		delete m_VertexData;
	}

	void FallingPlant::OnPlantFelled(const terrain::PlantInfo& plant, const glm::vec3& fallDirection)
	{
		if (!g_SceneManager->HasSceneLoaded())
		{
			return;
		}
		FallingPlant* falling = new FallingPlant(plant, fallDirection);
		g_SceneManager->CurrentScene()->AddRootObject(falling);
	}

	void FallingPlant::Initialize()
	{
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		const MaterialID terrainMaterialID = g_Renderer->GetMaterialID("Terrain");
		if (terrainGenerator != nullptr && terrainMaterialID != InvalidMaterialID)
		{
			terrain::ChunkMeshData plantMesh;
			terrainGenerator->BuildPlantMesh(m_Plant, plantMesh);
			if (!plantMesh.indices.empty())
			{
				m_Positions = std::move(plantMesh.positions);
				m_Normals = std::move(plantMesh.normals);
				m_Colours = std::move(plantMesh.colours);
				m_Weights = std::move(plantMesh.weights);
				m_Indices = std::move(plantMesh.indices);

				m_VertexData = new VertexBufferDataCreateInfo();
				m_VertexData->attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(terrainMaterialID)->shaderID)->vertexAttributes;
				m_VertexData->positions_3D = m_Positions;
				m_VertexData->normals = m_Normals;
				m_VertexData->colours_R32G32B32A32 = m_Colours;
				m_VertexData->extraVec4s = m_Weights;

				RenderObjectCreateInfo renderObjectCreateInfo = {};
				renderObjectCreateInfo.cullFace = CullFace::NONE;

				Mesh* mesh = SetMesh(new Mesh(this));
				mesh->SetTypeToMemory();
				MeshComponent* meshComponent = MeshComponent::LoadFromMemoryDynamic(mesh, *m_VertexData, m_Indices, terrainMaterialID,
					(u32)m_Positions.size(), &renderObjectCreateInfo, true, nullptr);
				if (meshComponent != nullptr)
				{
					meshComponent->UpdateDynamicVertexData(*m_VertexData, m_Indices);
				}
			}
		}

		GameObject::Initialize();
	}

	void FallingPlant::UploadPose()
	{
		if (m_Mesh == nullptr || m_Mesh->GetSubMesh(0) == nullptr || m_VertexData == nullptr)
		{
			return;
		}

		const glm::vec3 pivot = m_Plant.base - glm::vec3(0.0f, 0.2f, 0.0f);
		const glm::quat rotation = glm::angleAxis(m_Angle, m_Axis);
		// Shrinks towards the middle of the fallen trunk
		const glm::vec3 shrinkCenter = pivot + rotation * glm::vec3(0.0f, m_Plant.height * 0.4f, 0.0f);
		for (u32 i = 0; i < (u32)m_Positions.size(); ++i)
		{
			glm::vec3 pos = pivot + rotation * (m_Positions[i] - pivot);
			pos = shrinkCenter + (pos - shrinkCenter) * m_Scale;
			// Sink into the ground as it goes
			pos.y -= (1.0f - m_Scale) * 0.6f;
			m_VertexData->positions_3D[i] = pos;
			m_VertexData->normals[i] = rotation * m_Normals[i];
		}
		m_Mesh->GetSubMesh(0)->UpdateDynamicVertexData(*m_VertexData, m_Indices);
	}

	void FallingPlant::Update()
	{
		const real dt = glm::min(g_DeltaTime, 0.05f);
		if (dt <= 0.0f)
		{
			GameObject::Update();
			return;
		}

		if (!m_bReleased)
		{
			m_WaitTime += dt;
			TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
			m_bReleased = terrainGenerator == nullptr || !terrainGenerator->IsApplyingEdits() || m_WaitTime > 3.0f;
		}
		else if (!m_bLanded)
		{
			// A rod pivoting about its base: angular acceleration = 3g / (2L) * sin(angle)
			const real length = glm::max(m_Plant.height * 0.8f, 1.0f);
			m_AngularVelocity += 1.5f * 9.8f / length * std::sin(m_Angle) * dt;
			m_Angle += m_AngularVelocity * dt;
			if (m_Angle >= m_MaxAngle)
			{
				m_Angle = m_MaxAngle;
				// A couple of small bounces off the ground
				m_AngularVelocity = -m_AngularVelocity * 0.18f;
				++m_Bounces;
				if (m_Bounces == 1)
				{
					// The wood can be picked up as soon as the trunk hits the ground
					DropWood();
					const glm::vec3 pivot = m_Plant.base;
					const glm::quat rotation = glm::angleAxis(m_Angle, m_Axis);
					WoodcraftEffects* effects = WoodcraftEffects::Get();
					if (effects != nullptr)
					{
						if (m_Plant.kind != terrain::PlantKind::CACTUS && m_Plant.kind != terrain::PlantKind::SNAG)
						{
							effects->SpawnLeaves(pivot + rotation * glm::vec3(0.0f, m_Plant.height * 0.7f, 0.0f), m_Plant.height * 0.2f, 40);
						}
						for (real t : { 0.3f, 0.6f, 0.9f })
						{
							effects->SpawnDust(pivot + rotation * glm::vec3(0.0f, m_Plant.height * t, 0.0f), 0.8f, 6);
						}
					}
					GameSounds::PlayTreeFall(pivot + m_FallDirection * m_Plant.height * 0.5f);
				}
				if (m_Bounces >= 3 || glm::abs(m_AngularVelocity) < 0.05f)
				{
					m_AngularVelocity = 0.0f;
					m_bLanded = true;
				}
			}
		}
		else
		{
			m_RestTime += dt;
			if (!m_bWoodDropped && m_RestTime > 0.6f)
			{
				DropWood();
			}
			if (m_RestTime > 1.2f)
			{
				m_Scale = glm::max(m_Scale - dt / 1.5f, 0.0f);
				if (m_Scale <= 0.0f)
				{
					g_SceneManager->CurrentScene()->RemoveObject(this, true);
				}
			}
		}

		UploadPose();

		GameObject::Update();
	}

	void FallingPlant::DropWood()
	{
		m_bWoodDropped = true;

		// Cacti drop pieces of cactus & burnt trees charcoal, rather than wood
		const PrefabID woodID = m_Plant.kind == terrain::PlantKind::CACTUS ? g_ResourceManager->GetPrefabID("cactus") :
			m_Plant.kind == terrain::PlantKind::SNAG ? g_ResourceManager->GetPrefabID("charcoal") : woodcraft::GetWoodPrefabID();
		const i32 count = woodcraft::WoodYield(m_Plant);
		if (!woodID.IsValid() || count <= 0)
		{
			return;
		}

		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		WoodcraftEffects* effects = WoodcraftEffects::Get();
		const glm::quat rotation = glm::angleAxis(m_Angle, m_Axis);
		BaseScene* scene = g_SceneManager->CurrentScene();
		for (i32 i = 0; i < count; ++i)
		{
			// Spread along the fallen trunk
			const real t = ((real)i + 0.5f) / (real)count;
			glm::vec3 pos = m_Plant.base + rotation * glm::vec3(0.0f, glm::max(m_Plant.trunkHeight, m_Plant.height * 0.5f) * t + 0.5f, 0.0f);
			glm::vec3 hitPos, hitNormal;
			if (terrainGenerator != nullptr && terrainGenerator->Raycast(pos + VEC3_UP * 3.0f, -VEC3_UP, 10.0f, hitPos, hitNormal))
			{
				pos = hitPos;
			}
			pos += VEC3_UP * 0.5f;
			DroppedItem* item = scene->CreateDroppedItem(woodID, 1, pos, VEC3_ZERO);
			// (it flies over to the player, so it doesn't need long to be seen first)
			item->pickupDelay = 0.3f;
			if (effects != nullptr)
			{
				effects->SpawnChips(pos, VEC3_UP, 4, VEC3_ZERO);
			}
		}
	}

	//
	// Campfire
	//

	Campfire::Campfire(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /* = false */) :
		GameObject(name, CampfireSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
	}

	PropertyCollection* Campfire::BuildTypeUniquePropertyCollection()
	{
		return GameObject::BuildPropertyCollection();
	}

	void Campfire::Initialize()
	{
		if (!IsPrefabTemplate())
		{
			const glm::vec3 pos = m_Transform.GetWorldPosition();
			m_Seed = pos.x * 1.31f + pos.z * 0.77f;
			m_ParticleSeed = HashU32((u32)(i32)(pos.x * 10.0f) ^ HashU32((u32)(i32)(pos.z * 10.0f)));

			CreateMeshes();
			s_Campfires.push_back(this);
		}

		GameObject::Initialize();

		if (!IsPrefabTemplate())
		{
			ApplyStateVisibility();
		}
	}

	void Campfire::Destroy(bool bDetachFromParent /* = true */)
	{
		s_Campfires.erase(std::remove(s_Campfires.begin(), s_Campfires.end(), this), s_Campfires.end());
		bool bAnyLit = false;
		for (Campfire* campfire : s_Campfires)
		{
			bAnyLit = bAnyLit || campfire->IsLit();
		}
		if (!bAnyLit)
		{
			ReleaseLights();
		}

		GameObject::Destroy(bDetachFromParent);
	}

	void Campfire::BuildPreviewGeometry(PlacementPreview::Geometry& geometry)
	{
		// (on flat ground, slightly inflated so it reads as a solid shape)
		u32 h = HashU32(17u);
		auto Box = [&](const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation)
		{
			geometry.Box(center, halfExtents + glm::vec3(0.01f), rotation);
		};
		BuildUnlitLayout(h, [](real, real) { return 0.0f; }, Box, Box, [](const glm::vec3&, const glm::vec3&, const glm::quat&) {});
	}

	void Campfire::CreateMeshes()
	{
		using woodcraft::GetOrCreateMaterial;

		const MaterialID stoneMaterialID = GetOrCreateMaterial("Campfire stone", glm::vec4(0.48f, 0.47f, 0.45f, 1.0f), 0.9f, false);
		const MaterialID logMaterialID = GetOrCreateMaterial("Campfire log", glm::vec4(0.36f, 0.25f, 0.15f, 1.0f), 0.85f, false);
		const MaterialID logEndMaterialID = GetOrCreateMaterial("Campfire log end", glm::vec4(0.7f, 0.55f, 0.36f, 1.0f), 0.8f, false);
		const MaterialID charcoalMaterialID = GetOrCreateMaterial("Campfire charcoal", glm::vec4(0.06f, 0.055f, 0.05f, 1.0f), 0.95f, false);
		const MaterialID ashMaterialID = GetOrCreateMaterial("Campfire ash", glm::vec4(0.3f, 0.29f, 0.28f, 1.0f), 1.0f, false);
		const MaterialID scorchMaterialID = GetOrCreateMaterial("Campfire scorch", glm::vec4(0.11f, 0.095f, 0.08f, 1.0f), 1.0f, false);

		u32 h = HashU32((u32)m_ParticleSeed + 17u);
		const glm::vec3 origin = m_Transform.GetWorldPosition();
		const glm::quat worldRot = m_Transform.GetWorldRotation();
		const glm::quat invRot = glm::inverse(worldRot);
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();

		// Height of the ground (in local space) below a local xz position, so things sit on uneven ground
		auto GroundHeight = [&](real x, real z)
		{
			const glm::vec3 world = origin + worldRot * glm::vec3(x, 0.0f, z);
			glm::vec3 hitPos, hitNormal;
			if (terrainGenerator != nullptr && terrainGenerator->Raycast(world + VEC3_UP * 1.5f, -VEC3_UP, 3.0f, hitPos, hitNormal))
			{
				return (invRot * (hitPos - origin)).y;
			}
			return 0.0f;
		};

		// A ring of stones, & a teepee of logs (before burning)
		m_FreshLogs = CreateChildObject(this, "Campfire logs");
		{
			StaticMeshBuilder stones(stoneMaterialID);
			StaticMeshBuilder logs(logMaterialID);
			StaticMeshBuilder ends(logEndMaterialID);
			BuildUnlitLayout(h, GroundHeight,
				[&](const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation) { stones.Box(center, halfExtents, rotation); },
				[&](const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation) { logs.Box(center, halfExtents, rotation); },
				[&](const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation) { ends.Box(center, halfExtents, rotation); });
			stones.AddTo(this);
			logs.AddTo(m_FreshLogs);
			ends.AddTo(m_FreshLogs);
		}

		// What's left after burning: collapsed charcoal logs on a bed of ash, on scorched ground
		m_Remains = CreateChildObject(this, "Campfire remains");
		{
			StaticMeshBuilder charcoal(charcoalMaterialID);
			for (u32 i = 0; i < 5; ++i)
			{
				const real angle = RandomRange(h, 0.0f, TWO_PI);
				const glm::vec3 offset(RandomRange(h, -0.15f, 0.15f), 0.0f, RandomRange(h, -0.15f, 0.15f));
				const glm::quat rotation = glm::angleAxis(angle, VEC3_UP) * glm::angleAxis(RandomRange(h, -0.15f, 0.15f), VEC3_RIGHT);
				const real halfLength = RandomRange(h, 0.12f, 0.26f);
				charcoal.Box(glm::vec3(offset.x, GroundHeight(offset.x, offset.z) + 0.1f, offset.z), glm::vec3(0.055f, 0.05f, halfLength), rotation);
			}
			charcoal.AddTo(m_Remains);

			// Scorched ground (an irregular disc following the ground) with a mound of ash in the middle
			StaticMeshBuilder scorch(scorchMaterialID);
			StaticMeshBuilder ash(ashMaterialID);
			const u32 segments = 20;
			std::vector<glm::vec3> outer(segments);
			std::vector<glm::vec3> inner(segments);
			for (u32 i = 0; i < segments; ++i)
			{
				const real angle = TWO_PI * (real)i / (real)segments;
				const real outerRadius = 1.25f * RandomRange(h, 0.82f, 1.15f);
				const real innerRadius = 0.5f * RandomRange(h, 0.8f, 1.2f);
				const real c = std::cos(angle);
				const real s = std::sin(angle);
				outer[i] = glm::vec3(c * outerRadius, 0.0f, s * outerRadius);
				outer[i].y = GroundHeight(outer[i].x, outer[i].z) + 0.06f;
				inner[i] = glm::vec3(c * innerRadius, 0.0f, s * innerRadius);
				inner[i].y = GroundHeight(inner[i].x, inner[i].z) + 0.09f;
			}
			const glm::vec3 center(0.0f, GroundHeight(0.0f, 0.0f) + 0.14f, 0.0f);
			for (u32 i = 0; i < segments; ++i)
			{
				const u32 j = (i + 1) % segments;
				// Counter-clockwise seen from above
				scorch.Triangle(inner[i], inner[j], outer[j]);
				scorch.Triangle(inner[i], outer[j], outer[i]);
				ash.Triangle(center, inner[j], inner[i]);
			}
			scorch.AddTo(m_Remains);
			ash.AddTo(m_Remains);
		}

		// Fire (animated while lit)
		m_Flames.Create(this, "Campfire flames", woodcraft::GetOrCreateGlowMaterial("Campfire flame", glm::vec4(1.0f, 0.42f, 0.06f, 1.0f)));
		m_FlameCores.Create(this, "Campfire flame cores", woodcraft::GetOrCreateGlowMaterial("Campfire flame core", glm::vec4(1.0f, 0.82f, 0.3f, 1.0f)));
		m_Embers.Create(this, "Campfire embers", woodcraft::GetOrCreateGlowMaterial("Campfire ember", glm::vec4(1.0f, 0.55f, 0.12f, 1.0f)), false);
		m_Smoke.Create(this, "Campfire smoke", GetOrCreateMaterial("Campfire smoke", glm::vec4(0.3f, 0.3f, 0.3f, 1.0f), 1.0f, true), true);
	}

	void Campfire::ApplyStateVisibility()
	{
		if (m_FreshLogs != nullptr)
		{
			m_FreshLogs->SetVisible(m_State == State::UNLIT || (m_State == State::LIT && m_BurnTimeRemaining > BURN_DURATION * 0.3f));
		}
		if (m_Remains != nullptr)
		{
			m_Remains->SetVisible(m_State != State::UNLIT);
		}
		if (m_State != State::LIT)
		{
			m_Flames.Begin();
			m_Flames.Upload();
			m_FlameCores.Begin();
			m_FlameCores.Upload();
		}
	}

	void Campfire::Light()
	{
		if (m_State != State::UNLIT)
		{
			return;
		}
		m_State = State::LIT;
		m_BurnTimeRemaining = BURN_DURATION;
		ApplyStateVisibility();
		GameSounds::PlayCampfireIgnite(m_Transform.GetWorldPosition());
	}

	Campfire* Campfire::FindNearest(const glm::vec3& pos, real maxDistance)
	{
		Campfire* nearest = nullptr;
		real nearestDistSq = maxDistance * maxDistance;
		for (Campfire* campfire : s_Campfires)
		{
			const real distSq = glm::distance2(campfire->m_Transform.GetWorldPosition(), pos);
			if (distSq < nearestDistSq)
			{
				nearestDistSq = distSq;
				nearest = campfire;
			}
		}
		return nearest;
	}

	void Campfire::Update()
	{
		if (m_State == State::LIT)
		{
			const bool bWasShowingLogs = m_BurnTimeRemaining > BURN_DURATION * 0.3f;
			m_BurnTimeRemaining -= g_DeltaTime;
			if (m_BurnTimeRemaining <= 0.0f)
			{
				m_BurnTimeRemaining = 0.0f;
				m_State = State::BURNT;
				ApplyStateVisibility();
			}
			else if (bWasShowingLogs != (m_BurnTimeRemaining > BURN_DURATION * 0.3f))
			{
				// The logs have collapsed into embers
				ApplyStateVisibility();
			}
		}

		UpdateLights();

		const glm::vec3 pos = m_Transform.GetWorldPosition();
		const real cameraDistance = glm::distance(pos, g_CameraManager->CurrentCamera()->position);
		if (cameraDistance < CAMPFIRE_EFFECTS_RANGE && g_DeltaTime > 0.0f)
		{
			// Dies down towards the end: a low glow for the last minute or so
			const real intensity = m_State == State::LIT ? glm::mix(0.3f, 1.0f, glm::smoothstep(0.0f, 90.0f, m_BurnTimeRemaining)) : 0.0f;
			UpdateFire(intensity);
			if (intensity > 0.0f)
			{
				GameSounds::ReportFire(pos, intensity);
			}
		}

		GameObject::Update();
	}

	void Campfire::UpdateFire(real intensity)
	{
		const real dt = glm::min(g_DeltaTime, 0.1f);
		const real time = AnimationTime();
		const glm::vec3 origin = m_Transform.GetWorldPosition() + VEC3_UP * 0.08f;

		if (intensity > 0.0f)
		{
			// Flame tongues: diamonds stretched upwards, swaying & flickering independently
			m_Flames.Begin();
			m_FlameCores.Begin();
			const u32 tongueCount = 6;
			for (u32 i = 0; i < tongueCount; ++i)
			{
				const real seed = m_Seed + (real)i * 7.31f;
				const real flicker = FireFlicker(time * 1.6f, seed);
				const real angle = TWO_PI * (real)i / (real)tongueCount + 0.4f * std::sin(time * 0.7f + seed);
				const real ringRadius = i == 0 ? 0.0f : 0.17f;
				const glm::vec3 base = origin + glm::vec3(std::cos(angle) * ringRadius, 0.0f, std::sin(angle) * ringRadius);
				const real height = (i == 0 ? 1.05f : 0.7f) * intensity * (0.55f + 0.6f * flicker);
				const real width = (i == 0 ? 0.2f : 0.15f) * glm::mix(0.6f, 1.0f, intensity);
				const glm::vec3 sway(0.12f * (FireFlicker(time * 0.8f, seed + 3.0f) - 0.85f) * 3.0f, 0.0f, 0.12f * (FireFlicker(time * 0.8f, seed + 5.0f) - 0.85f) * 3.0f);
				const glm::vec3 tip = base + VEC3_UP * height + sway * height;
				const real twist = time * 1.5f + seed;
				const u32 sides = 4;
				for (u32 layer = 0; layer < 2; ++layer)
				{
					woodcraft::DynamicMesh& mesh = layer == 0 ? m_Flames : m_FlameCores;
					const real scale = layer == 0 ? 1.0f : 0.5f;
					const glm::vec3 layerTip = base + (tip - base) * (layer == 0 ? 1.0f : 0.65f);
					const glm::vec3 waist = base + (layerTip - base) * 0.3f;
					for (u32 s = 0; s < sides; ++s)
					{
						const real a0 = twist + TWO_PI * (real)s / (real)sides;
						const real a1 = twist + TWO_PI * (real)(s + 1) / (real)sides;
						const glm::vec3 p0 = waist + glm::vec3(std::cos(a0), 0.0f, std::sin(a0)) * width * scale;
						const glm::vec3 p1 = waist + glm::vec3(std::cos(a1), 0.0f, std::sin(a1)) * width * scale;
						mesh.AddTriangle(p0, layerTip, p1);
						mesh.AddTriangle(p1, base, p0);
					}
				}
			}
			m_Flames.Upload();
			m_FlameCores.Upload();

			// Embers drift up out of the fire
			m_EmberTimer -= dt;
			while (m_EmberTimer <= 0.0f)
			{
				m_EmberTimer += RandomRange(m_ParticleSeed, 0.08f, 0.25f) / intensity;
				woodcraft::Particle p = {};
				p.pos = origin + glm::vec3(RandomRange(m_ParticleSeed, -0.25f, 0.25f), RandomRange(m_ParticleSeed, 0.1f, 0.4f), RandomRange(m_ParticleSeed, -0.25f, 0.25f));
				p.vel = glm::vec3(RandomRange(m_ParticleSeed, -0.3f, 0.3f), RandomRange(m_ParticleSeed, 1.2f, 2.4f), RandomRange(m_ParticleSeed, -0.3f, 0.3f));
				p.rot = glm::angleAxis(RandomRange(m_ParticleSeed, 0.0f, TWO_PI), RandomUnitVector(m_ParticleSeed));
				p.spin = RandomUnitVector(m_ParticleSeed) * 6.0f;
				p.size = RandomRange(m_ParticleSeed, 0.012f, 0.025f);
				p.life = RandomRange(m_ParticleSeed, 0.8f, 2.0f);
				m_Embers.Spawn(p);
			}

			// Puffs of smoke
			m_SmokeTimer -= dt;
			while (m_SmokeTimer <= 0.0f)
			{
				m_SmokeTimer += RandomRange(m_ParticleSeed, 0.35f, 0.6f);
				woodcraft::Particle p = {};
				p.pos = origin + glm::vec3(RandomRange(m_ParticleSeed, -0.1f, 0.1f), 0.9f * intensity + 0.2f, RandomRange(m_ParticleSeed, -0.1f, 0.1f));
				p.vel = glm::vec3(RandomRange(m_ParticleSeed, -0.1f, 0.1f) + 0.15f, RandomRange(m_ParticleSeed, 0.6f, 0.9f), RandomRange(m_ParticleSeed, -0.1f, 0.1f));
				p.rot = glm::angleAxis(RandomRange(m_ParticleSeed, 0.0f, TWO_PI), RandomUnitVector(m_ParticleSeed));
				p.spin = RandomUnitVector(m_ParticleSeed) * 0.6f;
				p.size = RandomRange(m_ParticleSeed, 0.07f, 0.12f) * glm::mix(0.6f, 1.0f, intensity);
				p.life = RandomRange(m_ParticleSeed, 4.0f, 6.0f);
				m_Smoke.Spawn(p);
			}
		}

		// Embers rise (negative gravity) & slow, smoke grows as it rises
		m_Embers.Update(dt, -0.4f, 0.8f, 0.85f);
		m_Smoke.Update(dt, -0.05f, 0.15f, 1.32f);
	}

	void Campfire::UpdateLights()
	{
		const i32 frame = g_Renderer->GetFramesRenderedCount();
		if (frame == s_CampfireLightsUpdatedFrame)
		{
			return;
		}
		s_CampfireLightsUpdatedFrame = frame;

		const glm::vec3 cameraPos = g_CameraManager->CurrentCamera()->position;
		std::vector<Pair<real, Campfire*>> lit;
		for (Campfire* campfire : s_Campfires)
		{
			if (campfire->IsLit())
			{
				const real distSq = glm::distance2(campfire->m_Transform.GetWorldPosition(), cameraPos);
				if (distSq < CAMPFIRE_LIGHT_RANGE * CAMPFIRE_LIGHT_RANGE)
				{
					lit.emplace_back(distSq, campfire);
				}
			}
		}
		std::sort(lit.begin(), lit.end(), [](const Pair<real, Campfire*>& a, const Pair<real, Campfire*>& b) { return a.first < b.first; });

		const real time = AnimationTime();
		for (u32 i = 0; i < MAX_CAMPFIRE_LIGHTS; ++i)
		{
			PointLightID& lightID = s_CampfireLights[i];
			if (i < (u32)lit.size())
			{
				Campfire* campfire = lit[i].second;
				const real intensity = glm::mix(0.3f, 1.0f, glm::smoothstep(0.0f, 90.0f, campfire->m_BurnTimeRemaining));
				PointLightData data = {};
				data.pos = campfire->m_Transform.GetWorldPosition() + VEC3_UP * 0.7f;
				// The light moves a little with the flames
				data.pos += glm::vec3(FireFlicker(time * 0.7f, campfire->m_Seed + 11.0f) - 0.85f, 0.0f, FireFlicker(time * 0.7f, campfire->m_Seed + 13.0f) - 0.85f) * 0.25f;
				data.enabled = 1;
				data.colour = CAMPFIRE_LIGHT_COLOUR;
				// Fade out with distance so lights being reassigned isn't noticeable
				const real fade = 1.0f - glm::smoothstep(CAMPFIRE_LIGHT_RANGE * 0.7f, CAMPFIRE_LIGHT_RANGE, glm::sqrt(lit[i].first));
				data.brightness = 30.0f * intensity * FireFlicker(time, campfire->m_Seed) * fade;
				if (lightID == InvalidPointLightID)
				{
					lightID = g_Renderer->RegisterPointLight(&data);
				}
				else
				{
					g_Renderer->UpdatePointLightData(lightID, &data);
				}
			}
			else if (lightID != InvalidPointLightID)
			{
				g_Renderer->RemovePointLight(lightID);
				lightID = InvalidPointLightID;
			}
		}
	}

	void Campfire::ReleaseLights()
	{
		for (PointLightID& lightID : s_CampfireLights)
		{
			if (lightID != InvalidPointLightID)
			{
				g_Renderer->RemovePointLight(lightID);
				lightID = InvalidPointLightID;
			}
		}
	}

	void Campfire::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject campfireObj;
		if (parentObject.TryGetObject("campfire", campfireObj))
		{
			i32 state = 0;
			campfireObj.TryGetInt("state", state);
			m_State = (State)glm::clamp(state, 0, (i32)State::_COUNT - 1);
			campfireObj.TryGetFloat("burn time remaining", m_BurnTimeRemaining);
			m_BurnTimeRemaining = glm::clamp(m_BurnTimeRemaining, 0.0f, BURN_DURATION);
		}
	}

	void Campfire::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		FLEX_UNUSED(bSerializePrefabData);

		JSONObject campfireObj = {};
		campfireObj.fields.emplace_back("state", JSONValue((i32)m_State));
		campfireObj.fields.emplace_back("burn time remaining", JSONValue(m_BurnTimeRemaining, 1));
		parentObject.fields.emplace_back("campfire", JSONValue(campfireObj));
	}
} // namespace flex
