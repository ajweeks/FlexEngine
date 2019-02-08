#pragma once

#include "Audio/RandomizedAudioSource.hpp"
#include "Callbacks/InputCallbacks.hpp"
#include "Spring.hpp"
#include "Transform.hpp"

class btCollisionShape;

namespace flex
{
	class BaseScene;
	class MeshComponent;
	class BezierCurveList;
	class TerminalCamera;

	class GameObject
	{
	public:
		GameObject(const std::string& name, GameObjectType type);
		virtual ~GameObject();

		// Returns a new game object which is a direct copy of this object, parented to parent
		// If parent == nullptr then new object will have same parent as this object
		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren);

		static GameObject* CreateObjectFromJSON(const JSONObject& obj, BaseScene* scene, MaterialID overriddenMatID = InvalidMaterialID);

		virtual void Initialize();
		virtual void PostInitialize();
		virtual void Destroy();
		virtual void Update();

		virtual void DrawImGuiObjects();
		// Returns true if this object was deleted through the context menu
		virtual bool DoImGuiContextMenu(bool bActive);
		virtual bool DoDuplicateGameObjectButton(const char* buttonName);

		virtual Transform* GetTransform();
		virtual const Transform* GetTransform() const;

		virtual bool AllowInteractionWith(GameObject* gameObject);
		virtual void SetInteractingWith(GameObject* gameObject);
		bool IsBeingInteractedWith() const;

		GameObject* GetObjectInteractingWith();

		JSONObject Serialize(const BaseScene* scene) const;
		void ParseJSON(const JSONObject& obj, BaseScene* scene, MaterialID overriddenMatID = InvalidMaterialID);

		void RemoveRigidBody();

		void SetParent(GameObject* parent);
		GameObject* GetParent();
		void DetachFromParent();

		// Returns a list of objects, starting with the root, going up to this object
		std::vector<GameObject*> GetParentChain();

		// Walks up the tree to the highest parent
		GameObject* GetRootParent();

		GameObject* AddChild(GameObject* child);
		bool RemoveChild(GameObject* child);
		const std::vector<GameObject*>& GetChildren() const;

		bool HasChild(GameObject* child, bool bCheckChildrensChildren);

		void UpdateSiblingIndices(i32 myIndex);
		i32 GetSiblingIndex() const;

		// Returns all objects who share our parent
		std::vector<GameObject*> GetAllSiblings();
		// Returns all objects who share our parent and have a larger sibling index
		std::vector<GameObject*> GetEarlierSiblings();
		// Returns all objects who share our parent and have a smaller sibling index
		std::vector<GameObject*> GetLaterSiblings();

		void AddTag(const std::string& tag);
		bool HasTag(const std::string& tag);
		std::vector<std::string> GetTags() const;

		RenderID GetRenderID() const;
		void SetRenderID(RenderID renderID);

		std::string GetName() const;
		void SetName(const std::string& newName);

		bool IsSerializable() const;
		void SetSerializable(bool bSerializable);

		bool IsStatic() const;
		void SetStatic(bool bStatic);

		bool IsVisible() const;
		void SetVisible(bool bVisible, bool effectChildren = true);

		// If bIncludingChildren is true, true will be returned if this or any children are visible in scene explorer
		bool IsVisibleInSceneExplorer(bool bIncludingChildren = false) const;
		void SetVisibleInSceneExplorer(bool bVisibleInSceneExplorer);

		bool HasUniformScale() const;
		void SetUseUniformScale(bool bUseUniformScale, bool bEnforceImmediately);

		btCollisionShape* SetCollisionShape(btCollisionShape* collisionShape);
		btCollisionShape* GetCollisionShape() const;

		RigidBody* SetRigidBody(RigidBody* rigidBody);
		RigidBody* GetRigidBody() const;

		MeshComponent* GetMeshComponent();
		MeshComponent* SetMeshComponent(MeshComponent* meshComponent);

		// Called when another object has begun to overlap
		void OnOverlapBegin(GameObject* other);
		// Called when another object is no longer overlapping
		void OnOverlapEnd(GameObject* other);

		GameObjectType GetType() const;

		void AddSelfAndChildrenToVec(std::vector<GameObject*>& vec);
		void RemoveSelfAndChildrenToVec(std::vector<GameObject*>& vec);

		// Filled if this object is a trigger
		std::vector<GameObject*> overlappingObjects;

	protected:
		friend BaseScene;

		static const char* s_DefaultNewGameObjectName;

		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID);
		virtual void SerializeUniqueFields(JSONObject& parentObject) const;

		void CopyGenericFields(GameObject* newGameObject, GameObject* parent, bool bCopyChildren);

		// Returns a string containing our name with a "_xx" post-fix where xx is the next highest index or 00

		std::string m_Name;

		std::vector<std::string> m_Tags;

		Transform m_Transform;
		RenderID m_RenderID = InvalidRenderID;

		GameObjectType m_Type = GameObjectType::_NONE;

		/*
		* If true, this object will be written out to file
		* NOTE: If false, children will also not be serialized
		*/
		bool m_bSerializable = true;

		/*
		* Whether or not this object should be rendered
		* NOTE: Does *not* effect childrens' visibility
		*/
		bool m_bVisible = true;

		/*
		* Whether or not this object should be shown in the scene explorer UI
		* NOTE: Children are also hidden when this if false!
		*/
		bool m_bVisibleInSceneExplorer = true;

		/*
		* True if and only if this object will never move
		* If true, this object will be rendered to reflection probes
		*/
		bool m_bStatic = false;

		/*
		* If true this object will not collide with other game objects
		* Overlapping objects will cause OnOverlapBegin/End to be called
		*/
		bool m_bTrigger = false;

		/*
		* True if this object can currently be interacted with (can be based on
		* player proximity, among other things)
		*/
		bool m_bInteractable = false;

		bool m_bLoadedFromPrefab = false;

		bool m_bBeingInteractedWith = false;

		// Editor only
		bool m_bUniformScale = false;

		std::string m_PrefabName;

		/*
		* Will point at the player we're interacting with, or the object if we're the player
		*/
		GameObject* m_ObjectInteractingWith = nullptr;

		i32 m_SiblingIndex = 0;

		btCollisionShape* m_CollisionShape = nullptr;
		RigidBody* m_RigidBody = nullptr;

		GameObject* m_Parent = nullptr;
		std::vector<GameObject*> m_Children;

		MeshComponent* m_MeshComponent = nullptr;

		static AudioSourceID s_BunkSound;
		static RandomizedAudioSource s_SqueakySounds;

	};

	// Child classes

	class DirectionalLight : public GameObject
	{
	public:
		DirectionalLight();
		DirectionalLight(const std::string& name);

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void DrawImGuiObjects() override;

		bool operator==(const DirectionalLight& other);

		void SetPos(const glm::vec3& pos);
		glm::vec3 GetPos() const;
		void SetRot(const glm::quat& rot);
		glm::quat GetRot() const;

		glm::vec4 color = VEC4_ONE;
		real brightness = 1.0f;

		real shadowDarkness = 1.0f;
		bool bCastShadow = true;
		real shadowMapNearPlane = -80.0f;
		real shadowMapFarPlane = 100.0f;
		real shadowMapZoom = 30.0f;

		// DEBUG: (just for preview in ImGui)
		u32 shadowTextureID = 0;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;
	};

	class PointLight : public GameObject
	{
	public:
		PointLight(BaseScene* scene);
		PointLight(const std::string& name);

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void DrawImGuiObjects() override;

		bool operator==(const PointLight& other);

		void SetPos(const glm::vec3& pos);
		glm::vec3 GetPos() const;

		glm::vec4 color = VEC4_ONE;
		real brightness = 500.0f;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;
	};

	class Valve : public GameObject
	{
	public:
		Valve(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void PostInitialize() override;
		virtual void Update() override;

		// Serialized fields
		real minRotation = 0.0f;
		real maxRotation = 0.0f;

		// Non-serialized fields
		// Multiplied with value retrieved from input manager
		real rotationSpeedScale = 1.0f;

		// 1 = never slow down, 0 = slow down immediately
		real invSlowDownRate = 0.85f;

		real rotationSpeed = 0.0f;
		real pRotationSpeed = 0.0f;

		real rotation = 0.0f;
		real pRotation = 0.0f;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class RisingBlock : public GameObject
	{
	public:
		RisingBlock(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Update() override;

		// Serialized fields
		Valve* valve = nullptr; // (object name is serialized)
		glm::vec3 moveAxis;

		// If true this block will "fall" to its minimum
		// value when a player is not interacting with it
		bool bAffectedByGravity = false;

		// Non-serialized fields
		glm::vec3 startingPos;

		real pdDistBlockMoved = 0.0f;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class GlassPane : public GameObject
	{
	public:
		GlassPane(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		bool bBroken = false;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class ReflectionProbe : public GameObject
	{
	public:
		ReflectionProbe(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void PostInitialize() override;

		MaterialID captureMatID = 0;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class Skybox : public GameObject
	{
	public:
		Skybox(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class EngineCart;

	class Cart : public GameObject
	{
	public:
		Cart(CartID cartID, GameObjectType type = GameObjectType::CART);
		Cart(CartID cartID, const std::string& name, GameObjectType type = GameObjectType::CART, const char* meshName = emptyCartMeshName);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void DrawImGuiObjects() override;
		virtual real GetDrivePower() const;

		void OnTrackMount(TrackID trackID, real newDistAlongTrack);
		void OnTrackDismount();

		void SetItemHolding(GameObject* obj);
		void RemoveItemHolding();

		// Advances along track, rotates to face correct direction
		void AdvanceAlongTrack(real dT);

		// Returns velocity
		real UpdatePosition();

		CartID cartID = InvalidCartID;

		TrackID currentTrackID = InvalidTrackID;
		real distAlongTrack = -1.0f;
		real velocityT = 1.0f;

		real distToRearNeighbor = -1.0f;

		// Non-serialized fields
		real attachThreshold = 1.5f;

		Spring<real> m_TSpringToCartAhead;

		CartChainID chainID = InvalidCartChainID;

		static const char* emptyCartMeshName;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class EngineCart : public Cart
	{
	public:
		EngineCart(CartID cartID);
		EngineCart(CartID cartID, const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void Update() override;
		virtual void DrawImGuiObjects() override;
		virtual real GetDrivePower() const override;


		real moveDirection = 1.0f; // -1.0f or 1.0f
		real powerRemaining = 1.0f;

		real powerDrainMultiplier = 0.1f;
		real speed = 0.1f;

		static const char* engineMeshName;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class MobileLiquidBox : public GameObject
	{
	public:
		MobileLiquidBox();
		MobileLiquidBox(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void DrawImGuiObjects() override;

		bool bInCart = false;
		real liquidAmount = 0.0f;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	enum class BasicType
	{
		INT,
		BOOL,
		FLOAT,
		VAR,
		REG,
		INPUT,
		OUTPUT,

		// uint
		// short
		// byte
		// double
		// string ?
	};

	enum class Operator
	{
		ADD,
		SUB,
		MUL,
		DIV,
		MOD,
		ASSIGN,
		B_AND,
		B_OR,
		B_XOR,

		/*
		- [] operator
		- unary operands
		- +=, -=, *=, /=
		- ! operator
		- parentheses
		- power
		- fmod
		*/

		_NONE
	};

	struct Value
	{
		BasicType type;

		VariableID id = InvalidVariableID;
		std::string name;
		int intVal;
		bool boolVal;
		float realVal;
		VariableID varVal;
	};

	struct TreeNode
	{
		Value val;
		Operator op = Operator::_NONE;
		TreeNode* leaf1 = nullptr;
		TreeNode* leaf2 = nullptr;
	};

	struct Error
	{
		Error(i32 num, const std::string& str) :
			lineNumber(num),
			str(str)
		{}

		i32 lineNumber;
		std::string str;
	};

	enum class TokenType
	{
		LABEL,
		SINGLE_COLON,
		DOUBLE_COLON,
		SEMICOLON,
		BANG,
		TERNARY,
		NUMBER,
		ASSIGNMENT,
		//FUNCTION_DEF,
		//FUNCITON_CALL,
		OPEN_PAREN,
		CLOSE_PAREN,
		OPEN_BRACKET,
		CLOSE_BRACKET,
		OPEN_SQUARE_BRACKET,
		CLOSE_SQUARE_BRACKET,
		ADD,
		ADD_ASSIGN,
		SUBTRACT,
		SUBTRACT_ASSIGN,
		MULTIPLY,
		MULTIPLY_ASSIGN,
		DIVIDE,
		DIVIDE_ASSIGN,
		MODULO,
		MODULO_ASSIGN,
		LESS,
		LESS_EQ,
		GREATER,
		GREATER_EQ,
		EQUAL_TEST,
		NOT_EQUAL_TEST,
		BOOLEAN_AND,
		BOOLEAN_OR,
		BINARY_AND,
		BINARY_AND_ASSIGN,
		BINARY_OR,
		BINARY_OR_ASSIGN,
		BINARY_XOR,
		BINARY_XOR_ASSIGN,
		STRING,
		TILDE,
		BACK_QUOTE,
		DOT,

		// Keywords:
		//KEY_RETURN,
		KEY_INT,
		//KEY_FLOAT,
		KEY_BOOL,
		//KEY_STRING,
		KEY_TRUE,
		KEY_FALSE,
		KEY_IF,
		KEY_ELIF,
		KEY_ELSE,
		KEY_DO,
		KEY_WHILE,
		KEY_BREAK,

		_NONE
	};

	struct Token
	{
		TokenType type;
		union
		{
			int intVal;
			bool boolVal;
			float floatVal;
			//std::string strVal;
			//void* voidVal;
		} data;
	};

	struct TokenString
	{
		i32 lineNum;
		i32 linePos;
		i32 TokenID;
		TokenType type;
		char const* charPtr;
		i32 len;

		std::string ToString()
		{
			return std::string(charPtr, len);
		}
	};

	struct TokenContext
	{
		const char* buffer = nullptr;
		i32 bufferLen = -1;
		char const* bufferPtr = nullptr;
		std::string errorReason;
		i32 linePos = 0;
		i32 lineNumber = 0;

		char ConsumeNextChar()
		{
			assert((bufferPtr + 1 - buffer) <= bufferLen);

			char nextChar = bufferPtr[0];
			bufferPtr++;
			linePos++;
			if (nextChar == '\n')
			{
				linePos = 0;
				lineNumber++;
			}
			return nextChar;
		}

		char PeekNextChar() const
		{
			assert((bufferPtr - buffer) <= bufferLen);

			return bufferPtr[0];
		}

		char PeekChar(i32 index) const
		{
			assert(index >= 0 && index < GetRemainingLength());

			return bufferPtr[index];
		}

		i32 GetRemainingLength() const
		{
			return bufferLen - (bufferPtr - buffer);
		}

		bool HasNextChar() const
		{
			return GetRemainingLength() > 0;
		}

		bool CanNextCharBeLabelPart() const;

		TokenType AttemptParseKeyword(const char* keywordStr, TokenType keywordType);
		TokenType AttemptParseKeywords(const char* potentialKeywordStrs[], TokenType potentialKeywordTypes[], i32 pos[], i32 potentialCount);

	};

	struct Tokenizer
	{
		Tokenizer(const std::string& code);

		TokenString GetNextToken();

		void ConsumeWhitespaceAndComments();
		TokenType IsNextChar(char c, TokenType ifYes, TokenType ifNo);

		std::string str;
		TokenContext context;

		i32 nextTokenID = 0;
	};

	class Terminal : public GameObject
	{
	public:
		Terminal();
		Terminal(const std::string& name);

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void Update() override;
		virtual void DrawImGuiObjects() override;

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual bool AllowInteractionWith(GameObject* gameObject) override;

		void SetCamera(TerminalCamera* camera);

	protected:
		void TypeChar(char c);
		void DeleteChar(bool bDeleteUpToNextBreak = false); // (backspace)
		void DeleteCharInFront(bool bDeleteUpToNextBreak = false); // (delete)
		void Clear();

		void MoveCursorToStart();
		void MoveCursorToStartOfLine();
		void MoveCursorToEnd();
		void MoveCursorToEndOfLine();
		void MoveCursorLeft(bool bSkipToNextBreak = false);
		void MoveCursorRight(bool bSkipToNextBreak = false);
		void MoveCursorUp();
		void MoveCursorDown();

		void ClampCursorX();

		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	private:
		friend TerminalCamera;

		EventReply OnKeyEvent(KeyCode keyCode, KeyAction action, i32 modifiers);
		KeyEventCallback<Terminal> m_KeyEventCallback;

		bool SkipOverChar(char c);
		i32 GetIdxOfNextBreak(i32 y, i32 startX);
		i32 GetIdxOfPrevBreak(i32 y, i32 startX);

		void ParseCode();

		std::vector<std::string> lines;
		bool bParsePassed = false;

		glm::vec2i cursor;
		// Keeps track of the cursor x to be able to position the cursor correctly
		// after moving from a long line, over a short line, onto a longer line again
		i32 cursorMaxX = 0;

		// Non-serialized fields:
		TerminalCamera* m_Camera = nullptr;
		const i32 m_CharsWide = 45;

		const sec m_CursorBlinkRate = 0.6f;
		sec m_CursorBlinkTimer = 0.0f;

	};

} // namespace flex
