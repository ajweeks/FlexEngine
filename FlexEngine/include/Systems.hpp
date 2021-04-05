#pragma once

namespace flex
{
	class Wire;
	class Socket;
	class Road;
	class Terminal;
	class DirectoryWatcher;

	enum class SystemType
	{
		PLUGGABLES,
		ROAD_MANAGER,
		TERMINAL_MANAGER,

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

	class TerminalManager : public System
	{
	public:
		TerminalManager();
		virtual ~TerminalManager();

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void Update() override;

		virtual void DrawImGui() override;

		static void OpenSavePopup(const GameObjectID& terminalID);

		void RegisterTerminal(Terminal* terminal);
		void DeregisterTerminal(Terminal* terminal);

		bool LoadScript(const std::string& fileName, std::vector<std::string>& outFileLines);
		bool SaveScript(const std::string& fileName, const std::vector<std::string>& fileLines);

		static const char* SavePopupName;

	private:
		u64 CalculteChecksum(const std::string& filePath);
		void UpdateScriptHashes(std::vector<std::string>& outModifiedFiles);

		static GameObjectID m_TerminalSavingID;
		static bool m_bOpenSavePopup;

		// When non-zero, counts down until we start responding to changed events for m_TerminalSavingID
		// This prevents us from responding to "change" events which we caused by saving to disk
		u32 m_ScriptSaveTimer = 0;
		std::string m_LastSavedScriptFileName;

		std::vector<Terminal*> m_Terminals;
		std::map<std::string, u64> m_ScriptHashes; // Maps file path to file checksum to know when files change
		DirectoryWatcher* m_ScriptDirectoryWatch = nullptr;

	};
} // namespace flex
