#include "stdafx.hpp"

#include "Systems/Systems.hpp"

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
			static const real defaultWireLength = 2.0f;
			for (Wire* wire : wires)
			{
				Socket* socket0 = nullptr;
				Socket* socket1 = nullptr;

				if (wire->socket0ID.IsValid())
				{
					socket0 = (Socket*)g_SceneManager->CurrentScene()->GetGameObject(wire->socket0ID);
				}

				if (wire->socket1ID.IsValid())
				{
					socket1 = (Socket*)g_SceneManager->CurrentScene()->GetGameObject(wire->socket1ID);
				}

				if (wire->GetObjectInteractingWith() != nullptr)
				{
					GameObject* interacting = wire->GetObjectInteractingWith();
					Transform* interactingTransform = interacting->GetTransform();
					glm::vec3 interactingWorldPos = interactingTransform->GetWorldPosition();
					glm::vec3 wireHoldingOffset = interactingTransform->GetForward() * 5.0f + interactingTransform->GetUp() * -0.75f;
					if (socket0 != nullptr && socket1 != nullptr)
					{
						wire->startPoint = socket0->GetTransform()->GetWorldPosition();
						wire->endPoint = socket1->GetTransform()->GetWorldPosition();
					}
					else if (socket0 != nullptr)
					{
						wire->startPoint = socket0->GetTransform()->GetWorldPosition();
						wire->endPoint = interactingWorldPos + wireHoldingOffset + interactingTransform->GetRight() * defaultWireLength;
					}
					else if (socket1 != nullptr)
					{
						wire->startPoint = interactingWorldPos + wireHoldingOffset;
						wire->endPoint = socket1->GetTransform()->GetWorldPosition();
					}
					else
					{
						wire->startPoint = interactingWorldPos + wireHoldingOffset;
						wire->endPoint = wire->startPoint + interactingTransform->GetRight() * defaultWireLength;
					}
				}
				else
				{
					if (socket0 != nullptr)
					{
						wire->startPoint = socket0->GetTransform()->GetWorldPosition();
					}
					else
					{
						if (socket1 != nullptr)
						{
							wire->endPoint = socket1->GetTransform()->GetWorldPosition();
						}
					}
					if (socket1 != nullptr)
					{
						wire->endPoint = socket1->GetTransform()->GetWorldPosition();
					}
					else
					{
						if (socket0 != nullptr)
						{
							wire->endPoint = wire->startPoint + defaultWireLength;
						}
					}
				}

				wire->GetTransform()->SetWorldPosition(wire->startPoint + (wire->endPoint - wire->startPoint) / 2.0f);

				bool bWireOn0 = socket0 != nullptr && (socket0->parent->outputSignals[socket0->slotIdx] != -1);
				bool bWireOn1 = socket1 != nullptr && (socket1->parent->outputSignals[socket1->slotIdx] != -1);
				btVector3 wireCol = (bWireOn0 || bWireOn1) ? wireColOn : wireColOff;
				debugDrawer->drawLine(ToBtVec3(wire->startPoint), ToBtVec3(wire->endPoint), wireCol, wireCol);
				debugDrawer->drawSphere(ToBtVec3(wire->startPoint), 0.2f, wireColOff);
				debugDrawer->drawSphere(ToBtVec3(wire->endPoint), 0.2f, wireColOff);
			}
		}
	}

	i32 PluggablesSystem::GetReceivedSignal(Socket* socket)
	{
		i32 result = -1;
		for (Wire* wire : wires)
		{
			if (wire->socket0ID == socket->ID)
			{
				if (wire->socket1ID.IsValid())
				{
					Socket* socket1 = (Socket*)g_SceneManager->CurrentScene()->GetGameObject(wire->socket1ID);
					i32 sendSignal = socket1->parent->outputSignals[socket1->slotIdx];
					result = glm::max(result, sendSignal);
				}
			}
			else if (wire->socket1ID == socket->ID)
			{
				if (wire->socket0ID.IsValid())
				{
					Socket* socket0 = (Socket*)g_SceneManager->CurrentScene()->GetGameObject(wire->socket0ID);
					i32 sendSignal = socket0->parent->outputSignals[socket0->slotIdx];
					result = glm::max(result, sendSignal);
				}
			}
		}
		return result;
	}

	Wire* PluggablesSystem::AddWire(const GameObjectID& gameObjectID, Socket* socket0 /* = nullptr */, Socket* socket1 /* = nullptr */)
	{
		Wire* newWire = new Wire(g_SceneManager->CurrentScene()->GetUniqueObjectName("wire_", 3), gameObjectID);
		newWire->socket0ID = (socket0 == nullptr ? InvalidGameObjectID : socket0->ID);
		newWire->socket1ID = (socket1 == nullptr ? InvalidGameObjectID : socket1->ID);
		wires.push_back(newWire);
		if (socket0)
		{
			//socket0->OnConnectionMade(newWire);
		}
		if (socket1)
		{
			//socket1->OnConnectionMade(newWire);
		}

		return newWire;
	}

	bool PluggablesSystem::DestroySocket(Socket* socket)
	{
		// Try to find wire that owns this socket
		for (auto iter = wires.begin(); iter != wires.end(); ++iter)
		{
			Wire* wire = *iter;
			if (wire->socket0ID == socket->ID)
			{
				//wire->socket0->OnConnectionBroke(wire);
				RemoveSocket(wire->socket0ID);
				wire->socket0ID = InvalidGameObjectID;
				if (!wire->socket1ID.IsValid())
				{
					g_SceneManager->CurrentScene()->RemoveObject(wire, true);
					wires.erase(iter);
				}
				return true;
			}
			if (wire->socket1ID == socket->ID)
			{
				//wire->socket1->OnConnectionBroke(wire);
				RemoveSocket(wire->socket1ID);
				wire->socket1ID = InvalidGameObjectID;
				if (!wire->socket0ID.IsValid())
				{
					g_SceneManager->CurrentScene()->RemoveObject(wire, true);
					wires.erase(iter);
				}
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
		for (auto iter = wires.begin(); iter != wires.end(); ++iter)
		{
			if ((*iter)->ID == wire->ID)
			{
				BaseScene* scene = g_SceneManager->CurrentScene();
				if (wire->socket0ID.IsValid())
				{
					RemoveSocket(wire->socket0ID);
					scene->RemoveObject(wire->socket0ID, true);
				}
				if (wire->socket1ID.IsValid())
				{
					RemoveSocket(wire->socket1ID);
					scene->RemoveObject(wire->socket1ID, true);
				}
				scene->RemoveObject(wire, true);

				wires.erase(iter);

				return true;
			}
		}
		return false;
	}


	Socket* PluggablesSystem::AddSocket(const std::string& name, const GameObjectID& gameObjectID, i32 slotIdx /* = 0 */, Wire* connectedWire /* = nullptr */)
	{
		Socket* newSocket = new Socket(name, gameObjectID);
		newSocket->slotIdx = slotIdx;
		newSocket->connectedWire = connectedWire;
		sockets.push_back(newSocket);

		return newSocket;
	}

	Socket* PluggablesSystem::AddSocket(Socket* socket, i32 slotIdx /* = 0 */, Wire* connectedWire /* = nullptr */)
	{
		socket->slotIdx = slotIdx;
		socket->connectedWire = connectedWire;
		sockets.push_back(socket);

		return socket;
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
