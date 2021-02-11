#pragma once

namespace flex
{
	class Wire;
	class Socket;
	class Road;

	enum class SystemType
	{
		PLUGGABLES,
		ROAD_MANAGER,

		_NONE
	};

	class System
	{
	public:
		virtual void Initialize() = 0;
		virtual void Destroy() = 0;
		virtual void Update() = 0;

		virtual void DrawImGui();

	};

	class PluggablesSystem : public System
	{
	public:
		virtual ~PluggablesSystem() {};

		virtual void Initialize() override;
		virtual void Destroy() override;

		virtual void Update() override;

		i32 GetReceivedSignal(Socket* socket);

		Wire* AddWire(const GameObjectID& gameObjectID, Socket* socket0 = nullptr, Socket* socket1 = nullptr);
		bool DestroySocket(Socket* socket);
		bool DestroyWire(Wire* wire);

		Socket* AddSocket(const std::string& name, const GameObjectID& gameObjectID, i32 slotIdx = 0, Wire* connectedWire = nullptr);
		Socket* AddSocket(Socket* socket, i32 slotIdx = 0, Wire* connectedWire = nullptr);

		std::vector<Wire*> wires;
		std::vector<Socket*> sockets;

	private:
		bool RemoveSocket(const GameObjectID& socketID);

		// TODO: Serialization (requires ObjectIDs)
		// TODO: Use WirePool

	};


	class RoadManager : public System
	{
	public:
		RoadManager();
		virtual ~RoadManager();

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void Update() override;

		virtual void DrawImGui() override;

		void RegisterRoad(Road* road);
		void DeregisterRoad(Road* road);

	private:
		// TODO: Four proxy objects which the user can manipulate when a road segment is selected

		std::vector<GameObjectID> m_RoadIDs;

	};
} // namespace flex
