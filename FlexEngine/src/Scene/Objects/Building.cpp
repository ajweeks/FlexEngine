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
#include "Graphics/BitmapFont.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "InputManager.hpp"
#include "JSONTypes.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	std::vector<BuildPiece*> BuildPiece::s_Pieces;

	namespace
	{
		const char* s_PieceTypeNames[] = { "Floor", "Wall", "Roof", "Stairs", "Torch", "Ladder", "Rope" };
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
				outSecondary = GetOrCreateMaterial("Build torch flame", glm::vec4(1.0f, 0.6f, 0.2f, 1.0f), 1.0f, glm::vec4(1.0f, 0.55f, 0.15f, 1.0f) * 6.0f);
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
			// Wound so the face points along its normal
			indices.insert(indices.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
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

	BuildPiece::BuildPiece(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /* = false */) :
		GameObject(name, BuildPieceSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
	}

	PropertyCollection* BuildPiece::BuildTypeUniquePropertyCollection()
	{
		return GameObject::BuildPropertyCollection();
	}

	void BuildPiece::Setup(BuildPieceType type, BuildMaterial material, real length)
	{
		m_PieceType = type;
		m_Material = material;
		m_Length = length;
	}

	void BuildPiece::GetBoxes(BuildPieceType type, real length, std::vector<Box>& outBoxes)
	{
		const real half = GRID_SIZE * 0.5f;
		const glm::quat slope = glm::angleAxis(glm::radians(-45.0f), VEC3_RIGHT);
		switch (type)
		{
		case BuildPieceType::FLOOR:
			outBoxes.push_back({ glm::vec3(0.0f, 0.1f, 0.0f), glm::vec3(half, 0.1f, half), QUAT_IDENTITY, 0 });
			// Beams along two edges
			outBoxes.push_back({ glm::vec3(0.0f, 0.1f, half - 0.08f), glm::vec3(half, 0.12f, 0.08f), QUAT_IDENTITY, 1 });
			outBoxes.push_back({ glm::vec3(0.0f, 0.1f, -half + 0.08f), glm::vec3(half, 0.12f, 0.08f), QUAT_IDENTITY, 1 });
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
			// Six steps rising towards +z (walked on as a ramp, see CreateCollisionShape)
			const u32 stepCount = 6;
			const real stepDepth = GRID_SIZE / (real)stepCount;
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
		default:
			break;
		}
	}

	btCollisionShape* BuildPiece::CreateCollisionShape(BuildPieceType type, real length)
	{
		FLEX_UNUSED(length);

		std::vector<Box> boxes;
		switch (type)
		{
		case BuildPieceType::FLOOR:
		case BuildPieceType::WALL:
		case BuildPieceType::ROOF:
			GetBoxes(type, length, boxes);
			break;
		case BuildPieceType::STAIRS:
		{
			// A smooth ramp under the step edges, which is easy to walk up
			const real half = GRID_SIZE * 0.5f;
			boxes.push_back({ glm::vec3(0.0f, half - 0.12f, 0.0f), glm::vec3(half * 0.8f, 0.08f, half * 1.4142f), glm::angleAxis(glm::radians(-45.0f), VEC3_RIGHT), 0 });
			// Fill underneath so it can't be walked through from the side
			boxes.push_back({ glm::vec3(0.0f, half * 0.5f, half * 0.5f), glm::vec3(half * 0.8f, half * 0.5f, half * 0.5f), QUAT_IDENTITY, 0 });
		} break;
		default:
			// Torches, ladders & ropes don't block movement
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

	void BuildPiece::GetOutline(BuildPieceType type, real length, std::vector<std::pair<glm::vec3, glm::vec3>>& outLines)
	{
		std::vector<Box> boxes;
		GetBoxes(type, length, boxes);
		// Only the first (largest) few boxes are outlined to keep the preview readable
		const u32 boxCount = glm::min((u32)boxes.size(), type == BuildPieceType::STAIRS ? 6u : 3u);
		for (u32 b = 0; b < boxCount; ++b)
		{
			const Box& box = boxes[b];
			glm::vec3 corners[8];
			for (u32 i = 0; i < 8; ++i)
			{
				const glm::vec3 sign((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f);
				corners[i] = box.center + box.rotation * (sign * box.halfExtents);
			}
			for (u32 i = 0; i < 8; ++i)
			{
				for (u32 bit = 1; bit < 8; bit <<= 1)
				{
					if ((i & bit) == 0)
					{
						outLines.emplace_back(corners[i], corners[i | bit]);
					}
				}
			}
		}
	}

	void BuildPiece::CreateMesh()
	{
		MaterialID mainMaterialID, secondaryMaterialID;
		GetPieceMaterials(m_PieceType, m_Material, mainMaterialID, secondaryMaterialID);

		std::vector<Box> boxes;
		GetBoxes(m_PieceType, m_Length, boxes);

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
			btCollisionShape* shape = CreateCollisionShape(m_PieceType, m_Length);
			if (shape != nullptr)
			{
				SetCollisionShape(shape);
				RigidBody* rigidBody = SetRigidBody(new RigidBody((u32)CollisionType::STATIC, (u32)CollisionType::DEFAULT & ~(u32)CollisionType::STATIC));
				rigidBody->SetStatic(true);
			}
		}

		s_Pieces.push_back(this);

		GameObject::Initialize();
	}

	void BuildPiece::Destroy(bool bDetachFromParent /* = true */)
	{
		s_Pieces.erase(std::remove(s_Pieces.begin(), s_Pieces.end(), this), s_Pieces.end());

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

		const real time = g_SecElapsedSinceProgramStart;
		for (u32 i = 0; i < MAX_TORCH_LIGHTS; ++i)
		{
			TorchLight& light = s_TorchLights[i];
			if (i < (u32)torches.size())
			{
				BuildPiece* torch = torches[i].second;
				const glm::vec3 flamePos = torch->m_Transform.GetWorldPosition() + torch->m_Transform.GetWorldRotation() * (FLAME_OFFSET + glm::vec3(0.0f, 0.15f, 0.0f));
				// Flicker, offset per torch so they don't pulse in unison
				const real phase = flamePos.x * 1.7f + flamePos.z * 2.3f;
				const real flicker = 0.85f + 0.08f * std::sin(time * 9.0f + phase) + 0.07f * std::sin(time * 23.0f + phase * 1.3f);
				PointLightData data = {};
				data.pos = flamePos;
				data.enabled = 1;
				data.colour = glm::vec3(1.0f, 0.62f, 0.32f);
				// Fade out with distance so lights being reassigned isn't noticeable
				data.brightness = 45.0f * flicker * (1.0f - glm::smoothstep(TORCH_LIGHT_RANGE * 0.7f, TORCH_LIGHT_RANGE, glm::sqrt(torches[i].first)));
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
		return m_PieceType == BuildPieceType::ROPE ? pos - VEC3_UP * m_Length : pos;
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
		return glm::vec3(bottom.x, glm::clamp(pos.y, bottom.y, top.y), bottom.z);
	}

	glm::vec3 BuildPiece::GetClimbFacing() const
	{
		if (m_PieceType == BuildPieceType::LADDER)
		{
			return m_Transform.GetWorldRotation() * -VEC3_FORWARD;
		}
		return VEC3_ZERO;
	}

	BuildPiece* BuildPiece::FindClimbable(const glm::vec3& pos, real maxDistance)
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
			if (pos.y < bottom.y - 0.5f || pos.y > top.y + 1.2f)
			{
				continue;
			}
			const real distance = glm::length(glm::vec2(pos.x - bottom.x, pos.z - bottom.z));
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
		parentObject.fields.emplace_back("build piece", JSONValue(pieceObj));
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
		m_bActive = bActive;
		m_bPlacementValid = false;
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

	bool BuildMode::ComputePlacement(glm::vec3& outPos, glm::quat& outRot, real& outLength) const
	{
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
			return hitNormal.y > -0.5f;
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

		// Grid pieces: cells are aligned to a world grid horizontally. Vertically they line up with the piece being
		// aimed at, or start at the terrain's height.
		const glm::vec3 p = hitPos + hitNormal * 0.1f;
		real levelY;
		if (hitObject != nullptr && hitObject->GetTypeID() == BuildPieceSID)
		{
			const real pieceY = hitObject->GetTransform()->GetWorldPosition().y;
			levelY = pieceY + glm::floor((p.y - pieceY) / grid + 0.001f) * grid;
		}
		else
		{
			levelY = glm::round(hitPos.y * 4.0f) / 4.0f;
		}

		const glm::vec2 cell(glm::floor(p.x / grid), glm::floor(p.z / grid));
		const glm::vec3 cellCenter((cell.x + 0.5f) * grid, levelY, (cell.y + 0.5f) * grid);

		real yaw;
		if (m_SelectedType == BuildPieceType::WALL)
		{
			// On the cell edge nearest the aimed point (walls are built along the cell's local +z edge)
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
		}
		yaw += (real)m_Rotation * PI_DIV_TWO;

		outPos = cellCenter;
		outRot = glm::angleAxis(yaw, VEC3_UP);

		// Already occupied: stack on top instead
		for (u32 attempt = 0; attempt < 4; ++attempt)
		{
			bool bOccupied = false;
			for (BuildPiece* piece : BuildPiece::GetAllPieces())
			{
				if (piece->GetPieceType() == m_SelectedType &&
					glm::distance(piece->GetTransform()->GetWorldPosition(), outPos) < 0.1f &&
					glm::abs(glm::dot(piece->GetTransform()->GetWorldRotation(), outRot)) > 0.99f)
				{
					bOccupied = true;
					break;
				}
			}
			if (!bOccupied)
			{
				return true;
			}
			outPos.y += grid;
		}
		return false;
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
		if (g_InputManager->GetKeyPressed(KeyCode::KEY_R))
		{
			m_Rotation = (m_Rotation + 1) % 4;
		}
		if (g_InputManager->GetKeyPressed(KeyCode::KEY_N))
		{
			m_SelectedMaterial = (BuildMaterial)(((i32)m_SelectedMaterial + 1) % (i32)BuildMaterial::_COUNT);
		}

		m_bPlacementValid = ComputePlacement(m_PlacementPos, m_PlacementRot, m_PlacementLength);

		DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();
		if (m_bPlacementValid)
		{
			std::vector<std::pair<glm::vec3, glm::vec3>> lines;
			BuildPiece::GetOutline(m_SelectedType, m_PlacementLength, lines);
			const btVector3 colour(0.35f, 1.0f, 0.5f);
			for (const auto& line : lines)
			{
				debugRenderer->drawLine(ToBtVec3(m_PlacementPos + m_PlacementRot * line.first), ToBtVec3(m_PlacementPos + m_PlacementRot * line.second), colour);
			}
		}

		BuildPiece* targeted = GetTargetedPiece();
		if (targeted != nullptr)
		{
			const glm::vec3 pos = targeted->GetTransform()->GetWorldPosition();
			debugRenderer->drawSphere(ToBtVec3(pos), 0.15f, btVector3(1.0f, 0.4f, 0.3f));
		}

		if (m_Cooldown <= 0.0f)
		{
			if (g_InputManager->IsMouseButtonPressed(MouseButton::LEFT) && m_bPlacementValid)
			{
				Place();
				m_Cooldown = 0.15f;
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
			pieces += IntToString(i + 1) + " " + BuildPieceTypeName((BuildPieceType)i);
			pieces += bSelected ? "]" : " ";
			pieces += " ";
		}

		const bool bUsesMaterial = m_SelectedType == BuildPieceType::FLOOR || m_SelectedType == BuildPieceType::WALL ||
			m_SelectedType == BuildPieceType::ROOF || m_SelectedType == BuildPieceType::STAIRS;
		std::vector<std::string> lines;
		lines.push_back(std::string("BUILDING: ") + BuildPieceTypeName(m_SelectedType) + (bUsesMaterial ? std::string(" (") + BuildMaterialName(m_SelectedMaterial) + ")" : ""));
		lines.push_back(pieces);
		lines.push_back("[LMB] Place   [RMB] Remove   [Scroll/1-7] Piece   [R] Rotate   [N] Material   [B] Exit");

		const glm::vec4 titleColour(1.0f, 0.85f, 0.4f, 1.0f);
		const glm::vec4 textColour(0.95f, 0.95f, 0.95f, 1.0f);
		real yOffset = 0.18f;
		for (i32 i = (i32)lines.size() - 1; i >= 0; --i)
		{
			g_Renderer->DrawStringSS(lines[i], i == 0 ? titleColour : textColour, AnchorPoint::BOTTOM, glm::vec2(0.0f, yOffset), 1.5f, 0.6f);
			yOffset += lineHeight;
		}
	}
} // namespace flex
