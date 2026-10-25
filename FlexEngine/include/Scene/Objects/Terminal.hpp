#pragma once

#include "Scene/GameObject.hpp"

// In-world programmable terminals running VM scripts
namespace flex
{
	class Terminal final : public GameObject
	{
	public:
		Terminal();
		explicit Terminal(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;
		virtual void Render() override;

		bool IsInteractable(Player* player);
		void SetBeingInteractedWith(Player* player);

		void OnScriptChanged();
		void SetCamera(TerminalCamera* camera);
		void DrawImGuiWindow();

		static const i32 MAX_OUTPUT_COUNT = 4;

		std::vector<Wire*> wireSlots;

	protected:
		void TypeChar(char c);
		void DeleteChar(bool bDeleteUpToNextBreak = false); // (backspace)
		void DeleteCharInFront(bool bDeleteUpToNextBreak = false); // (delete)

		void MoveCursorToStart();
		void MoveCursorToStartOfLine();
		void MoveCursorToEnd();
		void MoveCursorToEndOfLine();
		void MoveCursorLeft(bool bSkipToNextBreak = false);
		void MoveCursorRight(bool bSkipToNextBreak = false);
		void MoveCursorUp();
		void MoveCursorDown();
		void ScrollCursorToView();
		void PageUp();
		void PageDown();

		void ClampCursorX();

		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

	private:
		friend TerminalCamera;
		friend TerminalManager;

		EventReply OnKeyEvent(KeyCode keyCode, KeyAction action, i32 modifiers);
		KeyEventCallback<Terminal> m_KeyEventCallback;

		bool SkipOverChar(char c);
		i32 GetIdxOfNextBreak(i32 y, i32 startX);
		i32 GetIdxOfPrevBreak(i32 y, i32 startX);

		void ParseCode();
		void EvaluateCode();

		bool SaveScript();
		void ReloadScript();

		std::string m_ScriptFileName;

		// Non-serialized fields:
		VM::VirtualMachine* m_VM = nullptr;

		std::vector<std::string> lines;
		bool m_bDirtyFlag = false;
		bool m_bShowExternalChangePopup = false;
		real m_DisplayReloadTimeRemaining = -1.0f; // When positive show text indicating reload occurred
		const real m_ReloadPopupInfoDuration = 2.5f;

		real m_LineHeight = 1.0f;
		real m_LetterScale = 0.04f;

		glm::vec2i cursor;
		// Keeps track of the cursor x to be able to position the cursor correctly
		// after moving from a long line, over a short line, onto a longer line again
		i32 cursorMaxX = 0;
		real scrollY = 0.0f;
		i32 screenRowCount = 31;

		TerminalCamera* m_Camera = nullptr;
		//const i32 m_Columns = 45;

		bool m_bBeingInteractedWith = false;

		const sec m_CursorBlinkRate = 0.6f;
		sec m_CursorBlinkTimer = 0.0f;

	};
} // namespace flex
