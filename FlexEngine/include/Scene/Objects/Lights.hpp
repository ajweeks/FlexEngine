#pragma once

#include "Scene/GameObject.hpp"

// Light objects (directional, point, spot & area)
namespace flex
{
	class DirectionalLight final : public GameObject
	{
	public:
		DirectionalLight();
		explicit DirectionalLight(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;
		virtual void SetVisible(bool bVisible, bool bEffectChildren /* = true */) override;

		bool operator==(const DirectionalLight& other);

		DirLightData data;

		// Editor-only
		glm::vec3 pos = VEC3_ZERO;

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;
	};

	class PointLight final : public GameObject
	{
	public:
		explicit PointLight(BaseScene* scene);
		explicit PointLight(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;
		virtual void SetVisible(bool bVisible, bool bEffectChildren /* = true */) override;

		bool operator==(const PointLight& other);

		PointLightData data;
		PointLightID pointLightID = InvalidPointLightID;

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;
	};

	class SpotLight final : public GameObject
	{
	public:
		explicit SpotLight(BaseScene* scene);
		explicit SpotLight(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;
		virtual void SetVisible(bool bVisible, bool bEffectChildren /* = true */) override;

		bool operator==(const SpotLight& other);

		SpotLightData data;
		SpotLightID spotLightID = InvalidSpotLightID;

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;
	};

	class AreaLight final : public GameObject
	{
	public:
		explicit AreaLight(BaseScene* scene);
		explicit AreaLight(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;
		virtual void SetVisible(bool bVisible, bool bEffectChildren /* = true */) override;

		bool operator==(const AreaLight& other);

		AreaLightData data;
		AreaLightID areaLightID = InvalidAreaLightID;

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		void UpdatePoints();

	};
} // namespace flex
