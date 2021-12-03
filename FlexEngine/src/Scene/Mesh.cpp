#include "stdafx.hpp"

#include "Scene/Mesh.hpp"

#include "Editor.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Platform/Platform.hpp"
#include "ResourceManager.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/LoadedMesh.hpp"
#include "Scene/MeshComponent.hpp"

namespace flex
{
	// Local global function to avoid having to forward declare cgltf_result
	bool CheckCGLTFResult(cgltf_result result, std::string& outErrorMessage)
	{
		if (result != cgltf_result_success)
		{
			switch (result)
			{
			case cgltf_result_data_too_short:	outErrorMessage = "Data too short"; break;
			case cgltf_result_unknown_format:	outErrorMessage = "Unknown format"; break;
			case cgltf_result_invalid_json:		outErrorMessage = "Invalid json"; break;
			case cgltf_result_invalid_gltf:		outErrorMessage = "Invalid gltf"; break;
			case cgltf_result_invalid_options:	outErrorMessage = "Invalid options"; break;
			case cgltf_result_file_not_found:	outErrorMessage = "File not found"; break;
			case cgltf_result_io_error:			outErrorMessage = "IO error"; break;
			case cgltf_result_out_of_memory:	outErrorMessage = "Out of memory"; break;
			default:							outErrorMessage = ""; break;
			}
			return false;
		}
		return true;
	}

	Mesh::Mesh(GameObject* owner) :
		m_OwningGameObject(owner),
		m_MinPoint(VEC3_ZERO),
		m_MaxPoint(VEC3_ZERO),
		m_BoundingSphereCenterPoint(VEC3_ZERO)
	{
	}

	void Mesh::PostInitialize()
	{
		for (MeshComponent* meshComponent : m_Meshes)
		{
			meshComponent->PostInitialize();
		}
	}

	void Mesh::Destroy()
	{
		for (MeshComponent* meshComponent : m_Meshes)
		{
			if (meshComponent != nullptr)
			{
				meshComponent->Destroy();
				delete meshComponent;
			}
		}
		m_Meshes.clear();

		m_Type = Type::_NONE;
		m_FileName = "";
		m_RelativeFilePath = "";
		m_OwningGameObject = nullptr;

		m_bInitialized = false;
	}

	void Mesh::DestroyAllSubmeshes()
	{
		for (MeshComponent* meshComponent : m_Meshes)
		{
			if (meshComponent != nullptr)
			{
				meshComponent->Destroy();
				delete meshComponent;
			}
		}
		m_Meshes.clear();
	}

	// TODO: Test
	void Mesh::Reload()
	{
		if (m_Type != Type::FILE)
		{
			PrintWarn("Prefab reloading is unsupported\n");
			return;
		}

		GameObject* owner = m_OwningGameObject;
		std::vector<MaterialID> materialIDs = GetMaterialIDs();
		std::string relativeFilePath = m_RelativeFilePath;

		Destroy();

		SetOwner(owner);
		m_RelativeFilePath = relativeFilePath;

		CreateInfo createInfo = {};
		createInfo.materialIDs = materialIDs;
		createInfo.relativeFilePath = relativeFilePath;
		createInfo.bForceLoadMesh = true;

		if (!LoadFromFile(createInfo))
		{
			PrintError("Failed to reload mesh at %s\n", m_RelativeFilePath.c_str());
		}

		g_Renderer->RenderObjectStateChanged();
	}

	Mesh* Mesh::CloneSelf(GameObject* newOwner, bool bCreateRenderObject)
	{
		std::vector<MaterialID> matIDs = GetMaterialIDs();

		Mesh* newMesh = newOwner->SetMesh(new Mesh(newOwner));
		switch (m_Type)
		{
		case Type::PREFAB:
		{
			PrefabShape shape = m_Meshes[0]->GetShape();
			newMesh->LoadPrefabShape(shape, matIDs[0], nullptr, bCreateRenderObject);
		} break;
		case Type::FILE:
		{
			std::string filePath = m_RelativeFilePath;
			Mesh::CreateInfo meshCreateInfo = {};
			meshCreateInfo.relativeFilePath = filePath;
			meshCreateInfo.materialIDs = matIDs;
			meshCreateInfo.bCreateRenderObject = bCreateRenderObject;
			newMesh->LoadFromFile(meshCreateInfo);
		} break;
		default:
		{
			PrintError("Unhandled mesh component prefab type in Mesh::CloneSelf\n");
		} break;
		}

		return newMesh;
	}

	void Mesh::RemoveSubmesh(u32 index)
	{
		m_Meshes[index] = nullptr;
	}

	bool Mesh::LoadFromFile(CreateInfo& createInfo)
	{
		CHECK_NE(m_OwningGameObject, nullptr);

		// TODO: Call SetRequiredAttributesFromMaterialID for each mesh with each matID?

		if (m_bInitialized)
		{
			PrintError("Attempted to load mesh after already initialized! If reloading, first call Destroy\n");
			return false;
		}

		m_Type = Type::FILE;
		m_RelativeFilePath = createInfo.relativeFilePath;
		m_FileName = StripLeadingDirectories(m_RelativeFilePath);

		m_BoundingSphereRadius = 0;
		m_BoundingSphereCenterPoint = VEC3_ZERO;

		LoadedMesh* loadedMesh = g_ResourceManager->FindOrLoadMesh(m_RelativeFilePath, createInfo.bForceLoadMesh);

		if (loadedMesh == nullptr)
		{
			PrintError("Failed to load mesh at %s\n", m_RelativeFilePath.c_str());
			return false;
		}

		cgltf_data* data = loadedMesh->data;
		if (data == nullptr)
		{
			PrintError("Failed to load mesh at %s\n", m_RelativeFilePath.c_str());
			return false;
		}

		if (data->meshes_count == 0)
		{
			PrintError("Loaded mesh file has no meshes\n");
			return false;
		}

		// TODO: Support multiple meshes in one file
		//CHECK_EQ(data->meshes_count, 1);
		for (i32 i = 0; i < (i32)1; ++i)
		{
			cgltf_mesh* mesh = &(data->meshes[i]);

			if (createInfo.materialIDs.size() > 1 && mesh->primitives_count != createInfo.materialIDs.size())
			{
				PrintWarn("Material ID count in mesh does not match primitive count found in file! Ignoring non-first elements\n");
				createInfo.materialIDs.resize(1);
			}

			for (i32 j = 0; j < (i32)mesh->primitives_count; ++j)
			{
				MaterialID matID = createInfo.materialIDs.size() > 1 ? createInfo.materialIDs[j] : createInfo.materialIDs.size() == 1 ? createInfo.materialIDs[0] : g_Renderer->GetPlaceholderMaterialID();
				MeshComponent* meshComponent;
				if (createInfo.bDynamic)
				{
					meshComponent = MeshComponent::LoadFromCGLTFDynamic(this, &mesh->primitives[j], matID, u32_max, createInfo.optionalRenderObjectCreateInfo, createInfo.bCreateRenderObject);
				}
				else
				{
					meshComponent = MeshComponent::LoadFromCGLTF(this, &mesh->primitives[j], matID, createInfo.optionalRenderObjectCreateInfo, createInfo.bCreateRenderObject);
				}
				if (meshComponent != nullptr)
				{
					m_Meshes.push_back(meshComponent);
				}
			}
		}

		CalculateBounds();

		m_bInitialized = true;

		return true;
	}

	bool Mesh::LoadFromFile(const std::string& filePath, MaterialID matID)
	{
		CreateInfo createInfo = {};
		createInfo.relativeFilePath = filePath;
		createInfo.materialIDs = { matID };
		return LoadFromFile(createInfo);
	}

	bool Mesh::LoadFromFile(const std::string& filePath, const std::vector<MaterialID>& matIDs)
	{
		CreateInfo createInfo = {};
		createInfo.relativeFilePath = filePath;
		createInfo.materialIDs = matIDs;
		return LoadFromFile(createInfo);
	}

	// NOTE(AJ): Unused! Might be stale
	bool Mesh::LoadFromMemory(const CreateInfo& createInfo)
	{
		if (m_bInitialized)
		{
			PrintError("Attempted to load mesh after already initialized! If reloading, first call Destroy\n");
			return false;
		}

		m_Type = Type::MEMORY;

		m_BoundingSphereRadius = 0;
		m_BoundingSphereCenterPoint = VEC3_ZERO;

		if (createInfo.vertexBufferCreateInfo->attributes == 0)
		{
			PrintError("Invalid vertex buffer data passed into Mesh::LoadFromMemory");
			return false;
		}

		if (createInfo.bDynamic)
		{
			MeshComponent::LoadFromMemoryDynamic(this, *createInfo.vertexBufferCreateInfo, createInfo.indices, createInfo.materialIDs[0], createInfo.initialMaxVertexCount, createInfo.optionalRenderObjectCreateInfo, createInfo.bCreateRenderObject);
		}
		else
		{
			MeshComponent::LoadFromMemory(this, *createInfo.vertexBufferCreateInfo, createInfo.indices, createInfo.materialIDs[0], createInfo.optionalRenderObjectCreateInfo, createInfo.bCreateRenderObject);
		}

		CalculateBounds();

		m_bInitialized = true;

		return true;
	}

	bool Mesh::LoadPrefabShape(
		PrefabShape shape,
		MaterialID materialID,
		RenderObjectCreateInfo* optionalRenderObjectCreateInfo /* = nullptr */,
		bool bCreateRenderObject /* = true */)
	{
		m_Type = Type::PREFAB;

		m_Meshes = { new MeshComponent(this, materialID, false) };
		return m_Meshes[0]->LoadPrefabShape(shape, optionalRenderObjectCreateInfo, bCreateRenderObject);
	}

	bool Mesh::CreateProcedural(u32 initialMaxVertCount,
		VertexAttributes attributes,
		MaterialID materialID,
		TopologyMode topologyMode /* = TopologyMode::TRIANGLE_LIST */,
		RenderObjectCreateInfo* optionalRenderObjectCreateInfo /* = nullptr */)
	{
		m_Type = Type::PROCEDURAL;

		m_Meshes = { new MeshComponent(this, materialID, false) };
		return m_Meshes[0]->CreateProcedural(initialMaxVertCount, attributes, topologyMode, optionalRenderObjectCreateInfo);
	}

	i32 Mesh::AddSubMesh(MeshComponent* meshComponent)
	{
		for (i32 i = 0; i < (i32)m_Meshes.size(); ++i)
		{
			if (m_Meshes[i] == nullptr)
			{
				m_Meshes[i] = meshComponent;
				return i;
			}
		}

		i32 newIndex = (i32)m_Meshes.size();
		m_Meshes.emplace_back(meshComponent);

		return newIndex;
	}

	LoadedMesh* Mesh::LoadMesh(const std::string& relativeFilePath)
	{
		if (relativeFilePath.find(':') != std::string::npos)
		{
			PrintError("Called MeshComponent::LoadMesh with an absolute file path! Filepath must be relative. %s\n", relativeFilePath.c_str());
			return nullptr;
		}

		if (!EndsWith(relativeFilePath, "gltf") && !EndsWith(relativeFilePath, "glb"))
		{
			PrintWarn("Attempted to load non gltf/glb model! Only those formats are supported! %s\n", relativeFilePath.c_str());
			return nullptr;
		}

		const std::string fileName = StripLeadingDirectories(relativeFilePath);

		LoadedMesh* mesh = nullptr;
		bool bNewMesh = true;
		{
			auto existingIter = g_ResourceManager->loadedMeshes.find(relativeFilePath);
			if (existingIter == g_ResourceManager->loadedMeshes.end())
			{
				if (g_bEnableLogging_Loading)
				{
					Print("Loading mesh %s\n", fileName.c_str());
				}
				mesh = new LoadedMesh();
			}
			else
			{
				if (g_bEnableLogging_Loading)
				{
					Print("Reloading mesh %s\n", fileName.c_str());
				}
				mesh = existingIter->second;
				bNewMesh = false;
			}
		}

		mesh->relativeFilePath = relativeFilePath;

		auto cleanup = [&](const std::string& errorMessage)
		{
			Print("%s", errorMessage.c_str());
			auto existingIter = g_ResourceManager->loadedMeshes.find(relativeFilePath);
			if (existingIter != g_ResourceManager->loadedMeshes.end())
			{
				g_ResourceManager->loadedMeshes.erase(existingIter);
			}
			delete mesh;
		};

		std::string outErrorMessage;

		cgltf_options ops = {};
		ops.type = cgltf_file_type_invalid; // auto detect gltf or glb
		cgltf_data* data = nullptr;

		cgltf_result result = cgltf_parse_file(&ops, relativeFilePath.c_str(), &data);
		if (!CheckCGLTFResult(result, outErrorMessage))
		{
			cleanup("Failed to parse gltf/glb file at " + relativeFilePath + " with error:\n\t" + outErrorMessage + "\n");
			return nullptr;
		}

		result = cgltf_load_buffers(&ops, data, relativeFilePath.c_str());
		if (!CheckCGLTFResult(result, outErrorMessage))
		{
			cleanup("Failed to load gltf/glb file at " + relativeFilePath + " with error:\n\t" + outErrorMessage + "\n");
			return nullptr;
		}

		result = cgltf_validate(data);
		if (!CheckCGLTFResult(result, outErrorMessage))
		{
			cleanup("Failed to validate gltf/glb file at " + relativeFilePath + " with error:\n\t" + outErrorMessage + "\n");
			return nullptr;
		}
		else
		{
			mesh->data = data;

			if (bNewMesh)
			{
				g_ResourceManager->loadedMeshes.emplace(relativeFilePath, mesh);
			}
		}

		return mesh;
	}

	Mesh* Mesh::ImportFromFile(const std::string& meshFilePath, GameObject* owner, bool bCreateRenderObject /* = true */)
	{
		return ImportFromFile(meshFilePath, owner, { g_Renderer->GetPlaceholderMaterialID() }, bCreateRenderObject);
	}

	Mesh* Mesh::ImportFromFile(const std::string& meshFilePath, GameObject* owner, const std::vector<MaterialID>& materialIDs, bool bCreateRenderObject /* = true */)
	{
		Mesh* newMesh = new Mesh(owner);

		owner->SetMesh(newMesh);
		CreateInfo createInfo = {};
		createInfo.relativeFilePath = meshFilePath;
		createInfo.materialIDs = materialIDs;
		createInfo.bCreateRenderObject = bCreateRenderObject;
		newMesh->LoadFromFile(createInfo);

		return newMesh;
	}

	Mesh* Mesh::ImportFromPrefab(const std::string& prefabName, GameObject* owner, const std::vector<MaterialID>& materialIDs, bool bCreateRenderObject /* = true */)
	{
		Mesh* newMesh = new Mesh(owner);

		owner->SetMesh(newMesh);
		newMesh->LoadPrefabShape(MeshComponent::StringToPrefabShape(prefabName), materialIDs[0], nullptr, bCreateRenderObject);

		return newMesh;
	}

	std::string Mesh::GetRelativeFilePath() const
	{
		return m_RelativeFilePath;
	}

	std::string Mesh::GetFileName() const
	{
		return m_FileName;
	}

	MeshComponent* Mesh::GetSubMeshWithRenderID(RenderID renderID) const
	{
		for (MeshComponent* meshComponent : m_Meshes)
		{
			if (meshComponent != nullptr && meshComponent->renderID == renderID)
			{
				return meshComponent;
			}
		}

		return nullptr;
	}

	std::vector<MeshComponent*> Mesh::GetSubMeshesCopy() const
	{
		return m_Meshes;
	}

	MeshComponent* Mesh::GetSubMesh(u32 index) const
	{
		return m_Meshes[index];
	}

	GameObject* Mesh::GetOwningGameObject() const
	{
		return m_OwningGameObject;
	}

	void Mesh::OnExternalMeshChange(const std::string& meshFilePath)
	{
		if (meshFilePath == m_RelativeFilePath)
		{
			Reload();
		}
	}

	void Mesh::SetOwner(GameObject* owner)
	{
		m_OwningGameObject = owner;
	}

	bool Mesh::DrawImGui()
	{
		bool bAnyPropertyChanged = false;

		const char* meshContextMenuStr = "mesh context menu";
		auto contextMenuCheck = [meshContextMenuStr]()
		{
			if (ImGui::IsMouseReleased(1) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
			{
				ImGui::OpenPopup(meshContextMenuStr);
			}
		};

		ImGui::Text("Mesh");

		contextMenuCheck();

		ImGui::SameLine();

		bool bDeleteSelf = false;

		if (ImGui::Button("Remove"))
		{
			bDeleteSelf = true;
		}

		switch (m_Type)
		{
		case Mesh::Type::FILE:
		{
			i32 selectedMeshIndex = 0;
			std::string currentMeshFileName = "NONE";
			i32 i = 0;
			for (auto iter : g_ResourceManager->loadedMeshes)
			{
				const std::string meshFileName = StripLeadingDirectories(iter.first);
				if (m_FileName.compare(meshFileName) == 0)
				{
					selectedMeshIndex = i;
					currentMeshFileName = meshFileName;
					break;
				}

				++i;
			}
			if (ImGui::BeginCombo("##meshcombo", currentMeshFileName.c_str()))
			{
				i = 0;

				for (auto meshPair : g_ResourceManager->loadedMeshes)
				{
					bool bSelected = (i == selectedMeshIndex);
					const std::string meshFileName = StripLeadingDirectories(meshPair.first);
					if (ImGui::Selectable(meshFileName.c_str(), &bSelected))
					{
						if (selectedMeshIndex != i)
						{
							bAnyPropertyChanged = true;
							selectedMeshIndex = i;
							m_RelativeFilePath = meshPair.first;
							Reload();
						}
					}

					++i;
				}

				ImGui::EndCombo();
			}

			contextMenuCheck();

			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(Editor::MeshPayloadCStr);

				if (payload && payload->Data)
				{
					bAnyPropertyChanged = true;
					m_RelativeFilePath = std::string((const char*)payload->Data, payload->DataSize);
					Reload();
				}
				ImGui::EndDragDropTarget();
			}
		} break;
		case Mesh::Type::PREFAB:
		{
			MeshComponent* meshComponent = GetSubMesh(0);
			i32 selectedMeshIndex = (i32)meshComponent->GetShape();
			std::string currentMeshName = MeshComponent::PrefabShapeToString(meshComponent->GetShape());

			if (ImGui::BeginCombo("Prefab", currentMeshName.c_str()))
			{
				for (i32 i = 0; i < (i32)PrefabShape::_NONE; ++i)
				{
					std::string shapeStr = MeshComponent::PrefabShapeToString((PrefabShape)i);
					bool bSelected = (selectedMeshIndex == i);
					if (ImGui::Selectable(shapeStr.c_str(), &bSelected))
					{
						if (selectedMeshIndex != i)
						{
							bAnyPropertyChanged = true;
							selectedMeshIndex = i;
							Destroy();
							LoadPrefabShape((PrefabShape)i, GetMaterialID(0));
						}
					}
				}

				contextMenuCheck();

				ImGui::EndCombo();
			}
		} break;
		case Mesh::Type::PROCEDURAL:
		{
			MeshComponent* meshComponent = GetSubMesh(0);
			ImGui::Text("Procedural mesh");
			ImGui::Text("Vertex count: %u", meshComponent->GetVertexBufferData()->VertexCount);
			contextMenuCheck();
		} break;
		case Mesh::Type::MEMORY:
		{
		} break;
		default:
		{
			PRINT_FATAL("Unhandled Mesh::Type in Renderer::DrawImGuiForGameObject: %d\n", (i32)m_Type);
		} break;
		}

		if (ImGui::BeginPopup(meshContextMenuStr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings))
		{
			if (ImGui::Button("Remove mesh"))
			{
				bAnyPropertyChanged = true;
				bDeleteSelf = true;
			}

			ImGui::EndPopup();
		}

		if (bDeleteSelf)
		{
			m_OwningGameObject->SetMesh(nullptr);
			// We are now deleted!
		}

		return bAnyPropertyChanged;
	}

	void Mesh::SetTypeToMemory()
	{
		m_Type = Type::MEMORY;
	}

	std::vector<MaterialID> Mesh::GetMaterialIDs() const
	{
		std::vector<MaterialID> result;
		result.reserve(m_Meshes.size());

		for (MeshComponent* meshComponent : m_Meshes)
		{
			result.emplace_back(meshComponent->GetMaterialID());
		}

		return result;
	}

	Mesh::Type Mesh::GetType() const
	{
		return m_Type;
	}

	u32 Mesh::GetSubmeshCount() const
	{
		return (u32)m_Meshes.size();
	}

	MaterialID Mesh::GetMaterialID(u32 slotIndex)
	{
		return m_Meshes[slotIndex] != nullptr ? m_Meshes[slotIndex]->GetMaterialID() : InvalidMaterialID;
	}

	RenderID Mesh::GetRenderID(u32 slotIndex)
	{
		return m_Meshes[slotIndex]->renderID;
	}

	void Mesh::CalculateBounds()
	{
		m_MinPoint = glm::vec3(FLT_MAX);
		m_MaxPoint = glm::vec3(-FLT_MAX);

		for (MeshComponent* meshComponent : m_Meshes)
		{
			if (meshComponent != nullptr)
			{
				m_MinPoint = glm::min(m_MinPoint, meshComponent->m_MinPoint);
				m_MaxPoint = glm::max(m_MaxPoint, meshComponent->m_MaxPoint);
			}
		}

		m_BoundingSphereRadius = glm::distance(m_MaxPoint, m_MinPoint) / 2.0f;
		m_BoundingSphereCenterPoint = m_MinPoint + (m_MaxPoint - m_MinPoint) / 2.0f;
	}
} // namespace flex
