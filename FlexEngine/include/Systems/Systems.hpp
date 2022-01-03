#pragma once

#include "Systems/System.hpp"

#include "PoolAllocator.hpp"

namespace flex
{
	class Wire;
	class WirePlug;
	class Socket;
	class Road;
	class Terminal;
	class DirectoryWatcher;
	struct PropertyCollection;
	struct JSONObject;

	class PluggablesSystem final : public System
	{
	public:
		virtual void Initialize() override;
		virtual void Destroy() override;

		virtual void Update() override;

		i32 GetReceivedSignal(Socket* socket);

		Wire* RegisterWire(Wire* wire);
		bool UnregisterWire(Wire* wire);

		WirePlug* RegisterWirePlug(WirePlug* wirePlug);
		bool UnregisterWirePlug(WirePlug* wirePlug);

		Socket* RegisterSocket(Socket* socket, i32 slotIdx = 0);
		bool UnregisterSocket(Socket* socket);

		Socket* GetSocketAtOtherEnd(Socket* socket);

		Socket* GetNearbySocket(const glm::vec3& posWS, real threshold, bool bExcludeFilled, Socket* excludeSocket);
		Socket* GetNearbySocket(const glm::vec3& posWS, real threshold, bool bExcludeFilled);

		bool PlugInToNearbySocket(WirePlug* plug, real nearbyThreshold);
		void PlugInToSocket(WirePlug* plug, Socket* socket);
		void UnplugFromSocket(WirePlug* plug);

		std::vector<Wire*> wires;
		std::vector<WirePlug*> wirePlugs;
		std::vector<Socket*> sockets;

		real maxDistBeforeSnapSq = 25.0f * 25.0f;

	private:
		AudioSourceID m_PlugInAudioSourceID = InvalidAudioSourceID;
		AudioSourceID m_UnplugAudioSourceID = InvalidAudioSourceID;

		// TODO: Serialization (requires ObjectIDs)
		// TODO: Use WirePool

	};

	class RoadManager final : public System
	{
	public:
		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void Update() override;

		virtual void DrawImGui() override;

		void RegisterRoad(Road* road);
		void DeregisterRoad(Road* road);

		void RegenerateAllRoads();

	private:
		// TODO: Four proxy objects which the user can manipulate when a road segment is selected

		std::vector<GameObjectID> m_RoadIDs;

	};

	class TerminalManager final : public System
	{
	public:
		TerminalManager();
		~TerminalManager();

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

	class PropertyCollectionManager
	{
	public:
		PropertyCollection* GetCollectionForObject(const GameObjectID& gameObjectID);
		PropertyCollection* GetCollectionForPrefab(const PrefabID& prefabID);

		PropertyCollection* RegisterObject(const GameObjectID& gameObjectID);
		bool DeregisterObject(const GameObjectID& gameObjectID);
		bool DeregisterObjectRecursive(const GameObjectID& gameObjectID);

		PropertyCollection* RegisterPrefabTemplate(const PrefabID& prefabID);
		bool DeregisterPrefabTemplate(const PrefabID& prefabID);
		bool DeregisterPrefabTemplateRecursive(const PrefabID& prefabID);

		bool SerializeObjectIfPresent(const GameObjectID& gameObjectID, JSONObject& parentObject, bool bSerializePrefabData);
		void DeserializeObjectIfPresent(const GameObjectID& gameObjectID, const JSONObject& parentObject);

		bool SerializePrefabTemplate(const PrefabID& prefabID, JSONObject& parentObject);
		void DeserializePrefabTemplate(const PrefabID& prefabID, const JSONObject& parentObject);

	private:
		std::map<GameObjectID, PropertyCollection*> m_RegisteredObjects;
		std::map<PrefabID, PropertyCollection*> m_RegisteredPrefabTemplates;

		PoolAllocator<PropertyCollection, 32> m_Allocator;

	};
} // namespace flex
