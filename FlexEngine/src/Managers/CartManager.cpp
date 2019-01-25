#include "stdafx.hpp"

#include "Managers/CartManager.hpp"

#pragma warning(push, 0)
#include <glm/gtx/norm.hpp> // For distance2

#include <LinearMath/btIDebugDraw.h>
#pragma warning(pop)

#include "FlexEngine.hpp"
#include "Graphics/Renderer.hpp"
#include "Profiler.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"
#include "Track/TrackManager.hpp"

namespace flex
{
	CartChain::CartChain(CartChainID chainID) :
		chainID(chainID)
	{
	}

	void CartChain::AddUnique(CartID cartID)
	{
		bool bAdded = false;
		if (std::find(carts.begin(), carts.end(), cartID) == carts.end())
		{
			carts.push_back(cartID);
			bAdded = true;
		}
		g_SceneManager->CurrentScene()->GetCartManager()->GetCart(cartID)->chainID = chainID;

		if (bAdded)
		{
			Sort();
		}
	}

	void CartChain::Remove(CartID cartID)
	{
		auto iter = std::find(carts.begin(), carts.end(), cartID);
		if (iter == carts.end())
		{
			PrintWarn("Attempted to remove cart from CartChain which isn't present!\n");
			return;
		}

		g_SceneManager->CurrentScene()->GetCartManager()->GetCart(*iter)->chainID = InvalidCartChainID;
		carts.erase(iter);
	}

	bool CartChain::Contains(CartID cartID) const
	{
		return (std::find(carts.begin(), carts.end(), cartID) != carts.end());
	}

	bool CartChain::operator!=(const CartChain& other)
	{
		return !(*this == other);
	}

	bool CartChain::operator==(const CartChain& other)
	{
		return other.carts == carts;
	}

	i32 CartChain::GetCartIndex(CartID cartID) const
	{
		for (i32 i = 0; i < (i32)carts.size(); ++i)
		{
			if (carts[i] == cartID)
			{
				return i;
			}
		}
		return -1;
	}

	real CartChain::GetCartAtIndexDistAlongTrack(i32 cartIndex)
	{
		assert(cartIndex >= 0 && cartIndex < (i32)carts.size());
		return g_SceneManager->CurrentScene()->GetCartManager()->GetCart(carts[cartIndex])->distAlongTrack;
	}

	void CartChain::Reset()
	{
		carts.clear();
		chainID = InvalidCartChainID;
	}

	void CartChain::Sort()
	{
		if (carts.size() <= 1)
		{
			return;
		}

		CartManager* cartManager = g_SceneManager->CurrentScene()->GetCartManager();
		std::sort(carts.begin(), carts.end(), [cartManager, this](CartID cartAID, CartID cartBID) -> bool
			{
				assert(cartManager->GetCart(cartAID)->chainID == chainID &&
					   cartManager->GetCart(cartBID)->chainID == chainID);
				real distA = cartManager->GetCart(cartAID)->distAlongTrack;
				real distB = cartManager->GetCart(cartBID)->distAlongTrack;
				return distA > distB;
			});
	}

	CartManager::CartManager(BaseScene* owningScene) :
		m_OwningScene(owningScene)
	{
	}

	CartID CartManager::CreateCart(const std::string& name)
	{
		// TODO: Use custom memory allocator
		CartID cartID = (CartID)m_Carts.size();
		Cart* newCart = new Cart(cartID, name);
		m_Carts.push_back(newCart);
		g_SceneManager->CurrentScene()->AddObjectAtEndOFFrame(newCart);
		return cartID;
	}

	CartID CartManager::CreateEngineCart(const std::string& name)
	{
		// TODO: Use custom memory allocator
		CartID cartID = (CartID)m_Carts.size();
		EngineCart* newCart = new EngineCart(cartID, name);
		m_Carts.push_back(newCart);
		g_SceneManager->CurrentScene()->AddObjectAtEndOFFrame(newCart);
		return cartID;
	}

	flex::Cart* CartManager::GetCart(CartID cartID)
	{
		assert(cartID < m_Carts.size());
		return m_Carts[cartID];
	}

	void CartManager::Update()
	{
		PROFILE_AUTO("Cart manager update");

		// Update cart chains
		{
			PROFILE_AUTO("Cart manager update - find chains");

			for (i32 i = 0; i < (i32)m_Carts.size(); ++i)
			{
				if (m_Carts[i]->currentTrackID == InvalidTrackID)
				{
					continue;
				}

				for (i32 j = i + 1; j < (i32)m_Carts.size(); ++j)
				{
					if (m_Carts[j]->currentTrackID == InvalidTrackID)
					{
						continue;
					}

					if (m_Carts[i]->currentTrackID == m_Carts[j]->currentTrackID)
					{
						real d = glm::distance2(m_Carts[i]->GetTransform()->GetWorldPosition(), m_Carts[j]->GetTransform()->GetWorldPosition());
						real t = glm::min(m_Carts[i]->attachThreshold * m_Carts[i]->attachThreshold,
							m_Carts[j]->attachThreshold * m_Carts[j]->attachThreshold);
						if (d <= t)
						{
							CartChainID c1 = InvalidCartChainID;
							CartChainID c2 = InvalidCartChainID;
							for (i32 c = 0; c < (i32)m_CartChains.size(); ++c)
							{
								if (m_CartChains[c].chainID != InvalidCartChainID)
								{
									if (m_CartChains[c].Contains(i))
									{
										c1 = c;
									}
									if (m_CartChains[c].Contains(j))
									{
										c2 = c;
									}
								}
							}

							if (c1 != InvalidCartChainID && c1 == c2)
							{
								continue;
							}

							if (c1 == InvalidCartChainID && c2 == InvalidCartChainID)
							{
								// Neither are already in a chain

								assert(GetCart(i)->chainID == InvalidCartChainID);
								assert(GetCart(j)->chainID == InvalidCartChainID);

								CartChainID newCartChainID = GetNextAvailableCartChainID();
								CartChain& newChain = m_CartChains[newCartChainID];
								assert(newChain.chainID == newCartChainID);

								newChain.AddUnique(i);
								newChain.AddUnique(j);

								assert(GetCart(i)->chainID == newCartChainID);
								assert(GetCart(j)->chainID == newCartChainID);

								if (newCartChainID != InvalidCartChainID &&
									newCartChainID >= m_CartChains.size())
								{
									PrintError("Cart chain update failed! Cart has invalid cart chain ID: %d, num cart chains: %d\n", newCartChainID, m_CartChains.size());
								}
							}
							else if (c1 == InvalidCartChainID)
							{
								// Only c2 is already in a chain

								assert(GetCart(i)->chainID == InvalidCartChainID);
								assert(GetCart(j)->chainID != InvalidCartChainID);

								CartChain& chain = m_CartChains[c2];
								assert(chain.chainID == c2);
								chain.AddUnique(i);

								assert(GetCart(i)->chainID == c2);
								assert(GetCart(j)->chainID == c2);

								if (m_Carts[i]->chainID != InvalidCartChainID &&
									m_Carts[i]->chainID >= m_CartChains.size())
								{
									PrintError("Cart chain update failed! Cart has invalid cart chain ID: %d, num cart chains: %d\n", m_Carts[i]->chainID, m_CartChains.size());
								}
							}
							else if (c2 == InvalidCartChainID)
							{
								// Only c1 is already in a chain

								assert(GetCart(i)->chainID != InvalidCartChainID);
								assert(GetCart(j)->chainID == InvalidCartChainID);

								CartChain& chain = m_CartChains[c1];
								assert(chain.chainID == c1);
								chain.AddUnique(j);

								assert(GetCart(i)->chainID == c1);
								assert(GetCart(j)->chainID == c1);

								if (m_Carts[j]->chainID != InvalidCartChainID &&
									m_Carts[j]->chainID >= m_CartChains.size())
								{
									PrintError("Cart chain update failed! Cart has invalid cart chain ID: %d, num cart chains: %d\n", m_Carts[j]->chainID, m_CartChains.size());
								}
							}
							else
							{
								// Both are already in chains moving the same direction, move all of c2 into c1

								assert(GetCart(i)->chainID != InvalidCartChainID);
								assert(GetCart(j)->chainID != InvalidCartChainID);

								CartChain& chain1 = m_CartChains[c1];
								CartChain& chain2 = m_CartChains[c2];

								assert(chain1.chainID == c1);
								assert(chain2.chainID == c2);

								assert(chain1 != chain2);

								if ((chain1.velT > 0.0f && chain2.velT > 0.0f) ||
									(chain1.velT < 0.0f && chain2.velT < 0.0f) ||
									(chain1.velT == 0.0f || chain2.velT == 0.0f))
								{
									while (!chain2.carts.empty())
									{
										CartID cartID = chain2.carts[chain2.carts.size() - 1];
										chain2.Remove(cartID);
										chain1.AddUnique(cartID);
									}
									m_CartChains[c2].Reset();

									assert(GetCart(i)->chainID == c1);
									assert(GetCart(j)->chainID == c1);
									assert(m_CartChains[c2].carts.empty());
								}
							}
						}
					}
				}
			}
		}

		{
			PROFILE_AUTO("Cart manager update - update positions in chains");

			for (i32 i = 0; i < (i32)m_CartChains.size(); ++i)
			{
				real avgVel = 0.0f;
				for (i32 j = 0; j < (i32)m_CartChains[i].carts.size(); ++j)
				{
					avgVel += GetCart(m_CartChains[i].carts[j])->UpdatePosition();
				}
				m_CartChains[i].velT = avgVel / m_CartChains[i].carts.size();
			}
		}

		{
			PROFILE_AUTO("Cart manager update - update positions out of chains");

			for (Cart* cart : m_Carts)
			{
				if (cart->chainID == InvalidCartChainID)
				{
					cart->UpdatePosition();
				}
			}
		}

		{
			PROFILE_AUTO("Cart manager update - draw spheres");

			PhysicsDebuggingSettings& physicsDebuggingSettings = g_Renderer->GetPhysicsDebuggingSettings();
			const bool bRenderBoundingSpheres =
				!physicsDebuggingSettings.DisableAll &&
				physicsDebuggingSettings.DrawWireframe &&
				g_EngineInstance->IsRenderingEditorObjects();
			if (bRenderBoundingSpheres)
			{
				auto debugDrawer = g_Renderer->GetDebugDrawer();
				for (Cart* cart : m_Carts)
				{
					debugDrawer->drawSphere(ToBtVec3(cart->GetTransform()->GetWorldPosition()), cart->attachThreshold, btVector3(0.8f, 0.4f, 0.67f));
				}
			}

			for (Cart* cart : m_Carts)
			{
				if (cart->chainID != InvalidCartChainID &&
					cart->chainID >= m_CartChains.size())
				{
					PrintError("Cart chain update failed! Cart has invalid cart chain ID: %d, num cart chains: %d\n", cart->chainID, m_CartChains.size());
				}
			}
		}
	}

	void CartManager::DrawImGuiObjects()
	{
		if (ImGui::TreeNode("Carts"))
		{
			CartManager* cartManager = m_OwningScene->GetCartManager();
			for (i32 i = 0; i < (i32)m_CartChains.size(); ++i)
			{
				if (m_CartChains[i].chainID != InvalidCartChainID)
				{
					std::string nodeName("Chain##" + std::to_string(i));
					if (ImGui::TreeNode(nodeName.c_str()))
					{
						for (CartID cartID : m_CartChains[i].carts)
						{
							ImGui::Text("%s", cartManager->GetCart(cartID)->GetName().c_str());
						}
						ImGui::TreePop();
					}
				}
			}
			ImGui::TreePop();
		}
	}

	void CartManager::Destroy()
	{
		m_Carts.clear();
		m_CartChains.clear();
	}

	CartChainID CartManager::GetNextAvailableCartChainID()
	{
		for (i32 i = 0; i < (i32)m_CartChains.size(); ++i)
		{
			if (m_CartChains[i].chainID == InvalidCartChainID)
			{
				m_CartChains[i].chainID = (CartChainID)i;
				return (CartChainID)i;
			}
		}

		CartChainID newCartChainID = (CartChainID)m_CartChains.size();
		m_CartChains.emplace_back(newCartChainID);
		assert(m_CartChains[(i32)m_CartChains.size()-1].chainID == newCartChainID);

		return newCartChainID;
	}

	real CartManager::GetChainDrivePower(CartChainID cartChainID)
	{
		assert(cartChainID < m_CartChains.size());
		assert(m_CartChains[cartChainID].chainID != InvalidCartChainID);

		real power = 0.0f;
		for (CartID cartID : m_CartChains[cartChainID].carts)
		{
			power += GetCart(cartID)->GetDrivePower();
		}

		return power;
	}

	CartChain* CartManager::GetCartChain(CartChainID cartChainID)
	{
		assert(cartChainID < m_CartChains.size());
		assert(m_CartChains[cartChainID].chainID != InvalidCartChainID);

		return &m_CartChains[cartChainID];
	}

	void CartManager::OnGameObjectDestroyed(GameObject* gameObject)
	{
		if (Cart* cart = dynamic_cast<Cart*>(gameObject))
		{
			for (i32 i = 0; i < (i32)m_CartChains.size(); ++i)
			{
				if (m_CartChains[i].chainID != InvalidCartChainID &&
					m_CartChains[i].Contains(cart->cartID))
				{
					m_CartChains[i].Remove(cart->cartID);

					//if (m_CartChains[i].carts.empty())
					//{
					//	m_CartChains.erase(m_CartChains.begin() + i);
					//}
				}
			}
		}
	}
} // namespace flex
