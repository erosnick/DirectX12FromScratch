#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>

#include "Model.h"

class GeometryGenerator
{
public:

    using uint16 = std::uint16_t;
    using uint32 = std::uint32_t;

    struct SimpleVertex
    {
        SimpleVertex()
                : position{},
                  normal{},
                  texcoord{},
                  color{1.0f, 1.0f, 1.0f}
        {}

        SimpleVertex(const glm::vec3& inPosition,const glm::vec3& inNormal, const glm::vec2& inTexcoord, const glm::vec3& inColor)
                : position(inPosition), normal(inNormal), texcoord(inTexcoord), color(inColor)
        {

        }

        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 texcoord;
        glm::vec3 color;
    };

    struct Vertex
    {
        Vertex()
                : position{},
                  normal{},
                  tangent{},
                  texcoord{}
        {}
        Vertex(
                const glm::vec3& inPosition,
                const glm::vec3& inNormal,
                const glm::vec3& inTangent,
                const glm::vec2& inTexcoord) :
                position(inPosition),
                normal(inNormal),
                tangent(inTangent),
                texcoord(inTexcoord){}
        Vertex(
                float px, float py, float pz,
                float nx, float ny, float nz,
                float tx, float ty, float tz,
                float u, float v) :
                position(px, py, pz),
                normal(nx, ny, nz),
                tangent(tx, ty, tz),
                texcoord(u, v){}

        SimpleVertex toSimpleVertex() const
        {
            SimpleVertex simpleVertex;

            simpleVertex.position = position;
            simpleVertex.normal = normal;
            simpleVertex.texcoord = texcoord;

            return simpleVertex;
        }

        glm::vec3 position;
        glm::vec3 normal;
        glm::vec3 tangent;
        glm::vec2 texcoord;
    };

    struct MeshData
    {
        std::vector<Vertex> vertices;
        std::vector<uint32> indices32;

        std::vector<uint16> &getIndices16()
        {
            if(mIndices16.empty())
            {
                mIndices16.resize(indices32.size());
                for(size_t i = 0; i < indices32.size(); ++i)
                    mIndices16[i] = static_cast<uint16>(indices32[i]);
            }

            return mIndices16;
        }

    private:
        std::vector<uint16> mIndices16;
    };

	DXModel toDXModel(const MeshData& meshData);

    template <typename T>
    void buildSplitBuffers(const std::vector<T>& vertices, std::vector<glm::vec3>& positions, std::vector<glm::vec3>& normals, std::vector<glm::vec2>& texcoords)
    {
        for (const auto& vertex : vertices)
        {
            positions.emplace_back(vertex.position);
            normals.emplace_back(vertex.normal);
            texcoords.emplace_back(vertex.texcoord);
        }
    }

    std::vector<SimpleVertex> toSimpleVertices(const std::vector<Vertex>& vertices);
    ///<summary>
    /// Creates a quad centered at the origin with the given dimensions,
    ///</summary>
    MeshData createQuad(float width, float height);

    ///<summary>
    /// Creates a box centered at the origin with the given dimensions, where each
    /// face has m rows and n columns of vertices.
    ///</summary>
    MeshData createBox(float width, float height, float depth, uint32_t numSubdivisions = 0);

    ///<summary>
    /// Creates a sphere centered at the origin with the given radius.  The
    /// slices and stacks parameters control the degree of tessellation.
    ///</summary>
    MeshData createSphere(float radius, uint32 sliceCount, uint32 stackCount);

    ///<summary>
    /// Creates a geosphere centered at the origin with the given radius.  The
    /// depth controls the level of tessellation.
    ///</summary>
    MeshData createGeosphere(float radius, uint32 numSubdivisions);

    ///<summary>
    /// Creates a cylinder parallel to the y-axis, and centered about the origin.
    /// The bottom and top radius can vary to form various cone shapes rather than true
    // cylinders.  The slices and stacks parameters control the degree of tessellation.
    ///</summary>
    MeshData createCylinder(float bottomRadius, float topRadius, float height, uint32_t sliceCount, uint32_t stackCount);

    ///<summary>
    /// Creates an mxn grid in the xz-plane with m rows and n columns, centered
    /// at the origin with the specified width and depth.
    ///</summary>
    MeshData createGrid(float width, float depth, uint32 m, uint32 n, float uvScale = 1.0f);
private:
    void subdivide(MeshData& meshData);
    Vertex MidPoint(const Vertex& v0, const Vertex& v1);
    void buildCylinderTopCap(float bottomRadius, float topRadius, float height, uint32_t sliceCount, uint32_t stackCount, MeshData& meshData);
    void buildCylinderBottomCap(float bottomRadius, float topRadius, float height, uint32_t sliceCount, uint32_t stackCount, MeshData& meshData);
};