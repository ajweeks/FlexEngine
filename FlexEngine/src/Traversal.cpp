#include "stdafx.hpp"

#include "Traversal.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
IGNORE_WARNINGS_POP

#include "Audio/AudioManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "GameSounds.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Graphics/Renderer.hpp"
#include "GameControls.hpp"
#include "InputManager.hpp"
#include "Localization.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "PlayerController.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/Objects/Building.hpp"
#include "Scene/Objects/Lights.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	namespace
	{
		const glm::vec3 GRAVITY(0.0f, -9.81f, 0.0f);
		const real THERMAL_CELL_SIZE = 380.0f;
		// Thermals drift downwind this many meters per meter of height
		const real THERMAL_DRIFT = 0.12f;
		// The columns of rising air only show once airborne this long, so a simple jump doesn't flash them
		const sec THERMAL_MOTES_DELAY = 1.5f;
		// How far above the player's center the end of a rope or ladder can be & still be grabbed
		const real CLIMB_GRAB_REACH_ABOVE = 2.5f;
		// ...& while jumping (the apex of a jump is about 3m up)
		const real CLIMB_GRAB_REACH_ABOVE_AIRBORNE = 5.0f;

		// Closest hit, ignoring one object (the player's own capsule)
		struct IgnoreObjectRayCallback final : public btCollisionWorld::ClosestRayResultCallback
		{
			IgnoreObjectRayCallback(const btVector3& from, const btVector3& to, const btCollisionObject* ignored, bool bIncludeRaycastOnly = false) :
				ClosestRayResultCallback(from, to),
				ignored(ignored)
			{
				m_collisionFilterMask = (i32)CollisionType::STATIC | (i32)CollisionType::DEFAULT;
				if (bIncludeRaycastOnly)
				{
					// Raycast only objects (tree foliage) must be in both the group & the mask
					m_collisionFilterGroup |= (i32)CollisionType::RAYCAST_ONLY;
					m_collisionFilterMask |= (i32)CollisionType::RAYCAST_ONLY;
				}
			}

			virtual bool needsCollision(btBroadphaseProxy* proxy) const override
			{
				return proxy->m_clientObject != ignored && ClosestRayResultCallback::needsCollision(proxy);
			}

			const btCollisionObject* ignored;
		};

		u32 HashU32(u32 x)
		{
			x ^= x >> 16;
			x *= 0x7feb352du;
			x ^= x >> 15;
			x *= 0x846ca68bu;
			x ^= x >> 16;
			return x;
		}

		// [0, 1)
		real HashToUnit(u32 x)
		{
			return (real)(HashU32(x) & 0xFFFFFF) / (real)0x1000000;
		}

		real ExpDecay(real rate, real dt)
		{
			return 1.0f - std::exp(-rate * dt);
		}

		glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback)
		{
			const real len = glm::length(v);
			return len > 1e-5f ? v / len : fallback;
		}

		// Rotates a towards b by at most maxAngle radians (both unit length)
		glm::vec3 RotateTowards(const glm::vec3& a, const glm::vec3& b, real maxAngle)
		{
			const real cosAngle = glm::clamp(glm::dot(a, b), -1.0f, 1.0f);
			const real angle = std::acos(cosAngle);
			if (angle <= maxAngle || angle < 1e-4f)
			{
				return b;
			}
			const glm::vec3 axis = SafeNormalize(glm::cross(a, b), VEC3_UP);
			return glm::rotate(glm::angleAxis(maxAngle, axis), a);
		}

		MaterialID GetOrCreateMaterial(const char* name, const glm::vec4& albedo, real roughness)
		{
			MaterialID materialID = g_Renderer->GetMaterialID(name);
			if (materialID == InvalidMaterialID)
			{
				MaterialCreateInfo createInfo = {};
				createInfo.name = name;
				createInfo.shaderName = "pbr";
				createInfo.constAlbedo = albedo;
				createInfo.constRoughness = roughness;
				createInfo.constMetallic = 0.0f;
				createInfo.bDynamic = true;
				createInfo.bSerializable = false;
				materialID = g_Renderer->InitializeMaterial(&createInfo);
			}
			return materialID;
		}
	} // anonymous namespace

	// Procedural, frequently updated geometry (in a child object of the player)
	struct Traversal::DynamicMesh
	{
		void Create(GameObject* parent, const char* name, MaterialID materialID, u32 initialMaxVertexCount = 512)
		{
			object = new GameObject(name, BaseObjectSID);
			object->SetSerializable(false);
			object->SetVisibleInSceneExplorer(false);

			vertexBufferCreateInfo = {};
			vertexBufferCreateInfo.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(materialID)->shaderID)->vertexAttributes;

			// Start with a tiny triangle, buffers can't be empty
			Begin();
			AddVertex(VEC3_ZERO, VEC3_UP, VEC2_ZERO);
			AddVertex(glm::vec3(0.01f, 0.0f, 0.0f), VEC3_UP, VEC2_ZERO);
			AddVertex(glm::vec3(0.0f, 0.0f, 0.01f), VEC3_UP, VEC2_ZERO);
			indices = { 0, 1, 2 };

			Mesh* mesh = object->SetMesh(new Mesh(object));
			Mesh::CreateInfo meshCreateInfo = {};
			meshCreateInfo.bDynamic = true;
			meshCreateInfo.vertexBufferCreateInfo = &vertexBufferCreateInfo;
			meshCreateInfo.indices = indices;
			meshCreateInfo.materialIDs = { materialID };
			meshCreateInfo.initialMaxVertexCount = initialMaxVertexCount;
			mesh->LoadFromMemory(meshCreateInfo);
			object->SetVisible(false);

			parent->AddChild(object);
		}

		void Begin()
		{
			vertexBufferCreateInfo.positions_3D.clear();
			vertexBufferCreateInfo.texCoords_UV.clear();
			vertexBufferCreateInfo.colours_R32G32B32A32.clear();
			vertexBufferCreateInfo.normals.clear();
			vertexBufferCreateInfo.tangents.clear();
			indices.clear();
		}

		u32 AddVertex(const glm::vec3& pos, const glm::vec3& normal, const glm::vec2& uv, const glm::vec4& colour = VEC4_ONE)
		{
			const glm::vec3 tangent = SafeNormalize(glm::cross(normal, glm::abs(normal.y) < 0.9f ? VEC3_UP : VEC3_RIGHT), VEC3_RIGHT);
			vertexBufferCreateInfo.positions_3D.push_back(pos);
			vertexBufferCreateInfo.normals.push_back(normal);
			vertexBufferCreateInfo.tangents.push_back(tangent);
			vertexBufferCreateInfo.texCoords_UV.push_back(uv);
			vertexBufferCreateInfo.colours_R32G32B32A32.push_back(colour);
			return (u32)vertexBufferCreateInfo.positions_3D.size() - 1;
		}

		void AddTriangle(u32 a, u32 b, u32 c)
		{
			indices.push_back(a);
			indices.push_back(b);
			indices.push_back(c);
		}

		// Adds a tube along the given (world space) points
		void AddTube(const glm::vec3* points, u32 pointCount, real radius, u32 radialCount)
		{
			for (u32 i = 0; i < pointCount; ++i)
			{
				const glm::vec3 tangent = SafeNormalize(points[glm::min(i + 1, pointCount - 1)] - points[i > 0 ? i - 1 : 0], VEC3_FORWARD);
				const glm::vec3 side0 = SafeNormalize(glm::cross(tangent, glm::abs(tangent.y) < 0.95f ? VEC3_UP : VEC3_RIGHT), VEC3_RIGHT);
				const glm::vec3 side1 = glm::cross(tangent, side0);
				const u32 ringStart = (u32)vertexBufferCreateInfo.positions_3D.size();
				for (u32 j = 0; j < radialCount; ++j)
				{
					const real theta = TWO_PI * (real)j / (real)radialCount;
					const glm::vec3 normal = side0 * std::cos(theta) + side1 * std::sin(theta);
					AddVertex(points[i] + normal * radius, normal, glm::vec2((real)j / (real)radialCount, (real)i / (real)(pointCount - 1)));
				}
				if (i > 0)
				{
					const u32 prevRingStart = ringStart - radialCount;
					for (u32 j = 0; j < radialCount; ++j)
					{
						const u32 j1 = (j + 1) % radialCount;
						AddTriangle(prevRingStart + j, ringStart + j, ringStart + j1);
						AddTriangle(prevRingStart + j, ringStart + j1, prevRingStart + j1);
					}
				}
			}
		}

		// Positions were added in world space, convert them to the object's local space & upload
		void Upload()
		{
			const bool bVisible = !indices.empty();
			object->SetVisible(bVisible);
			if (!bVisible)
			{
				return;
			}

			const glm::mat4 worldToLocal = glm::inverse(object->GetTransform()->GetWorldTransform());
			const glm::mat3 normalWorldToLocal = glm::mat3(worldToLocal);
			for (u32 i = 0; i < (u32)vertexBufferCreateInfo.positions_3D.size(); ++i)
			{
				vertexBufferCreateInfo.positions_3D[i] = glm::vec3(worldToLocal * glm::vec4(vertexBufferCreateInfo.positions_3D[i], 1.0f));
				vertexBufferCreateInfo.normals[i] = glm::normalize(normalWorldToLocal * vertexBufferCreateInfo.normals[i]);
				vertexBufferCreateInfo.tangents[i] = glm::normalize(normalWorldToLocal * vertexBufferCreateInfo.tangents[i]);
			}

			MeshComponent* meshComponent = object->GetMesh()->GetSubMesh(0);
			meshComponent->UpdateDynamicVertexData(vertexBufferCreateInfo, indices);
		}

		GameObject* object = nullptr;
		VertexBufferDataCreateInfo vertexBufferCreateInfo;
		std::vector<u32> indices;
	};


	const char* Traversal::ModeName(Mode mode)
	{
		static const char* names[] = { "None", "Gliding", "Wingsuit", "Climbing", "Skiing" };
		static_assert(ARRAY_LENGTH(names) == (u32)Mode::_COUNT, "Mode names out of sync with Mode enum");
		return names[(u32)mode];
	}

	void Traversal::Initialize(Player* player)
	{
		m_Player = player;

		m_RopeMaterialID = GetOrCreateMaterial("Traversal rope", glm::vec4(0.75f, 0.42f, 0.12f, 1.0f), 0.8f);
		// Ripstop nylon (linear colours): a deep orange-red with cream tips & centre, dark lines & webbing
		m_CanopyMaterialID = GetOrCreateMaterial("Glider canopy", glm::vec4(0.75f, 0.11f, 0.025f, 1.0f), 0.55f);
		m_CanopyTrimMaterialID = GetOrCreateMaterial("Glider canopy trim", glm::vec4(0.78f, 0.72f, 0.56f, 1.0f), 0.55f);
		m_GliderFrameMaterialID = GetOrCreateMaterial("Glider lines", glm::vec4(0.035f, 0.035f, 0.04f, 1.0f), 0.45f);
		m_RopeMesh = new DynamicMesh();
		m_RopeMesh->Create(player, "Grapple rope", m_RopeMaterialID);
		m_CanopyMesh = new DynamicMesh();
		m_CanopyMesh->Create(player, "Glider canopy", m_CanopyMaterialID, 2048);
		m_CanopyTrimMesh = new DynamicMesh();
		m_CanopyTrimMesh->Create(player, "Glider canopy trim", m_CanopyTrimMaterialID, 2048);
		m_GliderFrameMesh = new DynamicMesh();
		m_GliderFrameMesh->Create(player, "Glider lines", m_GliderFrameMaterialID, 2048);
		// A deep blue suit with yellow wing tips & tail edge (linear colours), contrasting with the terrain below
		m_WingsuitMaterialID = GetOrCreateMaterial("Wingsuit", glm::vec4(0.02f, 0.09f, 0.42f, 1.0f), 0.6f);
		m_WingsuitTrimMaterialID = GetOrCreateMaterial("Wingsuit trim", glm::vec4(0.9f, 0.55f, 0.02f, 1.0f), 0.6f);
		m_WingsuitMesh = new DynamicMesh();
		m_WingsuitMesh->Create(player, "Wingsuit", m_WingsuitMaterialID, 1024);
		m_WingsuitTrimMesh = new DynamicMesh();
		m_WingsuitTrimMesh->Create(player, "Wingsuit trim", m_WingsuitTrimMaterialID, 512);
		// Unlit & translucent (vertex alpha)
		m_AirStreakMaterialID = g_Renderer->GetMaterialID("Air streaks");
		if (m_AirStreakMaterialID == InvalidMaterialID)
		{
			MaterialCreateInfo createInfo = {};
			createInfo.name = "Air streaks";
			createInfo.shaderName = "colour";
			createInfo.colourMultiplier = VEC4_ONE;
			createInfo.bDynamic = true;
			createInfo.bSerializable = false;
			m_AirStreakMaterialID = g_Renderer->InitializeMaterial(&createInfo);
		}
		m_AirStreakMesh = new DynamicMesh();
		m_AirStreakMesh->Create(player, "Air streaks", m_AirStreakMaterialID, MAX_AIR_STREAKS * 6);
		m_AirStreakMesh->object->SetCastsShadow(false);

		// The player's visible body, which leans while flying (the capsule collider stays upright)
		for (GameObject* child : player->GetChildren())
		{
			if (child->GetName() == "Player mesh")
			{
				m_BodyMeshObject = child;
				m_BodyMeshBasePos = child->GetTransform()->GetLocalPosition();
				m_BodyMeshBaseRot = child->GetTransform()->GetLocalRotation();
				break;
			}
		}
		// A little sway as the glider settles into (& out of) turns
		m_BankSpring.DR = 0.75f;
		m_BankSpring.UAF = 5.0f;
		m_PitchSpring.DR = 0.8f;
		m_PitchSpring.UAF = 4.0f;
		m_MotesMaterialID = GetOrCreateMaterial("Thermal motes", glm::vec4(0.95f, 0.92f, 0.85f, 1.0f), 1.0f);
		m_MotesMesh = new DynamicMesh();
		m_MotesMesh->Create(player, "Thermal motes", m_MotesMaterialID, 4096);
		m_MotesMesh->object->SetCastsShadow(false);

		m_VarioBeepID = AudioManager::SynthesizeSound(0.07f, 880.0f);
		m_HookFireID = g_ResourceManager->GetOrLoadAudioSourceID(SID("latch-open-04.wav"), true);
		m_HookAttachID = g_ResourceManager->GetOrLoadAudioSourceID(SID("click-02.wav"), true);
		m_DeployID = g_ResourceManager->GetOrLoadAudioSourceID(SID("latch-closing-09.wav"), true);
	}

	void Traversal::Destroy()
	{
		// The mesh objects are children of the player, so they're destroyed along with it
		delete m_RopeMesh;
		m_RopeMesh = nullptr;
		delete m_CanopyMesh;
		m_CanopyMesh = nullptr;
		delete m_CanopyTrimMesh;
		m_CanopyTrimMesh = nullptr;
		delete m_GliderFrameMesh;
		m_GliderFrameMesh = nullptr;
		delete m_WingsuitMesh;
		m_WingsuitMesh = nullptr;
		delete m_WingsuitTrimMesh;
		m_WingsuitTrimMesh = nullptr;
		delete m_AirStreakMesh;
		m_AirStreakMesh = nullptr;
		m_BodyMeshObject = nullptr;
		delete m_MotesMesh;
		m_MotesMesh = nullptr;

		g_ResourceManager->DestroyAudioSource(m_VarioBeepID);
		m_VarioBeepID = InvalidAudioSourceID;
	}

	void Traversal::Reset()
	{
		SetMode(Mode::NONE);
		m_Hook = {};
		m_Stamina = 1.0f;
		m_bStaminaExhausted = false;
		m_bJumpQueued = false;
		m_bLeapQueued = false;
		m_RopeToss = {};
		m_MantleCooldown = 0.0f;
		// Respawning/teleporting: no folding animation or lean carried over
		m_CanopyOpenness = 0.0f;
		m_BankSpring.Reset();
		m_PitchSpring.Reset();
		m_TiltLastVelocity = VEC3_ZERO;
	}

	bool Traversal::CanControl() const
	{
		return m_Player->m_bPossessed &&
			!m_Player->IsAnyInventoryShowing() &&
			!m_Player->m_RidingVehicleID.IsValid() &&
			m_Player->m_TrackRidingID == InvalidTrackID &&
			!m_Player->m_bAwaitingTerrainCollision &&
			!g_EngineInstance->IsSimulationPaused();
	}

	real Traversal::GetMoveForward() const
	{
		if (!CanControl())
		{
			return 0.0f;
		}
		return g_InputManager->GetActionAxisValue(Action::MOVE_FORWARD) - g_InputManager->GetActionAxisValue(Action::MOVE_BACKWARD);
	}

	real Traversal::GetMoveRight() const
	{
		if (!CanControl())
		{
			return 0.0f;
		}
		return g_InputManager->GetActionAxisValue(Action::MOVE_RIGHT) - g_InputManager->GetActionAxisValue(Action::MOVE_LEFT);
	}

	glm::vec3 Traversal::GetLookDirection() const
	{
		return m_Player->GetLookDirection();
	}

	glm::vec3 Traversal::GetHeadingXZ() const
	{
		const glm::vec3 look = GetLookDirection();
		return SafeNormalize(glm::vec3(look.x, 0.0f, look.z), m_Player->GetTransform()->GetForward());
	}

	glm::vec3 Traversal::GetHandPos() const
	{
		Transform* transform = m_Player->GetTransform();
		return m_Player->GetEyePosition() + transform->GetRight() * 0.25f - VEC3_UP * 0.35f + GetLookDirection() * 0.3f;
	}

	bool Traversal::HasMomentum() const
	{
		return m_Mode != Mode::NONE || m_Hook.state == HookState::ATTACHED || !m_bGrounded;
	}

	void Traversal::SetGravityEnabled(bool bEnabled)
	{
		btRigidBody* rb = m_Player->GetRigidBody()->GetRigidBodyInternal();
		rb->setGravity(bEnabled ? ToBtVec3(GRAVITY) : btVector3(0.0f, 0.0f, 0.0f));
	}

	void Traversal::SetMode(Mode mode)
	{
		if (mode == m_Mode)
		{
			return;
		}

		if (mode == Mode::GLIDING && m_Mode != Mode::WINGSUIT)
		{
			AudioManager::PlaySource(m_DeployID);
		}
		else if (m_Mode == Mode::GLIDING && mode != Mode::WINGSUIT)
		{
			GameSounds::PlayGliderStow();
		}

		m_Mode = mode;
		if (mode != Mode::CLIMBING)
		{
			m_bMantling = false;
			m_ClimbObjectID = InvalidGameObjectID;
		}

		// Flying & climbing integrate their own forces
		SetGravityEnabled(mode != Mode::GLIDING && mode != Mode::WINGSUIT && mode != Mode::CLIMBING);
	}

	bool Traversal::BlockLedges(glm::vec3& velocity) const
	{
		const glm::vec2 horizontal(velocity.x, velocity.z);
		if (glm::length(horizontal) < 0.01f)
		{
			return false;
		}

		const glm::vec3 pos = m_Player->GetTransform()->GetWorldPosition();
		const real feetY = pos.y - m_Player->GetHeight() * 0.5f;
		// Ground must be found within a step up or down of the feet, a little past the edge of the capsule
		const real reach = m_Player->GetRadius() + 0.3f;
		auto HasGroundToward = [&](const glm::vec2& dir)
		{
			const glm::vec2 d = glm::normalize(dir);
			const glm::vec3 probe(pos.x + d.x * reach, feetY + 0.5f, pos.z + d.y * reach);
			glm::vec3 hitPos, hitNormal;
			return Raycast(probe, probe - VEC3_UP * 1.3f, hitPos, hitNormal);
		};

		bool bBlocked = false;
		glm::vec2 allowed = horizontal;
		if (!HasGroundToward(horizontal))
		{
			// Slide along the ledge if one axis is clear
			bBlocked = true;
			allowed = glm::vec2(0.0f);
			if (glm::abs(horizontal.x) > 0.01f && HasGroundToward(glm::vec2(horizontal.x, 0.0f)))
			{
				allowed.x = horizontal.x;
			}
			if (glm::abs(horizontal.y) > 0.01f && HasGroundToward(glm::vec2(0.0f, horizontal.y)))
			{
				allowed.y = horizontal.y;
			}
			if (allowed.x != 0.0f && allowed.y != 0.0f)
			{
				allowed = glm::vec2(0.0f);
			}
		}
		velocity.x = allowed.x;
		velocity.z = allowed.y;
		return bBlocked;
	}

	bool Traversal::Raycast(const glm::vec3& from, const glm::vec3& to, glm::vec3& outHitPos, glm::vec3& outHitNormal, u32 flags /* = 0 */) const
	{
		bool bHit = false;
		real closestDistance = glm::distance(from, to);

		const btVector3 btFrom = ToBtVec3(from);
		const btVector3 btTo = ToBtVec3(to);
		IgnoreObjectRayCallback callback(btFrom, btTo, m_Player->GetRigidBody()->GetRigidBodyInternal(), (flags & RAYCAST_FOLIAGE) != 0);
		g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld()->rayTest(btFrom, btTo, callback);
		if (callback.hasHit())
		{
			bHit = true;
			outHitPos = ToVec3(callback.m_hitPointWorld);
			outHitNormal = ToVec3(callback.m_hitNormalWorld);
			closestDistance = glm::distance(from, outHitPos);
		}

		if (flags & RAYCAST_PHYSICS_ONLY)
		{
			return bHit;
		}

		// Terrain collision only exists near the player, so also test against the terrain itself
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		glm::vec3 terrainHitPos, terrainHitNormal;
		if (terrainGenerator != nullptr && closestDistance > 0.5f &&
			terrainGenerator->Raycast(from, to - from, closestDistance, terrainHitPos, terrainHitNormal) &&
			glm::distance(from, terrainHitPos) < closestDistance - 0.25f)
		{
			bHit = true;
			outHitPos = terrainHitPos;
			outHitNormal = terrainHitNormal;
		}

		return bHit;
	}

	Traversal::GroundInfo Traversal::ProbeGround(real maxDistance) const
	{
		GroundInfo result = {};
		const glm::vec3 pos = m_Player->GetTransform()->GetWorldPosition();
		result.bHit = Raycast(pos, pos - VEC3_UP * maxDistance, result.pos, result.normal);
		result.distance = result.bHit ? pos.y - result.pos.y : maxDistance;
		return result;
	}

	real Traversal::GetGroundSlipperiness(const GroundInfo& ground) const
	{
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (!ground.bHit || terrainGenerator == nullptr)
		{
			return 0.2f;
		}

		// Weights: x: grass, y: rock, z: granular (sand & gravel), w: snow
		const terrain::SurfaceAttributes surface = terrainGenerator->SampleSurface(ground.pos, ground.normal);
		return glm::clamp(surface.weights.w + surface.weights.z * 0.6f + surface.weights.x * 0.15f, 0.0f, 1.0f);
	}

	void Traversal::GetThermalsNear(const glm::vec2& pos, real halfSize, std::vector<Thermal>& outThermals) const
	{
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (terrainGenerator == nullptr)
		{
			return;
		}

		// At most one thermal per cell, placed & sized deterministically from the world's seed
		const u32 seed = HashU32((u32)terrainGenerator->GetSeed() ^ 0x7e1f3a5du);
		const i32 minCellX = (i32)glm::floor((pos.x - halfSize) / THERMAL_CELL_SIZE) - 1;
		const i32 maxCellX = (i32)glm::floor((pos.x + halfSize) / THERMAL_CELL_SIZE) + 1;
		const i32 minCellZ = (i32)glm::floor((pos.y - halfSize) / THERMAL_CELL_SIZE) - 1;
		const i32 maxCellZ = (i32)glm::floor((pos.y + halfSize) / THERMAL_CELL_SIZE) + 1;
		for (i32 cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ)
		{
			for (i32 cellX = minCellX; cellX <= maxCellX; ++cellX)
			{
				const u32 cellHash = HashU32((u32)cellX * 0x8da6b343u + (u32)cellZ * 0xd8163841u + seed);
				if (HashToUnit(cellHash) > 0.7f)
				{
					continue;
				}
				Thermal thermal;
				thermal.center = glm::vec2(((real)cellX + 0.15f + 0.7f * HashToUnit(cellHash + 1)) * THERMAL_CELL_SIZE,
					((real)cellZ + 0.15f + 0.7f * HashToUnit(cellHash + 2)) * THERMAL_CELL_SIZE);
				thermal.radius = 45.0f + 55.0f * HashToUnit(cellHash + 3);
				thermal.strength = 2.0f + 3.5f * HashToUnit(cellHash + 4);
				outThermals.push_back(thermal);
			}
		}
	}

	real Traversal::ComputeLift(const glm::vec3& pos) const
	{
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (terrainGenerator == nullptr)
		{
			return 0.0f;
		}

		const terrain::ColumnSample column = terrainGenerator->SampleColumn(pos.x, pos.z);
		const bool bOverWater = column.lakeID != 0 && column.lakeT < 1.0f && column.height < column.waterLevel;
		const real groundHeight = bOverWater ? column.waterLevel : column.height;
		const real height = pos.y - groundHeight;

		// Ridge lift: wind blowing up slopes is deflected upwards
		const glm::vec2 windDir(std::cos(m_WindAngle), std::sin(m_WindAngle));
		const real windSpeed = 7.0f;
		const real sampleDist = 35.0f;
		const real upwindHeight = terrainGenerator->SampleColumn(pos.x - windDir.x * sampleDist, pos.z - windDir.y * sampleDist).height;
		const real downwindHeight = terrainGenerator->SampleColumn(pos.x + windDir.x * sampleDist, pos.z + windDir.y * sampleDist).height;
		const real slope = (downwindHeight - upwindHeight) / (2.0f * sampleDist);
		const real ridgeLift = windSpeed * glm::clamp(slope, 0.0f, 1.2f) * (1.0f - glm::smoothstep(40.0f, 220.0f, height));

		// Thermals: columns of rising air over sunlit ground, drifting downwind with height
		real thermalLift = 0.0f;
		DirectionalLight* sun = g_Renderer->GetDirectionalLight();
		const real sunElevation = sun != nullptr ? sun->data.dir.y : 0.7f;
		const real sunFactor = glm::smoothstep(0.05f, 0.45f, sunElevation);
		if (sunFactor > 0.0f && height > 0.0f)
		{
			const glm::vec2 drifted = glm::vec2(pos.x, pos.z) - windDir * height * THERMAL_DRIFT;
			thread_local std::vector<Thermal> thermals;
			thermals.clear();
			GetThermalsNear(drifted, THERMAL_CELL_SIZE * 0.5f, thermals);
			for (const Thermal& thermal : thermals)
			{
				const real distSq = glm::dot(drifted - thermal.center, drifted - thermal.center);
				thermalLift += thermal.strength * std::exp(-distSq / (thermal.radius * thermal.radius));
			}

			// Cold & wet ground heats up less, & thermals top out a while above the ground
			const real groundFactor = bOverWater ? 0.0f : glm::mix(0.35f, 1.0f, glm::smoothstep(-0.5f, 0.2f, column.temperature));
			thermalLift *= sunFactor * groundFactor * glm::smoothstep(5.0f, 40.0f, height) * (1.0f - glm::smoothstep(700.0f, 1000.0f, height));
		}

		return ridgeLift + thermalLift;
	}

	void Traversal::OnJumpPressed()
	{
		m_bJumpHeld = true;

		if (m_Hook.state == HookState::ATTACHED)
		{
			// Reels in while held, lifting the player off the ground (see UpdateInput)
			return;
		}

		switch (m_Mode)
		{
		case Mode::CLIMBING:
			m_bLeapQueued = true;
			break;
		case Mode::GLIDING:
		case Mode::WINGSUIT:
			SetMode(Mode::NONE);
			break;
		case Mode::SKIING:
			m_bJumpQueued = true;
			break;
		case Mode::NONE:
			if (m_bGrounded || m_TimeSinceGrounded < m_Settings.coyoteTime || m_HeightAboveGround < 0.6f)
			{
				// Also queued just before landing, & kept for a moment (see FixedUpdate)
				m_bJumpQueued = true;
				m_JumpQueuedTime = 0.0f;
			}
			else if (m_HeightAboveGround > m_Settings.glideMinHeight)
			{
				if (m_Hook.state != HookState::NONE)
				{
					ReleaseHook();
				}
				SetMode(Mode::GLIDING);
			}
			break;
		default:
			break;
		}
	}

	void Traversal::OnJumpReleased()
	{
		m_bJumpHeld = false;
	}

	void Traversal::Update()
	{
		PROFILE_AUTO("Traversal Update");

		const sec dt = g_DeltaTime;

		// Wind slowly changes direction (seeded per world so it's consistent between sessions)
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		const real windBaseAngle = terrainGenerator != nullptr ? HashToUnit((u32)terrainGenerator->GetSeed() * 2654435761u) * TWO_PI : 0.7f;
		m_WindAngle = windBaseAngle + 0.6f * (real)std::sin(g_SecElapsedSinceProgramStart / 400.0);

		UpdateSurroundings();
		UpdateInput();
		UpdateHook();
		UpdateRopeToss();
		UpdateSounds();

		// Camera feedback: widen the view with speed, & bank into turns while flying
		const glm::vec3 velocity = ToVec3(m_Player->GetRigidBody()->GetRigidBodyInternal()->getLinearVelocity());
		const real speed = glm::length(velocity);
		const real targetFOVOffset = HasMomentum() ? glm::smoothstep(14.0f, 70.0f, speed) * glm::radians(22.0f) : 0.0f;
		m_FOVOffset = glm::mix(m_FOVOffset, targetFOVOffset, ExpDecay(3.0f, dt));

		real targetRoll = 0.0f;
		if ((m_Mode == Mode::GLIDING || m_Mode == Mode::WINGSUIT || m_Mode == Mode::SKIING) && dt > 0.0f)
		{
			// Turn rate around the vertical axis
			const glm::vec2 h0(m_LastVelocity.x, m_LastVelocity.z);
			const glm::vec2 h1(velocity.x, velocity.z);
			if (glm::length(h0) > 2.0f && glm::length(h1) > 2.0f)
			{
				const real turnRate = std::atan2(h0.x * h1.y - h0.y * h1.x, glm::dot(h0, h1)) / dt;
				targetRoll = glm::clamp(turnRate * 0.08f, -0.2f, 0.2f);
			}
		}
		m_CameraRoll = glm::mix(m_CameraRoll, targetRoll, ExpDecay(4.0f, dt));
		m_LastVelocity = velocity;
	}

	void Traversal::UpdateSurroundings()
	{
		m_Ground = ProbeGround(400.0f);
		const real halfHeight = m_Player->m_Height * 0.5f;
		m_HeightAboveGround = m_Ground.bHit ? m_Ground.distance - halfHeight : 400.0f;

		const glm::vec3 pos = m_Player->GetTransform()->GetWorldPosition();
		m_Lift = (m_Mode == Mode::GLIDING || m_Mode == Mode::WINGSUIT) ? ComputeLift(pos) : 0.0f;
		m_GroundSlipperiness = (m_Mode == Mode::SKIING && m_Ground.distance < halfHeight + 1.0f) ? GetGroundSlipperiness(m_Ground) : m_GroundSlipperiness;
	}

	void Traversal::UpdateInput()
	{
		if (!CanControl())
		{
			if (m_Mode == Mode::CLIMBING || m_Mode == Mode::SKIING)
			{
				SetMode(Mode::NONE);
			}
			m_bJumpHeld = false;
			return;
		}

		// The place item button jumps when there's nothing to place
		const bool bJumpDown = g_InputManager->GetActionAxisValue(Action::PLACE_ITEM) > 0.0f &&
			!m_Player->m_bPreviewPlaceItemFromInventory &&
			!m_Player->m_Controller->IsPlaceableItemSelected();
		if (bJumpDown && !m_bJumpHeld)
		{
			OnJumpPressed();
		}
		else if (!bJumpDown && m_bJumpHeld)
		{
			OnJumpReleased();
		}

		// Grapple: fire, or release if already out
		if (g_InputManager->GetKeyPressed(GameKeys::GRAPPLING_HOOK))
		{
			if (m_Hook.state == HookState::NONE)
			{
				FireHook();
			}
			else
			{
				ReleaseHook();
			}
		}

		const bool bCrouchHeld = g_InputManager->GetKeyDown(GameKeys::CROUCH) != 0;
		const bool bClimbHeld = g_InputManager->GetKeyDown(GameKeys::CLIMB) != 0;

		m_Hook.bReelingIn = m_Hook.state == HookState::ATTACHED && m_bJumpHeld;
		m_Hook.bReelingOut = m_Hook.state == HookState::ATTACHED && bCrouchHeld;

		switch (m_Mode)
		{
		case Mode::NONE:
		{
			if (TryStartClimbingObject(bClimbHeld))
			{
				break;
			}
			if (bClimbHeld && !m_bStaminaExhausted && TryStartClimbing())
			{
				break;
			}
			if (bCrouchHeld && m_Hook.state != HookState::ATTACHED)
			{
				if (m_bGrounded)
				{
					if (m_Settings.bSkiingEnabled)
					{
						SetMode(Mode::SKIING);
					}
					m_GroundSlipperiness = GetGroundSlipperiness(m_Ground);
				}
				else if (m_HeightAboveGround > m_Settings.glideMinHeight * 2.0f)
				{
					SetMode(Mode::WINGSUIT);
				}
			}
		} break;
		case Mode::GLIDING:
		{
			if (bCrouchHeld)
			{
				SetMode(Mode::WINGSUIT);
			}
			else if (bClimbHeld && !m_bStaminaExhausted)
			{
				TryStartClimbing();
			}
		} break;
		case Mode::WINGSUIT:
		{
			if (!bCrouchHeld)
			{
				// Re-open the glider rather than dropping like a stone
				SetMode(Mode::GLIDING);
			}
		} break;
		case Mode::CLIMBING:
		{
			// Finish climbing onto a ledge even if shift is released, & ladders & ropes don't need it held
			if (!bClimbHeld && !m_bMantling && !m_ClimbObjectID.IsValid())
			{
				SetMode(Mode::NONE);
			}
		} break;
		case Mode::SKIING:
		{
			if (!bCrouchHeld)
			{
				SetMode(Mode::NONE);
			}
		} break;
		default:
			break;
		}
	}

	bool Traversal::TryStartClimbing()
	{
		const glm::vec3 pos = m_Player->GetTransform()->GetWorldPosition();
		const glm::vec3 heading = GetHeadingXZ();
		glm::vec3 hitPos, hitNormal;
		// Walls the player could just walk up aren't climbed
		if (!Raycast(pos, pos + heading * m_Settings.climbReach, hitPos, hitNormal) || hitNormal.y > Player::WALKABLE_NORMAL_Y)
		{
			return false;
		}

		m_WallNormal = hitNormal;
		if (m_Hook.state != HookState::NONE)
		{
			ReleaseHook();
		}
		SetMode(Mode::CLIMBING);
		return true;
	}

	void Traversal::FireHook()
	{
		BaseCamera* camera = g_CameraManager->CurrentCamera();
		const glm::vec3 origin = camera->position;
		const glm::vec3 dir = GetLookDirection();

		glm::vec3 hitPos, hitNormal;
		// Trees can be grappled to (their foliage too, which nothing else collides with)
		if (!Raycast(origin, origin + dir * m_Settings.hookRange, hitPos, hitNormal, RAYCAST_FOLIAGE))
		{
			// Nothing in range: fling the hook out & reel it back in
			hitPos = origin + dir * m_Settings.hookRange;
			m_Hook.target = hitPos;
			m_Hook.state = HookState::FLYING;
			m_Hook.ropeLength = -1.0f;
		}
		else
		{
			m_Hook.target = hitPos;
			m_Hook.targetNormal = hitNormal;
			m_Hook.state = HookState::FLYING;
			m_Hook.ropeLength = 0.0f;
			m_Hook.anchorCheckTimer = 0.0f;
			m_Hook.anchorMissCount = 0;
		}

		m_Hook.pos = GetHandPos();
		AudioManager::PlaySource(m_HookFireID);
	}

	void Traversal::ReleaseHook()
	{
		if (m_Hook.state == HookState::ATTACHED || m_Hook.state == HookState::FLYING)
		{
			m_Hook.state = HookState::RETRACTING;
		}
		m_Hook.bReelingIn = false;
		m_Hook.bReelingOut = false;
	}

	void Traversal::UpdateHook()
	{
		const sec dt = g_DeltaTime;
		const glm::vec3 handPos = GetHandPos();

		switch (m_Hook.state)
		{
		case HookState::FLYING:
		{
			const glm::vec3 toTarget = m_Hook.target - m_Hook.pos;
			const real dist = glm::length(toTarget);
			const real step = m_Settings.hookSpeed * dt;
			if (dist <= step)
			{
				m_Hook.pos = m_Hook.target;
				if (m_Hook.ropeLength < 0.0f)
				{
					// Missed
					m_Hook.state = HookState::RETRACTING;
				}
				else
				{
					m_Hook.state = HookState::ATTACHED;
					m_Hook.ropeLength = glm::distance(m_Player->GetTransform()->GetWorldPosition(), m_Hook.pos);
					AudioManager::PlaySource(m_HookAttachID);
					if (m_Mode == Mode::GLIDING || m_Mode == Mode::WINGSUIT || m_Mode == Mode::SKIING)
					{
						SetMode(Mode::NONE);
					}
				}
			}
			else
			{
				m_Hook.pos += toTarget * (step / dist);
			}
		} break;
		case HookState::RETRACTING:
		{
			const glm::vec3 toHand = handPos - m_Hook.pos;
			const real dist = glm::length(toHand);
			const real step = m_Settings.hookSpeed * 1.5f * dt;
			if (dist <= step)
			{
				m_Hook.state = HookState::NONE;
			}
			else
			{
				m_Hook.pos += toHand * (step / dist);
			}
		} break;
		case HookState::ATTACHED:
		{
			if (glm::distance(m_Player->GetTransform()->GetWorldPosition(), m_Hook.pos) > m_Settings.hookRange * 1.25f)
			{
				ReleaseHook();
				break;
			}

			// Let go if what the hook's caught on disappears (a tree being chopped down, or the terrain dug away)
			m_Hook.anchorCheckTimer -= dt;
			if (m_Hook.anchorCheckTimer <= 0.0f)
			{
				m_Hook.anchorCheckTimer = 0.25f;
				const glm::vec3 normal = SafeNormalize(m_Hook.targetNormal, VEC3_UP);
				glm::vec3 hitPos, hitNormal;
				// Generously long (the terrain's density & its mesh differ by up to a voxel)
				TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
				const bool bInTerrain = terrainGenerator != nullptr &&
					(terrainGenerator->SampleDensity(m_Hook.pos - normal * 0.5f) < 0.0f || terrainGenerator->SampleDensity(m_Hook.pos - normal * 1.2f) < 0.0f);
				if (bInTerrain || Raycast(m_Hook.pos + normal * 1.5f, m_Hook.pos - normal * 1.5f, hitPos, hitNormal, RAYCAST_FOLIAGE))
				{
					m_Hook.anchorMissCount = 0;
				}
				else if (++m_Hook.anchorMissCount >= 3)
				{
					if (getenv("FLEX_TRAVERSAL_DEBUG") != nullptr)
					{
						Print("Traversal: grappling hook's anchor disappeared, releasing\n");
					}
					ReleaseHook();
				}
			}
		} break;
		default:
			break;
		}
	}

	bool Traversal::TossRope()
	{
		if (m_RopeToss.state != RopeTossState::NONE)
		{
			return false;
		}

		// Thrown a little upwards of where the player's looking
		m_RopeToss = {};
		m_RopeTossFailure.clear();
		m_RopeToss.state = RopeTossState::FLYING;
		m_RopeToss.pos = GetHandPos();
		m_RopeToss.velocity = GetLookDirection() * m_Settings.ropeTossSpeed + VEC3_UP * 2.0f;
		AudioManager::PlaySource(m_HookFireID);
		return true;
	}

	void Traversal::UpdateRopeToss()
	{
		const sec dt = glm::min(g_DeltaTime, 0.05f);
		if (dt <= 0.0f)
		{
			return;
		}

		switch (m_RopeToss.state)
		{
		case RopeTossState::FLYING:
		{
			m_RopeToss.time += dt;
			const glm::vec3 newPos = m_RopeToss.pos + m_RopeToss.velocity * dt;
			m_RopeToss.velocity += GRAVITY * dt;
			glm::vec3 hitPos, hitNormal;
			if (Raycast(m_RopeToss.pos, newPos, hitPos, hitNormal))
			{
				glm::vec3 anchor, mid, bottom;
				if (ComputeRopeHang(hitPos, hitNormal, anchor, mid, bottom))
				{
					m_RopeToss.state = RopeTossState::FALLING;
					m_RopeToss.anchor = anchor;
					m_RopeToss.mid = mid;
					m_RopeToss.bottom = bottom;
					m_RopeToss.pos = anchor;
					m_RopeToss.time = 0.0f;
					AudioManager::PlaySource(m_HookAttachID);
				}
				else
				{
					// Nothing to hang from (e.g. it landed on flat ground)
					m_RopeToss.state = RopeTossState::NONE;
					char buffer[160];
					snprintf(buffer, ARRAY_LENGTH(buffer), "nothing to hang from where it landed (%.1f, %.1f, %.1f), normal (%.2f, %.2f, %.2f)",
						hitPos.x, hitPos.y, hitPos.z, hitNormal.x, hitNormal.y, hitNormal.z);
					m_RopeTossFailure = buffer;
				}
			}
			else if (m_RopeToss.time > 4.0f)
			{
				m_RopeToss.state = RopeTossState::NONE;
				m_RopeTossFailure = "didn't land on anything";
			}
			else
			{
				m_RopeToss.pos = newPos;
			}
		} break;
		case RopeTossState::FALLING:
		{
			// The free end drops down, unrolling the rope
			m_RopeToss.time += dt;
			const real upperLength = glm::distance(m_RopeToss.anchor, m_RopeToss.mid);
			const real lowerLength = glm::distance(m_RopeToss.mid, m_RopeToss.bottom);
			const real dropped = 0.5f * 9.81f * m_RopeToss.time * m_RopeToss.time + 2.0f * m_RopeToss.time;
			if (dropped >= upperLength + lowerLength)
			{
				SpawnTossedRope();
				m_RopeToss.state = RopeTossState::NONE;
			}
			else if (dropped < upperLength)
			{
				m_RopeToss.pos = glm::mix(m_RopeToss.anchor, m_RopeToss.mid, dropped / glm::max(upperLength, 1e-3f));
			}
			else
			{
				m_RopeToss.pos = glm::mix(m_RopeToss.mid, m_RopeToss.bottom, (dropped - upperLength) / glm::max(lowerLength, 1e-3f));
			}
		} break;
		default:
			break;
		}
	}

	bool Traversal::ComputeRopeHang(const glm::vec3& hitPos, const glm::vec3& hitNormal, glm::vec3& outTop, glm::vec3& outMid, glm::vec3& outBottom) const
	{
		const glm::vec3 playerPos = m_Player->GetTransform()->GetWorldPosition();
		const glm::vec3 towardsPlayer = SafeNormalize(glm::vec3(playerPos.x - hitPos.x, 0.0f, playerPos.z - hitPos.z), -GetHeadingXZ());
		glm::vec3 p, n;

		// Where the rope hangs from, & the (horizontal) direction it falls away from the surface in
		glm::vec3 anchor;
		glm::vec3 outwards;
		glm::vec3 lipSearchStart = hitPos;
		bool bOnTop = hitNormal.y > 0.5f;
		if (!bOnTop && hitNormal.y > -0.5f)
		{
			// Hit a wall: if its top is just above, hang over that instead of from the wall itself
			const glm::vec3 wallOut = SafeNormalize(glm::vec3(hitNormal.x, 0.0f, hitNormal.z), towardsPlayer);
			for (real h = 0.5f; h <= 3.01f; h += 0.5f)
			{
				const glm::vec3 from = hitPos + wallOut * 0.3f + VEC3_UP * h;
				if (!Raycast(from, from - wallOut * 1.0f, p, n))
				{
					// Open above the wall, find its top
					const glm::vec3 above = hitPos - wallOut * 0.4f + VEC3_UP * (h + 0.3f);
					if (Raycast(above, above - VEC3_UP * (h + 0.5f), p, n) && n.y > 0.5f)
					{
						lipSearchStart = p;
						bOnTop = true;
					}
					break;
				}
			}
			if (!bOnTop)
			{
				// Snagged on the wall
				outwards = wallOut;
				anchor = hitPos + wallOut * 0.3f;
			}
		}
		else if (!bOnTop)
		{
			// Caught on a ceiling
			outwards = towardsPlayer;
			anchor = hitPos - VEC3_UP * 0.15f;
		}

		if (bOnTop)
		{
			// Landed on top of something: walk towards the player to find the edge it can hang over
			outwards = towardsPlayer;
			real topY = lipSearchStart.y;
			bool bFoundEdge = false;
			for (real s = 0.25f; s <= 8.0f; s += 0.25f)
			{
				const glm::vec3 q = lipSearchStart + outwards * s;
				// The edge is where the ground drops away steeply (or ends)
				if (!Raycast(q + VEC3_UP * 1.5f, q - VEC3_UP * 1.5f, p, n) || p.y < topY - 0.3f)
				{
					// Just past the last point on top
					anchor = lipSearchStart + outwards * (s - 0.15f);
					anchor.y = topY + 0.08f;
					bFoundEdge = true;
					break;
				}
				if (p.y > topY + 1.0f)
				{
					// Rising towards the player (it's a slope or a step up, not a ledge)
					break;
				}
				topY = p.y;
			}
			if (!bFoundEdge)
			{
				return false;
			}
		}

		// The rope follows the ground's profile outwards from the anchor until it reaches a drop (where it hangs
		// straight down from), or the foot of a slope. Over a rounded lip that's two straight pieces: one lying over
		// the lip, & one hanging down from where the drop begins.
		const real sampleSpacing = 0.25f;
		const u32 sampleCount = 64;
		const real maxLength = m_Settings.ropeMaxLength;
		glm::vec3 prev = anchor - VEC3_UP * 0.1f; // Ground under the previous sample
		glm::vec3 mid = anchor;
		glm::vec3 bottom = anchor;
		bool bFound = false;
		for (u32 i = 1; i <= sampleCount; ++i)
		{
			const glm::vec3 q = anchor + outwards * (sampleSpacing * (real)i);
			const bool bHit = Raycast(q + VEC3_UP * 0.5f, q - VEC3_UP * maxLength, p, n);
			const glm::vec3 ground = bHit ? p : q - VEC3_UP * maxLength;
			if (prev.y - ground.y > sampleSpacing * 3.0f)
			{
				// A drop (steeper than ~70 degrees): hang straight down from just over its edge (the ray down to the
				// ground here was clear)
				mid = glm::vec3(q.x, glm::max(prev.y + 0.1f, ground.y), q.z);
				bottom = ground;
				bFound = true;
				break;
			}
			if (anchor.y - ground.y > 2.0f && prev.y - ground.y < sampleSpacing * 0.6f)
			{
				// Reached gentle ground at the foot of a slope
				mid = anchor;
				bottom = ground;
				bFound = true;
				break;
			}
			prev = ground;
		}
		if (!bFound)
		{
			if (anchor.y - prev.y < 1.5f)
			{
				return false;
			}
			mid = anchor;
			bottom = prev;
		}
		// Ends just above the ground
		bottom += VEC3_UP * 0.3f;

		if (glm::distance(anchor, mid) < 0.75f)
		{
			// Hanging straight down from the anchor (moved over the drop)
			mid.y = anchor.y;
			outTop = mid;
			outMid = mid;
		}
		else
		{
			outTop = anchor;
			outMid = mid;
		}
		// No longer than the rope
		const real upperLength = glm::distance(outTop, outMid);
		const real lowerLength = glm::distance(outMid, bottom);
		if (upperLength + lowerLength < 1.5f || upperLength > maxLength)
		{
			return false;
		}
		const real maxLowerLength = maxLength - upperLength;
		outBottom = lowerLength > maxLowerLength ? outMid + (bottom - outMid) * (maxLowerLength / lowerLength) : bottom;
		return true;
	}

	void Traversal::SpawnTossedRope()
	{
		// One piece per straight section (ropes over a rounded lip have two), climbed one after the other
		BaseScene* scene = g_SceneManager->CurrentScene();
		const glm::vec3 points[] = { m_RopeToss.anchor, m_RopeToss.mid, m_RopeToss.bottom };
		for (u32 i = 0; i < 2; ++i)
		{
			const glm::vec3 toBottom = points[i + 1] - points[i];
			const real length = glm::length(toBottom);
			if (length < 0.3f)
			{
				continue;
			}
			BuildPiece* rope = new BuildPiece(scene->GetUniqueObjectName("Rope_", 3));
			rope->Setup(BuildPieceType::ROPE, BuildMaterial::WOOD, length);
			rope->GetTransform()->SetWorldPosition(points[i]);
			// Ropes hang along their local -y
			rope->GetTransform()->SetWorldRotation(glm::rotation(-VEC3_UP, toBottom / length));
			scene->AddRootObject(rope);
			m_LastTossedRopeID = rope->ID;
		}
	}

	BuildPiece* Traversal::FindConnectedRope(BuildPiece* rope, bool bAbove) const
	{
		const glm::vec3 end = bAbove ? rope->GetClimbTop() : rope->GetClimbBottom();
		for (BuildPiece* piece : BuildPiece::GetAllPieces())
		{
			if (piece != rope && piece->GetPieceType() == BuildPieceType::ROPE &&
				glm::distance(bAbove ? piece->GetClimbBottom() : piece->GetClimbTop(), end) < 0.4f)
			{
				return piece;
			}
		}
		return nullptr;
	}

	bool Traversal::FixedUpdate()
	{
		if (m_Player->GetRigidBody() == nullptr)
		{
			return false;
		}

		const sec dt = g_FixedDeltaTime;
		btRigidBody* rb = m_Player->GetRigidBody()->GetRigidBodyInternal();
		glm::vec3 velocity = ToVec3(rb->getLinearVelocity());

		UpdateBodyTiltTargets(velocity);

		m_bGrounded = m_Player->m_bGrounded;
		if (m_bGrounded)
		{
			m_TimeSinceGrounded = 0.0f;
			if (m_Mode == Mode::GLIDING || m_Mode == Mode::WINGSUIT)
			{
				// Landed
				SetMode(Mode::NONE);
			}
			if (m_Mode != Mode::CLIMBING)
			{
				m_Stamina = glm::min(m_Stamina + m_Settings.staminaRegen * dt, 1.0f);
				if (m_Stamina >= 1.0f)
				{
					m_bStaminaExhausted = false;
				}
			}
		}
		else
		{
			m_TimeSinceGrounded += dt;
		}

		m_TimeSinceJump += dt;
		if (m_bJumpQueued)
		{
			if ((m_bGrounded || m_TimeSinceGrounded < m_Settings.coyoteTime) && (m_Mode == Mode::NONE || m_Mode == Mode::SKIING))
			{
				m_bJumpQueued = false;
				// Jumping off a slope pushes away from it a little, rather than straight up
				const glm::vec3 groundNormal = m_Player->GetGroundNormal();
				const glm::vec3 jumpDir = SafeNormalize(VEC3_UP + glm::vec3(groundNormal.x, 0.0f, groundNormal.z) * 0.3f, VEC3_UP);
				velocity.y = glm::max(velocity.y, 0.0f);
				velocity += jumpDir * m_Settings.jumpSpeed;
				m_TimeSinceGrounded = m_Settings.coyoteTime;
				m_TimeSinceJump = 0.0f;
				m_bGrounded = false;
				GameSounds::PlayJump();
			}
			else
			{
				// Buffered: jump on landing if it's soon enough
				m_JumpQueuedTime += dt;
				if (m_JumpQueuedTime > m_Settings.jumpBufferTime || m_Mode != Mode::NONE)
				{
					m_bJumpQueued = false;
				}
			}
		}

		// Pull up over steep lips & ledges being walked/jumped into (before the mode is handled, so it starts this step)
		m_MantleCooldown = glm::max(m_MantleCooldown - dt, 0.0f);
		if (m_Mode == Mode::NONE && m_Hook.state != HookState::ATTACHED)
		{
			TryAutoMantle(velocity);
		}

		bool bControlled = false;
		switch (m_Mode)
		{
		case Mode::GLIDING:
			FixedUpdateGlide(velocity);
			bControlled = true;
			break;
		case Mode::WINGSUIT:
			FixedUpdateWingsuit(velocity);
			bControlled = true;
			break;
		case Mode::CLIMBING:
			bControlled = FixedUpdateClimb(velocity);
			break;
		case Mode::SKIING:
			FixedUpdateSki(velocity);
			bControlled = true;
			break;
		default:
			break;
		}

		if (m_Hook.state == HookState::ATTACHED)
		{
			FixedUpdateHook(velocity);
			// Reeling in pulls the player off the ground (walking would cancel it out)
			bControlled = bControlled || !m_bGrounded || m_Hook.bReelingIn;
		}

		if (!bControlled && m_Mode == Mode::NONE && !m_bGrounded)
		{
			// Keep momentum in the air (e.g. after letting go of the rope), with a little air control
			const glm::vec3 heading = GetHeadingXZ();
			const glm::vec3 right = glm::cross(heading, VEC3_UP);
			const glm::vec3 input = heading * GetMoveForward() + right * GetMoveRight();
			const glm::vec3 horizontal(velocity.x, 0.0f, velocity.z);
			const real airControl = 6.0f;
			glm::vec3 newHorizontal = horizontal + input * airControl * dt;
			// Air control can't speed up beyond walking speed, only steer
			const real maxSpeed = glm::max(glm::length(horizontal), 8.0f);
			if (glm::length(newHorizontal) > maxSpeed)
			{
				newHorizontal = glm::normalize(newHorizontal) * maxSpeed;
			}
			velocity.x = newHorizontal.x;
			velocity.z = newHorizontal.z;

			// World gravity (applied by the physics world) is boosted for a quick, punchy arc. Letting go of jump
			// early cuts the jump short.
			real gravityScale = m_Settings.fallGravityScale;
			if (velocity.y > 0.0f)
			{
				gravityScale = (m_bJumpHeld && m_TimeSinceJump < 0.6f) ? m_Settings.riseGravityScale : m_Settings.jumpCutGravityScale;
			}
			velocity.y += GRAVITY.y * (gravityScale - 1.0f) * dt;
			velocity.y = glm::max(velocity.y, -m_Settings.maxFallSpeed);
			bControlled = true;
		}

		if (bControlled)
		{
			rb->setFriction(0.0f);
			rb->activate();
		}
		rb->setLinearVelocity(ToBtVec3(velocity));
		return bControlled;
	}

	void Traversal::FixedUpdateGlide(glm::vec3& velocity)
	{
		const sec dt = g_FixedDeltaTime;
		const glm::vec3 heading = GetHeadingXZ();
		const glm::vec3 right = glm::cross(heading, VEC3_UP);
		const real forward = GetMoveForward();

		const real targetSpeed = m_Settings.glideSpeed + forward * m_Settings.glideSpeedBoost;
		// Flying faster sinks faster
		const real sinkRate = m_Settings.glideSinkRate * (1.0f + 0.4f * forward);
		const glm::vec3 targetHorizontal = heading * targetSpeed + right * GetMoveRight() * 3.0f;

		glm::vec3 horizontal(velocity.x, 0.0f, velocity.z);
		horizontal = glm::mix(horizontal, targetHorizontal, ExpDecay(1.3f, dt));
		const real targetVertical = -sinkRate + m_Lift;
		velocity.y = glm::mix(velocity.y, targetVertical, ExpDecay(velocity.y < targetVertical - 6.0f ? 4.0f : 1.8f, dt));
		velocity.x = horizontal.x;
		velocity.z = horizontal.z;
	}

	void Traversal::FixedUpdateWingsuit(glm::vec3& velocity)
	{
		const sec dt = g_FixedDeltaTime;
		const glm::vec3 look = GetLookDirection();

		glm::vec3 acceleration = GRAVITY;
		const real speed = glm::length(velocity);
		if (speed > 0.5f)
		{
			// Lift turns the velocity towards where the player is looking (doing no work), drag slows them down
			const glm::vec3 velocityDir = velocity / speed;
			const real cosAlpha = glm::clamp(glm::dot(velocityDir, look), -1.0f, 1.0f);
			const real alpha = std::acos(cosAlpha);
			const glm::vec3 perpendicular = look - velocityDir * cosAlpha;
			const real perpendicularLength = glm::length(perpendicular);
			// Lift doesn't drop off sharply past a stall angle, which would make pulling out of dives frustrating
			const real maxLiftAngle = glm::radians(m_Settings.wingsuitMaxLiftAngle);
			if (perpendicularLength > 1e-4f)
			{
				const real efficiency = std::sin(glm::min(alpha, maxLiftAngle));
				const real lift = glm::min(m_Settings.wingsuitLift * speed * speed * efficiency, m_Settings.wingsuitMaxLift);
				acceleration += (perpendicular / perpendicularLength) * lift;
			}
			acceleration -= velocityDir * (m_Settings.wingsuitDrag * speed * speed);
		}

		// A little help from rising air
		acceleration.y += m_Lift * 0.3f;

		velocity += acceleration * dt;
	}

	bool Traversal::FindLedge(const glm::vec3& wallNormal, glm::vec3& outStandPos) const
	{
		// Looks for something to stand on just over the top of the wall: down from above the player's head, a little
		// way into the wall. Rounded lips are found too (anything walkable within reach).
		const glm::vec3 pos = m_Player->GetTransform()->GetWorldPosition();
		const real halfHeight = m_Player->GetHeight() * 0.5f;
		const real radius = m_Player->GetRadius();
		const glm::vec3 inwards = SafeNormalize(glm::vec3(-wallNormal.x, 0.0f, -wallNormal.z), -wallNormal);
		for (real inset : { radius + 0.35f, radius + 0.8f, radius + 1.3f, radius + 1.9f })
		{
			const glm::vec3 top = pos + VEC3_UP * (halfHeight + 1.4f) - wallNormal * inset;
			glm::vec3 hitPos, hitNormal;
			// The start must be in open air (the wall may continue upwards)
			if (Raycast(pos + VEC3_UP * (halfHeight + 1.4f), top, hitPos, hitNormal))
			{
				continue;
			}
			if (Raycast(top, top - VEC3_UP * (halfHeight * 2.0f + 1.6f), hitPos, hitNormal) && hitNormal.y > 0.5f && HasFooting(hitPos, hitNormal, inwards))
			{
				outStandPos = hitPos + VEC3_UP * (halfHeight + 0.1f);
				// Not a ledge if it's below where the player already is
				if (outStandPos.y < pos.y - 0.25f)
				{
					continue;
				}
				return true;
			}
		}
		return false;
	}

	bool Traversal::HasFooting(const glm::vec3& groundPos, const glm::vec3& groundNormal, const glm::vec3& dir) const
	{
		// Solid footing, not just a sliver of flat ground on a slope (which the player would slide straight off): the
		// ground under the whole capsule must be walkable
		if (groundNormal.y < Player::WALKABLE_NORMAL_Y + 0.1f)
		{
			return false;
		}
		const real radius = m_Player->GetRadius();
		const glm::vec3 forward = SafeNormalize(glm::vec3(dir.x, 0.0f, dir.z), VEC3_FORWARD) * radius;
		const glm::vec3 side = glm::cross(forward, VEC3_UP);
		for (const glm::vec3& offset : { forward, side, -side, -forward })
		{
			const glm::vec3 probe = groundPos + offset;
			glm::vec3 footPos, footNormal;
			if (!Raycast(probe + VEC3_UP * 0.6f, probe - VEC3_UP * 0.6f, footPos, footNormal, RAYCAST_PHYSICS_ONLY) || footNormal.y < Player::WALKABLE_NORMAL_Y)
			{
				return false;
			}
		}
		return true;
	}

	void Traversal::StartMantle(const glm::vec3& standPos)
	{
		const glm::vec3 pos = m_Player->GetTransform()->GetWorldPosition();
		const real halfHeight = m_Player->GetHeight() * 0.5f;

		// Rise until the bottom of the body clears the highest ground between here & the target (rounded lips are
		// often higher than the point being climbed onto)
		m_MantleClearY = standPos.y;
		const glm::vec3 delta(standPos.x - pos.x, 0.0f, standPos.z - pos.z);
		const real probeTop = glm::max(standPos.y, pos.y) + halfHeight + 0.4f;
		for (u32 i = 1; i <= 6; ++i)
		{
			const glm::vec3 q = pos + delta * ((real)i / 6.0f);
			glm::vec3 hitPos, hitNormal;
			if (Raycast(glm::vec3(q.x, probeTop, q.z), glm::vec3(q.x, standPos.y - halfHeight - 0.5f, q.z), hitPos, hitNormal, RAYCAST_PHYSICS_ONLY))
			{
				m_MantleClearY = glm::max(m_MantleClearY, hitPos.y + halfHeight + 0.08f);
			}
		}
		m_MantleClearY = glm::min(m_MantleClearY, standPos.y + 1.0f);
		if (getenv("FLEX_TRAVERSAL_DEBUG") != nullptr)
		{
			Print("Traversal: mantling from (%.1f, %.1f, %.1f) onto (%.1f, %.1f, %.1f), clearing %.1f\n", pos.x, pos.y, pos.z, standPos.x, standPos.y, standPos.z, m_MantleClearY);
		}

		if (m_Hook.state != HookState::NONE)
		{
			ReleaseHook();
		}
		SetMode(Mode::CLIMBING);
		m_ClimbObjectID = InvalidGameObjectID;
		m_bMantling = true;
		m_MantleTime = 0.0f;
		m_MantleLastY = pos.y;
		m_MantleTarget = standPos;
		m_WallNormal = SafeNormalize(-delta, m_WallNormal);
	}

	bool Traversal::FindMantleTarget(const glm::vec3& dir, real minHeight, real maxHeight, glm::vec3& outStandPos) const
	{
		const glm::vec3 pos = m_Player->GetTransform()->GetWorldPosition();
		const real height = m_Player->GetHeight();
		const real halfHeight = height * 0.5f;
		const real radius = m_Player->GetRadius();
		const real feetY = pos.y - halfHeight;
		const real topY = feetY + maxHeight + 0.35f;
		glm::vec3 hitPos, hitNormal;

		// Room above the player's head to rise into
		if (topY > pos.y && Raycast(pos, glm::vec3(pos.x, topY + 0.2f, pos.z), hitPos, hitNormal, RAYCAST_PHYSICS_ONLY))
		{
			return false;
		}

		const glm::vec3 topStart(pos.x, topY, pos.z);
		for (real inset : { radius + 0.3f, radius + 0.6f, radius + 0.95f, radius + 1.35f, radius + 1.9f, radius + 2.5f })
		{
			const glm::vec3 top = topStart + dir * inset;
			// The way over the lip must be open (otherwise the wall continues up past reach)
			if (Raycast(topStart, top, hitPos, hitNormal, RAYCAST_PHYSICS_ONLY))
			{
				return false;
			}
			if (!Raycast(top, glm::vec3(top.x, feetY + minHeight - 0.05f, top.z), hitPos, hitNormal, RAYCAST_PHYSICS_ONLY))
			{
				continue;
			}
			const real ledgeHeight = hitPos.y - feetY;
			if (hitNormal.y < Player::WALKABLE_NORMAL_Y || ledgeHeight < minHeight || ledgeHeight > maxHeight)
			{
				continue;
			}
			// Room to stand there
			glm::vec3 ceilingPos, ceilingNormal;
			if (Raycast(hitPos + VEC3_UP * 0.05f, hitPos + VEC3_UP * (height + 0.1f), ceilingPos, ceilingNormal, RAYCAST_PHYSICS_ONLY))
			{
				continue;
			}
			if (!HasFooting(hitPos, hitNormal, dir))
			{
				continue;
			}
			outStandPos = hitPos + VEC3_UP * (halfHeight + 0.05f);
			return true;
		}
		return false;
	}

	bool Traversal::TryAutoMantle(const glm::vec3& velocity)
	{
		if (m_MantleCooldown > 0.0f || !CanControl())
		{
			return false;
		}

		// Only when moving forwards into something (mantling while backing or strafing would be surprising)
		const glm::vec3 heading = GetHeadingXZ();
		const glm::vec3 right = glm::cross(heading, VEC3_UP);
		glm::vec3 moveDir = heading * GetMoveForward() + right * GetMoveRight();
		const real moveLength = glm::length(moveDir);
		if (moveLength < 0.5f)
		{
			return false;
		}
		moveDir /= moveLength;
		if (glm::dot(moveDir, heading) < 0.3f || velocity.y < -12.0f)
		{
			return false;
		}

		// Something steep in the way, at knee or chest height
		const glm::vec3 pos = m_Player->GetTransform()->GetWorldPosition();
		const real feetY = pos.y - m_Player->GetHeight() * 0.5f;
		const real reach = m_Player->GetRadius() + 0.45f;
		glm::vec3 hitPos(0.0f), hitNormal(0.0f);
		bool bBlocked = false;
		for (real height : { 0.4f, 1.1f })
		{
			const glm::vec3 from(pos.x, feetY + height, pos.z);
			if (Raycast(from, from + moveDir * reach, hitPos, hitNormal, RAYCAST_PHYSICS_ONLY) &&
				hitNormal.y < Player::WALKABLE_NORMAL_Y && hitNormal.y > -0.3f)
			{
				bBlocked = true;
				break;
			}
		}
		if (!bBlocked)
		{
			return false;
		}

		// Climb towards the wall (rather than along the move direction, so glancing approaches still work)
		const glm::vec3 wallDir = SafeNormalize(glm::vec3(-hitNormal.x, 0.0f, -hitNormal.z), moveDir);
		const glm::vec3 dir = SafeNormalize(wallDir + moveDir, wallDir);
		// Hands reach higher while in the air (jumping, falling or sliding down a steep slope) than a step up
		const bool bReaching = !m_bGrounded || m_bJumpHeld;
		const real maxHeight = bReaching ? m_Settings.mantleReach : m_Settings.stepAssistHeight;
		glm::vec3 standPos;
		if (!FindMantleTarget(dir, 0.35f, maxHeight, standPos))
		{
			return false;
		}

		m_bJumpQueued = false;
		StartMantle(standPos);
		return true;
	}

	bool Traversal::TryStartClimbingObject(bool bClimbHeld)
	{
		const glm::vec3 pos = m_Player->GetTransform()->GetWorldPosition();
		// Ropes & ladders hanging above the player's head can be reached up to & grabbed (jumping, or just reaching)
		BuildPiece* climbable = BuildPiece::FindClimbable(pos, m_Player->GetRadius() + 0.6f,
			m_bGrounded ? CLIMB_GRAB_REACH_ABOVE : CLIMB_GRAB_REACH_ABOVE_AIRBORNE);
		if (climbable == nullptr)
		{
			return false;
		}

		// Walking into it (or holding the climb key) grabs on
		const glm::vec3 toClimbable = climbable->ClosestPointOnClimbAxis(pos) - pos;
		const glm::vec3 toClimbableXZ = SafeNormalize(glm::vec3(toClimbable.x, 0.0f, toClimbable.z), GetHeadingXZ());
		const glm::vec3 moveDir = GetHeadingXZ() * GetMoveForward() + glm::cross(GetHeadingXZ(), VEC3_UP) * GetMoveRight();
		const bool bWalkingInto = glm::dot(moveDir, toClimbableXZ) > 0.5f;
		// Ladders can only be climbed from the front
		const glm::vec3 facing = climbable->GetClimbFacing();
		if (glm::length(facing) > 0.5f && glm::dot(-toClimbableXZ, facing) < 0.0f)
		{
			return false;
		}
		// Approaching the top of a ladder from above, walking onto it would grab on immediately, so only climb down
		// when looking down
		const bool bAboveTop = pos.y - m_Player->GetHeight() * 0.5f > climbable->GetClimbTop().y - 0.5f;
		if (bAboveTop && GetLookDirection().y > -0.5f)
		{
			return false;
		}
		if (!bWalkingInto && !bClimbHeld)
		{
			return false;
		}

		if (m_Hook.state != HookState::NONE)
		{
			ReleaseHook();
		}
		SetMode(Mode::CLIMBING);
		m_ClimbObjectID = climbable->ID;
		return true;
	}

	bool Traversal::FixedUpdateClimbObject(glm::vec3& velocity)
	{
		BuildPiece* climbable = (BuildPiece*)m_ClimbObjectID.Get();
		if (climbable == nullptr)
		{
			SetMode(Mode::NONE);
			return false;
		}

		const sec dt = g_FixedDeltaTime;
		const glm::vec3 pos = m_Player->GetTransform()->GetWorldPosition();
		const real halfHeight = m_Player->GetHeight() * 0.5f;
		const real forward = GetMoveForward();
		const bool bRope = climbable->GetPieceType() == BuildPieceType::ROPE;

		const glm::vec3 axisPoint = climbable->ClosestPointOnClimbAxis(pos);
		glm::vec3 facing = climbable->GetClimbFacing();
		if (glm::length(facing) < 0.5f)
		{
			// Ropes are held from whichever side the player is on
			facing = SafeNormalize(glm::vec3(pos.x - axisPoint.x, 0.0f, pos.z - axisPoint.z), -GetHeadingXZ());
		}
		m_WallNormal = facing;
		// Tossed ropes may lie at an angle (down a slope)
		const glm::vec3 axisDir = SafeNormalize(climbable->GetClimbTop() - climbable->GetClimbBottom(), VEC3_UP);
		const glm::vec3 holdDir = SafeNormalize(facing - axisDir * glm::dot(facing, axisDir), facing);

		if (m_bLeapQueued)
		{
			// Jump off backwards
			m_bLeapQueued = false;
			SetMode(Mode::NONE);
			velocity = facing * 4.5f + VEC3_UP * 4.0f;
			return true;
		}

		const glm::vec3 top = climbable->GetClimbTop();
		const glm::vec3 bottom = climbable->GetClimbBottom();
		const real feetY = pos.y - halfHeight;

		// Tossed ropes laid over a rounded lip are in two pieces: carry on onto the next one
		if (bRope && forward != 0.0f && (forward > 0.0f ? pos.y > top.y - 0.1f : pos.y < bottom.y + 0.1f))
		{
			BuildPiece* next = FindConnectedRope(climbable, forward > 0.0f);
			if (next != nullptr)
			{
				m_ClimbObjectID = next->ID;
				return FixedUpdateClimbObject(velocity);
			}
		}

		// Climbed off the top: step onto whatever the ladder leads to (checked from a little below where climbing up
		// stops, see below)
		if (forward > 0.0f && (feetY > top.y - 0.6f || pos.y > top.y))
		{
			glm::vec3 standPos;
			// Ropes may be anchored on a rounded lip, with somewhere flat to stand a little further up & over
			if (FindLedge(facing, standPos) ||
				(bRope && FindMantleTarget(SafeNormalize(glm::vec3(-facing.x, 0.0f, -facing.z), GetHeadingXZ()), 0.0f, m_Settings.mantleReach + 1.0f, standPos)))
			{
				StartMantle(standPos);
				velocity = VEC3_UP * 3.0f;
				return true;
			}
			if (bRope)
			{
				// Nothing to climb onto, hang at the top
				static sec s_LastStallPrint = -100.0f;
				if (g_SecElapsedSinceProgramStart - s_LastStallPrint > 2.0f)
				{
					s_LastStallPrint = g_SecElapsedSinceProgramStart;
					const glm::vec3 mantleDir = SafeNormalize(glm::vec3(-facing.x, 0.0f, -facing.z), GetHeadingXZ());
					Print("Climb stalled at the top of a rope: no ledge to step onto. Player at (%.1f, %.1f, %.1f), rope top (%.1f, %.1f, %.1f), facing (%.2f, %.2f), mantle reach %.1fm along (%.2f, %.2f): FindLedge (wall normal %.2f, %.2f) found no flat standable spot (needs headroom above & ground within a step) & FindMantleTarget found none either\n",
						pos.x, pos.y, pos.z, top.x, top.y, top.z, facing.x, facing.z, m_Settings.mantleReach + 1.0f, mantleDir.x, mantleDir.z, facing.x, facing.z);
				}
				velocity = VEC3_ZERO;
				return true;
			}
		}

		// Reached the ground while climbing down
		if (forward < 0.0f && (m_Player->IsGrounded() || feetY < bottom.y - 0.3f))
		{
			SetMode(Mode::NONE);
			return false;
		}
		// Slid off the bottom of a rope
		// (not when grabbed from below, reaching up to a rope end & climbing towards it)
		if (forward <= 0.0f && pos.y + halfHeight < bottom.y)
		{
			SetMode(Mode::NONE);
			return false;
		}

		const real climbSpeed = bRope ? m_Settings.climbSpeed * 1.1f : m_Settings.climbSpeed * 1.6f;
		const glm::vec3 holdPos = axisPoint + holdDir * (m_Player->GetRadius() + 0.15f);
		// Along the rope/ladder, & pulled towards it
		real alongSpeed = forward * climbSpeed;
		if (forward > 0.0f && pos.y > top.y + halfHeight * 0.2f)
		{
			alongSpeed = 0.0f;
		}
		const glm::vec3 toHold = holdPos - pos;
		velocity = axisDir * alongSpeed + (toHold - axisDir * glm::dot(toHold, axisDir)) * 12.0f;

		if (bRope)
		{
			// Ropes are harder work than ladders
			m_Stamina -= (glm::abs(forward) > 0.1f ? m_Settings.climbMoveStaminaDrain : m_Settings.climbStaminaDrain) * 0.5f * dt;
			if (m_Stamina <= 0.0f)
			{
				m_Stamina = 0.0f;
				m_bStaminaExhausted = true;
				SetMode(Mode::NONE);
				return false;
			}
		}
		return true;
	}

	bool Traversal::FixedUpdateClimb(glm::vec3& velocity)
	{
		if (m_ClimbObjectID.IsValid() && !m_bMantling)
		{
			return FixedUpdateClimbObject(velocity);
		}

		const sec dt = g_FixedDeltaTime;
		const glm::vec3 pos = m_Player->GetTransform()->GetWorldPosition();
		const real forward = GetMoveForward();
		const real sideways = GetMoveRight();

		if (m_bMantling)
		{
			// Up first (so the body clears the lip), then over onto the ledge
			m_MantleTime += dt;
			const glm::vec3 toTarget = m_MantleTarget - pos;
			const real horizontalDist = glm::length(glm::vec2(toTarget.x, toTarget.z));
			const real clearY = glm::max(m_MantleClearY, m_MantleTarget.y);
			const bool bTimedOut = m_MantleTime > 1.6f;
			if (horizontalDist < 0.2f || bTimedOut)
			{
				m_bMantling = false;
				SetMode(Mode::NONE);
				// Step on a little so the whole body is over the ledge
				velocity = SafeNormalize(glm::vec3(toTarget.x, 0.0f, toTarget.z), -m_WallNormal) * 1.5f;
				if (bTimedOut)
				{
					// Something was in the way, don't immediately try again
					m_MantleCooldown = 0.75f;
				}
				else
				{
					++m_MantleCount;
					// Settle before another (pulling up onto a sliver on a slope could otherwise repeat)
					m_MantleCooldown = 0.4f;
				}
				return true;
			}
			const glm::vec3 horizontal = SafeNormalize(glm::vec3(toTarget.x, 0.0f, toTarget.z), -m_WallNormal);
			if (pos.y < clearY - 0.05f)
			{
				// Pressing into the wall a little keeps the body against it as it rises over a rounded lip. If an
				// overhanging lip stops it rising, lean back out from under it.
				const bool bStalled = m_MantleTime > 0.1f && pos.y - m_MantleLastY < 0.005f;
				m_MantleLastY = pos.y;
				velocity = VEC3_UP * glm::clamp((clearY - pos.y) * 10.0f, 2.5f, 7.0f) + horizontal * (bStalled ? -2.0f : 0.8f);
			}
			else
			{
				velocity = horizontal * glm::clamp(horizontalDist * 8.0f, 2.0f, 5.0f) + VEC3_UP * glm::max((clearY - pos.y) * 8.0f, 0.0f);
			}
			return true;
		}

		glm::vec3 hitPos, hitNormal;
		const real reach = m_Settings.climbReach + 0.4f;
		const bool bHit = Raycast(pos, pos - m_WallNormal * reach, hitPos, hitNormal);
		// Rounded lips & the tops of walls: pushing up/forward climbs over onto whatever's there
		const bool bNearTop = !bHit || hitNormal.y > 0.55f;
		if (bNearTop)
		{
			glm::vec3 standPos;
			if (forward > 0.0f && FindLedge(bHit ? SafeNormalize(m_WallNormal + hitNormal, m_WallNormal) : m_WallNormal, standPos))
			{
				StartMantle(standPos);
				velocity = VEC3_UP * 3.0f;
				return true;
			}
			if (!bHit || hitNormal.y > 0.8f)
			{
				SetMode(Mode::NONE);
				if (!bHit && forward > 0.0f)
				{
					// Nothing found to stand on, hop up & over anyway
					velocity = VEC3_UP * 5.0f - m_WallNormal * 2.5f;
					return true;
				}
				return false;
			}
		}

		m_WallNormal = SafeNormalize(m_WallNormal + (hitNormal - m_WallNormal) * 0.5f, hitNormal);
		const glm::vec3 wallUp = SafeNormalize(VEC3_UP - m_WallNormal * glm::dot(VEC3_UP, m_WallNormal), VEC3_UP);
		glm::vec3 wallRight = glm::cross(wallUp, m_WallNormal);
		if (glm::dot(wallRight, m_Player->GetTransform()->GetRight()) < 0.0f)
		{
			wallRight = -wallRight;
		}

		const bool bMoving = glm::abs(forward) > 0.1f || glm::abs(sideways) > 0.1f;
		const real tiredScale = m_Stamina < 0.25f ? 0.55f : 1.0f;
		glm::vec3 desired = (wallUp * forward + wallRight * sideways) * m_Settings.climbSpeed * tiredScale;

		// Stay just off the wall
		const real wallDistance = glm::dot(pos - hitPos, m_WallNormal);
		const real targetDistance = m_Player->GetRadius() + 0.1f;
		desired -= m_WallNormal * (wallDistance - targetDistance) * 8.0f;

		m_Stamina -= (m_Settings.climbStaminaDrain + (bMoving ? m_Settings.climbMoveStaminaDrain : 0.0f)) * dt;

		if (m_bLeapQueued)
		{
			m_bLeapQueued = false;
			if (m_Stamina > m_Settings.climbLeapStamina * 0.5f)
			{
				m_Stamina -= m_Settings.climbLeapStamina;
				desired = wallUp * 7.0f + wallRight * sideways * 3.0f + m_WallNormal * 0.5f;
			}
		}

		if (m_Stamina <= 0.0f)
		{
			m_Stamina = 0.0f;
			m_bStaminaExhausted = true;
			SetMode(Mode::NONE);
			return false;
		}

		// Leaps carry momentum, otherwise movement responds immediately
		velocity = glm::length(velocity) > m_Settings.climbSpeed * 1.5f && glm::dot(velocity, wallUp) > 0.0f ?
			glm::mix(velocity, desired, ExpDecay(3.0f, dt)) : desired;
		return true;
	}

	void Traversal::FixedUpdateSki(glm::vec3& velocity)
	{
		const sec dt = g_FixedDeltaTime;
		const real halfHeight = m_Player->m_Height * 0.5f;
		const glm::vec3 pos = m_Player->GetTransform()->GetWorldPosition();

		glm::vec3 groundPos, groundNormal;
		const btVector3 from = ToBtVec3(pos);
		const btVector3 to = ToBtVec3(pos - VEC3_UP * (halfHeight + 0.8f));
		IgnoreObjectRayCallback callback(from, to, m_Player->GetRigidBody()->GetRigidBodyInternal());
		g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld()->rayTest(from, to, callback);
		if (!callback.hasHit())
		{
			// Airborne (off a jump): no control until landing
			return;
		}
		groundPos = ToVec3(callback.m_hitPointWorld);
		groundNormal = ToVec3(callback.m_hitNormalWorld);

		const real normalSpeed = glm::dot(velocity, groundNormal);
		glm::vec3 tangent = velocity - groundNormal * normalSpeed;
		real speed = glm::length(tangent);

		// Snow is slippery, rock & grass grip (sliding over them quickly comes to a stop)
		const real slipperiness = m_GroundSlipperiness;
		const real friction = glm::mix(0.9f, 0.03f, slipperiness);
		real deceleration = friction * 9.81f * glm::max(groundNormal.y, 0.0f) + 0.0015f * speed * speed;

		const real forward = GetMoveForward();
		if (forward < 0.0f)
		{
			// Snowplough
			deceleration += -forward * 7.0f;
		}
		speed = glm::max(speed - deceleration * dt, 0.0f);
		if (forward > 0.0f && speed < 9.0f)
		{
			// Skate/pole along on flat ground
			speed += forward * m_Settings.skiPush * dt;
		}

		// Carve towards the look direction
		const glm::vec3 look = GetLookDirection();
		const glm::vec3 lookOnGround = SafeNormalize(look - groundNormal * glm::dot(look, groundNormal), GetHeadingXZ());
		glm::vec3 direction = speed > 0.5f ? SafeNormalize(tangent, lookOnGround) : lookOnGround;
		const real turnRate = m_Settings.skiTurnRate * glm::mix(0.5f, 1.0f, slipperiness);
		direction = RotateTowards(direction, lookOnGround, turnRate * dt);

		speed = glm::min(speed, m_Settings.skiMaxSpeed);
		velocity = direction * speed + groundNormal * normalSpeed;
	}

	void Traversal::FixedUpdateHook(glm::vec3& velocity)
	{
		const sec dt = g_FixedDeltaTime;
		const glm::vec3 pos = m_Player->GetTransform()->GetWorldPosition();

		if (m_Hook.bReelingIn)
		{
			m_Hook.ropeLength = glm::max(m_Hook.ropeLength - m_Settings.reelSpeed * dt, 2.5f);
		}
		else if (m_Hook.bReelingOut)
		{
			m_Hook.ropeLength = glm::min(m_Hook.ropeLength + m_Settings.reelSpeed * dt, m_Settings.hookRange);
		}

		const glm::vec3 offset = pos - m_Hook.pos;
		const real dist = glm::length(offset);
		if (dist < 1e-3f)
		{
			return;
		}
		const glm::vec3 ropeDir = offset / dist;

		if (m_bGrounded && !m_Hook.bReelingIn)
		{
			// Walking around on the ground pays out rope rather than being held back
			m_Hook.ropeLength = glm::max(m_Hook.ropeLength, dist);
		}

		if (dist > m_Hook.ropeLength)
		{
			// Inelastic rope: remove outwards velocity, & pull back to the rope's length
			const real outwardSpeed = glm::dot(velocity, ropeDir);
			if (outwardSpeed > 0.0f)
			{
				velocity -= ropeDir * outwardSpeed;
			}
			velocity -= ropeDir * glm::min((dist - m_Hook.ropeLength) * 6.0f, 25.0f);
		}

		if (!m_bGrounded)
		{
			// Pump the swing
			const glm::vec3 heading = GetHeadingXZ();
			const glm::vec3 right = glm::cross(heading, VEC3_UP);
			glm::vec3 input = heading * GetMoveForward() + right * GetMoveRight();
			input -= ropeDir * glm::dot(input, ropeDir);
			velocity += input * m_Settings.swingControl * dt;
		}
	}

	void Traversal::UpdateSounds()
	{
		// Variometer: beeps faster & higher while climbing, like a real paraglider's
		const real verticalSpeed = (real)m_Player->GetRigidBody()->GetRigidBodyInternal()->getLinearVelocity().getY();
		if (m_Mode == Mode::GLIDING && verticalSpeed > 0.3f && m_VarioBeepID != InvalidAudioSourceID)
		{
			m_VarioTimer -= g_DeltaTime;
			if (m_VarioTimer <= 0.0f)
			{
				m_VarioTimer = glm::max(0.5f - verticalSpeed * 0.07f, 0.12f);
				AudioManager::SetSourcePitch(m_VarioBeepID, glm::clamp(1.0f + verticalSpeed * 0.09f, 1.0f, 1.8f));
				AudioManager::PlaySourceWithGain(m_VarioBeepID, 0.25f);
			}
		}
		else
		{
			m_VarioTimer = 0.0f;
		}
	}

	void Traversal::UpdateVisuals()
	{
		if (m_RopeMesh == nullptr || m_CanopyMesh == nullptr || m_CanopyTrimMesh == nullptr || m_GliderFrameMesh == nullptr || m_MotesMesh == nullptr ||
			m_WingsuitMesh == nullptr || m_WingsuitTrimMesh == nullptr || m_AirStreakMesh == nullptr)
		{
			return;
		}

		PROFILE_AUTO("Traversal UpdateVisuals");

		// Rope (while the hook is out)
		m_RopeMesh->Begin();
		if (m_Hook.state != HookState::NONE)
		{
			const glm::vec3 handPos = GetHandPos();
			const real dist = glm::distance(handPos, m_Hook.pos);
			// Slack rope sags
			const real slack = m_Hook.state == HookState::ATTACHED ? glm::max(m_Hook.ropeLength - dist, 0.0f) : 0.0f;
			const u32 pointCount = 16;
			glm::vec3 points[pointCount];
			for (u32 i = 0; i < pointCount; ++i)
			{
				const real t = (real)i / (real)(pointCount - 1);
				points[i] = glm::mix(handPos, m_Hook.pos, t) - VEC3_UP * (4.0f * t * (1.0f - t) * glm::min(slack * 0.5f, 8.0f));
			}
			m_RopeMesh->AddTube(points, pointCount, 0.035f, 6);

			// The hook itself
			const glm::vec3 hookDir = SafeNormalize(m_Hook.pos - handPos, VEC3_FORWARD);
			const glm::vec3 hookPoints[] = { m_Hook.pos - hookDir * 0.35f, m_Hook.pos };
			m_RopeMesh->AddTube(hookPoints, 2, 0.12f, 6);
		}

		// A tossed rope, while flying (trailing back to the hand) & falling (unrolling from its anchor)
		if (m_RopeToss.state == RopeTossState::FLYING)
		{
			const glm::vec3 handPos = GetHandPos();
			const u32 pointCount = 12;
			glm::vec3 points[pointCount];
			for (u32 i = 0; i < pointCount; ++i)
			{
				const real t = (real)i / (real)(pointCount - 1);
				points[i] = glm::mix(handPos, m_RopeToss.pos, t) - VEC3_UP * (2.0f * t * (1.0f - t));
			}
			m_RopeMesh->AddTube(points, pointCount, 0.03f, 5);
			const glm::vec3 knot[] = { m_RopeToss.pos - VEC3_UP * 0.1f, m_RopeToss.pos + VEC3_UP * 0.1f };
			m_RopeMesh->AddTube(knot, 2, 0.1f, 6);
		}
		else if (m_RopeToss.state == RopeTossState::FALLING)
		{
			const real upperLength = glm::distance(m_RopeToss.anchor, m_RopeToss.mid);
			const bool bPastMid = glm::distance(m_RopeToss.anchor, m_RopeToss.pos) > upperLength - 1e-3f && upperLength > 1e-3f;
			const glm::vec3 points[] = { m_RopeToss.anchor, bPastMid ? m_RopeToss.mid : m_RopeToss.pos, m_RopeToss.pos };
			m_RopeMesh->AddTube(points, bPastMid ? 3 : 2, 0.03f, 5);
		}

		UpdateBodyTilt();
		BuildGlider();
		BuildWingsuit();

		m_RopeMesh->Upload();
		m_CanopyMesh->Upload();
		m_CanopyTrimMesh->Upload();
		m_GliderFrameMesh->Upload();
		m_WingsuitMesh->Upload();
		m_WingsuitTrimMesh->Upload();

		m_AirStreakMesh->Begin();
		DrawAirStreaks();
		m_AirStreakMesh->Upload();

		m_MotesMesh->Begin();
		DrawThermalMotes();
		m_MotesMesh->Upload();
	}

	void Traversal::UpdateBodyTiltTargets(const glm::vec3& velocity)
	{
		const sec dt = g_FixedDeltaTime;
		real targetBank = 0.0f;
		real targetPitch = 0.0f;
		if ((m_Mode == Mode::GLIDING || m_Mode == Mode::WINGSUIT) && dt > 0.0f)
		{
			const glm::vec2 h0(m_TiltLastVelocity.x, m_TiltLastVelocity.z);
			const glm::vec2 h1(velocity.x, velocity.z);
			const real horizontalSpeed = glm::length(h1);
			if (glm::length(h0) > 1.0f && horizontalSpeed > 1.0f)
			{
				// Lean like a coordinated turn: so the lift balances gravity & the centripetal acceleration
				const real turnRate = std::atan2(h0.x * h1.y - h0.y * h1.x, glm::dot(h0, h1)) / dt;
				const glm::vec3 centripetalDir(-h1.y / horizontalSpeed, 0.0f, h1.x / horizontalSpeed);
				const glm::vec3 right = glm::cross(GetHeadingXZ(), VEC3_UP);
				const real lateralAcceleration = turnRate * horizontalSpeed * glm::dot(centripetalDir, right);
				const real maxBank = glm::radians(m_Mode == Mode::GLIDING ? 40.0f : 60.0f);
				targetBank = glm::clamp(std::atan2(lateralAcceleration, 9.81f), -maxBank, maxBank);
			}

			// Pitch with the flight path: gently under the glider, while the wingsuit's body lies along it
			const real flightPath = std::atan2(velocity.y, glm::max(horizontalSpeed, 0.1f));
			if (m_Mode == Mode::GLIDING)
			{
				targetPitch = glm::clamp(flightPath * 0.8f, glm::radians(-25.0f), glm::radians(25.0f));
			}
			else
			{
				// The body lies along the flight path, nose up towards where the player's looking (the angle of attack
				// which produces the lift, belly into the oncoming air)
				const real lookPitch = std::asin(glm::clamp(GetLookDirection().y, -1.0f, 1.0f));
				const real angleOfAttack = glm::clamp(lookPitch - flightPath, glm::radians(-10.0f), glm::radians(35.0f)) * 0.8f;
				targetPitch = glm::clamp(flightPath + angleOfAttack, glm::radians(-80.0f), glm::radians(40.0f)) - PI_DIV_TWO;
			}
		}
		m_TiltLastVelocity = velocity;
		m_BankSpring.SetTargetPos(targetBank);
		m_PitchSpring.SetTargetPos(targetPitch);
	}

	void Traversal::UpdateBodyTilt()
	{
		const sec dt = g_DeltaTime;
		if (dt > 0.0f)
		{
			m_BankSpring.Tick(dt);
			m_PitchSpring.Tick(dt);
		}

		Transform* playerTransform = m_Player->GetTransform();
		const glm::vec3 pos = playerTransform->GetWorldPosition();
		const glm::vec3 heading = GetHeadingXZ();
		const real bank = m_BankSpring.pos;
		const real pitch = m_PitchSpring.pos;

		// Bank around the heading, then pitch around the banked right axis
		const glm::vec3 right0 = glm::cross(heading, VEC3_UP);
		const glm::vec3 up1 = VEC3_UP * std::cos(bank) + right0 * std::sin(bank);
		const glm::vec3 right1 = glm::cross(heading, up1);
		const glm::vec3 forward2 = heading * std::cos(pitch) + up1 * std::sin(pitch);
		const glm::vec3 up2 = up1 * std::cos(pitch) - heading * std::sin(pitch);
		const glm::mat3 from(right0, VEC3_UP, heading);
		const glm::mat3 to(right1, up2, forward2);
		m_TiltRotation = glm::normalize(glm::quat_cast(to * glm::transpose(from)));

		// Hanging from the glider's harness, the body swings around a point above its center. Without the glider (the
		// wingsuit) it turns around its center.
		m_TiltPivot = pos + VEC3_UP * (0.65f * glm::clamp(m_CanopyOpenness * 1.5f, 0.0f, 1.0f));

		if (m_BodyMeshObject != nullptr)
		{
			// In first person the camera is inside the body, which stays upright so it doesn't swing into view
			BaseCamera* camera = g_CameraManager->CurrentCamera();
			const bool bFirstPerson = camera != nullptr && camera->bIsFirstPerson && camera->bPossessPlayer;
			const bool bTilted = !bFirstPerson && (glm::abs(bank) > 1e-4f || glm::abs(pitch) > 1e-4f);
			Transform* bodyTransform = m_BodyMeshObject->GetTransform();
			if (bTilted)
			{
				const glm::quat playerRot = playerTransform->GetWorldRotation();
				const glm::quat invPlayerRot = glm::inverse(playerRot);
				const glm::vec3 restPos = pos + playerRot * m_BodyMeshBasePos;
				const glm::vec3 tiltedPos = m_TiltPivot + m_TiltRotation * (restPos - m_TiltPivot);
				bodyTransform->SetLocalPosition(invPlayerRot * (tiltedPos - pos));
				bodyTransform->SetLocalRotation(invPlayerRot * m_TiltRotation * playerRot * m_BodyMeshBaseRot);
			}
			else if (bodyTransform->GetLocalPosition() != m_BodyMeshBasePos || bodyTransform->GetLocalRotation() != m_BodyMeshBaseRot)
			{
				bodyTransform->SetLocalPosition(m_BodyMeshBasePos);
				bodyTransform->SetLocalRotation(m_BodyMeshBaseRot);
			}
		}
	}

	namespace
	{
		// Adds a triangle facing the given direction (front faces wind clockwise around their normal here)
		void AddTriangleFacing(std::vector<u32>& indices, const std::vector<glm::vec3>& positions, u32 a, u32 b, u32 c, const glm::vec3& facing)
		{
			const glm::vec3 n = glm::cross(positions[b] - positions[a], positions[c] - positions[a]);
			if (glm::dot(n, facing) > 0.0f)
			{
				std::swap(b, c);
			}
			indices.push_back(a);
			indices.push_back(b);
			indices.push_back(c);
		}
	} // anonymous namespace

	void Traversal::BuildGlider()
	{
		// Inflates quickly when deployed, folds away faster when stowed
		const sec dt = g_DeltaTime;
		const real targetOpenness = m_Mode == Mode::GLIDING ? 1.0f : 0.0f;
		m_CanopyOpenness = glm::mix(m_CanopyOpenness, targetOpenness, ExpDecay(targetOpenness > m_CanopyOpenness ? 6.0f : 10.0f, dt));
		if (targetOpenness == 0.0f && m_CanopyOpenness < 0.02f)
		{
			m_CanopyOpenness = 0.0f;
		}

		m_CanopyMesh->Begin();
		m_CanopyTrimMesh->Begin();
		m_GliderFrameMesh->Begin();
		if (m_CanopyOpenness <= 0.0f)
		{
			return;
		}

		const real open = m_CanopyOpenness;
		// The glider's frame: centered on the harness (where the risers clip in), leaning with the body
		const glm::vec3 heading = GetHeadingXZ();
		const glm::vec3 F = m_TiltRotation * heading;
		const glm::vec3 U = m_TiltRotation * VEC3_UP;
		const glm::vec3 R = m_TiltRotation * glm::cross(heading, VEC3_UP);
		const glm::vec3 P = m_TiltPivot;
		auto ToWorld = [&](real x, real y, real z) { return P + R * x + U * y + F * z; };

		// Span-wise shape: an arc that's flat in the middle & curves down towards the tips (anhedral)
		const u32 cellCount = 16; // Even, so there's a rib at the center
		const u32 ribCount = cellCount + 1;
		const u32 chordCount = 8;
		const real halfSpan = 5.4f * open; // Along the curve
		const real wingHeight = 6.6f * glm::mix(0.35f, 1.0f, open); // Above the harness, at the center
		const real maxArcAngle = glm::radians(78.0f);
		const real rootChord = 2.7f * glm::mix(0.5f, 1.0f, open);

		real ribX[ribCount], ribY[ribCount], ribAngle[ribCount], ribChord[ribCount], ribLE[ribCount], ribThickness[ribCount];
		{
			const u32 half = cellCount / 2;
			const u32 subSteps = 8;
			real x = 0.0f;
			real y = wingHeight;
			for (u32 i = 0; i <= half; ++i)
			{
				const real s = (real)i / (real)half;
				const real angle = maxArcAngle * glm::pow(s, 1.7f);
				const real chord = rootChord * std::sqrt(1.0f - 0.78f * s * s);
				for (i32 sign : { 1, -1 })
				{
					const u32 rib = (u32)((i32)half + sign * (i32)i);
					ribX[rib] = x * (real)sign;
					ribY[rib] = y;
					ribAngle[rib] = angle * (real)sign;
					ribChord[rib] = chord;
					// The pilot hangs below ~30% of the chord, & the tips sweep back a little
					ribLE[rib] = 0.3f * rootChord - (rootChord - chord) * 0.35f;
					ribThickness[rib] = 0.15f * (1.0f - 0.45f * s * s) * chord;
				}
				if (i < half)
				{
					for (u32 k = 0; k < subSteps; ++k)
					{
						const real sMid = ((real)i + ((real)k + 0.5f) / (real)subSteps) / (real)half;
						const real a = maxArcAngle * glm::pow(sMid, 1.7f);
						const real step = halfSpan / (real)(half * subSteps);
						x += std::cos(a) * step;
						y -= std::sin(a) * step;
					}
				}
			}
		}

		// Airfoil (NACA style thickness, with camber & a flatter underside), dense around the leading edge
		glm::vec3 grid[2][ribCount][chordCount + 1];
		glm::vec3 ribOutward[ribCount];
		for (u32 rib = 0; rib < ribCount; ++rib)
		{
			const glm::vec3 outward = SafeNormalize(R * std::sin(ribAngle[rib]) + U * std::cos(ribAngle[rib]), U);
			ribOutward[rib] = outward;
			const glm::vec3 base = ToWorld(ribX[rib], ribY[rib], 0.0f);
			for (u32 j = 0; j <= chordCount; ++j)
			{
				const real c = 0.5f - 0.5f * std::cos(PI * (real)j / (real)chordCount);
				const real thickness = 5.0f * ribThickness[rib] * (0.2969f * std::sqrt(c) - 0.126f * c - 0.3516f * c * c + 0.2843f * c * c * c - 0.1036f * c * c * c * c);
				const real camber = 0.05f * ribChord[rib] * 4.0f * c * (1.0f - c);
				const glm::vec3 chordPos = base + F * (ribLE[rib] - c * ribChord[rib]);
				grid[0][rib][j] = chordPos + outward * (camber + thickness);
				grid[1][rib][j] = chordPos + outward * (camber - thickness * 0.55f);
			}
		}

		// Cells, alternating between the two fabrics (cream tips & center)
		for (u32 cell = 0; cell < cellCount; ++cell)
		{
			const bool bTrim = cell <= 1 || cell >= cellCount - 2 || cell == cellCount / 2 - 1 || cell == cellCount / 2;
			DynamicMesh* mesh = bTrim ? m_CanopyTrimMesh : m_CanopyMesh;
			for (u32 surface = 0; surface < 2; ++surface)
			{
				const real outwardSign = surface == 0 ? 1.0f : -1.0f;
				const u32 base = (u32)mesh->vertexBufferCreateInfo.positions_3D.size();
				for (u32 side = 0; side < 2; ++side)
				{
					const u32 rib = cell + side;
					for (u32 j = 0; j <= chordCount; ++j)
					{
						// Normal from the surface's tangents (span-wise from the neighbouring ribs, chord-wise along the rib)
						const glm::vec3 spanTangent = grid[surface][glm::min(rib + 1, cellCount)][j] - grid[surface][rib > 0 ? rib - 1 : 0][j];
						const glm::vec3 chordTangent = grid[surface][rib][glm::min(j + 1, chordCount)] - grid[surface][rib][j > 0 ? j - 1 : 0];
						glm::vec3 normal = SafeNormalize(glm::cross(spanTangent, chordTangent), ribOutward[rib]);
						if (glm::dot(normal, ribOutward[rib] * outwardSign) < 0.0f)
						{
							normal = -normal;
						}
						if (j == 0)
						{
							// The leading edge faces forwards
							normal = SafeNormalize(F + ribOutward[rib] * 0.25f * outwardSign, F);
						}
						mesh->AddVertex(grid[surface][rib][j], normal, glm::vec2((real)rib / (real)cellCount, (real)j / (real)chordCount));
					}
				}
				const std::vector<glm::vec3>& positions = mesh->vertexBufferCreateInfo.positions_3D;
				for (u32 j = 0; j < chordCount; ++j)
				{
					const u32 a = base + j;
					const u32 b = base + chordCount + 1 + j;
					const glm::vec3 facing = (ribOutward[cell] + ribOutward[cell + 1]) * outwardSign;
					AddTriangleFacing(mesh->indices, positions, a, a + 1, b, facing);
					AddTriangleFacing(mesh->indices, positions, b, a + 1, b + 1, facing);
				}
			}
		}

		// Close off the wingtips
		for (u32 tip = 0; tip < 2; ++tip)
		{
			const u32 rib = tip == 0 ? 0 : cellCount;
			const glm::vec3 facing = tip == 0 ? -R : R;
			const glm::vec3 normal = SafeNormalize(glm::cross(ribOutward[rib], F) * (tip == 0 ? 1.0f : -1.0f), facing);
			const glm::vec3 outwardNormal = glm::dot(normal, facing) >= 0.0f ? normal : -normal;
			const u32 base = (u32)m_CanopyTrimMesh->vertexBufferCreateInfo.positions_3D.size();
			for (u32 j = 0; j <= chordCount; ++j)
			{
				m_CanopyTrimMesh->AddVertex(grid[0][rib][j], outwardNormal, glm::vec2(0.0f, (real)j / (real)chordCount));
				m_CanopyTrimMesh->AddVertex(grid[1][rib][j], outwardNormal, glm::vec2(1.0f, (real)j / (real)chordCount));
			}
			for (u32 j = 0; j < chordCount; ++j)
			{
				const u32 a = base + j * 2;
				AddTriangleFacing(m_CanopyTrimMesh->indices, m_CanopyTrimMesh->vertexBufferCreateInfo.positions_3D, a, a + 1, a + 2, outwardNormal);
				AddTriangleFacing(m_CanopyTrimMesh->indices, m_CanopyTrimMesh->vertexBufferCreateInfo.positions_3D, a + 1, a + 3, a + 2, outwardNormal);
			}
		}

		// Lines: each rib's A, B & C lines join (cascade) & run down to the riser on their side. Brake lines run from
		// the outer trailing edge to the hands.
		const real lineRadius = 0.014f;
		const real riserSpread = 0.24f;
		const real riserLength = 0.55f;
		for (i32 sideSign : { -1, 1 })
		{
			const glm::vec3 riserTop = ToWorld((real)sideSign * riserSpread, riserLength, 0.05f);
			const glm::vec3 brakeHandle = ToWorld((real)sideSign * (riserSpread + 0.08f), 0.15f, -0.12f);
			for (u32 rib = 0; rib < ribCount; ++rib)
			{
				const i32 offset = (i32)rib - (i32)(cellCount / 2);
				if (offset == 0 || (offset > 0) != (sideSign > 0) || (glm::abs(offset) % 2) != 1)
				{
					continue;
				}
				const glm::vec3 attachA = grid[1][rib][2];
				const glm::vec3 attachB = grid[1][rib][4];
				const glm::vec3 attachC = grid[1][rib][6];
				const glm::vec3 cascade = glm::mix(attachB, riserTop, 0.3f);
				for (const glm::vec3& attach : { attachA, attachB, attachC })
				{
					const glm::vec3 upper[] = { attach, cascade };
					m_GliderFrameMesh->AddTube(upper, 2, lineRadius * 0.8f, 3);
				}
				const glm::vec3 main[] = { cascade, riserTop };
				m_GliderFrameMesh->AddTube(main, 2, lineRadius, 3);
			}

			// Brakes
			const u32 brakeCascadeRib = (u32)((i32)(cellCount / 2) + sideSign * 6);
			const glm::vec3 brakeCascade = ToWorld(ribX[brakeCascadeRib] * 0.6f, wingHeight * 0.45f, ribLE[cellCount / 2] - rootChord * 0.9f);
			for (u32 i = 5; i <= cellCount / 2; ++i)
			{
				const u32 rib = (u32)((i32)(cellCount / 2) + sideSign * (i32)i);
				const glm::vec3 brake[] = { grid[1][rib][chordCount], brakeCascade };
				m_GliderFrameMesh->AddTube(brake, 2, lineRadius * 0.8f, 3);
			}
			const glm::vec3 brakeMain[] = { brakeCascade, brakeHandle };
			m_GliderFrameMesh->AddTube(brakeMain, 2, lineRadius, 3);

			// Risers (webbing) down to the carabiners. In first person they'd be right beside the camera.
			BaseCamera* camera = g_CameraManager->CurrentCamera();
			const bool bFirstPerson = camera != nullptr && camera->bIsFirstPerson && camera->bPossessPlayer;
			if (!bFirstPerson)
			{
				const glm::vec3 carabiner = ToWorld((real)sideSign * riserSpread, 0.0f, 0.0f);
				const glm::vec3 riser[] = { riserTop, carabiner };
				m_GliderFrameMesh->AddTube(riser, 2, 0.035f, 4);
				const glm::vec3 handle[] = { brakeHandle - U * 0.09f, brakeHandle + U * 0.02f };
				m_GliderFrameMesh->AddTube(handle, 2, 0.03f, 4);
				// Harness straps from the carabiners down around the body to the seat
				const glm::vec3 seatSide = ToWorld((real)sideSign * 0.36f, -1.05f, -0.12f);
				const glm::vec3 strap[] = { carabiner, ToWorld((real)sideSign * 0.42f, -0.45f, -0.05f), seatSide };
				m_GliderFrameMesh->AddTube(strap, 3, 0.03f, 4);
			}
		}

		// Seat board & spreader bar between the carabiners (third person only, like the risers)
		BaseCamera* camera = g_CameraManager->CurrentCamera();
		if (!(camera != nullptr && camera->bIsFirstPerson && camera->bPossessPlayer))
		{
			const glm::vec3 spreader[] = { ToWorld(-riserSpread, 0.0f, 0.0f), ToWorld(riserSpread, 0.0f, 0.0f) };
			m_GliderFrameMesh->AddTube(spreader, 2, 0.025f, 4);
			const glm::vec3 seat[] = { ToWorld(-0.36f, -1.05f, -0.12f), ToWorld(0.36f, -1.05f, -0.12f) };
			m_GliderFrameMesh->AddTube(seat, 2, 0.07f, 4);
		}
	}

	void Traversal::BuildWingsuit()
	{
		// Spreads quickly when the arms go out, folds away faster
		const sec dt = g_DeltaTime;
		const real targetOpenness = m_Mode == Mode::WINGSUIT ? 1.0f : 0.0f;
		m_WingsuitOpenness = glm::mix(m_WingsuitOpenness, targetOpenness, ExpDecay(targetOpenness > m_WingsuitOpenness ? 7.0f : 12.0f, dt));
		if (targetOpenness == 0.0f && m_WingsuitOpenness < 0.02f)
		{
			m_WingsuitOpenness = 0.0f;
		}

		m_WingsuitMesh->Begin();
		m_WingsuitTrimMesh->Begin();
		// In first person the body stays upright around the camera, so the suit would only get in the way
		BaseCamera* camera = g_CameraManager->CurrentCamera();
		const bool bFirstPerson = camera != nullptr && camera->bIsFirstPerson && camera->bPossessPlayer;
		if (m_WingsuitOpenness <= 0.0f || bFirstPerson)
		{
			return;
		}

		const real open = m_WingsuitOpenness;
		// The body's frame (leaning with the dive): U runs from the feet to the head, F out of the belly (facing the
		// oncoming air while diving), R across the shoulders
		const glm::vec3 heading = GetHeadingXZ();
		const glm::vec3 F = m_TiltRotation * heading;
		const glm::vec3 U = m_TiltRotation * VEC3_UP;
		const glm::vec3 R = m_TiltRotation * glm::cross(heading, VEC3_UP);
		const glm::vec3 P = m_TiltPivot;
		auto ToWorld = [&](real x, real y, real z) { return P + R * x + U * y + F * z; };

		// The fabric billows out behind (away from the oncoming air) more the faster the player flies, & the trailing
		// edges flutter
		const real airSpeed = glm::length(m_LastVelocity);
		const real pressure = glm::smoothstep(8.0f, 45.0f, airSpeed);
		const real billow = 0.1f * (0.35f + 0.65f * pressure) * open;
		const real flutter = 0.025f * pressure * open;
		const real time = AnimationTime();

		// Adds a two sided grid surface (points row major, cols across), cells with column >= trimCol (or row >= trimRow)
		// going to the trim mesh
		auto AddSurface = [&](const std::vector<glm::vec3>& points, u32 rows, u32 cols, u32 trimRow, u32 trimCol)
		{
			auto Normal = [&](u32 r, u32 c)
			{
				const glm::vec3 dr = points[glm::min(r + 1, rows - 1) * cols + c] - points[(r > 0 ? r - 1 : 0) * cols + c];
				const glm::vec3 dc = points[r * cols + glm::min(c + 1, cols - 1)] - points[r * cols + (c > 0 ? c - 1 : 0)];
				glm::vec3 n = SafeNormalize(glm::cross(dr, dc), F);
				return glm::dot(n, F) < 0.0f ? -n : n;
			};
			for (u32 r = 0; r + 1 < rows; ++r)
			{
				for (u32 c = 0; c + 1 < cols; ++c)
				{
					DynamicMesh* mesh = (r >= trimRow || c >= trimCol) ? m_WingsuitTrimMesh : m_WingsuitMesh;
					const u32 corners[4][2] = { { r, c }, { r, c + 1 }, { r + 1, c + 1 }, { r + 1, c } };
					for (real side : { 1.0f, -1.0f })
					{
						u32 ids[4];
						for (u32 k = 0; k < 4; ++k)
						{
							const u32 cr = corners[k][0];
							const u32 cc = corners[k][1];
							ids[k] = mesh->AddVertex(points[cr * cols + cc], Normal(cr, cc) * side, glm::vec2((real)cc / (real)(cols - 1), (real)cr / (real)(rows - 1)));
						}
						const std::vector<glm::vec3>& positions = mesh->vertexBufferCreateInfo.positions_3D;
						AddTriangleFacing(mesh->indices, positions, ids[0], ids[1], ids[2], F * side);
						AddTriangleFacing(mesh->indices, positions, ids[0], ids[2], ids[3], F * side);
					}
				}
			}
		};

		// Arm wings: from the arm (the leading edge, shoulder to wrist) back to the hip, with a concave trailing edge
		const real wristX = glm::mix(0.42f, 1.02f, open);
		const real wristY = glm::mix(0.2f, 0.52f, open);
		const u32 spanCount = 7;
		const u32 chordCount = 5;
		std::vector<glm::vec3> points(spanCount * chordCount);
		for (real sideSign : { -1.0f, 1.0f })
		{
			for (u32 s = 0; s < spanCount; ++s)
			{
				const real t = (real)s / (real)(spanCount - 1);
				const glm::vec2 lead(glm::mix(0.26f, wristX, t), glm::mix(0.58f, wristY, t));
				const real chord = glm::mix(1.0f, 0.13f, std::pow(t, 0.75f)) * glm::mix(0.6f, 1.0f, open);
				const glm::vec2 trail(glm::mix(0.3f, wristX * 0.93f, t), lead.y - chord);
				for (u32 c = 0; c < chordCount; ++c)
				{
					const real v = (real)c / (real)(chordCount - 1);
					const glm::vec2 p = glm::mix(lead, trail, v);
					const real z = -billow * std::sin(PI * v) * std::sin(PI * glm::min(t * 1.15f, 1.0f)) -
						flutter * v * v * std::sin(time * 31.0f + t * 9.0f + sideSign);
					// Rows run root to tip, columns leading to trailing edge
					points[s * chordCount + c] = ToWorld(sideSign * p.x, p.y, z);
				}
			}
			// Contrasting wing tips
			AddSurface(points, spanCount, chordCount, spanCount - 3, chordCount);

			// Sleeves along the leading edge, & gloves
			const glm::vec3 arm[] = { ToWorld(sideSign * 0.2f, 0.6f, 0.0f), ToWorld(sideSign * glm::mix(0.26f, wristX, 0.5f), glm::mix(0.6f, wristY, 0.5f), -0.01f), ToWorld(sideSign * wristX, wristY, 0.0f) };
			m_WingsuitMesh->AddTube(arm, 3, 0.065f, 6);
			const glm::vec3 hand[] = { ToWorld(sideSign * wristX, wristY, 0.0f), ToWorld(sideSign * (wristX + 0.14f * open + 0.04f), wristY - 0.02f, 0.01f) };
			m_GliderFrameMesh->AddTube(hand, 2, 0.05f, 5);
		}

		// Tail wing between the legs, with a trailing edge curving in between the ankles
		const real ankleX = glm::mix(0.13f, 0.33f, open);
		const real ankleY = -1.08f;
		const u32 acrossCount = 7;
		const u32 alongCount = 5;
		points.resize(alongCount * acrossCount);
		for (u32 a = 0; a < alongCount; ++a)
		{
			const real v = (real)a / (real)(alongCount - 1);
			const real legX = glm::mix(0.17f, ankleX, v);
			for (u32 c = 0; c < acrossCount; ++c)
			{
				const real u = (real)c / (real)(acrossCount - 1) * 2.0f - 1.0f;
				const real y = glm::mix(-0.3f, ankleY + 0.18f * (1.0f - u * u) * open, v);
				const real z = -billow * 0.8f * (1.0f - u * u) * std::sin(PI * v) - flutter * v * v * std::sin(time * 27.0f + u * 5.0f);
				points[a * acrossCount + c] = ToWorld(u * legX, y, z);
			}
		}
		AddSurface(points, alongCount, acrossCount, alongCount - 2, acrossCount);

		// Legs & boots
		for (real sideSign : { -1.0f, 1.0f })
		{
			const glm::vec3 leg[] = { ToWorld(sideSign * 0.17f, -0.35f, 0.0f), ToWorld(sideSign * ankleX, ankleY, 0.0f) };
			m_WingsuitMesh->AddTube(leg, 2, 0.085f, 6);
			const glm::vec3 boot[] = { ToWorld(sideSign * ankleX, ankleY + 0.02f, 0.0f), ToWorld(sideSign * ankleX, ankleY - 0.1f, 0.12f) };
			m_GliderFrameMesh->AddTube(boot, 2, 0.07f, 5);
		}
	}

	void Traversal::DrawAirStreaks()
	{
		const sec dt = g_DeltaTime;
		BaseCamera* camera = g_CameraManager->CurrentCamera();
		m_AirStreakCount = 0;
		if (camera == nullptr || m_AirStreakMesh == nullptr)
		{
			return;
		}

		// Relative to the air (wind is ignored, it's slow compared to the speeds where these show)
		const glm::vec3 velocity = m_LastVelocity;
		const real speed = glm::length(velocity);
		const bool bAirborne = !(m_bGrounded && m_Mode == Mode::NONE) && m_Mode != Mode::CLIMBING;
		const real targetIntensity = bAirborne ? glm::smoothstep(10.0f, 42.0f, speed) : 0.0f;
		m_AirStreakIntensity = glm::mix(m_AirStreakIntensity, targetIntensity, ExpDecay(4.0f, dt));
		if (m_AirStreakIntensity < 0.01f || speed < 1.0f)
		{
			for (AirStreak& streak : m_AirStreaks)
			{
				streak.bAlive = false;
			}
			return;
		}

		PROFILE_AUTO("Traversal DrawAirStreaks");

		const glm::vec3 cameraPos = camera->position;
		const glm::vec3 dir = velocity / speed;
		const glm::vec3 side0 = SafeNormalize(glm::cross(dir, glm::abs(dir.y) < 0.95f ? VEC3_UP : VEC3_RIGHT), VEC3_RIGHT);
		const glm::vec3 side1 = glm::cross(dir, side0);
		// Spawned far enough ahead to fade in before they reach the camera
		const real spawnAhead = glm::clamp(speed * 0.5f, 10.0f, 30.0f);
		const u32 activeCount = (u32)glm::ceil(m_AirStreakIntensity * (real)MAX_AIR_STREAKS);
		// Long enough to read as motion, short enough to look fast (roughly the distance covered in a few frames)
		const real length = glm::clamp(speed * 0.07f, 0.8f, 4.5f);
		const real halfWidth = 0.012f;

		for (u32 i = 0; i < MAX_AIR_STREAKS; ++i)
		{
			AirStreak& streak = m_AirStreaks[i];
			if (streak.bAlive)
			{
				const glm::vec3 rel = streak.pos - cameraPos;
				const real along = glm::dot(rel, dir);
				if (along < -length - 1.0f || glm::dot(rel, rel) > 50.0f * 50.0f)
				{
					streak.bAlive = false;
				}
			}
			if (!streak.bAlive && i < activeCount)
			{
				// A random point on a disc ahead of the camera, around (not through) the view's center
				const real angle = HashToUnit(++m_AirStreakSeed) * TWO_PI;
				const real radius = 1.2f + 5.5f * HashToUnit(++m_AirStreakSeed);
				const real ahead = spawnAhead * (0.6f + 0.4f * HashToUnit(++m_AirStreakSeed));
				streak.pos = cameraPos + dir * ahead + (side0 * std::cos(angle) + side1 * std::sin(angle)) * radius;
				streak.age = 0.0f;
				streak.lengthScale = 0.6f + 0.8f * HashToUnit(++m_AirStreakSeed);
				streak.bAlive = true;
			}
			if (!streak.bAlive)
			{
				continue;
			}
			streak.age += dt;

			const glm::vec3 rel = streak.pos - cameraPos;
			const real distance = glm::length(rel);
			// Fade in once spawned, & out as they pass right by the camera (where they'd be big & blurry)
			const real alpha = 0.35f * m_AirStreakIntensity * glm::smoothstep(0.0f, 0.25f, streak.age) * glm::smoothstep(0.6f, 2.5f, distance) *
				(1.0f - glm::smoothstep(30.0f, 45.0f, distance));
			if (alpha < 0.005f)
			{
				continue;
			}

			// A thin ribbon along the flight direction, facing the camera, fading out at both ends
			const glm::vec3 head = streak.pos;
			const glm::vec3 tail = streak.pos + dir * (length * streak.lengthScale);
			const glm::vec3 mid = (head + tail) * 0.5f;
			const glm::vec3 widthDir = SafeNormalize(glm::cross(dir, mid - cameraPos), side0) * (halfWidth * glm::max(distance * 0.12f, 1.0f));
			const glm::vec3 normal = SafeNormalize(cameraPos - mid, -dir);
			const glm::vec4 clear(1.0f, 1.0f, 1.0f, 0.0f);
			const glm::vec4 colour(0.95f, 0.97f, 1.0f, alpha);
			const u32 base = m_AirStreakMesh->AddVertex(head - widthDir, normal, VEC2_ZERO, clear);
			m_AirStreakMesh->AddVertex(head + widthDir, normal, VEC2_ZERO, clear);
			m_AirStreakMesh->AddVertex(mid - widthDir, normal, VEC2_ZERO, colour);
			m_AirStreakMesh->AddVertex(mid + widthDir, normal, VEC2_ZERO, colour);
			m_AirStreakMesh->AddVertex(tail - widthDir, normal, VEC2_ZERO, clear);
			m_AirStreakMesh->AddVertex(tail + widthDir, normal, VEC2_ZERO, clear);
			// Both windings, so they're visible regardless of the facing convention
			for (u32 q = 0; q < 2; ++q)
			{
				const u32 a = base + q * 2;
				m_AirStreakMesh->AddTriangle(a, a + 1, a + 3);
				m_AirStreakMesh->AddTriangle(a, a + 3, a + 2);
				m_AirStreakMesh->AddTriangle(a, a + 3, a + 1);
				m_AirStreakMesh->AddTriangle(a, a + 2, a + 3);
			}
			++m_AirStreakCount;
		}
	}

	void Traversal::DrawThermalMotes()
	{
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		DirectionalLight* sun = g_Renderer->GetDirectionalLight();
		const real sunFactor = glm::smoothstep(0.05f, 0.45f, sun != nullptr ? sun->data.dir.y : 0.7f);
		// Only shown while in the air (they'd just be clutter while walking around)
		if (terrainGenerator == nullptr || sunFactor <= 0.0f || (m_bGrounded && m_Mode == Mode::NONE) || m_TimeSinceGrounded < THERMAL_MOTES_DELAY)
		{
			return;
		}

		PROFILE_AUTO("Traversal DrawThermalMotes");

		BaseCamera* camera = g_CameraManager->CurrentCamera();
		const glm::vec3 cameraPos = camera->position;
		const glm::vec2 windDir(std::cos(m_WindAngle), std::sin(m_WindAngle));
		m_NearbyThermals.clear();
		GetThermalsNear(glm::vec2(cameraPos.x, cameraPos.z), 600.0f, m_NearbyThermals);

		const real time = AnimationTime();
		const u32 motesPerThermal = 24;
		const real columnHeight = 600.0f;

		for (u32 t = 0; t < (u32)m_NearbyThermals.size(); ++t)
		{
			const Thermal& thermal = m_NearbyThermals[t];
			const terrain::ColumnSample column = terrainGenerator->SampleColumn(thermal.center.x, thermal.center.y);
			if (column.lakeID != 0 && column.lakeT < 1.0f && column.height < column.waterLevel)
			{
				// Water doesn't produce thermals
				continue;
			}
			const real groundHeight = column.height;
			const real visibility = glm::smoothstep(0.35f, 1.0f, glm::smoothstep(-0.5f, 0.2f, column.temperature));

			for (u32 i = 0; i < motesPerThermal; ++i)
			{
				// Each mote spirals upwards at its own rate, looping back to the bottom
				const real seed = (real)(t * 131 + i);
				const real phase = glm::fract(time * (0.004f + 0.002f * glm::fract(seed * 0.618f)) + (real)i / (real)motesPerThermal);
				const real height = 15.0f + phase * columnHeight;
				const real angle = seed * 2.4f + time * 0.25f * (1.0f + 0.5f * glm::fract(seed * 0.37f));
				const real radius = thermal.radius * 0.7f * std::sqrt(glm::fract(seed * 0.7548f));
				const glm::vec2 center = thermal.center + windDir * height * THERMAL_DRIFT;
				const glm::vec3 pos(center.x + std::cos(angle) * radius, groundHeight + height, center.y + std::sin(angle) * radius);

				const glm::vec3 toMote = pos - cameraPos;
				const real distance = glm::length(toMote);
				if (distance > 900.0f || distance < 2.0f || glm::dot(toMote, camera->forward) < 0.0f)
				{
					continue;
				}

				// Fade in & out at the ends of the column, & with distance
				const real alpha = 0.55f * visibility * sunFactor * glm::smoothstep(0.0f, 0.1f, phase) * (1.0f - glm::smoothstep(0.75f, 1.0f, phase)) * (1.0f - glm::smoothstep(400.0f, 900.0f, distance));
				if (alpha < 0.01f)
				{
					continue;
				}

				// A small camera facing quad, growing with distance so it stays a few pixels across
				const real halfSize = glm::max(0.2f, distance * 0.004f) * glm::sqrt(alpha);
				const glm::vec3 right = camera->right * halfSize;
				const glm::vec3 up = camera->up * halfSize;
				const glm::vec3 normal = -camera->forward;
				const u32 base = m_MotesMesh->AddVertex(pos - right - up, normal, glm::vec2(0.0f, 0.0f));
				m_MotesMesh->AddVertex(pos + right - up, normal, glm::vec2(1.0f, 0.0f));
				m_MotesMesh->AddVertex(pos + right + up, normal, glm::vec2(1.0f, 1.0f));
				m_MotesMesh->AddVertex(pos - right + up, normal, glm::vec2(0.0f, 1.0f));
				// Both windings, so they're visible regardless of the quad's facing convention
				m_MotesMesh->AddTriangle(base, base + 1, base + 2);
				m_MotesMesh->AddTriangle(base, base + 2, base + 3);
				m_MotesMesh->AddTriangle(base, base + 2, base + 1);
				m_MotesMesh->AddTriangle(base, base + 3, base + 2);
			}
		}
	}

	void Traversal::DrawHUD() const
	{
		if (!m_Player->m_bPossessed)
		{
			return;
		}

		std::vector<std::string> lines;
		const glm::vec3 velocity = ToVec3(m_Player->GetRigidBody()->GetRigidBodyInternal()->getLinearVelocity());
		switch (m_Mode)
		{
		case Mode::GLIDING:
			lines.push_back(LOC(Traversal_GliderTitle));
			lines.push_back(LOCF(Traversal_GliderStats, m_HeightAboveGround, glm::length(velocity) * 3.6f, velocity.y));
			lines.push_back(m_Lift > 0.8f ? LOC(Traversal_RisingAir) : LOC(Traversal_FindRisingAir));
			lines.push_back(LOC(Traversal_GliderControls));
			break;
		case Mode::WINGSUIT:
			lines.push_back(LOC(Traversal_WingsuitTitle));
			lines.push_back(LOCF(Traversal_FlightStats, m_HeightAboveGround, glm::length(velocity) * 3.6f));
			lines.push_back(LOC(Traversal_WingsuitHint));
			lines.push_back(LOC(Traversal_WingsuitControls));
			break;
		case Mode::CLIMBING:
			if (m_bMantling)
			{
				break;
			}
			lines.push_back(LOC(Traversal_ClimbingTitle));
			lines.push_back(LOC(Traversal_ClimbingControls));
			break;
		case Mode::SKIING:
			lines.push_back(m_GroundSlipperiness > 0.6f ? LOC(Traversal_SkiingTitle) : LOC(Traversal_SlidingTitle));
			lines.push_back(LOCF(Traversal_SpeedStats, glm::length(velocity) * 3.6f));
			lines.push_back(LOC(Traversal_SkiingControls));
			break;
		default:
			if (m_Hook.state == HookState::ATTACHED)
			{
				lines.push_back(LOC(Traversal_HookTitle));
				lines.push_back(LOC(Traversal_HookControls));
			}
			break;
		}

		if (!lines.empty())
		{
			BitmapFont* font = g_Renderer->SetFont(SID("editor-02"));
			const real lineHeight = 3.2f * font->GetMetric('W')->height / (real)g_Window->GetSize().y;
			const glm::vec4 titleColour(0.55f, 0.9f, 1.0f, 1.0f);
			const glm::vec4 textColour(0.95f, 0.95f, 0.95f, 1.0f);
			real yOffset = 0.05f;
			for (i32 i = (i32)lines.size() - 1; i >= 0; --i)
			{
				g_Renderer->DrawStringSS(lines[i], i == 0 ? titleColour : textColour, AnchorPoint::BOTTOM, glm::vec2(0.0f, yOffset), 1.5f, 0.6f);
				yOffset += lineHeight;
			}
		}

		// Stamina bar below the cross hair while it's being used
		if (m_Stamina < 0.999f)
		{
			const glm::vec2 center(0.0f, -0.12f);
			const glm::vec2 halfSize(0.12f, 0.008f);
			const glm::vec4 fillColour = m_bStaminaExhausted ? glm::vec4(0.9f, 0.25f, 0.2f, 0.9f) :
				(m_Stamina < 0.25f ? glm::vec4(1.0f, 0.65f, 0.2f, 0.9f) : glm::vec4(0.45f, 0.95f, 0.45f, 0.9f));
			g_Renderer->EnqueueUntexturedQuad(center, AnchorPoint::CENTER, halfSize + glm::vec2(0.004f), glm::vec4(0.0f, 0.0f, 0.0f, 0.6f));
			g_Renderer->EnqueueUntexturedQuad(center - glm::vec2(halfSize.x * (1.0f - m_Stamina), 0.0f), AnchorPoint::CENTER,
				glm::vec2(halfSize.x * m_Stamina, halfSize.y), fillColour);
		}
	}

	void Traversal::DrawImGui()
	{
		if (!ImGui::TreeNode("Traversal"))
		{
			return;
		}

		ImGui::Text("Mode: %s%s", ModeName(m_Mode), m_Hook.state == HookState::ATTACHED ? " (grappling)" : "");
		ImGui::Text("Grounded: %s, height above ground: %.1fm", m_bGrounded ? "true" : "false", m_HeightAboveGround);
		ImGui::Text("Lift: %.2fm/s, ground slipperiness: %.2f", m_Lift, m_GroundSlipperiness);
		ImGui::ProgressBar(m_Stamina, ImVec2(-1.0f, 0.0f), "Stamina");

		ImGui::DragFloat("Jump speed", &m_Settings.jumpSpeed, 0.1f, 0.0f, 30.0f);
		ImGui::DragFloat("Glide speed", &m_Settings.glideSpeed, 0.1f, 1.0f, 40.0f);
		ImGui::DragFloat("Glide sink rate", &m_Settings.glideSinkRate, 0.05f, 0.0f, 10.0f);
		ImGui::DragFloat("Wingsuit lift", &m_Settings.wingsuitLift, 0.0005f, 0.0f, 0.1f, "%.4f");
		ImGui::DragFloat("Wingsuit drag", &m_Settings.wingsuitDrag, 0.0001f, 0.0f, 0.01f, "%.4f");
		ImGui::DragFloat("Hook range", &m_Settings.hookRange, 1.0f, 5.0f, 500.0f);
		ImGui::DragFloat("Reel speed", &m_Settings.reelSpeed, 0.1f, 0.0f, 60.0f);
		ImGui::DragFloat("Climb speed", &m_Settings.climbSpeed, 0.1f, 0.0f, 20.0f);
		ImGui::DragFloat("Ski max speed", &m_Settings.skiMaxSpeed, 0.5f, 1.0f, 120.0f);

		ImGui::TreePop();
	}
} // namespace flex
