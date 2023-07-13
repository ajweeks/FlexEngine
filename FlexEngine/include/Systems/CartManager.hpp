#pragma once

#include "Callbacks/GameObjectCallbacks.hpp"
#include "Helpers.hpp" // For GameObjectID
#include "Systems/System.hpp"

namespace flex
{
	class BaseCart;
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

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void Update() override;

		virtual void DrawImGui() override;

		CartID RegisterCart(BaseCart* cart);
		void DeregisterCart(BaseCart* cart);
		BaseCart* GetCart(CartID cartID) const;
		CartChain* GetCartChain(CartChainID cartChainID);
		real GetChainDrivePower(CartChainID cartChainID);


	private:
		void OnGameObjectDestroyed(GameObject* gameObject);
		OnGameObjectDestroyedCallback<CartManager> m_OnGameObjectDestroyedCallback;

		// Allocates room for and return the id of the next cart chain ID
		CartChainID GetNextAvailableCartChainID();

		// TODO: Store GameObjectIDs
		std::vector<BaseCart*> m_Carts;
		std::vector<CartChain> m_CartChains;

	};
} // namespace flex
