#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH

#include <LinearMath/btIDebugDraw.h>

#include <glm/gtx/norm.hpp> // for distance2
#include <glm/gtx/quaternion.hpp> // for rotate
#include <glm/gtx/vector_angle.hpp> // for angle
#include <glm/ext/matrix_transform.hpp> // for rotate

#include <imgui/imgui_internal.h> // For PushItemFlag

IGNORE_WARNINGS_POP

#include "Scene/Objects/Wires.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Player.hpp"
#include "PropertyCollection.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/Objects/SoftBody.hpp"
#include "Scene/SceneManager.hpp"
#include "StringBuilder.hpp"

namespace flex
{
	const real WirePlug::nearbyThreshold = 3.0f;
	const real Wire::DEFAULT_LENGTH = 2.0f;

	Wire::Wire(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, WireSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		if (!m_bIsTemplate)
		{
			GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->RegisterWire(this);
		}
	}

	PropertyCollection* Wire::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("wire");

		collection->childCollection->RegisterProperty("plug 0 id", FIELD_OFFSET_AND_TYPE(Wire, plug0ID))
			.VersionAdded(6);
		collection->childCollection->RegisterProperty("plug 1 id", FIELD_OFFSET_AND_TYPE(Wire, plug1ID))
			.VersionAdded(6);

		return collection;
	}

	void Wire::Initialize()
	{
		GeneratePoints();

		MaterialID wireMatID;
		if (!g_Renderer->FindOrCreateMaterialByName("wire", wireMatID))
		{
			PrintWarn("Failed to find wire material, using placeholder\n");
			wireMatID = g_Renderer->GetPlaceholderMaterialID();
		}
		m_VertexBufferCreateInfo = {};
		m_VertexBufferCreateInfo.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(wireMatID)->shaderID)->vertexAttributes;

		UpdateIndices();

		SetMesh(new Mesh(this));
		Mesh::CreateInfo meshCreateInfo = {};
		meshCreateInfo.bDynamic = true;
		meshCreateInfo.vertexBufferCreateInfo = &m_VertexBufferCreateInfo;
		meshCreateInfo.indices = m_Indices;
		meshCreateInfo.materialIDs = { wireMatID };
		meshCreateInfo.initialMaxVertexCount = numRadialPoints * numPoints;
		m_Mesh->LoadFromMemory(meshCreateInfo);

		GameObject::Initialize();
	}

	void Wire::PostInitialize()
	{
		if (plug0ID.IsValid())
		{
			WirePlug* plug0 = (WirePlug*)plug0ID.Get();
			if (!plug0->wireID.IsValid())
			{
				plug0->wireID = ID;
			}
			else
			{
				if (plug0->wireID != ID)
				{
					StringBuilder plugPath;
					plug0->GetFullyPathedName(plugPath);
					PrintError("Wire plug \"%s\" references unexpected wire\n", plugPath.ToCString());
				}
			}
		}

		if (plug1ID.IsValid())
		{
			WirePlug* plug1 = (WirePlug*)plug1ID.Get();
			if (!plug1->wireID.IsValid())
			{
				plug1->wireID = ID;
			}
			else
			{
				if (plug1->wireID != ID)
				{
					StringBuilder plugPath;
					plug1->GetFullyPathedName(plugPath);
					PrintError("Wire plug \"%s\" references unexpected wire\n", plugPath.ToCString());
				}
			}
		}


		GameObject::PostInitialize();
	}

	void Wire::Update()
	{
		PROFILE_AUTO("Wire Update");

		UpdateIndices();
		// Uploads the new vertex data
		UpdateMesh();

		GameObject::Update();
	}

	void Wire::Destroy(bool bDetachFromParent /* = true */)
	{
		if (!m_bIsTemplate)
		{
			GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->UnregisterWire(this);
		}

		DestroyPoints();

		GameObject::Destroy(bDetachFromParent);
	}

	void Wire::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		bool bRegenPoints = false;

		if (ImGui::SliderInt("num points", &numPoints, 4, 24))
		{
			bRegenPoints = true;
			numPoints = glm::clamp(numPoints, 4, 24);
		}
		bRegenPoints = ImGui::SliderFloat("point inv mass", &pointInvMass, 0.0f, 0.1f) || bRegenPoints;

		if (ImGui::SliderFloat("stiffness", &stiffness, 0.0f, 1.0f))
		{
			m_SoftBody->SetStiffness(stiffness);
		}

		if (ImGui::SliderFloat("damping", &damping, 0.0f, 1.0f))
		{
			m_SoftBody->SetDamping(damping);
		}

		if (ImGui::Button("Reset points") || bRegenPoints)
		{
			DestroyPoints();
			GeneratePoints();
			UpdateIndices();
			UpdateMesh();
		}
	}

	bool Wire::ShouldSerialize()
	{
		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player != nullptr)
		{
			if (player->IsHolding(plug0ID.Get()) || player->IsHolding(plug1ID.Get()))
			{
				// Don't serialize wires being held
				return false;
			}
		}

		return true;
	}

	void Wire::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject wireInfo;
		if (parentObject.TryGetObject("wire", wireInfo))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(wireInfo, collection->childCollection);
		}
	}

	void Wire::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject wireInfo = {};
		SerializeRegisteredProperties(wireInfo, collection->childCollection, bSerializePrefabData);
		if (!wireInfo.fields.empty())
		{
			parentObject.fields.emplace_back("wire", JSONValue(wireInfo));
		}
	}

	GameObjectID Wire::GetOtherPlug(WirePlug* plug)
	{
		if (plug->ID == plug0ID)
		{
			return plug1ID;
		}
		else if (plug->ID == plug1ID)
		{
			return plug0ID;
		}

		return InvalidGameObjectID;
	}

	void Wire::SetStartTangent(const glm::vec3& tangent)
	{
		real dist = 2.0f / numPoints;
		m_SoftBody->points[1]->pos = m_SoftBody->points[0]->pos + tangent * dist;
		m_SoftBody->points[1]->invMass = 0.0f;
	}

	void Wire::ClearStartTangent()
	{
		m_SoftBody->points[1]->invMass = m_SoftBody->points[2]->invMass;
	}

	void Wire::SetEndTangent(const glm::vec3& tangent)
	{
		real dist = 2.0f / numPoints;
		m_SoftBody->points[numPoints - 2]->pos = m_SoftBody->points[numPoints - 1]->pos + tangent * dist;
		m_SoftBody->points[numPoints - 2]->invMass = 0.0f;
	}

	void Wire::ClearEndTangent()
	{
		m_SoftBody->points[numPoints - 2]->invMass = m_SoftBody->points[numPoints - 3]->invMass;
	}

	void Wire::UpdateWireMesh()
	{
		CHECK_NE(m_SoftBody, nullptr);
		WirePlug* plug0 = (WirePlug*)plug0ID.Get();
		Transform* plug0Transform = plug0->GetTransform();
		WirePlug* plug1 = (WirePlug*)plug1ID.Get();
		Transform* plug1Transform = plug1->GetTransform();

		m_SoftBody->points[0]->pos = plug0Transform->GetWorldPosition();
		m_SoftBody->points[m_SoftBody->points.size() - 1]->pos = plug1Transform->GetWorldPosition();

		m_SoftBody->Update();
	}

	glm::vec3 Wire::CalculateTangentAtPoint(real t)
	{
		CHECK_NE(m_SoftBody, nullptr);
		CHECK(t >= 0.0f && t <= 1.0f);
		if (t == 1.0f)
		{
			return glm::normalize(m_SoftBody->points[numPoints - 1]->pos - m_SoftBody->points[numPoints - 2]->pos);
		}
		else
		{
			u32 i0 = (u32)(t * numPoints);
			u32 i1 = (u32)(t * numPoints + 1.0f);
			return glm::normalize(m_SoftBody->points[i1]->pos - m_SoftBody->points[i0]->pos);
		}
	}

	void Wire::CalculateBasisAtPoint(real t, glm::vec3& outNormal, glm::vec3& outTangent, glm::vec3& outBitangent)
	{
		CHECK_NE(m_SoftBody, nullptr);
		CHECK(t >= 0.0f && t <= 1.0f);
		if (t == 1.0f)
		{
			outTangent = glm::normalize(m_SoftBody->points[numPoints - 1]->pos - m_SoftBody->points[numPoints - 2]->pos);
		}
		else
		{
			u32 i0 = (u32)(t * numPoints);
			u32 i1 = (u32)(t * numPoints + 1.0f);
			outTangent = glm::normalize(m_SoftBody->points[i1]->pos - m_SoftBody->points[i0]->pos);
		}
		// TODO: Take tangent with VEC3_RIGHT when UP is similar to tangent
		outBitangent = glm::normalize(glm::cross(VEC3_UP, outTangent));
		outNormal = glm::normalize(glm::cross(outTangent, outBitangent));
		outBitangent = glm::normalize(glm::cross(outNormal, outTangent));
	}

	void Wire::DestroyPoints()
	{
		if (m_SoftBody != nullptr)
		{
			m_SoftBody->Destroy(true);
			delete m_SoftBody;
			m_SoftBody = nullptr;
		}
	}

	void Wire::GeneratePoints()
	{
		CHECK_EQ(m_SoftBody, nullptr);

		WirePlug* plug0 = (WirePlug*)plug0ID.Get();
		Transform* plug0Transform = plug0->GetTransform();
		WirePlug* plug1 = (WirePlug*)plug1ID.Get();
		Transform* plug1Transform = plug1->GetTransform();

		Point* startPoint = new Point(plug0Transform->GetWorldPosition(), VEC3_ZERO, 0.0f);
		Point* endPoint = new Point(plug1Transform->GetWorldPosition(), VEC3_ZERO, 0.0f);

		m_SoftBody = new SoftBody("Wire simulation");
		m_SoftBody->SetRenderWireframe(false);

		m_SoftBody->points.reserve(numPoints);
		for (i32 i = 0; i < numPoints; ++i)
		{
			real invMass = (i == 0 || i == (numPoints - 1)) ? 0.0f : pointInvMass;
			real t = (real)i / (numPoints - 1);
			glm::vec3 pos = startPoint->pos + (endPoint->pos - startPoint->pos) * t;
			m_SoftBody->points.push_back(new Point(pos, VEC3_ZERO, invMass));
		}

		for (i32 i = 0; i < numPoints - 1; ++i)
		{
			m_SoftBody->AddUniqueDistanceConstraint(i, i + 1, i, stiffness);
		}

		m_SoftBody->SetSerializable(false);
		m_SoftBody->SetVisibleInSceneExplorer(false);
		m_SoftBody->SetOwnedInternally(true);
		m_SoftBody->SetStiffness(stiffness);
		m_SoftBody->SetDamping(damping);
		m_SoftBody->Initialize();
		m_SoftBody->PostInitialize();
	}

	void Wire::UpdateMesh()
	{
		u32 numVerts = numPoints * numRadialPoints;

		m_VertexBufferCreateInfo.positions_3D.clear();
		m_VertexBufferCreateInfo.texCoords_UV.clear();
		m_VertexBufferCreateInfo.colours_R32G32B32A32.clear();
		m_VertexBufferCreateInfo.normals.clear();
		m_VertexBufferCreateInfo.tangents.clear();

		m_VertexBufferCreateInfo.positions_3D.resize(numVerts);
		m_VertexBufferCreateInfo.texCoords_UV.resize(numVerts);
		m_VertexBufferCreateInfo.colours_R32G32B32A32.resize(numVerts);
		m_VertexBufferCreateInfo.normals.resize(numVerts);
		m_VertexBufferCreateInfo.tangents.resize(numVerts);

		glm::vec3 wirePosWS = m_Transform.GetWorldPosition();

		u32 vertIndex = 0;
		real radius = 0.05f;
		glm::vec4 color(0.3f, 0.3f, 0.35f, 1.0f);
		for (i32 i = 0; i < numPoints; ++i)
		{
			real t = i / (real)(numPoints - 1);
			glm::vec3 centerPos = m_SoftBody->points[i]->pos - wirePosWS;
			glm::vec3 wireNormalWS, wireTangentWS, wireBitangentWS;
			CalculateBasisAtPoint(t, wireNormalWS, wireTangentWS, wireBitangentWS);
			for (i32 j = 0; j < numRadialPoints; ++j)
			{
				real theta = j / (real)numRadialPoints;
				real cosTheta = cos(theta * TWO_PI);
				real sinTheta = sin(theta * TWO_PI);
				glm::vec3 pos = centerPos +
					(cosTheta * radius) * wireBitangentWS +
					(sinTheta * radius) * wireNormalWS;

				glm::vec3 normal = glm::normalize(cosTheta * wireBitangentWS + sinTheta * wireNormalWS);
				// TODO: Take cross product with VEC3_UP when normal is similar to VEC3_RIGHT
				glm::vec3 tangent = glm::normalize(glm::cross(normal, VEC3_RIGHT));

				m_VertexBufferCreateInfo.positions_3D[vertIndex] = pos;
				m_VertexBufferCreateInfo.texCoords_UV[vertIndex] = glm::vec2(theta, t);
				m_VertexBufferCreateInfo.colours_R32G32B32A32[vertIndex] = color;
				m_VertexBufferCreateInfo.normals[vertIndex] = normal;
				m_VertexBufferCreateInfo.tangents[vertIndex] = tangent;

				++vertIndex;
			}
		}

		if (!m_Indices.empty())
		{
			MeshComponent* meshComponent = m_Mesh->GetSubMesh(0);
			meshComponent->UpdateDynamicVertexData(m_VertexBufferCreateInfo, m_Indices);
			g_Renderer->ShrinkDynamicVertexData(meshComponent->renderID, 0.5f);
		}
	}

	void Wire::UpdateIndices()
	{
		u32 numIndices = (numPoints - 1) * numRadialPoints * 6;

		m_Indices.clear();
		m_Indices.resize(numIndices);

		i32 indexIndex = 0;
		for (i32 i = 0; i < numPoints - 1; ++i)
		{
			for (i32 j = 0; j < numRadialPoints; ++j)
			{
				m_Indices[indexIndex++] = i * numRadialPoints + j;
				m_Indices[indexIndex++] = (i + 1) * numRadialPoints + (j + 1) % numRadialPoints;
				m_Indices[indexIndex++] = (i + 1) * numRadialPoints + j;

				m_Indices[indexIndex++] = i * numRadialPoints + j;
				m_Indices[indexIndex++] = i * numRadialPoints + (j + 1) % numRadialPoints;
				m_Indices[indexIndex++] = (i + 1) * numRadialPoints + (j + 1) % numRadialPoints;
			}
		}
	}

	WirePlug::WirePlug(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, WirePlugSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		m_bInteractable = true;

		if (!m_bIsTemplate)
		{
			GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->RegisterWirePlug(this);
		}
	}

	PropertyCollection* WirePlug::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("wire plug");

		collection->childCollection->RegisterProperty("wire id", FIELD_OFFSET_AND_TYPE(WirePlug, wireID))
			.VersionAdded(6)
			.DefaultValue(InvalidGameObjectID);
		collection->childCollection->RegisterProperty("socket id", FIELD_OFFSET_AND_TYPE(WirePlug, socketID))
			.VersionAdded(6)
			.DefaultValue(InvalidGameObjectID);

		return collection;
	}

	WirePlug::WirePlug(const std::string& name, Wire* owningWire, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		WirePlug(name, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		wireID = owningWire->ID;
	}

	void WirePlug::Destroy(bool bDetachFromParent)
	{
		if (!m_bIsTemplate)
		{
			GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->UnregisterWirePlug(this);
		}

		GameObject::Destroy(bDetachFromParent);
	}

	void WirePlug::PlugIn(Socket* socket)
	{
		if (!socketID.IsValid())
		{
			socketID = socket->ID;
		}
		else
		{
			PrintError("Attempted to plug in to full socket!\n");
		}
	}

	void WirePlug::Unplug()
	{
		if (socketID.IsValid())
		{
			socketID = InvalidGameObjectID;
		}
		else
		{
			PrintError("Attempted to unplug socket that wasn't plugged in\n");
		}
	}

	void WirePlug::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject wireObj;
		if (parentObject.TryGetObject("wire plug", wireObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(wireObj, collection->childCollection);
		}
	}

	void WirePlug::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject wirePlugObj = {};
		SerializeRegisteredProperties(wirePlugObj, collection->childCollection, bSerializePrefabData);
		if (!wirePlugObj.fields.empty())
		{
			parentObject.fields.emplace_back("wire plug", JSONValue(wirePlugObj));
		}
	}

	Socket::Socket(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, SocketSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		m_bInteractable = true;

		if (!m_bIsTemplate)
		{
			GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->RegisterSocket(this);
		}
	}

	PropertyCollection* Socket::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("socket");

		collection->childCollection->RegisterProperty("slotIdx", FIELD_OFFSET_AND_TYPE(Socket, slotIdx))
			.VersionAdded(6);
		//	.CustomSerializeCallback([](SerializationContext& context, u32 value)
		//{
		//	if (value > 0)
		//	{
		//		context.Write("foo", value);
		//	}
		//});
		collection->childCollection->RegisterProperty("connected plug id", FIELD_OFFSET_AND_TYPE(Socket, connectedPlugID))
			.VersionAdded(6)
			.DefaultValue(InvalidGameObjectID);

		return collection;
	}

	void Socket::Destroy(bool bDetachFromParent /* = true */)
	{
		if (!m_bIsTemplate)
		{
			GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->UnregisterSocket(this);
		}

		GameObject::Destroy(bDetachFromParent);
	}

	void Socket::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject socketObj;
		if (parentObject.TryGetObject("socket", socketObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(socketObj, collection->childCollection);
		}
	}

	void Socket::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject socketObj = {};
		SerializeRegisteredProperties(socketObj, collection->childCollection, bSerializePrefabData);
		if (!socketObj.fields.empty())
		{
			parentObject.fields.emplace_back("socket", JSONValue(socketObj));
		}
	}

	void Socket::OnPlugIn(WirePlug* plug)
	{
		if (!connectedPlugID.IsValid())
		{
			connectedPlugID = plug->ID;
		}
		else
		{
			PrintError("Attempted to plug socket in when already full\n");
		}
	}

	void Socket::OnUnPlug()
	{
		if (connectedPlugID.IsValid())
		{
			connectedPlugID = InvalidGameObjectID;
		}
		else
		{
			PrintError("Attempted to unplug from socket which had nothing plugged in\n");
		}
	}
} // namespace flex
