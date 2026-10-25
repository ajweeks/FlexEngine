#pragma once

#include "Helpers.hpp" // For Hash (SID)

namespace flex
{
	// Type IDs of every game object type (serialized as the type name)
	static constexpr StringID BaseObjectSID = SID("object");
	static constexpr StringID EditorObjectSID = SID("editor object");
	static constexpr StringID DroppedItemSID = SID("dropped item");
	static constexpr StringID ValveSID = SID("valve");
	static constexpr StringID RisingBlockSID = SID("rising block");
	static constexpr StringID GlassPaneSID = SID("glass pane");
	static constexpr StringID ReflectionProbeSID = SID("reflection probe");
	static constexpr StringID SkyboxSID = SID("skybox");
	static constexpr StringID DirectionalLightSID = SID("directional light");
	static constexpr StringID PointLightSID = SID("point light");
	static constexpr StringID SpotLightSID = SID("spot light");
	static constexpr StringID AreaLightSID = SID("area light");
	static constexpr StringID BaseCartSID = SID("base cart");
	static constexpr StringID EmptyCartSID = SID("empty cart");
	static constexpr StringID EngineCartSID = SID("engine cart");
	static constexpr StringID MobileLiquidBoxSID = SID("mobile liquid box");
	static constexpr StringID BatterySID = SID("battery");
	static constexpr StringID SolarPanelSID = SID("solar panel");
	static constexpr StringID HeadLampSID = SID("head lamp");
	static constexpr StringID PowerPoleSID = SID("power pole");
	static constexpr StringID MineralDepositSID = SID("mineral deposit");
	static constexpr StringID MinerSID = SID("miner");
	static constexpr StringID SpeakerSID = SID("speaker");
	static constexpr StringID PickAxeSID = SID("pickaxe");
	static constexpr StringID GerstnerWaveSID = SID("gerstner wave");
	static constexpr StringID WireSID = SID("wire");
	static constexpr StringID WirePlugSID = SID("wire plug");
	static constexpr StringID SocketSID = SID("socket");
	static constexpr StringID TerminalSID = SID("terminal");
	static constexpr StringID TerrainGeneratorSID = SID("terrain generator");
	static constexpr StringID SpringObjectSID = SID("spring");
	static constexpr StringID SoftBodySID = SID("soft body");
	static constexpr StringID VehicleSID = SID("vehicle");
	static constexpr StringID RoadSID = SID("road");
	static constexpr StringID PlayerSID = SID("player");
	static constexpr StringID ParticleSystemSID = SID("particle system");

	class GameObject;
	struct PropertyCollection;
	struct GameObjectID;
	struct PrefabIDPair;

	struct GameObjectTypeInfo
	{
		enum Flags : u32
		{
			NO_SERIALIZED_MESH = 1 << 0,
			NO_SERIALIZED_MATERIAL = 1 << 1,
		};

		using ConstructFunc = GameObject* (*)(const std::string& name, const GameObjectID& gameObjectID, const PrefabIDPair& sourcePrefabID, bool bIsPrefabTemplate);
		using BuildPropertiesFunc = PropertyCollection* (*)();

		StringID typeID;
		ConstructFunc construct;
		BuildPropertiesFunc buildPropertyCollection;
		u32 flags;
	};

	// Returns null for types without an entry (base objects, players & unknown types)
	const GameObjectTypeInfo* FindGameObjectType(StringID typeID);
	const GameObjectTypeInfo* GetGameObjectTypes(u32& outCount);
} // namespace flex
