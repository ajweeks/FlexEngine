#include "stdafx.hpp"

#include "PlacementPreview.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtx/quaternion.hpp>
IGNORE_WARNINGS_POP

#include "Graphics/Renderer.hpp"
#include "Graphics/VertexBufferData.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	// (the same as build mode's previews)
	const glm::vec4 PlacementPreview::VALID_COLOUR = glm::vec4(0.55f, 0.85f, 1.6f, 0.38f);
	const glm::vec4 PlacementPreview::INVALID_COLOUR = glm::vec4(1.8f, 0.35f, 0.3f, 0.38f);

	namespace
	{
		// Hologram materials are shared by colour (the colour is a material constant)
		MaterialID GetHologramMaterial(const glm::vec4& colour, bool bXRay)
		{
			char name[96];
			snprintf(name, sizeof(name), "Placement preview%s %.3f %.3f %.3f %.3f", bXRay ? " xray" : "", colour.r, colour.g, colour.b, colour.a);
			MaterialID materialID = g_Renderer->GetMaterialID(name);
			if (materialID == InvalidMaterialID)
			{
				MaterialCreateInfo createInfo = {};
				createInfo.name = name;
				createInfo.shaderName = bXRay ? "hologram_xray" : "hologram";
				// Alpha is the preview's opacity
				createInfo.constEmissive = colour;
				createInfo.bSerializable = false;
				materialID = g_Renderer->InitializeMaterial(&createInfo);
			}
			return materialID;
		}
	} // anonymous namespace

	glm::quat RotationFromUp(const glm::vec3& up, const glm::vec3& forward /* = (0, 0, 1) */)
	{
		const real upLength = glm::length(up);
		const glm::vec3 y = upLength > 1e-6f ? up / upLength : VEC3_UP;
		glm::vec3 z = forward - y * glm::dot(forward, y);
		if (glm::length(z) < 1e-3f)
		{
			// Forward is (anti)parallel to up: any perpendicular will do
			z = glm::abs(y.x) < 0.9f ? glm::cross(VEC3_RIGHT, y) : glm::cross(VEC3_FORWARD, y);
		}
		z = glm::normalize(z);
		const glm::vec3 x = glm::cross(y, z);
		return glm::quat_cast(glm::mat3(x, y, z));
	}

	void PlacementPreview::Geometry::Triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
	{
		const glm::vec3 cross = glm::cross(b - a, c - a);
		const real length = glm::length(cross);
		if (length < 1e-10f)
		{
			return;
		}
		const glm::vec3 normal = cross / length;
		const u32 base = (u32)positions.size();
		positions.insert(positions.end(), { a, b, c });
		normals.insert(normals.end(), { normal, normal, normal });
		indices.insert(indices.end(), { base, base + 1, base + 2 });
	}

	void PlacementPreview::Geometry::Box(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation)
	{
		const glm::vec3 faceNormals[6] = { VEC3_RIGHT, -VEC3_RIGHT, VEC3_UP, -VEC3_UP, VEC3_FORWARD, -VEC3_FORWARD };
		for (const glm::vec3& normal : faceNormals)
		{
			const glm::vec3 axisU = glm::abs(normal.y) > 0.5f ? VEC3_RIGHT : VEC3_UP;
			const glm::vec3 axisV = glm::cross(normal, axisU);
			const u32 base = (u32)positions.size();
			const glm::vec2 corners[4] = { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) };
			for (const glm::vec2& corner : corners)
			{
				positions.push_back(center + rotation * ((normal + axisU * corner.x + axisV * corner.y) * halfExtents));
				normals.push_back(rotation * normal);
			}
			indices.insert(indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
		}
	}

	void PlacementPreview::Geometry::Ring(real innerRadius, real outerRadius, real halfHeight, u32 segments, real y /* = 0.0f */)
	{
		segments = glm::max(segments, 3u);
		for (u32 i = 0; i < segments; ++i)
		{
			const real a0 = TWO_PI * (real)i / (real)segments;
			const real a1 = TWO_PI * (real)(i + 1) / (real)segments;
			const glm::vec3 d0(std::cos(a0), 0.0f, std::sin(a0));
			const glm::vec3 d1(std::cos(a1), 0.0f, std::sin(a1));
			const glm::vec3 up(0.0f, y + halfHeight, 0.0f);
			const glm::vec3 down(0.0f, y - halfHeight, 0.0f);
			const glm::vec3 mid(0.0f, y, 0.0f);

			// Top (seen from above), with angles increasing clockwise seen from above
			Triangle(mid + d0 * innerRadius, mid + d1 * outerRadius, mid + d0 * outerRadius);
			Triangle(mid + d0 * innerRadius, mid + d1 * innerRadius, mid + d1 * outerRadius);
			// Bottom
			Triangle(mid + d0 * innerRadius, mid + d0 * outerRadius, mid + d1 * outerRadius);
			Triangle(mid + d0 * innerRadius, mid + d1 * outerRadius, mid + d1 * innerRadius);
			if (halfHeight > 0.0f)
			{
				// Outer wall (facing out), & inner wall (facing in)
				Triangle(down + d0 * outerRadius, up + d1 * outerRadius, down + d1 * outerRadius);
				Triangle(down + d0 * outerRadius, up + d0 * outerRadius, up + d1 * outerRadius);
				Triangle(down + d0 * innerRadius, down + d1 * innerRadius, up + d1 * innerRadius);
				Triangle(down + d0 * innerRadius, up + d1 * innerRadius, up + d0 * innerRadius);
			}
		}
	}

	glm::vec4 PlacementPreview::ValidColour(bool bValid)
	{
		return bValid ? VALID_COLOUR : INVALID_COLOUR;
	}

	void PlacementPreview::Show(u64 key, const BuildFunc& build, const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale, const glm::vec4& colour)
	{
		m_bShown = true;

		BaseScene* scene = g_SceneManager->CurrentScene();
		if (scene == nullptr)
		{
			return;
		}

		GameObject* object = m_ObjectID.Get();
		if (object == nullptr)
		{
			// Objects are added to the scene at the start of the next frame, wait for that before making another (if it's
			// still missing after that, it was destroyed along with the scene)
			const i32 frame = g_Renderer->GetFramesRenderedCount();
			if (m_ObjectID.IsValid() && frame - m_CreatedFrame < 3)
			{
				return;
			}
			m_CreatedFrame = frame;
			object = new GameObject("Placement preview", BaseObjectSID);
			object->SetSerializable(false);
			object->SetVisibleInSceneExplorer(false);
			object->SetCastsShadow(false);
			// Its mesh is built once it's in the scene
			scene->AddRootObject(object);
			m_ObjectID = object->ID;
			m_bMeshDirty = true;
			m_bVisible = false;
			return;
		}

		if (!m_bHasKey || key != m_Key)
		{
			m_bHasKey = true;
			m_Key = key;
			m_Geometry = {};
			if (build)
			{
				build(m_Geometry);
			}
			m_bMeshDirty = true;
		}
		if (colour != m_Colour)
		{
			m_Colour = colour;
			m_bMeshDirty = true;
		}
		if (m_bMeshDirty)
		{
			Rebuild(object);
		}

		Transform* transform = object->GetTransform();
		transform->SetWorldPosition(pos, false);
		transform->SetWorldRotation(rot, false);
		transform->SetWorldScale(scale, true);
		if (!m_bVisible)
		{
			object->SetVisible(true);
			m_bVisible = true;
		}
	}

	void PlacementPreview::Rebuild(GameObject* object)
	{
		m_bMeshDirty = false;
		const MaterialID materialID = GetHologramMaterial(m_Colour, false);
		Mesh* mesh = object->SetMesh(new Mesh(object));
		if (m_Geometry.Empty() || materialID == InvalidMaterialID)
		{
			return;
		}

		VertexBufferDataCreateInfo vertexData = {};
		vertexData.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(materialID)->shaderID)->vertexAttributes;
		vertexData.positions_3D = m_Geometry.positions;
		vertexData.normals = m_Geometry.normals;
		const size_t vertexCount = m_Geometry.positions.size();
		vertexData.tangents.reserve(vertexCount);
		vertexData.texCoords_UV.reserve(vertexCount);
		for (size_t i = 0; i < vertexCount; ++i)
		{
			const glm::vec3& normal = m_Geometry.normals[i];
			vertexData.tangents.push_back(glm::abs(normal.y) > 0.9f ? VEC3_RIGHT : glm::normalize(glm::cross(VEC3_UP, normal)));
			vertexData.texCoords_UV.push_back(glm::vec2(m_Geometry.positions[i].x + m_Geometry.positions[i].y, m_Geometry.positions[i].z));
		}
		vertexData.colours_R32G32B32A32.assign(vertexCount, VEC4_ONE);
		RenderObjectCreateInfo renderInfo = {};
		renderInfo.visibleInSceneExplorer = false;
		MeshComponent::LoadFromMemory(mesh, vertexData, m_Geometry.indices, materialID, &renderInfo);
		if (m_bXRay)
		{
			// The same triangles again, as dashes only where the depth test hides them (reversed-Z: farther is less)
			renderInfo.depthTestReadFunc = DepthTestFunc::LESS;
			MeshComponent::LoadFromMemory(mesh, vertexData, m_Geometry.indices, GetHologramMaterial(m_Colour, true), &renderInfo);
		}
		mesh->PostInitialize();
	}

	void PlacementPreview::Hide()
	{
		m_bShown = false;
		if (!m_bVisible)
		{
			return;
		}
		m_bVisible = false;
		if (GameObject* object = m_ObjectID.Get())
		{
			object->SetVisible(false);
		}
	}

	void PlacementPreview::Destroy()
	{
		if (GameObject* object = m_ObjectID.Get())
		{
			if (BaseScene* scene = g_SceneManager->CurrentScene())
			{
				scene->RemoveObject(object, true);
			}
		}
		m_ObjectID = InvalidGameObjectID;
		m_bHasKey = false;
		m_bVisible = false;
		m_Colour = glm::vec4(-1.0f);
	}
} // namespace flex
