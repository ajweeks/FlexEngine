#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <glm/gtx/norm.hpp> // for distance2
#include <glm/gtx/quaternion.hpp> // for rotate
#include <glm/gtx/vector_angle.hpp> // for angle
#include <glm/ext/matrix_transform.hpp> // for rotate

#include <imgui/imgui_internal.h> // For PushItemFlag

IGNORE_WARNINGS_POP

#include "Scene/GameObject.hpp"

#include "Editor.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Physics/RigidBody.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/Objects/Props.hpp"
#include "Scene/Objects/Wires.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

// Editor UI for game objects (inspector, context menus)
namespace flex
{
	void GameObject::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		if (!IsVisibleInSceneExplorer())
		{
			return;
		}

		ImGui::PushID(this);

		DrawImGuiForSelfInternal(bDrawingEditorObjects);

		ImGui::PopID();
	}

	void GameObject::DrawImGuiForSelfInternal(bool bDrawingEditorObjects)
	{
		// ImGui::PushID will have already been called, making names not need to be quailfied for uniqueness

		bool bAnyPropertyChanged = false;

		const char* typeStr = BaseScene::GameObjectTypeIDToString(m_TypeID);
		ImGui::Text("%s : %s %s", m_Name.c_str(), typeStr, m_SourcePrefabID.IsValid() ? "(prefab)" : "");

		if (m_SourcePrefabID.IsValid())
		{
			char prefabIDStrBuff[33];
			m_SourcePrefabID.m_PrefabID.ToString(prefabIDStrBuff);
			std::string prefabLoadedFromFilePath = g_ResourceManager->GetPrefabFileName(m_SourcePrefabID.m_PrefabID);
			ImGui::Text("Source prefab: %s (%s)", prefabLoadedFromFilePath.c_str(), prefabIDStrBuff);
		}

		if (DoImGuiContextMenu(true))
		{
			// Early return if object was just deleted
			return;
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();

			char buffer[33];
			ID.ToString(buffer);
			ImGui::Text("%s", buffer);

			if (m_SourcePrefabID.IsValid())
			{
				char prefabIDStrBuff[33];
				m_SourcePrefabID.m_PrefabID.ToString(prefabIDStrBuff);
				std::string prefabLoadedFromFilePath = g_ResourceManager->GetPrefabFileName(m_SourcePrefabID.m_PrefabID);
				ImGui::Text("Prefab source: %s (%s)", prefabLoadedFromFilePath.c_str(), prefabIDStrBuff);
			}

			ImGui::EndTooltip();
		}

		const std::string objectVisibleLabel("Visible");
		bool bVisible = m_bVisible;
		if (ImGui::Checkbox(objectVisibleLabel.c_str(), &bVisible))
		{
			bAnyPropertyChanged = true;
			SetVisible(bVisible);
		}

		ImGui::SameLine();

		bool bStatic = m_bStatic;
		if (ImGui::Checkbox("Static", &bStatic))
		{
			bAnyPropertyChanged = true;
			SetStatic(bStatic);
		}

		PropertyCollection* propertyCollection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);
		if (propertyCollection != nullptr)
		{
			bAnyPropertyChanged = propertyCollection->DrawImGuiForObject(this) || bAnyPropertyChanged;
		}

		// Transform
		ImGui::Text("Transform");
		{
			// TODO: Add local/global switch
			// TODO: EZ: Ensure precision is retained
			if (ImGui::BeginPopupContextItem("transform context menu"))
			{
				if (ImGui::Button("Copy"))
				{
					CopyTransformToClipboard(&m_Transform);
					ImGui::CloseCurrentPopup();
				}

				ImGui::SameLine();

				if (ImGui::Button("Paste"))
				{
					bAnyPropertyChanged = true;
					PasteTransformFromClipboard(&m_Transform);
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			static bool bWorldSpace = false;
			ImGui::Checkbox("World Space", &bWorldSpace);

			glm::vec3 translation = bWorldSpace ? m_Transform.GetWorldPosition() : m_Transform.GetLocalPosition();
			glm::vec3 rotation = glm::degrees((glm::eulerAngles(bWorldSpace ? m_Transform.GetWorldRotation() : m_Transform.GetLocalRotation())));
			glm::vec3 pScale = bWorldSpace ? m_Transform.GetWorldScale() : m_Transform.GetLocalScale();
			glm::vec3 scale = pScale;

			bool bValueChanged = false;

			bValueChanged = ImGui::DragFloat3("Position", &translation[0], 0.1f) || bValueChanged;
			if (ImGui::IsItemClicked(1))
			{
				translation = VEC3_ZERO;
				bValueChanged = true;
			}

			glm::vec3 cleanedRot;
			bValueChanged = DoImGuiRotationDragFloat3("Rotation", rotation, cleanedRot) || bValueChanged;

			bValueChanged = ImGui::DragFloat3("Scale", &scale[0], 0.01f) || bValueChanged;
			if (ImGui::IsItemClicked(1))
			{
				scale = VEC3_ONE;
				bValueChanged = true;
			}

			ImGui::SameLine();

			bool bUseUniformScale = m_bUniformScale;
			if (ImGui::Checkbox("u", &bUseUniformScale))
			{
				SetUseUniformScale(bUseUniformScale, true);
				bValueChanged = true;
			}
			if (m_bUniformScale)
			{
				real newScale = scale.x;
				if (scale.y != pScale.y)
				{
					newScale = scale.y;
				}
				else if (scale.z != pScale.z)
				{
					newScale = scale.z;
				}
				scale = glm::vec3(newScale);
			}

			if (bValueChanged)
			{
				bAnyPropertyChanged = true;

				glm::quat rotQuat(glm::quat(glm::radians(cleanedRot)));

				if (bWorldSpace)
				{
					m_Transform.SetWorldPosition(translation, false);
					m_Transform.SetWorldRotation(rotQuat, false);
					m_Transform.SetWorldScale(scale, true);
				}
				else
				{
					m_Transform.SetLocalPosition(translation, false);
					m_Transform.SetLocalRotation(rotQuat, false);
					m_Transform.SetLocalScale(scale, true);
				}

				if (m_RigidBody != nullptr)
				{
					m_RigidBody->GetRigidBodyInternal()->activate(true);
				}

				SetUseUniformScale(m_bUniformScale, false);

				//if (m_RigidBody != nullptr)
				//{
				//	m_RigidBody->MatchTransform();
				//}

				if (g_Editor->IsObjectSelected(ID))
				{
					g_Editor->CalculateSelectedObjectsCenter();
				}
			}
		}

		if (m_Mesh != nullptr)
		{
			bAnyPropertyChanged = g_Renderer->DrawImGuiForGameObject(this, bDrawingEditorObjects) || bAnyPropertyChanged;
		}
		else
		{
			if (ImGui::Button("Add mesh component"))
			{
				bAnyPropertyChanged = true;
				Mesh* mesh = SetMesh(new Mesh(this));
				mesh->LoadFromFile(MESH_DIRECTORY "cube.glb", g_Renderer->GetPlaceholderMaterialID());
			}

			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(Editor::MeshPayloadCStr);

				if (payload != nullptr && payload->Data != nullptr)
				{
					bAnyPropertyChanged = true;
					Mesh* mesh = SetMesh(new Mesh(this));
					std::string relativeFilePath = std::string((const char*)payload->Data, payload->DataSize);
					mesh->LoadFromFile(MESH_DIRECTORY + relativeFilePath, g_Renderer->GetPlaceholderMaterialID());

					ImGui::EndDragDropTarget();
				}
			}
		}

		if (m_RigidBody != nullptr)
		{
			ImGui::Spacing();

			btRigidBody* rbInternal = m_RigidBody->GetRigidBodyInternal();

			bool bShowTree = ImGui::TreeNode("Rigid body");

			if (ImGui::BeginPopupContextItem("rb context menu"))
			{
				if (ImGui::Button("Remove rigid body"))
				{
					bAnyPropertyChanged = true;
					RemoveRigidBody();
				}

				ImGui::EndPopup();
			}

			if (bShowTree)
			{
				if (m_RigidBody != nullptr)
				{
					bool bRBStatic = m_RigidBody->IsStatic();
					if (ImGui::Checkbox("Static##rb", &bRBStatic))
					{
						bAnyPropertyChanged = true;
						m_RigidBody->SetStatic(bRBStatic);
					}

					bool bKinematic = m_RigidBody->IsKinematic();
					if (ImGui::Checkbox("Kinematic", &bKinematic))
					{
						bAnyPropertyChanged = true;
						m_RigidBody->SetKinematic(bKinematic);
					}

					ImGui::PushItemWidth(80.0f);
					{
						u32 group = m_RigidBody->GetGroup();
						if (ImGuiExt::InputUInt("Group", &group, 1u, 16u))
						{
							bAnyPropertyChanged = true;
							group = glm::clamp(group, 0u, 16u);
							m_RigidBody->SetGroup(group);
						}

						ImGui::SameLine();

						u32 mask = m_RigidBody->GetMask();
						if (ImGuiExt::InputUInt("Mask", &mask, 1u, 16u))
						{
							bAnyPropertyChanged = true;
							mask = glm::clamp(mask, 0u, 16u);
							m_RigidBody->SetMask(mask);
						}
					}
					ImGui::PopItemWidth();

					// TODO: Array of buttons for each category
					i32 flags = m_RigidBody->GetPhysicsFlags();
					if (ImGui::SliderInt("Flags", &flags, 0, 16))
					{
						bAnyPropertyChanged = true;
						m_RigidBody->SetPhysicsFlags(flags);
					}

					real mass = m_RigidBody->GetMass();
					if (ImGui::SliderFloat("Mass", &mass, 0.0f, 1000.0f))
					{
						bAnyPropertyChanged = true;
						m_RigidBody->SetMass(mass);
					}

					real friction = m_RigidBody->GetFriction();
					if (ImGui::SliderFloat("Friction", &friction, 0.0f, 1.0f))
					{
						bAnyPropertyChanged = true;
						m_RigidBody->SetFriction(friction);
					}

					ImGui::Spacing();

					btCollisionShape* shape = m_RigidBody->GetRigidBodyInternal()->getCollisionShape();
					std::string shapeTypeStr = CollisionShapeTypeToString(shape->getShapeType());

					if (ImGui::BeginCombo("Shape", shapeTypeStr.c_str()))
					{
						i32 selectedColliderShape = -1;
						for (i32 i = 0; i < (i32)ARRAY_LENGTH(g_CollisionTypes); ++i)
						{
							if (g_CollisionTypes[i] == shape->getShapeType())
							{
								selectedColliderShape = i;
								break;
							}
						}

						if (selectedColliderShape == -1)
						{
							PrintError("Failed to find collider shape in array!\n");
						}
						else
						{
							for (i32 i = 0; i < (i32)ARRAY_LENGTH(g_CollisionTypes); ++i)
							{
								bool bSelected = (i == selectedColliderShape);
								const char* colliderShapeName = g_CollisionTypeStrs[i];
								if (ImGui::Selectable(colliderShapeName, &bSelected))
								{
									if (selectedColliderShape != i)
									{
										bAnyPropertyChanged = true;

										selectedColliderShape = i;

										btCollisionShape* collisionShape = CreateCollisionShape(g_CollisionTypes[selectedColliderShape]);
										SetCollisionShape(collisionShape);
									}
								}
							}
						}

						ImGui::EndCombo();
					}

					glm::vec3 scale = m_Transform.GetWorldScale();
					switch (shape->getShapeType())
					{
					case BOX_SHAPE_PROXYTYPE:
					{
						btBoxShape* boxShape = static_cast<btBoxShape*>(shape);
						btVector3 halfExtents = boxShape->getHalfExtentsWithMargin();
						glm::vec3 halfExtentsG = ToVec3(halfExtents);
						halfExtentsG /= scale;

						if (ImGui::DragFloat3("Half extents", &halfExtentsG.x, 0.01f, 0.0f, 1000.0f))
						{
							halfExtentsG *= scale;
							bAnyPropertyChanged = true;
							btCollisionShape* collisionShape = CreateCollisionShape(BOX_SHAPE_PROXYTYPE, halfExtentsG.x, halfExtentsG.y, halfExtentsG.z);
							SetCollisionShape(collisionShape);
						}
					} break;
					case SPHERE_SHAPE_PROXYTYPE:
					{
						btSphereShape* sphereShape = static_cast<btSphereShape*>(shape);
						real radius = sphereShape->getRadius();
						radius /= scale.x;

						if (ImGui::DragFloat("radius", &radius, 0.1f, 0.0f, 1000.0f))
						{
							radius *= scale.x;
							bAnyPropertyChanged = true;
							btCollisionShape* collisionShape = CreateCollisionShape(SPHERE_SHAPE_PROXYTYPE, radius);
							SetCollisionShape(collisionShape);
						}
					} break;
					case CAPSULE_SHAPE_PROXYTYPE:
					{
						btCapsuleShapeZ* capsuleShape = static_cast<btCapsuleShapeZ*>(shape);
						real radius = capsuleShape->getRadius();
						real halfHeight = capsuleShape->getHalfHeight();
						radius /= scale.x;
						halfHeight /= scale.x;

						bool bUpdateShape = ImGui::DragFloat("radius", &radius, 0.1f, 0.0f, 1000.0f);
						bUpdateShape |= ImGui::DragFloat("height", &halfHeight, 0.1f, 0.0f, 1000.0f);

						if (bUpdateShape)
						{
							radius *= scale.x;
							halfHeight *= scale.x;
							bAnyPropertyChanged = true;
							btCollisionShape* collisionShape = CreateCollisionShape(SPHERE_SHAPE_PROXYTYPE, radius, halfHeight * 2.0f);
							SetCollisionShape(collisionShape);
						}
					} break;
					case CYLINDER_SHAPE_PROXYTYPE:
					{
						btCylinderShape* cylinderShape = static_cast<btCylinderShape*>(shape);
						btVector3 halfExtents = cylinderShape->getHalfExtentsWithMargin();
						glm::vec3 halfExtentsG = ToVec3(halfExtents);
						halfExtentsG /= scale;

						if (ImGui::DragFloat3("Half extents", &halfExtentsG.x, 0.1f, 0.0f, 1000.0f))
						{
							halfExtentsG *= scale;
							bAnyPropertyChanged = true;
							btCollisionShape* collisionShape = CreateCollisionShape(SPHERE_SHAPE_PROXYTYPE, halfExtentsG.x, halfExtentsG.y, halfExtentsG.z);
							SetCollisionShape(collisionShape);
						}
					} break;
					default:
					{
						PrintWarn("Unhandled shape type in GameObject::DrawImGuiObjects\n");
					} break;
					}

					//glm::vec3 localOffsetPos = m_RigidBody->GetRigidBodyInternal()->getCollisionShape();
					//if (ImGui::DragFloat3("Pos offset", &localOffsetPos.x, 0.05f))
					//{
					//	bAnyPropertyChanged = true;
					//	m_RigidBody->SetLocalPosition(localOffsetPos);
					//}
					//if (ImGui::IsItemClicked(1))
					//{
					//	bAnyPropertyChanged = true;
					//	m_RigidBody->SetLocalPosition(VEC3_ZERO);
					//}

					//glm::vec3 localOffsetRotEuler = glm::degrees(glm::eulerAngles(m_RigidBody->GetLocalRotation()));
					//glm::vec3 cleanedRot;
					//if (DoImGuiRotationDragFloat3("Rot offset", localOffsetRotEuler, cleanedRot))
					//{
					//	bAnyPropertyChanged = true;
					//	m_RigidBody->SetLocalRotation(glm::quat(glm::radians(cleanedRot)));
					//}

					ImGui::Spacing();

					glm::vec3 linearVel = ToVec3(m_RigidBody->GetRigidBodyInternal()->getLinearVelocity());
					if (ImGui::DragFloat3("linear vel", &linearVel.x, 0.05f))
					{
						bAnyPropertyChanged = true;
						rbInternal->setLinearVelocity(ToBtVec3(linearVel));
					}
					if (ImGui::IsItemClicked(1))
					{
						bAnyPropertyChanged = true;
						rbInternal->setLinearVelocity(btVector3(0.0f, 0.0f, 0.0f));
					}

					glm::vec3 angularVel = ToVec3(m_RigidBody->GetRigidBodyInternal()->getAngularVelocity());
					if (ImGui::DragFloat3("angular vel", &angularVel.x, 0.05f))
					{
						bAnyPropertyChanged = true;
						rbInternal->setAngularVelocity(ToBtVec3(angularVel));
					}
					if (ImGui::IsItemClicked(1))
					{
						bAnyPropertyChanged = true;
						rbInternal->setAngularVelocity(btVector3(0.0f, 0.0f, 0.0f));
					}

					ImGui::Text("Activation state: %i", rbInternal->getActivationState());

					ImGui::Text("Group: %u", m_RigidBody->GetGroup());
					ImGui::Text("Mask: %u", m_RigidBody->GetMask());
				}

				ImGui::TreePop();
			}
		}
		else
		{
			if (ImGui::Button("Add rigid body"))
			{
				bAnyPropertyChanged = true;
				RigidBody* rb = SetRigidBody(new RigidBody());
				btCollisionShape* collisionShape = CreateCollisionShape(BOX_SHAPE_PROXYTYPE);
				SetCollisionShape(collisionShape);

				rb->Initialize(collisionShape, &m_Transform);
				rb->GetRigidBodyInternal()->setUserPointer(this);
			}
		}

		std::vector<SocketData> const* sockets = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->GetGameObjectSockets(ID);
		if (sockets != nullptr)
		{
			bool bTreeOpen = ImGui::TreeNode("Sockets");
			ImGui::SameLine();
			ImGui::Text("(%i)", (i32)sockets->size());

			if (bTreeOpen)
			{
				for (u32 i = 0; i < (u32)sockets->size(); ++i)
				{
					ImGui::Text("%u: %i", i, (*sockets)[i].outputSignal);
				}
				ImGui::TreePop();
			}
		}

		ImGui::Text("Interactable: %s", m_bInteractable ? "true" : "false");
		ImGui::Text("Serializable: %s", m_bSerializable ? "true" : "false");

		ImGui::Text("Trigger: %s", m_bTrigger ? "true" : "false"); // TODO: Make checkbox

		std::string nearbyInteractibleName;
		if (m_NearbyInteractableID.IsValid())
		{
			nearbyInteractibleName = m_NearbyInteractableID.Get()->GetName();
		}
		ImGui::Text("NearbyInteractible: %s", nearbyInteractibleName.c_str());
		ImGui::Text("Sibling index: %i", m_SiblingIndex);

		std::string parentName;
		if (m_Parent != nullptr)
		{
			parentName = m_Parent->GetName();
		}
		ImGui::Text("Parent: %s", parentName.c_str());
		ImGui::Text("Num children: %u", (u32)m_Children.size());

		if (bAnyPropertyChanged && m_SourcePrefabID.IsValid())
		{
			g_ResourceManager->SetPrefabDirty(m_SourcePrefabID.m_PrefabID);
		}
	}

	bool GameObject::DoImGuiContextMenu(bool bActive)
	{
		bool bDeletedOrDuplicated = false;

		ImGui::PushID(&ID.Data1);
		ImGui::PushID(&ID.Data2);

		const char* contextMenuID = "game object context window";
		static std::string newObjectName = m_Name;
		const size_t maxStrLen = 256;

		bool bRefreshNameField = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
			ImGui::IsMouseClicked(1);

		if (bActive && g_Editor->GetWantRenameActiveElement())
		{
			ImGui::OpenPopup(contextMenuID);
			g_Editor->ClearWantRenameActiveElement();
			bRefreshNameField = true;
		}
		if (bRefreshNameField)
		{
			newObjectName = m_Name;
			newObjectName.resize(maxStrLen);
		}

		if (ImGui::BeginPopupContextItem(contextMenuID))
		{
			if (ImGui::IsWindowAppearing())
			{
				ImGui::SetKeyboardFocusHere();
			}

			bool bRename = ImGui::InputText("##rename-game-object",
				(char*)newObjectName.data(),
				maxStrLen,
				ImGuiInputTextFlags_EnterReturnsTrue);

			ImGui::SameLine();

			bRename |= ImGui::Button("Rename");

			bool bInvalidName = std::string(newObjectName.c_str()).empty();

			if (bRename && !bInvalidName)
			{
				// Remove excess trailing \0 chars
				newObjectName = std::string(newObjectName.c_str());

				m_Name = newObjectName;

				ImGui::CloseCurrentPopup();
			}

			// ID
			{
				char buffer[33];
				ID.ToString(buffer);
				char data0Buffer[17];
				char data1Buffer[17];
				memcpy(data0Buffer, buffer, 16);
				data0Buffer[16] = 0;
				memcpy(data1Buffer, buffer + 16, 16);
				data1Buffer[16] = 0;
				ImGui::Text("GUID: %s-%s", data0Buffer, data1Buffer);

				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();

					ImGui::Text("Data1, Data2: %lu, %lu", ID.Data1, ID.Data2);

					ImGui::EndTooltip();
				}

				ImGui::SameLine();

				if (ImGui::Button("Copy"))
				{
					g_Window->SetClipboardText(buffer);
				}
			}

			if (m_SourcePrefabID.IsValid())
			{
				std::string prefabLoadedFromFilePath = g_ResourceManager->GetPrefabFileName(m_SourcePrefabID.m_PrefabID);
				ImGui::Text("Prefab source: %s", prefabLoadedFromFilePath.c_str());
			}

			BaseScene* currentScene = g_SceneManager->CurrentScene();

			const char* typeStr = BaseScene::GameObjectTypeIDToString(m_TypeID);
			StringID newTypeID;
			std::string newTypeStr;
			if (currentScene->DoGameObjectTypeList(typeStr, newTypeID, newTypeStr))
			{
				std::vector<GameObjectID> selectedObjectIDs = g_Editor->GetSelectedObjectIDs();

				std::string name = m_Name;
				GameObject* parent = m_Parent;
				GameObject* newObject = CreateObjectOfType(newTypeID, m_Name, ID, typeStr, m_SourcePrefabID, m_bIsTemplate);
				CopyGenericFields(newObject, parent, (CopyFlags)((i32)CopyFlags::ALL & ~(i32)CopyFlags::ADD_TO_SCENE));
				newObject->m_TypeID = newTypeID; // Overwrite type ID again
				currentScene->RemoveObjectImmediate(this, true);
				if (parent != nullptr)
				{
					parent->AddChildImmediate(newObject);
				}
				else
				{
					currentScene->AddRootObjectImmediate(newObject);
				}
				newObject->Initialize();
				newObject->PostInitialize();

				g_Editor->SetSelectedObjects(selectedObjectIDs);

				bDeletedOrDuplicated = true;
			}

			if (DrawImGuiDuplicateGameObjectButton())
			{
				ImGui::CloseCurrentPopup();
				bDeletedOrDuplicated = true;
			}

			ImGui::SameLine();

			if (ImGui::Button("Delete"))
			{
				g_SceneManager->CurrentScene()->RemoveObject(this, true);
				bDeletedOrDuplicated = true;

				ImGui::CloseCurrentPopup();
			}

			bool bDisableSaveAsPrefab = !IsStringLower(m_Name);
			if (bDisableSaveAsPrefab)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.1f, 0.1f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
			}
			bool bDoSaveAsPrefab = ImGui::Button("Save as prefab");
			bool bDoForceSaveAsPrefab = ImGui::Button("Save as prefab (force new)");
			if (bDisableSaveAsPrefab)
			{
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Prefab names must be all lowercase");
				ImGui::PopStyleColor(3);
			}

			if (bDoSaveAsPrefab && !bDisableSaveAsPrefab)
			{
				SaveAsPrefab();
				bDeletedOrDuplicated = true;

				ImGui::CloseCurrentPopup();
			}

			if (bDoForceSaveAsPrefab && !bDisableSaveAsPrefab)
			{
				m_SourcePrefabID.Clear();
				SaveAsPrefab();
				bDeletedOrDuplicated = true;

				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		ImGui::PopID(); // ID.Data2
		ImGui::PopID(); // ID.Data1

		return bDeletedOrDuplicated;
	}

	bool GameObject::DrawImGuiDuplicateGameObjectButton()
	{
		if (ImGui::Button("Duplicate..."))
		{
			GameObject* newGameObject = CopySelf();
			if (newGameObject != nullptr)
			{
				g_Editor->SetSelectedObject(newGameObject->ID);
			}

			return true;
		}

		return false;
	}
} // namespace flex
