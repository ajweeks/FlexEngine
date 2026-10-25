#pragma once

#include "Scene/GameObject.hpp"

// Wires, plugs & sockets which connect devices together
namespace flex
{
	// Connects together devices to transmit information & power
	class Wire final : public GameObject
	{
	public:
		Wire(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Update() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;
		virtual bool ShouldSerialize() override;

		GameObjectID GetOtherPlug(WirePlug* plug);

		void SetStartTangent(const glm::vec3& tangent);
		void ClearStartTangent();
		void SetEndTangent(const glm::vec3& tangent);
		void ClearEndTangent();

		void UpdateWireMesh();

		glm::vec3 CalculateTangentAtPoint(real t);
		void CalculateBasisAtPoint(real t, glm::vec3& outNormal, glm::vec3& outTangent, glm::vec3& outBitangent);

		static const real DEFAULT_LENGTH;

		GameObjectID plug0ID = InvalidGameObjectID;
		GameObjectID plug1ID = InvalidGameObjectID;

		i32 numPoints = 12;
		i32 numRadialPoints = 10;
		real stiffness = 0.95f;
		real pointInvMass = 1.0f / 5.0f;
		real damping = 0.9f;

		SoftBody* m_SoftBody = nullptr;

	private:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		void DestroyPoints();
		void GeneratePoints();
		void UpdateMesh();
		void UpdateIndices();

		VertexBufferDataCreateInfo m_VertexBufferCreateInfo;
		std::vector<u32> m_Indices;

	};

	// End of wire - the part you actually interact with
	class WirePlug final : public GameObject
	{
	public:
		WirePlug(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);
		WirePlug(const std::string& name, Wire* owningWire, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Destroy(bool bDetachFromParent /* = true */) override;

		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		void PlugIn(Socket* socket);
		void Unplug();

		static const real nearbyThreshold;

		GameObjectID wireID = InvalidGameObjectID;
		GameObjectID socketID = InvalidGameObjectID;

	};

	// Connect wires to objects
	class Socket final : public GameObject
	{
	public:
		Socket(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		void OnPlugIn(WirePlug* plug);
		void OnUnPlug();

		GameObjectID connectedPlugID = InvalidGameObjectID;
		i32 slotIdx = 0;
	};
} // namespace flex
