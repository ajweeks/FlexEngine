#pragma once

// Translucent "hologram" previews of things about to be placed (campfires...) or done (terrain edits): blue where
// they can go, red where they can't (or any other colour). Each preview owns one (unsaved, hidden from the scene
// explorer) object in the current scene with the hologram shader.
//
//     PlacementPreview::Frame frame(m_Preview); // Hides the preview at the end of the scope unless it was shown
//     if (bAiming)
//     {
//         m_Preview.Show(PREVIEW_KEY, [](PlacementPreview::Geometry& geometry) { geometry.Box(...); },
//             pos, rot, VEC3_ONE, PlacementPreview::ValidColour(bValid));
//     }
namespace flex
{
	class PlacementPreview final
	{
	public:
		// Local space triangles (counter-clockwise seen from the front)
		struct Geometry
		{
			void Triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);
			void Box(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation);
			// A flat ring around the local Y axis (in the XZ plane at height y), between two radii, with walls halfHeight
			// above & below it so it shows on uneven ground
			void Ring(real innerRadius, real outerRadius, real halfHeight, u32 segments, real y = 0.0f);
			bool Empty() const { return indices.empty(); }

			std::vector<glm::vec3> positions;
			std::vector<glm::vec3> normals;
			std::vector<u32> indices;
		};
		using BuildFunc = std::function<void(Geometry&)>;

		// Hides the preview at the end of a scope (e.g. an update function with early outs) unless Show was called in it
		class Frame
		{
		public:
			explicit Frame(PlacementPreview& preview) : m_Preview(preview) { m_Preview.m_bShown = false; }
			~Frame() { if (!m_Preview.m_bShown) { m_Preview.Hide(); } }
			Frame(const Frame&) = delete;
			Frame& operator=(const Frame&) = delete;

		private:
			PlacementPreview& m_Preview;
		};

		PlacementPreview() = default;
		PlacementPreview(const PlacementPreview&) = delete;
		PlacementPreview& operator=(const PlacementPreview&) = delete;

		// Shows the preview this frame. build is called to (re)create the geometry whenever key changes (so a key per
		// distinct shape). The preview is created lazily, so it may take a couple of frames to appear.
		void Show(u64 key, const BuildFunc& build, const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale, const glm::vec4& colour);
		void Hide();
		// Removes its object from the scene (it's also destroyed along with the scene)
		void Destroy();

		bool IsVisible() const { return m_bVisible; }

		// Emissive colour (alpha: opacity) for things which can/can't be placed
		static glm::vec4 ValidColour(bool bValid);
		static const glm::vec4 VALID_COLOUR;
		static const glm::vec4 INVALID_COLOUR;

	private:
		void Rebuild(GameObject* object);

		GameObjectID m_ObjectID = InvalidGameObjectID;
		i32 m_CreatedFrame = 0;
		u64 m_Key = 0;
		bool m_bHasKey = false;
		glm::vec4 m_Colour = glm::vec4(-1.0f);
		Geometry m_Geometry;
		bool m_bMeshDirty = true;
		bool m_bVisible = false;
		bool m_bShown = false;
	};

	// Rotation taking local +Y to up (& keeping local +Z as close to forward as it can)
	glm::quat RotationFromUp(const glm::vec3& up, const glm::vec3& forward = glm::vec3(0.0f, 0.0f, 1.0f));
} // namespace flex
