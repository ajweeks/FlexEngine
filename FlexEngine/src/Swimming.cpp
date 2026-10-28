#include "stdafx.hpp"

#include "Swimming.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletDynamics/Dynamics/btRigidBody.h>
IGNORE_WARNINGS_POP

#include "Audio/AudioManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "Game.hpp"
#include "GameSounds.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Localization.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Objects/Props.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/SceneManager.hpp"
#include "Filepaths.hpp"
#include "Platform/Platform.hpp"
#include "Water.hpp"

namespace flex
{
	namespace
	{
		// Fraction of the player's height under water at which they start floating (& stop, once back on the ground)
		const real SWIM_START_SUBMERSION = 0.62f;
		const real SWIM_STOP_SUBMERSION = 0.5f;
		// How high the eyes float above the surface while treading water
		const real FLOAT_EYE_HEIGHT = 0.3f;
		const real SWIM_SPEED = 3.0f;
		const real SWIM_SPRINT_SPEED = 5.0f;
		const real DIVE_SPEED = 2.6f;
		const real BREATH_REGEN = 5.0f; // Seconds of breath regained per second at the surface
		// Half the height of the band around the water surface which the camera is kept out of. The near plane is
		// 5cm away, so its corners reach about 7cm above & below the eye at most.
		const real WATER_LINE_BAND = 0.12f;

		real ExpDecay(real rate, real dt)
		{
			return 1.0f - std::exp(-rate * dt);
		}

		glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback)
		{
			const real len = glm::length(v);
			return len > 1e-5f ? v / len : fallback;
		}
	}

	void Swimming::Initialize(Player* player)
	{
		m_Player = player;
		Reset();
	}

	void Swimming::Reset()
	{
		m_bSwimming = false;
		m_bHeadUnderwater = false;
		m_Submersion = 0.0f;
		m_Breath = MAX_BREATH;
		m_DrowningTime = 0.0f;
		m_BlackoutAlpha = 0.0f;
	}

	bool Swimming::FixedUpdate(glm::vec3& velocity, bool bCanControl, real moveForward, real moveRight, bool bSwimUp, bool bSwimDown, bool bSprint)
	{
		const sec dt = g_FixedDeltaTime;
		const glm::vec3 pos = m_Player->GetTransform()->GetWorldPosition();
		const real height = m_Player->GetHeight();
		const real feetY = pos.y - height * 0.5f;

		real surface;
		m_bHasSurface = water::SampleSurface(pos, surface);
		if (!m_bHasSurface || m_Player->GetRidingVehicleID().IsValid() || m_Player->IsAwaitingTerrainCollision())
		{
			m_bSwimming = false;
			m_Submersion = 0.0f;
			return false;
		}
		m_SurfaceHeight = surface;
		m_Submersion = glm::clamp((surface - feetY) / height, 0.0f, 1.0f);

		const bool bWasSwimming = m_bSwimming;
		if (!m_bSwimming)
		{
			m_bSwimming = m_Submersion > SWIM_START_SUBMERSION;
		}
		else if (m_Submersion < SWIM_STOP_SUBMERSION && m_Player->IsGrounded())
		{
			// Waded out
			m_bSwimming = false;
		}

		if (m_bSwimming && !bWasSwimming && m_TimeSinceSplash > 1.0f)
		{
			m_TimeSinceSplash = 0.0f;
			GameSounds::PlaySplash(glm::vec3(pos.x, surface, pos.z), glm::clamp(-velocity.y / 10.0f, 0.2f, 1.0f));
		}
		if (!m_bSwimming)
		{
			return false;
		}

		if (!bCanControl)
		{
			moveForward = moveRight = 0.0f;
			bSwimUp = bSwimDown = bSprint = false;
		}
		bSwimDown = bSwimDown || m_bTestDive;

		// The physics world adds gravity after this step, cancel it out so only the forces below act
		velocity.y += water::GRAVITY_ACCEL * dt;

		const glm::vec3 look = m_Player->GetLookDirection();
		const glm::vec3 heading = SafeNormalize(glm::vec3(look.x, 0.0f, look.z), m_Player->GetTransform()->GetForward());
		const glm::vec3 right = glm::cross(heading, VEC3_UP);
		const glm::vec3 eye = m_Player->GetEyePosition();
		const real eyeDepth = surface - eye.y;
		// Diving: moving forward follows the look direction, at the surface it stays horizontal (unless looking
		// steeply down, which dives)
		const bool bUnder = eyeDepth > 0.25f;
		glm::vec3 forwardDir = heading;
		if (bUnder || look.y < -0.6f)
		{
			forwardDir = look;
		}
		glm::vec3 targetVel = (forwardDir * moveForward + right * moveRight);
		if (glm::length(targetVel) > 1.0f)
		{
			targetVel = glm::normalize(targetVel);
		}
		targetVel *= bSprint ? SWIM_SPRINT_SPEED : SWIM_SPEED;

		glm::vec3 horizontal(velocity.x, 0.0f, velocity.z);
		const glm::vec3 targetHorizontal(targetVel.x, 0.0f, targetVel.z);
		horizontal = glm::mix(horizontal, targetHorizontal, ExpDecay(2.5f, dt));
		velocity.x = horizontal.x;
		velocity.z = horizontal.z;

		const real floatCenterY = surface + FLOAT_EYE_HEIGHT - (eye.y - pos.y);
		real targetVertical;
		if (bSwimDown)
		{
			targetVertical = -DIVE_SPEED;
		}
		else if (bSwimUp)
		{
			// Up to the surface, & a little above it (to climb out onto ledges)
			targetVertical = pos.y < floatCenterY + 0.4f ? DIVE_SPEED : 0.0f;
		}
		else if (glm::abs(targetVel.y) > 0.1f)
		{
			targetVertical = targetVel.y;
		}
		else if (pos.y < floatCenterY - 0.3f)
		{
			// People float: drift slowly back up when not swimming down
			targetVertical = 0.8f;
		}
		else
		{
			// Treading water: spring towards floating height, bobbing gently
			const real bob = 0.06f * std::sin((real)g_SecElapsedSinceProgramStart * 1.7f);
			targetVertical = glm::clamp((floatCenterY + bob - pos.y) * 3.0f, -2.0f, 2.0f);
		}
		// Can't swim up out of the water
		if (pos.y > floatCenterY + 0.4f)
		{
			targetVertical = glm::min(targetVertical, 0.0f);
		}
		velocity.y = glm::mix(velocity.y, targetVertical, ExpDecay(m_Player->IsGrounded() ? 6.0f : 3.0f, dt));
		// Falling in from a height: water slows the player quickly
		velocity.y = glm::max(velocity.y, -12.0f);

		return true;
	}

	void Swimming::Update()
	{
		const sec dt = g_DeltaTime;
		m_TimeSinceSplash += dt;
		if (m_Player == nullptr || m_Player->GetRigidBody() == nullptr)
		{
			return;
		}

		UpdateTest();

		const glm::vec3 eye = m_Player->GetEyePosition();
		real surface;
		m_bHeadUnderwater = !m_Player->GetRidingVehicleID().IsValid() && water::SampleSurface(eye, surface) && eye.y < surface;

		const bool bPaused = g_EngineInstance->IsSimulationPaused() || (g_Game != nullptr && !g_Game->IsPlaying());
		if (!bPaused)
		{
			if (m_bHeadUnderwater)
			{
				m_Breath = glm::max(m_Breath - dt, 0.0f);
				if (m_Breath <= 0.0f)
				{
					m_DrowningTime += dt;
				}
			}
			else
			{
				m_Breath = glm::min(m_Breath + BREATH_REGEN * dt, MAX_BREATH);
				m_DrowningTime = 0.0f;
			}
		}

		if (m_DrowningTime > 0.0f)
		{
			// Flashes black, faster & darker as the player runs out of time
			const real t = glm::clamp(m_DrowningTime / DROWN_TIME, 0.0f, 1.0f);
			const real rate = glm::mix(0.7f, 2.0f, t);
			const real pulse = 0.5f - 0.5f * std::cos(m_DrowningTime * rate * TWO_PI);
			m_BlackoutAlpha = glm::clamp(pulse * glm::mix(0.55f, 1.0f, t) + t * t * 0.5f, 0.0f, 1.0f);
			if (m_DrowningTime >= DROWN_TIME)
			{
				Drown();
			}
		}
		else
		{
			m_BlackoutAlpha = glm::max(m_BlackoutAlpha - dt * 2.0f, 0.0f);
		}

		// Underwater look & sound for whichever camera is in use (the first person camera refines this once it has
		// moved off the water line, see AdjustCameraForWaterLine)
		BaseCamera* camera = g_CameraManager->CurrentCamera();
		if (camera != nullptr)
		{
			real camSurface;
			const bool bCamUnder = water::SampleSurface(camera->position, camSurface) && camera->position.y < camSurface;
			UpdateUnderwaterEffects(bCamUnder, bCamUnder ? camSurface - camera->position.y : 0.0f);
		}
	}

	void Swimming::AdjustCameraForWaterLine(glm::vec3& pos)
	{
		real surface;
		if (m_Player->GetRidingVehicleID().IsValid() || !water::SampleSurface(pos, surface))
		{
			m_bCameraUnderwater = false;
			UpdateUnderwaterEffects(false, 0.0f);
			return;
		}

		// Inside the band the near plane would straddle the surface: stay on the side the camera was on, until it's
		// moved right through the band
		const real offset = pos.y - surface;
		if (offset > WATER_LINE_BAND)
		{
			m_bCameraUnderwater = false;
		}
		else if (offset < -WATER_LINE_BAND)
		{
			m_bCameraUnderwater = true;
		}
		else
		{
			pos.y = surface + (m_bCameraUnderwater ? -WATER_LINE_BAND : WATER_LINE_BAND);
		}
		UpdateUnderwaterEffects(m_bCameraUnderwater, glm::max(surface - pos.y, 0.0f));
	}

	void Swimming::UpdateUnderwaterEffects(bool bUnderwater, real depth)
	{
		// Quick but not instant, so dipping under for a frame doesn't flicker. Deeper water is darker.
		m_UnderwaterAmount = bUnderwater ? 1.0f : 0.0f;
		g_Renderer->SetUnderwater(m_UnderwaterAmount, depth);
		AudioManager::SetUnderwaterMuffle(bUnderwater ? glm::mix(0.75f, 1.0f, glm::clamp(depth / 6.0f, 0.0f, 1.0f)) : 0.0f);
	}

	void Swimming::Drown()
	{
		++m_DrownCount;
		Print("Player drowned (%u)\n", m_DrownCount);
		// Respawns (resetting traversal, & this, on the way)
		m_Player->Reset();
		Reset();
		m_Player->ShowNotification(LOC(Swim_Drowned));
	}

	void Swimming::DrawHUD() const
	{
		if (!m_Player->m_bPossessed)
		{
			return;
		}

		if (m_bSwimming && m_Breath >= MAX_BREATH && !m_bHeadUnderwater)
		{
			g_Renderer->DrawStringSS(LOC(Swim_Controls), glm::vec4(0.95f, 0.95f, 0.95f, 0.8f), AnchorPoint::BOTTOM, glm::vec2(0.0f, 0.17f), 1.5f, 0.6f);
		}

		// Breath: a row of bubbles above the stamina bar, popping as it runs out
		if (m_Breath < MAX_BREATH - 0.01f || m_bHeadUnderwater)
		{
			const i32 bubbleCount = 10;
			const real fraction = m_Breath / MAX_BREATH;
			const glm::vec2 halfSize(0.011f, 0.011f);
			const real spacing = 0.026f;
			const real y = -0.155f;
			const bool bLow = fraction < 0.25f;
			const real flash = bLow ? 0.6f + 0.4f * std::sin((real)g_SecElapsedSinceProgramStart * 10.0f) : 1.0f;
			for (i32 i = 0; i < bubbleCount; ++i)
			{
				const glm::vec2 center(((real)i - (bubbleCount - 1) * 0.5f) * spacing, y);
				// How full this bubble is [0, 1]
				const real fill = glm::clamp(fraction * bubbleCount - (real)i, 0.0f, 1.0f);
				g_Renderer->EnqueueUntexturedQuad(center, AnchorPoint::CENTER, halfSize + glm::vec2(0.003f), glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
				if (fill > 0.0f)
				{
					const glm::vec4 colour = bLow ? glm::vec4(1.0f, 0.45f, 0.3f, 0.9f * flash) : glm::vec4(0.55f, 0.85f, 1.0f, 0.9f);
					g_Renderer->EnqueueUntexturedQuad(center, AnchorPoint::CENTER, halfSize * glm::mix(0.35f, 1.0f, fill), colour);
				}
			}
			if (m_DrowningTime > 0.0f)
			{
				g_Renderer->DrawStringSS(LOC(Swim_OutOfBreath), glm::vec4(1.0f, 0.4f, 0.3f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, -0.2f), 1.5f, 0.6f);
			}
		}
	}

	// FLEX_TEST_SWIM=<any>: once playing, moves the player into the nearest deep water, checks they float, dives
	// (screenshots swim_surface.png & swim_under.png), drops wood (floats) & a battery (sinks), then holds them under
	// until they drown & prints PASS/FAIL
	void Swimming::UpdateTest()
	{
		static const bool s_bTest = getenv("FLEX_TEST_SWIM") != nullptr;
		if (!s_bTest || (g_Game != nullptr && !g_Game->IsPlaying()) || m_Player->IsAwaitingTerrainCollision())
		{
			return;
		}
		TerrainGenerator* terrain = TerrainGenerator::GetActive();
		if (terrain == nullptr || terrain->IsWorldChangePending())
		{
			return;
		}
		m_TestTime += g_DeltaTime;
		const glm::vec3 pos = m_Player->GetTransform()->GetWorldPosition();
		BaseScene* scene = g_SceneManager->CurrentScene();
		auto Screenshot = [](const char* name)
		{
			const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
			Platform::CreateDirectoryRecursive(directory);
			g_Renderer->RequestScreenshot(directory + name);
		};
		auto Next = [this]() { ++m_TestState; m_TestTime = 0.0f; };

		switch (m_TestState)
		{
		case 0:
		{
			if (m_TestTime < 3.0f)
			{
				break;
			}
			// Spiral out for deep, open water
			bool bFound = false;
			const real spacing = 24.0f;
			for (i32 ring = 0; ring < 120 && !bFound; ++ring)
			{
				const i32 steps = glm::max(ring * 8, 1);
				for (i32 i = 0; i < steps && !bFound; ++i)
				{
					const real angle = TWO_PI * (real)i / (real)steps;
					const real x = pos.x + std::cos(angle) * ring * spacing;
					const real z = pos.z + std::sin(angle) * ring * spacing;
					const terrain::ColumnSample column = terrain->SampleColumn(x, z);
					if (column.lakeID != 0 && column.lakeT < 0.5f && column.waterLevel - column.height > 8.0f)
					{
						m_TestWaterPos = glm::vec3(x, column.waterLevel, z);
						bFound = true;
					}
				}
			}
			if (!bFound)
			{
				PrintError("Swim test: FAIL (no deep water found)\n");
				g_EngineInstance->Stop();
				m_TestState = 100;
				break;
			}
			Print("Swim test: water at (%.1f, %.1f, %.1f), %.0fm away\n", m_TestWaterPos.x, m_TestWaterPos.y, m_TestWaterPos.z, glm::distance(pos, m_TestWaterPos));
			m_Player->TeleportOnTerrain(m_TestWaterPos + VEC3_UP * 1.0f, true);
			Next();
			break;
		}
		case 1:
			if (m_TestTime > 1.0f && m_TestTime - g_DeltaTime <= 1.0f)
			{
				// (Teleporting moves to dry land nearby, so put the player straight into the water)
				const glm::vec3 waterPos = m_TestWaterPos + VEC3_UP * 0.5f;
				m_Player->GetRigidBody()->SetWorldPosition(waterPos);
				m_Player->GetTransform()->SetWorldPosition(waterPos);
			}
			if (m_TestTime > 7.0f)
			{
				Print("Swim test: swimming %d, submersion %.2f, eye %.2fm above the surface (%.2f), breath %.1f\n", (i32)m_bSwimming,
					m_Submersion, m_Player->GetEyePosition().y - m_SurfaceHeight, m_SurfaceHeight, m_Breath);
				Screenshot("swim_surface.png");
				// Drop some things
				const glm::vec3 look = m_Player->GetLookDirection();
				const glm::vec3 dropPos = pos + glm::normalize(glm::vec3(look.x, 0.0f, look.z) + glm::vec3(0.01f, 0.0f, 0.0f)) * 2.0f + VEC3_UP * 1.0f;
				DroppedItem* wood = scene->CreateDroppedItem(g_ResourceManager->GetPrefabID("wood"), 1, dropPos, VEC3_ZERO);
				DroppedItem* battery = scene->CreateDroppedItem(g_ResourceManager->GetPrefabID("battery"), 1, dropPos + VEC3_RIGHT * 1.0f, VEC3_ZERO);
				// (Not picked up again)
				if (wood != nullptr) wood->pickupDelay = 1e9f;
				if (battery != nullptr) battery->pickupDelay = 1e9f;
				m_TestWoodID = wood != nullptr ? wood->ID : InvalidGameObjectID;
				m_TestBatteryID = battery != nullptr ? battery->ID : InvalidGameObjectID;
				Next();
			}
			break;
		case 2:
			// Dive
			m_bTestDive = m_TestTime < 2.0f;
			if (m_TestTime > 4.0f)
			{
				Print("Swim test: dove to %.1fm below the surface, head under %d, breath %.1f\n", m_SurfaceHeight - m_Player->GetEyePosition().y, (i32)m_bHeadUnderwater, m_Breath);
				Screenshot("swim_under.png");
				Next();
			}
			break;
		case 3:
		{
			if (m_TestTime < 2.0f)
			{
				break;
			}
			GameObject* wood = m_TestWoodID.Get();
			GameObject* battery = m_TestBatteryID.Get();
			if (wood != nullptr && battery != nullptr)
			{
				Print("Swim test: wood at %.2fm (relative to the surface), battery at %.2fm\n", wood->GetTransform()->GetWorldPosition().y - m_SurfaceHeight,
					battery->GetTransform()->GetWorldPosition().y - m_SurfaceHeight);
			}
			// Out of breath
			m_Breath = 0.0f;
			m_bTestDive = true;
			Next();
			break;
		}
		case 4:
			m_bTestDive = true;
			if (m_TestTime > 4.0f && m_TestTime - g_DeltaTime <= 4.0f)
			{
				Print("Swim test: drowning for %.1fs, blackout %.2f\n", m_DrowningTime, m_BlackoutAlpha);
				Screenshot("swim_drowning.png");
			}
			if (m_DrownCount > 0 || m_TestTime > 15.0f)
			{
				m_bTestDive = false;
				GameObject* wood = m_TestWoodID.Get();
				GameObject* battery = m_TestBatteryID.Get();
				const real woodY = wood != nullptr ? wood->GetTransform()->GetWorldPosition().y - m_TestWaterPos.y : -999.0f;
				const real batteryY = battery != nullptr ? battery->GetTransform()->GetWorldPosition().y - m_TestWaterPos.y : 999.0f;
				const bool bPass = m_DrownCount == 1 && glm::abs(woodY) < 0.5f && batteryY < -2.0f;
				Print("Swim test: %s (drowned %u after %.1fs, wood %.2fm, battery %.2fm relative to the surface)\n", bPass ? "PASS" : "FAIL", m_DrownCount, m_TestTime, woodY, batteryY);
				Next();
			}
			break;
		case 5:
			if (m_TestTime > 2.0f)
			{
				g_EngineInstance->Stop();
				Next();
			}
			break;
		default:
			break;
		}
	}

	void Swimming::DrawImGui()
	{
		if (!ImGui::TreeNode("Swimming"))
		{
			return;
		}
		ImGui::Text("Swimming: %s, head under water: %s, submersion %.2f, surface %s%.2f", m_bSwimming ? "true" : "false",
			m_bHeadUnderwater ? "true" : "false", m_Submersion, m_bHasSurface ? "" : "(none) ", m_SurfaceHeight);
		ImGui::ProgressBar(m_Breath / MAX_BREATH, ImVec2(-1.0f, 0.0f), "Breath");
		ImGui::Text("Drowning: %.1fs / %.1fs, drowned %u times", m_DrowningTime, DROWN_TIME, m_DrownCount);
		if (ImGui::Button("Empty breath"))
		{
			m_Breath = 0.0f;
		}
		ImGui::TreePop();
	}
} // namespace flex
