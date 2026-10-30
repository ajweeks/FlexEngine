#include "stdafx.hpp"

#include "Scene/Objects/Building.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtx/norm.hpp> // For distance2
#include <glm/gtx/quaternion.hpp> // For rotation

#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCompoundShape.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
IGNORE_WARNINGS_POP

#include "Audio/AudioManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "GameControls.hpp"
#include "GameSounds.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "InputManager.hpp"
#include "JSONTypes.hpp"
#include "Localization.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/Objects/Lights.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/SceneManager.hpp"
#include "Traversal.hpp"
#include "Window/Window.hpp"
#include "WorldStats.hpp"

namespace flex
{
	std::vector<BuildPiece*> BuildPiece::s_Pieces;

	namespace
	{
		const char* s_PieceTypeNames[] = { "Floor", "Wall", "Roof", "Stairs", "Torch", "Ladder", "Rope", "FencePost" };
		static_assert(ARRAY_LENGTH(s_PieceTypeNames) == (u32)BuildPieceType::_COUNT, "s_PieceTypeNames must match BuildPieceType");
		const char* s_MaterialNames[] = { "Wood", "Stone" };
		static_assert(ARRAY_LENGTH(s_MaterialNames) == (u32)BuildMaterial::_COUNT, "s_MaterialNames must match BuildMaterial");

		const glm::vec3 FLAME_OFFSET(0.0f, 0.85f, 0.0f);
		const u32 MAX_TORCH_LIGHTS = 12;
		const real TORCH_LIGHT_RANGE = 160.0f;

		struct TorchLight
		{
			PointLightID lightID = InvalidPointLightID;
		};
		TorchLight s_TorchLights[MAX_TORCH_LIGHTS];
		i32 s_TorchLightsUpdatedFrame = -1;

		MaterialID GetOrCreateMaterial(const char* name, const glm::vec4& albedo, real roughness, const glm::vec4& emissive = VEC4_ZERO)
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
				createInfo.constEmissive = emissive;
				createInfo.bSerializable = false;
				materialID = g_Renderer->InitializeMaterial(&createInfo);
			}
			return materialID;
		}

		// Materials for each of a piece's two material slots
		void GetPieceMaterials(BuildPieceType type, BuildMaterial material, MaterialID& outMain, MaterialID& outSecondary)
		{
			switch (type)
			{
			case BuildPieceType::TORCH:
				outMain = GetOrCreateMaterial("Build torch handle", glm::vec4(0.32f, 0.2f, 0.11f, 1.0f), 0.9f);
				// Unlit, so the flame glows (pbr materials ignore emissive)
				outSecondary = g_Renderer->GetMaterialID("Build torch flame glow");
				if (outSecondary == InvalidMaterialID)
				{
					MaterialCreateInfo createInfo = {};
					createInfo.name = "Build torch flame glow";
					createInfo.shaderName = "colour";
					createInfo.colourMultiplier = glm::vec4(1.0f, 0.62f, 0.2f, 1.0f);
					createInfo.bSerializable = false;
					outSecondary = g_Renderer->InitializeMaterial(&createInfo);
				}
				return;
			case BuildPieceType::ROPE:
				outMain = GetOrCreateMaterial("Build rope", glm::vec4(0.62f, 0.5f, 0.32f, 1.0f), 0.95f);
				outSecondary = outMain;
				return;
			default:
				break;
			}

			if (material == BuildMaterial::STONE)
			{
				outMain = GetOrCreateMaterial("Build stone", glm::vec4(0.52f, 0.51f, 0.49f, 1.0f), 0.9f);
				outSecondary = GetOrCreateMaterial("Build stone trim", glm::vec4(0.38f, 0.37f, 0.36f, 1.0f), 0.85f);
			}
			else
			{
				outMain = GetOrCreateMaterial("Build wood", glm::vec4(0.55f, 0.38f, 0.22f, 1.0f), 0.8f);
				outSecondary = GetOrCreateMaterial("Build wood trim", glm::vec4(0.36f, 0.24f, 0.13f, 1.0f), 0.8f);
			}
		}

		// Translucent emissive materials for placement previews
		MaterialID GetPreviewMaterial(bool bValid)
		{
			const char* name = bValid ? "Build preview" : "Build preview blocked";
			MaterialID materialID = g_Renderer->GetMaterialID(name);
			if (materialID == InvalidMaterialID)
			{
				MaterialCreateInfo createInfo = {};
				createInfo.name = name;
				createInfo.shaderName = "hologram";
				// Alpha is the preview's opacity
				createInfo.constEmissive = bValid ? glm::vec4(0.55f, 0.85f, 1.6f, 0.38f) : glm::vec4(1.8f, 0.35f, 0.3f, 0.38f);
				createInfo.bSerializable = false;
				materialID = g_Renderer->InitializeMaterial(&createInfo);
			}
			return materialID;
		}

		// Closest hit along a ray against physics colliders (ignoring one object) & the terrain's density
		bool RaycastWorld(const glm::vec3& from, const glm::vec3& dir, real maxDistance, const btCollisionObject* ignored,
			glm::vec3& outHitPos, glm::vec3& outHitNormal, GameObject** outHitObject)
		{
			struct IgnoreObjectRayCallback final : public btCollisionWorld::ClosestRayResultCallback
			{
				IgnoreObjectRayCallback(const btVector3& from, const btVector3& to, const btCollisionObject* ignored) :
					ClosestRayResultCallback(from, to),
					ignored(ignored)
				{
					m_collisionFilterMask = (i32)CollisionType::STATIC | (i32)CollisionType::DEFAULT;
				}

				virtual bool needsCollision(btBroadphaseProxy* proxy) const override
				{
					return proxy->m_clientObject != ignored && ClosestRayResultCallback::needsCollision(proxy);
				}

				const btCollisionObject* ignored;
			};

			bool bHit = false;
			real closest = maxDistance;
			if (outHitObject != nullptr)
			{
				*outHitObject = nullptr;
			}

			const btVector3 btFrom = ToBtVec3(from);
			const btVector3 btTo = ToBtVec3(from + dir * maxDistance);
			IgnoreObjectRayCallback callback(btFrom, btTo, ignored);
			g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld()->rayTest(btFrom, btTo, callback);
			if (callback.hasHit())
			{
				bHit = true;
				outHitPos = ToVec3(callback.m_hitPointWorld);
				outHitNormal = glm::normalize(ToVec3(callback.m_hitNormalWorld));
				closest = glm::distance(from, outHitPos);
				if (outHitObject != nullptr && callback.m_collisionObject != nullptr)
				{
					*outHitObject = static_cast<GameObject*>(callback.m_collisionObject->getUserPointer());
				}
			}

			// Terrain collision only exists near the player, so also test against the terrain itself
			TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
			glm::vec3 terrainHitPos, terrainHitNormal;
			if (terrainGenerator != nullptr &&
				terrainGenerator->Raycast(from, dir, closest, terrainHitPos, terrainHitNormal) &&
				glm::distance(from, terrainHitPos) < closest - 0.1f)
			{
				bHit = true;
				outHitPos = terrainHitPos;
				outHitNormal = terrainHitNormal;
				if (outHitObject != nullptr)
				{
					*outHitObject = terrainGenerator;
				}
			}

			return bHit;
		}

		// Unit cube corners & faces, for building boxes
		const glm::vec3 s_FaceNormals[6] = { VEC3_RIGHT, -VEC3_RIGHT, VEC3_UP, -VEC3_UP, VEC3_FORWARD, -VEC3_FORWARD };

	} // anonymous namespace

	void AppendBoxToVertexData(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation,
		VertexBufferDataCreateInfo& vertexData, std::vector<u32>& indices)
	{
		for (const glm::vec3& normal : s_FaceNormals)
		{
			// Two axes spanning the face
			const glm::vec3 axisU = glm::abs(normal.y) > 0.5f ? VEC3_RIGHT : VEC3_UP;
			const glm::vec3 axisV = glm::cross(normal, axisU);
			const u32 base = (u32)vertexData.positions_3D.size();
			const glm::vec2 corners[4] = { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) };
			for (const glm::vec2& corner : corners)
			{
				const glm::vec3 local = (normal + axisU * corner.x + axisV * corner.y) * halfExtents;
				vertexData.positions_3D.push_back(center + rotation * local);
				vertexData.normals.push_back(rotation * normal);
				vertexData.tangents.push_back(rotation * axisU);
				// Texture coordinates in meters, so any future textures tile at a constant density
				vertexData.texCoords_UV.push_back(glm::vec2(glm::dot(local, axisU), glm::dot(local, axisV)));
				vertexData.colours_R32G32B32A32.push_back(VEC4_ONE);
			}
			// Counter-clockwise seen from outside (along the normal), like the engine's other meshes. These were
			// previously wound the other way, so back faces were culled & boxes were seen inside out.
			indices.insert(indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
		}
	}

	const char* BuildPieceTypeName(BuildPieceType type)
	{
		return (u32)type < (u32)BuildPieceType::_COUNT ? s_PieceTypeNames[(u32)type] : "Invalid";
	}

	const char* BuildMaterialName(BuildMaterial material)
	{
		return (u32)material < (u32)BuildMaterial::_COUNT ? s_MaterialNames[(u32)material] : "Invalid";
	}

	const char* BuildPieceDisplayName(BuildPieceType type)
	{
		static const LocID ids[] = { LOC_ID(Build_Floor), LOC_ID(Build_Wall), LOC_ID(Build_Roof), LOC_ID(Build_Stairs), LOC_ID(Build_Torch), LOC_ID(Build_Ladder), LOC_ID(Build_Rope), LOC_ID(Build_FencePost) };
		static_assert(ARRAY_LENGTH(ids) == (u32)BuildPieceType::_COUNT, "ids must match BuildPieceType");
		return (u32)type < (u32)BuildPieceType::_COUNT ? Loc::Get(ids[(u32)type]) : "";
	}

	const char* BuildMaterialDisplayName(BuildMaterial material)
	{
		static const LocID ids[] = { LOC_ID(Build_Wood), LOC_ID(Build_Stone) };
		static_assert(ARRAY_LENGTH(ids) == (u32)BuildMaterial::_COUNT, "ids must match BuildMaterial");
		return (u32)material < (u32)BuildMaterial::_COUNT ? Loc::Get(ids[(u32)material]) : "";
	}

	BuildPiece::BuildPiece(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /* = false */) :
		GameObject(name, BuildPieceSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
	}

	PropertyCollection* BuildPiece::BuildTypeUniquePropertyCollection()
	{
		return GameObject::BuildPropertyCollection();
	}

	void BuildPiece::Setup(BuildPieceType type, BuildMaterial material, real length, real size /* = GRID_SIZE */)
	{
		m_PieceType = type;
		m_Material = material;
		m_Length = length;
		m_Size = size;
	}

	void BuildPiece::GetBoxes(BuildPieceType type, real length, real size, std::vector<Box>& outBoxes)
	{
		const real half = size * 0.5f;
		const glm::quat slope = glm::angleAxis(glm::radians(-45.0f), VEC3_RIGHT);
		switch (type)
		{
		case BuildPieceType::FLOOR:
			outBoxes.push_back({ glm::vec3(0.0f, 0.1f, 0.0f), glm::vec3(half, 0.1f, half), QUAT_IDENTITY, 0 });
			// Beams along two edges, poking out of the slab's sides by a few mm so none of their faces are coplanar with the
			// slab's (they z-fight, worse with distance)
			outBoxes.push_back({ glm::vec3(0.0f, 0.1f, half - 0.08f + 0.0015f), glm::vec3(half + 0.003f, 0.12f, 0.08f + 0.0015f), QUAT_IDENTITY, 1 });
			outBoxes.push_back({ glm::vec3(0.0f, 0.1f, -half + 0.08f - 0.0015f), glm::vec3(half + 0.003f, 0.12f, 0.08f + 0.0015f), QUAT_IDENTITY, 1 });
			break;
		case BuildPieceType::WALL:
			// Along the cell's +z edge
			outBoxes.push_back({ glm::vec3(0.0f, half, half - 0.1f), glm::vec3(half - 0.12f, half, 0.1f), QUAT_IDENTITY, 0 });
			outBoxes.push_back({ glm::vec3(half - 0.12f, half, half - 0.1f), glm::vec3(0.12f, half, 0.13f), QUAT_IDENTITY, 1 });
			outBoxes.push_back({ glm::vec3(-half + 0.12f, half, half - 0.1f), glm::vec3(0.12f, half, 0.13f), QUAT_IDENTITY, 1 });
			break;
		case BuildPieceType::ROOF:
			// Rising from the cell's -z edge to the top of its +z edge
			outBoxes.push_back({ glm::vec3(0.0f, half, 0.0f), glm::vec3(half + 0.1f, 0.08f, half * 1.4142f + 0.15f), slope, 0 });
			break;
		case BuildPieceType::STAIRS:
		{
			// Steps about half a meter high rising towards +z (walked on as a ramp, see CreateCollisionShape)
			const u32 stepCount = (u32)glm::max(glm::round(size * 2.0f), 2.0f);
			const real stepDepth = size / (real)stepCount;
			for (u32 i = 0; i < stepCount; ++i)
			{
				const real top = stepDepth * (real)(i + 1);
				outBoxes.push_back({ glm::vec3(0.0f, top * 0.5f, -half + stepDepth * ((real)i + 0.5f)), glm::vec3(half * 0.8f, top * 0.5f, stepDepth * 0.5f), QUAT_IDENTITY, i % 2 });
			}
		} break;
		case BuildPieceType::TORCH:
			outBoxes.push_back({ glm::vec3(0.0f, 0.4f, 0.0f), glm::vec3(0.035f, 0.4f, 0.035f), QUAT_IDENTITY, 0 });
			outBoxes.push_back({ FLAME_OFFSET, glm::vec3(0.07f, 0.09f, 0.07f), QUAT_IDENTITY, 1 });
			break;
		case BuildPieceType::LADDER:
		{
			// Climbed from its -z side
			outBoxes.push_back({ glm::vec3(0.32f, length * 0.5f, 0.0f), glm::vec3(0.035f, length * 0.5f, 0.035f), QUAT_IDENTITY, 1 });
			outBoxes.push_back({ glm::vec3(-0.32f, length * 0.5f, 0.0f), glm::vec3(0.035f, length * 0.5f, 0.035f), QUAT_IDENTITY, 1 });
			for (real y = 0.3f; y < length - 0.1f; y += 0.35f)
			{
				outBoxes.push_back({ glm::vec3(0.0f, y, 0.0f), glm::vec3(0.32f, 0.022f, 0.022f), QUAT_IDENTITY, 0 });
			}
		} break;
		case BuildPieceType::ROPE:
			// Hangs down from its anchor (the origin)
			outBoxes.push_back({ glm::vec3(0.0f, -length * 0.5f, 0.0f), glm::vec3(0.03f, length * 0.5f, 0.03f), QUAT_IDENTITY, 0 });
			outBoxes.push_back({ glm::vec3(0.0f, -0.05f, 0.0f), glm::vec3(0.08f, 0.08f, 0.08f), QUAT_IDENTITY, 0 });
			// Knots to grip
			for (real y = 1.0f; y < length - 0.2f; y += 1.0f)
			{
				outBoxes.push_back({ glm::vec3(0.0f, -y, 0.0f), glm::vec3(0.055f, 0.05f, 0.055f), QUAT_IDENTITY, 0 });
			}
			break;
		case BuildPieceType::FENCE_POST:
			// A stout post (sunk a little into the ground) with a cap & a rail stub to tie ropes around
			outBoxes.push_back({ glm::vec3(0.0f, 0.55f, 0.0f), glm::vec3(0.09f, 0.75f, 0.09f), QUAT_IDENTITY, 0 });
			outBoxes.push_back({ glm::vec3(0.0f, 1.33f, 0.0f), glm::vec3(0.12f, 0.035f, 0.12f), QUAT_IDENTITY, 1 });
			outBoxes.push_back({ glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.11f, 0.05f, 0.11f), QUAT_IDENTITY, 1 });
			break;
		default:
			break;
		}
	}

	void BuildPiece::GetSolidBoxes(BuildPieceType type, real size, std::vector<Box>& outBoxes)
	{
		switch (type)
		{
		case BuildPieceType::FLOOR:
		case BuildPieceType::WALL:
		case BuildPieceType::ROOF:
			GetBoxes(type, 0.0f, size, outBoxes);
			break;
		case BuildPieceType::STAIRS:
		{
			// A smooth ramp under the step edges, which is easy to walk up
			const real half = size * 0.5f;
			outBoxes.push_back({ glm::vec3(0.0f, half - 0.12f, 0.0f), glm::vec3(half * 0.8f, 0.08f, half * 1.4142f), glm::angleAxis(glm::radians(-45.0f), VEC3_RIGHT), 0 });
			// Fill underneath so it can't be walked through from the side
			outBoxes.push_back({ glm::vec3(0.0f, half * 0.5f, half * 0.5f), glm::vec3(half * 0.8f, half * 0.5f, half * 0.5f), QUAT_IDENTITY, 0 });
		} break;
		case BuildPieceType::FENCE_POST:
			outBoxes.push_back({ glm::vec3(0.0f, 0.55f, 0.0f), glm::vec3(0.09f, 0.75f, 0.09f), QUAT_IDENTITY, 0 });
			break;
		case BuildPieceType::TORCH:
			outBoxes.push_back({ glm::vec3(0.0f, 0.4f, 0.0f), glm::vec3(0.035f, 0.4f, 0.035f), QUAT_IDENTITY, 0 });
			break;
		default:
			// Ladders, ladders & ropes don't block movement
			break;
		}
	}

	bool BuildPiece::IsGridPiece(BuildPieceType type)
	{
		return type == BuildPieceType::FLOOR || type == BuildPieceType::WALL || type == BuildPieceType::ROOF || type == BuildPieceType::STAIRS;
	}

	void BuildPiece::GetOccupancyBoxes(BuildPieceType type, real size, std::vector<Box>& outBoxes)
	{
		const real half = size * 0.5f;
		// Height of a floor's slab (& its beams): pieces in the same cell stand on top of it
		const real slab = 0.22f;
		switch (type)
		{
		case BuildPieceType::FLOOR:
			outBoxes.push_back({ glm::vec3(0.0f, 0.1f, 0.0f), glm::vec3(half, 0.1f, half), QUAT_IDENTITY, 0 });
			break;
		case BuildPieceType::WALL:
			// The panel without its posts, short of the corners so walls meeting at a corner don't overlap
			outBoxes.push_back({ glm::vec3(0.0f, (size + slab) * 0.5f, half - 0.1f), glm::vec3(half - 0.2f, (size - slab) * 0.5f, 0.1f), QUAT_IDENTITY, 0 });
			break;
		case BuildPieceType::ROOF:
			// Thin & without the overhang, so roofs side by side, in a row down a slope or meeting at a ridge only touch
			outBoxes.push_back({ glm::vec3(0.0f, half, 0.0f), glm::vec3(half, 0.03f, half * 1.4142f), glm::angleAxis(glm::radians(-45.0f), VEC3_RIGHT), 0 });
			break;
		case BuildPieceType::STAIRS:
		{
			// The steps, above a floor's slab
			std::vector<Box> steps;
			GetBoxes(type, 0.0f, size, steps);
			for (const Box& step : steps)
			{
				const real top = step.center.y + step.halfExtents.y;
				outBoxes.push_back({ glm::vec3(step.center.x, (top + slab) * 0.5f, step.center.z), glm::vec3(step.halfExtents.x, (top - slab) * 0.5f, step.halfExtents.z), QUAT_IDENTITY, 0 });
			}
		} break;
		case BuildPieceType::TORCH:
			// Wider than the pole so torches can't be packed together
			outBoxes.push_back({ glm::vec3(0.0f, 0.4f, 0.0f), glm::vec3(0.12f, 0.4f, 0.12f), QUAT_IDENTITY, 0 });
			break;
		case BuildPieceType::FENCE_POST:
			outBoxes.push_back({ glm::vec3(0.0f, 0.55f, 0.0f), glm::vec3(0.12f, 0.75f, 0.12f), QUAT_IDENTITY, 0 });
			break;
		default:
			break;
		}
	}

	btCollisionShape* BuildPiece::CreateCollisionShape(BuildPieceType type, real length, real size)
	{
		FLEX_UNUSED(length);
		std::vector<Box> boxes;
		GetSolidBoxes(type, size, boxes);
		if (boxes.empty())
		{
			return nullptr;
		}

		btCompoundShape* compound = new btCompoundShape();
		for (const Box& box : boxes)
		{
			btTransform childTransform(ToBtQuaternion(box.rotation), ToBtVec3(box.center));
			compound->addChildShape(childTransform, new btBoxShape(ToBtVec3(box.halfExtents)));
		}
		return compound;
	}

	void BuildPiece::ComputeFenceLinks()
	{
		m_FenceLinks.clear();
		const glm::vec3 pos = m_Transform.GetWorldPosition();
		const glm::quat invRot = glm::inverse(m_Transform.GetWorldRotation());
		for (BuildPiece* other : s_Pieces)
		{
			if (other == this || other->m_PieceType != BuildPieceType::FENCE_POST)
			{
				continue;
			}
			const glm::vec3 d = other->m_Transform.GetWorldPosition() - pos;
			if (glm::length(glm::vec2(d.x, d.z)) <= FENCE_LINK_DISTANCE && glm::abs(d.y) < 1.0f && glm::length2(d) > 0.04f)
			{
				m_FenceLinks.push_back(invRot * d);
			}
		}
	}

	void BuildPiece::MarkFenceNeighboursDirty(BuildPiece* changed)
	{
		if (changed->m_PieceType != BuildPieceType::FENCE_POST)
		{
			return;
		}
		const glm::vec3 pos = changed->m_Transform.GetWorldPosition();
		for (BuildPiece* other : s_Pieces)
		{
			if (other != changed && other->m_PieceType == BuildPieceType::FENCE_POST &&
				glm::distance(other->m_Transform.GetWorldPosition(), pos) <= FENCE_LINK_DISTANCE + 1.0f)
			{
				other->m_bLinksDirty = true;
			}
		}
	}

	void BuildPiece::CreateMesh()
	{
		MaterialID mainMaterialID, secondaryMaterialID;
		GetPieceMaterials(m_PieceType, m_Material, mainMaterialID, secondaryMaterialID);

		std::vector<Box> boxes;
		GetBoxes(m_PieceType, m_Length, m_Size, boxes);
		if (m_PieceType == BuildPieceType::FENCE_POST)
		{
			ComputeFenceLinks();
			for (const glm::vec3& link : m_FenceLinks)
			{
				// Our half of each plank; the other post draws the rest
				const real dist = glm::length(link);
				const glm::vec3 dir = link / dist;
				const glm::quat rot = glm::rotation(VEC3_RIGHT, dir);
				for (real h : { 0.5f, 0.95f })
				{
					boxes.push_back({ link * 0.25f + glm::vec3(0.0f, h, 0.0f), glm::vec3(dist * 0.25f, 0.035f, 0.025f), rot, 1 });
				}
			}
		}

		Mesh* mesh = SetMesh(new Mesh(this));
		const MaterialID materialIDs[2] = { mainMaterialID, secondaryMaterialID };
		for (u32 slot = 0; slot < 2; ++slot)
		{
			VertexBufferDataCreateInfo vertexData = {};
			vertexData.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(materialIDs[slot])->shaderID)->vertexAttributes;
			std::vector<u32> indices;
			for (const Box& box : boxes)
			{
				if (box.materialSlot == slot)
				{
					AppendBoxToVertexData(box.center, box.halfExtents, box.rotation, vertexData, indices);
				}
			}
			if (indices.empty())
			{
				continue;
			}
			MeshComponent::LoadFromMemory(mesh, vertexData, indices, materialIDs[slot]);
		}
	}

	void BuildPiece::Initialize()
	{
		CreateMesh();

		if (m_RigidBody == nullptr)
		{
			btCollisionShape* shape = CreateCollisionShape(m_PieceType, m_Length, m_Size);
			if (shape != nullptr)
			{
				SetCollisionShape(shape);
				RigidBody* rigidBody = SetRigidBody(new RigidBody((u32)CollisionType::STATIC, (u32)CollisionType::DEFAULT & ~(u32)CollisionType::STATIC));
				rigidBody->SetStatic(true);
			}
		}

		s_Pieces.push_back(this);
		MarkFenceNeighboursDirty(this);

		GameObject::Initialize();
	}

	void BuildPiece::Destroy(bool bDetachFromParent /* = true */)
	{
		s_Pieces.erase(std::remove(s_Pieces.begin(), s_Pieces.end(), this), s_Pieces.end());
		MarkFenceNeighboursDirty(this);

		bool bAnyTorches = false;
		for (BuildPiece* piece : s_Pieces)
		{
			bAnyTorches = bAnyTorches || piece->m_PieceType == BuildPieceType::TORCH;
		}
		if (!bAnyTorches)
		{
			ReleaseTorchLights();
		}

		GameObject::Destroy(bDetachFromParent);
	}

	void BuildPiece::Update()
	{
		if (m_bLinksDirty)
		{
			m_bLinksDirty = false;
			CreateMesh();
			if (Mesh* mesh = GetMesh())
			{
				mesh->PostInitialize();
			}
		}

		if (m_PieceType == BuildPieceType::TORCH)
		{
			UpdateTorchLights();
		}

		GameObject::Update();
	}

	void BuildPiece::UpdateTorchLights()
	{
		const i32 frame = g_Renderer->GetFramesRenderedCount();
		if (frame == s_TorchLightsUpdatedFrame)
		{
			return;
		}
		s_TorchLightsUpdatedFrame = frame;

		const glm::vec3 cameraPos = g_CameraManager->CurrentCamera()->position;
		std::vector<Pair<real, BuildPiece*>> torches;
		for (BuildPiece* piece : s_Pieces)
		{
			if (piece->m_PieceType == BuildPieceType::TORCH)
			{
				const real distSq = glm::distance2(piece->m_Transform.GetWorldPosition(), cameraPos);
				if (distSq < TORCH_LIGHT_RANGE * TORCH_LIGHT_RANGE)
				{
					torches.emplace_back(distSq, piece);
				}
			}
		}
		std::sort(torches.begin(), torches.end(), [](const Pair<real, BuildPiece*>& a, const Pair<real, BuildPiece*>& b) { return a.first < b.first; });
		if (!torches.empty())
		{
			// Quiet crackle from the nearest torch
			GameSounds::ReportFire(torches[0].second->m_Transform.GetWorldPosition(), 0.3f);
		}

		const real time = AnimationTime();
		for (u32 i = 0; i < MAX_TORCH_LIGHTS; ++i)
		{
			TorchLight& light = s_TorchLights[i];
			if (i < (u32)torches.size())
			{
				BuildPiece* torch = torches[i].second;
				const glm::vec3 flamePos = torch->m_Transform.GetWorldPosition() + torch->m_Transform.GetWorldRotation() * (FLAME_OFFSET + glm::vec3(0.0f, 0.15f, 0.0f));
				// Flicker, offset per torch so they don't pulse in unison
				const real phase = flamePos.x * 1.7f + flamePos.z * 2.3f;
				const real flicker = FireFlicker(time, phase);
				PointLightData data = {};
				data.pos = flamePos;
				data.enabled = 1;
				// Light colours are linear: a warm white (~3500K) reads as firelight once lit surfaces' own colour is
				// added, while a saturated orange turns wooden interiors entirely orange
				data.colour = glm::vec3(1.0f, 0.8f, 0.6f);
				// Fade out with distance so lights being reassigned isn't noticeable
				data.brightness = 16.0f * flicker * (1.0f - glm::smoothstep(TORCH_LIGHT_RANGE * 0.7f, TORCH_LIGHT_RANGE, glm::sqrt(torches[i].first)));
				if (light.lightID == InvalidPointLightID)
				{
					light.lightID = g_Renderer->RegisterPointLight(&data);
				}
				else
				{
					g_Renderer->UpdatePointLightData(light.lightID, &data);
				}
			}
			else if (light.lightID != InvalidPointLightID)
			{
				g_Renderer->RemovePointLight(light.lightID);
				light.lightID = InvalidPointLightID;
			}
		}
	}

	void BuildPiece::ReleaseTorchLights()
	{
		for (TorchLight& light : s_TorchLights)
		{
			if (light.lightID != InvalidPointLightID)
			{
				g_Renderer->RemovePointLight(light.lightID);
				light.lightID = InvalidPointLightID;
			}
		}
	}

	bool BuildPiece::IsClimbable() const
	{
		return m_PieceType == BuildPieceType::LADDER || m_PieceType == BuildPieceType::ROPE;
	}

	glm::vec3 BuildPiece::GetClimbBottom() const
	{
		const glm::vec3 pos = m_Transform.GetWorldPosition();
		// Ropes hang along their local -y (tossed ropes may lie at an angle down a slope)
		return m_PieceType == BuildPieceType::ROPE ? pos - (m_Transform.GetWorldRotation() * VEC3_UP) * m_Length : pos;
	}

	glm::vec3 BuildPiece::GetClimbTop() const
	{
		const glm::vec3 pos = m_Transform.GetWorldPosition();
		return m_PieceType == BuildPieceType::ROPE ? pos : pos + VEC3_UP * m_Length;
	}

	glm::vec3 BuildPiece::ClosestPointOnClimbAxis(const glm::vec3& pos) const
	{
		const glm::vec3 bottom = GetClimbBottom();
		const glm::vec3 top = GetClimbTop();
		if (glm::abs(top.x - bottom.x) + glm::abs(top.z - bottom.z) < 1e-3f)
		{
			return glm::vec3(bottom.x, glm::clamp(pos.y, bottom.y, top.y), bottom.z);
		}
		// Slanted: the point at the same height
		const real t = glm::clamp((pos.y - bottom.y) / glm::max(top.y - bottom.y, 1e-3f), 0.0f, 1.0f);
		return glm::mix(bottom, top, t);
	}

	glm::vec3 BuildPiece::GetClimbFacing() const
	{
		if (m_PieceType == BuildPieceType::LADDER)
		{
			return m_Transform.GetWorldRotation() * -VEC3_FORWARD;
		}
		return VEC3_ZERO;
	}

	BuildPiece* BuildPiece::FindClimbable(const glm::vec3& pos, real maxDistance, real reachAbove /* = 0.5f */)
	{
		BuildPiece* closest = nullptr;
		real closestDistance = maxDistance;
		for (BuildPiece* piece : s_Pieces)
		{
			if (!piece->IsClimbable())
			{
				continue;
			}
			const glm::vec3 bottom = piece->GetClimbBottom();
			const glm::vec3 top = piece->GetClimbTop();
			// The player's capsule (about 2m tall, centered on pos) must overlap the climbable's height
			if (pos.y < bottom.y - reachAbove || pos.y > top.y + 1.2f)
			{
				continue;
			}
			const glm::vec3 axisPoint = piece->ClosestPointOnClimbAxis(pos);
			const real distance = glm::length(glm::vec2(pos.x - axisPoint.x, pos.z - axisPoint.z));
			if (distance < closestDistance)
			{
				closest = piece;
				closestDistance = distance;
			}
		}
		return closest;
	}

	void BuildPiece::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject pieceObj;
		if (parentObject.TryGetObject("build piece", pieceObj))
		{
			i32 type = 0;
			i32 material = 0;
			pieceObj.TryGetInt("type", type);
			pieceObj.TryGetInt("material", material);
			pieceObj.TryGetFloat("length", m_Length);
			// Pieces saved before the grid was made coarser have no size
			m_Size = LEGACY_GRID_SIZE;
			pieceObj.TryGetFloat("size", m_Size);
			m_Size = glm::clamp(m_Size, 0.5f, 32.0f);
			m_PieceType = (BuildPieceType)glm::clamp(type, 0, (i32)BuildPieceType::_COUNT - 1);
			m_Material = (BuildMaterial)glm::clamp(material, 0, (i32)BuildMaterial::_COUNT - 1);
		}
	}

	void BuildPiece::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		FLEX_UNUSED(bSerializePrefabData);

		JSONObject pieceObj = {};
		pieceObj.fields.emplace_back("type", JSONValue((i32)m_PieceType));
		pieceObj.fields.emplace_back("material", JSONValue((i32)m_Material));
		if (m_Length != 0.0f)
		{
			pieceObj.fields.emplace_back("length", JSONValue(m_Length, 2));
		}
		pieceObj.fields.emplace_back("size", JSONValue(m_Size, 2));
		parentObject.fields.emplace_back("build piece", JSONValue(pieceObj));
	}

	//
	// Placement rules
	//

	namespace
	{
		struct OrientedBox
		{
			glm::vec3 center;
			glm::vec3 axes[3];
			glm::vec3 halfExtents;
		};

		OrientedBox ToWorld(const BuildPiece::Box& box, const glm::vec3& pos, const glm::quat& rot)
		{
			OrientedBox result;
			const glm::quat boxRot = rot * box.rotation;
			result.center = pos + rot * box.center;
			result.axes[0] = boxRot * glm::vec3(1.0f, 0.0f, 0.0f);
			result.axes[1] = boxRot * glm::vec3(0.0f, 1.0f, 0.0f);
			result.axes[2] = boxRot * glm::vec3(0.0f, 0.0f, 1.0f);
			result.halfExtents = box.halfExtents;
			return result;
		}

		// Separating axis test: the boxes overlap when they interpenetrate by more than tolerance along every axis
		bool OrientedBoxesOverlap(const OrientedBox& a, const OrientedBox& b, real tolerance)
		{
			glm::vec3 axes[15];
			u32 axisCount = 0;
			for (u32 i = 0; i < 3; ++i)
			{
				axes[axisCount++] = a.axes[i];
				axes[axisCount++] = b.axes[i];
			}
			for (u32 i = 0; i < 3; ++i)
			{
				for (u32 j = 0; j < 3; ++j)
				{
					const glm::vec3 axis = glm::cross(a.axes[i], b.axes[j]);
					const real length = glm::length(axis);
					// Parallel edges: already covered by the face axes
					if (length > 1e-3f)
					{
						axes[axisCount++] = axis / length;
					}
				}
			}

			const glm::vec3 offset = b.center - a.center;
			for (u32 i = 0; i < axisCount; ++i)
			{
				const glm::vec3& axis = axes[i];
				real radiusA = 0.0f;
				real radiusB = 0.0f;
				for (u32 k = 0; k < 3; ++k)
				{
					radiusA += glm::abs(glm::dot(a.axes[k], axis)) * a.halfExtents[k];
					radiusB += glm::abs(glm::dot(b.axes[k], axis)) * b.halfExtents[k];
				}
				const real penetration = radiusA + radiusB - glm::abs(glm::dot(offset, axis));
				if (penetration <= tolerance)
				{
					return false;
				}
			}
			return true;
		}

		bool AnyBoxesOverlap(const std::vector<BuildPiece::Box>& boxesA, const glm::vec3& posA, const glm::quat& rotA,
			const std::vector<BuildPiece::Box>& boxesB, const glm::vec3& posB, const glm::quat& rotB)
		{
			for (const BuildPiece::Box& boxA : boxesA)
			{
				const OrientedBox a = ToWorld(boxA, posA, rotA);
				for (const BuildPiece::Box& boxB : boxesB)
				{
					if (OrientedBoxesOverlap(a, ToWorld(boxB, posB, rotB), BUILD_OVERLAP_TOLERANCE))
					{
						return true;
					}
				}
			}
			return false;
		}
	} // anonymous namespace

	bool BuildPiecesOverlap(const BuildPlacement& a, const BuildPlacement& b)
	{
		// Torches & fence posts only conflict with each other
		auto isSmall = [](BuildPieceType t) { return t == BuildPieceType::TORCH || t == BuildPieceType::FENCE_POST; };
		const bool bSmallPair = isSmall(a.type) && isSmall(b.type);
		if (!bSmallPair && (!BuildPiece::IsGridPiece(a.type) || !BuildPiece::IsGridPiece(b.type)))
		{
			return false;
		}

		// Cheap reject: grid pieces fit within a sphere of about their cell's size around their position
		const real reach = (a.size + b.size) * 1.1f;
		if (glm::distance2(a.pos, b.pos) > reach * reach)
		{
			return false;
		}

		if (a.type == BuildPieceType::WALL && b.type == BuildPieceType::WALL)
		{
			// Walls on either side of the same edge don't touch, but there's only room for one
			const glm::vec3 edgeA = a.pos + a.rot * glm::vec3(0.0f, 0.0f, a.size * 0.5f);
			const glm::vec3 edgeB = b.pos + b.rot * glm::vec3(0.0f, 0.0f, b.size * 0.5f);
			const bool bParallel = glm::abs(glm::dot(a.rot * VEC3_RIGHT, b.rot * VEC3_RIGHT)) > 0.9f;
			if (bParallel &&
				glm::distance(glm::vec2(edgeA.x, edgeA.z), glm::vec2(edgeB.x, edgeB.z)) < 0.3f &&
				glm::abs(edgeA.y - edgeB.y) < glm::min(a.size, b.size) - 0.1f)
			{
				return true;
			}
		}

		std::vector<BuildPiece::Box> boxesA, boxesB;
		BuildPiece::GetOccupancyBoxes(a.type, a.size, boxesA);
		BuildPiece::GetOccupancyBoxes(b.type, b.size, boxesB);
		return AnyBoxesOverlap(boxesA, a.pos, a.rot, boxesB, b.pos, b.rot);
	}

	bool BuildPieceOverlapsAABB(const BuildPlacement& piece, const glm::vec3& aabbMin, const glm::vec3& aabbMax)
	{
		std::vector<BuildPiece::Box> boxes;
		BuildPiece::GetSolidBoxes(piece.type, piece.size, boxes);
		const std::vector<BuildPiece::Box> aabb = { { (aabbMin + aabbMax) * 0.5f, (aabbMax - aabbMin) * 0.5f, QUAT_IDENTITY, 0 } };
		return AnyBoxesOverlap(boxes, piece.pos, piece.rot, aabb, VEC3_ZERO, QUAT_IDENTITY);
	}

	BuildPlacement ExtendBuildPiece(const BuildPlacement& aimed, const glm::vec3& hitPos)
	{
		const real s = aimed.size;
		const real half = s * 0.5f;
		const glm::vec3 local = glm::inverse(aimed.rot) * (hitPos - aimed.pos);

		glm::vec3 offset(0.0f);
		glm::quat extraRot = QUAT_IDENTITY;
		// Comparisons prefer the first option on ties, so the same hit point always gives the same result
		switch (aimed.type)
		{
		case BuildPieceType::FLOOR:
			if (glm::abs(local.x) >= glm::abs(local.z))
			{
				offset = glm::vec3(local.x >= 0.0f ? s : -s, 0.0f, 0.0f);
			}
			else
			{
				offset = glm::vec3(0.0f, 0.0f, local.z >= 0.0f ? s : -s);
			}
			break;
		case BuildPieceType::WALL:
		{
			// Along the wall (local x) or on top of it, whichever edge is nearest
			const real toSide = half - glm::abs(local.x);
			const real toTop = s - local.y;
			if (toSide <= toTop)
			{
				offset = glm::vec3(local.x >= 0.0f ? s : -s, 0.0f, 0.0f);
			}
			else
			{
				offset = glm::vec3(0.0f, s, 0.0f);
			}
		} break;
		case BuildPieceType::STAIRS:
		case BuildPieceType::ROOF:
		{
			// Sides (local x) or ends (local z, the slope rising towards +z)
			if (glm::abs(local.x) >= glm::abs(local.z))
			{
				offset = glm::vec3(local.x >= 0.0f ? s : -s, 0.0f, 0.0f);
			}
			else if (local.z < 0.0f)
			{
				// Down the slope
				offset = glm::vec3(0.0f, -s, -s);
			}
			else if (aimed.type == BuildPieceType::STAIRS)
			{
				// Up the slope
				offset = glm::vec3(0.0f, s, s);
			}
			else
			{
				// A roof sloping back down from the ridge
				offset = glm::vec3(0.0f, 0.0f, s);
				extraRot = glm::angleAxis(PI, VEC3_UP);
			}
		} break;
		default:
			break;
		}

		BuildPlacement result = aimed;
		result.pos = aimed.pos + aimed.rot * offset;
		result.rot = glm::normalize(aimed.rot * extraRot);
		return result;
	}

	bool RunBuildingSelfTests()
	{
		bool bPassed = true;
		auto Check = [&bPassed](bool bValue, const char* what)
		{
			if (!bValue)
			{
				PrintError("Building test failed: %s\n", what);
				bPassed = false;
			}
		};
		auto Piece = [](BuildPieceType type, const glm::vec3& pos, real yawQuarterTurns)
		{
			BuildPlacement placement;
			placement.type = type;
			placement.pos = pos;
			placement.rot = glm::angleAxis(yawQuarterTurns * PI_DIV_TWO, VEC3_UP);
			return placement;
		};
		auto Near = [](const glm::vec3& a, const glm::vec3& b) { return glm::distance(a, b) < 0.01f; };

		const real g = BuildPiece::GRID_SIZE;
		const glm::vec3 base(8.0f * g + g * 0.5f, 12.25f, -3.0f * g + g * 0.5f);
		const BuildPlacement floor = Piece(BuildPieceType::FLOOR, base, 0.0f);

		// Overlaps
		Check(BuildPiecesOverlap(floor, floor), "a floor overlaps itself");
		Check(BuildPiecesOverlap(floor, Piece(BuildPieceType::FLOOR, base + glm::vec3(1.0f, 0.0f, 0.0f), 0.0f)), "floors offset by 1m overlap");
		Check(!BuildPiecesOverlap(floor, Piece(BuildPieceType::FLOOR, base + glm::vec3(g, 0.0f, 0.0f), 0.0f)), "neighbouring floors don't overlap");
		Check(!BuildPiecesOverlap(floor, Piece(BuildPieceType::FLOOR, base + glm::vec3(g, 0.0f, g), 1.0f)), "diagonal floors don't overlap");
		Check(!BuildPiecesOverlap(floor, Piece(BuildPieceType::FLOOR, base + glm::vec3(0.0f, g, 0.0f), 0.0f)), "floors above each other don't overlap");
		for (i32 i = 0; i < 4; ++i)
		{
			const BuildPlacement wall = Piece(BuildPieceType::WALL, base, (real)i);
			Check(!BuildPiecesOverlap(floor, wall), "a wall on a floor's edge doesn't overlap it");
			Check(!BuildPiecesOverlap(Piece(BuildPieceType::WALL, base, (real)((i + 1) % 4)), wall), "walls meeting at a corner don't overlap");
			Check(!BuildPiecesOverlap(Piece(BuildPieceType::FLOOR, base + glm::vec3(0.0f, g, 0.0f), 0.0f), wall), "a floor on top of walls doesn't overlap them");
			for (i32 j = 0; j < 4; ++j)
			{
				Check(!BuildPiecesOverlap(Piece(BuildPieceType::ROOF, base + glm::vec3(0.0f, g, 0.0f), (real)j), wall), "a roof on top of walls doesn't overlap them");
			}
			Check(BuildPiecesOverlap(Piece(BuildPieceType::ROOF, base, (real)i), wall), "a roof in a walled cell (same level) overlaps the walls");
		}
		const BuildPlacement wall = Piece(BuildPieceType::WALL, base, 0.0f);
		Check(BuildPiecesOverlap(wall, wall), "a wall overlaps itself");
		Check(BuildPiecesOverlap(wall, Piece(BuildPieceType::WALL, base + glm::vec3(0.0f, 0.0f, g), 2.0f)), "walls on both sides of one edge overlap");
		Check(!BuildPiecesOverlap(wall, Piece(BuildPieceType::WALL, base + glm::vec3(g, 0.0f, 0.0f), 0.0f)), "walls in a row don't overlap");
		Check(!BuildPiecesOverlap(wall, Piece(BuildPieceType::WALL, base + glm::vec3(0.0f, g, 0.0f), 0.0f)), "stacked walls don't overlap");
		Check(!BuildPiecesOverlap(floor, Piece(BuildPieceType::STAIRS, base, 1.0f)), "stairs on a floor don't overlap it");
		Check(BuildPiecesOverlap(Piece(BuildPieceType::STAIRS, base, 0.0f), wall), "stairs running into a wall overlap it");
		Check(!BuildPiecesOverlap(Piece(BuildPieceType::STAIRS, base, 1.0f), Piece(BuildPieceType::STAIRS, base + glm::vec3(0.0f, 0.0f, g), 1.0f)), "stairs side by side don't overlap");

		// Extending from the aimed edge
		for (i32 rot = 0; rot < 4; ++rot)
		{
			for (BuildPieceType type : { BuildPieceType::FLOOR, BuildPieceType::WALL, BuildPieceType::STAIRS, BuildPieceType::ROOF })
			{
				const BuildPlacement aimed = Piece(type, base, (real)rot);
				const glm::vec3 hits[] = { glm::vec3(1.8f, 0.2f, 0.3f), glm::vec3(-1.8f, 0.2f, 0.3f), glm::vec3(0.3f, 0.2f, 1.8f), glm::vec3(0.3f, 0.2f, -1.8f), glm::vec3(0.3f, 3.9f, 1.9f) };
				for (const glm::vec3& localHit : hits)
				{
					const BuildPlacement extended = ExtendBuildPiece(aimed, aimed.pos + aimed.rot * localHit);
					Check(extended.type == type, "extending keeps the type");
					Check(!BuildPiecesOverlap(aimed, extended), "an extension doesn't overlap the piece it extends");
					Check(glm::distance(extended.pos, aimed.pos) > g * 0.9f, "an extension moves at least a cell");
					// Still on the grid
					const glm::vec3 delta = (extended.pos - aimed.pos) / g;
					Check(Near(delta, glm::round(delta)), "an extension stays on the grid");
				}
			}
		}
		Check(Near(ExtendBuildPiece(floor, base + glm::vec3(1.9f, 0.2f, 0.5f)).pos, base + glm::vec3(g, 0.0f, 0.0f)), "aiming at a floor's +x edge extends it towards +x");
		Check(Near(ExtendBuildPiece(floor, base + glm::vec3(0.5f, 0.2f, -1.9f)).pos, base + glm::vec3(0.0f, 0.0f, -g)), "aiming at a floor's -z edge extends it towards -z");
		const BuildPlacement turnedWall = Piece(BuildPieceType::WALL, base, 1.0f); // Along the cell's +x edge, running along z
		Check(Near(ExtendBuildPiece(turnedWall, base + glm::vec3(1.9f, 2.0f, -1.7f)).pos, base + glm::vec3(0.0f, 0.0f, -g)), "aiming near a wall's end extends it along its length");
		Check(Near(ExtendBuildPiece(turnedWall, base + glm::vec3(1.9f, 3.8f, 0.2f)).pos, base + glm::vec3(0.0f, g, 0.0f)), "aiming near a wall's top stacks another on it");
		const BuildPlacement stairs = Piece(BuildPieceType::STAIRS, base, 0.0f);
		Check(Near(ExtendBuildPiece(stairs, base + glm::vec3(0.0f, 3.5f, 1.8f)).pos, base + glm::vec3(0.0f, g, g)), "aiming at the top of stairs continues them upwards");
		const BuildPlacement ridge = ExtendBuildPiece(Piece(BuildPieceType::ROOF, base, 0.0f), base + glm::vec3(0.0f, 3.9f, 1.8f));
		Check(Near(ridge.pos, base + glm::vec3(0.0f, 0.0f, g)) && Near(ridge.rot * VEC3_FORWARD, -VEC3_FORWARD), "aiming at a roof's ridge adds a mirrored roof");
		Check(Near(ExtendBuildPiece(floor, base + glm::vec3(1.9f, 0.2f, 0.5f)).pos, ExtendBuildPiece(floor, base + glm::vec3(1.9f, 0.2f, 0.5f)).pos), "extending is deterministic");

		// The player's bounds
		const glm::vec3 feet = base + glm::vec3(0.0f, 0.2f, 0.0f);
		const glm::vec3 playerMin = feet + glm::vec3(-0.4f, 0.0f, -0.4f);
		const glm::vec3 playerMax = feet + glm::vec3(0.4f, 1.8f, 0.4f);
		Check(!BuildPieceOverlapsAABB(floor, playerMin, playerMax), "a player standing on a floor doesn't overlap it");
		Check(BuildPieceOverlapsAABB(floor, playerMin - VEC3_UP * 0.15f, playerMax - VEC3_UP * 0.15f), "a player sunk into a floor overlaps it");
		Check(!BuildPieceOverlapsAABB(wall, playerMin, playerMax), "a player in the middle of a cell doesn't overlap its walls");
		Check(BuildPieceOverlapsAABB(wall, playerMin + glm::vec3(0.0f, 0.0f, 1.5f), playerMax + glm::vec3(0.0f, 0.0f, 1.5f)), "a player at a cell's edge overlaps a wall there");

		if (bPassed)
		{
			Print("Building placement tests passed\n");
		}
		return bPassed;
	}

	//
	// BuildMode
	//

	void BuildMode::Initialize(Player* player)
	{
		m_Player = player;
		m_PlaceSoundID = g_ResourceManager->GetOrLoadAudioSourceID(SID("click-02.wav"), true);
		m_RemoveSoundID = g_ResourceManager->GetOrLoadAudioSourceID(SID("crunch-13.wav"), true);
	}

	void BuildMode::SetActive(bool bActive)
	{
		if (bActive == m_bActive)
		{
			// (Called every frame by the build test tour, which shouldn't reset the placement state)
			return;
		}
		m_bActive = bActive;
		m_bPlacementValid = false;
		m_bPlacementHasPosition = false;
		m_BlockReason = BuildBlockReason::NONE;
		if (!bActive)
		{
			DestroyPreview();
		}
	}

	void BuildMode::DestroyPreview()
	{
		GameObject* preview = m_PreviewID.Get();
		if (preview != nullptr)
		{
			g_SceneManager->CurrentScene()->RemoveObject(preview, true);
		}
		m_PreviewID = InvalidGameObjectID;
		m_PreviewType = BuildPieceType::_COUNT;
	}

	void BuildMode::UpdatePreview(bool bShow)
	{
		GameObject* preview = m_PreviewID.Get();
		if (!bShow)
		{
			if (preview != nullptr)
			{
				preview->SetVisible(false);
			}
			return;
		}

		if (preview == nullptr)
		{
			// Objects are added to the scene at the start of the next frame, wait for that before making another
			// (if it's still missing after that, it was destroyed along with the scene)
			const i32 frame = g_Renderer->GetFramesRenderedCount();
			if (m_PreviewID.IsValid() && frame - m_PreviewCreatedFrame < 3)
			{
				return;
			}
			m_PreviewCreatedFrame = frame;
			preview = new GameObject("Build preview", BaseObjectSID);
			preview->SetSerializable(false);
			preview->SetVisibleInSceneExplorer(false);
			preview->SetCastsShadow(false);
			// Its mesh is built once it's in the scene
			g_SceneManager->CurrentScene()->AddRootObject(preview);
			m_PreviewID = preview->ID;
			m_PreviewType = BuildPieceType::_COUNT;
			return;
		}

		const bool bValid = m_bPlacementValid;
		if (m_PreviewType != m_SelectedType || m_bPreviewValid != bValid || glm::abs(m_PreviewLength - m_PlacementLength) > 0.05f)
		{
			m_PreviewType = m_SelectedType;
			m_bPreviewValid = bValid;
			m_PreviewLength = m_PlacementLength;

			const MaterialID materialID = GetPreviewMaterial(bValid);
			std::vector<BuildPiece::Box> boxes;
			BuildPiece::GetBoxes(m_SelectedType, m_PlacementLength, BuildPiece::GRID_SIZE, boxes);

			VertexBufferDataCreateInfo vertexData = {};
			vertexData.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(materialID)->shaderID)->vertexAttributes;
			std::vector<u32> indices;
			for (const BuildPiece::Box& box : boxes)
			{
				// Slightly inflated so it doesn't z-fight with pieces it lines up with
				AppendBoxToVertexData(box.center, box.halfExtents + glm::vec3(0.01f), box.rotation, vertexData, indices);
			}
			Mesh* mesh = preview->SetMesh(new Mesh(preview));
			if (!indices.empty())
			{
				MeshComponent::LoadFromMemory(mesh, vertexData, indices, materialID);
				mesh->PostInitialize();
			}
		}

		preview->GetTransform()->SetWorldPosition(m_PlacementPos, false);
		preview->GetTransform()->SetWorldRotation(m_PlacementRot, true);
		preview->SetVisible(true);
	}

	BuildPiece* BuildMode::GetTargetedPiece() const
	{
		BaseCamera* camera = g_CameraManager->CurrentCamera();
		const glm::vec3 origin = camera->position;
		const glm::vec3 dir = camera->forward;

		glm::vec3 hitPos, hitNormal;
		GameObject* hitObject = nullptr;
		real hitDistance = m_Reach;
		BuildPiece* result = nullptr;
		if (RaycastWorld(origin, dir, m_Reach, m_Player->GetRigidBody()->GetRigidBodyInternal(), hitPos, hitNormal, &hitObject))
		{
			hitDistance = glm::distance(origin, hitPos);
			if (hitObject != nullptr && hitObject->GetTypeID() == BuildPieceSID)
			{
				result = (BuildPiece*)hitObject;
			}
		}

		// Pieces without collision (torches, ladders & ropes) are picked by their distance from the ray
		for (BuildPiece* piece : BuildPiece::GetAllPieces())
		{
			if (piece->GetRigidBody() != nullptr)
			{
				continue;
			}
			glm::vec3 a = piece->GetTransform()->GetWorldPosition();
			glm::vec3 b = a + piece->GetTransform()->GetWorldRotation() * glm::vec3(0.0f, 1.0f, 0.0f);
			if (piece->IsClimbable())
			{
				a = piece->GetClimbBottom();
				b = piece->GetClimbTop();
			}
			// Closest points between the ray & the segment
			const glm::vec3 segment = b - a;
			const real segmentLengthSq = glm::max(glm::dot(segment, segment), 1e-6f);
			real bestDistance = FLT_MAX;
			real bestT = 0.0f;
			for (u32 i = 0; i <= 16; ++i)
			{
				const real s = (real)i / 16.0f;
				const glm::vec3 point = a + segment * s;
				const real t = glm::clamp(glm::dot(point - origin, dir), 0.0f, hitDistance);
				const real distance = glm::distance(origin + dir * t, point);
				if (distance < bestDistance)
				{
					bestDistance = distance;
					bestT = t;
				}
			}
			FLEX_UNUSED(segmentLengthSq);
			if (bestDistance < 0.35f && bestT < hitDistance + 0.3f)
			{
				result = piece;
				hitDistance = bestT;
			}
		}
		return result;
	}

	bool BuildMode::ComputePlacement(glm::vec3& outPos, glm::quat& outRot, real& outLength, bool& outHasPosition)
	{
		outHasPosition = false;
		m_BlockReason = BuildBlockReason::NO_TARGET;
		m_BlockingType = BuildPieceType::_COUNT;
		BaseCamera* camera = g_CameraManager->CurrentCamera();
		const glm::vec3 origin = camera->position;
		const glm::vec3 dir = camera->forward;
		const btCollisionObject* playerBody = m_Player->GetRigidBody()->GetRigidBodyInternal();

		glm::vec3 hitPos, hitNormal;
		GameObject* hitObject = nullptr;
		if (!RaycastWorld(origin, dir, m_Reach, playerBody, hitPos, hitNormal, &hitObject))
		{
			return false;
		}

		outHasPosition = true;
		m_BlockReason = BuildBlockReason::NONE;
		const real grid = BuildPiece::GRID_SIZE;
		const glm::vec3 heading = glm::normalize(glm::vec3(dir.x, 0.0f, dir.z) + glm::vec3(1e-5f, 0.0f, 0.0f));
		outLength = 0.0f;

		switch (m_SelectedType)
		{
		case BuildPieceType::TORCH:
		{
			// Upright on the ground, leaning out of walls
			const glm::vec3 lean = glm::normalize(VEC3_UP + glm::vec3(hitNormal.x, 0.0f, hitNormal.z) * 0.9f);
			outRot = glm::rotation(VEC3_UP, glm::abs(hitNormal.y) > 0.7f ? VEC3_UP : lean);
			outPos = hitPos - (outRot * VEC3_UP) * 0.15f;
			if (hitNormal.y <= -0.5f)
			{
				m_BlockReason = BuildBlockReason::CEILING;
				return false;
			}
			m_BlockReason = CheckGridPlacement(outPos, outRot);
			return m_BlockReason == BuildBlockReason::NONE;
		}
		case BuildPieceType::FENCE_POST:
		{
			// Upright on the ground
			outRot = glm::angleAxis((real)m_Rotation * PI_DIV_TWO, VEC3_UP);
			outPos = hitPos;
			if (hitNormal.y < 0.5f)
			{
				m_BlockReason = BuildBlockReason::NO_TARGET;
				return false;
			}
			m_BlockReason = CheckGridPlacement(outPos, outRot);
			return m_BlockReason == BuildBlockReason::NONE;
		}
		case BuildPieceType::LADDER:
		{
			// Against the wall being looked at, from the ground up to a little above the aimed point
			glm::vec3 facing = glm::vec3(hitNormal.x, 0.0f, hitNormal.z);
			const bool bOnWall = glm::length(facing) > 0.3f;
			facing = bOnWall ? glm::normalize(facing) : -heading;
			glm::vec3 base = hitPos + facing * 0.12f;
			real top = base.y + (bOnWall ? 1.2f : BuildPiece::DEFAULT_LADDER_LENGTH);
			glm::vec3 groundPos, groundNormal;
			if (RaycastWorld(base + VEC3_UP * 0.1f + facing * 0.3f, -VEC3_UP, 40.0f, playerBody, groundPos, groundNormal, nullptr))
			{
				base.y = groundPos.y;
			}
			outLength = glm::clamp(top - base.y, 2.0f, 40.0f);
			outPos = base;
			// Local -z faces the climber
			outRot = glm::quatLookAt(facing, VEC3_UP);
			return true;
		}
		case BuildPieceType::ROPE:
		{
			// Hanging from the aimed point (e.g. the lip of a ledge) down to the ground
			const glm::vec3 anchor = hitPos + hitNormal * 0.15f;
			glm::vec3 groundPos, groundNormal;
			real length = 30.0f;
			if (RaycastWorld(anchor - VEC3_UP * 0.1f, -VEC3_UP, 80.0f, playerBody, groundPos, groundNormal, nullptr))
			{
				length = anchor.y - groundPos.y - 0.3f;
			}
			outLength = glm::clamp(length, 1.5f, 80.0f);
			outPos = anchor;
			outRot = QUAT_IDENTITY;
			return true;
		}
		default:
			break;
		}

		BuildPiece* hitPiece = (hitObject != nullptr && hitObject->GetTypeID() == BuildPieceSID) ? (BuildPiece*)hitObject : nullptr;
		if (hitPiece == nullptr)
		{
			// Floors placed on uneven ground are partly buried, so the terrain (which the ray is also tested against)
			// often hides them, mostly near their edges. Treat a hit on the ground just above or below a floor as a hit
			// on the floor, so pieces line up with it instead of the terrain.
			for (BuildPiece* piece : BuildPiece::GetAllPieces())
			{
				if (piece->GetPieceType() != BuildPieceType::FLOOR)
				{
					continue;
				}
				const glm::vec3 piecePos = piece->GetTransform()->GetWorldPosition();
				const glm::vec3 local = glm::inverse(piece->GetTransform()->GetWorldRotation()) * (hitPos - piecePos);
				const real half = piece->GetSize() * 0.5f + 0.05f;
				if (glm::abs(local.x) <= half && glm::abs(local.z) <= half && local.y > -0.5f && local.y < 1.0f &&
					(hitPiece == nullptr || piecePos.y > hitPiece->GetTransform()->GetWorldPosition().y))
				{
					hitPiece = piece;
				}
			}
			if (hitPiece != nullptr)
			{
				hitPos.y = hitPiece->GetTransform()->GetWorldPosition().y + 0.2f;
				hitNormal = VEC3_UP;
			}
		}

		// Aiming at a piece of the same type: extend it out from the edge aimed nearest to
		if (hitPiece != nullptr && hitPiece->GetPieceType() == m_SelectedType && glm::abs(hitPiece->GetSize() - grid) < 0.01f)
		{
			BuildPlacement aimed;
			aimed.type = m_SelectedType;
			aimed.pos = hitPiece->GetTransform()->GetWorldPosition();
			aimed.rot = hitPiece->GetTransform()->GetWorldRotation();
			aimed.size = grid;
			const BuildPlacement extended = ExtendBuildPiece(aimed, hitPos);
			outPos = extended.pos;
			outRot = extended.rot;
			m_BlockReason = CheckGridPlacement(outPos, outRot);
			return m_BlockReason == BuildBlockReason::NONE;
		}

		// Grid pieces: cells are aligned to a world grid horizontally. Vertically they line up with the piece being
		// aimed at, or start at the terrain's height.
		const glm::vec3 p = hitPos + hitNormal * 0.1f;
		real levelY;
		// Cells line up with the world grid, or with the piece being aimed at when it's on a grid of the same size
		// that's offset from the world's (e.g. pieces placed by scripts)
		glm::vec2 gridOrigin(0.0f);
		if (hitPiece != nullptr)
		{
			const glm::vec3 piecePos = hitPiece->GetTransform()->GetWorldPosition();
			const real pieceGrid = hitPiece->GetSize();
			levelY = piecePos.y + glm::floor((p.y - piecePos.y) / pieceGrid + 0.001f) * pieceGrid;
			if (glm::abs(pieceGrid - grid) < 0.01f)
			{
				gridOrigin = glm::vec2(piecePos.x, piecePos.z) - glm::vec2(grid * 0.5f);
			}
		}
		else
		{
			levelY = glm::round(hitPos.y * 4.0f) / 4.0f;
			// Building next to existing pieces: line up with the nearest one's grid (it may be offset from the
			// world's), & with its level when the ground is about as high, so new pieces meet it instead of overlapping
			BuildPiece* nearest = nullptr;
			real nearestDistanceSq = grid * grid * 2.25f;
			for (BuildPiece* piece : BuildPiece::GetAllPieces())
			{
				if (!BuildPiece::IsGridPiece(piece->GetPieceType()) || glm::abs(piece->GetSize() - grid) > 0.01f)
				{
					continue;
				}
				const glm::vec3 piecePos = piece->GetTransform()->GetWorldPosition();
				const real distanceSq = glm::distance2(glm::vec2(piecePos.x, piecePos.z), glm::vec2(p.x, p.z));
				if (distanceSq < nearestDistanceSq && glm::abs(piecePos.y - hitPos.y) < grid)
				{
					nearest = piece;
					nearestDistanceSq = distanceSq;
				}
			}
			if (nearest != nullptr)
			{
				const glm::vec3 piecePos = nearest->GetTransform()->GetWorldPosition();
				gridOrigin = glm::vec2(piecePos.x, piecePos.z) - glm::vec2(grid * 0.5f);
				if (glm::abs(piecePos.y - hitPos.y) < 1.0f)
				{
					levelY = piecePos.y;
				}
			}
		}

		const glm::vec2 cell(glm::floor((p.x - gridOrigin.x) / grid), glm::floor((p.z - gridOrigin.y) / grid));
		const glm::vec3 cellCenter(gridOrigin.x + (cell.x + 0.5f) * grid, levelY, gridOrigin.y + (cell.y + 0.5f) * grid);

		real yaw;
		if (m_SelectedType == BuildPieceType::WALL)
		{
			// On the cell edge nearest the aimed point (walls are built along the cell's local +z edge). Rotating
			// doesn't apply: it used to move the wall to another edge than the one aimed at.
			const glm::vec2 local(p.x - cellCenter.x, p.z - cellCenter.z);
			if (glm::abs(local.x) > glm::abs(local.y))
			{
				yaw = local.x > 0.0f ? PI_DIV_TWO : -PI_DIV_TWO;
			}
			else
			{
				yaw = local.y > 0.0f ? 0.0f : PI;
			}
		}
		else
		{
			// Stairs & roofs rise away from the player
			yaw = std::atan2(heading.x, heading.z);
			yaw = glm::round(yaw / PI_DIV_TWO) * PI_DIV_TWO;
			yaw += (real)m_Rotation * PI_DIV_TWO;
		}

		outPos = cellCenter;
		outRot = glm::angleAxis(yaw, VEC3_UP);
		m_BlockReason = CheckGridPlacement(outPos, outRot);
		return m_BlockReason == BuildBlockReason::NONE;
	}

	BuildBlockReason BuildMode::CheckGridPlacement(const glm::vec3& pos, const glm::quat& rot)
	{
		BuildPlacement placement;
		placement.type = m_SelectedType;
		placement.pos = pos;
		placement.rot = rot;
		placement.size = BuildPiece::GRID_SIZE;

		for (BuildPiece* piece : BuildPiece::GetAllPieces())
		{
			BuildPlacement other;
			other.type = piece->GetPieceType();
			other.pos = piece->GetTransform()->GetWorldPosition();
			other.rot = piece->GetTransform()->GetWorldRotation();
			other.size = piece->GetSize();
			if (BuildPiecesOverlap(placement, other))
			{
				m_BlockingType = other.type;
				return BuildBlockReason::OVERLAPS_PIECE;
			}
		}

		// Pieces placed around the player would trap them (or push them through the ground)
		btVector3 playerMin, playerMax;
		m_Player->GetRigidBody()->GetRigidBodyInternal()->getAabb(playerMin, playerMax);
		if (BuildPieceOverlapsAABB(placement, ToVec3(playerMin), ToVec3(playerMax)))
		{
			return BuildBlockReason::OVERLAPS_PLAYER;
		}

		return BuildBlockReason::NONE;
	}

	void BuildMode::DrawTargetHighlight(BuildPiece* piece) const
	{
		DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();
		std::vector<BuildPiece::Box> boxes;
		BuildPiece::GetBoxes(piece->GetPieceType(), piece->GetLength(), piece->GetSize(), boxes);
		const glm::vec3 pos = piece->GetTransform()->GetWorldPosition();
		const glm::quat rot = piece->GetTransform()->GetWorldRotation();
		// Pulses so it reads as "selected" rather than as part of the piece
		const real pulse = 0.75f + 0.25f * glm::sin(AnimationTime() * 8.0f);
		const btVector3 colour(1.0f * pulse, 0.3f * pulse, 0.2f * pulse);
		for (const BuildPiece::Box& box : boxes)
		{
			const btTransform transform(ToBtQuaternion(rot * box.rotation), ToBtVec3(pos + rot * box.center));
			// A few nested outlines, since debug lines are only a pixel wide
			for (real inflate : { 0.02f, 0.04f, 0.06f })
			{
				const btVector3 extents = ToBtVec3(box.halfExtents + glm::vec3(inflate));
				debugRenderer->drawBox(-extents, extents, transform, colour);
			}
		}
	}

	void BuildMode::Update()
	{
		if (!m_bActive)
		{
			return;
		}

		m_Cooldown -= g_DeltaTime;

		const bool bCanUse = m_Player->AbleToInteract() && !m_Player->IsAnyInventoryShowing() && !g_EngineInstance->IsSimulationPaused();
		if (!bCanUse)
		{
			m_bPlacementValid = false;
			m_bPlacementHasPosition = false;
			m_BlockReason = BuildBlockReason::NONE;
			UpdatePreview(false);
			return;
		}

		// Choose the piece with the scroll wheel (or number keys), rotate with R & switch materials with N
		const real scroll = g_InputManager->GetVerticalScrollDistance();
		const i32 typeCount = (i32)BuildPieceType::_COUNT;
		if (scroll > 0.0f)
		{
			m_SelectedType = (BuildPieceType)(((i32)m_SelectedType + typeCount - 1) % typeCount);
		}
		else if (scroll < 0.0f)
		{
			m_SelectedType = (BuildPieceType)(((i32)m_SelectedType + 1) % typeCount);
		}
		for (i32 i = 0; i < typeCount; ++i)
		{
			if (g_InputManager->GetKeyPressed((KeyCode)((i32)KeyCode::KEY_1 + i)))
			{
				m_SelectedType = (BuildPieceType)i;
			}
		}
		if (g_InputManager->GetKeyPressed(GameKeys::BUILD_ROTATE))
		{
			m_Rotation = (m_Rotation + 1) % 4;
		}
		if (g_InputManager->GetKeyPressed(GameKeys::BUILD_MATERIAL))
		{
			m_SelectedMaterial = (BuildMaterial)(((i32)m_SelectedMaterial + 1) % (i32)BuildMaterial::_COUNT);
		}

		m_bPlacementValid = ComputePlacement(m_PlacementPos, m_PlacementRot, m_PlacementLength, m_bPlacementHasPosition);
		if (m_bPlacementHasPosition && (m_PlacementPos.y < terrain::WORLD_MIN_HEIGHT || m_PlacementPos.y > terrain::WORLD_MAX_HEIGHT))
		{
			m_bPlacementValid = false;
			m_BlockReason = BuildBlockReason::HEIGHT_LIMIT;
		}
		// Ropes are tossed rather than placed (see Traversal::TossRope)
		const bool bTossing = m_SelectedType == BuildPieceType::ROPE;
		UpdatePreview(m_bPlacementHasPosition && !bTossing);

		BuildPiece* targeted = GetTargetedPiece();
		if (targeted != nullptr)
		{
			DrawTargetHighlight(targeted);
		}

		if (m_Cooldown <= 0.0f)
		{
			if (g_InputManager->IsMouseButtonPressed(MouseButton::LEFT) && bTossing)
			{
				m_Player->GetTraversal()->TossRope();
				m_Cooldown = 0.3f;
			}
			else if (g_InputManager->IsMouseButtonPressed(MouseButton::LEFT) && m_bPlacementValid)
			{
				Place();
				m_Cooldown = 0.15f;
			}
			else if (g_InputManager->IsMouseButtonPressed(MouseButton::LEFT) && !m_bPlacementValid)
			{
				GameSounds::PlayBuildDenied();
				m_Cooldown = 0.25f;
			}
			else if (g_InputManager->IsMouseButtonPressed(MouseButton::RIGHT) && targeted != nullptr)
			{
				g_SceneManager->CurrentScene()->RemoveObject(targeted, true);
				AudioManager::PlaySource(m_RemoveSoundID);
				m_Cooldown = 0.15f;
			}
		}
	}

	void BuildMode::Place()
	{
		BaseScene* scene = g_SceneManager->CurrentScene();
		BuildPiece* piece = new BuildPiece(scene->GetUniqueObjectName(std::string(BuildPieceTypeName(m_SelectedType)) + "_", 3));
		piece->Setup(m_SelectedType, m_SelectedMaterial, m_PlacementLength);
		piece->GetTransform()->SetWorldPosition(m_PlacementPos);
		piece->GetTransform()->SetWorldRotation(m_PlacementRot);
		scene->AddRootObject(piece);
		AudioManager::PlaySource(m_PlaceSoundID);
		if (WorldStats* stats = GetCurrentWorldStats())
		{
			stats->piecesBuilt++;
			stats->torchesPlaced += m_SelectedType == BuildPieceType::TORCH ? 1 : 0;
		}
		if (m_SelectedType == BuildPieceType::TORCH)
		{
			GameSounds::PlayTorchIgnite(m_PlacementPos);
		}
	}

	void BuildMode::RemoveTargeted()
	{
		BuildPiece* targeted = GetTargetedPiece();
		if (targeted != nullptr)
		{
			g_SceneManager->CurrentScene()->RemoveObject(targeted, true);
		}
	}

	void BuildMode::DrawHUD() const
	{
		if (!m_bActive)
		{
			return;
		}

		BitmapFont* font = g_Renderer->SetFont(SID("editor-02"));
		const real lineHeight = 3.2f * font->GetMetric('W')->height / (real)g_Window->GetSize().y;

		// Piece list along the bottom, the selected one highlighted
		std::string pieces;
		for (i32 i = 0; i < (i32)BuildPieceType::_COUNT; ++i)
		{
			const bool bSelected = i == (i32)m_SelectedType;
			pieces += bSelected ? "[" : " ";
			pieces += IntToString(i + 1) + " " + BuildPieceDisplayName((BuildPieceType)i);
			pieces += bSelected ? "]" : " ";
			pieces += " ";
		}

		const bool bUsesMaterial = m_SelectedType == BuildPieceType::FLOOR || m_SelectedType == BuildPieceType::WALL ||
			m_SelectedType == BuildPieceType::ROOF || m_SelectedType == BuildPieceType::STAIRS;
		std::vector<std::string> lines;
		// Why the piece can't go where it's aimed (shown above the title)
		std::string blocked;
		if (!m_bPlacementValid && m_SelectedType != BuildPieceType::ROPE)
		{
			switch (m_BlockReason)
			{
			case BuildBlockReason::NO_TARGET: blocked = LOC(Build_Blocked_NoTarget); break;
			case BuildBlockReason::OVERLAPS_PIECE: blocked = LOCF(Build_Blocked_Piece, m_BlockingType < BuildPieceType::_COUNT ? BuildPieceDisplayName(m_BlockingType) : ""); break;
			case BuildBlockReason::OVERLAPS_PLAYER: blocked = LOC(Build_Blocked_Player); break;
			case BuildBlockReason::CEILING: blocked = LOC(Build_Blocked_Ceiling); break;
			case BuildBlockReason::HEIGHT_LIMIT: blocked = LOC(Build_Blocked_HeightLimit); break;
			default: break;
			}
		}
		lines.push_back(bUsesMaterial ? LOCF(Build_TitleMaterial, BuildPieceDisplayName(m_SelectedType), BuildMaterialDisplayName(m_SelectedMaterial)) : LOCF(Build_Title, BuildPieceDisplayName(m_SelectedType)));
		lines.push_back(pieces);
		lines.push_back(LOC(Build_Controls));

		const glm::vec4 titleColour(1.0f, 0.85f, 0.4f, 1.0f);
		const glm::vec4 textColour(0.95f, 0.95f, 0.95f, 1.0f);
		real yOffset = 0.18f;
		for (i32 i = (i32)lines.size() - 1; i >= 0; --i)
		{
			g_Renderer->DrawStringSS(lines[i], i == 0 ? titleColour : textColour, AnchorPoint::BOTTOM, glm::vec2(0.0f, yOffset), 1.5f, 0.6f);
			yOffset += lineHeight;
		}
		if (!blocked.empty())
		{
			g_Renderer->DrawStringSS(blocked, glm::vec4(1.0f, 0.45f, 0.35f, 1.0f), AnchorPoint::BOTTOM, glm::vec2(0.0f, yOffset), 1.5f, 0.6f);
		}
	}
} // namespace flex
