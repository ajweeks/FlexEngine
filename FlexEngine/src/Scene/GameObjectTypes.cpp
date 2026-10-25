#include "stdafx.hpp"

#include "Scene/GameObjectTypes.hpp"

#include "Player.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Objects/Carts.hpp"
#include "Scene/Objects/GerstnerWave.hpp"
#include "Scene/Objects/Industry.hpp"
#include "Scene/Objects/Lights.hpp"
#include "Scene/Objects/Props.hpp"
#include "Scene/Objects/Road.hpp"
#include "Scene/Objects/SoftBody.hpp"
#include "Scene/Objects/Terminal.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/Objects/Vehicle.hpp"
#include "Scene/Objects/Wires.hpp"

namespace flex
{
	namespace
	{
		template<typename T>
		GameObject* Construct(const std::string& name, const GameObjectID& gameObjectID, const PrefabIDPair& sourcePrefabID, bool bIsPrefabTemplate)
		{
			return new T(name, gameObjectID, sourcePrefabID, bIsPrefabTemplate);
		}

		// Mesh & material are generated at runtime rather than loaded from the scene file
		static const u32 PROCEDURAL = GameObjectTypeInfo::NO_SERIALIZED_MESH | GameObjectTypeInfo::NO_SERIALIZED_MATERIAL;

#define TYPE(Type, flags) GameObjectTypeInfo{ Type##SID, &Construct<Type>, &Type::BuildTypeUniquePropertyCollection, (u32)(flags) }

		// Every serializable game object type. Add new types here.
		static const GameObjectTypeInfo s_Types[] =
		{
			TYPE(DirectionalLight, 0),
			TYPE(PointLight, 0),
			TYPE(SpotLight, 0),
			TYPE(AreaLight, 0),
			TYPE(Valve, 0),
			TYPE(RisingBlock, 0),
			TYPE(GlassPane, 0),
			TYPE(ReflectionProbe, 0),
			TYPE(Skybox, GameObjectTypeInfo::NO_SERIALIZED_MESH),
			TYPE(EmptyCart, PROCEDURAL),
			TYPE(EngineCart, PROCEDURAL),
			TYPE(MobileLiquidBox, 0),
			TYPE(Battery, 0),
			TYPE(GerstnerWave, PROCEDURAL),
			TYPE(Wire, PROCEDURAL),
			TYPE(WirePlug, 0),
			TYPE(Socket, 0),
			TYPE(Terminal, PROCEDURAL),
			TYPE(TerrainGenerator, PROCEDURAL),
			TYPE(SpringObject, PROCEDURAL),
			TYPE(SoftBody, 0),
			TYPE(Vehicle, 0),
			TYPE(Road, PROCEDURAL),
			TYPE(SolarPanel, 0),
			TYPE(HeadLamp, 0),
			TYPE(PowerPole, 0),
			TYPE(MineralDeposit, GameObjectTypeInfo::NO_SERIALIZED_MESH),
			TYPE(Miner, 0),
			TYPE(Speaker, 0),
			TYPE(PickAxe, 0),
		};

#undef TYPE
	} // anonymous namespace

	const GameObjectTypeInfo* FindGameObjectType(StringID typeID)
	{
		for (const GameObjectTypeInfo& info : s_Types)
		{
			if (info.typeID == typeID)
			{
				return &info;
			}
		}
		return nullptr;
	}

	const GameObjectTypeInfo* GetGameObjectTypes(u32& outCount)
	{
		outCount = (u32)ARRAY_LENGTH(s_Types);
		return s_Types;
	}
} // namespace flex
