#pragma once

namespace flex
{
	class Cart;
	class BaseScene;

	struct CartChain
	{
		CartChain(CartChainID chainID);

		void AddUnique(CartID cartID);
		void Remove(CartID cartID);
		bool Contains(CartID cartID) const;
		i32 GetCartIndex(CartID cartID) const;
		real GetCartAtIndexDistAlongTrack(i32 cartIndex);
		void Reset();

		bool operator!=(const CartChain& other);
		bool operator==(const CartChain& other);

		std::vector<CartID> carts;
		CartChainID chainID = InvalidCartChainID;
	};

	class CartManager
	{
	public:
		CartManager(BaseScene* owningScene);

		// Creates a new cart with given name and adds to the current scene
		CartID CreateCart(const std::string& name);
		CartID CreateEngineCart(const std::string& name);
		Cart* GetCart(CartID cartID);
		CartChain* GetCartChain(CartChainID cartChainID);
		real GetChainDrivePower(CartChainID cartChainID);
		void OnGameObjectDestroyed(GameObject* gameObject);

		void Update();

		void DrawImGuiObjects();

		void Destroy();
	private:
		// Allocates room for and return the id of the next cart chain ID
		CartChainID GetNextAvailableCartChainID();

		std::vector<Cart*> m_Carts;
		std::vector<CartChain> m_CartChains;

		BaseScene* m_OwningScene = nullptr;

	};
} // namespace flex
