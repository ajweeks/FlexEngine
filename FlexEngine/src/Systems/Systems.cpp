#include "stdafx.hpp"

#include "Systems/Systems.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtx/norm.hpp> // For distance2
IGNORE_WARNINGS_POP

#include "Graphics/Renderer.hpp" // For PhysicsDebugDrawBase
#include "Helpers.hpp"
#include "Platform/Platform.hpp" // For DirectoryWatcher
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"
#include "StringBuilder.hpp"

namespace flex
{
	const char* TerminalManager::SavePopupName = "Save script";
	GameObjectID TerminalManager::m_TerminalSavingID = InvalidGameObjectID;
	bool TerminalManager::m_bOpenSavePopup = false;

	void System::DrawImGui()
	{
	}

	void PluggablesSystem::Initialize()
	{
	}

	void PluggablesSystem::Destroy()
	{
		wires.clear();
	}

	void PluggablesSystem::Update()
	{
		if (!wires.empty())
		{
			PhysicsDebugDrawBase* debugDrawer = g_Renderer->GetDebugDrawer();
			btVector3 wireColOn(sin(g_SecElapsedSinceProgramStart * 3.5f) * 0.4f + 0.6f, 0.2f, 0.2f);
			static const btVector3 wireColOff(0.9f, 0.9f, 0.9f);
			for (Wire* wire : wires)
			{
				Transform* wireTransform = wire->GetTransform();

				WirePlug* plug0 = (WirePlug*)wire->plug0ID.Get();
				Transform* plug0Transform = plug0->GetTransform();
				WirePlug* plug1 = (WirePlug*)wire->plug1ID.Get();
				Transform* plug1Transform = plug1->GetTransform();

				Socket* socket0 = (Socket*)plug0->socketID.Get();
				Socket* socket1 = (Socket*)plug1->socketID.Get();

				GameObject* objectInteractingWith = wire->GetObjectInteractingWith();

				if (objectInteractingWith != nullptr)
				{
					// Wire is being carried
					Transform* playerTransform = objectInteractingWith->GetTransform();

					glm::vec3 playerWorldPos = playerTransform->GetWorldPosition();
					glm::vec3 playerForward = playerTransform->GetForward();
					glm::vec3 playerUp = playerTransform->GetUp();
					glm::vec3 playerRight = playerTransform->GetRight();
					glm::vec3 wireHoldingOffset = playerForward * 5.0f + playerUp * -0.75f;

					glm::vec3 plug0DefaultPos = playerWorldPos + wireHoldingOffset - (Wire::DEFAULT_LENGTH * 0.5f) * playerRight;
					glm::vec3 plug1DefaultPos = playerWorldPos + wireHoldingOffset + (Wire::DEFAULT_LENGTH * 0.5f) * playerRight;

					real dist0 = 0.0f;
					real dist1 = 0.0f;
					Socket* nearbySocket0 = nullptr;
					Socket* nearbySocket1 = nullptr;

					if (socket0 == nullptr)
					{
						nearbySocket0 = GetNearbySocket(plug0DefaultPos, WirePlug::nearbyThreshold, true, dist0);
					}

					if (socket1 == nullptr)
					{
						nearbySocket1 = GetNearbySocket(plug1DefaultPos, WirePlug::nearbyThreshold, true, dist1);
					}

					if (nearbySocket1 != nullptr && nearbySocket0 == nearbySocket1)
					{
						glm::vec3 socketPos = nearbySocket0->GetTransform()->GetWorldPosition();
						if (dist0 < dist1)
						{
							plug0Transform->SetWorldPosition(socketPos + plug0->posOffset);
							plug1Transform->SetWorldPosition(plug1DefaultPos);
						}
						else
						{
							plug0Transform->SetWorldPosition(plug0DefaultPos);
							plug1Transform->SetWorldPosition(socketPos + plug1->posOffset);
						}
					}
					else
					{
						if (socket0 != nullptr)
						{
							plug0Transform->SetWorldPosition(socket0->GetTransform()->GetWorldPosition() + plug0->posOffset);
						}
						else if (nearbySocket0 != nullptr)
						{
							plug0Transform->SetWorldPosition(nearbySocket0->GetTransform()->GetWorldPosition() + plug0->posOffset);
						}

						if (socket1 != nullptr)
						{
							plug1Transform->SetWorldPosition(socket1->GetTransform()->GetWorldPosition() + plug0->posOffset);
						}
						else if (nearbySocket1 != nullptr)
						{
							plug1Transform->SetWorldPosition(nearbySocket1->GetTransform()->GetWorldPosition() + plug0->posOffset);
						}
					}

					// Not plugged in, and not nearby any sockets - follow player.
					if (socket0 == nullptr && nearbySocket0 == nullptr)
					{
						plug0Transform->SetWorldPosition(plug0DefaultPos);
					}

					if (socket1 == nullptr && nearbySocket1 == nullptr)
					{
						plug1Transform->SetWorldPosition(plug1DefaultPos);
					}
				}
				else
				{
					if (socket0 != nullptr)
					{
						plug0Transform->SetWorldPosition(socket0->GetTransform()->GetWorldPosition() + plug0->posOffset);
					}
					if (socket1 != nullptr)
					{
						plug1Transform->SetWorldPosition(socket1->GetTransform()->GetWorldPosition() + plug0->posOffset);
					}
				}

				glm::vec3 plug0Pos = plug0Transform->GetWorldPosition();
				glm::vec3 plug1Pos = plug1Transform->GetWorldPosition();

				wireTransform->SetWorldPosition(plug0Pos + (plug1Pos - plug0Pos) * 0.5f);

				btVector3 plug0PosBt = ToBtVec3(plug0Pos);
				btVector3 plug1PosBt = ToBtVec3(plug1Pos);

				bool bWireOn0 = socket0 != nullptr && (socket0->parent->outputSignals[socket0->slotIdx] != -1);
				bool bWireOn1 = socket1 != nullptr && (socket1->parent->outputSignals[socket1->slotIdx] != -1);
				btVector3 wireCol = (bWireOn0 || bWireOn1) ? wireColOn : wireColOff;
				debugDrawer->drawLine(plug0PosBt, plug1PosBt, wireCol, wireCol);
				debugDrawer->drawSphere(plug0PosBt, 0.2f, wireColOff);
				debugDrawer->drawSphere(plug1PosBt, 0.2f, wireColOff);
			}
		}
	}

	i32 PluggablesSystem::GetReceivedSignal(Socket* socket)
	{
		i32 result = -1;
		for (Wire* wire : wires)
		{
			WirePlug* plug0 = (WirePlug*)wire->plug0ID.Get();
			WirePlug* plug1 = (WirePlug*)wire->plug1ID.Get();

			if (plug0->socketID == socket->ID)
			{
				if (plug1->socketID.IsValid())
				{
					Socket* socket1 = (Socket*)plug1->socketID.Get();
					i32 sendSignal = socket1->parent->outputSignals[socket1->slotIdx];
					result = glm::max(result, sendSignal);
				}
			}
			else if (plug1->socketID == socket->ID)
			{
				if (plug1->socketID.IsValid())
				{
					Socket* socket0 = (Socket*)plug0->socketID.Get();
					i32 sendSignal = socket0->parent->outputSignals[socket0->slotIdx];
					result = glm::max(result, sendSignal);
				}
			}
		}
		return result;
	}

	Wire* PluggablesSystem::AddWire(const GameObjectID& gameObjectID /* = InvalidGameObjectID */)
	{
		Wire* newWire = new Wire(g_SceneManager->CurrentScene()->GetUniqueObjectName("wire_", 3), gameObjectID);

		// Plugs were not found, create new ones
		WirePlug* plug0 = new WirePlug("wire plug 0", InvalidGameObjectID);
		WirePlug* plug1 = new WirePlug("wire plug 1", InvalidGameObjectID);

		newWire->AddChildImmediate(plug0);
		newWire->AddChildImmediate(plug1);

		newWire->plug0ID = plug0->ID;
		newWire->plug1ID = plug1->ID;

		wires.push_back(newWire);

		return newWire;
	}

	bool PluggablesSystem::DestroySocket(Socket* socket)
	{
		// Unplug any wire plugs plugged into this socket before removing it
		for (auto iter = wires.begin(); iter != wires.end(); ++iter)
		{
			Wire* wire = *iter;
			WirePlug* plug0 = (WirePlug*)wire->plug0ID.Get();
			WirePlug* plug1 = (WirePlug*)wire->plug1ID.Get();

			if (plug0->socketID == socket->ID)
			{
				plug0->Unplug(socket);

				RemoveSocket(plug0->socketID);
				return true;
			}
			if (plug1->socketID == socket->ID)
			{
				plug1->Unplug(socket);

				RemoveSocket(plug1->socketID);
				return true;
			}
		}

		return RemoveSocket(socket->ID);
	}

	bool PluggablesSystem::RemoveSocket(const GameObjectID& socketID)
	{
		for (auto iter = sockets.begin(); iter != sockets.end(); ++iter)
		{
			if ((*iter)->ID == socketID)
			{
				sockets.erase(iter);
				return true;
			}
		}
		return false;
	}

	bool PluggablesSystem::DestroyWire(Wire* wire)
	{
		BaseScene* scene = g_SceneManager->CurrentScene();

		WirePlug* plug0 = (WirePlug*)wire->plug0ID.Get();
		WirePlug* plug1 = (WirePlug*)wire->plug1ID.Get();

		for (auto iter = wires.begin(); iter != wires.end(); ++iter)
		{
			if ((*iter)->ID == wire->ID)
			{
				if (plug0->socketID.IsValid())
				{
					plug0->Unplug((Socket*)plug0->socketID.Get());
					RemoveSocket(plug0->socketID);
					scene->RemoveObject(plug0->socketID, true);
				}
				if (plug1->socketID.IsValid())
				{
					plug1->Unplug((Socket*)plug1->socketID.Get());
					RemoveSocket(plug1->socketID);
					scene->RemoveObject(plug1->socketID, true);
				}
				scene->RemoveObject(plug0, true);
				scene->RemoveObject(plug1, true);
				scene->RemoveObject(wire, true);

				wires.erase(iter);

				return true;
			}
		}
		return false;
	}

	Socket* PluggablesSystem::AddSocket(const std::string& name, const GameObjectID& gameObjectID)
	{
		Socket* newSocket = new Socket(name, gameObjectID);
		sockets.push_back(newSocket);

		return newSocket;
	}

	Socket* PluggablesSystem::AddSocket(Socket* socket, i32 slotIdx /* = 0 */)
	{
		socket->slotIdx = slotIdx;
		sockets.push_back(socket);

		return socket;
	}

	Socket* PluggablesSystem::GetSocketAtOtherEnd(Socket* socket)
	{
		WirePlug* plug = (WirePlug*)socket->connectedPlugID.Get();
		if (plug != nullptr)
		{
			Wire* wire = (Wire*)plug->wireID.Get();
			WirePlug* otherPlug = (WirePlug*)wire->GetOtherPlug(plug).Get();
			return (Socket*)otherPlug->socketID.Get();
		}

		return nullptr;
	}

	Socket* PluggablesSystem::GetNearbySocket(const glm::vec3& posWS, real threshold, bool bExcludeFilled, real& outDist2)
	{
		real threshold2 = threshold * threshold;
		real closestDist2 = threshold2;
		Socket* closestSocket = nullptr;
		for (Socket* socket : sockets)
		{
			if (!bExcludeFilled || !socket->connectedPlugID.IsValid())
			{
				real dist2 = glm::distance2(socket->GetTransform()->GetWorldPosition(), posWS);
				if (dist2 < closestDist2)
				{
					closestSocket = socket;
					closestDist2 = dist2;
				}
			}
		}

		if (closestSocket != nullptr)
		{
			outDist2 = closestDist2;
			return closestSocket;
		}

		outDist2 = FLT_MAX;
		return nullptr;
	}

	RoadManager::RoadManager()
	{
	}

	RoadManager::~RoadManager()
	{
	}

	void RoadManager::Initialize()
	{
	}

	void RoadManager::Destroy()
	{
		m_RoadIDs.clear();
	}

	void RoadManager::Update()
	{
	}

	void RoadManager::DrawImGui()
	{
		if (ImGui::TreeNode("Road Manager"))
		{
			BaseScene* scene = g_SceneManager->CurrentScene();
			for (u32 i = 0; i < (u32)m_RoadIDs.size(); ++i)
			{
				char buf[256];
				sprintf(buf, "Road segment %i:", i);
				scene->DrawImGuiGameObjectIDField(buf, m_RoadIDs[i]);
			}

			ImGui::TreePop();
		}
	}


	void RoadManager::RegisterRoad(Road* road)
	{
		if (!Contains(m_RoadIDs, road->ID))
		{
			m_RoadIDs.push_back(road->ID);
		}
	}

	void RoadManager::DeregisterRoad(Road* road)
	{
		for (auto iter = m_RoadIDs.begin(); iter != m_RoadIDs.end(); ++iter)
		{
			if (*iter == road->ID)
			{
				m_RoadIDs.erase(iter);
				return;
			}
		}
	}

	void RoadManager::RegenerateAllRoads()
	{
		BaseScene* scene = g_SceneManager->CurrentScene();
		for (const GameObjectID& roadID : m_RoadIDs)
		{
			Road* road = static_cast<Road*>(scene->GetGameObject(roadID));
			if (road != nullptr)
			{
				road->RegenerateMesh();
			}
		}
	}

	TerminalManager::TerminalManager()
	{
		m_ScriptDirectoryWatch = new DirectoryWatcher(SCRIPTS_DIRECTORY, true);
	}

	TerminalManager::~TerminalManager()
	{
		delete m_ScriptDirectoryWatch;
	}

	void TerminalManager::Initialize()
	{
		std::vector<std::string> modifiedFiles;
		UpdateScriptHashes(modifiedFiles);
	}

	void TerminalManager::Destroy()
	{
	}

	u64 TerminalManager::CalculteChecksum(const std::string& filePath)
	{
		u64 checksum = 0;

		std::string fileContents;
		if (FileExists(filePath) && ReadFile(filePath, fileContents, false))
		{
			u32 i = 1;
			for (char c : fileContents)
			{
				checksum += (u64)i * (u64)c;
				++i;
			}
		}
		return checksum;
	}

	void TerminalManager::UpdateScriptHashes(std::vector<std::string>& outModifiedFiles)
	{
		std::vector<std::string> scriptFilePaths;
		if (Platform::FindFilesInDirectory(SCRIPTS_DIRECTORY, scriptFilePaths, ".flex_script"))
		{
			for (const std::string& scriptFilePath : scriptFilePaths)
			{
				u64 hash = CalculteChecksum(scriptFilePath);
				auto iter = m_ScriptHashes.find(scriptFilePath);
				if (iter == m_ScriptHashes.end())
				{
					// New script
					m_ScriptHashes.emplace(scriptFilePath, hash);
				}
				else
				{
					// Existing script
					if (iter->second != hash)
					{
						// File was modified
						std::string scriptFileName = StripLeadingDirectories(scriptFilePath);
						outModifiedFiles.push_back(scriptFileName);
						m_ScriptHashes[scriptFilePath] = hash;
					}
				}
			}
		}
	}

	void TerminalManager::Update()
	{
		if (m_ScriptSaveTimer != 0)
		{
			--m_ScriptSaveTimer;
		}

		if (m_ScriptDirectoryWatch->Update())
		{
			std::vector<std::string> modifiedFiles;
			UpdateScriptHashes(modifiedFiles);

			for (Terminal* terminal : m_Terminals)
			{
				if (Contains(modifiedFiles, terminal->m_ScriptFileName))
				{
					// Ignore updates to files we just saved
					if (m_ScriptSaveTimer == 0 || terminal->m_ScriptFileName != m_LastSavedScriptFileName)
					{
						terminal->OnScriptChanged();
					}
				}
			}
		}
	}

	void TerminalManager::DrawImGui()
	{
		if (ImGui::TreeNode("Terminal Manager"))
		{
			BaseScene* scene = g_SceneManager->CurrentScene();
			for (i32 i = 0; i < (i32)m_Terminals.size(); ++i)
			{
				char buf[256];
				sprintf(buf, "Terminal %i:", i);
				scene->DrawImGuiGameObjectIDField(buf, m_Terminals[i]->ID, true);
			}

			ImGui::TreePop();
		}

		static char newScriptNameBuffer[256];
		bool bFocusTextBox = false;
		if (m_bOpenSavePopup)
		{
			m_bOpenSavePopup = false;
			bFocusTextBox = true;
			strncpy(newScriptNameBuffer, ".flex_script", 256);
			ImGui::OpenPopup(SavePopupName);
		}

		if (ImGui::BeginPopupModal(SavePopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			bool bNameEntered = ImGui::InputText("Name", newScriptNameBuffer, 256, ImGuiInputTextFlags_EnterReturnsTrue);
			if (bFocusTextBox)
			{
				ImGui::SetKeyboardFocusHere();
			}

			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			bNameEntered = ImGui::Button("Save") || bNameEntered;

			if (bNameEntered)
			{
				std::string newScriptPath = std::string(newScriptNameBuffer);
				newScriptPath = Trim(newScriptPath);

				bool bNameIsValid = true;

				size_t finalDot = newScriptPath.rfind('.');
				if (finalDot == std::string::npos || finalDot == newScriptPath.length() - 1)
				{
					bNameIsValid = false;
				}
				else
				{
					std::string fileType = newScriptPath.substr(finalDot + 1);
					bNameIsValid = (fileType == "flex_script");
				}

				if (newScriptPath.find('\\') != std::string::npos || newScriptPath.find('/') != std::string::npos)
				{
					bNameIsValid = false;
				}

				if (bNameIsValid)
				{
					newScriptPath = SCRIPTS_DIRECTORY + newScriptPath;

					Terminal* terminal = (Terminal*)g_SceneManager->CurrentScene()->GetGameObject(m_TerminalSavingID);
					bool bSaved = SaveScript(newScriptPath, terminal->lines);

					if (bSaved)
					{
						terminal->m_ScriptFileName = newScriptPath;
						ImGui::CloseCurrentPopup();
					}
					else
					{
						std::string terminalIDStr = m_TerminalSavingID.ToString();
						PrintError("Failed to save terminal script, invalid terminal game object ID given (%s)\n", terminalIDStr.c_str());
					}
				}
				else
				{
					PrintError("Invalid terminal script name provided: \"%s\"\n", newScriptPath.c_str());
				}
			}

			ImGui::EndPopup();
		}
	}

	void TerminalManager::OpenSavePopup(const GameObjectID& terminalID)
	{
		m_TerminalSavingID = terminalID;
		m_bOpenSavePopup = true;
	}

	void TerminalManager::RegisterTerminal(Terminal* terminal)
	{
		m_Terminals.push_back(terminal);
	}

	void TerminalManager::DeregisterTerminal(Terminal* terminal)
	{
		for (i32 i = 0; i < (i32)m_Terminals.size(); ++i)
		{
			if (m_Terminals[i]->ID == terminal->ID)
			{
				m_Terminals.erase(m_Terminals.begin() + i);
				break;
			}
		}
	}

	bool TerminalManager::LoadScript(const std::string& fileName, std::vector<std::string>& outFileLines)
	{
		std::string filePath = SCRIPTS_DIRECTORY + fileName;
		if (FileExists(filePath))
		{
			std::string fileContent;
			if (ReadFile(filePath, fileContent, false))
			{
				outFileLines = SplitNoStrip(fileContent, '\n');

				if (outFileLines.empty())
				{
					outFileLines.emplace_back("");
				}

				return true;
			}
		}

		return false;
	}

	bool TerminalManager::SaveScript(const std::string& fileName, const std::vector<std::string>& fileLines)
	{
		std::string filePath = SCRIPTS_DIRECTORY + fileName;
		StringBuilder stringBuilder;
		for (i32 i = 0; i < (i32)fileLines.size(); ++i)
		{
			if (!fileLines[i].empty())
			{
				stringBuilder.Append(fileLines[i]);

				if (i < (i32)fileLines.size() - 1)
				{
					stringBuilder.Append('\n');
				}
			}
			else
			{
				stringBuilder.Append('\n');
			}
		}

		std::string fileContent = stringBuilder.ToString();

		bool bSuccess = WriteFile(filePath, fileContent, false);

		if (bSuccess)
		{
			m_ScriptSaveTimer = 2;
			m_LastSavedScriptFileName = fileName;
		}

		return bSuccess;
	}
} // namespace flex
