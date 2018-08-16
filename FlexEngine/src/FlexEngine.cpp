#include "stdafx.hpp"

#include "FlexEngine.hpp"

#include <sstream>
#include <stdlib.h> // For srand, rand
#include <time.h> // For time

#pragma warning(push, 0)
#include <imgui.h>
#include <imgui_internal.h>

#include <BulletDynamics/Dynamics/btDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <LinearMath/btIDebugDraw.h>
#pragma warning(pop)

#include "Audio/AudioManager.hpp"
#include "Cameras/CameraManager.hpp"
#include "Cameras/DebugCamera.hpp"
#include "Cameras/OverheadCamera.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "JSONTypes.hpp"
#include "JSONParser.hpp"
#include "Physics/PhysicsManager.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Profiler.hpp"
#include "Player.hpp"
#include "PlayerController.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "Time.hpp"
#include "Window/Monitor.hpp"
#include "Window/GLFWWindowWrapper.hpp"

namespace flex
{
	const u32 FlexEngine::EngineVersionMajor = 0;
	const u32 FlexEngine::EngineVersionMinor = 7;
	const u32 FlexEngine::EngineVersionPatch = 0;

	std::string FlexEngine::s_CurrentWorkingDirectory;
	std::vector<AudioSourceID> FlexEngine::s_AudioSourceIDs;


	// Globals declared in stdafx.hpp
	class Window* g_Window = nullptr;
	class CameraManager* g_CameraManager = nullptr;
	class InputManager* g_InputManager = nullptr;
	class Renderer* g_Renderer = nullptr;
	class FlexEngine* g_EngineInstance = nullptr;
	class SceneManager* g_SceneManager = nullptr;
	struct Monitor* g_Monitor = nullptr;
	class PhysicsManager* g_PhysicsManager = nullptr;

	sec g_SecElapsedSinceProgramStart = 0;
	sec g_DeltaTime = 0;


	FlexEngine::FlexEngine()
	{
		// TODO: Add custom seeding for different random systems
		std::srand((u32)time(NULL));

		RetrieveCurrentWorkingDirectory();

		std::string configDirAbs = RelativePathToAbsolute(RESOURCE_LOCATION + std::string("config/"));
		m_CommonSettingsFileName = "common.ini";
		m_CommonSettingsAbsFilePath = configDirAbs + m_CommonSettingsFileName;

		CreateDirectoryRecursive(configDirAbs);

		RendererID preferredInitialRenderer = RendererID::GL;

		m_RendererIndex = RendererID::_LAST_ELEMENT;
		m_RendererCount = 0;

#if COMPILE_OPEN_GL
		++m_RendererCount;
		if (m_RendererIndex == RendererID::_LAST_ELEMENT || preferredInitialRenderer == RendererID::GL)
		{
			m_RendererIndex = RendererID::GL;
		}
#endif
#if COMPILE_VULKAN
		++m_RendererCount;
		if (m_RendererIndex == RendererID::_LAST_ELEMENT || preferredInitialRenderer == RendererID::VULKAN)
		{
			m_RendererIndex = RendererID::VULKAN;
		}
#endif

		m_RendererName = RenderIDToString(m_RendererIndex);

		GetConsoleHandle();

		assert(m_RendererCount > 0); // At least one renderer must be enabled! (see stdafx.h)
		Print("%i renderer%s %s, Current renderer: %s\n", 
			  m_RendererCount, (m_RendererCount > 1 ? "s" : ""), "enabled", m_RendererName.c_str());

		DeselectCurrentlySelectedObjects();
	}

	FlexEngine::~FlexEngine()
	{
		Destroy();
	}

	void FlexEngine::Initialize()
	{
		const char* profileBlockStr = "Engine initialize";
		PROFILE_BEGIN(profileBlockStr);

		g_EngineInstance = this;

		AudioManager::Initialize();

		CreateWindowAndRenderer();

		g_InputManager = new InputManager();

		g_CameraManager = new CameraManager();

		DebugCamera* debugCamera = new DebugCamera();
		debugCamera->SetPosition(glm::vec3(20.0f, 8.0f, -16.0f));
		debugCamera->SetYaw(glm::radians(130.0f));
		debugCamera->SetPitch(glm::radians(-10.0f));
		g_CameraManager->AddCamera(debugCamera, true);

		OverheadCamera* overheadCamera = new OverheadCamera();
		g_CameraManager->AddCamera(overheadCamera, false);

		InitializeWindowAndRenderer();
		g_InputManager->Initialize();

		g_PhysicsManager = new PhysicsManager();
		g_PhysicsManager->Initialize();

		// Transform gizmo materials
		{
			MaterialCreateInfo matCreateInfo = {};
			matCreateInfo.name = "Transform";
			matCreateInfo.shaderName = "color";
			matCreateInfo.constAlbedo = glm::vec3(1.0f);
			matCreateInfo.engineMaterial = true;
			m_TransformGizmoMatXID = g_Renderer->InitializeMaterial(&matCreateInfo);
			m_TransformGizmoMatYID = g_Renderer->InitializeMaterial(&matCreateInfo);
			m_TransformGizmoMatZID = g_Renderer->InitializeMaterial(&matCreateInfo);
		}

		BaseScene::ParseFoundMeshFiles();
		BaseScene::ParseFoundMaterialFiles();
		BaseScene::ParseFoundPrefabFiles();

		g_SceneManager = new SceneManager();
		g_SceneManager->AddFoundScenes();

		LoadCommonSettingsFromDisk();

		g_SceneManager->InitializeCurrentScene();		
		g_Renderer->PostInitialize();
		g_SceneManager->PostInitializeCurrentScene();

		SetupImGuiStyles();

		g_CameraManager->Initialize();

		if (s_AudioSourceIDs.empty())
		{
			s_AudioSourceIDs.push_back(AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/dud_dud_dud_dud.wav"));
			s_AudioSourceIDs.push_back(AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/drmapan.wav"));
			s_AudioSourceIDs.push_back(AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/blip.wav"));
		}

		PROFILE_END(profileBlockStr);
		Profiler::PrintBlockDuration(profileBlockStr);
	}

	AudioSourceID FlexEngine::GetAudioSourceID(SoundEffect effect)
	{
		assert((i32)effect >= 0);
		assert((i32)effect < (i32)SoundEffect::LAST_ELEMENT);

		return s_AudioSourceIDs[(i32)effect];
	}

	void FlexEngine::Destroy()
	{
		// TODO: Time engine destruction using non-glfw timer

		SaveCommonSettingsToDisk(false);
		g_Window->SaveToConfig();

		DeselectCurrentlySelectedObjects();

		if (m_TransformGizmo)
		{
			m_TransformGizmo->Destroy();
			SafeDelete(m_TransformGizmo);
		}

		if (g_SceneManager)
		{
			g_SceneManager->DestroyAllScenes();
			SafeDelete(g_SceneManager);
		}

		SafeDelete(g_InputManager);

		if (g_PhysicsManager)
		{
			g_PhysicsManager->Destroy();
			SafeDelete(g_PhysicsManager);
		}

		SafeDelete(g_CameraManager);

		DestroyWindowAndRenderer();
		
		SafeDelete(g_Monitor);

		MeshComponent::DestroyAllLoadedMeshes();

		AudioManager::Destroy();

		// Reset console color to default
		Print("\n");
	}

	void FlexEngine::CreateWindowAndRenderer()
	{
		assert(g_Window == nullptr);
		assert(g_Renderer == nullptr);

		const std::string titleString = "Flex Engine v" + EngineVersionString();

		if (g_Window == nullptr)
		{
			g_Window = new GLFWWindowWrapper(titleString);
		}
		if (g_Window == nullptr)
		{
			PrintError("Failed to create a window! Are any compile flags set in stdafx.hpp?\n");
			return;
		}

		g_Window->Initialize();
		g_Monitor = new Monitor();
		g_Window->RetrieveMonitorInfo();

		float desiredAspectRatio = 16.0f / 9.0f;
		float desiredWindowSizeScreenPercetange = 0.85f;

		// What kind of monitor has different scales along each axis?
		assert(g_Monitor->contentScaleX == g_Monitor->contentScaleY);

		i32 newWindowSizeY = i32(g_Monitor->height * desiredWindowSizeScreenPercetange * g_Monitor->contentScaleY);
		i32 newWindowSizeX = i32(newWindowSizeY * desiredAspectRatio);

		i32 newWindowPosX = i32(newWindowSizeX * 0.1f);
		i32 newWindowPosY = i32(newWindowSizeY * 0.1f);

		g_Window->Create(glm::vec2i(newWindowSizeX, newWindowSizeY), glm::vec2i(newWindowPosX, newWindowPosY));


#if COMPILE_VULKAN
		if (m_RendererIndex == RendererID::VULKAN)
		{
			g_Renderer = new vk::VulkanRenderer();
		}
#endif
#if COMPILE_OPEN_GL
		if (m_RendererIndex == RendererID::GL)
		{
			g_Renderer = new gl::GLRenderer();
		}
#endif
		if (g_Renderer == nullptr)
		{
			PrintError("Failed to create a renderer!\n");
			return;
		}
	}

	void FlexEngine::InitializeWindowAndRenderer()
	{
		g_Window->SetUpdateWindowTitleFrequency(0.5f);
		g_Window->PostInitialize();

		g_Renderer->Initialize();
	}

	void FlexEngine::DestroyWindowAndRenderer()
	{
		if (g_Renderer)
		{
			g_Renderer->Destroy();
			SafeDelete(g_Renderer);
		}

		if (g_Window)
		{
			g_Window->Destroy();
			SafeDelete(g_Window);
		}
	}

	std::string FlexEngine::RenderIDToString(RendererID rendererID) const
	{
		switch (rendererID)
		{
		case RendererID::VULKAN: return "Vulkan";
		case RendererID::GL: return "OpenGL";
		case RendererID::_LAST_ELEMENT:  return "Invalid renderer ID";
		default:
			return "Unknown";
		}
	}

	void FlexEngine::PreSceneChange()
	{
		if (m_TransformGizmo)
		{
			m_TransformGizmo->Destroy();
			SafeDelete(m_TransformGizmo);
		}

		DeselectCurrentlySelectedObjects();
	}

	void FlexEngine::OnSceneChanged()
	{
		SaveCommonSettingsToDisk(false);

		RenderObjectCreateInfo gizmoAxisCreateInfo = {};
		gizmoAxisCreateInfo.depthTestReadFunc = DepthTestFunc::ALWAYS;
		gizmoAxisCreateInfo.depthWriteEnable = true;
		gizmoAxisCreateInfo.editorObject = true;

		real cylinderRadius = 0.2f;
		real cylinderHeight = 1.6f;

		u32 rbFlags = ((u32)PhysicsFlag::TRIGGER) | ((u32)PhysicsFlag::UNSELECTABLE);
		u32 rbGroup = (u32)CollisionType::EDITOR_OBJECT;
		u32 rbMask = 1;

		// X Axis
		GameObject* transformXAxis = new GameObject("Transform gizmo x axis", GameObjectType::NONE);
		transformXAxis->AddTag("transform gizmo");
		transformXAxis->SetVisibleInSceneExplorer(false);
		MeshComponent* xAxisMesh = transformXAxis->SetMeshComponent(new MeshComponent(m_TransformGizmoMatXID, transformXAxis));

		btCylinderShape* xAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
		transformXAxis->SetCollisionShape(xAxisShape);

		RigidBody* gizmoXAxisRB = transformXAxis->SetRigidBody(new RigidBody(rbGroup, rbMask));
		gizmoXAxisRB->SetMass(0.0f);
		gizmoXAxisRB->SetKinematic(true);
		gizmoXAxisRB->SetPhysicsFlags(rbFlags);

		xAxisMesh->SetRequiredAttributesFromMaterialID(m_TransformGizmoMatXID);
		xAxisMesh->LoadFromFile(RESOURCE_LOCATION + "meshes/transform-gizmo-x-axis.fbx", nullptr, &gizmoAxisCreateInfo);

		// Y Axis
		GameObject* transformYAxis = new GameObject("Transform gizmo y axis", GameObjectType::NONE);
		transformYAxis->AddTag("transform gizmo");
		transformYAxis->SetVisibleInSceneExplorer(false);
		MeshComponent* yAxisMesh = transformYAxis->SetMeshComponent(new MeshComponent(m_TransformGizmoMatYID, transformYAxis));

		btCylinderShape* yAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
		transformYAxis->SetCollisionShape(yAxisShape);

		RigidBody* gizmoYAxisRB = transformYAxis->SetRigidBody(new RigidBody(rbGroup, rbMask));
		gizmoYAxisRB->SetMass(0.0f);
		gizmoYAxisRB->SetKinematic(true);
		gizmoYAxisRB->SetPhysicsFlags(rbFlags);

		yAxisMesh->SetRequiredAttributesFromMaterialID(m_TransformGizmoMatXID);
		yAxisMesh->LoadFromFile(RESOURCE_LOCATION + "meshes/transform-gizmo-y-axis.fbx", nullptr, &gizmoAxisCreateInfo);

		// Z Axis
		GameObject* transformZAxis = new GameObject("Transform gizmo z axis", GameObjectType::NONE);
		transformZAxis->AddTag("transform gizmo");
		transformZAxis->SetVisibleInSceneExplorer(false);

		MeshComponent* zAxisMesh = transformZAxis->SetMeshComponent(new MeshComponent(m_TransformGizmoMatZID, transformZAxis));

		btCylinderShape* zAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
		transformZAxis->SetCollisionShape(zAxisShape);

		RigidBody* gizmoZAxisRB = transformZAxis->SetRigidBody(new RigidBody(rbGroup, rbMask));
		gizmoZAxisRB->SetMass(0.0f);
		gizmoZAxisRB->SetKinematic(true);
		gizmoZAxisRB->SetPhysicsFlags(rbFlags);

		zAxisMesh->SetRequiredAttributesFromMaterialID(m_TransformGizmoMatXID);
		zAxisMesh->LoadFromFile(RESOURCE_LOCATION + "meshes/transform-gizmo-z-axis.fbx", nullptr, &gizmoAxisCreateInfo);


		m_TransformGizmo = new GameObject("Transform gizmo", GameObjectType::NONE);
		m_TransformGizmo->SetVisibleInSceneExplorer(false);

		m_TransformGizmo->AddChild(transformXAxis);
		m_TransformGizmo->AddChild(transformYAxis);
		m_TransformGizmo->AddChild(transformZAxis);
		m_TransformGizmo->Initialize();


		gizmoXAxisRB->SetLocalRotation(glm::quat(glm::vec3(0, 0, PI / 2.0f)));
		gizmoXAxisRB->SetLocalPosition(glm::vec3(cylinderHeight, 0, 0));

		gizmoYAxisRB->SetLocalPosition(glm::vec3(0, cylinderHeight, 0));

		gizmoZAxisRB->SetLocalRotation(glm::quat(glm::vec3(PI / 2.0f, 0, 0)));
		gizmoZAxisRB->SetLocalPosition(glm::vec3(0, 0, cylinderHeight));

		m_TransformGizmo->PostInitialize();
	}

	bool FlexEngine::IsRenderingImGui() const
	{
		return m_bRenderImGui;
	}

	bool FlexEngine::IsRenderingEditorObjects() const
	{
		return m_bRenderEditorObjects;
	}

	void FlexEngine::CycleRenderer()
	{
		// TODO? ??
		//g_Renderer->InvalidateFontObjects();

		DeselectCurrentlySelectedObjects();
		PreSceneChange();
		g_SceneManager->DestroyAllScenes();
		DestroyWindowAndRenderer();

		while (true)
		{
			m_RendererIndex = RendererID(((i32)m_RendererIndex + 1) % (i32)RendererID::_LAST_ELEMENT);

#if COMPILE_VULKAN
			if (m_RendererIndex == RendererID::VULKAN)
			{
				break;
			}
#endif
#if COMPILE_OPEN_GL
			if (m_RendererIndex == RendererID::GL)
			{
				break;
			}
#endif
		}
		m_RendererName = RenderIDToString(m_RendererIndex);
		Print("Current renderer: %s\n", m_RendererName);

		CreateWindowAndRenderer();
		InitializeWindowAndRenderer();

		SetupImGuiStyles();

		g_SceneManager->AddFoundScenes();
		
		LoadCommonSettingsFromDisk();

		g_SceneManager->InitializeCurrentScene();
		g_Renderer->PostInitialize();
		g_SceneManager->PostInitializeCurrentScene();
	}

	void FlexEngine::UpdateAndRender()
	{
		m_bRunning = true;
		sec frameStartTime = Time::CurrentSeconds();
		while (m_bRunning)
		{
			sec frameEndTime = Time::CurrentSeconds();
			sec dt = frameEndTime - frameStartTime;
			frameStartTime = frameEndTime;

			dt = glm::clamp(dt, m_MinDT, m_MaxDT);

			g_DeltaTime = dt;
			g_SecElapsedSinceProgramStart = frameEndTime;

			Profiler::StartFrame();

			PROFILE_BEGIN("Update");
			g_Window->PollEvents();

			const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
			if (frameBufferSize.x == 0 || frameBufferSize.y == 0)
			{
				g_InputManager->ClearAllInputs();
			}

			// Disabled for now since we only support Open GL
#if 0
			if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_T))
			{
				g_InputManager->Update();
				g_InputManager->PostUpdate();
				g_InputManager->ClearAllInputs();
				CycleRenderer();
				continue;
			}
#endif

			// This variable should be the one changed during this frame so we always
			// end frames that we start, next frame we will begin using the new value
			bool renderImGuiNextFrame = m_bRenderImGui;

			// Call as early as possible in the frame
			// Starts new ImGui frame and clears debug draw lines
			g_Renderer->NewFrame();
			 
			if (m_bRenderImGui)
			{
				PROFILE_BEGIN("DrawImGuiObjects");
				DrawImGuiObjects();
				PROFILE_END("DrawImGuiObjects");
			}

			// Hovered object
			{
				GameObject* hoveredOverGameObject = nullptr;

				glm::vec2 mousePos = g_InputManager->GetMousePosition();
				PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();
				btVector3 cameraPos = Vec3ToBtVec3(g_CameraManager->CurrentCamera()->GetPosition());

				real maxDist = 1000.0f;

				btVector3 rayStart(cameraPos);
				btVector3 rayDir = physicsWorld->GenerateDirectionRayFromScreenPos((i32)mousePos.x, (i32)mousePos.y);
				btVector3 rayEnd = rayStart + rayDir * maxDist;

				btRigidBody* pickedBody = physicsWorld->PickBody(rayStart, rayEnd);

				Material& xMat = g_Renderer->GetMaterial(m_TransformGizmoMatXID);
				Material& yMat = g_Renderer->GetMaterial(m_TransformGizmoMatYID);
				Material& zMat = g_Renderer->GetMaterial(m_TransformGizmoMatZID);
				glm::vec4 white(1.0f);
				if (m_DraggingAxisIndex != 0)
				{
					xMat.colorMultiplier = white;
				}
				if (m_DraggingAxisIndex != 1)
				{
					yMat.colorMultiplier = white;
				}
				if (m_DraggingAxisIndex != 2)
				{
					zMat.colorMultiplier = white;
				}

				std::vector<GameObject*> transformAxes = m_TransformGizmo->GetChildren();

				real gizmoHoverMultiplier = 3.0f;
				real gizmoSelectedMultiplier = 8.0f;

				glm::vec4 selectedColor(gizmoSelectedMultiplier, gizmoSelectedMultiplier, gizmoSelectedMultiplier, 1.0f);
				glm::vec4 hoverColor(gizmoHoverMultiplier, gizmoHoverMultiplier, gizmoHoverMultiplier, 1.0f);

				// TODO: Bring keybindings out to external file (or at least variables)
				InputManager::MouseButton dragButton = InputManager::MouseButton::LEFT;
				bool bMouseDown = g_InputManager->IsMouseButtonDown(dragButton);
				bool bMousePressed = g_InputManager->IsMouseButtonPressed(dragButton);
				bool bMouseReleased = g_InputManager->IsMouseButtonReleased(dragButton);

				if (!m_bDraggingGizmo && pickedBody)
				{
					hoveredOverGameObject = (GameObject*)(pickedBody->getUserPointer());

					if (hoveredOverGameObject)
					{
						btRigidBody* pickedTransform = physicsWorld->PickBody(rayStart, rayEnd);
						if (pickedTransform)
						{
							GameObject* pickedTransformGameObject = (GameObject*)(pickedTransform->getUserPointer());

							if (pickedTransformGameObject)
							{

								if (hoveredOverGameObject == transformAxes[0]) // X Axis
								{
									if (bMousePressed)
									{
										m_DraggingAxisIndex = 0;
										xMat.colorMultiplier = selectedColor;
									}
									else
									{
										xMat.colorMultiplier = hoverColor;
									}
								}
								else if (hoveredOverGameObject == transformAxes[1]) // Y Axis
								{
									if (bMousePressed)
									{
										m_DraggingAxisIndex = 1;
										yMat.colorMultiplier = selectedColor;
									}
									else
									{
										yMat.colorMultiplier = hoverColor;
									}
								}
								else if (hoveredOverGameObject == transformAxes[2]) // Z Axis
								{
									if (bMousePressed)
									{
										m_DraggingAxisIndex = 2;
										zMat.colorMultiplier = selectedColor;
									}
									else
									{
										zMat.colorMultiplier = hoverColor;
									}
								}
							}
						}
					}
				}

				if (bMouseDown || bMouseReleased)
				{
					static glm::vec2 pDragDist(0.0f);
					glm::vec2 dragDist = g_InputManager->GetMouseDragDistance(dragButton);
					glm::vec2 dDragDist = dragDist - pDragDist;
					real maxMoveDist = 1.0f;

					if (bMouseReleased)
					{
						if (m_bDraggingGizmo)
						{
							if (!m_CurrentlySelectedObjects.empty())
							{
								CalculateSelectedObjectsCenter();
								m_SelectedObjectDragStartPos = m_SelectedObjectsCenterPos;
							}
							m_bDraggingGizmo = false;
							m_DraggingAxisIndex = -1;
						}
						else
						{
							// If the mouse hasn't moved then the user clicked on something - select it
							if (glm::length(dragDist) < maxMoveDist)
							{
								if (hoveredOverGameObject)
								{
									RigidBody* rb = hoveredOverGameObject->GetRigidBody();
									if (!(rb->GetPhysicsFlags() & (u32)PhysicsFlag::UNSELECTABLE))
									{
										if (g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_LEFT_SHIFT))
										{
											ToggleSelectedObject(hoveredOverGameObject);
										}
										else
										{
											DeselectCurrentlySelectedObjects();
											m_CurrentlySelectedObjects.push_back(hoveredOverGameObject);
										}
										g_InputManager->ClearMouseInput();
									}
									else
									{
										DeselectCurrentlySelectedObjects();
									}
								}
								else
								{
									DeselectCurrentlySelectedObjects();
								}
							}
						}
					}

					// Handle dragging transform gizmo
					if (!m_CurrentlySelectedObjects.empty())
					{
						glm::vec3 dPos(0.0f);
						Transform* selectedObjectTransform = m_CurrentlySelectedObjects[m_CurrentlySelectedObjects.size() - 1]->GetTransform();
						glm::quat selectedObjectRotation = selectedObjectTransform->GetLocalRotation();
						glm::vec3 selectedObjectScale = selectedObjectTransform->GetLocalScale();
						real scale = 0.01f;
						if (m_DraggingAxisIndex == 0) // X Axis
						{
							if (bMousePressed)
							{
								m_bDraggingGizmo = true;
								m_SelectedObjectDragStartPos = m_SelectedObjectsCenterPos;
							}
							else if (bMouseDown)
							{
								glm::vec3 right = selectedObjectRotation * glm::vec3(1, 0, 0);
								glm::vec3 deltaPos = (dDragDist.x * scale * selectedObjectScale.x * right);
								dPos = deltaPos;
							}
						}
						else if (m_DraggingAxisIndex == 1) // Y Axis
						{
							if (bMousePressed)
							{
								m_bDraggingGizmo = true;
								m_SelectedObjectDragStartPos = m_SelectedObjectsCenterPos;
							}
							else if (bMouseDown)
							{
								glm::vec3 up = selectedObjectRotation * glm::vec3(0, 1, 0);
								glm::vec3 deltaPos = (-dDragDist.y * selectedObjectScale.y * scale * up);
								dPos = deltaPos;
							}
						}
						else if (m_DraggingAxisIndex == 2) // Z Axis
						{
							if (bMousePressed)
							{
								m_bDraggingGizmo = true;
								m_SelectedObjectDragStartPos = m_SelectedObjectsCenterPos;
							}
							else if (bMouseDown)
							{
								glm::vec3 forward = selectedObjectRotation * glm::vec3(0, 0, 1);
								glm::vec3 deltaPos = (-dDragDist.x * selectedObjectScale.z * scale * forward);
								dPos = deltaPos;
							}
						}

						if (m_bDraggingGizmo)
						{
							g_Renderer->GetDebugDrawer()->drawLine(
								Vec3ToBtVec3(m_SelectedObjectDragStartPos),
								Vec3ToBtVec3(selectedObjectTransform->GetLocalPosition()),
								(m_DraggingAxisIndex == 0 ? btVector3(1.0f, 0.0f, 0.0f) : m_DraggingAxisIndex == 1 ? btVector3(0.0f, 1.0f, 0.0f) : btVector3(0.0f, 0.0f, 1.0f)));

							for (GameObject* gameObject : m_CurrentlySelectedObjects)
							{
								gameObject->GetTransform()->Translate(dPos);
							}

							CalculateSelectedObjectsCenter();
						}
					}

					pDragDist = dragDist;
				}
			}

			if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_G))
			{
				m_bRenderEditorObjects = !m_bRenderEditorObjects;
			}

			if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_F1, true))
			{
				renderImGuiNextFrame = !renderImGuiNextFrame;
			}

			if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_ESCAPE))
			{
				DeselectCurrentlySelectedObjects();
			}

			if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_DELETE))
			{
				if (!m_CurrentlySelectedObjects.empty())
				{
					i32 i = 0;
					while (i < (i32)m_CurrentlySelectedObjects.size())
					{
						if (!g_SceneManager->CurrentScene()->DestroyGameObject(m_CurrentlySelectedObjects[i], true))
						{
							// This path should never execute, but if it does in a shipping build at least prevent an infinite loop
							assert(false);
							++i;
						}
					}

					DeselectCurrentlySelectedObjects();
				}
			}

			if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_RIGHT_BRACKET))
			{
				g_SceneManager->SetNextSceneActiveAndInit();
			}
			else if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_LEFT_BRACKET))
			{
				g_SceneManager->SetPreviousSceneActiveAndInit();
			}

			if (g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_LEFT_CONTROL) &&
				g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_R))
			{
				g_Renderer->ReloadShaders();
			}
			else if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_R))
			{
				g_InputManager->ClearAllInputs();

				g_SceneManager->ReloadCurrentScene();
			}

			if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_P))
			{
				PhysicsDebuggingSettings& settings = g_Renderer->GetPhysicsDebuggingSettings();
				settings.DrawWireframe = !settings.DrawWireframe;
			}

			bool altDown = g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_LEFT_ALT) ||
				g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_RIGHT_ALT);
			if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_F11) ||
				(altDown && g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_ENTER)))
			{
				g_Window->ToggleFullscreen();
			}

			if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_F) && !m_CurrentlySelectedObjects.empty())
			{
				glm::vec3 minPos(FLT_MAX);
				glm::vec3 maxPos(FLT_MIN);
				for (GameObject* gameObject : m_CurrentlySelectedObjects)
				{
					MeshComponent* mesh = gameObject->GetMeshComponent();
					if (mesh)
					{
						Transform* transform = gameObject->GetTransform();
						glm::vec3 min = transform->GetWorldTransform() * glm::vec4(mesh->m_MinPoint, 1.0f);
						glm::vec3 max = transform->GetWorldTransform() * glm::vec4(mesh->m_MaxPoint, 1.0f);
						minPos = glm::min(minPos, min);
						maxPos = glm::max(maxPos, max);
					}
				}
				glm::vec3 sphereCenterWS = minPos + (maxPos - minPos) / 2.0f;
				real sphereRadius = glm::length(maxPos - minPos) / 2.0f;

				BaseCamera* cam = g_CameraManager->CurrentCamera();

				glm::vec3 currentOffset = cam->GetPosition() - sphereCenterWS;
				glm::vec3 newOffset = glm::normalize(currentOffset) * sphereRadius * 2.0f;

				cam->SetPosition(sphereCenterWS + newOffset);
				cam->LookAt(sphereCenterWS);
			}

			if (g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_LEFT_CONTROL) &&
				g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_A))
			{
				SelectAll();
			}

			Profiler::Update();

			g_CameraManager->Update();

			if (!m_CurrentlySelectedObjects.empty())
			{
				m_TransformGizmo->SetVisible(true);
				m_TransformGizmo->GetTransform()->SetWorldPosition(m_SelectedObjectsCenterPos);
				m_TransformGizmo->GetTransform()->SetWorldRotation(m_SelectedObjectRotation);
			}
			else
			{
				m_TransformGizmo->SetVisible(false);
			}
			g_SceneManager->UpdateCurrentScene();

			
			g_Window->Update();

			if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_S) &&
				g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_LEFT_CONTROL))
			{
				g_SceneManager->CurrentScene()->SerializeToFile(true);
			}

			if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_D) &&
				g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_LEFT_CONTROL))
			{
				if (!m_CurrentlySelectedObjects.empty())
				{
					std::vector<GameObject*> newSelectedGameObjects;

					for (GameObject* gameObject : m_CurrentlySelectedObjects)
					{
						GameObject* duplicatedObject = gameObject->CopySelfAndAddToScene(nullptr, true);

						duplicatedObject->AddSelfAndChildrenToVec(newSelectedGameObjects);
					}

					DeselectCurrentlySelectedObjects();

					m_CurrentlySelectedObjects = newSelectedGameObjects;
					CalculateSelectedObjectsCenter();
				}
			}

			bool bWriteProfilingResultsToFile = 
				g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_K);

			g_Renderer->Update();

			// TODO: Consolidate functions?
			g_InputManager->Update();
			g_InputManager->PostUpdate();
			PROFILE_END("Update");

			PROFILE_BEGIN("Render");
			g_Renderer->Draw();
			PROFILE_END("Render");

			// We can update this now that the renderer has had a chance to end the frame
			m_bRenderImGui = renderImGuiNextFrame;

			if (!m_bRenderImGui)
			{
				// Prevent mouse from being ignored while hovering over invisible ImGui elements
				ImGui::GetIO().WantCaptureMouse = false;
			}

			m_SecondsSinceLastCommonSettingsFileSave += g_DeltaTime;
			if (m_SecondsSinceLastCommonSettingsFileSave > m_SecondsBetweenCommonSettingsFileSave)
			{
				m_SecondsSinceLastCommonSettingsFileSave = 0.0f;
				SaveCommonSettingsToDisk(false);
				g_Window->SaveToConfig();
			}

			const bool bProfileFrame = (m_FrameCount > 3);
			if (bProfileFrame)
			{
				Profiler::EndFrame(m_bUpdateProfilerFrame);
			}

			m_bUpdateProfilerFrame = false;

			if (bWriteProfilingResultsToFile)
			{
				Profiler::PrintResultsToFile();
			}

			++m_FrameCount;
		}
	}

	void FlexEngine::SetupImGuiStyles()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.MouseDrawCursor = false;

		std::string fontFilePath(RESOURCE_LOCATION + "fonts/lucon.ttf");
		io.Fonts->AddFontFromFileTTF(fontFilePath.c_str(), 13);

		io.FontGlobalScale = g_Monitor->contentScaleX;

		ImGuiStyle& style = ImGui::GetStyle();
		style.Colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
		style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.85f);
		style.Colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.05f, 0.10f, 0.90f);
		style.Colors[ImGuiCol_Border] = ImVec4(0.70f, 0.70f, 0.70f, 0.40f);
		style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_FrameBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.30f);
		style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.90f, 0.80f, 0.80f, 0.40f);
		style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.90f, 0.65f, 0.65f, 0.45f);
		style.Colors[ImGuiCol_TitleBg] = ImVec4(0.74f, 0.33f, 0.09f, 0.94f);
		style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.87f, 0.15f, 0.02f, 0.94f);
		style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.74f, 0.33f, 0.09f, 0.20f);
		style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.83f, 0.25f, 0.07f, 0.55f);
		style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.20f, 0.25f, 0.30f, 0.60f);
		style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.80f, 0.75f, 0.40f, 0.40f);
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.80f, 0.75f, 0.41f, 0.50f);
		style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.92f, 0.82f, 0.29f, 0.60f);
		style.Colors[ImGuiCol_CheckMark] = ImVec4(0.97f, 0.54f, 0.03f, 1.00f);
		style.Colors[ImGuiCol_SliderGrab] = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
		style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.82f, 0.61f, 0.37f, 1.00f);
		style.Colors[ImGuiCol_Button] = ImVec4(0.95f, 0.53f, 0.22f, 0.60f);
		style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.82f, 0.49f, 0.20f, 1.00f);
		style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.71f, 0.37f, 0.11f, 1.00f);
		style.Colors[ImGuiCol_Header] = ImVec4(0.66f, 0.32f, 0.17f, 0.76f);
		style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.74f, 0.43f, 0.29f, 0.76f);
		style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.60f, 0.23f, 0.07f, 0.80f);
		style.Colors[ImGuiCol_Separator] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
		style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.70f, 0.62f, 0.60f, 1.00f);
		style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.90f, 0.78f, 0.70f, 1.00f);
		style.Colors[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
		style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.60f);
		style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
		style.Colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(1.00f, 0.57f, 0.31f, 0.35f);
		style.Colors[ImGuiCol_ModalWindowDarkening] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
		style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 1.00f, 0.60f);
		style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	}

	void FlexEngine::DrawImGuiObjects()
	{
		ImGui::ShowDemoWindow();

		static const std::string titleString = (std::string("Flex Engine v") + EngineVersionString());
		static const char* titleCharStr = titleString.c_str();
		ImGuiWindowFlags mainWindowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_ResizeFromAnySide;
		glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
		m_ImGuiMainWindowWidthMax = frameBufferSize.x - 100.0f;
		ImGui::SetNextWindowSizeConstraints(ImVec2(350, 300), 
											ImVec2((real)frameBufferSize.x, (real)frameBufferSize.y),
											[](ImGuiSizeCallbackData* data)
		{
			FlexEngine* engine = (FlexEngine*)data->UserData;
			engine->m_ImGuiMainWindowWidth = data->DesiredSize.x;
			engine->m_ImGuiMainWindowWidth = glm::min(engine->m_ImGuiMainWindowWidthMax,
													  glm::max(engine->m_ImGuiMainWindowWidth, engine->m_ImGuiMainWindowWidthMin));
		}, this);
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Once);
		real frameBufferHeight = (real)frameBufferSize.y;
		ImGui::SetNextWindowSize(ImVec2(m_ImGuiMainWindowWidth, frameBufferHeight),
								 ImGuiCond_Always);
		if (ImGui::Begin(titleCharStr, nullptr, mainWindowFlags))
		{
			if (ImGui::TreeNode("Stats"))
			{
				static const std::string rendererNameStringStr = std::string("Current renderer: " + m_RendererName);
				static const char* renderNameStr = rendererNameStringStr.c_str();
				ImGui::TextUnformatted(renderNameStr);

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Misc settings"))
			{
				ImGui::Checkbox("Log to console", &g_bEnableLogToConsole);

				ImGui::Checkbox("Toggle profiler overview", &Profiler::s_bDisplayingFrame);

				if (ImGui::Button("Display latest frame"))
				{
					m_bUpdateProfilerFrame = true;
					Profiler::s_bDisplayingFrame = true;
				}

				ImGui::TreePop();
			}

			static const char* rendererSettingsStr = "Renderer settings";
			if (ImGui::TreeNode(rendererSettingsStr))
			{
				if (ImGui::Button("  Save  "))
				{
					g_Renderer->SaveSettingsToDisk(false, true);
				}

				ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColor);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColor);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColor);
				{
					ImGui::SameLine();
					if (ImGui::Button("Save over defaults"))
					{
						g_Renderer->SaveSettingsToDisk(true, true);
					}

					ImGui::SameLine();
					if (ImGui::Button("Reload defaults"))
					{
						g_Renderer->LoadSettingsFromDisk(true);
					}
				}
				ImGui::PopStyleColor();
				ImGui::PopStyleColor();
				ImGui::PopStyleColor();

				bool bVSyncEnabled = g_Renderer->GetVSyncEnabled();
				static const char* vSyncEnabledStr = "VSync";
				if (ImGui::Checkbox(vSyncEnabledStr, &bVSyncEnabled))
				{
					g_Renderer->SetVSyncEnabled(bVSyncEnabled);
				}

				static const char* displayBoundingVolumesStr = "Display bounding volumes";
				bool bDisplayBoundingVolumes = g_Renderer->IsDisplayBoundingVolumesEnabled();
				if (ImGui::Checkbox(displayBoundingVolumesStr, &bDisplayBoundingVolumes))
				{
					g_Renderer->SetDisplayBoundingVolumesEnabled(bDisplayBoundingVolumes);
				}

				static const char* physicsDebuggingStr = "Physics debugging";
				if (ImGui::TreeNode(physicsDebuggingStr))
				{
					PhysicsDebuggingSettings& physicsDebuggingSettings = g_Renderer->GetPhysicsDebuggingSettings();

					static const char* disableAllStr = "Disable All";
					ImGui::Checkbox(disableAllStr, &physicsDebuggingSettings.DisableAll);

					ImGui::Spacing();
					ImGui::Spacing();
					ImGui::Spacing();

					static const char* wireframeStr = "Wireframe";
					ImGui::Checkbox(wireframeStr, &physicsDebuggingSettings.DrawWireframe);

					static const char* aabbStr = "AABB";
					ImGui::Checkbox(aabbStr, &physicsDebuggingSettings.DrawAabb);

					static const char* drawFeaturesTextStr = "Draw Features Text";
					ImGui::Checkbox(drawFeaturesTextStr, &physicsDebuggingSettings.DrawFeaturesText);

					static const char* drawContactPointsStr = "Draw Contact Points";
					ImGui::Checkbox(drawContactPointsStr, &physicsDebuggingSettings.DrawContactPoints);

					static const char* noDeactivationStr = "No Deactivation";
					ImGui::Checkbox(noDeactivationStr, &physicsDebuggingSettings.NoDeactivation);

					static const char* noHelpTextStr = "No Help Text";
					ImGui::Checkbox(noHelpTextStr, &physicsDebuggingSettings.NoHelpText);

					static const char* drawTextStr = "Draw Text";
					ImGui::Checkbox(drawTextStr, &physicsDebuggingSettings.DrawText);

					static const char* profileTimingsStr = "Profile Timings";
					ImGui::Checkbox(profileTimingsStr, &physicsDebuggingSettings.ProfileTimings);

					static const char* satComparisonStr = "Sat Comparison";
					ImGui::Checkbox(satComparisonStr, &physicsDebuggingSettings.EnableSatComparison);

					static const char* disableBulletLCPStr = "Disable Bullet LCP";
					ImGui::Checkbox(disableBulletLCPStr, &physicsDebuggingSettings.DisableBulletLCP);

					static const char* ccdStr = "CCD";
					ImGui::Checkbox(ccdStr, &physicsDebuggingSettings.EnableCCD);

					static const char* drawConstraintsStr = "Draw Constraints";
					ImGui::Checkbox(drawConstraintsStr, &physicsDebuggingSettings.DrawConstraints);

					static const char* drawConstraintLimitsStr = "Draw Constraint Limits";
					ImGui::Checkbox(drawConstraintLimitsStr, &physicsDebuggingSettings.DrawConstraintLimits);

					static const char* fastWireframeStr = "Fast Wireframe";
					ImGui::Checkbox(fastWireframeStr, &physicsDebuggingSettings.FastWireframe);

					static const char* drawNormalsStr = "Draw Normals";
					ImGui::Checkbox(drawNormalsStr, &physicsDebuggingSettings.DrawNormals);

					static const char* drawFramesStr = "Draw Frames";
					ImGui::Checkbox(drawFramesStr, &physicsDebuggingSettings.DrawFrames);

					ImGui::TreePop();
				}

				static const char* postProcessStr = "Post processing";
				if (ImGui::TreeNode(postProcessStr))
				{
					Renderer::PostProcessSettings& postProcessSettings = g_Renderer->GetPostProcessSettings();

					bool bPostProcessingEnabled = g_Renderer->IsPostProcessingEnabled();
					if (ImGui::Checkbox("Enabled", &bPostProcessingEnabled))
					{
						g_Renderer->SetPostProcessingEnabled(bPostProcessingEnabled);
					}

					static const char* fxaaEnabledStr = "FXAA";
					ImGui::Checkbox(fxaaEnabledStr, &postProcessSettings.bEnableFXAA);

					if (postProcessSettings.bEnableFXAA)
					{
						ImGui::Indent();
						static const char* fxaaShowEdgesEnabledStr = "Show edges";
						ImGui::Checkbox(fxaaShowEdgesEnabledStr, &postProcessSettings.bEnableFXAADEBUGShowEdges);
						ImGui::Unindent();
					}

					static const char* brightnessStr = "Brightness (RGB)";
					real maxBrightness = 2.5f;
					ImGui::SliderFloat3(brightnessStr, &postProcessSettings.brightness.r, 0.0f, maxBrightness);
					ImGui::SameLine();
					ImGui::ColorButton("##1", ImVec4(
						postProcessSettings.brightness.r / maxBrightness,
						postProcessSettings.brightness.g / maxBrightness,
						postProcessSettings.brightness.b / maxBrightness, 1));

					static const char* offsetStr = "Offset (RGB)";
					real minOffset = -0.35f;
					real maxOffset = 0.35f;
					ImGui::SliderFloat3(offsetStr, &postProcessSettings.offset.r, minOffset, maxOffset);
					ImGui::SameLine();
					ImGui::ColorButton("##2", ImVec4(
						(postProcessSettings.offset.r - minOffset) / (maxOffset - minOffset),
						(postProcessSettings.offset.g - minOffset) / (maxOffset - minOffset),
						(postProcessSettings.offset.b - minOffset) / (maxOffset - minOffset), 1));

					static const char* saturationStr = "Saturation";
					const real maxSaturation = 2.0f;
					ImGui::SliderFloat(saturationStr, &postProcessSettings.saturation, 0.0f, maxSaturation);
					ImGui::SameLine();
					ImGui::ColorButton("##3", ImVec4(
						postProcessSettings.saturation / maxSaturation,
						postProcessSettings.saturation / maxSaturation,
						postProcessSettings.saturation / maxSaturation, 1));

					ImGui::TreePop();
				}

				ImGui::TreePop();
			}

			static const char* windowSettingsStr = "Window settings";
			if (ImGui::TreeNode(windowSettingsStr))
			{
				bool bAutoRestoreWindow = g_Window->GetAutoRestoreStateEnabled();
				if (ImGui::Checkbox("Auto restore state", &bAutoRestoreWindow))
				{
					g_Window->SetAutoRestoreStateEnabled(bAutoRestoreWindow);
					g_Renderer->SaveSettingsToDisk(false, true);
				}

				glm::vec2i windowPos = g_Window->GetPosition();
				if (ImGui::DragInt2("Position", &windowPos.x, 1.0f))
				{
					g_Window->SetPosition(windowPos.x, windowPos.y);
				}

				if (ImGui::Button("Center"))
				{
					glm::vec2i windowSize = g_Window->GetSize();
					g_Window->SetPosition(g_Monitor->width / 2 - windowSize.x / 2,
										  g_Monitor->height / 2 - windowSize.y / 2);
				}

				ImGui::NewLine();

				glm::vec2i windowSize = g_Window->GetSize();
				if (ImGui::DragInt2("Size", &windowSize.x, 1.0f, 100, 3840))
				{
					g_Window->SetSize(windowSize.x, windowSize.y);
				}

				bool bWindowWasMaximized = g_Window->IsMaximized();
				if (bWindowWasMaximized)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
				}
				if (ImGui::Button("Maximize"))
				{
					g_Window->Maximize();
				}
				if (bWindowWasMaximized)
				{
					ImGui::PopStyleColor();
				}

				ImGui::SameLine();

				if (ImGui::Button("Iconify"))
				{
					g_Window->Iconify();
				}

				if (ImGui::Button("1920x1080"))
				{
					g_Window->SetSize(1920, 1080);
				}

				ImGui::SameLine();

				if (ImGui::Button("1600x900"))
				{
					g_Window->SetSize(1600, 900);
				}

				ImGui::SameLine();

				if (ImGui::Button("1280x720"))
				{
					g_Window->SetSize(1280, 720);
				}

				ImGui::SameLine();

				if (ImGui::Button("800x600"))
				{
					g_Window->SetSize(800, 600);
				}

				ImGui::TreePop();
			}

			// TODO: Add DrawImGuiItems to camera class and let it handle itself?
			const char* cameraStr = "Camera";
			if (ImGui::TreeNode(cameraStr))
			{
				BaseCamera* currentCamera = g_CameraManager->CurrentCamera();

				const i32 cameraCount = g_CameraManager->CameraCount();

				if (cameraCount > 1) // Only show arrows if other cameras exist
				{
					static const char* arrowPrevStr = "<";
					static const char* arrowNextStr = ">";

					if (ImGui::Button(arrowPrevStr))
					{
						g_CameraManager->SetActiveIndexRelative(-1, false);
					}

					ImGui::SameLine();

					std::string cameraNameStr = currentCamera->GetName();
					ImGui::TextUnformatted(cameraNameStr.c_str());

					ImGui::SameLine();

					if (ImGui::Button(arrowNextStr))
					{
						g_CameraManager->SetActiveIndexRelative(1, false);
					}
				}

				static const char* moveSpeedStr = "Move speed";
				float moveSpeed = currentCamera->GetMoveSpeed();
				if (ImGui::SliderFloat(moveSpeedStr, &moveSpeed, 1.0f, 250.0f))
				{
					currentCamera->SetMoveSpeed(moveSpeed);
				}

				static const char* turnSpeedStr = "Turn speed";
				float turnSpeed = glm::degrees(currentCamera->GetRotationSpeed());
				if (ImGui::SliderFloat(turnSpeedStr, &turnSpeed, 0.01f, 0.3f))
				{
					currentCamera->SetRotationSpeed(glm::radians(turnSpeed));
				}

				glm::vec3 camPos = currentCamera->GetPosition();
				if (ImGui::DragFloat3("Position", &camPos.x, 0.1f))
				{
					currentCamera->SetPosition(camPos);
				}

				glm::vec2 camYawPitch;
				camYawPitch[0] = glm::degrees(currentCamera->GetYaw());
				camYawPitch[1] = glm::degrees(currentCamera->GetPitch());
				if (ImGui::DragFloat2("Yaw & Pitch", &camYawPitch.x, 0.05f))
				{
					currentCamera->SetYaw(glm::radians(camYawPitch[0]));
					currentCamera->SetPitch(glm::radians(camYawPitch[1]));
				}

				real camFOV = glm::degrees(currentCamera->GetFOV());
				if (ImGui::DragFloat("FOV", &camFOV, 0.05f, 10.0f, 150.0f))
				{
					currentCamera->SetFOV(glm::radians(camFOV));
				}

				if (ImGui::Button("Reset orientation"))
				{
					currentCamera->ResetOrientation();
				}

				ImGui::SameLine();
				if (ImGui::Button("Reset position"))
				{
					currentCamera->ResetPosition();
				}

				ImGui::TreePop();
			}

			static const char* scenesStr = "Scenes";
			if (ImGui::TreeNode(scenesStr))
			{
				static const char* arrowPrevStr = "<";
				static const char* arrowNextStr = ">";

				if (ImGui::Button(arrowPrevStr))
				{
					g_SceneManager->SetPreviousSceneActiveAndInit();
				}
				
				ImGui::SameLine();

				BaseScene* currentScene = g_SceneManager->CurrentScene();

				const std::string currentSceneNameStr(currentScene->GetName() + (currentScene->IsUsingSaveFile() ? " (saved)" : " (default)"));
				ImGui::Text(currentSceneNameStr.c_str());
				
				DoSceneContextMenu(currentScene);

				if (ImGui::IsItemHovered())
				{
					std::string fileName = currentScene->GetShortRelativeFilePath();
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(fileName.c_str());
					ImGui::EndTooltip();
				}

				ImGui::SameLine();

				if (ImGui::Button(arrowNextStr))
				{
					g_SceneManager->SetNextSceneActiveAndInit();
				}

				i32 sceneItemWidth = 240;
				if (ImGui::BeginChild("Scenes", ImVec2((real)sceneItemWidth, 120), true, ImGuiWindowFlags_NoResize))
				{
					i32 currentSceneIndex = g_SceneManager->GetCurrentSceneIndex();
					for (i32 i = 0; i < (i32)g_SceneManager->GetSceneCount(); ++i)
					{
						bool bSceneSelected = (i == currentSceneIndex);
						BaseScene* scene = g_SceneManager->GetSceneAtIndex(i);
						std::string sceneFileName = scene->GetFileName();
						if (ImGui::Selectable(sceneFileName.c_str(), &bSceneSelected, 0, ImVec2((real)sceneItemWidth, 0)))
						{
							if (i != currentSceneIndex)
							{
								if (g_SceneManager->SetCurrentScene(i))
								{
									g_SceneManager->InitializeCurrentScene();
									g_SceneManager->PostInitializeCurrentScene();
								}
							}
						}

						DoSceneContextMenu(scene);
					}
				}
				ImGui::EndChild();

				static const char* addSceneStr = "Add scene...";
				std::string addScenePopupID = "Add scene";
				if (ImGui::Button(addSceneStr))
				{
					ImGui::OpenPopup(addScenePopupID.c_str());
				}

				if (ImGui::BeginPopupModal(addScenePopupID.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
				{
					static std::string newSceneName = "Scene_" + IntToString(g_SceneManager->GetSceneCount(), 2);

					const size_t maxStrLen = 256;
					newSceneName.resize(maxStrLen);
					bool bCreate = ImGui::InputText("Scene name", 
													(char*)newSceneName.data(), 
													maxStrLen,
													ImGuiInputTextFlags_EnterReturnsTrue);

					bCreate |= ImGui::Button("Create");

					if (bCreate)
					{
						// Remove trailing '\0' characters
						newSceneName = std::string(newSceneName.c_str());

						g_SceneManager->CreateNewScene(newSceneName, true);

						ImGui::CloseCurrentPopup();
					}

					ImGui::SameLine();

					if (ImGui::Button("Cancel"))
					{
						ImGui::CloseCurrentPopup();
					}

					ImGui::EndPopup();
				}

				ImGui::SameLine();

				if (ImGui::Button("Refresh scenes"))
				{
					g_SceneManager->AddFoundScenes();
					g_SceneManager->RemoveDeletedScenes();
				}

				ImGui::TreePop();
			}
			
			const char* reloadStr = "Reloading";
			if (ImGui::TreeNode(reloadStr))
			{
				if (ImGui::Button("Reload scene file"))
				{
					g_SceneManager->ReloadCurrentScene();
				}

				if (ImGui::Button("Hard reload scene file (reloads all meshes)"))
				{
					Print("Clearing all loaded meshes\n");
					MeshComponent::DestroyAllLoadedMeshes();
					g_SceneManager->ReloadCurrentScene();
				}

				if (ImGui::Button("Reload all shaders"))
				{
					g_Renderer->ReloadShaders();
				}

				if (ImGui::Button("Reload player positions"))
				{
					g_SceneManager->CurrentScene()->GetPlayer(0)->GetController()->ResetTransformAndVelocities();
					g_SceneManager->CurrentScene()->GetPlayer(1)->GetController()->ResetTransformAndVelocities();
				}

				ImGui::TreePop();
			}

			const char* audioStr = "Audio";
			if (ImGui::TreeNode(audioStr))
			{
				real gain = AudioManager::GetMasterGain();
				if (ImGui::SliderFloat("Master volume", &gain, 0.0f, 1.0f))
				{
					AudioManager::SetMasterGain(gain);
				}

				ImGui::TreePop();
			}

			g_Renderer->DrawImGuiItems();
		}

		ImGui::End();

		g_Renderer->DrawAssetBrowserImGui();
	}

	void FlexEngine::Stop()
	{
		m_bRunning = false;
	}

	std::vector<GameObject*> FlexEngine::GetSelectedObjects()
	{
		return m_CurrentlySelectedObjects;
	}

	void FlexEngine::SetSelectedObject(GameObject* gameObject)
	{
		DeselectCurrentlySelectedObjects();

		if (gameObject != nullptr)
		{
			gameObject->AddSelfAndChildrenToVec(m_CurrentlySelectedObjects);
		}

		CalculateSelectedObjectsCenter();
	}

	void FlexEngine::ToggleSelectedObject(GameObject* gameObject)
	{
		auto iter = Find(m_CurrentlySelectedObjects, gameObject);
		if (iter == m_CurrentlySelectedObjects.end())
		{
			gameObject->AddSelfAndChildrenToVec(m_CurrentlySelectedObjects);
		}
		else
		{
			m_CurrentlySelectedObjects.erase(iter);

			if (m_CurrentlySelectedObjects.empty())
			{
				DeselectCurrentlySelectedObjects();
			}
		}

		CalculateSelectedObjectsCenter();
	}

	void FlexEngine::AddSelectedObject(GameObject* gameObject)
	{
		auto iter = Find(m_CurrentlySelectedObjects, gameObject);
		if (iter == m_CurrentlySelectedObjects.end())
		{
			gameObject->AddSelfAndChildrenToVec(m_CurrentlySelectedObjects);

			CalculateSelectedObjectsCenter();
		}
	}

	void FlexEngine::DeselectObject(GameObject* gameObject)
	{
		for (auto iter = m_CurrentlySelectedObjects.begin(); iter != m_CurrentlySelectedObjects.end(); ++iter)
		{
			if ((*iter) == gameObject)
			{
				gameObject->RemoveSelfAndChildrenToVec(m_CurrentlySelectedObjects);
				CalculateSelectedObjectsCenter();
				return;
			}
		}

		PrintWarn("Attempted to deselect object which wasn't selected!\n");
	}

	bool FlexEngine::IsObjectSelected(GameObject* gameObject)
	{
		bool bSelected = (Find(m_CurrentlySelectedObjects, gameObject) != m_CurrentlySelectedObjects.end());
		return bSelected;
	}

	glm::vec3 FlexEngine::GetSelectedObjectsCenter()
	{
		return m_SelectedObjectsCenterPos;
	}

	void FlexEngine::DeselectCurrentlySelectedObjects()
	{
		m_CurrentlySelectedObjects.clear();
		m_SelectedObjectRotation = glm::quat(glm::vec3(0.0f));
		m_SelectedObjectsCenterPos = glm::vec3(0.0f);
		m_SelectedObjectDragStartPos = glm::vec3(0.0f);
		m_DraggingAxisIndex = -1;
		m_bDraggingGizmo = false;
	}

	bool FlexEngine::LoadCommonSettingsFromDisk()
	{
		if (m_CommonSettingsAbsFilePath.empty())
		{
			PrintError("Failed to read common settings to disk: file path is not set!\n");
			return false;
		}

		if (FileExists(m_CommonSettingsAbsFilePath))
		{
			Print("Loading common settings from %s\n", m_CommonSettingsFileName.c_str());

			JSONObject rootObject = {};

			if (JSONParser::Parse(m_CommonSettingsAbsFilePath, rootObject))
			{
				std::string lastOpenedSceneName = rootObject.GetString("last opened scene");
				if (!lastOpenedSceneName.empty())
				{
					// Don't print errors if not found, file might have been deleted since last session
					g_SceneManager->SetCurrentScene(lastOpenedSceneName, false);
				}

				JSONObject cameraTransform;
				if (rootObject.SetObjectChecked("camera transform", cameraTransform))
				{
					BaseCamera* cam = g_CameraManager->CurrentCamera();
					glm::vec3 camPos = ParseVec3(cameraTransform.GetString("position"));
					if (IsNanOrInf(camPos))
					{
						PrintError("Camera pos was saved out as nan or inf, resetting to 0\n");
						camPos = glm::vec3(0.0f);
					}
					cam->SetPosition(camPos);
					
					real camPitch = cameraTransform.GetFloat("pitch");
					if (IsNanOrInf(camPitch))
					{
						PrintError("Camera pitch was saved out as nan or inf, resetting to 0\n");
						camPitch = 0.0f;
					}
					cam->SetPitch(camPitch);

					real camYaw = cameraTransform.GetFloat("yaw");
					if (IsNanOrInf(camYaw))
					{
						PrintError("Camera yaw was saved out as nan or inf, resetting to 0\n");
						camYaw = 0.0f;
					}
					cam->SetYaw(camYaw);
				}

				real masterGain;
				if (rootObject.SetFloatChecked("master gain", masterGain))
				{
					AudioManager::SetMasterGain(masterGain);
				}

				return true;
			}
			else
			{
				PrintError("Failed to parse common settings config file\n");
				return false;
			}
		}

		return false;
	}

	void FlexEngine::SaveCommonSettingsToDisk(bool bAddEditorStr)
	{
		if (m_CommonSettingsAbsFilePath.empty())
		{
			PrintError("Failed to save common settings to disk: file path is not set!\n");
			return;
		}

		JSONObject rootObject{};

		std::string lastOpenedSceneName = g_SceneManager->CurrentScene()->GetFileName();
		rootObject.fields.emplace_back("last opened scene", JSONValue(lastOpenedSceneName));

		BaseCamera* cam = g_CameraManager->CurrentCamera();
		std::string posStr = Vec3ToString(cam->GetPosition(), 3);
		real pitch = cam->GetPitch();
		real yaw = cam->GetYaw();
		JSONObject cameraTransform = {};
		cameraTransform.fields.emplace_back("position", JSONValue(posStr));
		cameraTransform.fields.emplace_back("pitch", JSONValue(pitch));
		cameraTransform.fields.emplace_back("yaw", JSONValue(yaw));
		rootObject.fields.emplace_back("camera transform", JSONValue(cameraTransform));

		real masterGain = AudioManager::GetMasterGain();
		rootObject.fields.emplace_back("master gain", JSONValue(masterGain));

		std::string fileContents = rootObject.Print(0);

		if (!WriteFile(m_CommonSettingsAbsFilePath, fileContents, false))
		{
			PrintError("Failed to write common settings config file\n");
		}

		if (bAddEditorStr)
		{
			g_Renderer->AddEditorString("Saved common settings");
		}
	}

	void FlexEngine::DoSceneContextMenu(BaseScene* scene)
	{
		bool bClicked = ImGui::IsMouseReleased(1) && 
						ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

		BaseScene* currentScene = g_SceneManager->CurrentScene();

		std::string contextMenuID = "scene context menu " + scene->GetFileName();
		if (ImGui::BeginPopupContextItem(contextMenuID.c_str()))
		{
			{
				const i32 sceneNameMaxCharCount = 256;

				// We don't know the names of scene's that haven't been loaded
				if (scene->IsLoaded())
				{
					static char newSceneName[sceneNameMaxCharCount];
					if (bClicked)
					{
						strcpy_s(newSceneName, scene->GetName().c_str());
					}

					bool bRenameScene = ImGui::InputText("##rename-scene",
														 newSceneName,
														 sceneNameMaxCharCount,
														 ImGuiInputTextFlags_EnterReturnsTrue);

					ImGui::SameLine();

					bRenameScene |= ImGui::Button("Rename scene");

					if (bRenameScene)
					{
						scene->SetName(newSceneName);
						// Don't close popup here since we will likely want to save that change
					}
				}

				static char newSceneFileName[sceneNameMaxCharCount];
				if (bClicked)
				{
					strcpy_s(newSceneFileName, scene->GetFileName().c_str());
				}

				bool bRenameSceneFileName = ImGui::InputText("##rename-scene-file-name",
															 newSceneFileName,
															 sceneNameMaxCharCount,
															 ImGuiInputTextFlags_EnterReturnsTrue);

				ImGui::SameLine();

				bRenameSceneFileName |= ImGui::Button("Rename file");

				if (bRenameSceneFileName)
				{
					std::string newSceneFileNameStr(newSceneFileName);
					std::string fileDir = RelativePathToAbsolute(scene->GetDefaultRelativeFilePath());
					ExtractDirectoryString(fileDir);
					std::string newSceneFilePath = fileDir + newSceneFileNameStr;
					bool bNameEmpty = newSceneFileNameStr.empty();
					bool bCorrectFileType = EndsWith(newSceneFileNameStr, ".json");
					bool bFileExists = FileExists(newSceneFilePath);
					bool bSceneNameValid = (!bNameEmpty &&
											bCorrectFileType &&
											!bFileExists);

					if (bSceneNameValid)
					{
						if (scene->SetFileName(newSceneFileNameStr, true))
						{
							ImGui::CloseCurrentPopup();
						}
					}
					else
					{
						PrintError("Attempted name scene with invalid name: %s\n", newSceneFileNameStr.c_str());
						if (bNameEmpty)
						{
							PrintError("(file name is empty!)\n");
						}
						else if (!bCorrectFileType)
						{
							PrintError("(must end with \".json\"!)\n");
						}
						else if (bFileExists)
						{
							PrintError("(file already exists!)\n");
						}
					}
				}
			}

			// Only allow current scene to be saved
			if (currentScene == scene)
			{
				if (ImGui::Button("Save"))
				{
					scene->SerializeToFile(false);

					ImGui::CloseCurrentPopup();
				}

				ImGui::SameLine();

				ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColor);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColor);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColor);

				if (ImGui::Button("Save over default"))
				{
					scene->SerializeToFile(true);

					ImGui::CloseCurrentPopup();
				}

				if (scene->IsUsingSaveFile())
				{
					ImGui::SameLine();

					if (ImGui::Button("Hard reload (deletes save file!)"))
					{
						DeleteFile(scene->GetRelativeFilePath());
						g_SceneManager->ReloadCurrentScene();

						ImGui::CloseCurrentPopup();
					}
				}

				ImGui::PopStyleColor();
				ImGui::PopStyleColor();
				ImGui::PopStyleColor();
			}

			static const char* duplicateScenePopupLabel = "Duplicate scene";
			const i32 sceneNameMaxCharCount = 256;
			static char newSceneName[sceneNameMaxCharCount];
			static char newSceneFileName[sceneNameMaxCharCount];
			if (ImGui::Button("Duplicate..."))
			{
				ImGui::OpenPopup(duplicateScenePopupLabel);

				std::string newSceneNameStr = scene->GetName();
				newSceneNameStr += " Copy";
				strcpy_s(newSceneName, newSceneNameStr.c_str());

				std::string newSceneFileNameStr = scene->GetFileName();
				StripFileType(newSceneFileNameStr);

				bool bValidName = false;
				do 
				{
					i16 numNumericalChars = 0;
					i32 numEndingWith = GetNumberEndingWith(newSceneFileNameStr, numNumericalChars);
					if (numNumericalChars > 0)
					{
						u32 charsBeforeNum = (newSceneFileNameStr.length() - numNumericalChars);
						newSceneFileNameStr = newSceneFileNameStr.substr(0, charsBeforeNum) +
							IntToString(numEndingWith + 1, numNumericalChars);
					}
					else
					{
						newSceneFileNameStr += "_01";
					}

					std::string filePathFrom = RelativePathToAbsolute(scene->GetDefaultRelativeFilePath());
					std::string fullNewFilePath = filePathFrom;
					ExtractDirectoryString(fullNewFilePath);
					fullNewFilePath += newSceneFileNameStr + ".json";
					bValidName = !FileExists(fullNewFilePath);
				} while (!bValidName);

				newSceneFileNameStr += ".json";

				strcpy_s(newSceneFileName, newSceneFileNameStr.c_str());
			}

			bool bCloseContextMenu = false;
			if (ImGui::BeginPopupModal(duplicateScenePopupLabel,
				NULL,
				ImGuiWindowFlags_AlwaysAutoResize))
			{

				bool bDuplicateScene = ImGui::InputText("Name##duplicate-scene-name",
														newSceneName,
														sceneNameMaxCharCount,
														ImGuiInputTextFlags_EnterReturnsTrue);

				bDuplicateScene |= ImGui::InputText("File name##duplicate-scene-file-path",
													newSceneFileName,
													sceneNameMaxCharCount,
													ImGuiInputTextFlags_EnterReturnsTrue);

				bDuplicateScene |= ImGui::Button("Duplicate");

				bool bValidInput = true;

				if (strlen(newSceneName) == 0 ||
					strlen(newSceneFileName) == 0 ||
					!EndsWith(newSceneFileName, ".json"))
				{
					bValidInput = false;
				}

				if (bDuplicateScene && bValidInput)
				{
					std::string filePathFrom = RelativePathToAbsolute(scene->GetDefaultRelativeFilePath());
					std::string sceneFileDir = filePathFrom;
					ExtractDirectoryString(sceneFileDir);
					std::string filePathTo = sceneFileDir + newSceneFileName;

					if (FileExists(filePathTo))
					{
						PrintError("Attempting to duplicate scene onto already existing file name!\n");
					}
					else
					{
						if (CopyFile(filePathFrom, filePathTo))
						{
							BaseScene* newScene = new BaseScene(newSceneFileName);
							g_SceneManager->AddScene(newScene);
							g_SceneManager->SetCurrentScene(newScene);

							g_SceneManager->InitializeCurrentScene();
							g_SceneManager->PostInitializeCurrentScene();
							newScene->SetName(newSceneName);

							g_SceneManager->CurrentScene()->SerializeToFile(true);

							bCloseContextMenu = true;

							ImGui::CloseCurrentPopup();
						}
						else
						{
							PrintError("Failed to copy scene's file to %s\n", newSceneFileName);
						}
					}
				}

				ImGui::SameLine();

				if (ImGui::Button("Cancel"))
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Open in explorer"))
			{
				std::string directory = currentScene->GetRelativeFilePath();
				ExtractDirectoryString(directory);
				directory = RelativePathToAbsolute(directory);
				OpenExplorer(directory);
			}

			ImGui::SameLine();

			static const char* deleteSceneStr = "Delete scene...";
			const std::string deleteScenePopupID = "Delete scene";

			ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColor);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColor);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColor);

			if (ImGui::Button(deleteSceneStr))
			{
				ImGui::OpenPopup(deleteScenePopupID.c_str());
			}

			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();

			if (ImGui::BeginPopupModal(deleteScenePopupID.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
			{
				static std::string sceneName = g_SceneManager->CurrentScene()->GetName();

				ImGui::PushStyleColor(ImGuiCol_Text, g_WarningTextColor);
				std::string textStr = "Are you sure you want to permanently delete " + sceneName + "? (both the default & saved files)";
				ImGui::Text(textStr.c_str());
				ImGui::PopStyleColor();

				ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColor);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColor);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColor);
				if (ImGui::Button("Delete"))
				{
					g_SceneManager->DeleteScene(scene);

					ImGui::CloseCurrentPopup();

					bCloseContextMenu = true;
				}
				ImGui::PopStyleColor();
				ImGui::PopStyleColor();
				ImGui::PopStyleColor();

				ImGui::SameLine();

				if (ImGui::Button("Cancel"))
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			if (bCloseContextMenu)
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	void FlexEngine::CalculateSelectedObjectsCenter()
	{
		if (m_CurrentlySelectedObjects.empty())
		{
			m_SelectedObjectsCenterPos = glm::vec3(0.0f);
			m_SelectedObjectRotation = glm::quat(glm::vec3(0.0f));
			return;
		}

		glm::vec3 avgPos(0.0f);
		for (GameObject* gameObject : m_CurrentlySelectedObjects)
		{
			avgPos += gameObject->GetTransform()->GetWorldPosition();
		}
		m_SelectedObjectsCenterPos = m_SelectedObjectDragStartPos;
		m_SelectedObjectRotation = m_CurrentlySelectedObjects[m_CurrentlySelectedObjects.size() - 1]->GetTransform()->GetWorldRotation();

		m_SelectedObjectsCenterPos = (avgPos / (real)m_CurrentlySelectedObjects.size());
	}

	void FlexEngine::SelectAll()
	{
		for (GameObject* gameObject : g_SceneManager->CurrentScene()->GetAllObjects())
		{
			m_CurrentlySelectedObjects.push_back(gameObject);
		}
		CalculateSelectedObjectsCenter();
	}

	bool FlexEngine::IsDraggingGizmo() const
	{
		return m_bDraggingGizmo;
	}
	
	std::string FlexEngine::EngineVersionString()
	{
		return std::to_string(EngineVersionMajor) + "." +
			std::to_string(EngineVersionMinor) + "." +
			std::to_string(EngineVersionPatch);
	}
} // namespace flex
