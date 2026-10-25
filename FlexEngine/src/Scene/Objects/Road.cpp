#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btTriangleIndexVertexArray.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btCollisionShape.h>

#include <LinearMath/btIDebugDraw.h>

#include <glm/gtx/norm.hpp> // for distance2
#include <glm/gtx/quaternion.hpp> // for rotate
#include <glm/gtx/vector_angle.hpp> // for angle
#include <glm/ext/matrix_transform.hpp> // for rotate

#include <imgui/imgui_internal.h> // For PushItemFlag

IGNORE_WARNINGS_POP

#include "Scene/Objects/Road.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Physics/RigidBody.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	bool RoadSegment::Overlaps(const glm::vec2& point)
	{
		VertexBufferData* vertexBufferData = mesh->GetVertexBufferData();
		u32* indexBuffer = mesh->GetIndexBufferUnsafePtr();
		u32 indexCount = mesh->GetIndexCount();
		u8* vertexData = (u8*)vertexBufferData->vertexData;
		u32 vertexStride = vertexBufferData->VertexStride;

		for (u32 i = 0; i < indexCount - 2; i += 3)
		{
			glm::vec3 tri0(*(glm::vec3*)(vertexData + vertexStride * indexBuffer[i + 0]));
			glm::vec3 tri1(*(glm::vec3*)(vertexData + vertexStride * indexBuffer[i + 1]));
			glm::vec3 tri2(*(glm::vec3*)(vertexData + vertexStride * indexBuffer[i + 2]));

			if (PointOverlapsTriangle(point, glm::vec2(tri0.x, tri0.z), glm::vec2(tri1.x, tri1.z), glm::vec2(tri2.x, tri2.z)))
			{
				return true;
			}
		}

		return false;
	}

	real RoadSegment::SignedDistanceTo(const glm::vec3& point, glm::vec3& outClosestPoint)
	{
		VertexBufferData* vertexBufferData = mesh->GetVertexBufferData();
		u32* indexBuffer = mesh->GetIndexBufferUnsafePtr();
		u32 indexCount = mesh->GetIndexCount();
		u8* vertexData = (u8*)vertexBufferData->vertexData;
		u32 vertexStride = vertexBufferData->VertexStride;

		glm::vec3 closestPoint;
		real dist = 99999.0f;
		for (u32 i = 0; i < indexCount - 2; i += 3)
		{
			glm::vec3 a(*(glm::vec3*)(vertexData + vertexStride * indexBuffer[i + 0]));
			glm::vec3 b(*(glm::vec3*)(vertexData + vertexStride * indexBuffer[i + 1]));
			glm::vec3 c(*(glm::vec3*)(vertexData + vertexStride * indexBuffer[i + 2]));

			real d = SignedDistanceToTriangle(point, a, b, c, closestPoint);
			if (d < dist)
			{
				dist = d;
				outClosestPoint = closestPoint;
			}
		}

		return dist;
	}

	void RoadSegment::ComputeAABB()
	{
		aabb.minX = aabb.minY = aabb.minZ = 99999.0f;
		aabb.maxX = aabb.maxY = aabb.maxZ = -99999.0f;
		const u32 sampleCount = 10;
		real maxWidth = glm::max(widthStart, widthEnd);
		for (u32 i = 0; i < sampleCount; ++i)
		{
			real t = ((real)i / (sampleCount - 1));
			glm::vec3 point = curve.GetPointOnCurve(t);
			aabb.minX = glm::min(aabb.minX, point.x - maxWidth);
			aabb.minY = glm::min(aabb.minY, point.y - maxWidth);
			aabb.minZ = glm::min(aabb.minZ, point.z - maxWidth);
			aabb.maxX = glm::max(aabb.maxX, point.x + maxWidth);
			aabb.maxY = glm::max(aabb.maxY, point.y + maxWidth);
			aabb.maxZ = glm::max(aabb.maxZ, point.z + maxWidth);
		}
	}

	Road::Road(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, RoadSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		AddTag("road");
	}

	PropertyCollection* Road::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();
		return collection;
	}

	void Road::Initialize()
	{
		GameObject::Initialize();

		m_Mesh = new Mesh(this);
		m_Mesh->SetTypeToMemory();

		m_QuadCountPerSegment = 25;

		GetSystem<RoadManager>(SystemType::ROAD_MANAGER)->RegisterRoad(this);
	}

	void Road::PostInitialize()
	{
		GenerateMesh();
	}

	void Road::Destroy(bool bDetachFromParent /* = true */)
	{
		GetSystem<RoadManager>(SystemType::ROAD_MANAGER)->DeregisterRoad(this);

		GameObject::Destroy(bDetachFromParent);

		// Mesh components will have already been destroyed by Mesh Destroy
		roadSegments.clear();

		for (u32 i = 0; i < (u32)m_RigidBodies.size(); ++i)
		{
			m_RigidBodies[i]->Destroy();
			delete m_RigidBodies[i];
			delete m_MeshVertexArrays[i];
		}
		m_RigidBodies.clear();
		m_MeshVertexArrays.clear();
	}

	void Road::Update()
	{
		PROFILE_AUTO("Road Update");

		GameObject::Update();

		const PhysicsDebuggingSettings& debuggingSettings = g_Renderer->GetPhysicsDebuggingSettings();
		if (!debuggingSettings.bDisableAll &&
			g_Renderer->GetDebugRenderer()->getDebugMode() != btIDebugDraw::DebugDrawModes::DBG_NoDebug)
		{
			for (u32 i = 0; i < (u32)roadSegments.size(); ++i)
			{
				RoadSegment& segment = roadSegments[i];

				// TOOD: move curve segment by our transform/offset it
				segment.curve.DrawDebug(false, btVector4(0.0f, 1.0f, 0.0f, 1.0f), btVector4(1.0f, 1.0f, 1.0f, 1.0f));

				if (debuggingSettings.bDrawAabb)
				{
					segment.aabb.DrawDebug(btVector3(1.0f, 0.5f, 0.0f));
				}
			}
		}
	}

	void Road::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		if (ImGuiExt::DragUInt("Quad count per segment", &m_QuadCountPerSegment, 0.1f, 1u, 32u))
		{
			for (u32 i = 0; i < (u32)roadSegments.size(); ++i)
			{
				GenerateSegment(i);
			}
		}
	}

	void Road::GenerateSegmentsThroughPoints(const std::vector<glm::vec3>& points)
	{
		const i32 startingCurveCount = (i32)roadSegments.size();

		roadSegments.reserve(roadSegments.size() + points.size());

		// First pass: set control points without yet setting handle points
		{
			RoadSegment segment = {};
			segment.widthStart = m_MaxWidth;
			segment.widthEnd = m_MaxWidth;
			for (u32 i = 1; i < (u32)points.size(); ++i)
			{
				segment.curve = BezierCurve3D(points[i - 1], VEC3_ZERO, VEC3_ZERO, points[i]);
				roadSegments.push_back(segment);
			}
		}

		// Second pass: generate tangents so segments are continuous
		{
			real tangentScale = 0.25f;

			for (u32 i = 1; i < (u32)points.size() - 1; ++i)
			{
				i32 prevPointIndex = i - 1;
				RoadSegment& prevSeg = roadSegments[startingCurveCount + prevPointIndex];
				RoadSegment& currSeg = roadSegments[startingCurveCount + i];

				glm::vec3 pointDiff = (currSeg.curve.points[3] - prevSeg.curve.points[0]) * tangentScale;

				prevSeg.curve.points[2] = prevSeg.curve.points[3] - pointDiff;
				currSeg.curve.points[1] = currSeg.curve.points[0] + pointDiff;
			}

			// Set final one (or two if looping) tangent point(s)
			i32 finalSegmentIndex = (i32)(startingCurveCount + points.size() - 2);
			if (NearlyEquals(roadSegments[finalSegmentIndex].curve.points[3], roadSegments[0].curve.points[0], 0.1f))
			{
				// Loop
				glm::vec3 tangent = (roadSegments[0].curve.points[3] - roadSegments[finalSegmentIndex].curve.points[0]) * tangentScale;
				roadSegments[0].curve.points[1] = roadSegments[0].curve.points[0] + tangent;
				roadSegments[finalSegmentIndex].curve.points[2] = roadSegments[finalSegmentIndex].curve.points[3] - tangent;
			}
			else
			{
				roadSegments[finalSegmentIndex].curve.points[2] = roadSegments[finalSegmentIndex].curve.points[3] - roadSegments[finalSegmentIndex].curve.points[0];
			}
		}

		// Third pass: actually generate meshes
		for (u32 i = 0; i < (u32)points.size() - 1; ++i)
		{
			GenerateSegment(startingCurveCount + i);
		}
	}

	void Road::GenerateSegment(i32 index)
	{
		if (index >= (i32)roadSegments.size())
		{
			PrintError("Attempted to generate road segment without curve defined!\n");
			return;
		}

		PROFILE_AUTO("Generate road segment");

		if (index < (i32)roadSegments.size() && roadSegments[index].mesh != nullptr)
		{
			roadSegments[index].mesh->Destroy();
			roadSegments[index].mesh = nullptr;
		}

		if (index >= (i32)roadSegments.size())
		{
			roadSegments.resize(index + 1);
		}

		GenerateMaterial();

		Material* roadMaterial = g_Renderer->GetMaterial(m_RoadMaterialID);
		Shader* roadShader = g_Renderer->GetShader(roadMaterial->shaderID);

		RoadSegment& segment = roadSegments[index];
		BezierCurve3D& curve = segment.curve;

		segment.ComputeAABB();

		//if (roadSegments.size() >= terrainShader->maxObjectCount - 1)
		//{
		//	terrainShader->maxObjectCount = (u32)(roadSegments.size() + 1);
		//	g_Renderer->SetStaticGeometryBufferDirty(terrainShader->staticVertexBufferIndex);
		//}

		const u32 vertexCount = m_QuadCountPerSegment * 2 + 2;
		const u32 triCount = m_QuadCountPerSegment * 2;
		const u32 indexCount = triCount * 3;

		VertexBufferDataCreateInfo vertexBufferCreateInfo = {};
		vertexBufferCreateInfo.positions_3D.clear();
		vertexBufferCreateInfo.texCoords_UV.clear();
		vertexBufferCreateInfo.colours_R32G32B32A32.clear();
		vertexBufferCreateInfo.normals.clear();
		vertexBufferCreateInfo.tangents.clear();

		vertexBufferCreateInfo.attributes = roadShader->vertexAttributes;
		vertexBufferCreateInfo.positions_3D.reserve(vertexCount);
		vertexBufferCreateInfo.texCoords_UV.reserve(vertexCount);
		vertexBufferCreateInfo.colours_R32G32B32A32.reserve(vertexCount);
		vertexBufferCreateInfo.normals.reserve(vertexCount);
		vertexBufferCreateInfo.tangents.reserve(vertexCount);

		std::vector<u32> indices(indexCount);

		for (u32 z = 0; z < m_QuadCountPerSegment + 1; ++z)
		{
			real t = (real)z / m_QuadCountPerSegment;
			real width = Lerp(segment.widthStart, segment.widthEnd, t);
			glm::vec3 pos = curve.GetPointOnCurve(t);

			glm::vec3 posF = curve.GetPointOnCurveUnclamped(t + 0.01f);
			glm::vec3 posB = curve.GetPointOnCurveUnclamped(t - 0.01f);
			glm::vec3 normal = glm::normalize(glm::vec3(posF.x - posB.x, 2.0f, posF.z - posB.z));

			glm::vec3 forward = glm::normalize(curve.GetFirstDerivativeOnCurve(t));
			glm::vec3 tangent = glm::cross(forward, normal);

			real xScales[] = { -width, width };
			for (u32 x = 0; x < 2; ++x)
			{
				glm::vec3 vertPosWS = pos + xScales[x] * tangent;
				glm::vec2 uv((real)x, t);

				vertexBufferCreateInfo.positions_3D.emplace_back(vertPosWS);
				vertexBufferCreateInfo.texCoords_UV.emplace_back(uv);
				vertexBufferCreateInfo.colours_R32G32B32A32.emplace_back(VEC4_ONE);
				vertexBufferCreateInfo.normals.emplace_back(normal);
				vertexBufferCreateInfo.tangents.emplace_back(tangent);
			}
		}

		i32 i = 0;
		for (u32 z = 0; z < m_QuadCountPerSegment; ++z)
		{
			u32 vertIdx = z * 6;
			indices[vertIdx + 0] = i + 0;
			indices[vertIdx + 1] = i + 1;
			indices[vertIdx + 2] = i + 3;

			indices[vertIdx + 3] = i + 0;
			indices[vertIdx + 4] = i + 3;
			indices[vertIdx + 5] = i + 2;

			i += 2;
		}

		RenderObjectCreateInfo renderObjectCreateInfo = {};
		MeshComponent* meshComponent = MeshComponent::LoadFromMemory(m_Mesh, vertexBufferCreateInfo, indices, m_RoadMaterialID, &renderObjectCreateInfo);
		if (meshComponent != nullptr)
		{
			CHECK_EQ(roadSegments[index].mesh, nullptr);
			roadSegments[index].mesh = meshComponent;
		}

		CreateRigidBody(index);
	}

	GameObject* Road::CopySelf(GameObject* parent, CopyFlags copyFlags, std::string* optionalName, const GameObjectID& optionalGameObjectID)
	{
		FLEX_UNUSED(copyFlags);

		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		Road* newGameObject = new Road(newObjectName, newGameObjectID);

		CopyGenericFields(newGameObject, parent, CopyFlags::ADD_TO_SCENE);

		newGameObject->roadSegments = roadSegments;
		newGameObject->m_RoadMaterialID = m_RoadMaterialID;
		newGameObject->m_QuadCountPerSegment = m_QuadCountPerSegment;

		return newGameObject;
	}

	void Road::RegenerateMesh()
	{
		m_Mesh->DestroyAllSubmeshes();

		roadSegments.clear();
		GenerateMesh();
	}

	void Road::GenerateMesh()
	{
		if (roadSegments.empty())
		{
			TerrainGenerator* terrainGenerator = (TerrainGenerator*)m_TerrainGameObjectID.Get();
			if (terrainGenerator == nullptr)
			{
				m_TerrainGameObjectID = g_SceneManager->CurrentScene()->FirstObjectWithTag("terrain");
				terrainGenerator = (TerrainGenerator*)m_TerrainGameObjectID.Get();
			}

			std::vector<glm::vec3> newPoints =
			{
				glm::vec3(4.0f, 0.0f, 0.0f),
				glm::vec3(9.0f, 0.0f, 64.0f),
				glm::vec3(60.0f, 0.0f, 84.0f),
				glm::vec3(96.0f, 0.0f, 72.0f),
				glm::vec3(104.0f, 0.0f, 32.0f),
				glm::vec3(64.0f, 0.0f, -24.0f),
				glm::vec3(4.0f, 0.0f, 0.0f)
			};

			if (terrainGenerator != nullptr)
			{
				real offset = 0.5f;

				for (u32 i = 0; i < (u32)newPoints.size(); ++i)
				{
					newPoints[i].y = terrainGenerator->SampleHeight(glm::vec2(newPoints[i].x, newPoints[i].z)) + offset;
				}
			}

			GenerateSegmentsThroughPoints(newPoints);

			if (terrainGenerator != nullptr)
			{
				terrainGenerator->UpdateRoadSegments();
			}
		}
		else
		{
			for (u32 i = 0; i < (u32)roadSegments.size(); ++i)
			{
				GenerateSegment(i);
			}
		}
	}

	void Road::GenerateMaterial()
	{
		m_RoadMaterialID = g_Renderer->GetMaterialID("Road surface");
		if (m_RoadMaterialID == InvalidMaterialID)
		{
			// TODO: Add serialized member which points at material ID once support for that is in
			MaterialCreateInfo matCreateInfo = {};
			matCreateInfo.name = "Road surface";
			matCreateInfo.shaderName = "pbr";
			matCreateInfo.bDynamic = false;
			matCreateInfo.constRoughness = 0.95f;
			matCreateInfo.constMetallic = 0.0f;
			matCreateInfo.constAlbedo = glm::vec4(0.25f, 0.25f, 0.28f, 1.0f);
			matCreateInfo.albedoTexturePath = TEXTURE_DIRECTORY "road-albedo.png";
			matCreateInfo.enableAlbedoSampler = true;
			matCreateInfo.bSerializable = false;

			m_RoadMaterialID = g_Renderer->InitializeMaterial(&matCreateInfo, m_RoadMaterialID);
		}
	}

	void Road::CreateRigidBody(u32 meshIndex)
	{
		if (roadSegments.empty())
		{
			return;
		}

		if (meshIndex < (u32)m_RigidBodies.size() && m_RigidBodies[meshIndex] != nullptr)
		{
			m_RigidBodies[meshIndex]->Destroy();
			delete m_RigidBodies[meshIndex];
			m_RigidBodies[meshIndex] = nullptr;
		}

		if (meshIndex >= (u32)m_RigidBodies.size())
		{
			m_RigidBodies.resize(meshIndex + 1, nullptr);
			m_MeshVertexArrays.resize(meshIndex + 1, nullptr);
		}

		MeshComponent* submesh = roadSegments[meshIndex].mesh;

		btBvhTriangleMeshShape* shape;
		submesh->CreateCollisionMesh(&m_MeshVertexArrays[meshIndex], &shape);

		// TODO: Don't even create rb?
		RigidBody* rigidBody = new RigidBody((u32)CollisionType::STATIC, (u32)CollisionType::DEFAULT & ~(u32)CollisionType::STATIC);
		rigidBody->SetStatic(true);
		rigidBody->Initialize(shape, &m_Transform);
		m_RigidBodies[meshIndex] = rigidBody;
	}
} // namespace flex
