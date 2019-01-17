#include "stdafx.hpp"

#include "Managers/CartManager.hpp"

#pragma warning(push, 0)
#pragma warning(pop)

#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	CartID CartManager::CreateCart(const std::string& name)
	{
		// TODO: Use custom memory allocator
		Cart* newCart = new Cart(name);
		m_Carts.push_back(newCart);
		g_SceneManager->CurrentScene()->AddRootObject(newCart);
		return (CartID)m_Carts.size() - 1;
	}

	flex::Cart* CartManager::GetCart(CartID cartID)
	{
		assert(cartID < m_Carts.size());
		return m_Carts[cartID];
	}

} // namespace flex
