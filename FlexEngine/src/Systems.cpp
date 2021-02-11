#include "stdafx.hpp"

#include "Systems.hpp"

#include "Graphics/Renderer.hpp" // For PhysicsDebugDrawBase
#include "Helpers.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
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
				scene->GameObjectIDField(buf, m_RoadIDs[i]);
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
} // namespace flex
