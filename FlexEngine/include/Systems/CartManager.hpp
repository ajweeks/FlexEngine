#pragma once

#include "Callbacks/GameObjectCallbacks.hpp"
#include "Helpers.hpp" // For GameObjectID
#include "Systems/System.hpp"

namespace flex
{
	class Cart;
	class EngineCart;
	class BaseScene;

	struct CartChain
	{
		explicit CartChain(CartChainID chainID);

		void AddUnique(CartID cartID);
		void Remove(CartID cartID);
		bool Contains(CartID cartID) const;
		i32 GetCartIndex(CartID cartID) const;
		real GetCartAtIndexDistAlongTrack(i32 cartIndex);
		void Reset();

		bool operator!=(const CartChain& other);
		bool operator==(const CartChain& other);

		// Carts in order (0=rear, n=front)
		std::vector<CartID> carts;
		CartChainID chainID = InvalidCartChainID;

		real velT = 0.0f;

	private:
		void Sort();

	};

	class CartManager : public System
	{
	public:
		CartManager();
		virtual ~CartManager() = default;

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void Update() override;

		virtual void DrawImGui() override;

		// Creates a new cart with given name and adds to the current scene
		Cart* CreateCart(const std::string& name, GameObjectID gameObjectID = InvalidGameObjectID, bool bPrefabTemplate = false);
		EngineCart* CreateEngineCart(const std::string& name, GameObjectID gameObjectID = InvalidGameObjectID, bool bPrefabTemplate = false);
		Cart* GetCart(CartID cartID) const;
		CartChain* GetCartChain(CartChainID cartChainID);
		real GetChainDrivePower(CartChainID cartChainID);


	private:
		void OnGameObjectDestroyed(GameObject* gameObject);
		OnGameObjectDestroyedCallback<CartManager> m_OnGameObjectDestroyedCallback;

		// Allocates room for and return the id of the next cart chain ID
		CartChainID GetNextAvailableCartChainID();

		std::vector<Cart*> m_Carts;
		std::vector<CartChain> m_CartChains;

	};
} // namespace flex
