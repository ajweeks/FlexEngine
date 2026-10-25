#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH

#include <LinearMath/btIDebugDraw.h>

#include <glm/gtx/norm.hpp> // for distance2
#include <glm/gtx/quaternion.hpp> // for rotate
#include <glm/gtx/vector_angle.hpp> // for angle
#include <glm/ext/matrix_transform.hpp> // for rotate

#include <imgui/imgui_internal.h> // For PushItemFlag

IGNORE_WARNINGS_POP

#include "Scene/Objects/Terminal.hpp"
#include "Cameras/TerminalCamera.hpp"
#include "FlexEngine.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Player.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/SceneManager.hpp"
#include "VirtualMachine/Backend/VirtualMachine.hpp"
#include "VirtualMachine/Diagnostics.hpp"
#include "Window/Window.hpp"

namespace flex
{
	Terminal::Terminal() :
		Terminal(g_SceneManager->CurrentScene()->GetUniqueObjectName("Terminal_", 2), InvalidGameObjectID)
	{
	}

	Terminal::Terminal(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, TerminalSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate),
		m_KeyEventCallback(this, &Terminal::OnKeyEvent),
		cursor(0, 0)
	{
		m_bInteractable = true;

		MaterialID matID;
		// TODO: Don't rely on material names!
		if (!g_Renderer->FindOrCreateMaterialByName("terminal copper", matID))
		{
			// TODO: Create own material
			matID = 0;
		}
		Mesh* mesh = SetMesh(new Mesh(this));
		if (!mesh->LoadFromFile(MESH_DIRECTORY "terminal-copper.glb", matID))
		{
			PrintWarn("Failed to load terminal mesh!\n");
		}

		m_VM = new VM::VirtualMachine();
	}

	PropertyCollection* Terminal::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();
		return collection;
	}

	void Terminal::Initialize()
	{
		g_InputManager->BindKeyEventCallback(&m_KeyEventCallback, 20);
		ParseCode();

		GetSystem<TerminalManager>(SystemType::TERMINAL_MANAGER)->RegisterTerminal(this);

		GameObject::Initialize();
	}

	void Terminal::Destroy(bool bDetachFromParent /* = true */)
	{
		g_InputManager->UnbindKeyEventCallback(&m_KeyEventCallback);

		if (m_VM != nullptr)
		{
			delete m_VM;
			m_VM = nullptr;
		}

		GetSystem<TerminalManager>(SystemType::TERMINAL_MANAGER)->DeregisterTerminal(this);

		GameObject::Destroy(bDetachFromParent);
	}

	void Terminal::Update()
	{
		PROFILE_AUTO("Terminal Update");

		PluggablesSystem* pluggablesSystem = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES);

		if (m_NearbyInteractableID.IsValid())
		{
			const real scale = 2.0f;
			const glm::vec3 right = m_Transform.GetRight();
			const glm::vec3 forward = m_Transform.GetForward();
			glm::vec3 c = m_Transform.GetWorldPosition();
			glm::vec3 v1 = c + (right + forward) * scale;
			glm::vec3 v2 = c + (-right + forward) * scale;
			glm::vec3 v3 = c - (forward * scale);
			DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();
			debugRenderer->drawTriangle(ToBtVec3(v1), ToBtVec3(v2), ToBtVec3(v3), btVector3(0.9f, 0.3f, 0.2f), 1.0f);
			glm::vec3 o(0.0f, sin(g_SecElapsedSinceProgramStart * 2.0f) + 1.0f, 0.0f);
			debugRenderer->drawTriangle(ToBtVec3(v1 + o), ToBtVec3(v2 + o), ToBtVec3(v3 + o), btVector3(0.9f, 0.3f, 0.2f), 1.0f);
			o = glm::vec3(0.0f, 2.0f, 0.0f);
			debugRenderer->drawTriangle(ToBtVec3(v1 + o), ToBtVec3(v2 + o), ToBtVec3(v3 + o), btVector3(0.9f, 0.3f, 0.2f), 1.0f);
		}

		std::vector<SocketData> const* sockets = pluggablesSystem->GetGameObjectSockets(ID);
		u32 socketCount = sockets != nullptr ? (u32)sockets->size() : 0;

		if (m_VM != nullptr && m_VM->IsExecuting())
		{
			u32 outputCount = glm::min((u32)m_VM->terminalOutputs.size(), socketCount);
			for (u32 i = 0; i < outputCount; ++i)
			{
				pluggablesSystem->SetGameObjectOutputSignal(ID, i, m_VM->terminalOutputs[i].valInt);
			}
			for (u32 i = outputCount; i < socketCount; ++i)
			{
				pluggablesSystem->SetGameObjectOutputSignal(ID, i, -1);
			}
		}
		else
		{
			for (u32 i = 0; i < socketCount; ++i)
			{
				pluggablesSystem->SetGameObjectOutputSignal(ID, i, -1);
			}
		}

		GameObject::Update();
	}

	void Terminal::Render()
	{
		PROFILE_AUTO("Terminal Render");

		if (m_DisplayReloadTimeRemaining != -1.0f)
		{
			m_DisplayReloadTimeRemaining -= g_DeltaTime;
			if (m_DisplayReloadTimeRemaining <= 0.0f)
			{
				m_DisplayReloadTimeRemaining = -1.0f;
			}
		}

		m_CursorBlinkTimer += g_DeltaTime;
		bool bRenderCursor = m_bBeingInteractedWith;
		if (fmod(m_CursorBlinkTimer, m_CursorBlinkRate) > m_CursorBlinkRate / 2.0f)
		{
			bRenderCursor = false;
		}
		if (!g_Window->HasFocus())
		{
			bRenderCursor = false;
		}
		bool bRenderText = !lines.empty();

		if (bRenderCursor || bRenderText)
		{
			BitmapFont* font = g_Renderer->SetFont(SID("editor-02-ws"));

			const real letterSpacing = 0.9f;
			const glm::vec3 right = m_Transform.GetRight();
			const glm::vec3 up = m_Transform.GetUp();
			const glm::vec3 forward = m_Transform.GetForward();

			const real width = 1.4f;
			const real height = 1.65f;
			const glm::vec3 posTL = m_Transform.GetWorldPosition() +
				right * (width / 2.0f) +
				up * height +
				forward * 1.05f;

			const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
			const real frameBufferScale = glm::max(1.0f / (real)frameBufferSize.x, 1.0f / (real)frameBufferSize.y);

			glm::vec3 pos = posTL;

			const glm::quat rot = m_Transform.GetWorldRotation();
			real charHeight = g_Renderer->GetStringHeight("W", font, false) * frameBufferScale * font->metaData.size * m_LetterScale;
			const real lineHeight = charHeight * m_LineHeight;
			real charWidth = g_Renderer->GetStringWidth("W", font, letterSpacing, false) * frameBufferScale * font->metaData.size * m_LetterScale;
			const real lineNoWidth = 3.0f * charWidth;

			if (bRenderCursor)
			{
				glm::vec3 cursorPos = pos;
				cursorPos += (right * (charWidth * (-cursor.x + 0.5f))) + up * ((cursor.y - scrollY) * -lineHeight);
				g_Renderer->DrawStringWS("|", VEC4_ONE, cursorPos, rot, letterSpacing, m_LetterScale);
			}

			if (bRenderText)
			{
				// TODO: EZ: Define in hot-reloadable config file
				static const glm::vec4 lineNumberColour(0.4f, 0.4f, 0.4f, 1.0f);
				static const glm::vec4 lineNumberColourActive(0.65f, 0.65f, 0.65f, 1.0f);
				static const glm::vec4 textColour(0.85f, 0.81f, 0.80f, 1.0f);
				static const glm::vec4 errorColour(0.65f, 0.12f, 0.13f, 1.0f);
				static const glm::vec4 stepColour(0.24f, 0.65f, 0.23f, 1.0f);
				static const glm::vec4 commentColour(0.35f, 0.35f, 0.35f, 1.0f);

				glm::vec3 firstLinePos = pos;
				i32 lineCount = (i32)lines.size();
				i32 firstRenderedLineIndex = (i32)scrollY;
				i32 lastRenderedLineIndex = firstRenderedLineIndex + glm::min(lineCount - (i32)scrollY, screenRowCount);
				for (i32 lineNumber = firstRenderedLineIndex; lineNumber < lastRenderedLineIndex; ++lineNumber)
				{
					std::string line = lines[lineNumber];
					size_t lineCommentIdx = line.find("//");
					glm::vec4 lineNoCol = (lineNumber == cursor.y ? lineNumberColourActive : lineNumberColour);
					g_Renderer->DrawStringWS(IntToString(lineNumber + 1, 2, ' '), lineNoCol, pos + right * lineNoWidth, rot, letterSpacing, m_LetterScale);
					if (lineCommentIdx == std::string::npos)
					{
						g_Renderer->DrawStringWS(lines[lineNumber], textColour, pos, rot, letterSpacing, m_LetterScale);
					}
					else
					{
						glm::vec3 tPos = pos;
						if (lineCommentIdx > 0)
						{
							std::string codeStr = lines[lineNumber].substr(0, lineCommentIdx);
							g_Renderer->DrawStringWS(codeStr, textColour, pos, rot, letterSpacing, m_LetterScale);
							tPos += -right * (g_Renderer->GetStringWidth(codeStr, font, letterSpacing, false) * frameBufferScale * font->metaData.size * m_LetterScale);
						}

						g_Renderer->DrawStringWS(lines[lineNumber].substr(lineCommentIdx), commentColour, tPos, rot, letterSpacing, m_LetterScale);
					}
					pos.y -= lineHeight;
				}

				if (m_VM != nullptr)
				{
					const std::vector<Diagnostic>& diagnostics = m_VM->GetDiagnosticContainer()->diagnostics;
					if (!diagnostics.empty())
					{
						for (u32 i = 0; i < (u32)diagnostics.size(); ++i)
						{
							const Span& span = diagnostics[i].span;
							pos = firstLinePos;
							pos.y -= lineHeight * diagnostics[i].span.lineNumber;
							g_Renderer->DrawStringWS("!", errorColour, pos + right * (charWidth * 1.f), rot, letterSpacing, m_LetterScale);
							u32 spanLen = glm::min(60u, glm::max(0u, (u32)span.Length()));
							std::string underlineStr = std::string(diagnostics[i].span.columnIndex, ' ') + std::string(spanLen, '_');
							pos.y -= lineHeight * 0.2f;
							g_Renderer->DrawStringWS(underlineStr, errorColour, pos, rot, letterSpacing, m_LetterScale);
						}
					}

					const bool bVMExecuting = m_VM->IsExecuting();
					i32 vmLineNumber = m_VM->CurrentLineNumber();
					if (bVMExecuting && vmLineNumber != -1)
					{
						pos = firstLinePos;
						pos.y -= lineHeight * vmLineNumber;
						u32 spanLen = glm::min(60u, glm::max(0u, (u32)lines[vmLineNumber].size()));
						std::string underlineStr = std::string(spanLen, '_');
						pos.y -= lineHeight * 0.2f;
						g_Renderer->DrawStringWS(underlineStr, stepColour, pos, rot, letterSpacing, m_LetterScale);
					}
				}
			}
		}
	}

	void Terminal::DrawImGuiWindow()
	{
		ImGui::Begin("Terminal");
		{
			ImGui::TextWrapped("Hit F5 to compile and evaluate code.");

			ImGui::Separator();

			if (m_ScriptFileName.empty())
			{
				ImGui::Text("Unsaved");
			}
			else
			{
				ImGui::Text("%s", m_ScriptFileName.c_str());
			}

			if (ImGui::Button(m_bDirtyFlag ? "Save *" : "Save"))
			{
				if (m_ScriptFileName.empty())
				{
					TerminalManager::OpenSavePopup(ID);
				}
				else
				{
					SaveScript();
				}
			}

			if (!m_ScriptFileName.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColour);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColour);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColour);

				ImGui::SameLine();
				if (ImGui::Button("Reload"))
				{
					ReloadScript();
				}

				ImGui::PopStyleColor(3);
			}

			ImGui::SameLine();

			bool bInstallTerminalWatch = g_EngineInstance->GetInstallTerminalWatch();
			if (ImGui::Checkbox("Auto-reload from disk", &bInstallTerminalWatch))
			{
				g_EngineInstance->SetInstallTerminalWatch(bInstallTerminalWatch);

			}

			ImGui::Separator();

			//ImGui::DragFloat("Line height", &m_LineHeight, 0.01f);
			//ImGui::DragFloat("Scale", &m_LetterScale, 0.01f);

			//ImGui::Text("Variables");
			//if (ImGui::BeginChild("", ImVec2(0.0f, 220.0f), true))
			//{
			//	for (i32 i = 0; i < lexer->context->variableCount; ++i)
			//	{
			//		const Context::InstantiatedIdentifier& var = lexer->context->instantiatedIdentifiers[i];
			//		std::string valStr = var.value->ToString();
			//		const char* typeNameCStr = g_TypeNameStrings[(i32)ValueTypeToTypeName(var.value->type)];
			//		ImGui::Text("%s = %s", var.name.c_str(), valStr.c_str());
			//		ImGui::SameLine();
			//		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
			//		ImGui::Text("(%s)", typeNameCStr);
			//		ImGui::PopStyleColor();
			//	}
			//}
			//ImGui::EndChild();

			bool bSuccess = m_VM->diagnosticContainer->diagnostics.empty();

			if (m_DisplayReloadTimeRemaining != -1.0f)
			{
				real alpha = glm::clamp(m_DisplayReloadTimeRemaining / m_ReloadPopupInfoDuration * 2.0f, 0.0f, 1.0f);
				ImVec4 textCol = ImVec4(0.6f, 0.9f, 0.66f, alpha);
				ImGui::PushStyleColor(ImGuiCol_Text, textCol);
				ImGui::Text("Reloaded code");
				ImGui::PopStyleColor();
			}
			else
			{
				ImGui::NewLine();
			}

			if (!bSuccess)
			{

				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
				for (const Diagnostic& diagnostic : m_VM->diagnosticContainer->diagnostics)
				{
					ImGui::TextWrapped("L%d: %s", diagnostic.span.lineNumber + 1, diagnostic.message.c_str());
				}
				ImGui::PopStyleColor();
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 0.5f, 1.0f));
				ImGui::Text("Success");
				ImGui::PopStyleColor();
			}

			{
				{
					ImVec2 regionAvail = ImGui::GetContentRegionAvail();
					real runtimeChildWindowHeight = glm::min(regionAvail.y, 200.0f);
					ImGui::BeginChildFrame(ImGui::GetID("registers"), ImVec2(regionAvail.x / 2.0f - 2, runtimeChildWindowHeight));
					{
						ImGui::Text("Registers");
						ImGui::BeginColumns("registers-columns", 2, ImGuiColumnsFlags_NoResize);
						real colWidth = regionAvail.x / 4.0f - 2.0f;
						ImGui::SetColumnWidth(0, colWidth);
						ImGui::SetColumnWidth(1, colWidth);
						for (u32 i = 0; i < (u32)m_VM->registers.size(); ++i)
						{
							const Variant& registerValue = m_VM->registers[i];
							if (registerValue.type != Variant::Type::_NONE)
							{
								std::string regValStr = registerValue.ToString();
								ImGui::Text("r%i = %s", i, regValStr.c_str());
							}
							else
							{
								ImGui::Text("r%i", i);
							}

							if (i == (u32)(m_VM->registers.size() / 2 - 1))
							{
								ImGui::NextColumn();
							}
						}
						ImGui::EndColumns(); // registers-columns
					}
					ImGui::EndChildFrame(); // runtime state

					ImGui::SameLine();

					ImGui::BeginChildFrame(ImGui::GetID("stack"), ImVec2(regionAvail.x / 2.0f - 2, runtimeChildWindowHeight));
					{
						ImGui::Text("Stack");
						// Copy of stack that we can pop elements off of to see contents
						std::stack<Variant> stackCopy = m_VM->stack;
						while (!stackCopy.empty())
						{
							const Variant& value = stackCopy.top();
							std::string valStr = value.ToString();
							ImGui::Text("%s", valStr.c_str());
							stackCopy.pop();
						}
					}
					ImGui::EndChildFrame(); // stack
				}

				for (u32 i = 0; i < Terminal::MAX_OUTPUT_COUNT; ++i)
				{
					const Variant& terminalVal = m_VM->terminalOutputs[i];
					if (terminalVal.type != Variant::Type::_NONE)
					{
						std::string terminalValStr = terminalVal.ToString();
						ImGui::Text("out_%i = %s", i, terminalValStr.c_str());
					}
					else
					{
						ImGui::NewLine();
					}
				}

				ImGui::Text("zf: %d sf: %d", m_VM->ZeroFlagSet() ? 1 : 0, m_VM->SignFlagSet() ? 1 : 0);

				ImGui::Text("Inst index: %d", m_VM->m_RunningState.instructionIdx);
				ImGui::SameLine();
				ImGui::Text("%s", (m_VM->m_RunningState.terminated ? "terminated" : ""));

				ImVec2 regionAvail = ImGui::GetContentRegionAvail();
				if (ImGui::BeginChild("source representations", ImVec2(regionAvail.x - 2, glm::max(regionAvail.y, 50.0f)), true))
				{
					if (!m_VM->instructionStr.empty())
					{
						ImGui::Text("Instructions");

						if (!m_VM->IsExecuting())
						{
							ImGui::SameLine();
							if (ImGui::Button("Start (F10)"))
							{
								m_VM->Execute(true);
							}
						}
						else
						{
							ImGui::SameLine();
							if (ImGui::Button("Step (F10)"))
							{
								m_VM->Execute(true);
							}
							ImGui::SameLine();
							if (ImGui::Button("Reset"))
							{
								m_VM->ClearRuntimeState();
							}
						}

						const ImVec2 preCursorPos = ImGui::GetCursorPos();
						ImGui::Text("%s", m_VM->instructionStr.c_str());
						const ImVec2 postCursorPos = ImGui::GetCursorPos();

						if (m_VM->IsExecuting())
						{
							i32 instIdx = m_VM->InstructionIndex();
							std::string underlineStr = instIdx == 0 ? "" : std::string(instIdx, '\n');
							underlineStr += "____________________";
							ImGui::SetCursorPos(ImVec2(preCursorPos.x, preCursorPos.y + 1.0f));
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.25f, 0.7f, 1.0f));
							ImGui::Text("%s", underlineStr.c_str());
							ImGui::PopStyleColor();

							ImGui::SetCursorPos(postCursorPos);
						}
					}

					if (!m_VM->unpatchedInstructionStr.empty())
					{
						ImGui::Separator();
						ImGui::Text("Instructions (unpatched)");
						const ImVec2 preCursorPos = ImGui::GetCursorPos();
						ImGui::Text("%s", m_VM->unpatchedInstructionStr.c_str());
						const ImVec2 postCursorPos = ImGui::GetCursorPos();

						if (m_VM->IsExecuting())
						{
							i32 strLineNum = 0;
							bool bFound = false;
							i32 instIdx = m_VM->InstructionIndex();
							for (i32 i = 0; i < (i32)m_VM->state->instructionBlocks.size(); ++i)
							{
								i32 startOffset = (i32)m_VM->state->instructionBlocks[i].startOffset;
								i32 lineCountInBlock = (i32)m_VM->state->instructionBlocks[i].instructions.size();
								if (instIdx >= startOffset &&
									instIdx < (startOffset + lineCountInBlock))
								{
									bFound = true;
									strLineNum += (instIdx - startOffset);
									break;
								}
								strLineNum += lineCountInBlock + 2; // Two additional lines for "label: {\n" & "\n}"
							}

							if (bFound)
							{
								strLineNum += 1;
								std::string underlineStr = strLineNum == 0 ? "" : std::string(strLineNum, '\n');
								underlineStr += "____________________";
								ImGui::SetCursorPos(ImVec2(preCursorPos.x, preCursorPos.y + 1.0f));
								ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.25f, 0.7f, 1.0f));
								ImGui::Text("%s", underlineStr.c_str());
								ImGui::PopStyleColor();

								ImGui::SetCursorPos(postCursorPos);
							}
						}
					}

					if (!m_VM->irStr.empty())
					{
						ImGui::Separator();
						ImGui::Text("IR");
						ImGui::Text("%s", m_VM->irStr.c_str());
					}

					if (!m_VM->astStr.empty())
					{
						ImGui::Separator();
						ImGui::Text("AST");
						ImGui::Text("%s", m_VM->astStr.c_str());
					}
				}
				ImGui::EndChild();
			}
		}
		ImGui::End();

		const char* externalScriptChangeStr = "Script changed externally";
		if (m_bShowExternalChangePopup)
		{
			m_bShowExternalChangePopup = false;
			ImGui::OpenPopup(externalScriptChangeStr);
		}

		if (ImGui::BeginPopupModal(externalScriptChangeStr, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Script %s was modified on disk, do you want to reload?", m_ScriptFileName.c_str());
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.2f, 1.0f));
			ImGui::Text("Warning: you will lose any changes you've made!");
			ImGui::PopStyleColor();

			if (ImGui::Button("Ignore external change"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Reload"))
			{
				m_bDirtyFlag = false;
				OnScriptChanged();

				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	bool Terminal::IsInteractable(Player* player)
	{
		Transform* playerTransform = player->GetTransform();
		glm::vec3 dPos = m_Transform.GetWorldPosition() - playerTransform->GetWorldPosition();
		real FoP = glm::dot(m_Transform.GetForward(), glm::normalize(dPos));
		real FoF = glm::dot(m_Transform.GetForward(), playerTransform->GetForward());
		// Ensure player is looking our direction and in front of us
		if (FoF < -0.15f && FoP < -0.35f)
		{
			return true;
		}

		return false;
	}

	void Terminal::SetBeingInteractedWith(Player* player)
	{
		m_bBeingInteractedWith = player != nullptr;
	}

	void Terminal::OnScriptChanged()
	{
		if (m_bDirtyFlag)
		{
			m_bShowExternalChangePopup = true;
			return;
		}

		cursor.x = cursor.y = cursorMaxX = 0;
		scrollY = 0.0f;
		m_CursorBlinkTimer = 0.0f;
		GetSystem<TerminalManager>(SystemType::TERMINAL_MANAGER)->LoadScript(m_ScriptFileName, lines);
		ParseCode();
		m_DisplayReloadTimeRemaining = m_ReloadPopupInfoDuration;
	}

	void Terminal::SetCamera(TerminalCamera* camera)
	{
		m_Camera = camera;
	}

	void Terminal::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject terminalObj = parentObject.GetObject("terminal");

		m_ScriptFileName = terminalObj.GetString("script file path");

		if (!m_ScriptFileName.empty())
		{
			if (!GetSystem<TerminalManager>(SystemType::TERMINAL_MANAGER)->LoadScript(m_ScriptFileName, lines))
			{
				PrintWarn("Failed to load terminal script from \"%s\"\n", m_ScriptFileName.c_str());
				lines.emplace_back("// Failed to load from script");
			}
		}

		if (lines.empty())
		{
			lines.emplace_back("");
		}

		MoveCursorToStart();
	}

	void Terminal::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		JSONObject terminalObj = {};

		SaveScript();

		SerializeField(terminalObj, bSerializePrefabData, "script file path", &m_ScriptFileName, ValueType::STRING);

		parentObject.fields.emplace_back("terminal", JSONValue(terminalObj));
	}

	void Terminal::TypeChar(char c)
	{
		m_CursorBlinkTimer = 0.0f;

		if (lines.empty())
		{
			lines.emplace_back("");
		}

		std::string& curLine = lines[cursor.y];

		if (c == '\n')
		{
			// Move remainder of line onto newline
			std::string remainderOfLine = curLine.substr(cursor.x);
			curLine.erase(curLine.begin() + cursor.x, curLine.end());
			lines.insert(lines.begin() + cursor.y + 1, remainderOfLine);
			m_CursorBlinkTimer = 0.0f;
			cursor.y += 1;
			cursor.x = 0;
			ClampCursorX();
			ScrollCursorToView();
			cursorMaxX = cursor.x;
		}
		else
		{
			if (cursor.x == (i32)curLine.size())
			{
				curLine.push_back(c);
			}
			else
			{
				curLine.insert(curLine.begin() + cursor.x, c);
			}
			MoveCursorRight();
			cursorMaxX = cursor.x;
		}
		m_bDirtyFlag = true;
		ParseCode();
	}

	void Terminal::DeleteChar(bool bDeleteUpToNextBreak /* = false */)
	{
		m_CursorBlinkTimer = 0.0f;
		if (lines.empty() || (cursor.x == 0 && cursor.y == 0))
		{
			return;
		}

		std::string& curLine = lines[cursor.y];

		if (cursor.x == 0)
		{
			if (cursor.y > 0)
			{
				// Move current line up to previous
				const i32 prevLinePLen = (i32)lines[cursor.y - 1].size();
				lines[cursor.y - 1].append(curLine);
				// TODO: Enforce screen width
				lines.erase(lines.begin() + cursor.y);
				cursor.x = prevLinePLen;
				MoveCursorUp();
				cursorMaxX = cursor.x;
			}
		}
		else
		{
			if (bDeleteUpToNextBreak)
			{
				i32 newX = glm::max(GetIdxOfPrevBreak(cursor.y, cursor.x), 0);
				curLine.erase(curLine.begin() + newX, curLine.begin() + cursor.x);
				cursor.x = newX;
			}
			else
			{
				curLine.erase(curLine.begin() + cursor.x - 1);
				cursor.x -= 1;
			}
			cursorMaxX = cursor.x;
			ClampCursorX();
		}
		m_bDirtyFlag = true;
		ParseCode();
	}

	void Terminal::DeleteCharInFront(bool bDeleteUpToNextBreak /* = false */)
	{
		m_CursorBlinkTimer = 0.0f;
		if (lines.empty())
		{
			return;
		}
		std::string& curLine = lines[cursor.y];
		if (cursor.x == (i32)curLine.size() && cursor.y == (i32)lines.size() - 1)
		{
			return;
		}

		if (cursor.x == (i32)curLine.size())
		{
			if (cursor.y < (i32)lines.size() - 1)
			{
				// Move next line up into current
				curLine.append(lines[cursor.y + 1]);
				lines.erase(lines.begin() + cursor.y + 1);
				// TODO: Enforce screen width
			}
		}
		else
		{

			if (bDeleteUpToNextBreak)
			{
				i32 newX = GetIdxOfNextBreak(cursor.y, cursor.x);
				curLine.erase(curLine.begin() + cursor.x, curLine.begin() + newX);
			}
			else
			{
				curLine.erase(curLine.begin() + cursor.x);
			}
		}
		m_bDirtyFlag = true;
		ParseCode();
	}

	void Terminal::MoveCursorToStart()
	{
		m_CursorBlinkTimer = 0.0f;
		cursor.y = 0;
		cursor.x = 0;
		cursorMaxX = cursor.x;
		scrollY = 0.0f;
	}

	void Terminal::MoveCursorToStartOfLine()
	{
		m_CursorBlinkTimer = 0.0f;
		cursor.x = 0;
		cursorMaxX = cursor.x;
	}

	void Terminal::MoveCursorToEnd()
	{
		m_CursorBlinkTimer = 0.0f;
		if (!lines.empty())
		{
			cursor.y = (i32)lines.size() - 1;
			cursor.x = (i32)lines[cursor.y].size();
		}
		else
		{
			cursor.x = cursor.y = 0;
		}
		cursorMaxX = cursor.x;

		i32 lineCount = (i32)lines.size();
		scrollY = (real)(lineCount - glm::min(lineCount, (i32)screenRowCount));
	}

	void Terminal::MoveCursorToEndOfLine()
	{
		m_CursorBlinkTimer = 0.0f;
		cursor.x = (i32)lines[cursor.y].size();
		cursorMaxX = cursor.x;
	}

	void Terminal::MoveCursorLeft(bool bSkipToNextBreak /* = false */)
	{
		m_CursorBlinkTimer = 0.0f;
		if (lines.empty() || (cursor.x == 0 && cursor.y == 0))
		{
			return;
		}

		if (bSkipToNextBreak)
		{
			if (cursor.x == 0 && cursor.y > 0)
			{
				cursor.x = INT_MAX;
				MoveCursorUp();
			}
			else
			{
				cursor.x = glm::max(GetIdxOfPrevBreak(cursor.y, cursor.x), 0);
			}
		}
		else
		{
			cursor.x -= 1;
			if (cursor.x < 0 && cursor.y > 0)
			{
				cursor.x = INT_MAX;
				MoveCursorUp();
			}
		}
		cursorMaxX = cursor.x;
	}

	void Terminal::MoveCursorRight(bool bSkipToNextBreak /* = false */)
	{
		m_CursorBlinkTimer = 0.0f;
		if (lines.empty())
		{
			return;
		}
		std::string& curLine = lines[cursor.y];
		if (cursor.x == (i32)curLine.size() && cursor.y == (i32)lines.size() - 1)
		{
			return;
		}

		if (bSkipToNextBreak)
		{
			if (cursor.x == (i32)curLine.size() && cursor.y < (i32)lines.size() - 1)
			{
				cursor.x = 0;
				cursorMaxX = cursor.x;
				MoveCursorDown();
			}
			else
			{
				cursor.x = GetIdxOfNextBreak(cursor.y, cursor.x);
			}
		}
		else
		{
			cursor.x += 1;
			if (cursor.x > (i32)curLine.size() && cursor.y < (i32)lines.size() - 1)
			{
				cursor.x = 0;
				cursorMaxX = 0;
				MoveCursorDown();
			}
		}
		cursorMaxX = cursor.x;
	}

	void Terminal::MoveCursorUp()
	{
		m_CursorBlinkTimer = 0.0f;
		if (cursor.y == 0)
		{
			cursor.x = 0;
			cursorMaxX = cursor.x;
		}
		else
		{
			cursor.y -= 1;
			cursor.x = glm::min(glm::max(cursor.x, cursorMaxX), (i32)lines[cursor.y].size());
			ScrollCursorToView();
		}
	}

	void Terminal::MoveCursorDown()
	{
		m_CursorBlinkTimer = 0.0f;
		if (cursor.y == (i32)lines.size() - 1)
		{
			cursor.x = (i32)lines[cursor.y].size();
			cursorMaxX = cursor.x;
		}
		else
		{
			cursor.y += 1;
			cursor.x = glm::min(glm::max(cursor.x, cursorMaxX), (i32)lines[cursor.y].size());
			ScrollCursorToView();
		}
	}

	void Terminal::ScrollCursorToView()
	{
		i32 cursorYInScreen = (i32)(cursor.y - scrollY);
		if (cursorYInScreen < 0)
		{
			// Scrolled up past top of screen
			scrollY = (real)cursor.y;

			// We've deleted the bottom-most line, so only one will be visible now,
			// Scroll up a (nearly) full page length
			if ((i32)scrollY == ((i32)lines.size() - 1))
			{
				scrollY = glm::max(0.0f, scrollY - (real)(screenRowCount - 1));
			}
		}
		else if (cursorYInScreen >= screenRowCount)
		{
			// Scrolled down past bottom of screen
			scrollY = (real)(cursor.y - screenRowCount + 1);
		}
	}

	void Terminal::PageUp()
	{
		if (cursor.y < screenRowCount)
		{
			MoveCursorToStart();
		}
		else
		{
			cursor.y -= screenRowCount;
			cursor.x = glm::min(glm::max(cursor.x, cursorMaxX), (i32)lines[cursor.y].size());
			scrollY = glm::max(0.0f, scrollY - (real)screenRowCount);
		}
	}

	void Terminal::PageDown()
	{
		i32 lineCount = (i32)lines.size();
		if (cursor.y >= (lineCount - screenRowCount - 1))
		{
			MoveCursorToEnd();
		}
		else
		{
			cursor.y += screenRowCount;
			cursor.x = glm::min(glm::max(cursor.x, cursorMaxX), (i32)lines[cursor.y].size());
			scrollY = glm::min((real)(lineCount - screenRowCount - 1), scrollY + (real)screenRowCount);
		}
	}

	void Terminal::ClampCursorX()
	{
		cursor.x = glm::clamp(cursor.x, 0, (i32)lines[cursor.y].size());
	}

	bool Terminal::SkipOverChar(char c)
	{
		if (isalpha(c) || isdigit(c))
		{
			return true;
		}

		return false;
	}

	i32 Terminal::GetIdxOfNextBreak(i32 y, i32 startX)
	{
		std::string& line = lines[y];

		i32 result = startX;
		while (result < (i32)line.size() && !SkipOverChar(line.at(result)))
		{
			result += 1;
		}

		while (result < (i32)line.size() && SkipOverChar(line.at(result)))
		{
			result += 1;
		}

		return result;
	}

	i32 Terminal::GetIdxOfPrevBreak(i32 y, i32 startX)
	{
		std::string& line = lines[y];

		i32 result = startX;
		while (result > 0 && !SkipOverChar(line.at(result - 1)))
		{
			result -= 1;
		}

		while (result > 0 && SkipOverChar(line.at(result - 1)))
		{
			result -= 1;
		}

		return result - 1;
	}

	void Terminal::ParseCode()
	{
		CHECK_NE(m_VM, nullptr);

		std::string str;
		for (const std::string& line : lines)
		{
			str.append(line);
			str.push_back('\n');
		}

		m_VM->GenerateFromSource(str.c_str());
	}

	void Terminal::EvaluateCode()
	{
		if (m_VM->IsCompiled())
		{
			m_VM->ClearRuntimeState();
			m_VM->Execute();
		}
	}

	bool Terminal::SaveScript()
	{
		if (!m_ScriptFileName.empty())
		{
			if (GetSystem<TerminalManager>(SystemType::TERMINAL_MANAGER)->SaveScript(m_ScriptFileName, lines))
			{
				m_bDirtyFlag = false;
				return true;
			}
			else
			{
				PrintError("Failed to save terminal script to %s\n", m_ScriptFileName.c_str());
			}
		}
		else
		{
			PrintError("Failed to save terminal script, file name not set\n");
		}

		return false;
	}

	void Terminal::ReloadScript()
	{
		if (!m_ScriptFileName.empty())
		{
			OnScriptChanged();
		}
		else
		{
			PrintError("Failed to reload terminal script, file name not set\n");
		}
	}

	EventReply Terminal::OnKeyEvent(KeyCode keyCode, KeyAction action, i32 modifiers)
	{
		if (m_Camera != nullptr)
		{
			if (m_Camera->IsTransitioningOut())
			{
				// Don't respond to input while camera is transitioning out
				return EventReply::UNCONSUMED;
			}

			if (action == KeyAction::KEY_PRESS || action == KeyAction::KEY_REPEAT)
			{
				const bool bCapsLock = (modifiers & (i32)InputModifier::CAPS_LOCK) > 0;
				const bool bShiftDown = (modifiers & (i32)InputModifier::SHIFT) > 0;
				const bool bCtrlDown = (modifiers & (i32)InputModifier::CONTROL) > 0;
				if (keyCode == KeyCode::KEY_PAGE_UP)
				{
					PageUp();
					return EventReply::CONSUMED;
				}
				else if (keyCode == KeyCode::KEY_PAGE_DOWN)
				{
					PageDown();
					return EventReply::CONSUMED;
				}
				else if (keyCode == KeyCode::KEY_F7)
				{
					ParseCode();
					return EventReply::CONSUMED;
				}
				else if (keyCode == KeyCode::KEY_F5)
				{
					ParseCode();
					EvaluateCode();
					return EventReply::CONSUMED;
				}
				else if (keyCode == KeyCode::KEY_F10)
				{
					m_VM->Execute(true);
					return EventReply::CONSUMED;
				}
				else if (keyCode == KeyCode::KEY_ESCAPE)
				{
					m_Camera->TransitionOut();
					return EventReply::CONSUMED;
				}
				else if (keyCode == KeyCode::KEY_SLASH)
				{
					if (bCtrlDown) // Comment line
					{
						i32 pCursorPosX = cursor.x;
						cursor.x = 0;
						std::string strippedLine = TrimLeadingWhitespace(lines[cursor.y]);
						i32 leadingWhitespaceCount = (i32)(strippedLine.size() - lines[cursor.y].size());
						if (strippedLine.size() < 2)
						{
							TypeChar('/');
							TypeChar('/');
							cursor.x = pCursorPosX + 2;
						}
						else
						{
							cursor.x = leadingWhitespaceCount;
							if (strippedLine[0] == '/' && strippedLine[1] == '/')
							{
								DeleteCharInFront(false);
								DeleteCharInFront(false);
								cursor.x = pCursorPosX - 2;
							}
							else
							{

								TypeChar('/');
								TypeChar('/');
								cursor.x = pCursorPosX + 2;
							}
						}
					}
					else if (bShiftDown)
					{
						TypeChar(InputManager::GetShiftModifiedKeyCode('/'));
					}
					else
					{
						TypeChar('/');
					}
					return EventReply::CONSUMED;
				}

				// Most letters, numbers, & characters
				if ((i32)keyCode >= (i32)KeyCode::KEY_APOSTROPHE && (i32)keyCode <= (i32)KeyCode::KEY_RIGHT_BRACKET)
				{
					char c = KeyCodeStrings[(i32)keyCode][0];
					if (isalpha(c))
					{
						if (bCtrlDown)
						{
							return EventReply::UNCONSUMED;
						}

						if (!bShiftDown && !bCapsLock)
						{
							c = (char)tolower(c);
						}
					}
					else
					{
						if (bShiftDown)
						{
							c = InputManager::GetShiftModifiedKeyCode(c);
						}
					}
					TypeChar(c);
					return EventReply::CONSUMED;
				}
				if (keyCode >= KeyCode::KEY_KP_0 && keyCode <= KeyCode::KEY_KP_9)
				{
					if (bShiftDown)
					{
						if (keyCode == KeyCode::KEY_KP_1)
						{
							// End
							if (bCtrlDown)
							{
								MoveCursorToEnd();
							}
							else
							{
								MoveCursorToEndOfLine();
							}
						}
						if (keyCode == KeyCode::KEY_KP_3)
						{
							// TODO: Pg Down
						}
						if (keyCode == KeyCode::KEY_KP_7)
						{
							// Home
							if (bCtrlDown)
							{
								MoveCursorToStart();
							}
							else
							{
								MoveCursorToStartOfLine();
							}
						}
						if (keyCode == KeyCode::KEY_KP_9)
						{
							// TODO: Pg Up
						}
						if (keyCode == KeyCode::KEY_KP_2)
						{
							MoveCursorDown();
						}
						if (keyCode == KeyCode::KEY_KP_4)
						{
							MoveCursorLeft();
						}
						if (keyCode == KeyCode::KEY_KP_6)
						{
							MoveCursorRight();
						}
						if (keyCode == KeyCode::KEY_KP_8)
						{
							MoveCursorUp();
						}

						return EventReply::CONSUMED;
					}
					else
					{
						if (!bCtrlDown)
						{
							char c = (char)('0' + ((i32)keyCode - (i32)KeyCode::KEY_KP_0));
							TypeChar(c);
							return EventReply::CONSUMED;
						}
					}
					return EventReply::UNCONSUMED;
				}
				if (keyCode == KeyCode::KEY_KP_MULTIPLY)
				{
					TypeChar('*');
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_KP_DIVIDE)
				{
					TypeChar('/');
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_KP_ADD)
				{
					TypeChar('+');
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_KP_SUBTRACT)
				{
					TypeChar('-');
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_KP_DECIMAL)
				{
					if (bShiftDown)
					{
						DeleteCharInFront(bCtrlDown);
					}
					else
					{
						TypeChar('.');
					}
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_SPACE)
				{
					TypeChar(' ');
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_TAB)
				{
					TypeChar(' ');
					TypeChar(' ');
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_ENTER ||
					keyCode == KeyCode::KEY_KP_ENTER)
				{
					TypeChar('\n');
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_BACKSPACE)
				{
					DeleteChar(bCtrlDown);
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_DELETE)
				{
					DeleteCharInFront(bCtrlDown);
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_HOME)
				{
					if (bCtrlDown)
					{
						MoveCursorToStart();
					}
					else
					{
						MoveCursorToStartOfLine();
					}
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_END)
				{
					if (bCtrlDown)
					{
						MoveCursorToEnd();
					}
					else
					{
						MoveCursorToEndOfLine();
					}
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_LEFT)
				{
					MoveCursorLeft(bCtrlDown);
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_RIGHT)
				{
					MoveCursorRight(bCtrlDown);
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_UP)
				{
					MoveCursorUp();
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_DOWN)
				{
					MoveCursorDown();
					return EventReply::CONSUMED;
				}
			}
		}

		return EventReply::UNCONSUMED;
	}
} // namespace flex
