#include "stdafx.hpp"

#include "Systems/Systems.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtx/norm.hpp> // For distance2
IGNORE_WARNINGS_POP

#include "Graphics/Renderer.hpp" // For PhysicsDebugDrawBase
#include "Helpers.hpp"
#include "Platform/Platform.hpp" // For DirectoryWatcher
#include "Player.hpp"
#include "PropertyCollection.hpp"
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

	void PluggablesSystem::Initialize()
	{
		PROFILE_AUTO("PluggablesSystem CreateContext");
		m_PlugInAudioSourceID = g_ResourceManager->GetOrLoadAudioSourceID(SID("latch-open-19.wav"), true);
		m_UnplugAudioSourceID = g_ResourceManager->GetOrLoadAudioSourceID(SID("latch-closing-09.wav"), true);
	}

	void PluggablesSystem::Destroy()
	{
		registeredWires.clear();
	}

	void PluggablesSystem::OnPreSceneChange()
	{
		registeredWires.clear();
	}

	void PluggablesSystem::Update()
	{
		if (!registeredWires.empty())
		{
			BaseScene* scene = g_SceneManager->CurrentScene();

			btVector3 wireColOn(sin(g_SecElapsedSinceProgramStart * 3.5f) * 0.4f + 0.6f, 0.2f, 0.2f);
			static const btVector3 wireColPluggedIn(0.05f, 0.6f, 0.1f);
			static const btVector3 wireColOff(0.9f, 0.9f, 0.9f);

			Player* player = scene->GetPlayer(0);
			Transform* playerTransform = player->GetTransform();

			glm::vec3 playerWorldPos = playerTransform->GetWorldPosition();
			glm::vec3 playerForward = player->GetLookDirection();
			glm::vec3 playerUp = playerTransform->GetUp();
			glm::vec3 playerRight = playerTransform->GetRight();
			glm::vec3 wireHoldingOffset = playerForward * 5.0f + playerUp * -0.75f;

			glm::vec3 plugLDefaultPos = playerWorldPos + wireHoldingOffset - (Wire::DEFAULT_LENGTH * 0.5f) * playerRight;
			glm::vec3 plugRDefaultPos = playerWorldPos + wireHoldingOffset + (Wire::DEFAULT_LENGTH * 0.5f) * playerRight;

			const real hoverAmount = TWEAKABLE(0.1f);

			// Handle plugging/unplugging events
			for (Wire* wire : registeredWires)
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
					bool bLeftHand = player->GetHeldItem(Hand::LEFT) == plug0->ID;
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
					bool bLeftHand = player->GetHeldItem(Hand::LEFT) == plug1->ID;
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
		PluggablesSystem* pluggablesSystem = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES);
		for (Wire* wire : registeredWires)
		{
			WirePlug* plug0 = (WirePlug*)wire->plug0ID.Get();
			WirePlug* plug1 = (WirePlug*)wire->plug1ID.Get();

			if (plug0->socketID == socket->ID)
			{
				if (plug1->socketID.IsValid())
				{
					Socket* socket1 = (Socket*)plug1->socketID.Get();
					i32 sendSignal = pluggablesSystem->GetGameObjectOutputSignal(socket1->GetParent()->ID, socket1->slotIdx);
					result = glm::max(result, sendSignal);
				}
			}
			else if (plug1->socketID == socket->ID)
			{
				if (plug1->socketID.IsValid())
				{
					Socket* socket0 = (Socket*)plug0->socketID.Get();
					i32 sendSignal = pluggablesSystem->GetGameObjectOutputSignal(socket0->GetParent()->ID, socket0->slotIdx);
					result = glm::max(result, sendSignal);
				}
			}
		}
		return result;
	}

	Wire* PluggablesSystem::RegisterWire(Wire* wire)
	{
		// Plugs were not found, create new ones
		PrefabID wirePlugID = g_ResourceManager->GetPrefabID("wire plug");
		GameObject* wirePlugTemplate = g_ResourceManager->GetPrefabTemplate(wirePlugID);
		GameObject::CopyFlags copyFlags = (GameObject::CopyFlags)((u32)GameObject::CopyFlags::ALL & ~(u32)GameObject::CopyFlags::ADD_TO_SCENE);
		WirePlug* plug0 = (WirePlug*)wirePlugTemplate->CopySelf(nullptr, copyFlags);
		WirePlug* plug1 = (WirePlug*)wirePlugTemplate->CopySelf(nullptr, copyFlags);

		plug0->wireID = wire->ID;
		plug1->wireID = wire->ID;

		wire->plug0ID = plug0->ID;
		wire->plug1ID = plug1->ID;

		wire->AddSibling(plug0);
		wire->AddSibling(plug1);

		plug0->GetTransform()->SetLocalPosition(glm::vec3(-1.0f, 0.0f, 0.0f));
		plug1->GetTransform()->SetLocalPosition(glm::vec3(1.0f, 0.0f, 0.0f));

		registeredWires.push_back(wire);

		return wire;
	}

	bool PluggablesSystem::UnregisterWire(Wire* wire)
	{
		BaseScene* scene = g_SceneManager->CurrentScene();
		Player* player = scene->GetPlayer(0);

		WirePlug* plug0 = (WirePlug*)wire->plug0ID.Get();
		WirePlug* plug1 = (WirePlug*)wire->plug1ID.Get();

		for (auto iter = registeredWires.begin(); iter != registeredWires.end(); ++iter)
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
				}
				if (plug1 != nullptr)
				{
					if (plug1->socketID.IsValid())
					{
						UnplugFromSocket(plug1);
					}
				}

				registeredWires.erase(iter);

				return true;
			}
		}
		return false;
	}

	WirePlug* PluggablesSystem::RegisterWirePlug(WirePlug* wirePlug)
	{
		registeredWirePlugs.push_back(wirePlug);
		return wirePlug;
	}

	bool PluggablesSystem::UnregisterWirePlug(WirePlug* wirePlug)
	{
		for (auto iter = registeredWirePlugs.begin(); iter != registeredWirePlugs.end(); ++iter)
		{
			if ((*iter)->ID == wirePlug->ID)
			{
				// TODO: Check for wires/sockets interacting with plug
				registeredWirePlugs.erase(iter);
				return true;
			}
		}
		return false;
	}

	Socket* PluggablesSystem::RegisterSocket(Socket* socket, i32 slotIdx /* = 0 */)
	{
		socket->slotIdx = slotIdx;
		registeredSockets.push_back(socket);
		return socket;
	}

	bool PluggablesSystem::UnregisterSocket(Socket* socket)
	{
		for (auto iter = registeredSockets.begin(); iter != registeredSockets.end(); ++iter)
		{
			if ((*iter)->ID == socket->ID)
			{
				registeredSockets.erase(iter);
				return true;
			}
		}
		return false;
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
		for (Socket* socket : registeredSockets)
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

	void PluggablesSystem::CacheGameObjectSockets(const GameObjectID& gameObjectID)
	{
		GameObject* gameObject = gameObjectID.Get();
		if (gameObject == nullptr)
		{
			// TODO: Print errors here (once editor objects can be ignored)
			return;
		}

		std::vector<Socket*> sockets;
		gameObject->GetChildrenOfType(SocketSID, false, sockets);
		if (!sockets.empty())
		{
			std::vector<SocketData> socketData;
			socketData.reserve(sockets.size());
			for (Socket* socket : sockets)
			{
				socketData.emplace_back(socket->ID);
			}
			gameObjectSockets[gameObjectID] = socketData;
		}
	}

	void PluggablesSystem::RemoveGameObjectSockets(const GameObjectID& gameObjectID)
	{
		auto iter = gameObjectSockets.find(gameObjectID);
		if (iter != gameObjectSockets.end())
		{
			gameObjectSockets.erase(iter);
		}
	}

	void PluggablesSystem::OnSocketChildAdded(const GameObjectID& parentObjectID, GameObject* childObject)
	{
		std::vector<SocketData>* sockets;

		auto iter = gameObjectSockets.find(parentObjectID);
		if (iter != gameObjectSockets.end())
		{
			sockets = &iter->second;
		}
		else
		{
			gameObjectSockets[parentObjectID] = {};
			sockets = &gameObjectSockets[parentObjectID];
		}

		Socket* socket = (Socket*)childObject;
		socket->slotIdx = (i32)sockets->size();
		(*sockets).emplace_back(socket->ID);
	}

	std::vector<SocketData> const* PluggablesSystem::GetGameObjectSockets(const GameObjectID& gameObjectID)
	{
		auto iter = gameObjectSockets.find(gameObjectID);
		if (iter != gameObjectSockets.end())
		{
			return &iter->second;
		}
		return nullptr;
	}

	void PluggablesSystem::SetGameObjectOutputSignal(const GameObjectID& gameObjectID, i32 slotIdx, i32 value)
	{
		auto iter = gameObjectSockets.find(gameObjectID);
		if (iter != gameObjectSockets.end())
		{
			std::vector<SocketData>* sockets = &iter->second;
			if (slotIdx < (i32)sockets->size())
			{
				(*sockets)[slotIdx].outputSignal = value;
			}
		}
	}

	i32 PluggablesSystem::GetGameObjectOutputSignal(const GameObjectID& gameObjectID, i32 slotIdx)
	{
		std::vector<SocketData> const* sockets = GetGameObjectSockets(gameObjectID);
		if (sockets != nullptr)
		{
			if (slotIdx < (i32)sockets->size())
			{
				return (*sockets)[slotIdx].outputSignal;
			}
		}
		return -1;
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

	void RoadManager::OnPreSceneChange()
	{
		m_RoadIDs.clear();
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

	TerminalManager::TerminalManager(bool bInstallDirectoryWatch)
	{
		if (bInstallDirectoryWatch)
		{
			m_ScriptDirectoryWatch = new DirectoryWatcher(SCRIPTS_DIRECTORY, true);
		}
	}

	TerminalManager::~TerminalManager()
	{
		delete m_ScriptDirectoryWatch;
		m_ScriptDirectoryWatch = nullptr;
	}

	void TerminalManager::Initialize()
	{
		PROFILE_AUTO("TerminalManager CreateContext");
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

		if (m_ScriptDirectoryWatch != nullptr && m_ScriptDirectoryWatch->Update())
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

	void TerminalManager::SetInstallDirectoryWatch(bool bInstallDirectoryWatch)
	{
		if (bInstallDirectoryWatch)
		{
			if (m_ScriptDirectoryWatch == nullptr)
			{
				m_ScriptDirectoryWatch = new DirectoryWatcher(SCRIPTS_DIRECTORY, true);
			}
		}
		else
		{
			if (m_ScriptDirectoryWatch != nullptr)
			{
				delete m_ScriptDirectoryWatch;
				m_ScriptDirectoryWatch = nullptr;
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

	//
	// PropertyCollectionManager
	//

	void PropertyCollectionManager::Initialize()
	{
		PROFILE_AUTO("PropertyCollectionManager Initialize");
		GameObject::RegisterPropertyCollections();
	}

	void PropertyCollectionManager::Destroy()
	{
	}

	void PropertyCollectionManager::Update()
	{
	}

	void PropertyCollectionManager::DrawImGui()
	{
	}

	void PropertyCollectionManager::RegisterType(StringID gameObjectTypeID, PropertyCollection* collection)
	{
		if (m_RegisteredObjectTypes.find(gameObjectTypeID) != m_RegisteredObjectTypes.end())
		{
			// Game Object Type registered multiple times
			ENSURE_NO_ENTRY();
			return;
		}

		m_RegisteredObjectTypes.emplace(gameObjectTypeID, collection);
	}

	bool PropertyCollectionManager::SerializeGameObject(const GameObjectID& gameObjectID, PropertyCollection* collection, const char* debugObjectName, JSONObject& parentObject, bool bSerializePrefabData)
	{
		GameObject* gameObject = gameObjectID.Get();
		if (gameObject == nullptr)
		{
			char buffer[33];
			gameObjectID.ToString(buffer);
			PrintError("Failed to get object to be serialized (ID: %s, name: %s)\n", buffer, debugObjectName);
			return false;
		}

		if (collection != nullptr)
		{
			const PrefabIDPair& sourcePrefabIDPair = gameObject->GetSourcePrefabIDPair();
			GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(sourcePrefabIDPair);

			if (prefabTemplate == nullptr || bSerializePrefabData)
			{
				// No need to worry about inherited fields if this object isn't a prefab instance
				SerializeCollection(collection, gameObject, parentObject);
			}
			else
			{
				SerializeCollectionUniqueFieldsOnly(collection, gameObject, prefabTemplate, parentObject);
			}

			return true;
		}

		return false;
	}

	void PropertyCollectionManager::DeserializeGameObject(GameObject* gameObject, PropertyCollection* collection, const JSONObject& parentObject)
	{
		CHECK_NE(gameObject, nullptr);

		if (collection != nullptr)
		{
			Deserialize(collection, gameObject, parentObject);
		}
	}

	bool PropertyCollectionManager::SerializePrefabTemplate(const PrefabIDPair& prefabIDPair, PropertyCollection* collection, const char* debugObjectName, JSONObject& parentObject)
	{
		GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(prefabIDPair);
		if (prefabTemplate == nullptr)
		{
			char bufferPrefabID[33];
			char bufferSubObjectID[33];
			prefabIDPair.m_PrefabID.ToString(bufferPrefabID);
			prefabIDPair.m_SubGameObjectID.ToString(bufferSubObjectID);
			PrintError("Attempted to serialize collection for invalid prefab template (Prefab ID: %s, Sub-object ID: %s, name: %s)\n", bufferPrefabID, bufferSubObjectID, debugObjectName);
			return false;
		}

		if (collection != nullptr)
		{
			// Nested prefabs not supported, so all fields can be serialized
			// without checking for parent values.
			SerializeCollection(collection, prefabTemplate, parentObject);
			return true;
		}
		return false;
	}

	void PropertyCollectionManager::DeserializePrefabTemplate(GameObject* prefabTemplate, PropertyCollection* collection, const JSONObject& parentObject)
	{
		CHECK_NE(prefabTemplate, nullptr);

		if (collection != nullptr)
		{
			Deserialize(collection, prefabTemplate, parentObject);
		}
	}

	PropertyCollection* PropertyCollectionManager::AllocateCollection(const char* collectionName)
	{
		if (m_Allocator.GetPoolCount() > 2048)
		{
			char memSizeStrBuf[64];
			ByteCountToString(memSizeStrBuf, ARRAY_LENGTH(memSizeStrBuf), m_Allocator.MemoryUsed());
			PrintWarn("Property collection manager has %u allocation pools allocated, taking up %s!\n", m_Allocator.GetPoolCount(), memSizeStrBuf);
		}

		PropertyCollection* result = new (m_Allocator.Alloc()) PropertyCollection(collectionName);
		return result;
	}

	PropertyCollection* PropertyCollectionManager::GetCollectionForObjectType(StringID gameObjectTypeID) const
	{
		auto iter = m_RegisteredObjectTypes.find(gameObjectTypeID);
		if (iter != m_RegisteredObjectTypes.end())
		{
			return iter->second;
		}
		return nullptr;
	}

	void PropertyCollectionManager::SerializeCollection(PropertyCollection* collection, GameObject* gameObject, JSONObject& parentObject)
	{
		for (const auto& iter : collection->values)
		{
			const char* label = iter.first;
			const PropertyValue& value = iter.second;

			CHECK_LT(value.offset, 65536u);

			void* valuePtr = (u8*)gameObject + value.offset;

			if (!IsDefaultValue(value, valuePtr))
			{
				parentObject.fields.emplace_back(label, JSONValue::FromRawPtr(valuePtr, value.type, value.GetPrecision()));
			}
		}
	}

	void PropertyCollectionManager::SerializeCollectionUniqueFieldsOnly(PropertyCollection* collection, GameObject* gameObject, GameObject* prefabTemplate, JSONObject& parentObject)
	{
		for (const auto& iter : collection->values)
		{
			const char* label = iter.first;
			const PropertyValue& value = iter.second;

			CHECK_LT(value.offset, 65536u);
			// Serialization may fail if class layouts don't match
			CHECK(typeid(*gameObject) == typeid(*prefabTemplate));

			void* fieldValuePtr = (u8*)gameObject + value.offset;
			void* templateValuePtr = (u8*)prefabTemplate + value.offset;

			SerializePrefabInstanceFieldIfUnique(parentObject, label, fieldValuePtr, templateValuePtr, value.type, value.GetPrecision());
		}
	}

	void PropertyCollectionManager::Deserialize(PropertyCollection* collection, GameObject* gameObject, const JSONObject& parentObject)
	{
		for (const auto& iter : collection->values)
		{
			const char* label = iter.first;
			const PropertyValue& value = iter.second;

			CHECK_LT(value.offset, 65536u);

			void* valuePtr = (u8*)gameObject + value.offset;
			parentObject.TryGetValueOfType(label, valuePtr, value.type);
		}
	}

	bool ValuesAreEqual(ValueType valueType, void* value0, void* value1)
	{
		const real threshold = 0.0001f;

		switch (valueType)
		{
		case ValueType::STRING:
			return *(std::string*)value0 == *(std::string*)value1;
		case ValueType::INT:
			return *(i32*)value0 == *(i32*)value1;
		case ValueType::UINT:
			return *(u32*)value0 == *(u32*)value1;
		case ValueType::LONG:
			return *(i64*)value0 == *(i64*)value1;
		case ValueType::ULONG:
			return *(u64*)value0 == *(u64*)value1;
		case ValueType::FLOAT:
			return NearlyEquals(*(real*)value0, *(real*)value1, threshold);
		case ValueType::BOOL:
			return *(bool*)value0 == *(bool*)value1;
		case ValueType::VEC2:
			return NearlyEquals(*(glm::vec2*)value0, *(glm::vec2*)value1, threshold);
		case ValueType::VEC3:
			return NearlyEquals(*(glm::vec3*)value0, *(glm::vec3*)value1, threshold);
		case ValueType::VEC4:
			return NearlyEquals(*(glm::vec4*)value0, *(glm::vec4*)value1, threshold);
		case ValueType::QUAT:
			return NearlyEquals(*(glm::quat*)value0, *(glm::quat*)value1, threshold);
		case ValueType::GUID:
			return *(GUID*)value0 == *(GUID*)value1;
		default:
			ENSURE_NO_ENTRY();
			return false;
		}
	}

	void SerializePrefabInstanceFieldIfUnique(JSONObject& parentObject, const char* label, void* valuePtr, void* templateValuePtr, ValueType valueType, u32 precision /* = 2 */)
	{
		if (!ValuesAreEqual(valueType, valuePtr, templateValuePtr))
		{
			parentObject.fields.emplace_back(label, JSONValue::FromRawPtr(valuePtr, valueType, precision));
		}
	}

	bool IsDefaultValue(const PropertyValue& value, void* valuePtr)
	{
		if (value.defaultValueSet != 0)
		{
			switch (value.type)
			{
			case ValueType::INT:
				return ValuesAreEqual(value.type, valuePtr, (void*)&value.defaultValue.intValue);
			case ValueType::UINT:
				return ValuesAreEqual(value.type, valuePtr, (void*)&value.defaultValue.uintValue);
			case ValueType::FLOAT:
				return ValuesAreEqual(value.type, valuePtr, (void*)&value.defaultValue.realValue);
			case ValueType::BOOL:
				return ValuesAreEqual(value.type, valuePtr, (void*)&value.defaultValue.boolValue);
			case ValueType::GUID:
				return ValuesAreEqual(value.type, valuePtr, (void*)&value.defaultValue.guidValue);
			case ValueType::VEC2:
				return ValuesAreEqual(value.type, valuePtr, (void*)&value.defaultValue.vec2Value);
			case ValueType::VEC3:
				return ValuesAreEqual(value.type, valuePtr, (void*)&value.defaultValue.vec3Value);
			case ValueType::VEC4:
				return ValuesAreEqual(value.type, valuePtr, (void*)&value.defaultValue.vec4Value);
			case ValueType::QUAT:
				return ValuesAreEqual(value.type, valuePtr, (void*)&value.defaultValue.quatValue);
			default:
				ENSURE_NO_ENTRY();
			}
		}

		return false;
	}

} // namespace flex
