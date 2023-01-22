#include "pch.h"
#include "GeometryGenerator.h"
#include <algorithm>

using namespace DirectX;

GeometryGenerator::MeshData GeometryGenerator::CreateBox(float Width, float Height, float Depth, uint32 NumSubdivisions)
{
	MeshData Data;

	//
	// Create the vertices.
	//

	Vertex BoxVertex[24];

	float HalfWidth = 0.5f * Width;
	float HalfHeight = 0.5f * Height;
	float HalfDepth = 0.5f * Depth;

	// Fill in the front face vertex data.
	BoxVertex[0] = Vertex(-HalfWidth, -HalfHeight, -HalfDepth, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
	BoxVertex[1] = Vertex(-HalfWidth, +HalfHeight, -HalfDepth, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	BoxVertex[2] = Vertex(+HalfWidth, +HalfHeight, -HalfDepth, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
	BoxVertex[3] = Vertex(+HalfWidth, -HalfHeight, -HalfDepth, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f);

	// Fill in the back face vertex data.
	BoxVertex[4] = Vertex(-HalfWidth, -HalfHeight, +HalfDepth, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
	BoxVertex[5] = Vertex(+HalfWidth, -HalfHeight, +HalfDepth, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
	BoxVertex[6] = Vertex(+HalfWidth, +HalfHeight, +HalfDepth, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	BoxVertex[7] = Vertex(-HalfWidth, +HalfHeight, +HalfDepth, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f);

	// Fill in the top face vertex data.
	BoxVertex[8] = Vertex(-HalfWidth, +HalfHeight, -HalfDepth, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
	BoxVertex[9] = Vertex(-HalfWidth, +HalfHeight, +HalfDepth, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	BoxVertex[10] = Vertex(+HalfWidth, +HalfHeight, +HalfDepth, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
	BoxVertex[11] = Vertex(+HalfWidth, +HalfHeight, -HalfDepth, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f);

	// Fill in the bottom face vertex data.
	BoxVertex[12] = Vertex(-HalfWidth, -HalfHeight, -HalfDepth, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
	BoxVertex[13] = Vertex(+HalfWidth, -HalfHeight, -HalfDepth, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
	BoxVertex[14] = Vertex(+HalfWidth, -HalfHeight, +HalfDepth, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	BoxVertex[15] = Vertex(-HalfWidth, -HalfHeight, +HalfDepth, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f);

	// Fill in the left face vertex data.
	BoxVertex[16] = Vertex(-HalfWidth, -HalfHeight, +HalfDepth, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f);
	BoxVertex[17] = Vertex(-HalfWidth, +HalfHeight, +HalfDepth, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f);
	BoxVertex[18] = Vertex(-HalfWidth, +HalfHeight, -HalfDepth, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f);
	BoxVertex[19] = Vertex(-HalfWidth, -HalfHeight, -HalfDepth, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f);

	// Fill in the right face vertex data.
	BoxVertex[20] = Vertex(+HalfWidth, -HalfHeight, -HalfDepth, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f);
	BoxVertex[21] = Vertex(+HalfWidth, +HalfHeight, -HalfDepth, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
	BoxVertex[22] = Vertex(+HalfWidth, +HalfHeight, +HalfDepth, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
	BoxVertex[23] = Vertex(+HalfWidth, -HalfHeight, +HalfDepth, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);

	Data.Vertices.assign(&BoxVertex[0], &BoxVertex[24]);

	//
	// Create the indices.
	//

	uint32 Indices[36];

	// Fill in the front face index data
	Indices[0] = 0; Indices[1] = 1; Indices[2] = 2;
	Indices[3] = 0; Indices[4] = 2; Indices[5] = 3;

	// Fill in the back face index data
	Indices[6] = 4; Indices[7] = 5; Indices[8] = 6;
	Indices[9] = 4; Indices[10] = 6; Indices[11] = 7;

	// Fill in the top face index data
	Indices[12] = 8; Indices[13] = 9; Indices[14] = 10;
	Indices[15] = 8; Indices[16] = 10; Indices[17] = 11;

	// Fill in the bottom face index data
	Indices[18] = 12; Indices[19] = 13; Indices[20] = 14;
	Indices[21] = 12; Indices[22] = 14; Indices[23] = 15;

	// Fill in the left face index data
	Indices[24] = 16; Indices[25] = 17; Indices[26] = 18;
	Indices[27] = 16; Indices[28] = 18; Indices[29] = 19;

	// Fill in the right face index data
	Indices[30] = 20; Indices[31] = 21; Indices[32] = 22;
	Indices[33] = 20; Indices[34] = 22; Indices[35] = 23;

	Data.Indices32.assign(&Indices[0], &Indices[36]);

	// Put a cap on the number of subdivisions.
	NumSubdivisions = std::min<uint32>(NumSubdivisions, 6u);

	for (uint32 i = 0; i < NumSubdivisions; ++i)
		Subdivide(Data);

	return Data;
}

GeometryGenerator::MeshData GeometryGenerator::CreateSphere(float Radius, uint32 SliceCount, uint32 StackCount)
{
	MeshData Data;

	// Compute the vertices stating at the top pole and moving down the stacks.

	// Poles: Note that there will be texture coordinate distortion as there is
	// not a unique point on the texture map to assign to the pole when mapping
	// a rectangular texture on to a sphere.
	Vertex TopVertex(0.0f, Radius, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	Vertex BottomVertex(0.0f, -Radius, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);

	Data.Vertices.push_back(TopVertex);

	float PhiStep = XM_PI / StackCount;
	float ThetaStep = 2.0f * XM_PI / SliceCount;

	// Compute vertices for each stack ring (do not count the poles as rings).
	for (uint32 i = 1; i <= StackCount; i++)
	{
		float Phi = i * PhiStep;

		// Vertices of ring.
		for (uint32 j = 0; j <= SliceCount; j++)
		{
			float Theta = j * ThetaStep;

			Vertex RingVertex;

			// Spherical to cartesian.
			RingVertex.Position.x = Radius * sinf(Phi) * cosf(Theta);
			RingVertex.Position.y = Radius * cosf(Phi);
			RingVertex.Position.z = Radius * sinf(Phi) * sinf(Theta);

			// Partial derivative of P with respect to theta.
			RingVertex.Tangent.x = -Radius * sinf(Phi) * sinf(Theta);
			RingVertex.Tangent.y = 0.0f;
			RingVertex.Tangent.z = Radius * sinf(Phi) * cosf(Theta);

			XMVECTOR Tangent = XMLoadFloat3(&RingVertex.Tangent);
			XMStoreFloat3(&RingVertex.Tangent, XMVector3Normalize(Tangent));
			
			XMVECTOR Position = XMLoadFloat3(&RingVertex.Position);
			XMStoreFloat3(&RingVertex.Normal, XMVector3Normalize(Position));

			RingVertex.UV.x = Theta / XM_2PI;
			RingVertex.UV.y = Phi / XM_PI;

			Data.Vertices.push_back(RingVertex);
		}
	}

	Data.Vertices.push_back(BottomVertex);

	// Compute indices for top stack. The top stack was written first to the vertex
	// buffer and connects the top pole to the first ring.
	for (uint32 i = 1; i <= SliceCount; i++)
	{
		Data.Indices32.push_back(0);
		Data.Indices32.push_back(i + 1);
		Data.Indices32.push_back(i);
	}

	// Compute indices for inner stacks (not connected to poles).
	// This is just skipping the top pole vertex.
	uint32 BaseIndex = 1;
	uint32 RingVertexCount = SliceCount + 1;
	for (uint32 i = 0; i < StackCount; i++)
	{
		for (uint32 j = 0; j < SliceCount; j++)
		{
			Data.Indices32.push_back(BaseIndex + i * RingVertexCount + j);
			Data.Indices32.push_back(BaseIndex + i * RingVertexCount + j + 1);
			Data.Indices32.push_back(BaseIndex + (i + 1) * RingVertexCount + j);

			Data.Indices32.push_back(BaseIndex + (i + 1) * RingVertexCount + j);
			Data.Indices32.push_back(BaseIndex + i * RingVertexCount + j + 1);
			Data.Indices32.push_back(BaseIndex + (i + 1) * RingVertexCount + j + 1);
		}
	}

	// Compute indices for bottom stack. The bottom stack was written last to the 
	// vertex buffer and connects the bottom pole to the bottom ring.


	// South pole vertex was added last.
	uint32 SouthPoleIndex = (uint32)Data.Vertices.size() - 1;

	// Offset the indices to the index of the first vertex in the last ring.
	BaseIndex = SouthPoleIndex - RingVertexCount;

	for (uint32 i = 0; i < SliceCount; i++)
	{
		Data.Indices32.push_back(SouthPoleIndex);
		Data.Indices32.push_back(BaseIndex);
		Data.Indices32.push_back(BaseIndex + i + 1);
	}

	return Data;
}

GeometryGenerator::MeshData GeometryGenerator::CreateGeosphere(float Radius, uint32 NumSubdivisions)
{
	MeshData Data;

	// Put a cap on the number of subdivisions.
	NumSubdivisions = std::min<uint32>(NumSubdivisions, 6u);

	// Approximate a sphere by tesselating an icosahedron.
	const float X = 0.525731f;
	const float Z = 0.850651f;

	XMFLOAT3 Position[12] =
	{
		XMFLOAT3(-X, 0.0f, Z), XMFLOAT3(X, 0.0f, Z),
		XMFLOAT3(-X, 0.0f, -Z), XMFLOAT3(X, 0.0f, -Z),
		XMFLOAT3(0.0f, Z, X), XMFLOAT3(0.0f, Z, -X),
		XMFLOAT3(0.0f, -Z, X), XMFLOAT3(0.0f, -Z, -X),
		XMFLOAT3(Z, X, 0.0f), XMFLOAT3(-Z, X, 0.0f),
		XMFLOAT3(Z, -X, 0.0f), XMFLOAT3(-Z, -X, 0.0f)
	};

	uint32 Kernal[60] = 
	{
		1,4,0,  4,9,0,  4,5,9,  8,5,4,  1,8,4,
		1,10,8, 10,3,8, 8,3,5,  3,2,5,  3,7,2,
		3,10,7, 10,6,7, 6,11,7, 6,0,11, 6,1,0,
		10,1,6, 11,0,9, 2,11,9, 5,2,9,  11,2,7
	};

	Data.Vertices.resize(12);
	Data.Indices32.assign(&Kernal[0], &Kernal[60]);

	for (uint32 i = 0; i < 12; i++)
	{
		Data.Vertices[i].Position = Position[i];
	}

	for (uint32 i = 0; i < NumSubdivisions; i++)
	{
		Subdivide(Data);
	}

	// Project vertices onto sphere and scale.
	for (uint32 i = 0; i < Data.Vertices.size(); i++)
	{
		// Project onto uint sphere.
		XMVECTOR Normal = XMVector3Normalize(XMLoadFloat3(&Data.Vertices[i].Position));

		// Project onto sphere.
		XMVECTOR Position = Radius * Normal;


		XMStoreFloat3(&Data.Vertices[i].Position, Position);
		XMStoreFloat3(&Data.Vertices[i].Normal, Normal);

		// Derive texture coordinates from spherical coordinates.
		float Theta = atan2f(Data.Vertices[i].Position.z, Data.Vertices[i].Position.x);
	
		// Put in [0, 2PI]
		if (Theta < 0.0f)
		{
			Theta += XM_2PI;
		}

		float Phi = acosf(Data.Vertices[i].Position.y / Radius);

		Data.Vertices[i].UV.x = Theta / XM_2PI;
		Data.Vertices[i].UV.y = Phi / XM_PI;

		// Partial derivative of P with respect to theta.
		Data.Vertices[i].Tangent.x = -Radius * sinf(Phi) * sinf(Theta);
		Data.Vertices[i].Tangent.y = 0.0f;
		Data.Vertices[i].Tangent.z = Radius * sinf(Phi) * cosf(Theta);

		XMVECTOR Tangent = XMLoadFloat3(&Data.Vertices[i].Tangent);
		XMStoreFloat3(&Data.Vertices[i].Tangent, XMVector3Normalize(Tangent));
	}

	return Data;
}

GeometryGenerator::MeshData GeometryGenerator::CreateCylinder(float BottomRadius, float TopRadius, float Height, uint32 SliceCount, uint32 StackCount)
{
	MeshData Data;

	// Build stacks
	float StackHeight = Height / StackCount;
	
	// Amount to increment radius as we move up each stack level
	// from bottom to top.
	float RadiusStep = (TopRadius - BottomRadius) / StackCount;

	uint32 RingCount = StackCount + 1;

	// Compute vertices for each stack ring starting at the bottom
	// and moving up.
	for (uint32 i = 0; i < RingCount; i++)
	{
		float Y = -0.5f * Height + i * StackHeight;
		float R = BottomRadius + i * RadiusStep;

		// Vertices of ring.
		float DeltaTheta = 2.0f * XM_PI / SliceCount;

		for (uint32 j = 0; j <= SliceCount; j++)
		{
			Vertex RingVertex;

			float Cos = cosf(j * DeltaTheta);
			float Sin = sinf(j * DeltaTheta);

			RingVertex.Position = XMFLOAT3(R * Cos, Y, R * Sin);

			RingVertex.UV.x = (float)j / SliceCount;
			RingVertex.UV.y = 1.0f - (float)i / StackCount;

			// Cylinder can be parameterized as follows, where we
			// introduce V parameter that goes in the same direction
			// as the V tex-coord so that the bitangent goes in the
			// same direction as the v tex-coord.
			// Let R0 be the Bottom radius and let R1 e the top radius.
			// Y(V) = H - HV for V in [0, 1].
			// R(V) = R1 + (R0 - R10)V
			//
			// X(T, V) = R(V)*cos(T)
			// Y(T, V) = H - HV
			// Z(T, V) = R(V)*sin(T)
			//
			// DX / DT = -R(V)*sin(T)
			// DY / DT = 0
			// DZ / DT = R(V)*cost(T)
			//
			// DX / DX = (R0 - R1)*cos(T)
			// DY / DV = -H
			// DZ / DV = (R0 - R1)*sin(T)

			// This is unit length.
			RingVertex.Tangent = XMFLOAT3(-Sin, 0.0F, Cos);

			float DeltaRadius = BottomRadius - TopRadius;
			XMFLOAT3 Bitangent(DeltaRadius * Cos, -Height, DeltaRadius * Sin);

			XMVECTOR Tangent = XMLoadFloat3(&RingVertex.Tangent);
			XMVECTOR Binormal = XMLoadFloat3(&Bitangent);
			XMVECTOR Normal = XMVector3Normalize(XMVector3Cross(Tangent, Binormal));

			XMStoreFloat3(&RingVertex.Normal, Normal);

			Data.Vertices.push_back(RingVertex);
		}
	}

	// Add one because we duplicate the first and last vertex per ring
	// since the texture coordinates are different.
	uint32 RingVertexCount = SliceCount + 1;

	// Compute indices for each stack.
	for (uint32 i = 0; i < StackCount; i++)
	{
		for (uint32 j = 0; j < SliceCount; j++)
		{
			Data.Indices32.push_back(i * RingVertexCount + j);
			Data.Indices32.push_back((i + 1) * RingVertexCount + j);
			Data.Indices32.push_back((i + 1) * RingVertexCount + j + 1);

			Data.Indices32.push_back(i * RingVertexCount + j);
			Data.Indices32.push_back((i + 1) * RingVertexCount + j + 1);
			Data.Indices32.push_back(i * RingVertexCount + j + 1);
		}
	}

	BuildCylinderTopCap(BottomRadius, TopRadius, Height, SliceCount, StackCount, Data);
	BuildCylinderBottomCap(BottomRadius, TopRadius, Height, SliceCount, StackCount, Data);

	return Data;
}

GeometryGenerator::MeshData GeometryGenerator::CreateGrid(float Width, float Depth, uint32 M, uint32 N)
{
	MeshData Data;

	uint32 VertexCount = M * N;
	uint32 FaceCount = (M - 1)*(N - 1) * 2;

	//
	// Create the vertices.
	//

	float HalfWidth = 0.5f * Width;
	float HalfDepth = 0.5f * Depth;

	float DX = Width / (N - 1);
	float DZ = Depth / (M - 1);

	float DU = 1.0f / (N - 1);
	float DV = 1.0f / (M - 1);

	Data.Vertices.resize(VertexCount);
	for (uint32 i = 0; i < M; ++i)
	{
		float Z = HalfDepth - i * DZ;
		for (uint32 j = 0; j < N; ++j)
		{
			float X = -HalfWidth + j * DX;

			Data.Vertices[i * N + j].Position = XMFLOAT3(X, 0.0f, Z);
			Data.Vertices[i * N + j].Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
			Data.Vertices[i * N + j].Tangent = XMFLOAT3(1.0f, 0.0f, 0.0f);

			// Stretch texture over grid.
			Data.Vertices[i * N + j].UV.x = j * DU;
			Data.Vertices[i * N + j].UV.y = i * DV;
		}
	}

	//
	// Create the indices.
	//

	Data.Indices32.resize(FaceCount * 3); // 3 indices per face

											  // Iterate over each quad and compute indices.
	uint32 K = 0;
	for (uint32 i = 0; i < M - 1; ++i)
	{
		for (uint32 j = 0; j < N - 1; ++j)
		{
			Data.Indices32[K] = i * N + j;
			Data.Indices32[K + 1] = i * N + j + 1;
			Data.Indices32[K + 2] = (i + 1) * N + j;

			Data.Indices32[K + 3] = (i + 1) * N + j;
			Data.Indices32[K + 4] = i * N + j + 1;
			Data.Indices32[K + 5] = (i + 1) * N + j + 1;

			K += 6; // next quad
		}
	}

	return Data;
}

GeometryGenerator::MeshData GeometryGenerator::CreateQuad(float X, float Y, float W, float H, float Depth)
{
	return MeshData();
}

void GeometryGenerator::BuildCylinderTopCap(float BottomRadius, float TopRadius, float Height, uint32 SliceCount, uint32 StackCount, MeshData& MeshData)
{
	uint32 BaseIndex = (uint32)MeshData.Vertices.size();

	float Y = 0.5f * Height;
	float DeltaTheta = 2.0f * XM_PI / SliceCount;

	// Duplicate cap ring vertices because the texture coordinates and normals differ.
	for (uint32 i = 0; i <= SliceCount; i++)
	{
		float X = TopRadius * cosf(i * DeltaTheta);
		float Z = TopRadius * sinf(i * DeltaTheta);

		// Scale down by the height to try and make top cap texture coordinate area proprortional to base.
		float U = X / Height + 0.5f;
		float V = Z / Height + 0.5f;

		MeshData.Vertices.push_back(Vertex(X, Y, Z, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, U, V));
	}

	// Cap center vertex
	MeshData.Vertices.push_back(Vertex(0.0f, Y, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.5f));

	// Index of center vertex.
	uint32 CenterIndex = (uint32)MeshData.Vertices.size() - 1;

	for (uint32 i = 0; i < SliceCount; i++)
	{
		MeshData.Indices32.push_back(CenterIndex);
		MeshData.Indices32.push_back(BaseIndex + i + 1);
		MeshData.Indices32.push_back(BaseIndex + i);
	}
}

void GeometryGenerator::BuildCylinderBottomCap(float BottomRadius, float TopRadius, float Height, uint32 SliceCount, uint32 StackCount, MeshData& MeshData)
{
	uint32 BaseIndex = (uint32)MeshData.Vertices.size();

	float Y = -0.5f * Height;
	float DeltaTheta = 2.0f * XM_PI / SliceCount;

	// Duplicate cap ring vertices because the texture coordinates and normals differ.
	for (uint32 i = 0; i <= SliceCount; i++)
	{
		float X = BottomRadius * cosf(i * DeltaTheta);
		float Z = BottomRadius * sinf(i * DeltaTheta);

		// Scale down by the height to try and make top cap texture coordinate area proprortional to base.
		float U = X / Height + 0.5f;
		float V = Z / Height + 0.5f;

		MeshData.Vertices.push_back(Vertex(X, Y, Z, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, U, V));
	}

	// Cap center vertex
	MeshData.Vertices.push_back(Vertex(0.0f, Y, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.5f));

	// Index of center vertex.
	uint32 CenterIndex = (uint32)MeshData.Vertices.size() - 1;

	for (uint32 i = 0; i < SliceCount; i++)
	{
		MeshData.Indices32.push_back(CenterIndex);
		MeshData.Indices32.push_back(BaseIndex + i);
		MeshData.Indices32.push_back(BaseIndex + i + 1);
	}
}

void GeometryGenerator::Subdivide(MeshData& InMeshData)
{
	// Save a copy of the input geometry.
	MeshData InputCopy = InMeshData;

	InMeshData.Vertices.resize(0);
	InMeshData.Indices32.resize(0);

	//       v1
	//       *
	//      / \
	//     /   \
	//  m0*-----*m1
	//   / \   / \
	//  /   \ /   \
	// *-----*-----*
	// v0    m2     v2
	uint32 NumTriangles = (uint32)InputCopy.Indices32.size() / 3;

	for (uint32 i = 0; i < NumTriangles; i++)
	{
		Vertex Vertex0 = InputCopy.Vertices[InputCopy.Indices32[i * 3 + 0]];
		Vertex Vertex1 = InputCopy.Vertices[InputCopy.Indices32[i * 3 + 1]];
		Vertex Vertex2 = InputCopy.Vertices[InputCopy.Indices32[i * 3 + 2]];

		// Generate the midpoints
		Vertex Midpoint0 = MidPoint(Vertex0, Vertex1);
		Vertex Midpoint1 = MidPoint(Vertex1, Vertex2);
		Vertex Midpoint2 = MidPoint(Vertex0, Vertex2);

		// Add new geometry.
		InMeshData.Vertices.push_back(Vertex0);
		InMeshData.Vertices.push_back(Vertex1);
		InMeshData.Vertices.push_back(Vertex2);
		InMeshData.Vertices.push_back(Midpoint0);
		InMeshData.Vertices.push_back(Midpoint1);
		InMeshData.Vertices.push_back(Midpoint2);

		InMeshData.Indices32.push_back(i * 6 + 0);
		InMeshData.Indices32.push_back(i * 6 + 3);
		InMeshData.Indices32.push_back(i * 6 + 5);

		InMeshData.Indices32.push_back(i * 6 + 3);
		InMeshData.Indices32.push_back(i * 6 + 4);
		InMeshData.Indices32.push_back(i * 6 + 5);

		InMeshData.Indices32.push_back(i * 6 + 5);
		InMeshData.Indices32.push_back(i * 6 + 4);
		InMeshData.Indices32.push_back(i * 6 + 2);

		InMeshData.Indices32.push_back(i * 6 + 3);
		InMeshData.Indices32.push_back(i * 6 + 1);
		InMeshData.Indices32.push_back(i * 6 + 4);
	}
}

GeometryGenerator::Vertex GeometryGenerator::MidPoint(const Vertex& Vertex0, const Vertex& Vertex1)
{
	XMVECTOR Position0 = XMLoadFloat3(&Vertex0.Position);
	XMVECTOR Position1 = XMLoadFloat3(&Vertex1.Position);

	XMVECTOR Normal0 = XMLoadFloat3(&Vertex0.Normal);
	XMVECTOR Normal1 = XMLoadFloat3(&Vertex1.Normal);

	XMVECTOR Tangent0 = XMLoadFloat3(&Vertex0.Tangent);
	XMVECTOR Tangent1 = XMLoadFloat3(&Vertex1.Tangent);

	XMVECTOR UV0 = XMLoadFloat2(&Vertex0.UV);
	XMVECTOR UV1 = XMLoadFloat2(&Vertex1.UV);

	// Compute the midpoints of all the attributes. Vectors need to be normalized
	// since linear interpolating can make them not unit length.
	XMVECTOR Position = 0.5f * (Position0 + Position1);
	XMVECTOR Normal = XMVector3Normalize(0.5f * (Normal0 + Normal1));
	XMVECTOR Tangent = XMVector3Normalize(0.5f * (Tangent0 + Tangent1));
	XMVECTOR UV = 0.5f * (UV0 + UV1);

	Vertex Midpoint;
	XMStoreFloat3(&Midpoint.Position, Position);
	XMStoreFloat3(&Midpoint.Normal, Normal);
	XMStoreFloat3(&Midpoint.Tangent, Tangent);
	XMStoreFloat2(&Midpoint.UV, UV);

	return Midpoint;
}