#include "stdafx.hpp"

#include "Player.hpp"
#include "Swimming.hpp"
#include "GameSounds.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionDispatch/btManifoldResult.h>

#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/norm.hpp> // For distance2
IGNORE_WARNINGS_POP

#include <queue>

#include "Cameras/BaseCamera.hpp"
#include "Crafting.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "Game.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "GameControls.hpp"
#include "InputManager.hpp"
#include "Inventory.hpp"
#include "Localization.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Platform/Platform.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Objects/Campfire.hpp" // For the placement preview test
#include "Scene/Objects/Industry.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/SceneManager.hpp"
#include "Systems/TrackManager.hpp"
#include "Traversal.hpp"
#include "Woodcraft.hpp"
#include "Window/Window.hpp"

// The player in streamed terrain: digging/filling with a pickaxe or shovel, & staying on top of the terrain
namespace flex
{
	namespace
	{
		// Seconds to dig out (or fill) a full sized hole/mound with each tool. Shovels are best in loose ground,
		// pickaxes in rock.
		struct ToolDigTimes
		{
			real loose;
			real rock;
			real fill;
		};

		bool GetToolDigTimes(StringID toolTypeID, ToolDigTimes& outTimes)
		{
			switch (toolTypeID)
			{
			case ShovelSID: outTimes = { 0.35f, 2.4f, 0.3f }; return true;
			case PickAxeSID: outTimes = { 0.8f, 0.65f, 0.5f }; return true;
			default: return false;
			}
		}

		std::string ToolName(StringID toolTypeID)
		{
			switch (toolTypeID)
			{
			case ShovelSID: return LOC(Item_Shovel);
			case PickAxeSID: return LOC(Item_Pickaxe);
			case AxeSID: return LOC(Item_Axe);
			default: return "";
			}
		}

		// Dig & fill strokes move the surface by this much (meters, along the edit normal)
		const real STROKE_DEPTH = 1.0f;
		// Edit planes are snapped to multiples of this along their normal, so neighbouring holes, mounds & flattened
		// areas line up, & repeated edits build flat surfaces. Each step a stroke moves is one edit.
		const real LEVEL_STEP = 0.25f;
		// At most this many edits are made per second, & only once the previous edit's chunks have been re-meshed
		const real MAX_TERRAIN_EDIT_RATE = 15.0f;
		// The aim moved this far (meters) off the hole/mound being worked on starts a new one
		const real STROKE_AIM_SLACK = 0.6f;
		// Each flatten "dab" takes this much longer than a fill stroke, & isn't repeated until the aim has moved this
		// fraction of the brush's radius
		const real FLATTEN_TIME_SCALE = 1.5f;
		const real FLATTEN_SPACING = 0.3f;
		// Edit planes within this angle (radians) of level are level
		const real LEVEL_SNAP_ANGLE = 0.35f;

		// Dig & fill strokes take this much longer than the tools' dig times (which were for a sphere of the brush's
		// radius)
		const real STROKE_TIME_SCALE = 2.0f;

		const u64 BRUSH_PREVIEW_KEY = 1;
		// Where the brush was last shown (for the test's log)
		glm::vec3 s_LastBrushCenter = VEC3_ZERO;
		glm::vec3 s_LastBrushNormal = VEC3_UP;
		// Frames an edit waited because it would bury the player, or for re-meshing (for the test's log)
		u32 s_BlockedFrames = 0;
		u32 s_ApplyingFrames = 0;
		const glm::vec4 BRUSH_IDLE_COLOUR(0.8f, 0.9f, 1.2f, 0.3f);
		const glm::vec4 BRUSH_DIG_COLOUR(1.6f, 0.75f, 0.25f, 0.45f);
		const glm::vec4 BRUSH_FILL_COLOUR(0.45f, 1.0f, 1.6f, 0.45f);
		const glm::vec4 BRUSH_FLATTEN_COLOUR(0.6f, 1.6f, 0.5f, 0.45f);

		// direction < 0: down to the step below, > 0: up to the step above, 0: nearest
		real SnapLevel(real level, i32 direction)
		{
			const real steps = level / LEVEL_STEP;
			return (direction < 0 ? std::floor(steps) : (direction > 0 ? std::ceil(steps) : std::round(steps))) * LEVEL_STEP;
		}

		// Snaps a direction to 15 degree steps of pitch (from up) & 22.5 degree steps of heading (& to up near it), so
		// edits made from slightly different angles share a plane orientation
		glm::vec3 QuantizeEditNormal(const glm::vec3& normal)
		{
			const real length = glm::length(normal);
			if (length < 1e-6f)
			{
				return VEC3_UP;
			}
			const glm::vec3 n = normal / length;
			const real pitchStep = PI / 12.0f;
			const real yawStep = PI / 8.0f;
			real pitch = std::acos(glm::clamp(n.y, -1.0f, 1.0f));
			if (pitch < LEVEL_SNAP_ANGLE)
			{
				return VEC3_UP;
			}
			if (pitch > PI - LEVEL_SNAP_ANGLE)
			{
				return -VEC3_UP;
			}
			pitch = std::round(pitch / pitchStep) * pitchStep;
			const real yaw = std::round(std::atan2(n.x, n.z) / yawStep) * yawStep;
			return glm::vec3(std::sin(pitch) * std::sin(yaw), std::cos(pitch), std::sin(pitch) * std::cos(yaw));
		}

		// A ring at the brush's radius (where it stops having any effect) & a fainter one where it's at full strength
		void BuildBrushGeometry(PlacementPreview::Geometry& geometry)
		{
			geometry.Ring(0.95f, 1.0f, 0.05f, 48);
			geometry.Ring(0.43f, 0.46f, 0.03f, 32);
		}
	} // anonymous namespace

	void Player::UpdateTerrainTool()
	{
		m_bTerrainToolActive = false;
		m_TimeSinceTerrainEdit += g_DeltaTime;
		// (hidden unless it's shown below)
		PlacementPreview::Frame brushFrame(m_TerrainBrushPreview);

		if (m_TerrainTestStep == -2)
		{
			// FLEX_TEST_TERRAIN_TOOL=4: stands on a steep slope & screenshots the terrain brush gizmo from four sides
			const char* testMode = getenv("FLEX_TEST_TERRAIN_TOOL");
			m_TerrainTestMode = testMode != nullptr ? atoi(testMode) : 0;
			m_TerrainTestStep = m_TerrainTestMode > 0 ? 0 : -1;
		}
		if (m_TerrainTestStep >= 0)
		{
			UpdateTerrainToolTest();
		}

		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		const StringID toolTypeID = GetWieldedToolType();
		ToolDigTimes digTimes;
		if (terrainGenerator == nullptr ||
			!m_bPossessed ||
			!GetToolDigTimes(toolTypeID, digTimes) ||
			m_BuildMode.IsActive() ||
			IsAnyInventoryShowing() ||
			m_RidingVehicleID.IsValid() ||
			m_TrackRidingID != InvalidTrackID ||
			IsPlacingTrack() ||
			IsEditingTrack() ||
			g_EngineInstance->IsSimulationPaused() ||
			(m_Woodcraft != nullptr && m_Woodcraft->BlocksTerrainTool()) ||
			(m_Crafting != nullptr && m_Crafting->IsPlacing()))
		{
			EndTerrainStroke();
			m_bFlattenPlaneLocked = false;
			return;
		}

		const i32 materialCount = (i32)ARRAY_LENGTH(terrain::FILL_MATERIALS);
		if (g_InputManager->GetKeyPressed(GameKeys::TERRAIN_MATERIAL))
		{
			const i32 step = g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_SHIFT) != 0 ? -1 : 1;
			m_TerrainFillMaterialIndex = (m_TerrainFillMaterialIndex + step + materialCount) % materialCount;
			// (the mound being placed is of the previous material)
			EndTerrainStroke();
		}
		m_TerrainFillMaterialIndex = glm::clamp(m_TerrainFillMaterialIndex, 0, materialCount - 1);
		const terrain::Material fillMaterial = terrain::FILL_MATERIALS[m_TerrainFillMaterialIndex];

		BaseCamera* camera = g_CameraManager->CurrentCamera();
		glm::vec3 hitPos, hitNormal;
		// Digging into the same spot again looks through the hole just dug, at its far side (up to a dig radius
		// further away). Surfaces a little past the reach are still dug into, with the brush held at the reach.
		const real radius = m_TerrainDigRadius;
		const real rayLength = m_TerrainToolReach + radius * 0.9f;
		if (!terrainGenerator->Raycast(camera->position, camera->forward, rayLength, hitPos, hitNormal))
		{
			EndTerrainStroke();
			return;
		}
		const real hitDistance = glm::distance(camera->position, hitPos);
		if (hitDistance > m_TerrainToolReach)
		{
			hitPos = camera->position + camera->forward * m_TerrainToolReach;
		}
		m_bTerrainToolActive = true;

		TerrainToolAction action = TerrainToolAction::NONE;
		if (g_InputManager->IsMouseButtonDown(GameKeys::USE_ITEM) || m_TestTerrainAction == 1)
		{
			action = TerrainToolAction::DIG;
		}
		else if (g_InputManager->IsMouseButtonDown(GameKeys::USE_ITEM_SECONDARY) || m_TestTerrainAction == 2)
		{
			action = TerrainToolAction::FILL;
		}
		else if (g_InputManager->IsMouseButtonDown(GameKeys::TERRAIN_FLATTEN) || m_TestTerrainAction == 3)
		{
			action = TerrainToolAction::FLATTEN;
		}
		if (action != TerrainToolAction::FLATTEN)
		{
			// (a new plane is picked the next time the button is pressed)
			m_bFlattenPlaneLocked = false;
			m_LastFlattenCenter = glm::vec3(1e30f);
		}

		auto ShowBrush = [&](const glm::vec3& center, const glm::vec3& normal, const glm::vec4& colour)
		{
			// (lifted a little, so it isn't hidden by the surface it lies on)
			s_LastBrushCenter = center;
			s_LastBrushNormal = normal;
			// Lifted off the surface so it isn't buried by slopes & bumps, & drawn without a depth test (dashed where hidden)
			m_TerrainBrushPreview.SetXRay(true);
			m_TerrainBrushPreview.Show(BRUSH_PREVIEW_KEY, BuildBrushGeometry, center + normal * (0.08f + radius * 0.05f), RotationFromUp(normal), glm::vec3(radius), colour);
		};
		// The plane a stroke is moving the surface to, progress [0, 1] of the way through it
		auto StrokeLevel = [&](real progress)
		{
			const TerrainStroke& stroke = m_TerrainStroke;
			if (stroke.action == TerrainToolAction::FLATTEN)
			{
				return stroke.startLevel;
			}
			const real amount = STROKE_DEPTH * progress;
			return stroke.action == TerrainToolAction::DIG ? SnapLevel(stroke.startLevel - amount, -1) : SnapLevel(stroke.startLevel + amount, 1);
		};

		if (m_TerrainStroke.bActive)
		{
			const glm::vec3 offset = hitPos - m_TerrainStroke.anchor;
			const real lateral = glm::length(offset - m_TerrainStroke.normal * glm::dot(offset, m_TerrainStroke.normal));
			const bool bComplete = m_TerrainStroke.action != TerrainToolAction::FLATTEN && m_TerrainStroke.progress >= 1.0f &&
				m_TerrainStroke.bApplied && m_TerrainStroke.appliedLevel == StrokeLevel(1.0f);
			const bool bAimedAway = m_TerrainStroke.action != TerrainToolAction::FLATTEN && lateral > radius * 0.5f + STROKE_AIM_SLACK;
			if (action != m_TerrainStroke.action || bComplete || bAimedAway)
			{
				EndTerrainStroke();
			}
		}

		if (action == TerrainToolAction::NONE)
		{
			// Where the brush would start
			ShowBrush(hitPos, ChooseTerrainEditNormal(toolTypeID, hitPos, hitNormal), BRUSH_IDLE_COLOUR);
			return;
		}

		if (!m_TerrainStroke.bActive)
		{
			m_TerrainStroke = {};
			m_TerrainStroke.bActive = true;
			m_TerrainStroke.action = action;
			m_TerrainStroke.anchor = hitPos;
			if (action == TerrainToolAction::FLATTEN)
			{
				if (!m_bFlattenPlaneLocked)
				{
					// To the level first aimed at, kept until the button is released
					m_bFlattenPlaneLocked = true;
					m_FlattenNormal = ChooseTerrainEditNormal(toolTypeID, hitPos, hitNormal);
					m_FlattenLevel = SnapLevel(glm::dot(hitPos, m_FlattenNormal), 0);
				}
				m_TerrainStroke.normal = m_FlattenNormal;
				m_TerrainStroke.startLevel = m_FlattenLevel;
			}
			else
			{
				m_TerrainStroke.normal = ChooseTerrainEditNormal(toolTypeID, hitPos, hitNormal);
				m_TerrainStroke.startLevel = glm::dot(hitPos, m_TerrainStroke.normal);

				// Still working on the same spot: carry on from the last stroke, so holes deepen straight down with a flat
				// bottom (rather than funnelling towards wherever the aim lands on their walls) & mounds rise as columns
				const TerrainStroke& last = m_LastTerrainStroke;
				if (last.bApplied && last.action == action && m_TimeSinceTerrainEdit < 1.0f)
				{
					const glm::vec3 offset = hitPos - last.anchor;
					const real lateral = glm::length(offset - last.normal * glm::dot(offset, last.normal));
					if (lateral < radius * 0.6f)
					{
						m_TerrainStroke.anchor = last.anchor;
						m_TerrainStroke.normal = last.normal;
						m_TerrainStroke.startLevel = last.appliedLevel;
					}
				}
			}
			m_TerrainStroke.hardness = action == TerrainToolAction::DIG ? SampleTerrainHardness(hitPos, hitNormal) : 0.0f;
		}
		if (action == TerrainToolAction::FLATTEN)
		{
			// The flattening brush follows the aim (on its plane)
			m_TerrainStroke.anchor = hitPos;
		}

		real strokeTime = digTimes.fill * STROKE_TIME_SCALE;
		if (action == TerrainToolAction::DIG)
		{
			strokeTime = Lerp(digTimes.loose, digTimes.rock, m_TerrainStroke.hardness) * STROKE_TIME_SCALE;
		}
		else if (action == TerrainToolAction::FLATTEN)
		{
			strokeTime = digTimes.fill * FLATTEN_TIME_SCALE;
		}
		// (a long frame, e.g. a hitch, doesn't make a whole stroke at once)
		m_TerrainStroke.progress = glm::min(m_TerrainStroke.progress + glm::min(g_DeltaTime, 0.1f) / strokeTime, 1.0f);

		const real level = StrokeLevel(m_TerrainStroke.progress);
		const glm::vec3 normal = m_TerrainStroke.normal;
		const glm::vec3 center = GetTerrainStrokeCenter(level);

		bool bWantsEdit;
		if (action == TerrainToolAction::FLATTEN)
		{
			bWantsEdit = m_TerrainStroke.progress >= 1.0f;
		}
		else
		{
			// (the first step needs to move the surface noticeably)
			bWantsEdit = m_TerrainStroke.bApplied ? level != m_TerrainStroke.appliedLevel : glm::abs(level - m_TerrainStroke.startLevel) > 0.02f;
		}

		// Don't bury ourselves: much of the capsule below the new surface, within the brush (the capsule's bottom sits
		// a little below the surface anyway, since the collision mesh is coarser than the density)
		bool bBlocked = false;
		if (action != TerrainToolAction::DIG)
		{
			const glm::vec3 offset = m_Transform.GetWorldPosition() - center;
			const real axial = glm::dot(offset, normal);
			const real lateral = glm::length(offset - normal * axial);
			const real lowest = axial - (glm::abs(normal.y) * glm::max(m_Height * 0.5f - m_Radius, 0.0f) + m_Radius);
			bBlocked = lateral < radius * 0.8f + m_Radius && lowest < -0.6f && axial > -m_Height;
		}

		glm::vec4 brushColour = BRUSH_DIG_COLOUR;
		if (bBlocked)
		{
			brushColour = PlacementPreview::INVALID_COLOUR;
		}
		else if (action == TerrainToolAction::FILL)
		{
			brushColour = BRUSH_FILL_COLOUR;
		}
		else if (action == TerrainToolAction::FLATTEN)
		{
			brushColour = BRUSH_FLATTEN_COLOUR;
		}
		// (on the surface made so far, rather than on the plane being moved towards, which may be buried)
		const real shownLevel = action == TerrainToolAction::FLATTEN ? level : (m_TerrainStroke.bApplied ? m_TerrainStroke.appliedLevel : m_TerrainStroke.startLevel);
		ShowBrush(GetTerrainStrokeCenter(shownLevel), normal, brushColour);

		if (m_ToolSwingTimer <= 0.0f)
		{
			SwingTool(glm::clamp(strokeTime * 0.4f, 0.25f, 0.45f));
		}

		// Rate limited, & waiting for the previous edit's chunks (restarting their re-meshing each frame could mean
		// they never finish)
		if (!bWantsEdit || bBlocked || m_TimeSinceTerrainEdit < 1.0f / MAX_TERRAIN_EDIT_RATE || terrainGenerator->IsApplyingEdits())
		{
			if (bWantsEdit)
			{
				m_TestTerrainWaitTime += g_DeltaTime;
				s_BlockedFrames += bBlocked ? 1 : 0;
				s_ApplyingFrames += terrainGenerator->IsApplyingEdits() ? 1 : 0;
			}
			return;
		}

		if (action == TerrainToolAction::FLATTEN)
		{
			// (not again & again on the same spot while the aim is held still)
			if (glm::distance(center, m_LastFlattenCenter) > radius * FLATTEN_SPACING)
			{
				terrainGenerator->FlattenArea(center, normal, radius, fillMaterial);
				m_LastFlattenCenter = center;
				m_TerrainStroke.bApplied = true;
				m_TerrainStroke.appliedLevel = level;
				m_TimeSinceTerrainEdit = 0.0f;
				++m_TestTerrainEditCount;
			}
			// Each dab is its own stroke
			EndTerrainStroke();
			return;
		}

		// Reaching from the plane to past the surface the stroke started from (& any slope around it)
		const real height = glm::abs(level - m_TerrainStroke.startLevel) + radius + 0.5f;
		if (action == TerrainToolAction::FILL)
		{
			terrainGenerator->RaiseArea(center, normal, radius, fillMaterial, height);
		}
		else
		{
			terrainGenerator->LowerArea(center, normal, radius, height);
			GameSounds::PlayDig(center, normal, m_TerrainStroke.hardness);
		}
		m_TerrainStroke.bApplied = true;
		m_TerrainStroke.appliedLevel = level;
		m_TimeSinceTerrainEdit = 0.0f;
		++m_TestTerrainEditCount;
	}

	void Player::EndTerrainStroke()
	{
		if (m_TerrainStroke.bActive && m_TerrainStroke.bApplied)
		{
			m_LastTerrainStroke = m_TerrainStroke;
			++m_TestTerrainStrokeCount;
			if (m_TerrainStroke.action != TerrainToolAction::FILL)
			{
				// Removes any scraps of terrain the stroke left floating
				if (TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive())
				{
					terrainGenerator->RemoveFloatingFragments(GetTerrainStrokeCenter(m_TerrainStroke.appliedLevel), m_TerrainDigRadius + 1.0f);
				}
			}
			if (m_TerrainStroke.action == TerrainToolAction::DIG && m_Crafting != nullptr)
			{
				// Rock gives stone (& sometimes ore)
				m_Crafting->OnDug(GetTerrainStrokeCenter(m_TerrainStroke.appliedLevel), m_TerrainStroke.hardness);
			}
		}
		m_TerrainStroke = {};
	}

	glm::vec3 Player::GetTerrainStrokeCenter(real level) const
	{
		// The anchor, moved along the normal onto the plane
		return m_TerrainStroke.anchor + m_TerrainStroke.normal * (level - glm::dot(m_TerrainStroke.anchor, m_TerrainStroke.normal));
	}

	glm::vec3 Player::ChooseTerrainEditNormal(StringID toolTypeID, const glm::vec3& hitPos, const glm::vec3& hitNormal) const
	{
		glm::vec3 normal = hitNormal;
		if (TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive())
		{
			const glm::vec3 smoothNormal = terrainGenerator->SampleSmoothNormal(hitPos, m_TerrainDigRadius * 0.6f);
			// (unless the surface aimed at is something thin, which the average says little about)
			if (glm::dot(smoothNormal, hitNormal) > 0.3f)
			{
				normal = smoothNormal;
			}
		}
		if (toolTypeID == ShovelSID && normal.y > -0.3f)
		{
			// Shovels work the ground: level, or close to it
			normal = glm::normalize(glm::mix(normal, VEC3_UP, 0.7f));
		}
		return QuantizeEditNormal(normal);
	}

	real Player::SampleTerrainHardness(const glm::vec3& pos, const glm::vec3& normal) const
	{
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		terrain::Material placedMaterial;
		if (terrainGenerator->FindPlacedMaterial(pos, placedMaterial))
		{
			switch (placedMaterial)
			{
			case terrain::Material::ROCK:
			case terrain::Material::GRANITE:
			case terrain::Material::SANDSTONE:
			case terrain::Material::MARBLE:
			case terrain::Material::BASALT:
				return 1.0f;
			case terrain::Material::CLAY:
				return 0.35f;
			default:
				// Soil, sand, gravel, snow & grass
				return 0.0f;
			}
		}

		// Generated terrain. On its surface cliffs are rock & the ground is soil/sand/gravel/grass/snow. Below the surface
		// (e.g. the walls of a hole, which are steep, so would otherwise look like cliffs) there's a few meters of loose
		// ground over rock.
		const terrain::ColumnSample column = terrainGenerator->SampleColumn(pos.x, pos.z);
		const real depth = column.height - pos.y;
		const real SOIL_DEPTH = 4.0f;
		const real depthHardness = glm::smoothstep(SOIL_DEPTH, SOIL_DEPTH + 3.0f, depth);
		if (depth > 0.75f)
		{
			return depthHardness;
		}
		const glm::vec4 weights = terrainGenerator->SampleSurface(pos, normal).weights;
		const real total = weights.x + weights.y + weights.z + weights.w;
		const real surfaceHardness = total > 1e-3f ? glm::clamp(weights.y / total, 0.0f, 1.0f) : 0.0f;
		return glm::max(surfaceHardness, depthHardness);
	}

	void Player::TeleportOnTerrain(const glm::vec3& pos, bool bKeepExactPosition /* = false */, bool bFade /* = true */)
	{
		m_Transform.SetWorldPosition(pos);
		m_RigidBody->GetRigidBodyInternal()->setLinearVelocity(btVector3(0, 0, 0));
		m_bTerrainPlacementPending = true;
		m_bTerrainPlacementExact = bKeepExactPosition;
		if (bFade)
		{
			StartTeleportFade();
		}
	}

	void Player::StartTeleportFade()
	{
		m_TeleportFadeState = TeleportFadeState::HOLDING;
		m_TeleportFadeAlpha = 1.0f;
		m_TeleportFadeTime = 0.0f;
		m_TeleportPlacedTime = 0.0f;
	}

	void Player::UpdateTeleportFade()
	{
		if (m_TeleportFadeState == TeleportFadeState::NONE)
		{
			return;
		}

		// (Unscaled, so slow motion can't stretch it out)
		const sec dt = g_UnpausedDeltaTime;
		const bool bInGame = g_Game == nullptr || g_Game->IsPlaying();
		m_TeleportFadeTime += dt;

		if (m_TeleportFadeState == TeleportFadeState::HOLDING)
		{
			// Placed once collision has loaded below the player (or there's no streamed terrain to wait for)
			const bool bPlaced = !IsAwaitingTerrainCollision() || TerrainGenerator::GetActive() == nullptr;
			m_TeleportPlacedTime = bPlaced ? m_TeleportPlacedTime + dt : 0.0f;

			// Landed: standing on the ground, or settled for a moment (e.g. swimming, or held up by something else)
			const real speed = glm::length(ToVec3(m_RigidBody->GetRigidBodyInternal()->getLinearVelocity()));
			const bool bLanded = bPlaced &&
				((m_bGrounded && m_TeleportPlacedTime > 0.05f) ||
				 m_RidingVehicleID.IsValid() ||
				 (speed < 1.0f && m_TeleportPlacedTime > 0.5f) ||
				 m_TeleportPlacedTime > 2.0f);

			// Never black for longer than this, even if the terrain failed to load
			const sec TIMEOUT = 12.0f;
			const sec MIN_HOLD = 0.1f;
			if ((bLanded && m_TeleportFadeTime > MIN_HOLD) || m_TeleportFadeTime > TIMEOUT)
			{
				if (!bLanded)
				{
					PrintWarn("Teleport fade timed out waiting for the player to be placed\n");
				}
				// Over the loading screen, stay black until it's gone so the world fades in after it
				if (bInGame)
				{
					m_TeleportFadeState = TeleportFadeState::FADING_IN;
					Print("Teleport: player landed after %.2fs (placed for %.2fs, grounded: %d), fading in\n", m_TeleportFadeTime, m_TeleportPlacedTime, (i32)m_bGrounded);
				}
			}
		}
		else if (m_TeleportFadeState == TeleportFadeState::FADING_IN)
		{
			const sec FADE_IN_DURATION = 0.35f;
			m_TeleportFadeAlpha -= dt / FADE_IN_DURATION;
			if (m_TeleportFadeAlpha <= 0.0f)
			{
				m_TeleportFadeAlpha = 0.0f;
				m_TeleportFadeState = TeleportFadeState::NONE;
			}
		}
	}

	void Player::DrawTeleportFade() const
	{
		// (Also the flashing blackout while drowning)
		const real drowningAlpha = m_Traversal != nullptr && m_Traversal->GetSwimming() != nullptr ? m_Traversal->GetSwimming()->GetBlackoutAlpha() : 0.0f;
		const real fadeAlpha = glm::max(m_TeleportFadeAlpha, drowningAlpha);
		if (fadeAlpha <= 0.0f)
		{
			return;
		}
		// Not over menus/the loading screen (which have their own backgrounds), or while flying the debug camera
		BaseCamera* camera = g_CameraManager->CurrentCamera();
		if ((g_Game != nullptr && !g_Game->IsPlaying()) || camera == nullptr || !camera->bPossessPlayer)
		{
			return;
		}

		const glm::vec2i windowSize = g_Window->GetSize();
		const real aspectRatio = (real)glm::max(windowSize.x, 1) / (real)glm::max(windowSize.y, 1);
		SpriteQuadDrawInfo drawInfo = {};
		drawInfo.anchor = AnchorPoint::CENTER;
		drawInfo.bScreenSpace = true;
		drawInfo.bReadDepth = false;
		drawInfo.bEnableAlbedoSampler = false;
		// Above the HUD (& minimap)
		drawInfo.zOrder = Z_ORDER_UI + 40;
		drawInfo.colour = glm::vec4(0.0f, 0.0f, 0.0f, glm::smoothstep(0.0f, 1.0f, fadeAlpha));
		drawInfo.pos = VEC3_ZERO;
		drawInfo.scale = glm::vec3(aspectRatio * 1.05f, 1.05f, 1.0f);
		g_Renderer->EnqueueSprite(drawInfo);
	}

	void Player::PlaceOnTerrain(bool bKeepHeight)
	{
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		const glm::vec3 pos = m_Transform.GetWorldPosition();
		glm::vec3 groundPos;
		if (terrainGenerator == nullptr || !terrainGenerator->FindStandingPoint(pos, bKeepHeight, groundPos))
		{
			return;
		}

		// The capsule's center is half its height above its feet
		HoldUntilTerrainCollision(groundPos + glm::vec3(0.0f, m_Height * 0.5f + 0.25f, 0.0f));
	}

	bool Player::IsSupportedBelow(const glm::vec3& pos) const
	{
		// Feet, plus some slack
		const real maxDrop = m_Height * 0.5f + 1.0f;
		const glm::vec3 down(0.0f, -1.0f, 0.0f);

		// Terrain (its collision may not have loaded yet, so this uses the density)
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		glm::vec3 hitPos, hitNormal;
		if (terrainGenerator != nullptr && terrainGenerator->Raycast(pos, down, maxDrop, hitPos, hitNormal))
		{
			return true;
		}

		// Anything else solid (e.g. something built), other than the player
		struct IgnoreObjectCallback : public btCollisionWorld::ClosestRayResultCallback
		{
			IgnoreObjectCallback(const btVector3& from, const btVector3& to, const btCollisionObject* ignored) :
				btCollisionWorld::ClosestRayResultCallback(from, to), ignored(ignored) {}
			btScalar addSingleResult(btCollisionWorld::LocalRayResult& rayResult, bool bNormalInWorldSpace) override
			{
				if (rayResult.m_collisionObject == ignored)
				{
					return 1.0f;
				}
				return btCollisionWorld::ClosestRayResultCallback::addSingleResult(rayResult, bNormalInWorldSpace);
			}
			const btCollisionObject* ignored;
		};
		const btVector3 rayStart = ToBtVec3(pos);
		const btVector3 rayEnd = ToBtVec3(pos + down * maxDrop);
		IgnoreObjectCallback rayCallback(rayStart, rayEnd, m_RigidBody->GetRigidBodyInternal());
		rayCallback.m_collisionFilterMask = (i32)CollisionType::STATIC;
		g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld()->rayTest(rayStart, rayEnd, rayCallback);
		return rayCallback.hasHit();
	}

	void Player::HoldUntilTerrainCollision(const glm::vec3& holdPos)
	{
		m_TerrainHoldPos = holdPos;
		m_Transform.SetWorldPosition(m_TerrainHoldPos);
		btRigidBody* rigidBodyInternal = m_RigidBody->GetRigidBodyInternal();
		rigidBodyInternal->clearForces();
		rigidBodyInternal->setLinearVelocity(btVector3(0, 0, 0));
		m_bAwaitingTerrainCollision = true;
		m_AwaitingTerrainCollisionTime = 0.0f;
		m_InsideTerrainTime = 0.0f;
		m_Traversal->Reset();
	}

	void Player::UpdateTerrainSafety()
	{
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (terrainGenerator == nullptr || m_RidingVehicleID.IsValid() || m_TrackRidingID != InvalidTrackID)
		{
			m_bAwaitingTerrainCollision = false;
			return;
		}

		const glm::vec3 pos = m_Transform.GetWorldPosition();
		if (m_bTerrainPlacementPending && terrainGenerator->IsWorldChangePending())
		{
			// The density still describes the previous world (e.g. just after loading a world), so wait for it
			btRigidBody* rigidBodyInternal = m_RigidBody->GetRigidBodyInternal();
			rigidBodyInternal->clearForces();
			rigidBodyInternal->setLinearVelocity(btVector3(0, 0, 0));
			m_RigidBody->SetWorldPosition(pos);
			return;
		}

		if (m_bTerrainPlacementPending)
		{
			m_bTerrainPlacementPending = false;
			// Restoring a saved position: keep it if the whole capsule is in open air & standing on something (it
			// may be on a building, so the terrain below isn't necessarily the ground)
			bool bKeepExact = false;
			if (m_bTerrainPlacementExact)
			{
				const real axisHalfLength = glm::max(m_Height * 0.5f - m_Radius, 0.0f);
				const bool bClear = terrainGenerator->SampleDensity(pos) > 0.0f &&
					terrainGenerator->SampleDensity(pos - VEC3_UP * axisHalfLength) > 0.0f &&
					terrainGenerator->SampleDensity(pos + VEC3_UP * axisHalfLength) > 0.0f;
				bKeepExact = bClear && IsSupportedBelow(pos);
			}
			if (bKeepExact)
			{
				HoldUntilTerrainCollision(pos);
			}
			else
			{
				PlaceOnTerrain(true);
			}
			m_bTerrainPlacementExact = false;
		}
		else if (!m_bAwaitingTerrainCollision)
		{
			// Stuck inside the terrain (e.g. collision changed under the player, or something was placed on them)
			if (terrainGenerator->SampleDensity(pos) < -1.0f)
			{
				m_InsideTerrainTime += g_DeltaTime;
				if (m_InsideTerrainTime > 0.3f)
				{
					PrintWarn("Player was inside terrain at (%.1f, %.1f, %.1f), moving to the surface\n", pos.x, pos.y, pos.z);
					PlaceOnTerrain(false);
				}
			}
			else
			{
				m_InsideTerrainTime = 0.0f;
			}
		}

		if (m_bAwaitingTerrainCollision)
		{
			// Hold in place until there's something to stand on
			m_AwaitingTerrainCollisionTime += g_DeltaTime;
			m_Transform.SetWorldPosition(m_TerrainHoldPos);
			m_RigidBody->GetRigidBodyInternal()->setLinearVelocity(btVector3(0, 0, 0));

			if (terrainGenerator->IsCollisionLoadedBelow(m_TerrainHoldPos))
			{
				m_bAwaitingTerrainCollision = false;
			}
			else if (m_AwaitingTerrainCollisionTime > 30.0f)
			{
				PrintWarn("Timed out waiting for terrain collision below the player\n");
				m_bAwaitingTerrainCollision = false;
			}
		}
	}

	void Player::DrawTerrainToolHUD() const
	{
		if (m_Woodcraft != nullptr)
		{
			m_Woodcraft->DrawHUD();
		}

		if (!m_bTerrainToolActive)
		{
			return;
		}

		const std::string material = terrain::MaterialName(terrain::FILL_MATERIALS[m_TerrainFillMaterialIndex]);
		const StringID toolTypeID = GetWieldedToolType();
		std::vector<std::string> lines;
		std::string title = ToolName(toolTypeID);
		lines.push_back(ToUpper(title));
		lines.push_back(toolTypeID == ShovelSID ? LOC(TerrainTool_DigLoose) : LOC(TerrainTool_DigRock));
		lines.push_back(LOCF(TerrainTool_Place, material.c_str()));
		lines.push_back(LOC(TerrainTool_Flatten));
		lines.push_back(LOC(TerrainTool_ChangeMaterial));

		BitmapFont* font = g_Renderer->SetFont(SID("editor-02"));
		const real lineHeight = 3.2f * font->GetMetric('W')->height / (real)g_Window->GetSize().y;
		const glm::vec4 titleColour(1.0f, 0.85f, 0.4f, 1.0f);
		const glm::vec4 textColour(0.95f, 0.95f, 0.95f, 1.0f);

		real yOffset = 0.11f; // (above the quick access bar)
		for (i32 i = (i32)lines.size() - 1; i >= 0; --i)
		{
			g_Renderer->DrawStringSS(lines[i], i == 0 ? titleColour : textColour, AnchorPoint::BOTTOM_LEFT, glm::vec2(0.03f, yOffset), 1.5f, 0.6f);
			yOffset += lineHeight;
		}
	}

	void Player::UpdateTerrainToolTest()
	{
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (!m_bPossessed || terrainGenerator == nullptr || IsAwaitingTerrainCollision() || !terrainGenerator->IsLoadComplete() ||
			g_CameraManager->CurrentCamera() == nullptr)
		{
			m_TerrainTestTime = 0.0f;
			return;
		}
		m_TerrainTestTime += g_UnpausedDeltaTime;
		const sec t = m_TerrainTestTime;

		auto Screenshot = [](const char* name)
		{
			const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
			Platform::CreateDirectoryRecursive(directory);
			g_Renderer->RequestScreenshot(directory + "terrain_" + name + ".png");
			Print("Terrain tool test: screenshot %s (brush at (%.2f, %.2f, %.2f), normal (%.2f, %.2f, %.2f))\n", name,
				s_LastBrushCenter.x, s_LastBrushCenter.y, s_LastBrushCenter.z, s_LastBrushNormal.x, s_LastBrushNormal.y, s_LastBrushNormal.z);
		};
		auto Next = [this]() { ++m_TerrainTestStep; m_TerrainTestTime = 0.0f; };
		// Whether the test time just passed time
		auto At = [t](sec time) { return t >= time && t - g_UnpausedDeltaTime < time; };
		auto SetYaw = [this](real yaw) { m_Transform.SetWorldRotation(glm::angleAxis(yaw, VEC3_UP)); };
		auto Hold = [this](const char* itemName)
		{
			const PrefabID itemID = g_ResourceManager->GetPrefabID(itemName);
			for (i32 attempt = 0; attempt < 2; ++attempt)
			{
				for (i32 i = 0; i < (i32)QUICK_ACCESS_ITEM_COUNT; ++i)
				{
					if (m_QuickAccessInventory[i].prefabID == itemID && m_QuickAccessInventory[i].count > 0)
					{
						SetSelectedQuickAccessItemSlot(i);
						return;
					}
				}
				AddToInventory(itemID, 1);
			}
			PrintError("Terrain tool test: couldn't hold %s\n", itemName);
		};
		auto Stats = [this, terrainGenerator](const char* what)
		{
			Print("Terrain tool test: %s: %u strokes, %u edits applied, %.2fs waiting (%u frames blocked by the player, %u re-meshing), %u saved edits\n", what,
				m_TestTerrainStrokeCount, m_TestTerrainEditCount, m_TestTerrainWaitTime, s_BlockedFrames, s_ApplyingFrames, terrainGenerator->GetEditCount());
			s_BlockedFrames = 0;
			s_ApplyingFrames = 0;
			const glm::vec3 pos = m_Transform.GetWorldPosition();
			Print("Terrain tool test:   player at (%.2f, %.2f, %.2f), flatten level %.2f\n", pos.x, pos.y, pos.z, m_FlattenLevel);
			m_TestTerrainStrokeCount = 0;
			m_TestTerrainEditCount = 0;
			m_TestTerrainWaitTime = 0.0f;
		};
		const PrefabID woodID = woodcraft::GetWoodPrefabID();

		if (m_TerrainTestMode == 2)
		{
			// Reloaded: the edits made by the first run should be there
			if (t > 6.0f)
			{
				Print("Terrain tool test: reloaded with %u saved edits\n", terrainGenerator->GetEditCount());
				SetPitch(glm::radians(25.0f));
				Screenshot("reload");
				m_TerrainTestMode = 3;
				m_TerrainTestTime = 0.0f;
			}
			return;
		}
		if (m_TerrainTestMode == 3)
		{
			if (t > 1.0f)
			{
				g_EngineInstance->Stop();
				m_TerrainTestStep = -1;
			}
			return;
		}

		switch (m_TerrainTestStep)
		{
		case 0:
		{
			m_TestTerrainAction = 0;
			// First moves to flat, dry ground (searching outwards in rings)
			static bool bMoved = false;
			if (!bMoved && t > 1.0f)
			{
				bMoved = true;
				const glm::vec3 start = m_Transform.GetWorldPosition();
				for (real ringRadius = 0.0f; ringRadius < 400.0f; ringRadius += 20.0f)
				{
					bool bFound = false;
					for (i32 i = 0; i < 12 && !bFound; ++i)
					{
						const real angle = TWO_PI * (real)i / 12.0f;
						const real x = start.x + std::cos(angle) * ringRadius;
						const real z = start.z + std::sin(angle) * ringRadius;
						const terrain::ColumnSample column = terrainGenerator->SampleColumn(x, z);
						const bool bDry = column.lakeID == 0 || column.lakeT > 1.5f;
						real minHeight = column.height;
						real maxHeight = column.height;
						for (const glm::vec2& offset : { glm::vec2(8, 0), glm::vec2(-8, 0), glm::vec2(0, 8), glm::vec2(0, -8) })
						{
							const real h = terrainGenerator->SampleColumn(x + offset.x, z + offset.y).height;
							minHeight = glm::min(minHeight, h);
							maxHeight = glm::max(maxHeight, h);
						}
						// (mode 4: a slope, to check the brush gizmo against buried parts)
						const bool bSlope = m_TerrainTestMode == 4;
						if (bDry && (bSlope ? (maxHeight - minHeight > 5.0f && maxHeight - minHeight < 14.0f) : maxHeight - minHeight < 1.0f))
						{
							TeleportOnTerrain(glm::vec3(x, column.height + 2.0f, z));
							bFound = true;
						}
					}
					if (bFound)
					{
						break;
					}
				}
				m_TerrainTestTime = 0.0f;
				break;
			}
			if (bMoved && t > 3.0f)
			{
				const glm::vec3 pos = m_Transform.GetWorldPosition();
				Print("Terrain tool test: start at (%.1f, %.1f, %.1f) with %u saved edits\n", pos.x, pos.y, pos.z, terrainGenerator->GetEditCount());
				m_TerrainTestYaw = 0.0f;
				SetYaw(m_TerrainTestYaw);
				Hold("shovel");
				SetPitch(glm::radians(40.0f));
				Next();
			}
		} break;
		case 1:
			if (m_TerrainTestMode == 4)
			{
				// Looks at the slope from four sides (the brush is partly hidden by the terrain from some)
				const i32 view = (i32)(t / 2.0f);
				SetYaw(glm::radians(90.0f * (real)view));
				SetPitch(glm::radians(view % 2 == 0 ? 8.0f : 30.0f));
				if (At(1.5f + 2.0f * (real)view) || (view > 0 && At(2.0f * (real)view + 1.0f)))
				{
					char name[32];
					snprintf(name, sizeof(name), "slope_view%d", view);
					Screenshot(name);
				}
				if (t > 8.5f)
				{
					g_EngineInstance->Stop();
					m_TerrainTestStep = -1;
				}
				break;
			}
			// The brush, before using it
			if (At(1.5f))
			{
				Screenshot("1_brush");
			}
			if (t > 1.6f)
			{
				Stats("before");
				Next();
			}
			break;
		case 2:
		case 4:
		case 6:
		case 8:
		{
			// Dig, fill & flatten with the shovel, then dig with the pickaxe
			const i32 index = (m_TerrainTestStep - 2) / 2;
			const char* names[] = { "2_shovel_digging", "4_shovel_filling", "6_flattening", "8_pickaxe_digging" };
			const i32 actions[] = { 1, 2, 3, 1 };
			m_TestTerrainAction = actions[index];
			if (index == 2)
			{
				// Sweeping across a strip
				SetYaw(m_TerrainTestYaw + std::sin(t * 1.4f) * 0.5f);
			}
			if (At(1.2f))
			{
				Screenshot(names[index]);
			}
			if (t > (index == 2 ? 4.5f : 2.5f))
			{
				m_TestTerrainAction = 0;
				Stats(names[index]);
				Next();
			}
		} break;
		case 3:
		case 5:
		case 7:
		case 9:
		{
			const i32 index = (m_TerrainTestStep - 3) / 2;
			const char* names[] = { "3_shovel_dug", "5_shovel_filled", "7_flattened", "9_pickaxe_dug" };
			if (At(2.5f))
			{
				Screenshot(names[index]);
			}
			if (t > 2.6f)
			{
				// On to fresh ground
				m_TerrainTestYaw += glm::radians(90.0f);
				SetYaw(m_TerrainTestYaw);
				// (filling further away, so the player isn't standing in the brush)
				SetPitch(glm::radians(index == 0 ? 22.0f : 40.0f));
				if (index == 2)
				{
					Hold("pickaxe");
				}
				Next();
			}
		} break;
		case 10:
			// Campfire placement preview (on fresh ground)
			SetYaw(m_TerrainTestYaw + glm::radians(45.0f));
			if (m_Woodcraft != nullptr && woodID.IsValid())
			{
				if (m_Woodcraft->CountItems(woodID) < Campfire::WOOD_COST)
				{
					AddToInventory(woodID, Campfire::WOOD_COST);
				}
				m_Woodcraft->SetPlacingCampfire(true);
			}
			// (far enough to be clear of the edits made so far)
			SetPitch(glm::radians(20.0f));
			if (At(1.5f))
			{
				Print("Terrain tool test: campfire placement valid: %d\n", m_Woodcraft != nullptr && m_Woodcraft->IsCampfirePlacementValid() ? 1 : 0);
				BaseCamera* camera = g_CameraManager->CurrentCamera();
				glm::vec3 hitPos, hitNormal;
				if (terrainGenerator->Raycast(camera->position, camera->forward, 8.0f, hitPos, hitNormal))
				{
					const terrain::ColumnSample column = terrainGenerator->SampleColumn(hitPos.x, hitPos.z);
					Print("Terrain tool test:   aiming at (%.2f, %.2f, %.2f), normal y %.2f, smooth normal y %.2f, lake %u (t %.2f, level %.2f)\n",
						hitPos.x, hitPos.y, hitPos.z, hitNormal.y, terrainGenerator->SampleSmoothNormal(hitPos, 0.8f).y, column.lakeID, column.lakeT, column.waterLevel);
				}
				else
				{
					Print("Terrain tool test:   not aiming at terrain within 8 m\n");
				}
				Screenshot("10_campfire_valid");
			}
			if (t > 1.6f && m_Woodcraft != nullptr)
			{
				// Build one there, so the next preview (in the same spot) is blocked by it
				BaseCamera* camera = g_CameraManager->CurrentCamera();
				glm::vec3 hitPos, hitNormal;
				if (terrainGenerator->Raycast(camera->position, camera->forward, 8.0f, hitPos, hitNormal))
				{
					m_Woodcraft->BuildCampfire(hitPos, m_TerrainTestYaw);
				}
				Next();
			}
			break;
		case 11:
			if (m_Woodcraft != nullptr)
			{
				m_Woodcraft->SetPlacingCampfire(true);
			}
			if (At(1.5f))
			{
				Print("Terrain tool test: campfire placement next to another valid: %d\n", m_Woodcraft != nullptr && m_Woodcraft->IsCampfirePlacementValid() ? 1 : 0);
				Screenshot("11_campfire_invalid");
			}
			if (t > 1.6f)
			{
				if (m_Woodcraft != nullptr)
				{
					m_Woodcraft->SetPlacingCampfire(false);
				}
				Next();
			}
			break;
		case 12:
			if (t > 0.5f)
			{
				// The whole area
				SetYaw(m_TerrainTestYaw + glm::radians(45.0f));
				SetPitch(glm::radians(25.0f));
				Next();
			}
			break;
		case 13:
			if (At(1.0f))
			{
				Screenshot("12_overview");
			}
			if (t > 1.2f)
			{
				Print("Terrain tool test: done, %u saved edits\n", terrainGenerator->GetEditCount());
				g_Game->SaveWorld();
				Next();
			}
			break;
		case 14:
			if (t > 1.0f)
			{
				g_EngineInstance->Stop();
				m_TerrainTestStep = -1;
			}
			break;
		default:
			break;
		}
	}

	StringID Player::GetWieldedToolType() const
	{
		const GameObjectStack& stack = m_QuickAccessInventory[m_SelectedQuickAccessItemSlot];
		if (stack.count <= 0 || !stack.prefabID.IsValid())
		{
			return InvalidStringID;
		}

		GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(stack.prefabID);
		return prefabTemplate != nullptr && IsHandToolType(prefabTemplate->GetTypeID()) ? prefabTemplate->GetTypeID() : InvalidStringID;
	}

	bool Player::IsWieldingPickAxe() const
	{
		return GetWieldedToolType() == PickAxeSID;
	}

	bool Player::IsWieldingAxe() const
	{
		return GetWieldedToolType() == AxeSID;
	}

	void Player::SwingTool(real duration)
	{
		if (m_ToolSwingTimer <= 0.0f && duration > 0.0f)
		{
			m_ToolSwingDuration = duration;
			m_ToolSwingTimer = duration;
		}
	}
} // namespace flex
