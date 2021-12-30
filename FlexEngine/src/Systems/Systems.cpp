#include "stdafx.hpp"

#include "Systems/Systems.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtx/norm.hpp> // For distance2
IGNORE_WARNINGS_POP

#include "Graphics/Renderer.hpp" // For PhysicsDebugDrawBase
#include "Helpers.hpp"
#include "Platform/Platform.hpp" // For DirectoryWatcher
#include "Player.hpp"
#include "ResourceManager.hpp"
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
		m_PlugInAudioSourceID = g_ResourceManager->GetOrLoadAudioSourceID(SID("latch-open-19.wav"), true);
		m_UnplugAudioSourceID = g_ResourceManager->GetOrLoadAudioSourceID(SID("latch-closing-09.wav"), true);
	}

	void PluggablesSystem::Destroy()
	{
		wires.clear();
	}

	void PluggablesSystem::Update()
	{
		if (!wires.empty())
		{
			BaseScene* scene = g_SceneManager->CurrentScene();

			btVector3 wireColOn(sin(g_SecElapsedSinceProgramStart * 3.5f) * 0.4f + 0.6f, 0.2f, 0.2f);
			static const btVector3 wireColPluggedIn(0.05f, 0.6f, 0.1f);
			static const btVector3 wireColOff(0.9f, 0.9f, 0.9f);

			Player* player = scene->GetPlayer(0);
			Transform* playerTransform = player->GetTransform();

			glm::vec3 playerWorldPos = playerTransform->GetWorldPosition();
			glm::vec3 playerForward = playerTransform->GetForward();
			glm::vec3 playerUp = playerTransform->GetUp();
			glm::vec3 playerRight = playerTransform->GetRight();
			glm::vec3 wireHoldingOffset = playerForward * 5.0f + playerUp * -0.75f;

			glm::vec3 plugLDefaultPos = playerWorldPos + wireHoldingOffset - (Wire::DEFAULT_LENGTH * 0.5f) * playerRight;
			glm::vec3 plugRDefaultPos = playerWorldPos + wireHoldingOffset + (Wire::DEFAULT_LENGTH * 0.5f) * playerRight;

			const real hoverAmount = TWEAKABLE(0.1f);

			// Handle plugging/unplugging events
			for (Wire* wire : wires)
			{
				Transform* wireTransform = wire->GetTransform();

				WirePlug* plug0 = (WirePlug*)wire->plug0ID.Get();
				WirePlug* plug1 = (WirePlug*)wire->plug1ID.Get();
				if (plug0 == nullptr || plug1 == nullptr)
				{
					// If wire was added this frame its plug won't have been registered yet
					continue;
				}
				Transform* plug0Transform = plug0->GetTransform();
				Transform* plug1Transform = plug1->GetTransform();

				{
					glm::vec3 plug0Pos = plug0Transform->GetWorldPosition();
					glm::vec3 plug1Pos = plug1Transform->GetWorldPosition();
					if (IsNanOrInf(plug0Pos) || IsNanOrInf(plug1Pos))
					{
						DEBUG_BREAK();
					}
					if (!NearlyEquals(plug0Pos, plug1Pos, 0.001f))
					{
						// Keep wire position in between plug ends
						wireTransform->SetWorldPosition(plug0Pos + (plug1Pos - plug0Pos) * 0.5f);
					}
					else
					{
						wireTransform->SetWorldPosition(plug0Pos);
					}
				}

				Socket* socket0 = (Socket*)plug0->socketID.Get();
				Socket* socket1 = (Socket*)plug1->socketID.Get();

				Socket* nearbySocket0 = nullptr;
				Socket* nearbySocket1 = nullptr;

				if (socket0 != nullptr)
				{
					// Plugged in, stick to socket
					glm::vec3 socketPos = socket0->GetTransform()->GetWorldPosition();
					plug0Transform->SetWorldPosition(socketPos);

					glm::quat plug0Rot = socket0->GetTransform()->GetWorldRotation();
					plug0Transform->SetWorldRotation(plug0Rot);
					wire->SetStartTangent(-(plug0Rot * VEC3_FORWARD));
				}
				else if (player->IsHolding(plug0))
				{
					// Plug is being carried, stick to player but look for nearby sockets to snap to
					bool bLeftHand = player->heldItemLeftHand == plug0->ID;
					glm::vec3 plugDefaultPos = bLeftHand ? plugLDefaultPos : plugRDefaultPos;
					nearbySocket0 = GetNearbySocket(plugDefaultPos, WirePlug::nearbyThreshold, true);
					if (nearbySocket0 != nullptr)
					{
						// Found nearby socket
						glm::vec3 nearbySocketPos = nearbySocket0->GetTransform()->GetWorldPosition();
						glm::quat nearbySocketRot = nearbySocket0->GetTransform()->GetWorldRotation();
						glm::vec3 tangent = -(nearbySocketRot * VEC3_FORWARD);
						plug0Transform->SetWorldPosition(nearbySocketPos + tangent * hoverAmount, false);

						plug0Transform->SetWorldRotation(nearbySocketRot);
						wire->SetStartTangent(tangent);
					}
					else
					{
						// Stick to player
						plug0Transform->SetWorldPosition(plugDefaultPos);
					}
				}
				else
				{
					// Plug isn't being held, and isn't plugged in. Rest.
				}

				if (socket1 != nullptr)
				{
					// Plugged in, stick to socket
					glm::vec3 socketPos = socket1->GetTransform()->GetWorldPosition();
					plug1Transform->SetWorldPosition(socketPos, false);

					glm::quat plug1Rot = socket1->GetTransform()->GetWorldRotation();
					plug1Transform->SetWorldRotation(plug1Rot);
					wire->SetEndTangent(-(plug1Rot * VEC3_FORWARD));
				}
				else if (player->IsHolding(plug1))
				{
					// Plug is being carried, stick to player but look for nearby sockets to snap to
					bool bLeftHand = player->heldItemLeftHand == plug1->ID;
					glm::vec3 plugDefaultPos = bLeftHand ? plugLDefaultPos : plugRDefaultPos;
					nearbySocket1 = GetNearbySocket(plugDefaultPos, WirePlug::nearbyThreshold, true, nearbySocket0);
					if (nearbySocket1 != nullptr)
					{
						// Found nearby socket
						glm::vec3 nearbySocketPos = nearbySocket1->GetTransform()->GetWorldPosition();
						glm::quat nearbySocketRot = nearbySocket1->GetTransform()->GetWorldRotation();
						glm::vec3 tangent = -(nearbySocketRot * VEC3_FORWARD);
						plug1Transform->SetWorldPosition(nearbySocketPos + tangent * hoverAmount, false);

						plug1Transform->SetWorldRotation(nearbySocketRot);
						wire->SetStartTangent(tangent);
					}
					else
					{
						// Stick to player
						plug1Transform->SetWorldPosition(plugDefaultPos);
					}
				}
				else
				{
					// Plug isn't being held, and isn't plugged in. Rest.
				}

				if (socket0 == nullptr && nearbySocket0 == nullptr)
				{
					// Update socket based on wire tangent
					glm::vec3 wireStartTangent;
					wire->CalculateTangentAtPoint(0.0f, wireStartTangent);
					plug0Transform->SetWorldRotation(SafeQuatLookAt(-wireStartTangent));
					wire->ClearStartTangent();
				}

				if (socket1 == nullptr && nearbySocket1 == nullptr)
				{
					// Update socket based on wire tangent
					glm::vec3 wireEndTangent;
					wire->CalculateTangentAtPoint(1.0f, wireEndTangent);
					plug1Transform->SetWorldRotation(SafeQuatLookAt(wireEndTangent));
					wire->ClearEndTangent();
				}

				wire->UpdateWireMesh();

				glm::vec3 plug0Pos = plug0Transform->GetWorldPosition();
				glm::vec3 plug1Pos = plug1Transform->GetWorldPosition();

				if (IsNanOrInf(plug0Pos) || IsNanOrInf(plug1Pos))
				{
					DEBUG_BREAK();
				}

				if (glm::distance2(plug0Pos, plug1Pos) > maxDistBeforeSnapSq)
				{
					bool plug0PluggedIn = plug0->socketID.IsValid();
					bool plug1PluggedIn = plug1->socketID.IsValid();

					bool plug0Held = player->IsHolding(plug0);
					bool plug1Held = player->IsHolding(plug1);

					if (!plug0PluggedIn && !plug0Held && plug1Held && player->HasFreeHand())
					{
						// Pick up loose end 0
						player->PickupWithFreeHand(plug0);
					}
					else if (!plug1PluggedIn && !plug1Held && plug0Held && player->HasFreeHand())
					{
						// Pick up loose end 1
						player->PickupWithFreeHand(plug1);
					}
					else if (plug0PluggedIn)
					{
						// Unplug and pickup 0
						UnplugFromSocket(plug0);
						if (plug1Held && player->HasFreeHand())
						{
							player->PickupWithFreeHand(plug0);
						}
					}
					else if (plug1PluggedIn)
					{
						// Unplug and pickup 1
						UnplugFromSocket(plug1);
						if (plug0Held && player->HasFreeHand())
						{
							player->PickupWithFreeHand(plug1);
						}
					}
				}

#if 0
				btVector3 plug0PosBt = ToBtVec3(plug0Pos);
				btVector3 plug1PosBt = ToBtVec3(plug1Pos);

				bool bWire0PluggedIn = socket0 != nullptr;
				bool bWire1PluggedIn = socket1 != nullptr;
				bool bWire0On = bWire0PluggedIn && (socket0->GetParent()->outputSignals[socket0->slotIdx] != -1);
				bool bWire1On = bWire1PluggedIn && (socket1->GetParent()->outputSignals[socket1->slotIdx] != -1);
				debugDrawer->drawSphere(plug0PosBt, 0.2f, bWire0PluggedIn ? wireColPluggedIn : (bWire0On ? wireColOn : wireColOff));
				debugDrawer->drawSphere(plug1PosBt, 0.2f, bWire1PluggedIn ? wireColPluggedIn : (bWire1On ? wireColOn : wireColOff));
#endif
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
					i32 sendSignal = socket1->GetParent()->outputSignals[socket1->slotIdx];
					result = glm::max(result, sendSignal);
				}
			}
			else if (plug1->socketID == socket->ID)
			{
				if (plug1->socketID.IsValid())
				{
					Socket* socket0 = (Socket*)plug0->socketID.Get();
					i32 sendSignal = socket0->GetParent()->outputSignals[socket0->slotIdx];
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
		PrefabID wirePlugID = g_ResourceManager->GetPrefabID("wire plug");
		GameObject* wirePlugTemplate = g_ResourceManager->GetPrefabTemplate(wirePlugID);
		GameObject::CopyFlags copyFlags = (GameObject::CopyFlags)((u32)GameObject::CopyFlags::ALL & ~(u32)GameObject::CopyFlags::ADD_TO_SCENE);
		WirePlug* plug0 = (WirePlug*)wirePlugTemplate->CopySelf(nullptr, copyFlags);
		WirePlug* plug1 = (WirePlug*)wirePlugTemplate->CopySelf(nullptr, copyFlags);

		plug0->wireID = newWire->ID;
		plug1->wireID = newWire->ID;

		newWire->plug0ID = plug0->ID;
		newWire->plug1ID = plug1->ID;

		newWire->AddSibling(plug0);
		newWire->AddSibling(plug1);

		plug0->GetTransform()->SetLocalPosition(glm::vec3(-1.0f, 0.0f, 0.0f));
		plug1->GetTransform()->SetLocalPosition(glm::vec3(1.0f, 0.0f, 0.0f));

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
				UnplugFromSocket(plug0);
				RemoveSocket(plug0->socketID);
				return true;
			}

			if (plug1->socketID == socket->ID)
			{
				UnplugFromSocket(plug1);
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
		Player* player = scene->GetPlayer(0);

		WirePlug* plug0 = (WirePlug*)wire->plug0ID.Get();
		WirePlug* plug1 = (WirePlug*)wire->plug1ID.Get();

		for (auto iter = wires.begin(); iter != wires.end(); ++iter)
		{
			if ((*iter)->ID == wire->ID)
			{
				player->DropIfHolding(plug0);
				player->DropIfHolding(plug1);

				if (plug0 != nullptr)
				{
					if (plug0->socketID.IsValid())
					{
						UnplugFromSocket(plug0);
					}
					scene->RemoveObject(plug0, true);
				}
				if (plug1 != nullptr)
				{
					if (plug1->socketID.IsValid())
					{
						UnplugFromSocket(plug1);
					}
					scene->RemoveObject(plug1, true);
				}
				scene->RemoveObject(wire, true);

				wires.erase(iter);

				return true;
			}
		}
		return false;
	}

	WirePlug* PluggablesSystem::AddWirePlug(const GameObjectID& gameObjectID /* = InvalidGameObjectID */)
	{
		WirePlug* plug = new WirePlug("wire plug", gameObjectID);
		wirePlugs.push_back(plug);
		return plug;
	}

	bool PluggablesSystem::DestroyWirePlug(WirePlug* wirePlug)
	{
		for (auto iter = wirePlugs.begin(); iter != wirePlugs.end(); ++iter)
		{
			if ((*iter)->ID == wirePlug->ID)
			{
				// TODO: Check for wires/sockets interacting with plug
				wirePlugs.erase(iter);
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
			if (wire == nullptr)
			{
				PrintWarn("Plug lost reference to its wire\n");
				return nullptr;
			}
			WirePlug* otherPlug = (WirePlug*)wire->GetOtherPlug(plug).Get();
			return (Socket*)otherPlug->socketID.Get();
		}

		return nullptr;
	}

	Socket* PluggablesSystem::GetNearbySocket(const glm::vec3& posWS, real threshold, bool bExcludeFilled, Socket* excludeSocket)
	{
		real threshold2 = threshold * threshold;
		real closestDist2 = threshold2;
		Socket* closestSocket = nullptr;
		for (Socket* socket : sockets)
		{
			if (socket != excludeSocket && (!bExcludeFilled || !socket->connectedPlugID.IsValid()))
			{
				real dist2 = glm::distance2(socket->GetTransform()->GetWorldPosition(), posWS);
				if (dist2 < closestDist2)
				{
					closestSocket = socket;
					closestDist2 = dist2;
				}
			}
		}

		return closestSocket;
	}

	Socket* PluggablesSystem::GetNearbySocket(const glm::vec3& posWS, real threshold, bool bExcludeFilled)
	{
		return GetNearbySocket(posWS, threshold, bExcludeFilled, nullptr);
	}

	bool PluggablesSystem::PlugInToNearbySocket(WirePlug* plug, real nearbyThreshold)
	{
		Socket* nearbySocket = GetNearbySocket(plug->GetTransform()->GetWorldPosition(), nearbyThreshold, true);
		if (nearbySocket != nullptr)
		{
			PlugInToSocket(plug, nearbySocket);
			return true;
		}
		return false;
	}

	void PluggablesSystem::PlugInToSocket(WirePlug* plug, Socket* socket)
	{
		plug->PlugIn(socket);
		socket->OnPlugIn(plug);

		AudioManager::PlaySource(m_PlugInAudioSourceID);
	}

	void PluggablesSystem::UnplugFromSocket(WirePlug* plug)
	{
		Socket* socket = (Socket*)plug->socketID.Get();
		if (socket != nullptr)
		{
			socket->OnUnPlug();
		}
		plug->Unplug();

		AudioManager::PlaySource(m_UnplugAudioSourceID);
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
			}

			if (i < (i32)fileLines.size() - 1)
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

	PropertyCollection* PropertyCollectionManager::GetCollectionForObject(const GameObjectID& gameObjectID)
	{
		auto iter = m_RegisteredObjects.find(gameObjectID);
		if (iter != m_RegisteredObjects.end())
		{
			return iter->second;
		}
		return nullptr;
	}

	PropertyCollection* PropertyCollectionManager::RegisterObject(const GameObjectID& gameObjectID)
	{
		auto iter = m_RegisteredObjects.find(gameObjectID);
		if (iter != m_RegisteredObjects.end())
		{
			GameObject* gameObject = gameObjectID.Get();
			std::string gameObjectName = gameObject != nullptr ? gameObject->GetName() : gameObjectID.ToString();
			PrintWarn("Attempted to register object with PropertyCollectionManager multiple times! %s\n", gameObjectName.c_str());
		}

		PropertyCollection* result = m_Allocator.Alloc();
		m_RegisteredObjects.emplace(gameObjectID, result);
		return result;
	}

	bool PropertyCollectionManager::DeregisterObject(const GameObjectID& gameObjectID)
	{
		auto iter = m_RegisteredObjects.find(gameObjectID);
		if (iter != m_RegisteredObjects.end())
		{
			m_RegisteredObjects.erase(iter);
			return true;
		}
		return false;
	}

	void PropertyCollectionManager::DeserializeObjectIfPresent(const GameObjectID& gameObjectID, const JSONObject& parentObject)
	{
		PropertyCollection* collection = GetCollectionForObject(gameObjectID);
		if (collection != nullptr)
		{
			collection->Deserialize(parentObject);
		}
	}

	bool PropertyCollectionManager::SerializeObjectIfPresent(const GameObjectID& gameObjectID, JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetCollectionForObject(gameObjectID);
		if (collection != nullptr)
		{
			return collection->SerializeGameObjectFields(parentObject, gameObjectID, bSerializePrefabData);
		}
		return false;
	}
} // namespace flex
