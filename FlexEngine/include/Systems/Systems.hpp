#pragma once

#include "Systems/System.hpp"

#include "PoolAllocator.hpp"

namespace flex
{
	class DirectoryWatcher;
	struct JSONObject;
	class Road;
	class Socket;
	class Terminal;
	class Wire;
	class WirePlug;
	enum class ValueType;
	struct PropertyCollection;
	struct PropertyValue;

	struct SocketData
	{
		SocketData(const GameObjectID& socketID) :
			socketID(socketID),
			outputSignal(0)
		{
		}

		GameObjectID socketID;
		i32 outputSignal;
	};

	class PluggablesSystem final : public System
	{
	public:
		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void OnPreSceneChange() override;

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

		void CacheGameObjectSockets(const GameObjectID& gameObjectID);
		void RemoveGameObjectSockets(const GameObjectID& gameObjectID);
		void OnSocketChildAdded(const GameObjectID& parentObjectID, const GameObjectID& childObjectID);
		std::vector<SocketData> const* GetGameObjectSockets(const GameObjectID& gameObjectID);

		void SetGameObjectOutputSignal(const GameObjectID& gameObjectID, i32 slotIdx, i32 value);
		i32 GetGameObjectOutputSignal(const GameObjectID& gameObjectID, i32 slotIdx);

		std::vector<Wire*> registeredWires;
		std::vector<WirePlug*> registeredWirePlugs;
		std::vector<Socket*> registeredSockets;
		// Stores each registered object's child sockets
		std::map<GameObjectID, std::vector<SocketData>> gameObjectSockets;

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
		virtual void OnPreSceneChange() override;

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
		TerminalManager(bool bInstallDirectoryWatch);
		~TerminalManager();

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void Update() override;

		virtual void DrawImGui() override;

		static void OpenSavePopup(const GameObjectID& terminalID);

		void RegisterTerminal(Terminal* terminal);
		void DeregisterTerminal(Terminal* terminal);

		void SetInstallDirectoryWatch(bool bInstallDirectoryWatch);

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

	class PropertyCollectionManager : public System
	{
	public:
		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void Update() override;

		virtual void DrawImGui() override;

		void RegisterType(StringID gameObjectTypeID, PropertyCollection* collection);

		// Serializes fields which differ from the owning prefab's, if object is loaded from a prefab
		bool SerializeGameObject(const GameObjectID& gameObjectID, PropertyCollection* collection, const char* debugObjectName, JSONObject& parentObject, bool bSerializePrefabData);
		void DeserializeGameObject(GameObject* gameObject, PropertyCollection* collection, const JSONObject& parentObject);

		bool SerializePrefabTemplate(const PrefabIDPair& prefabIDPair, PropertyCollection* collection, const char* debugObjectName, JSONObject& parentObject);
		void DeserializePrefabTemplate(GameObject* prefabTemplate, PropertyCollection* collection, const JSONObject& parentObject);

		PropertyCollection* AllocateCollection(const char* collectionName);

		// Serializes all the fields for the given object (does not check for fields a prefab may already define)
		static void SerializeCollection(PropertyCollection* collection, GameObject* gameObject, JSONObject& parentObject);
		static void SerializeCollectionUniqueFieldsOnly(PropertyCollection* collection, GameObject* gameObject, GameObject* prefabTemplate, JSONObject& parentObject);

		PropertyCollection* GetCollectionForObjectType(StringID gameObjectTypeID) const;

	private:
		void Deserialize(PropertyCollection* collection, GameObject* gameObject, const JSONObject& parentObject);

		bool SerializeGameObjectFields(PropertyCollection* collection, JSONObject& parentObject, const GameObjectID& gameObjectID, bool bSerializePrefabData);

		// GameObjectTypeID -> Collection for that class built at startup
		std::map<StringID, PropertyCollection*> m_RegisteredObjectTypes;

		PoolAllocator<PropertyCollection, 64> m_Allocator;
	};

	extern bool ValuesAreEqual(ValueType valueType, void* value0, void* value1);

	extern void SerializeGameObjectField(JSONObject& parentObject, const char* label, void* valuePtr, ValueType valueType, GameObjectID gameObjectID, u32 precision = 2);
	extern void SerializePrefabInstanceFieldIfUnique(JSONObject& parentObject, const char* label, void* valuePtr, void* templateValuePtr, ValueType valueType, u32 precision = 2);

	extern bool IsDefaultValue(const PropertyValue& value, void* valuePtr);

} // namespace flex
