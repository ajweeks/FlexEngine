#include "stdafx.hpp"

#include "Scene/Mesh.hpp"

#include "Scene/LoadedMesh.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Platform/Platform.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/MeshComponent.hpp"

namespace flex
{
	std::map<std::string, LoadedMesh*> Mesh::s_LoadedMeshes;
	std::vector<std::string> Mesh::s_DiscoveredMeshes;

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
			meshComponent->Destroy();
			delete meshComponent;
		}
		m_Meshes.clear();

		m_Type = Type::_NONE;
		m_FileName = "";
		m_RelativeFilePath = "";
		m_OwningGameObject = nullptr;

		m_bInitialized = false;
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
		MeshImportSettings importSettings = m_ImportSettings;

		Destroy();

		SetOwner(owner);
		m_RelativeFilePath = relativeFilePath;
		m_ImportSettings = importSettings;

		if (!LoadFromFile(m_RelativeFilePath, materialIDs, &m_ImportSettings))
		{
			PrintError("Failed to reload mesh at %s\n", m_RelativeFilePath.c_str());
		}

		g_Renderer->RenderObjectStateChanged();
	}

	bool Mesh::LoadFromFile(
		const std::string& relativeFilePath,
		MaterialID materialID,
		bool bDynamic /* = false */,
		MeshImportSettings* importSettings /* = nullptr */,
		RenderObjectCreateInfo* optionalCreateInfo /* = nullptr */)
	{
		return LoadFromFile(relativeFilePath, std::vector<MaterialID>({ materialID }), bDynamic, importSettings, optionalCreateInfo);
	}

	bool Mesh::LoadFromFile(
		const std::string& relativeFilePath,
		const std::vector<MaterialID>& inMaterialIDs,
		bool bDynamic /* = false */,
		MeshImportSettings* importSettings /* = nullptr */,
		RenderObjectCreateInfo* optionalCreateInfo /* = nullptr */)
	{
		// TODO: Call SetRequiredAttributesFromMaterialID for each mesh with each matID?

		if (m_bInitialized)
		{
			PrintError("Attempted to load mesh after already initialized! If reloading, first call Destroy\n");
			return false;
		}

		m_Type = Type::FILE;
		m_RelativeFilePath = relativeFilePath;
		m_FileName = StripLeadingDirectories(m_RelativeFilePath);

		m_BoundingSphereRadius = 0;
		m_BoundingSphereCenterPoint = VEC3_ZERO;

		LoadedMesh* loadedMesh = GetLoadedMesh(relativeFilePath, importSettings);

		if (loadedMesh == nullptr)
		{
			PrintError("Failed to load mesh at %s\n", relativeFilePath.c_str());
			return false;
		}

		m_ImportSettings = loadedMesh->importSettings;

		cgltf_data* data = loadedMesh->data;
		if (data == nullptr)
		{
			PrintError("Failed to load mesh at %s\n", relativeFilePath.c_str());
			return false;
		}

		if (data->meshes_count == 0)
		{
			PrintError("Loaded mesh file has no meshes\n");
			return false;
		}

		// TODO: Support multiple meshes in one file
		//assert(data->meshes_count == 1);
		for (i32 i = 0; i < (i32)1; ++i)
		{
			cgltf_mesh* mesh = &(data->meshes[i]);

			std::vector<MaterialID> materialIDs = inMaterialIDs;

			if (materialIDs.size() > 1 && mesh->primitives_count != materialIDs.size())
			{
				PrintWarn("Material ID count in mesh does not match primitive count found in file! Ignoring non-first elements\n");
				materialIDs.resize(1);
			}

			for (i32 j = 0; j < (i32)mesh->primitives_count; ++j)
			{
				MaterialID matID = materialIDs.size() > 1 ? materialIDs[j] : materialIDs.size() == 1 ? materialIDs[0] : g_Renderer->GetPlaceholderMaterialID();
				MeshComponent* meshComponent;
				if (bDynamic)
				{
					meshComponent = MeshComponent::LoadFromCGLTFDynamic(this, &mesh->primitives[j], matID, u32_max, importSettings, optionalCreateInfo);
				}
				else
				{
					meshComponent = MeshComponent::LoadFromCGLTF(this, &mesh->primitives[j], matID, importSettings, optionalCreateInfo);
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

	// NOTE(AJ): Unused! Might be stale
	bool Mesh::LoadFromMemory(const VertexBufferDataCreateInfo& vertexBufferCreateInfo,
		const std::vector<u32>& indices,
		MaterialID matID,
		RenderObjectCreateInfo* optionalCreateInfo)
	{
		return LoadFromMemoryInternal(vertexBufferCreateInfo, indices, matID, false, 0, optionalCreateInfo);
	}

	bool Mesh::LoadFromMemoryDynamic(const VertexBufferDataCreateInfo& vertexBufferCreateInfo,
		const std::vector<u32>& indices,
		MaterialID matID,
		u32 initialMaxVertexCount,
		RenderObjectCreateInfo* optionalCreateInfo)
	{
		return LoadFromMemoryInternal(vertexBufferCreateInfo, indices, matID, true, initialMaxVertexCount, optionalCreateInfo);
	}

	bool Mesh::LoadFromMemoryInternal(const VertexBufferDataCreateInfo& vertexBufferCreateInfo,
		const std::vector<u32>& indices,
		MaterialID matID,
		bool bDynamic,
		u32 initialMaxVertexCount,
		RenderObjectCreateInfo* optionalCreateInfo)
	{
		if (m_bInitialized)
		{
			PrintError("Attempted to load mesh after already initialized! If reloading, first call Destroy\n");
			return false;
		}

		m_Type = Type::MEMORY;

		m_BoundingSphereRadius = 0;
		m_BoundingSphereCenterPoint = VEC3_ZERO;

		if (vertexBufferCreateInfo.attributes == 0)
		{
			PrintError("Invalid vertex buffer data passed into Mesh::LoadFromMemory");
			return false;
		}

		MeshComponent* meshComponent = nullptr;

		if (bDynamic)
		{
			meshComponent = MeshComponent::LoadFromMemoryDynamic(this, vertexBufferCreateInfo, indices, matID, initialMaxVertexCount, optionalCreateInfo);
		}
		else
		{
			meshComponent = MeshComponent::LoadFromMemory(this, vertexBufferCreateInfo, indices, matID, optionalCreateInfo);
		}

		if (meshComponent)
		{
			m_Meshes.push_back(meshComponent);
		}

		CalculateBounds();

		m_bInitialized = true;

		return true;
	}

	bool Mesh::LoadPrefabShape(PrefabShape shape, MaterialID materialID, RenderObjectCreateInfo* optionalCreateInfo /* = nullptr */)
	{
		m_Type = Type::PREFAB;

		m_Meshes = { new MeshComponent(this, materialID, false) };
		return m_Meshes[0]->LoadPrefabShape(shape, optionalCreateInfo);
	}

	bool Mesh::CreateProcedural(u32 initialMaxVertCount,
		VertexAttributes attributes,
		MaterialID materialID,
		TopologyMode topologyMode /* = TopologyMode::TRIANGLE_LIST */,
		RenderObjectCreateInfo* optionalCreateInfo /* = nullptr */)
	{
		m_Type = Type::PROCEDURAL;

		m_Meshes = { new MeshComponent(this, materialID, false) };
		return m_Meshes[0]->CreateProcedural(initialMaxVertCount, attributes, topologyMode, optionalCreateInfo);
	}

	void Mesh::DiscoverMeshes()
	{
		std::vector<std::string> filePaths;
		if (Platform::FindFilesInDirectory(MESH_DIRECTORY, filePaths, "*"))
		{
			for (const std::string& filePath : filePaths)
			{
				if (!Contains(s_DiscoveredMeshes, filePath))
				{
					s_DiscoveredMeshes.push_back(filePath);
				}
			}
		}
	}

	bool Mesh::FindPreLoadedMesh(const std::string& relativeFilePath, LoadedMesh** loadedMesh)
	{
		auto iter = s_LoadedMeshes.find(relativeFilePath);
		if (iter == s_LoadedMeshes.end())
		{
			return false;
		}
		else
		{
			*loadedMesh = iter->second;
			return true;
		}
	}

	LoadedMesh* Mesh::LoadMesh(const std::string& relativeFilePath, MeshImportSettings* importSettings /* = nullptr */)
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
			auto existingIter = Mesh::s_LoadedMeshes.find(relativeFilePath);
			if (existingIter == Mesh::s_LoadedMeshes.end())
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

		// If import settings were passed in, save them in the cached mesh
		if (importSettings)
		{
			mesh->importSettings = *importSettings;
		}

		mesh->relativeFilePath = relativeFilePath;

		auto cleanup = [&](const std::string& errorMessage)
		{
			Print("%s", errorMessage.c_str());
			auto existingIter = Mesh::s_LoadedMeshes.find(relativeFilePath);
			if (existingIter != Mesh::s_LoadedMeshes.end())
			{
				Mesh::s_LoadedMeshes.erase(existingIter);
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
				s_LoadedMeshes.emplace(relativeFilePath, mesh);
			}
		}

		return mesh;
	}

	bool Mesh::CheckCGLTFResult(cgltf_result result, std::string& outErrorMessage)
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

	Mesh* Mesh::ParseJSON(const JSONObject& object, GameObject* owner, const std::vector<MaterialID>& inMaterialIDs)
	{
		Mesh* newMesh = nullptr;

		std::string meshFilePath = object.GetString("file");
		if (!meshFilePath.empty())
		{
			meshFilePath = MESH_DIRECTORY + meshFilePath;
		}
		std::string meshPrefabName = object.GetString("prefab");
		bool bSwapNormalYZ = object.GetBool("swapNormalYZ");
		bool bFlipNormalZ = object.GetBool("flipNormalZ");
		bool bFlipU = object.GetBool("flipU");
		bool bFlipV = object.GetBool("flipV");
		bool bDontCreateRenderObject = false;
		object.SetBoolChecked("dont create render object", bDontCreateRenderObject);

		std::vector<MaterialID> materialIDs = inMaterialIDs;

		if (materialIDs.empty() || materialIDs[0] == InvalidMaterialID)
		{
			PrintWarn("Mesh component requires material index to be parsed: %s\n", owner->GetName().c_str());
			materialIDs = { g_Renderer->GetPlaceholderMaterialID() };
		}

		if (!meshFilePath.empty())
		{
			newMesh = new Mesh(owner);

			MeshImportSettings importSettings = {};
			importSettings.bFlipU = bFlipU;
			importSettings.bFlipV = bFlipV;
			importSettings.bFlipNormalZ = bFlipNormalZ;
			importSettings.bSwapNormalYZ = bSwapNormalYZ;
			importSettings.bDontCreateRenderObject = bDontCreateRenderObject;

			owner->SetMesh(newMesh);
			newMesh->LoadFromFile(meshFilePath, materialIDs, &importSettings);
		}
		else if (!meshPrefabName.empty())
		{
			newMesh = new Mesh(owner);

			PrefabShape prefabShape = MeshComponent::StringToPrefabShape(meshPrefabName);
			newMesh->LoadPrefabShape(prefabShape, materialIDs[0]);

			owner->SetMesh(newMesh);
		}
		else
		{
			PrintError("Unhandled mesh field on object: %s\n", owner->GetName().c_str());
		}

		return newMesh;
	}

	Mesh* Mesh::ImportFromFile(const std::string& meshFilePath, GameObject* owner)
	{
		Mesh* newMesh = new Mesh(owner);

		std::vector<MaterialID> materialIDs = { g_Renderer->GetPlaceholderMaterialID() };

		MeshImportSettings importSettings = {};

		owner->SetMesh(newMesh);
		newMesh->LoadFromFile(meshFilePath, materialIDs, &importSettings);

		return newMesh;
	}

	JSONObject Mesh::Serialize() const
	{
		JSONObject meshObject = {};

		if (m_Type == Mesh::Type::FILE)
		{
			std::string prefixStr = MESH_DIRECTORY;
			std::string meshFilepath = GetRelativeFilePath().substr(prefixStr.length());
			meshObject.fields.emplace_back("file", JSONValue(meshFilepath));
		}
		else if (m_Type == Mesh::Type::PREFAB)
		{
			std::string prefabShapeStr = MeshComponent::PrefabShapeToString(m_Meshes[0]->GetShape());
			meshObject.fields.emplace_back("prefab", JSONValue(prefabShapeStr));
		}
		else
		{
			PrintError("Unhandled mesh prefab type when attempting to serialize scene!\n");
		}

		MeshImportSettings importSettings = m_ImportSettings;
		meshObject.fields.emplace_back("swapNormalYZ", JSONValue(importSettings.bSwapNormalYZ));
		meshObject.fields.emplace_back("flipNormalZ", JSONValue(importSettings.bFlipNormalZ));
		meshObject.fields.emplace_back("flipU", JSONValue(importSettings.bFlipU));
		meshObject.fields.emplace_back("flipV", JSONValue(importSettings.bFlipV));
		meshObject.fields.emplace_back("dont create render object", JSONValue(importSettings.bDontCreateRenderObject));

		return meshObject;
	}

	std::string Mesh::GetRelativeFilePath() const
	{
		return m_RelativeFilePath;
	}

	std::string Mesh::GetFileName() const
	{
		return m_FileName;
	}

	MeshImportSettings Mesh::GetImportSettings() const
	{
		return m_ImportSettings;
	}

	MeshComponent* Mesh::GetSubMeshWithRenderID(RenderID renderID) const
	{
		for (MeshComponent* meshComponent : m_Meshes)
		{
			if (meshComponent->renderID == renderID)
			{
				return meshComponent;
			}
		}

		return nullptr;
	}

	std::vector<MeshComponent*> Mesh::GetSubMeshes() const
	{
		return m_Meshes;
	}

	GameObject* Mesh::GetOwningGameObject() const
	{
		return m_OwningGameObject;
	}

	LoadedMesh* Mesh::GetLoadedMesh(const std::string& relativeFilePath, MeshImportSettings* importSettings)
	{
		LoadedMesh* result = nullptr;
		if (FindPreLoadedMesh(relativeFilePath, &result))
		{
			// If no import settings have been passed in, grab any from the cached mesh
			if (importSettings == nullptr)
			{
				importSettings = &result->importSettings;
			}
		}
		else
		{
			// Mesh hasn't been loaded before, load it now
			result = LoadMesh(relativeFilePath, importSettings);
		}
		return result;
	}

	void Mesh::SetOwner(GameObject* owner)
	{
		m_OwningGameObject = owner;
	}

	void Mesh::DrawImGui()
	{
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
			for (auto iter : s_LoadedMeshes)
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

				for (auto meshPair : s_LoadedMeshes)
				{
					bool bSelected = (i == selectedMeshIndex);
					const std::string meshFileName = StripLeadingDirectories(meshPair.first);
					if (ImGui::Selectable(meshFileName.c_str(), &bSelected))
					{
						if (selectedMeshIndex != i)
						{
							selectedMeshIndex = i;
							std::string relativeFilePath = meshPair.first;
							GameObject* owner = m_OwningGameObject;
							Destroy();
							m_OwningGameObject = owner;
							LoadFromFile(relativeFilePath, GetMaterialIDs(), &m_ImportSettings);
						}
					}

					++i;
				}

				ImGui::EndCombo();
			}

			contextMenuCheck();

			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(Renderer::MeshPayloadCStr);

				if (payload && payload->Data)
				{
					std::string draggedMeshFileName((const char*)payload->Data, payload->DataSize);
					auto meshIter = s_LoadedMeshes.find(draggedMeshFileName);
					if (meshIter != s_LoadedMeshes.end())
					{
						std::string newMeshFilePath = meshIter->first;

						if (m_RelativeFilePath.compare(newMeshFilePath) != 0)
						{
							GameObject* owner = m_OwningGameObject;
							Destroy();
							m_OwningGameObject = owner;
							LoadFromFile(newMeshFilePath, GetMaterialIDs(), &m_ImportSettings);
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
		} break;
		case Mesh::Type::PREFAB:
		{
			MeshComponent* meshComponent = GetSubMeshes()[0];
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
			MeshComponent* meshComponent = GetSubMeshes()[0];
			ImGui::Text("Procedural mesh");
			ImGui::Text("Vertex count: %u", meshComponent->GetVertexBufferData()->VertexCount);
			contextMenuCheck();
		} break;
		case Mesh::Type::MEMORY:
		{
		} break;
		default:
		{
			PrintError("Unhandled Mesh::Type in Renderer::DrawImGuiForGameObject: %d\n", (i32)m_Type);
			assert(false);
		} break;
		}

		if (ImGui::BeginPopup(meshContextMenuStr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings))
		{
			if (ImGui::Button("Remove mesh"))
			{
				bDeleteSelf = true;
			}

			ImGui::EndPopup();
		}

		if (bDeleteSelf)
		{
			m_OwningGameObject->SetMesh(nullptr);
			// We are now deleted!
		}
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
		return m_Meshes[slotIndex]->GetMaterialID();
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
			m_MinPoint = glm::min(m_MinPoint, meshComponent->m_MinPoint);
			m_MaxPoint = glm::max(m_MaxPoint, meshComponent->m_MaxPoint);
		}

		m_BoundingSphereRadius = glm::distance(m_MaxPoint, m_MinPoint) / 2.0f;
		m_BoundingSphereCenterPoint = m_MinPoint + (m_MaxPoint - m_MinPoint) / 2.0f;
	}

} // namespace flex
