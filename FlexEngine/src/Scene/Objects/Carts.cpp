#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH

#include <glm/gtx/norm.hpp> // for distance2
#include <glm/gtx/quaternion.hpp> // for rotate
#include <glm/gtx/vector_angle.hpp> // for angle
#include <glm/ext/matrix_transform.hpp> // for rotate

#include <imgui/imgui_internal.h> // For PushItemFlag

IGNORE_WARNINGS_POP

#include "Scene/Objects/Carts.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Player.hpp"
#include "PropertyCollection.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/SceneManager.hpp"
#include "Systems/CartManager.hpp"
#include "Systems/TrackManager.hpp"

namespace flex
{
	const char* BaseCart::emptyCartMeshName = "cart-empty.glb";
	const char* EngineCart::engineMeshName = "cart-engine.glb";

	BaseCart::BaseCart(const std::string& name,
		StringID typeID,
		const GameObjectID& gameObjectID,
		const char* meshName,
		const PrefabIDPair& sourcePrefabID,
		bool bPrefabTemplate) :
		GameObject(name, typeID, gameObjectID, sourcePrefabID, bPrefabTemplate)
	{
		m_bItemizable = true;

		MaterialID matID;
		if (!g_Renderer->FindOrCreateMaterialByName("pbr grey", matID))
		{
			// :shrug:
			// TODO: Create own material
			matID = 0;
		}
		Mesh* mesh = SetMesh(new Mesh(this));
		Mesh::CreateInfo meshCreateInfo = {};
		meshCreateInfo.relativeFilePath = std::string(MESH_DIRECTORY) + std::string(meshName);
		meshCreateInfo.materialIDs = { matID };
		meshCreateInfo.bCreateRenderObject = !bPrefabTemplate;
		if (!mesh->LoadFromFile(meshCreateInfo))
		{
			PrintWarn("Failed to load cart mesh!\n");
		}

		m_TSpringToCartAhead.DR = 1.0f;
	}

	PropertyCollection* BaseCart::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("cart info");

		collection->childCollection->RegisterProperty("track ID", FIELD_OFFSET_AND_TYPE(BaseCart, currentTrackID))
			.VersionAdded(6)
			.DefaultValue(InvalidTrackID);
		collection->childCollection->RegisterProperty("dist along track", FIELD_OFFSET_AND_TYPE(BaseCart, distAlongTrack))
			.VersionAdded(6)
			.DefaultValue(-1.0f);

		return collection;
	}

	void BaseCart::Initialize()
	{
		GetSystem<CartManager>(SystemType::CART_MANAGER)->RegisterCart(this);

		GameObject::Initialize();
	}

	void BaseCart::Destroy(bool bDetachFromParent)
	{
		GetSystem<CartManager>(SystemType::CART_MANAGER)->DeregisterCart(this);

		GameObject::Destroy(bDetachFromParent);
	}

	void BaseCart::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		if (ImGui::TreeNode("BaseCart"))
		{
			ImGui::Text("track ID: %d", currentTrackID);
			ImGui::Text("dist along track: %.2f", distAlongTrack);

			ImGui::TreePop();
		}
	}

	real BaseCart::GetDrivePower() const
	{
		return 0.0f;
	}

	void BaseCart::OnTrackMount(TrackID trackID, real newDistAlongTrack)
	{
		if (trackID == InvalidTrackID)
		{
			PrintWarn("Attempted to attach cart to track with invalid ID!\n");
		}
		else
		{
			currentTrackID = trackID;

			TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
			i32 curveIndex, juncIndex;
			TrackID newTrackID = trackID;
			TrackState trackState;
			glm::vec3 newPos = trackManager->GetPointOnTrack(currentTrackID, newDistAlongTrack, newDistAlongTrack,
				LookDirection::CENTER, false, &newTrackID, &newDistAlongTrack, &juncIndex, &curveIndex, &trackState, false);
			CHECK_EQ(newTrackID, trackID);

			distAlongTrack = newDistAlongTrack;
			m_Transform.SetLocalPosition(newPos);

			velocityT = (distAlongTrack > 0.5f ? -1.0f : 1.0f);
		}
	}

	void BaseCart::OnTrackDismount()
	{
		currentTrackID = InvalidTrackID;
		distAlongTrack = -1.0f;
	}

	void BaseCart::SetItemHolding(GameObject* obj)
	{
		FLEX_UNUSED(obj);
		// TODO:
	}

	void BaseCart::RemoveItemHolding()
	{
	}

	void BaseCart::AdvanceAlongTrack(real dT)
	{
		if (currentTrackID != InvalidTrackID)
		{
			TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);

			real pDistAlongTrack = distAlongTrack;
			distAlongTrack = trackManager->AdvanceTAlongTrack(currentTrackID, dT, distAlongTrack);

			real newDistAlongTrack;
			i32 curveIndex;
			i32 juncIndex;
			TrackID newTrackID = InvalidTrackID;
			TrackState trackState;
			glm::vec3 newPos = trackManager->GetPointOnTrack(currentTrackID, distAlongTrack, pDistAlongTrack,
				LookDirection::CENTER, false, &newTrackID, &newDistAlongTrack, &juncIndex, &curveIndex, &trackState, false);

			bool bSwitchedTracks = (newTrackID != InvalidTrackID) && (currentTrackID != newTrackID);
			if (bSwitchedTracks)
			{
				currentTrackID = newTrackID;
				distAlongTrack = newDistAlongTrack;

				velocityT = (distAlongTrack > 0.5f ? -1.0f : 1.0f);
			}

			if (currentTrackID != InvalidTrackID)
			{
				glm::vec3 trackF = trackManager->GetTrack(currentTrackID)->GetCurveDirectionAt(distAlongTrack);
				glm::quat pRot = m_Transform.GetWorldRotation();
				glm::quat newRot = glm::quatLookAt(trackF, VEC3_UP);
				// TODO: Make frame-rate independent
				m_Transform.SetWorldRotation(glm::slerp(pRot, newRot, 0.5f));
			}

			m_Transform.SetWorldPosition(newPos);
		}
	}

	real BaseCart::UpdatePosition()
	{
		if (currentTrackID != InvalidTrackID)
		{
			if (chainID != InvalidCartChainID)
			{
				TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
				CartManager* cartManager = GetSystem<CartManager>(SystemType::CART_MANAGER);

				//real targetT = trackManager->GetCartTargetDistAlongTrackInChain(chainID, cartID);
				//CHECK_NE(targetT, -1.0f);

				//m_TSpringToCartAhead.targetPos = targetT;
				//m_TSpringToCartAhead.Tick(g_DeltaTime);
				//real dT = (m_TSpringToCartAhead.pos - distAlongTrack);

				//real newDistAlongTrack = trackManager->AdvanceTAlongTrack(currentTrackID, g_DeltaTime*0.1f, distAlongTrack);
				//real dT = newDistAlongTrack - distAlongTrack;

				//real minDist = 0.05f;
				//real d = (targetT - distAlongTrack);
				//real stepSize = 0.08f;
				//if (velocityT < 0.0f && targetT > 0.75f && distAlongTrack < 0.25f)
				//{
				//	// Target has crossed 0.0->1.0
				//	d = (targetT - (distAlongTrack + 1.0f));

				//}
				//else if (velocityT > 0.0f && targetT < 0.25f && distAlongTrack > 0.75f)
				//{
				//	// Target has crossed 1.0->0.0
				//	d = (targetT - (distAlongTrack - 1.0f));
				//}

				//{
				//	if (glm::abs(d) <= minDist)
				//	{
				//		targetT = d >= 0.0f ? targetT - minDist : targetT + minDist;
				//		d = (targetT - distAlongTrack);
				//	}
				//	if (d < 0.0f)
				//	{
				//		stepSize = -stepSize;
				//	}
				//}
				//real dA = d >= 0.0f ? std::min(d, stepSize) : -std::min(-d, -stepSize);
				//Print("%.2f, %.2f, %.2f\n", targetT, distAlongTrack, dA);

				//velocityT += dA * g_DeltaTime;
				//real dT = velocityT * g_DeltaTime;

				//real pDistAlongTrack = distAlongTrack;
				//real newDistAlongTrack = trackManager->AdvanceTAlongTrack(currentTrackID, g_DeltaTime*chainDrivePower, distAlongTrack);

				//TrackID newTrackID;
				//real newDistAlongTrack;
				//i32 newJunctionIdx;
				//i32 newCurveIdx;
				//TrackState newTrackState;
				//trackManager->GetPointOnTrack(currentTrackID, newDistAlongTrack, distAlongTrack, LookDirection::CENTER, false, &newTrackID, &newDistAlongTrack, &newJunctionIdx, &newCurveIdx, &newTrackState, true);

				//if (newTrackID != currentTrackID)
				//{
				//	currentTrackID = newTrackID;
				//}

				//distAlongTrack = newDistAlongTrack;


				const CartChain* chain = cartManager->GetCartChain(chainID);
				i32 cartInChainIndex = chain->GetCartIndex(cartID);

				//real pDistToRearNeighbor = distToRearNeighbor;
				if (cartInChainIndex < (i32)chain->carts.size() - 1)
				{
					// Not at end
					distToRearNeighbor = trackManager->GetCartTargetDistAlongTrackInChain(chainID, chain->carts[cartInChainIndex + 1]);
				}

				//real neighborDT = distToRearNeighbor - pDistToRearNeighbor;

				real chainDrivePower = cartManager->GetChainDrivePower(chainID);

				if (distToRearNeighbor > -1.0f && distToRearNeighbor < 0.2f)
				{
					chainDrivePower += 1.0f;
				}


				real dT = g_DeltaTime * chainDrivePower * velocityT;
				AdvanceAlongTrack(dT);
				return dT;
			}
		}

		return 0.0f;
	}

	void BaseCart::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject cartInfo;
		if (parentObject.TryGetObject("cart info", cartInfo))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(cartInfo, collection->childCollection);
		}
	}

	void BaseCart::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject cartInfo = {};
		SerializeRegisteredProperties(cartInfo, collection->childCollection, bSerializePrefabData);
		if (!cartInfo.fields.empty())
		{
			parentObject.fields.emplace_back("cart info", JSONValue(cartInfo));
		}
	}

	EmptyCart::EmptyCart(const std::string& name,
		const GameObjectID& gameObjectID,
		const PrefabIDPair& sourcePrefabID,
		bool bPrefabTemplate) :
		BaseCart(name, EmptyCartSID, gameObjectID, BaseCart::emptyCartMeshName, sourcePrefabID, bPrefabTemplate)
	{
	}

	PropertyCollection* EmptyCart::BuildTypeUniquePropertyCollection()
	{
		return BaseCart::BuildTypeUniquePropertyCollection();
	}

	GameObject* EmptyCart::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);

		// TODO: FIXME: Get newly generated cart ID! & move allocation into cart manager
		bool bCopyingToPrefabTemplate = copyFlags & CopyFlags::COPYING_TO_PREFAB;
		EmptyCart* newGameObject = new EmptyCart(newObjectName, newGameObjectID, m_SourcePrefabID, bCopyingToPrefabTemplate);

		newGameObject->currentTrackID = currentTrackID;
		newGameObject->distAlongTrack = distAlongTrack;

		copyFlags = (CopyFlags)(copyFlags & ~CopyFlags::MESH);
		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	EngineCart::EngineCart(const std::string& name,
		const GameObjectID& gameObjectID,
		const PrefabIDPair& sourcePrefabID,
		bool bPrefabTemplate) :
		BaseCart(name, EngineCartSID, gameObjectID, engineMeshName, sourcePrefabID, bPrefabTemplate)
	{
		// TODO: Serialize and expose in ImGui
		m_bItemizable = true;
	}

	PropertyCollection* EngineCart::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = BaseCart::BuildTypeUniquePropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("engine cart info");

		// BaseCart already registered trackID & dist along track fields
		collection->childCollection->RegisterProperty("move direction", FIELD_OFFSET_AND_TYPE(EngineCart, moveDirection))
			.VersionAdded(6)
			.Precision(1)
			.DefaultValue(1.0f);
		collection->childCollection->RegisterProperty("power remaining", FIELD_OFFSET_AND_TYPE(EngineCart, powerRemaining))
			.VersionAdded(6)
			.DefaultValue(1.0f);

		return collection;
	}

	GameObject* EngineCart::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);

		bool bCopyingToPrefabTemplate = copyFlags & CopyFlags::COPYING_TO_PREFAB;
		// TODO: FIXME: Get newly generated cart ID! & move allocation into cart manager
		EngineCart* newGameObject = new EngineCart(newObjectName, newGameObjectID, m_SourcePrefabID, bCopyingToPrefabTemplate);

		newGameObject->powerRemaining = powerRemaining;

		newGameObject->currentTrackID = currentTrackID;
		newGameObject->distAlongTrack = distAlongTrack;

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void EngineCart::Update()
	{
		PROFILE_AUTO("EngineCart Update");

		powerRemaining -= powerDrainMultiplier * g_DeltaTime;
		powerRemaining = glm::max(powerRemaining, 0.0f);

		if (chainID == InvalidCartChainID)
		{
			real dT = g_DeltaTime * GetDrivePower();
			AdvanceAlongTrack(dT);
		}

		if (currentTrackID != InvalidTrackID && powerRemaining > 0.0f)
		{
			TrackID pTrackID = currentTrackID;

			bool bSwitchedTracks = (currentTrackID != pTrackID);
			if (bSwitchedTracks)
			{
				if (distAlongTrack > 0.5f)
				{
					moveDirection = -1.0f;
				}
				else
				{
					moveDirection = 1.0f;
				}
			}
			else if (distAlongTrack == -1.0f ||
				(distAlongTrack == 1.0f && moveDirection > 0.0f) ||
				(distAlongTrack == 0.0f && moveDirection < 0.0f))
			{
				moveDirection = -moveDirection;
			}
		}

		GameObject::Update();
	}

	void EngineCart::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		if (ImGui::TreeNode("Engine BaseCart"))
		{
			ImGui::Text("track ID: %d", currentTrackID);
			ImGui::Text("dist along track: %.2f", distAlongTrack);
			ImGui::Text("move direction: %.2f", moveDirection);
			ImGui::Text("power remaining: %.2f", powerRemaining);

			ImGui::TreePop();
		}
	}

	real EngineCart::GetDrivePower() const
	{
		return (1.0f - glm::pow(1.0f - powerRemaining, 5.0f)) * moveDirection * speed;
	}

	void EngineCart::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject cartInfo;
		// TODO: Also call BaseCart::PartTypeUniqueFields?
		if (parentObject.TryGetObject("engine cart info", cartInfo))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(cartInfo, collection->childCollection);
		}
	}

	void EngineCart::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject cartInfo = {};
		// TODO: Also call BaseCart::SerializeTypeUniqueFields?
		SerializeRegisteredProperties(cartInfo, collection->childCollection, bSerializePrefabData);
		if (!cartInfo.fields.empty())
		{
			parentObject.fields.emplace_back("engine cart info", JSONValue(cartInfo));
		}
	}

	MobileLiquidBox::MobileLiquidBox() :
		// TODO: Extract string to m_UniqueObjectNamePrefix
		MobileLiquidBox(g_SceneManager->CurrentScene()->GetUniqueObjectName("MobileLiquidBox_", 4), InvalidGameObjectID)
	{
	}

	MobileLiquidBox::MobileLiquidBox(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, MobileLiquidBoxSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		MaterialID matID;
		if (!g_Renderer->FindOrCreateMaterialByName("pbr white", matID))
		{
			// TODO: Create own material
			matID = 0;
		}
		Mesh* mesh = SetMesh(new Mesh(this));
		if (!mesh->LoadFromFile(MESH_DIRECTORY "mobile-liquid-box.glb", matID))
		{
			PrintWarn("Failed to load mobile-liquid-box mesh!\n");
		}
	}

	PropertyCollection* MobileLiquidBox::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();
		return collection;
	}

	GameObject* MobileLiquidBox::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		MobileLiquidBox* newGameObject = new MobileLiquidBox(newObjectName, newGameObjectID);

		newGameObject->bInCart = bInCart;
		newGameObject->liquidAmount = liquidAmount;

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void MobileLiquidBox::Initialize()
	{
		// TODO: Query from CartManager?
		BaseScene* scene = g_SceneManager->CurrentScene();
		std::vector<BaseCart*> carts = scene->GetObjectsOfType<BaseCart>(BaseCartSID);
		Player* player = scene->GetPlayer(0);
		if (player != nullptr)
		{
			glm::vec3 playerPos = player->GetTransform()->GetWorldPosition();
			real threshold = 8.0f;
			real closestCartDist = threshold;
			i32 closestCartIdx = -1;
			for (i32 i = 0; i < (i32)carts.size(); ++i)
			{
				real d = glm::distance(carts[i]->GetTransform()->GetWorldPosition(), playerPos);
				if (d <= closestCartDist)
				{
					closestCartDist = d;
					closestCartIdx = i;
				}
			}

			if (closestCartIdx != -1)
			{
				carts[closestCartIdx]->AddChild(this);
				m_Transform.SetLocalPosition(glm::vec3(0.0f, 1.5f, 0.0f));
				SetVisible(true);
			}
		}
	}

	void MobileLiquidBox::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		if (ImGui::TreeNode("Mobile liquid box"))
		{
			ImGui::Text("In cart: %d", bInCart ? 1 : 0);
			ImGui::Text("Liquid amount: %.2f", liquidAmount);

			ImGui::TreePop();
		}
	}
} // namespace flex
