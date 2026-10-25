#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH

#include <LinearMath/btIDebugDraw.h>

#include <glm/gtx/norm.hpp> // for distance2
#include <glm/gtx/quaternion.hpp> // for rotate
#include <glm/gtx/vector_angle.hpp> // for angle
#include <glm/ext/matrix_transform.hpp> // for rotate

#include <imgui/imgui_internal.h> // For PushItemFlag

IGNORE_WARNINGS_POP

#include "Scene/Objects/SoftBody.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "PropertyCollection.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/LoadedMesh.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	ms SoftBody::FIXED_UPDATE_TIMESTEP = 1000.0f / 120.0f;
	u32 SoftBody::MAX_UPDATE_COUNT = 20;
	const char* SpringObject::s_ExtendedMeshFilePath = MESH_DIRECTORY "spring-extended.glb";
	const char* SpringObject::s_ContractedMeshFilePath = MESH_DIRECTORY "spring-contracted.glb";
	MaterialID SpringObject::s_SpringMatID = InvalidMaterialID;
	MaterialID SpringObject::s_BobberMatID = InvalidMaterialID;

	SpringObject::SpringObject(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, SpringObjectSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
	}

	PropertyCollection* SpringObject::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("spring");

		collection->childCollection->RegisterProperty("end point", FIELD_OFFSET_AND_TYPE(SpringObject, m_TargetPos))
			.VersionAdded(6)
			.Precision(2)
			.DefaultValue(VEC3_ZERO);

		return collection;
	}

	GameObject* SpringObject::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		SpringObject* newObject = new SpringObject(newObjectName, newGameObjectID);

		// Ensure mesh & rigid body isn't copied
		copyFlags = (CopyFlags)(copyFlags & ~CopyFlags::MESH);
		copyFlags = (CopyFlags)(copyFlags & ~CopyFlags::RIGIDBODY);
		CopyGenericFields(newObject, parent, copyFlags);

		return newObject;
	}

	void SpringObject::Initialize()
	{
		if (s_SpringMatID == InvalidMaterialID)
		{
			CreateMaterials();
		}

		{
			LoadedMesh* extendedMesh = Mesh::LoadMesh(s_ExtendedMeshFilePath);
			cgltf_mesh* extendedMeshCGLTF = &(extendedMesh->data->meshes[0]);
			m_ExtendedMesh = MeshComponent::LoadFromCGLTF(nullptr, &extendedMeshCGLTF->primitives[0], s_SpringMatID, nullptr, false);

			LoadedMesh* contractedMesh = Mesh::LoadMesh(s_ContractedMeshFilePath);
			cgltf_mesh* contractedMeshCGLTF = &(contractedMesh->data->meshes[0]);
			m_ContractedMesh = MeshComponent::LoadFromCGLTF(nullptr, &contractedMeshCGLTF->primitives[0], s_SpringMatID, nullptr, false);
		}

		if (m_bSimulateTarget)
		{
			m_OriginTransform = new GameObject("Spring origin", BaseObjectSID);
			m_OriginTransform->SetSerializable(false);
			m_OriginTransform->SetVisibleInSceneExplorer(false);
			AddChild(m_OriginTransform);
			m_OriginTransform->Initialize();
			m_OriginTransform->PostInitialize();
		}

		VertexBufferData* extendedVertBufferData = m_ExtendedMesh->GetVertexBufferData();
		VertexBufferData* contractedVertBufferData = m_ContractedMesh->GetVertexBufferData();
		u32 vertCount = extendedVertBufferData->VertexCount;

		m_DynamicVertexBufferCreateInfo = {};
		m_DynamicVertexBufferCreateInfo.attributes = extendedVertBufferData->Attributes;

		m_Mesh = new Mesh(m_OriginTransform);
		m_Indices = m_ExtendedMesh->GetIndexBufferCopy();
		Mesh::CreateInfo meshCreateInfo = {};
		meshCreateInfo.bDynamic = true;
		meshCreateInfo.vertexBufferCreateInfo = &m_DynamicVertexBufferCreateInfo;
		meshCreateInfo.indices = m_Indices;
		meshCreateInfo.materialIDs = { s_SpringMatID };
		meshCreateInfo.initialMaxVertexCount = vertCount;
		m_Mesh->LoadFromMemory(meshCreateInfo);

		m_DynamicVertexBufferCreateInfo.positions_3D.resize(vertCount);
		m_DynamicVertexBufferCreateInfo.texCoords_UV.resize(vertCount);
		m_DynamicVertexBufferCreateInfo.colours_R32G32B32A32.resize(vertCount);
		m_DynamicVertexBufferCreateInfo.normals.resize(vertCount);
		m_DynamicVertexBufferCreateInfo.tangents.resize(vertCount);

		extendedPositions.resize(vertCount);
		extendedNormals.resize(vertCount);
		extendedTangents.resize(vertCount);

		contractedPositions.resize(vertCount);
		contractedNormals.resize(vertCount);
		contractedTangents.resize(vertCount);

		extendedVertBufferData->CopyInto((real*)extendedPositions.data(), (VertexAttributes)VertexAttribute::POSITION);
		extendedVertBufferData->CopyInto((real*)extendedNormals.data(), (VertexAttributes)VertexAttribute::NORMAL);
		extendedVertBufferData->CopyInto((real*)extendedTangents.data(), (VertexAttributes)VertexAttribute::TANGENT);

		contractedVertBufferData->CopyInto((real*)contractedPositions.data(), (VertexAttributes)VertexAttribute::POSITION);
		contractedVertBufferData->CopyInto((real*)contractedNormals.data(), (VertexAttributes)VertexAttribute::NORMAL);
		contractedVertBufferData->CopyInto((real*)contractedTangents.data(), (VertexAttributes)VertexAttribute::TANGENT);

		// Unchanging attributes, just copy extended data in once
		extendedVertBufferData->CopyInto((real*)m_DynamicVertexBufferCreateInfo.texCoords_UV.data(), (VertexAttributes)VertexAttribute::UV);
		extendedVertBufferData->CopyInto((real*)m_DynamicVertexBufferCreateInfo.colours_R32G32B32A32.data(), (VertexAttributes)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT);

		glm::vec3 initialRootPos = m_Transform.GetWorldPosition();
		glm::vec3 initialTargetPos;
		if (m_TargetPos == VEC3_ZERO)
		{
			initialTargetPos = initialRootPos + m_Transform.GetUp() * m_MinLength;
		}
		else
		{
			initialTargetPos = m_TargetPos;
		}

		//m_Target->GetTransform()->SetWorldPosition(initialTargetPos);

		if (m_bSimulateTarget)
		{
			m_SpringSim = new SoftBody("Spring sim");
			m_SpringSim->points = std::vector<Point*>{ new Point(initialRootPos, VEC3_ZERO, 0.0f), new Point(initialTargetPos, VEC3_ZERO, 1.0f / 20.0f) };
			m_SpringSim->SetSerializable(false);
			m_SpringSim->SetVisibleInSceneExplorer(false);
			m_SpringSim->SetOwnedInternally(true);
			real stiffness = 0.02f;
			m_SpringSim->SetStiffness(stiffness);
			m_SpringSim->SetDamping(0.999f);
			m_SpringSim->AddUniqueDistanceConstraint(0, 1, 0, stiffness);
			m_SpringSim->Initialize();
			m_SpringSim->PostInitialize();

			m_Bobber = new GameObject("Spring bobber", BaseObjectSID);
			m_Bobber->SetSerializable(false);
			m_Bobber->SetVisibleInSceneExplorer(false);
			m_Bobber->SetOwnedInternally(true);
			Mesh* bobberMesh = m_Bobber->SetMesh(new Mesh(m_Bobber));
			bobberMesh->LoadFromFile(MESH_DIRECTORY "sphere.glb", s_BobberMatID);
			m_Bobber->Initialize();
			m_Bobber->PostInitialize();
		}
		else
		{
			m_Target = new GameObject("Spring target", BaseObjectSID);
			m_Target->SetSerializable(false);
			m_Target->GetTransform()->SetWorldPosition(initialTargetPos);
			AddSibling(m_Target);
			m_Target->Initialize();
			m_Target->PostInitialize();
		}

		GameObject::Initialize();
	}

	void SpringObject::Update()
	{
		PROFILE_AUTO("SpringObject Update");

		Transform* originTransform = m_OriginTransform->GetTransform();
		glm::vec3 rootPos = originTransform->GetWorldPosition();
		glm::vec3 targetPos;
		if (m_bSimulateTarget)
		{
			m_SpringSim->GetTransform()->SetWorldPosition(rootPos);
			targetPos = m_SpringSim->points[1]->pos;
			m_Bobber->GetTransform()->SetWorldPosition(targetPos);
		}
		else
		{
			targetPos = m_Target->GetTransform()->GetWorldPosition();
		}

		real delta = (glm::dot(targetPos - rootPos, originTransform->GetUp()) - m_MinLength) / (m_MaxLength - m_MinLength);
		real t = glm::clamp(delta, 0.0f, 1.0f);

		for (u32 i = 0; i < (u32)m_DynamicVertexBufferCreateInfo.positions_3D.size(); ++i)
		{
			m_DynamicVertexBufferCreateInfo.positions_3D[i] = Lerp(contractedPositions[i], extendedPositions[i], t);
			m_DynamicVertexBufferCreateInfo.normals[i] = Lerp(contractedNormals[i], extendedNormals[i], t);
			m_DynamicVertexBufferCreateInfo.tangents[i] = Lerp(contractedTangents[i], extendedTangents[i], t);
		}
		m_Mesh->GetSubMesh(0)->UpdateDynamicVertexData(m_DynamicVertexBufferCreateInfo, m_Indices);

		glm::vec3 lookDir = rootPos - targetPos;
		real lookLen = glm::length(lookDir);
		glm::vec3 lookDirNorm = lookDir / lookLen;
		if (lookLen > 0.0001f)
		{
			static glm::quat offsetQuat = glm::quat(glm::vec3(PI_DIV_TWO, PI, 0.0f));
			//g_Renderer->GetDebugRenderer()->drawLine(ToBtVec3(rootPos), ToBtVec3(rootPos + lookDirNorm), btVector3(1.0f, 0.0f, 0.0f), btVector3(1.0f, 0.0f, 0.0f));
			glm::vec3 up = glm::abs(glm::angle(lookDirNorm, VEC3_UP)) < 0.01f ? VEC3_FORWARD : VEC3_UP;
			glm::quat targetRot = glm::quatLookAt(lookDirNorm, up) * offsetQuat;
			if (!glm::any(glm::isnan(targetRot)))
			{
				glm::quat finalRot = glm::slerp(originTransform->GetWorldRotation(), targetRot, g_DeltaTime * 100.0f);
				originTransform->SetWorldRotation(finalRot);
			}
		}

		GameObject::Update();

		g_Renderer->GetDebugRenderer()->DrawAxes(ToBtVec3(originTransform->GetWorldPosition()), ToBtQuaternion(originTransform->GetWorldRotation()), 2.0f);

		m_SpringSim->Update();
		m_Bobber->Update();
	}

	void SpringObject::Destroy(bool bDetachFromParent /* = true */)
	{
		m_ExtendedMesh->Destroy();
		delete m_ExtendedMesh;
		m_ExtendedMesh = nullptr;

		m_ContractedMesh->Destroy();
		delete m_ContractedMesh;
		m_ContractedMesh = nullptr;

		m_SpringSim->Destroy();
		delete m_SpringSim;
		m_SpringSim = nullptr;

		m_Bobber->Destroy();
		delete m_Bobber;
		m_Bobber = nullptr;

		GameObject::Destroy(bDetachFromParent);
	}

	void SpringObject::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		ImGui::Spacing();
		ImGui::Text("Spring");
		m_SpringSim->DrawImGuiObjects(bDrawingEditorObjects);

		ImGui::Spacing();
		ImGui::Text("Bobber");
		m_Bobber->DrawImGuiObjects(bDrawingEditorObjects);

		ImGui::Spacing();
		ImGui::Text("Origin");
		m_Bobber->DrawImGuiObjects(bDrawingEditorObjects);

	}

	void SpringObject::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject springObj;
		if (parentObject.TryGetObject("spring", springObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(springObj, collection->childCollection);
		}
	}

	void SpringObject::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject springObj = {};

		m_TargetPos = m_SpringSim->points[1]->pos;
		SerializeRegisteredProperties(springObj, collection->childCollection, bSerializePrefabData);
		if (!springObj.fields.empty())
		{
			parentObject.fields.emplace_back("spring", JSONValue(springObj));
		}
	}

	void SpringObject::CreateMaterials()
	{
		s_SpringMatID = g_Renderer->GetMaterialID("Spring");
		if (s_SpringMatID == InvalidMaterialID)
		{
			MaterialCreateInfo matCreateInfo = {};
			matCreateInfo.name = "Spring";
			matCreateInfo.shaderName = "pbr";
			matCreateInfo.constAlbedo = glm::vec4(0.8f, 0.05f, 0.04f, 1.0f);
			matCreateInfo.constRoughness = 1.0f;
			matCreateInfo.constMetallic = 0.0f;
			matCreateInfo.bDynamic = true;
			matCreateInfo.bSerializable = false;
			s_SpringMatID = g_Renderer->InitializeMaterial(&matCreateInfo);
		}

		s_BobberMatID = g_Renderer->GetMaterialID("Bobber");
		if (s_BobberMatID == InvalidMaterialID)
		{
			MaterialCreateInfo matCreateInfo = {};
			matCreateInfo.name = "Bobber";
			matCreateInfo.shaderName = "pbr";
			matCreateInfo.constAlbedo = glm::vec4(0.2f, 0.2f, 0.24f, 1.0f);
			matCreateInfo.constRoughness = 0.0f;
			matCreateInfo.constMetallic = 1.0f;
			matCreateInfo.bSerializable = false;
			s_BobberMatID = g_Renderer->InitializeMaterial(&matCreateInfo);
		}
	}

	DistanceConstraint::DistanceConstraint(i32 pointIndex0, i32 pointIndex1, real stiffness, real targetDistance) :
		Constraint(stiffness, Constraint::EqualityType::EQUALITY, Constraint::Type::DISTANCE),
		targetDistance(targetDistance)
	{
		pointIndices[0] = pointIndex0;
		pointIndices[1] = pointIndex1;
	}

	BendingConstraint::BendingConstraint(i32 pointIndex0, i32 pointIndex1, i32 pointIndex2, i32 pointIndex3, real stiffness, real targetPhi) :
		Constraint(stiffness, Constraint::EqualityType::EQUALITY, Constraint::Type::BENDING),
		targetPhi(targetPhi)
	{
		pointIndices[0] = pointIndex0;
		pointIndices[1] = pointIndex1;
		pointIndices[2] = pointIndex2;
		pointIndices[3] = pointIndex3;
	}

	Triangle::Triangle()
	{
		pointIndices[0] = 0;
		pointIndices[1] = 0;
		pointIndices[2] = 0;
	}

	Triangle::Triangle(i32 pointIndex0, i32 pointIndex1, i32 pointIndex2)
	{
		pointIndices[0] = pointIndex0;
		pointIndices[1] = pointIndex1;
		pointIndices[2] = pointIndex2;
	}

	SoftBody::SoftBody(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, SoftBodySID, gameObjectID, sourcePrefabID, bIsPrefabTemplate),
		m_SolverIterationCount(4) // Default, gets overridden in ParseUniqueFields
	{
	}

	PropertyCollection* SoftBody::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("soft body");

		collection->childCollection->RegisterProperty("solver iteration count", FIELD_OFFSET_AND_TYPE(SoftBody, m_SolverIterationCount))
			.VersionAdded(6);
		collection->childCollection->RegisterProperty("render wireframe", FIELD_OFFSET_AND_TYPE(SoftBody, m_bRenderWireframe))
			.VersionAdded(6);
		collection->childCollection->RegisterProperty("damping", FIELD_OFFSET_AND_TYPE(SoftBody, m_Damping))
			.VersionAdded(6)
			.Precision(2);
		collection->childCollection->RegisterProperty("stiffness", FIELD_OFFSET_AND_TYPE(SoftBody, m_Stiffness))
			.VersionAdded(6)
			.Precision(2);
		collection->childCollection->RegisterProperty("bending stiffness", FIELD_OFFSET_AND_TYPE(SoftBody, m_BendingStiffness))
			.VersionAdded(6)
			.Precision(2);

		return collection;
	}

	GameObject* SoftBody::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		SoftBody* newSoftBody = new SoftBody(newObjectName, newGameObjectID);

		CopyGenericFields(newSoftBody, parent, copyFlags);

		newSoftBody->points.resize(points.size());
		newSoftBody->constraints.resize(constraints.size());
		newSoftBody->initialPositions.resize(initialPositions.size());

		for (u32 i = 0; i < (u32)points.size(); ++i)
		{
			newSoftBody->points[i] = new Point(initialPositions[i], VEC3_ZERO, points[i]->invMass);
		}

		for (u32 i = 0; i < (u32)constraints.size(); ++i)
		{
			switch (constraints[i]->type)
			{
			case Constraint::Type::DISTANCE:
			{
				DistanceConstraint* distanceConstraint = (DistanceConstraint*)constraints[i];
				newSoftBody->constraints[i] = new DistanceConstraint(distanceConstraint->pointIndices[0], distanceConstraint->pointIndices[1], constraints[i]->stiffness, distanceConstraint->targetDistance);
			} break;
			case Constraint::Type::BENDING:
			{
				BendingConstraint* bendingConstraint = (BendingConstraint*)constraints[i];
				newSoftBody->constraints[i] = new BendingConstraint(
					bendingConstraint->pointIndices[0],
					bendingConstraint->pointIndices[1],
					bendingConstraint->pointIndices[2],
					bendingConstraint->pointIndices[3],
					constraints[i]->stiffness, bendingConstraint->targetPhi);
			} break;
			default:
			{
				PrintError("Unhandled constraint type in SoftBody::CopySelf\n");
			} break;
			}
		}

		newSoftBody->m_SolverIterationCount = m_SolverIterationCount;
		newSoftBody->m_bRenderWireframe = m_bRenderWireframe;
		newSoftBody->m_Damping = m_Damping;
		newSoftBody->m_Stiffness = m_Stiffness;
		newSoftBody->m_BendingStiffness = m_BendingStiffness;

		if (m_Mesh != nullptr)
		{
			newSoftBody->m_CurrentMeshFileName = m_CurrentMeshFileName;
			newSoftBody->m_CurrentMeshFilePath = m_CurrentMeshFilePath;
			newSoftBody->m_SelectedMeshIndex = m_SelectedMeshIndex;
			newSoftBody->LoadFromMesh();
		}

		return newSoftBody;
	}

	void SoftBody::Initialize()
	{
		m_MSToSim = 0.0f;

		GameObject::Initialize();
	}

	void SoftBody::Destroy(bool bDetachFromParent /* = true */)
	{
		for (Point* point : points)
		{
			delete point;
		}
		points.clear();

		for (Constraint* constraint : constraints)
		{
			delete constraint;
		}
		constraints.clear();

		for (Triangle* triangle : triangles)
		{
			delete triangle;
		}
		triangles.clear();

		if (m_Mesh != nullptr)
		{
			m_Mesh->Destroy();
			m_Mesh = nullptr;
			m_MeshComponent = nullptr;
		}

		GameObject::Destroy(bDetachFromParent);
	}

	void SoftBody::Update()
	{
		PROFILE_AUTO("SoftBody Update");

		GameObject::Update();

		//PROFILE_BEGIN("SoftBody Update");

		if (!m_bPaused || m_bSingleStep)
		{
			if (m_bSingleStep)
			{
				m_MSToSim = FIXED_UPDATE_TIMESTEP;
			}
			else
			{
				m_MSToSim += g_DeltaTime * 1000.0f;
			}

			m_bSingleStep = false;

			u32 fixedUpdateCount = glm::min((u32)(m_MSToSim / FIXED_UPDATE_TIMESTEP), MAX_UPDATE_COUNT);

			// TODO: Make member/store in manager
			std::vector<glm::vec3> predictedPositions(points.size());

			for (u32 updateIteration = 0; updateIteration < fixedUpdateCount; ++updateIteration)
			{
				const sec dt = FIXED_UPDATE_TIMESTEP / 1000.0f;

				std::vector<Constraint*> collisionConstraints;

				glm::vec3 globalExternalForces = glm::vec3(0.0f, -9.81f, 0.0f); // Just gravity for now

				// Apply external forces
				for (Point* point : points)
				{
					if (point->invMass != 0.0f)
					{
						point->vel += (dt / point->invMass) * globalExternalForces;
					}
				}

				// Damp velocities
				for (Point* point : points)
				{
					point->vel *= m_Damping;
				}

				// Explicit Euler step
				{
					u32 i = 0;
					for (Point* point : points)
					{
						// Predict next position
						predictedPositions[i++] = point->pos + point->vel * dt;
					}
				}

				// Generate collision constraints
				for (u32 i = 0; i < (u32)points.size(); ++i)
				{
					//glm::vec3 deltaPos = predictedPositions[i] - points[i]->pos;

				}


				// Project constraints (update predicted positions)
				for (u32 iteration = 0; iteration < m_SolverIterationCount; ++iteration)
				{
					std::vector<Constraint*> constraintLists[] = { constraints, collisionConstraints };
					for (std::vector<Constraint*>& constraintList : constraintLists)
					{
						for (Constraint* constraint : constraintList)
						{
							switch (constraint->type)
							{
							case Constraint::Type::DISTANCE:
							{
								DistanceConstraint* distanceConstraint = (DistanceConstraint*)constraint;
								Point* point0 = points[distanceConstraint->pointIndices[0]];
								Point* point1 = points[distanceConstraint->pointIndices[1]];

								if (point0->invMass == 0.0f && point1->invMass == 0.0f)
								{
									continue;
								}

								glm::vec3 pos0 = predictedPositions[distanceConstraint->pointIndices[0]];
								glm::vec3 pos1 = predictedPositions[distanceConstraint->pointIndices[1]];

								real k = distanceConstraint->stiffness;
								// Make stiffness scale linearly with iteration count
								real kPrime = 1.0f - glm::pow(1.0f - k, 1.0f / m_SolverIterationCount);

								glm::vec3 posDiff = pos1 - pos0;
								real posDiffDist = glm::length(posDiff);
								glm::vec3 posDiffN = posDiff / posDiffDist;
								real gradient = posDiffDist - distanceConstraint->targetDistance;
								glm::vec3 dp0 = ((point0->invMass / (point0->invMass + point1->invMass)) * gradient * kPrime) * posDiffN;
								glm::vec3 dp1 = ((point1->invMass / (point0->invMass + point1->invMass)) * gradient * kPrime) * -posDiffN;

								predictedPositions[distanceConstraint->pointIndices[0]] += dp0;
								predictedPositions[distanceConstraint->pointIndices[1]] += dp1;
							} break;
							case Constraint::Type::BENDING:
							{
								/*
								Points must be in the following order: (describing adjacent triangles)
									  1
									  *
									 /|\
									/ | \
								 2 /  |  \ 3
								  *   |   *
								   \  |  /
									\ | /
									 \|/
									  *
									  0
								*/

								BendingConstraint* bendingConstraint = (BendingConstraint*)constraint;
								// NOTE: Differing notation used here than in [Muller06] ([0,3] rather than [1,4])
								Point* point0 = points[bendingConstraint->pointIndices[0]];
								Point* point1 = points[bendingConstraint->pointIndices[1]];
								Point* point2 = points[bendingConstraint->pointIndices[2]];
								Point* point3 = points[bendingConstraint->pointIndices[3]];

								glm::vec3 pos0 = VEC3_ZERO;
								glm::vec3 pos1 = point1->pos - point0->pos;
								glm::vec3 pos2 = point2->pos - point0->pos;
								glm::vec3 pos3 = point3->pos - point0->pos;

								glm::vec3 normal0 = glm::normalize(glm::cross(pos1 - pos0, pos2 - pos0));
								glm::vec3 normal1 = glm::normalize(glm::cross(pos1 - pos0, pos3 - pos0));

								real d = glm::dot(normal0, normal1);
								real phi = glm::acos(d) - bendingConstraint->targetPhi;

								glm::vec3 q2 = (glm::cross(pos1, normal1) + glm::cross(normal0, pos1) * d) / glm::length(glm::cross(pos1, pos2));
								glm::vec3 q3 = (glm::cross(pos1, normal0) + glm::cross(normal1, pos1) * d) / glm::length(glm::cross(pos1, pos3));
								glm::vec3 q1 = -(glm::cross(pos2, normal1) + glm::cross(normal0, pos2) * d) / glm::length(glm::cross(pos1, pos2)) -
									(glm::cross(pos3, normal0) + glm::cross(normal1, pos3) * d) / glm::length(glm::cross(pos1, pos3));
								glm::vec3 q0 = -q1 - q2 - q3;
								real q0Sqlen = glm::dot(q0, q0);
								real q1Sqlen = glm::dot(q1, q1);
								real q2Sqlen = glm::dot(q2, q2);
								real q3Sqlen = glm::dot(q3, q3);
								real qSum = q0Sqlen + q1Sqlen + q2Sqlen + q3Sqlen;

								if (IsNanOrInf(q0) || IsNanOrInf(q1) || IsNanOrInf(q2) || IsNanOrInf(q3))
								{
									continue;
								}

								real invMassSum = point0->invMass + point1->invMass + point2->invMass + point3->invMass;

								real denom = (invMassSum * qSum);
								if (abs(denom) <= 0.0001f)
								{
									continue;
								}
								real deltaPBase = glm::sqrt(1.0f - d * d) * phi;
								if (glm::dot(glm::cross(normal0, normal1), pos3 - pos1) > 0.0f)
								{
									deltaPBase *= -1.0f;
								}

								glm::vec3 deltaP0 = -(point0->invMass * deltaPBase) / denom * q0;
								glm::vec3 deltaP1 = -(point1->invMass * deltaPBase) / denom * q1;
								glm::vec3 deltaP2 = -(point2->invMass * deltaPBase) / denom * q2;
								glm::vec3 deltaP3 = -(point3->invMass * deltaPBase) / denom * q3;

								real k = bendingConstraint->stiffness;
								// Make stiffness scale linearly with iteration count
								real kPrime = 1.0f - glm::pow(1.0f - k, 1.0f / m_SolverIterationCount);

								//Print("%.2f, %.2f\n", d, phi);
								//Print("%.2f, %.2f, %.2f, %.2f\n", q0.x, q1.x, q2.x, q3.x);
								//Print("%.2f, %.2f, %.2f\n", deltaP0.x, deltaP0.y, deltaP0.z);
								//Print("%.2f, %.2f, %.2f\n", deltaP1.x, deltaP1.y, deltaP1.z);
								//Print("%.2f, %.2f, %.2f\n", deltaP2.x, deltaP2.y, deltaP2.z);
								//Print("%.2f, %.2f, %.2f\n", deltaP3.x, deltaP3.y, deltaP3.z);
								//Print("\n");

								predictedPositions[bendingConstraint->pointIndices[0]] += deltaP0 * kPrime;
								predictedPositions[bendingConstraint->pointIndices[1]] += deltaP1 * kPrime;
								predictedPositions[bendingConstraint->pointIndices[2]] += deltaP2 * kPrime;
								predictedPositions[bendingConstraint->pointIndices[3]] += deltaP3 * kPrime;
							} break;
							default:
							{
								PrintError("Unhandled constraint type\n");
							} break;
							}
						}
					}
				}

				// Integrate
				{
					u32 i = 0;
					for (Point* point : points)
					{
						point->vel = (predictedPositions[i] - point->pos) / dt;
						point->pos = predictedPositions[i++];
					}
				}

				// Update velocities (friction, restitution)
				// Skip for now


				for (Constraint* collisionConstraint : collisionConstraints)
				{
					delete collisionConstraint;
				}
			}


			m_MSToSim -= fixedUpdateCount * FIXED_UPDATE_TIMESTEP;
		}

		if (m_MeshComponent != nullptr)
		{
			if (m_MeshVertexBufferCreateInfo.positions_3D.size() != points.size())
			{
				m_MeshVertexBufferCreateInfo.positions_3D.resize(points.size());
			}
			glm::vec3 worldPos = m_Transform.GetWorldPosition();
			for (u32 i = 0; i < (u32)points.size(); ++i)
			{
				m_MeshVertexBufferCreateInfo.positions_3D[i] = points[i]->pos - worldPos;
			}

			std::vector<u32> indices = m_MeshComponent->GetIndexBufferCopy();
			MeshComponent::CalculateTangents(m_MeshVertexBufferCreateInfo, indices);

			//for (u32 i = 0; i <(u32)m_MeshVertexBufferCreateInfo.normals.size(); ++i)
			//{
			//	glm::mat4 worldMat = glm::mat4(m_Transform.GetLocalRotation()) * glm::scale(MAT4_IDENTITY, m_Transform.GetWorldScale());
			//	m_MeshVertexBufferCreateInfo.normals[i] = (glm::vec3)(worldMat * glm::vec4(m_MeshVertexBufferCreateInfo.normals[i], 1.0f));
			//	m_MeshVertexBufferCreateInfo.tangents[i] = (glm::vec3)(worldMat * glm::vec4(m_MeshVertexBufferCreateInfo.tangents[i], 1.0f));
			//}

			m_MeshComponent->UpdateDynamicVertexData(m_MeshVertexBufferCreateInfo, indices);
		}

		if (m_bRenderWireframe)
		{
			Draw();
		}
	}

	void SoftBody::Draw()
	{
		DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();
		for (Point* point : points)
		{
			debugRenderer->drawSphere(ToBtVec3(point->pos), 0.1f, btVector3(0.8f, 0.2f, 0.1f));
		}

		for (u32 i = 0; i < (u32)constraints.size(); ++i)
		{
			Constraint* constraint = constraints[i];

			switch (constraint->type)
			{
			case Constraint::Type::DISTANCE:
			{
				DistanceConstraint* distanceConstraint = (DistanceConstraint*)constraint;
				debugRenderer->drawLine(
					ToBtVec3(points[distanceConstraint->pointIndices[0]]->pos),
					ToBtVec3(points[distanceConstraint->pointIndices[1]]->pos),
					btVector3(0.5f, 0.4f, 0.1f), btVector3(0.5f, 0.4f, 0.1f));
			} break;
			case Constraint::Type::BENDING:
			{
				BendingConstraint* bendingConstraint = (BendingConstraint*)constraint;

				if (i == (u32)m_ShownBendingIndex)
				{
					Point* point0 = points[bendingConstraint->pointIndices[0]];
					Point* point1 = points[bendingConstraint->pointIndices[1]];
					Point* point2 = points[bendingConstraint->pointIndices[2]];
					Point* point3 = points[bendingConstraint->pointIndices[3]];

					glm::vec3 normal0 = glm::normalize(glm::cross(point1->pos - point0->pos, point2->pos - point0->pos));
					glm::vec3 normal1 = glm::normalize(glm::cross(point1->pos - point0->pos, point3->pos - point0->pos));

					btVector3 col = btVector3(0.1f, 0.9f, 0.1f);
					btVector3 colN = btVector3(0.9f, 0.1f, 0.1f);
					real f = 0.01f;

					debugRenderer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[0]]->pos + normal0 * f), ToBtVec3(points[bendingConstraint->pointIndices[1]]->pos + normal0 * f), col, col);
					debugRenderer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[1]]->pos + normal0 * f), ToBtVec3(points[bendingConstraint->pointIndices[2]]->pos + normal0 * f), col, col);
					debugRenderer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[2]]->pos + normal0 * f), ToBtVec3(points[bendingConstraint->pointIndices[0]]->pos + normal0 * f), col, col);
					debugRenderer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[0]]->pos + normal1 * f), ToBtVec3(points[bendingConstraint->pointIndices[1]]->pos + normal1 * f), col, col);
					debugRenderer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[0]]->pos + normal1 * f), ToBtVec3(points[bendingConstraint->pointIndices[3]]->pos + normal1 * f), col, col);
					debugRenderer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[3]]->pos + normal1 * f), ToBtVec3(points[bendingConstraint->pointIndices[1]]->pos + normal1 * f), col, col);

					glm::vec3 tri0Mid = (points[bendingConstraint->pointIndices[0]]->pos + points[bendingConstraint->pointIndices[1]]->pos + points[bendingConstraint->pointIndices[2]]->pos) / 3.0f;
					glm::vec3 tri1Mid = (points[bendingConstraint->pointIndices[0]]->pos + points[bendingConstraint->pointIndices[1]]->pos + points[bendingConstraint->pointIndices[3]]->pos) / 3.0f;

					debugRenderer->drawLine(ToBtVec3(tri0Mid), ToBtVec3(tri0Mid + normal0 * 1.0f), colN, colN);
					debugRenderer->drawLine(ToBtVec3(tri1Mid), ToBtVec3(tri1Mid + normal1 * 1.0f), colN, colN);
				}
			} break;
			}
		}
	}

	u32 SoftBody::AddUniqueDistanceConstraint(i32 index0, i32 index1, u32 atIndex, real stiffness)
	{
		for (Constraint* constraint : constraints)
		{
			if (constraint != nullptr &&
				constraint->type == Constraint::Type::DISTANCE)
			{
				DistanceConstraint* distanceConstraint = (DistanceConstraint*)constraint;
				if (((distanceConstraint->pointIndices[0] == index0 && distanceConstraint->pointIndices[1] == index1) ||
					(distanceConstraint->pointIndices[1] == index0 && distanceConstraint->pointIndices[0] == index1)))
				{
					return atIndex != u32_max ? atIndex : (u32)constraints.size();
				}
			}
		}

		if (atIndex != u32_max)
		{
			if (atIndex >= (u32)constraints.size())
			{
				constraints.resize(atIndex + 1);
			}

			constraints[atIndex] = new DistanceConstraint(index0, index1, stiffness, glm::distance(points[index0]->pos, points[index1]->pos));
			return atIndex + 1;
		}
		else
		{
			constraints.push_back(new DistanceConstraint(index0, index1, stiffness, glm::distance(points[index0]->pos, points[index1]->pos)));
			return (u32)constraints.size();
		}
	}

	u32 SoftBody::AddUniqueBendingConstraint(i32 index0, i32 index1, i32 index2, i32 index3, u32 atIndex, real stiffness)
	{
		for (Constraint* constraint : constraints)
		{
			if (constraint != nullptr &&
				constraint->type == Constraint::Type::BENDING)
			{
				BendingConstraint* bendingConstraint = (BendingConstraint*)constraint;
				if ((bendingConstraint->pointIndices[0] == index0 && bendingConstraint->pointIndices[1] == index1) &&
					(bendingConstraint->pointIndices[2] == index2 && bendingConstraint->pointIndices[3] == index3))
				{
					return atIndex != u32_max ? atIndex : (u32)constraints.size();
				}
			}
		}

		glm::vec3 normal0 = glm::normalize(glm::cross(points[index1]->pos - points[index0]->pos, points[index2]->pos - points[index0]->pos));
		glm::vec3 normal1 = glm::normalize(glm::cross(points[index1]->pos - points[index0]->pos, points[index3]->pos - points[index0]->pos));

		real initialPhi = glm::acos(glm::dot(normal0, normal1));

		if (atIndex != u32_max)
		{
			if (atIndex >= (u32)constraints.size())
			{
				constraints.resize(atIndex + 1);
			}

			constraints[atIndex] = new BendingConstraint(index0, index1, index2, index3, stiffness, initialPhi);
			return atIndex + 1;
		}
		else
		{
			constraints.push_back(new BendingConstraint(index0, index1, index2, index3, stiffness, initialPhi));
			return (u32)constraints.size();
		}
	}

	void SoftBody::LoadFromMesh()
	{
		if (m_Mesh != nullptr)
		{
			m_Mesh->Destroy();
			delete m_Mesh;
			m_MeshComponent = nullptr;
		}

		if (m_MeshMaterialID == InvalidMaterialID)
		{
			MaterialCreateInfo matCreateInfo = {};
			matCreateInfo.name = "Soft Body Material";
			matCreateInfo.shaderName = "pbr";
			matCreateInfo.bDynamic = true;
			matCreateInfo.constRoughness = 0.95f;
			matCreateInfo.bSerializable = false;

			m_MeshMaterialID = g_Renderer->InitializeMaterial(&matCreateInfo);
		}

		m_Mesh = new Mesh(this);

		RenderObjectCreateInfo renderObjectCreateInfo = {};
		renderObjectCreateInfo.cullFace = CullFace::NONE;

		Mesh::CreateInfo meshCreateInfo = {};
		meshCreateInfo.relativeFilePath = m_CurrentMeshFilePath;
		meshCreateInfo.materialIDs = { m_MeshMaterialID };
		meshCreateInfo.bDynamic = true;
		meshCreateInfo.optionalRenderObjectCreateInfo = &renderObjectCreateInfo;
		if (!m_Mesh->LoadFromFile(meshCreateInfo))
		{
			PrintError("Failed to load mesh\n");
			m_Mesh->Destroy();
			delete m_Mesh;
			m_Mesh = nullptr;
		}
		else
		{
			m_MeshVertexBufferCreateInfo = {};
			m_MeshVertexBufferCreateInfo.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(m_MeshMaterialID)->shaderID)->vertexAttributes;

			m_MeshComponent = m_Mesh->GetSubMesh(0);
			VertexBufferData* vertexBufferData = m_MeshComponent->GetVertexBufferData();
			std::vector<u32> indexData = m_MeshComponent->GetIndexBufferCopy();
			std::vector<glm::vec3> posData(vertexBufferData->VertexCount);
			std::vector<glm::vec2> uvData(vertexBufferData->VertexCount);
			VertexBufferData::ResizeForPresentAttributes(m_MeshVertexBufferCreateInfo, vertexBufferData->VertexCount);
			bool bCopySuccess = vertexBufferData->CopyInto((real*)posData.data(), (VertexAttributes)VertexAttribute::POSITION) == (posData.size() * sizeof(glm::vec3));
			bCopySuccess = bCopySuccess && vertexBufferData->CopyInto((real*)uvData.data(), (VertexAttributes)VertexAttribute::UV) == (uvData.size() * sizeof(glm::vec2));
			if (!bCopySuccess)
			{
				PrintError("Something bad happened in soft body mesh conversion\n");
			}
			else
			{
				for (Point* point : points)
				{
					delete point;
				}
				points.clear();

				for (Constraint* constraint : constraints)
				{
					delete constraint;
				}
				triangles.clear();

				for (Triangle* triangle : triangles)
				{
					delete triangle;
				}
				triangles.clear();

				points.resize(vertexBufferData->VertexCount);
				initialPositions.resize(vertexBufferData->VertexCount);
				glm::vec3 smallestPos(99999.0f);
				for (u32 i = 0; i < (u32)posData.size(); ++i)
				{
					glm::vec3 pos = posData[i];
					if (pos.x < smallestPos.x || (pos.x == smallestPos.x && pos.z < smallestPos.z))
					{
						smallestPos = pos;
					}
					initialPositions[i] = pos;

					m_MeshVertexBufferCreateInfo.texCoords_UV[i] = uvData[i];
					points[i] = new Point(pos, VEC3_ZERO, 1.0f);
				}

				triangles.resize(indexData.size() / 3);
				for (u32 i = 0; i < (u32)triangles.size(); ++i)
				{
					triangles[i] = new Triangle(indexData[i * 3], indexData[i * 3 + 1], indexData[i * 3 + 2]);
				}

				constraints.resize(indexData.size() / 3);
				u32 constraintIndex = 0;
				for (u32 i = 0; i < (u32)indexData.size() - 1; i += 3)
				{
					constraintIndex = AddUniqueDistanceConstraint(indexData[i + 0], indexData[i + 1], constraintIndex, 0.995f);
					constraintIndex = AddUniqueDistanceConstraint(indexData[i + 1], indexData[i + 2], constraintIndex, 0.995f);
					constraintIndex = AddUniqueDistanceConstraint(indexData[i + 2], indexData[i + 0], constraintIndex, 0.995f);
				}

				m_FirstBendingConstraintIndex = (i32)constraints.size();
				m_ShownBendingIndex = m_FirstBendingConstraintIndex;

				for (u32 i = 0; i < (u32)indexData.size() - 1; i += 3)
				{
					i32 index0 = indexData[i + 0];
					i32 index1 = indexData[i + 1];
					i32 index2 = indexData[i + 2];

					Triangle tri{ index0, index1, index2 };

					// edge0 = 0 -> 1, edge1 = 1 -> 2, edge2 = 2 -> 0
					Triangle sharedTri;
					i32 outOutsideVertIndex;
					if (GetTriangleSharingEdge(indexData, index0, index1, tri, sharedTri, outOutsideVertIndex))
					{
						i32 index3 = sharedTri.pointIndices[outOutsideVertIndex];
						constraintIndex = AddUniqueBendingConstraint(index0, index1, index2, index3, constraintIndex, 0.995f);
					}
					if (GetTriangleSharingEdge(indexData, index1, index2, tri, sharedTri, outOutsideVertIndex))
					{
						i32 index3 = sharedTri.pointIndices[outOutsideVertIndex];
						constraintIndex = AddUniqueBendingConstraint(index1, index2, index0, index3, constraintIndex, 0.995f);
					}
					if (GetTriangleSharingEdge(indexData, index2, index0, tri, sharedTri, outOutsideVertIndex))
					{
						i32 index3 = sharedTri.pointIndices[outOutsideVertIndex];
						constraintIndex = AddUniqueBendingConstraint(index2, index0, index1, index3, constraintIndex, 0.995f);
					}
				}
			}
		}
	}

	bool SoftBody::GetTriangleSharingEdge(const std::vector<u32>& indexData, i32 edgeIndex0, i32 edgeIndex1, const Triangle& originalTri, Triangle& outTri, i32& outOutsideVertIndex)
	{
		outOutsideVertIndex = -1;

		for (u32 i = 0; i < (u32)indexData.size() - 1; i += 3)
		{
			i32 index0 = indexData[i + 0];
			i32 index1 = indexData[i + 1];
			i32 index2 = indexData[i + 2];

			if (memcmp(originalTri.pointIndices, &indexData[i], sizeof(i32) * 3) == 0)
			{
				// Skip original tri
				continue;
			}

			if (index0 == edgeIndex0 && (index1 == edgeIndex1 || index2 == edgeIndex1))
			{
				outOutsideVertIndex = (index1 == edgeIndex1) ? 2 : 1;
			}
			if (index1 == edgeIndex0 && (index0 == edgeIndex1 || index2 == edgeIndex1))
			{
				outOutsideVertIndex = (index0 == edgeIndex1) ? 2 : 0;
			}
			if (index2 == edgeIndex0 && (index0 == edgeIndex1 || index1 == edgeIndex1))
			{
				outOutsideVertIndex = (index0 == edgeIndex1) ? 1 : 0;
			}
			if (outOutsideVertIndex != -1)
			{
				outTri.pointIndices[0] = index0;
				outTri.pointIndices[1] = index1;
				outTri.pointIndices[2] = index2;
				return true;
			}
		}
		return false;
	}

	void SoftBody::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject softBodyObject;
		if (parentObject.TryGetObject("soft body", softBodyObject))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(softBodyObject, collection->childCollection);

			std::vector<JSONObject> pointsArr;
			if (softBodyObject.TryGetObjectArray("points", pointsArr))
			{
				points.resize(pointsArr.size());
				initialPositions.resize(pointsArr.size());

				u32 i = 0;
				for (JSONObject& pointObj : pointsArr)
				{
					glm::vec3 pos = pointObj.GetVec3("position");
					glm::vec3 vel = VEC3_ZERO;
					real invMass = pointObj.GetFloat("inverse mass");
					points[i] = new Point(pos, vel, invMass);
					initialPositions[i] = points[i]->pos;

					i++;
				}
			}

			std::vector<JSONObject> constraintsArr;
			if (softBodyObject.TryGetObjectArray("constraints", constraintsArr))
			{
				constraints.resize(constraintsArr.size());

				u32 i = 0;
				for (JSONObject& constraintObj : constraintsArr)
				{
					real stiffness = constraintObj.GetFloat("stiffness");
					Constraint::Type type = (Constraint::Type)constraintObj.GetInt("type");

					switch (type)
					{
					case Constraint::Type::DISTANCE:
					{
						JSONObject distanceConstraintObj = constraintObj.GetObject("distance constraint");

						i32 index0 = distanceConstraintObj.GetInt("index 0");
						i32 index1 = distanceConstraintObj.GetInt("index 1");
						real targetDistance = distanceConstraintObj.GetFloat("target distance");

						constraints[i] = new DistanceConstraint(index0, index1, stiffness, targetDistance);
					} break;
					case Constraint::Type::BENDING:
					{
						JSONObject bendingConstraintObj = constraintObj.GetObject("bending constraint");

						i32 index0 = bendingConstraintObj.GetInt("index 0");
						i32 index1 = bendingConstraintObj.GetInt("index 1");
						i32 index2 = bendingConstraintObj.GetInt("index 2");
						i32 index3 = bendingConstraintObj.GetInt("index 3");
						real targetPhi = bendingConstraintObj.GetFloat("target phi");

						constraints[i] = new BendingConstraint(index0, index1, index2, index3, stiffness, targetPhi);
					} break;
					default:
					{
						PrintError("Unhandled constraint type in SoftBody::ParseUniqueFields\n");
						constraints[i] = nullptr;
					} break;
					}

					i++;
				}
			}

			if (softBodyObject.TryGetString("mesh file path", m_CurrentMeshFilePath))
			{
				m_CurrentMeshFileName = StripLeadingDirectories(m_CurrentMeshFilePath);
				for (i32 i = 0; i < (i32)g_ResourceManager->discoveredMeshes.size(); ++i)
				{
					if (g_ResourceManager->discoveredMeshes[i] == m_CurrentMeshFilePath)
					{
						m_SelectedMeshIndex = i;
						break;
					}
				}
				if (m_SelectedMeshIndex == -1)
				{
					m_CurrentMeshFilePath = "";
					m_CurrentMeshFileName = "";
					PrintWarn("SoftBody's previously saved mesh (%s) might have been moved or renamed, clearing field.\n", m_CurrentMeshFileName.c_str());
				}
				else
				{
					LoadFromMesh();
				}
			}
		}
	}

	void SoftBody::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject softBodyObject = {};

		SerializeRegisteredProperties(softBodyObject, collection->childCollection, bSerializePrefabData);

		std::vector<JSONObject> pointsArr(points.size());
		{
			// Points are simulated & parsed in world space
			u32 i = 0;
			for (const Point* point : points)
			{
				pointsArr[i].fields.emplace_back("position", JSONValue(point->pos));
				pointsArr[i].fields.emplace_back("inverse mass", JSONValue(point->invMass));

				i++;
			}
		}
		softBodyObject.fields.emplace_back("points", JSONValue(pointsArr));

		std::vector<JSONObject> constraintsArr(constraints.size());
		{
			u32 i = 0;
			for (const Constraint* constraint : constraints)
			{
				constraintsArr[i].fields.emplace_back("stiffness", JSONValue(constraint->stiffness));
				constraintsArr[i].fields.emplace_back("type", JSONValue((i32)constraint->type));

				switch (constraint->type)
				{
				case Constraint::Type::DISTANCE:
				{
					DistanceConstraint* distanceConstraint = (DistanceConstraint*)constraint;

					JSONObject distanceConstraintObj = {};

					distanceConstraintObj.fields.emplace_back("index 0", JSONValue(distanceConstraint->pointIndices[0]));
					distanceConstraintObj.fields.emplace_back("index 1", JSONValue(distanceConstraint->pointIndices[1]));
					distanceConstraintObj.fields.emplace_back("target distance", JSONValue(distanceConstraint->targetDistance));

					constraintsArr[i].fields.emplace_back("distance constraint", JSONValue(distanceConstraintObj));
				} break;
				case Constraint::Type::BENDING:
				{
					BendingConstraint* bendingConstraint = (BendingConstraint*)constraint;

					JSONObject bendingConstraintObj = {};

					bendingConstraintObj.fields.emplace_back("index 0", JSONValue(bendingConstraint->pointIndices[0]));
					bendingConstraintObj.fields.emplace_back("index 1", JSONValue(bendingConstraint->pointIndices[1]));
					bendingConstraintObj.fields.emplace_back("index 2", JSONValue(bendingConstraint->pointIndices[2]));
					bendingConstraintObj.fields.emplace_back("index 3", JSONValue(bendingConstraint->pointIndices[3]));
					bendingConstraintObj.fields.emplace_back("target phi", JSONValue(bendingConstraint->targetPhi));

					constraintsArr[i].fields.emplace_back("bending constraint", JSONValue(bendingConstraintObj));
				} break;
				default:
				{
					PrintError("Unhandled type in SoftBody::SerializeUniqueFields\n");
				} break;
				}

				i++;
			}
		}
		softBodyObject.fields.emplace_back("constraints", JSONValue(constraintsArr));

		if (m_SelectedMeshIndex != -1)
		{
			softBodyObject.fields.emplace_back("mesh file path", JSONValue(m_CurrentMeshFilePath));
		}

		parentObject.fields.emplace_back("soft body", JSONValue(softBodyObject));
	}

	void SoftBody::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		ImGuiExt::SliderUInt("Solver iteration count", &m_SolverIterationCount, 1, 60);

		ImGui::SameLine();

		if (ImGui::Button("Reset"))
		{
			u32 i = 0;
			for (Point* point : points)
			{
				point->pos = initialPositions[i++];
				point->vel = VEC3_ZERO;
			}
		}

		if (ImGui::Button("Single Step"))
		{
			m_bSingleStep = true;
			m_bPaused = true;
		}

		ImGui::Text("%.2fms", m_UpdateDuration);

		if (ImGui::Checkbox("Render wireframe", &m_bRenderWireframe))
		{
			std::vector<SoftBody*> softBodies = g_SceneManager->CurrentScene()->GetObjectsOfType<SoftBody>(SoftBodySID);
			for (SoftBody* softBody : softBodies)
			{
				softBody->m_bRenderWireframe = m_bRenderWireframe;
			}
		}

		if (ImGui::SliderFloat("Damping", &m_Damping, 0.0f, 1.0f))
		{
			std::vector<SoftBody*> softBodies = g_SceneManager->CurrentScene()->GetObjectsOfType<SoftBody>(SoftBodySID);
			for (SoftBody* softBody : softBodies)
			{
				softBody->m_Damping = m_Damping;
			}
		}

		if (ImGui::SliderFloat("Stiffness", &m_Stiffness, 0.0f, 1.0f))
		{
			std::vector<SoftBody*> softBodies = g_SceneManager->CurrentScene()->GetObjectsOfType<SoftBody>(SoftBodySID);
			for (SoftBody* softBody : softBodies)
			{
				for (Constraint* constraint : softBody->constraints)
				{
					if (constraint->type == Constraint::Type::DISTANCE)
					{
						constraint->stiffness = m_Stiffness;
					}
				}
			}
		}

		if (ImGui::SliderFloat("Bending stiffness", &m_BendingStiffness, 0.0f, 1.0f))
		{
			std::vector<SoftBody*> softBodies = g_SceneManager->CurrentScene()->GetObjectsOfType<SoftBody>(SoftBodySID);
			for (SoftBody* softBody : softBodies)
			{
				for (Constraint* constraint : softBody->constraints)
				{
					if (constraint->type == Constraint::Type::BENDING)
					{
						constraint->stiffness = m_BendingStiffness;
					}
				}
			}
		}

		if (ImGui::BeginCombo("##meshcombo", m_CurrentMeshFileName.c_str()))
		{
			for (i32 i = 0; i < (i32)g_ResourceManager->discoveredMeshes.size(); ++i)
			{
				ImGui::PushID(i);
				const std::string& meshFilePath = g_ResourceManager->discoveredMeshes[i];
				bool bSelected = (i == m_SelectedMeshIndex);
				const std::string meshFileNameShort = StripLeadingDirectories(meshFilePath);
				if (ImGui::Selectable(meshFileNameShort.c_str(), &bSelected))
				{
					if (m_SelectedMeshIndex != i)
					{
						m_SelectedMeshIndex = i;
						m_CurrentMeshFilePath = meshFilePath;
						m_CurrentMeshFileName = meshFileNameShort;
					}
				}

				ImGui::PopID();
			}

			ImGui::EndCombo();
		}

		ImGui::InputInt("Selected bending constraint", &m_ShownBendingIndex);

		if (m_SelectedMeshIndex != -1)
		{
			ImGui::SameLine();

			if (ImGui::Button("Load mesh"))
			{
				LoadFromMesh();
			}
		}
	}

	void SoftBody::SetStiffness(real stiffness)
	{
		m_Stiffness = stiffness;
	}

	void SoftBody::SetDamping(real damping)
	{
		m_Damping = damping;
	}

	void SoftBody::SetRenderWireframe(bool bRenderWireframe)
	{
		m_bRenderWireframe = bRenderWireframe;
	}
} // namespace flex
