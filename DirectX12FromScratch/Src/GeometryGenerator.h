#pragma once

#include <cstdint>
#include <DirectXMath.h>
#include <vector>

class GeometryGenerator
{
public:

	using uint16 = std::uint16_t;
	using uint32 = std::uint32_t;

	struct Vertex
	{
		Vertex() {}
		Vertex(
			const DirectX::XMFLOAT3& InPosition,
			const DirectX::XMFLOAT3& InNormal,
			const DirectX::XMFLOAT3& InTangent,
			const DirectX::XMFLOAT2& InUV) :
			Position(InPosition),
			Normal(InNormal),
			Tangent(InTangent),
			UV(InUV)
		{}

		Vertex(
		float PositionX, float PositionY, float PositionZ,
		float NormalX, float NormalY, float NormalZ,
		float TangentX, float TangentY, float TangentZ,
		float U, float V) :
		Position(PositionX, PositionY, PositionZ),
		Normal(NormalX, NormalY, NormalZ),
		Tangent(TangentX, TangentY, TangentZ),
		UV(U, V)
		{}

		DirectX::XMFLOAT3 Position;
		DirectX::XMFLOAT3 Normal;
		DirectX::XMFLOAT3 Tangent;
		DirectX::XMFLOAT2 UV;
	};

	struct MeshData
	{
		std::vector<Vertex> Vertices;
		std::vector<uint32> Indices32;

		std::vector<uint16>& GetIndices16()
		{
			if (Indices16.empty())
			{
				Indices16.resize(Indices32.size());

				for (size_t i = 0; i < Indices32.size(); i++)
				{
					Indices16[i] = static_cast<uint16>(Indices32[i]);
				}
			}

			return Indices16;
		}

	private:
		std::vector<uint16> Indices16;
	};

	///<summary>
	/// Creates a box centered at the origin with the given dimensions, where
	/// each face has m rows and columns of vertices.
	///</summary>
	MeshData CreateBox(float Width, float Height, float Depth, uint32 NumSubdivisions);

	///<summary>
	/// Creates a sphere centerd at the origin with the given radius.
	/// The slices and stacks parameters control the degree of tesselation.
	///</summary>
	MeshData CreateSphere(float Radius, uint32 SliceCount, uint32 StackCount);

	///<summary>
	/// Creates a geosphere centered at the origin with the given radius.
	/// The depth controls the level of tesselation.
	///</summary>
	MeshData CreateGeosphere(float Radius, uint32 NumSubdivisions);

	///<summary>
	/// Creates a cylinder parallel to the y-axis, and centered about the origin.
	/// The bottom and top radius can vary to form various cone shape rather
	/// true cylinders. The slices and stacks parameters control the degree of
	/// tessellation.
	///</summary>
	MeshData CreateCylinder(float BottomRadius, float TopRadius, float Height, uint32 SliceCount, uint32 StackCount);

	///<summary>
	/// Creates an m x n grid in the xz-plane with m rows and n columns, centered
	/// at the origin with the specified width and depth.
	///</summary>
	MeshData CreateGrid(float Width, float Depth, uint32 M, uint32 N);

	///<summary>
	/// Create a quad aligned with the screen. This is useful for post processing 
	/// and screen effects.
	///</summary>
	MeshData CreateQuad(float X, float Y, float W, float H, float Depth);

private:

	void Subdivide(MeshData& InMeshData);
	Vertex MidPoint(const Vertex& Vertex0, const Vertex& Vertex1);
	void BuildCylinderTopCap(float BottomRadius, float TopRadius, float Height, uint32 SliceCount, uint32 StackCount, MeshData& MeshData);
	void BuildCylinderBottomCap(float BottomRadius, float TopRadius, float Height, uint32 SliceCount, uint32 StackCount, MeshData& MeshData);
};