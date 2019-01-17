#pragma once

namespace flex
{
	class Cart;

	class CartManager
	{
	public:
		// Creates a new cart with given name and adds to the current scene
		CartID CreateCart(const std::string& name);

		Cart* GetCart(CartID cartID);

	private:
		std::vector<Cart*> m_Carts;

	};
} // namespace flex
