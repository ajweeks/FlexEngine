#pragma once

#include "Audio/RandomizedAudioSource.hpp"
#include "Callbacks/InputCallbacks.hpp"
#include "Graphics/RendererTypes.hpp"
#include "Graphics/VertexBufferData.hpp" // For VertexBufferData::CreateInfo
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

		virtual void OnTransformChanged();

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
		virtual void SetVisible(bool bVisible, bool bEffectChildren = true);

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

		bool CastsShadow() const;
		void SetCastsShadow(bool bCastsShadow);

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

		bool m_bCastsShadow = true;

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
		explicit DirectionalLight(const std::string& name);

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void DrawImGuiObjects() override;
		virtual void SetVisible(bool bVisible, bool bEffectChildren /* = true */) override;
		virtual void OnTransformChanged() override;

		bool operator==(const DirectionalLight& other);

		void SetPos(const glm::vec3& newPos);
		glm::vec3 GetPos() const;
		void SetRot(const glm::quat& newRot);
		glm::quat GetRot() const;

		DirLightData data;

		// DEBUG:
		glm::vec3 pos = VEC3_ZERO;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;
	};

	class PointLight : public GameObject
	{
	public:
		explicit PointLight(BaseScene* scene);
		explicit PointLight(const std::string& name);

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void DrawImGuiObjects() override;
		virtual void SetVisible(bool bVisible, bool bEffectChildren /* = true */) override;
		virtual void OnTransformChanged() override;

		bool operator==(const PointLight& other);

		void SetPos(const glm::vec3& pos);
		glm::vec3 GetPos() const;

		PointLightData data;
		PointLightID ID = InvalidPointLightID;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;
	};

	class Valve : public GameObject
	{
	public:
		explicit Valve(const std::string& name);

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
		explicit RisingBlock(const std::string& name);

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
		explicit GlassPane(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		bool bBroken = false;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class ReflectionProbe : public GameObject
	{
	public:
		explicit ReflectionProbe(const std::string& name);

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
		explicit Skybox(const std::string& name);

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
		explicit EngineCart(CartID cartID);
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
		explicit MobileLiquidBox(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void DrawImGuiObjects() override;

		bool bInCart = false;
		real liquidAmount = 0.0f;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class GerstnerWave : public GameObject
	{
	public:
		explicit GerstnerWave(const std::string& name);

		virtual void Update() override;
		void AddWave();
		void RemoveWave(i32 index);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void DrawImGuiObjects() override;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

		void UpdateDependentVariables(i32 waveIndex);

		i32 vertSideCount = 100;
		real size = 30.0f;
		VertexBufferData::CreateInfo bufferInfo;

		struct WaveInfo
		{
			bool enabled = true;
			real a = 0.35f;
			real waveDirTheta = 0.5f;
			real waveLen = 5.0f;

			// Non-serialized, calculated from fields above
			real waveDirCos;
			real waveDirSin;
			real moveSpeed = -1.0f;
			real waveVecMag = -1.0f;
			real accumOffset = 0.0f;
		};

		std::vector<WaveInfo> waves;

		GameObject* bobber = nullptr;
		Spring<real> bobberTarget;

	};

	class Blocks : public GameObject
	{
	public:
		explicit Blocks(const std::string& name);

		virtual void Update() override;

	protected:

	};

	struct Tokenizer;
	struct Value;
	struct Expression;
	struct Statement;
	struct IfStatement;
	struct WhileStatement;
	enum class TypeName;

	enum class OperatorType
	{
		ASSIGN,
		ADD,
		SUB,
		MUL,
		DIV,
		MOD,
		BIN_AND,
		BIN_OR,
		BIN_XOR,
		EQUAL,
		NOT_EQUAL,
		GREATER,
		GREATER_EQUAL,
		LESS,
		LESS_EQUAL,
		BOOLEAN_AND,
		BOOLEAN_OR,
		NEGATE,

		/*
		- [] operator
		- +=, -=, *=, /=
		- ! operator
		- parentheses
		- power
		- fmod
		*/

		_NONE
	};

	bool ValidOperatorOnType(OperatorType op, TypeName type);

	struct Operator
	{
		static OperatorType Parse(Tokenizer& tokenizer);
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
		ROOT,

		IDENTIFIER,
		SINGLE_COLON,
		DOUBLE_COLON,
		SEMICOLON,
		BANG,
		TERNARY,
		INT_LITERAL,
		FLOAT_LITERAL,
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
		EQUAL_TEST,
		NOT_EQUAL_TEST,
		GREATER,
		GREATER_EQUAL,
		LESS,
		LESS_EQUAL,
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

		KEYWORDS_START, // Not a valid type!
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
		//KEY_CONST,
		KEYWORDS_END, // Not a valid type!

		_NONE
	};

	static const char* g_KeywordStrings[] =
	{
		//"return",
		"int",
		//"float",
		"bool",
		"true",
		"false",
		"if",
		"elif",
		"else",
		"do",
		"while",
		"break",
		//"const",
	};

	static_assert(ARRAY_LENGTH(g_KeywordStrings) == (u32)((u32)TokenType::KEYWORDS_END - (u32)TokenType::KEYWORDS_START - 1), "Length of g_KeywordStrings must match number of keyword entries in TokenType enum");

	struct Token
	{
		std::string ToString() const;

		i32 lineNum;
		i32 linePos;
		i32 tokenID;
		TokenType type;
		char const* charPtr;
		i32 len;
	};

	extern Token g_EmptyToken;

	struct TokenContext
	{
		TokenContext();
		~TokenContext();

		void Reset();

		bool HasNextChar() const;
		char ConsumeNextChar();
		char PeekNextChar() const;
		char PeekChar(i32 index) const;
		i32 GetRemainingLength() const;

		bool CanNextCharBeIdentifierPart() const;

		TokenType AttemptParseKeyword(const char* keywordStr, TokenType keywordType);
		TokenType AttemptParseKeywords(const char* potentialKeywordStrs[], TokenType potentialKeywordTypes[], i32 pos[], i32 potentialCount);

		Value* InstantiateIdentifier(const Token& identifierToken, TypeName typeName);
		Value* GetVarInstanceFromToken(const Token& token);

		static bool IsKeyword(const char* str);
		static bool IsKeyword(TokenType type);

		const char* buffer = nullptr;
		i32 bufferLen = -1;
		char const* bufferPtr = nullptr;
		std::string errorReason;
		Token errorToken;
		i32 linePos = 0;
		i32 lineNumber = 0;

		std::vector<Error> errors;

		static const i32 MAX_VARS = 512;
		struct InstantiatedIdentifier
		{
			std::string name;
			i32 index = 0;
			Value* value = nullptr;
		};
		InstantiatedIdentifier* instantiatedIdentifiers = nullptr; // Array of length MAX_VARS
		i32 variableCount = 0;

		// TODO: Hash strings
		std::map<std::string, i32> tokenNameToInstantiatedIdentifierIdx;
	};

	struct Tokenizer
	{
		Tokenizer();
		explicit Tokenizer(const std::string& codeStr);
		~Tokenizer();

		void SetCodeStr(const std::string& newCodeStr);

		Token PeekNextToken();
		Token GetNextToken();

		void ConsumeWhitespaceAndComments();
		TokenType Type1IfNextCharIsCElseType2(char c, TokenType ifYes, TokenType ifNo);

		inline bool ValidDigitChar(char c);

		std::string codeStrCopy;
		TokenContext* context = nullptr;

		i32 nextTokenID = 0;

		// True when we have peeked a token but not yet consumed it
		// Prevents parsing a token multiple times when calling Peek followed by Get
		bool bConsumedLastParsedToken = true;
		Token lastParsedToken;
	};

	enum class TypeName
	{
		INT,
		FLOAT,
		BOOL,

		_NONE
	};

	static const char* g_TypeNameStrings[] =
	{
		"int",
		"float",
		"bool",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(g_TypeNameStrings) == ((u32)TypeName::_NONE + 1), "Length of g_TypeNameStrings must match length of TypeName enum");

	struct Type
	{
		static TypeName GetTypeNameFromStr(const std::string& str);
		static TypeName Parse(Tokenizer& tokenizer);
	};

	struct Node
	{
		explicit Node(const Token& token);

		Token token;
	};

	enum class ValueType
	{
		OPERATION,
		IDENTIFIER,
		INT_RAW,
		FLOAT_RAW,
		BOOL_RAW,

		NONE
	};

	TypeName ValueTypeToTypeName(ValueType valueType);

	struct Identifier : public Node
	{
		Identifier(const Token& token, const std::string& identifierStr, TypeName type);

		// type name will be _NONE when referencing existing identifiers
		static Identifier* Parse(Tokenizer& tokenizer, TypeName typeName);

		std::string identifierStr;
		TypeName type;
	};

	struct Operation : public Node
	{
		Operation(const Token& token, Expression* lhs, OperatorType op, Expression* rhs);
		~Operation();

		OperatorType op;
		Expression* lhs; // Is null when this is a unary operation
		Expression* rhs;

		// Returns the result of the comparison, or the new value of the lhs of the assignment
		Value* Evaluate(TokenContext& context);
		static Operation* Parse(Tokenizer& tokenizer);

	};

	struct Value
	{
		Value();
		explicit Value(Operation* opearation);
		explicit Value(Identifier* identifierValue);
		Value(i32 intRaw, bool bTemporary);
		Value(real floatRaw, bool bTemporary);
		Value(bool boolRaw, bool bTemporary);
		~Value();

		std::string ToString() const;

		ValueType type;

		bool bIsTemporary = true;

		union Val
		{
			Operation* operation;
			Identifier* identifier;
			i32 intRaw;
			real floatRaw;
			bool boolRaw;
			void* nullValue;

			Val(Operation* operation) : operation(operation) {}
			Val(Identifier* identifier) : identifier(identifier) {}
			Val(i32 intRaw) : intRaw(intRaw) {}
			Val(real floatRaw) : floatRaw(floatRaw) {}
			Val(bool boolRaw) : boolRaw(boolRaw) {}
			Val() : nullValue(nullptr) {}
		} val;
	};

	struct Expression : public Node
	{
		Expression(const Token& token, Operation* operation);
		Expression(const Token& token, i32 intRaw);
		Expression(const Token& token, real floatRaw);
		Expression(const Token& token, bool boolRaw);
		Expression(const Token& token, Identifier* identifier);
		~Expression();

		Value value;

		// Returns a pointer to the result of this expression
		Value* Evaluate(TokenContext& context);
		//bool Compare(TokenContext& context, Expression* other, OperatorType op);

		static Expression* Parse(Tokenizer& tokenizer);
		static bool ExpectOperator(Tokenizer &tokenizer, Token token, OperatorType* outOp);
	};

	struct Assignment : public Node
	{
		// If typename is specified, this assignment will instantiate var on evaluation
		// Otherwise, assignment is to pre-existing variable
		Assignment(const Token& token,
			Identifier* identifier,
			Expression* rhs,
			TypeName typeName = TypeName::_NONE);
		~Assignment();

		TypeName typeName = TypeName::_NONE; // Optional, not used when re-assigning
		Identifier* identifier = nullptr;
		Expression* rhs = nullptr; // If null, this assignment is actually just a declaration

		void Evaluate(TokenContext& context);
		// Should be called when tokenizer is pointing at char after '='
		static Assignment* Parse(Tokenizer& tokenizer);
	};

	enum class StatementType
	{
		ASSIGNMENT,
		IF,
		ELIF,
		ELSE,
		WHILE,

		_NONE
	};

	static const char* StatementTypeStrings[] =
	{
		"assignment",
		"If",
		"elif",
		"else",
		"while",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(StatementTypeStrings) == (u32)StatementType::_NONE + 1, "Length of StatementTypeStrings must match length of StatementType enum");

	struct Statement : public Node
	{
		Statement(const Token& token, Assignment* assignment);
		Statement(const Token& token, StatementType type, IfStatement* ifStatement);
		Statement(const Token& token, Statement* elseStatement);
		Statement(const Token& token, WhileStatement* whileStatement);
		~Statement();

		StatementType type;
		// TODO: Rename
		union Contents
		{
			Assignment* assignment;
			IfStatement* ifStatement;
			Statement* elseStatement;
			WhileStatement* whileStatement;

			Contents(Assignment* assignment) : assignment(assignment) {}
			Contents(IfStatement* ifStatement) : ifStatement(ifStatement) {}
			Contents(Statement* elseStatement) : elseStatement(elseStatement) {}
			Contents(WhileStatement* whileStatement) : whileStatement(whileStatement) {}
		} contents;

		void Evaluate(TokenContext& context);
		static Statement* Parse(Tokenizer& tokenizer);
	};

	enum class IfFalseAction
	{
		ELSE,
		ELIF,
		NONE
	};

	struct IfStatement : public Node
	{
		IfStatement(const Token& token, Expression* condition, Statement* body, IfStatement* elseIfStatement);
		IfStatement(const Token& token, Expression* condition, Statement* body, Statement* elseStatement);
		IfStatement(const Token& token, Expression* condition, Statement* body);
		~IfStatement();

		union IfFalse
		{
			void* nothingStatement;
			IfStatement* elseIfStatement;
			Statement* elseStatement;

			IfFalse() : nothingStatement(nullptr) {}
			IfFalse(IfStatement* elseIfStatement) : elseIfStatement(elseIfStatement) {}
			IfFalse(Statement* elseStatement) : elseStatement(elseStatement) {}
		} ifFalseStatement;

		IfFalseAction ifFalseAction = IfFalseAction::NONE;
		Expression* condition = nullptr;
		Statement* body = nullptr;

		void Evaluate(TokenContext& context);
		static IfStatement* Parse(Tokenizer& tokenizer);
	};

	struct WhileStatement : public Node
	{
		WhileStatement(const Token& token, Expression* condition, Statement* body);
		~WhileStatement();

		Expression* condition = nullptr;
		Statement* body = nullptr;

		// TODO: Handle Do While loops

		void Evaluate(TokenContext& context);
		static WhileStatement* Parse(Tokenizer& tokenizer);
	};

	struct RootItem
	{
		RootItem(Statement* statement, RootItem* nextItem);
		~RootItem();

		Statement* statement = nullptr;
		RootItem* nextItem = nullptr;

		void Evaluate(TokenContext& context);
		static RootItem* Parse(Tokenizer& tokenizer);
	};

	struct AST
	{
		explicit AST(Tokenizer* tokenizer);

		void Generate();
		void Evaluate();
		void Destroy();

		RootItem* rootItem = nullptr;
		Tokenizer* tokenizer = nullptr;
		bool bValid = false;

		glm::vec2i lastErrorTokenLocation = glm::vec2i(-1);
	};

	class Terminal : public GameObject
	{
	public:
		Terminal();
		explicit Terminal(const std::string& name);

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
		void EvaluateCode();

		AST* ast = nullptr;
		Tokenizer* tokenizer = nullptr;

		std::vector<std::string> lines;
		bool bParsePassed = false;

		real magicX = 2.87f;
		real magicY = 3.1f;

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
