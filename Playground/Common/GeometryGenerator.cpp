#include "GeometryGenerator.h"

std::vector<SimpleVertex> GeometryGenerator::toSimpleVertices(const std::vector<Vertex>& vertices)
{
	std::vector<SimpleVertex> simpleVertices;

	for (const auto& vertex : vertices)
	{
		SimpleVertex simpleVertex = vertex.toSimpleVertex();
		simpleVertices.emplace_back(simpleVertex);
	}

	return simpleVertices;
}

GeometryGenerator::MeshData GeometryGenerator::createQuad(float width, float height, float uvScale)
{
	Vertex vertex[4];

	float halfWidth = 0.5f * width;
	float halfHeight = 0.5f * height;

	vertex[0] = { -halfWidth, -halfHeight, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	vertex[1] = {  halfWidth, -halfHeight, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, uvScale, 0.0f };
	vertex[2] = {  halfWidth,  halfHeight, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, uvScale, uvScale };
	vertex[3] = { -halfWidth,  halfHeight, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, uvScale };

	MeshData meshData;

	meshData.vertices.assign(std::begin(vertex), std::end(vertex));

	uint32 indices[6] = { 0, 1, 2, 2, 3, 0 };

	meshData.indices32.assign(std::begin(indices), std::end(indices));
	
	return meshData;
}

GeometryGenerator::MeshData GeometryGenerator::createBox(float width, float height, float depth, uint32_t numSubdivisions, float uvScale)
{
    //
	// Create the vertices.
	//

	Vertex vertex[24];

	float halfWidth = 0.5f * width;
	float halfHeight = 0.5f * height;
	float halfDepth = 0.5f * depth;
    
	// Fill in the back face vertex data.
	vertex[0] = Vertex(+halfWidth, -halfHeight, -halfDepth, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	vertex[1] = Vertex(-halfWidth, -halfHeight, -halfDepth, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
	vertex[2] = Vertex(-halfWidth, +halfHeight, -halfDepth, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
	vertex[3] = Vertex(+halfWidth, +halfHeight, -halfDepth, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);

	// Fill in the front face vertex data.
	vertex[4] = Vertex(-halfWidth, -halfHeight, +halfDepth, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	vertex[5] = Vertex(+halfWidth, -halfHeight, +halfDepth, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
	vertex[6] = Vertex(+halfWidth, +halfHeight, +halfDepth, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
	vertex[7] = Vertex(-halfWidth, +halfHeight, +halfDepth, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f);

	// Fill in the top face vertex data.
	vertex[8]  = Vertex(-halfWidth, +halfHeight, +halfDepth, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	vertex[9]  = Vertex(+halfWidth, +halfHeight, +halfDepth, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
	vertex[10] = Vertex(+halfWidth, +halfHeight, -halfDepth, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
	vertex[11] = Vertex(-halfWidth, +halfHeight, -halfDepth, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);

	// Fill in the bottom face vertex data.
	vertex[12] = Vertex(-halfWidth, -halfHeight, -halfDepth, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	vertex[13] = Vertex(+halfWidth, -halfHeight, -halfDepth, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
	vertex[14] = Vertex(+halfWidth, -halfHeight, +halfDepth, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
	vertex[15] = Vertex(-halfWidth, -halfHeight, +halfDepth, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
												  
	// Fill in the left face vertex data.		  
	vertex[16] = Vertex(-halfWidth, -halfHeight, -halfDepth, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f);
	vertex[17] = Vertex(-halfWidth, -halfHeight, +halfDepth, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f);
	vertex[18] = Vertex(-halfWidth, +halfHeight, +halfDepth, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f);
	vertex[19] = Vertex(-halfWidth, +halfHeight, -halfDepth, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f);
												  
	// Fill in the right face vertex data.		  
	vertex[20] = Vertex(+halfWidth, -halfHeight, +halfDepth, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
	vertex[21] = Vertex(+halfWidth, -halfHeight, -halfDepth, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
	vertex[22] = Vertex(+halfWidth, +halfHeight, -halfDepth, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
	vertex[23] = Vertex(+halfWidth, +halfHeight, +halfDepth, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f);

	MeshData meshData;

	meshData.vertices.assign(std::begin(vertex), std::end(vertex));
 
	//
	// Create the indices.
	//

	uint32_t indices[36];

	// Fill in the front face index data
	indices[0] = 0; indices[1] = 1; indices[2] = 2;
	indices[3] = 0; indices[4] = 2; indices[5] = 3;

	// Fill in the back face index data
	indices[6] = 4; indices[7]  = 5; indices[8]  = 6;
	indices[9] = 4; indices[10] = 6; indices[11] = 7;

	// Fill in the top face index data
	indices[12] = 8; indices[13] =  9; indices[14] = 10;
	indices[15] = 8; indices[16] = 10; indices[17] = 11;

	// Fill in the bottom face index data
	indices[18] = 12; indices[19] = 13; indices[20] = 14;
	indices[21] = 12; indices[22] = 14; indices[23] = 15;

	// Fill in the left face index data
	indices[24] = 16; indices[25] = 17; indices[26] = 18;
	indices[27] = 16; indices[28] = 18; indices[29] = 19;

	// Fill in the right face index data
	indices[30] = 20; indices[31] = 21; indices[32] = 22;
	indices[33] = 20; indices[34] = 22; indices[35] = 23;

	meshData.indices32.assign(std::begin(indices), std::end(indices));

    // Put a cap on the number of subdivisions.
    numSubdivisions = std::min<uint32_t>(numSubdivisions, 6u);

	for (uint32_t i = 0; i < numSubdivisions; ++i)
	{
        subdivide(meshData);
	}

    return meshData;
}

GeometryGenerator::MeshData GeometryGenerator::createSphere(float radius, uint32 sliceCount, uint32 stackCount, float uvScale)
{
	MeshData meshData;

	//
	// Compute the vertices stating at the top pole and moving down the stacks.
	//

	// Poles: note that there will be texture coordinate distortion as there is
	// not a unique point on the texture map to assign to the pole when mapping
	// a rectangular texture onto a sphere.
	Vertex topVertex(0.0f, +radius, 0.0f, 0.0f, +1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	Vertex bottomVertex(0.0f, -radius, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);

	meshData.vertices.push_back(topVertex);

	float phiStep = glm::pi<float>() / stackCount;
	float thetaStep = 2.0f * glm::pi<float>() / sliceCount;

	// Compute vertices for each stack ring (do not count the poles as rings).
	for (uint32 i = 1; i <= stackCount - 1; ++i)
	{
		float phi = i * phiStep;

		// Vertices of ring.
		for (uint32 j = 0; j <= sliceCount; ++j)
		{
			float theta = j * thetaStep;

			Vertex vertex;

			// spherical to cartesian
			vertex.position.x = radius * std::sinf(phi) * std::cosf(theta);
			vertex.position.y = radius * std::cosf(phi);
			vertex.position.z = radius * std::sinf(phi) * std::sinf(theta);

			// Partial derivative of P with respect to theta
			vertex.tangent.x = -radius * std::sinf(phi) * std::sinf(theta);
			vertex.tangent.y = 0.0f;
			vertex.tangent.z = +radius * std::sinf(phi) * std::cosf(theta);

			vertex.tangent = glm::normalize(vertex.tangent);

			vertex.normal = glm::normalize(vertex.position);

			vertex.texcoord.x = theta / (glm::pi<float>() * 2.0f);
			vertex.texcoord.y = phi / glm::pi<float>();

			meshData.vertices.push_back(vertex);
		}
	}

	meshData.vertices.push_back(bottomVertex);

	//
	// Compute indices for top stack.  The top stack was written first to the vertex buffer
	// and connects the top pole to the first ring.
	//

	for (uint32 i = 1; i <= sliceCount; ++i)
	{
		meshData.indices32.push_back(0);
		meshData.indices32.push_back(i + 1);
		meshData.indices32.push_back(i);
	}

	//
	// Compute indices for inner stacks (not connected to poles).
	//

	// Offset the indices to the index of the first vertex in the first ring.
	// This is just skipping the top pole vertex.
	uint32 baseIndex = 1;
	uint32 ringVertexCount = sliceCount + 1;
	for (uint32 i = 0; i < stackCount - 2; ++i)
	{
		for (uint32 j = 0; j < sliceCount; ++j)
		{
			meshData.indices32.push_back(baseIndex + i * ringVertexCount + j);
			meshData.indices32.push_back(baseIndex + i * ringVertexCount + j + 1);
			meshData.indices32.push_back(baseIndex + (i + 1) * ringVertexCount + j);

			meshData.indices32.push_back(baseIndex + (i + 1) * ringVertexCount + j);
			meshData.indices32.push_back(baseIndex + i * ringVertexCount + j + 1);
			meshData.indices32.push_back(baseIndex + (i + 1) * ringVertexCount + j + 1);
		}
	}

	//
	// Compute indices for bottom stack.  The bottom stack was written last to the vertex buffer
	// and connects the bottom pole to the bottom ring.
	//

	// South pole vertex was added last.
	uint32 southPoleIndex = (uint32)meshData.vertices.size() - 1;

	// Offset the indices to the index of the first vertex in the last ring.
	baseIndex = southPoleIndex - ringVertexCount;

	for (uint32 i = 0; i < sliceCount; ++i)
	{
		meshData.indices32.push_back(southPoleIndex);
		meshData.indices32.push_back(baseIndex + i);
		meshData.indices32.push_back(baseIndex + i + 1);
	}

	return meshData;
}

GeometryGenerator::MeshData GeometryGenerator::createGeosphere(float radius, uint32 numSubdivisions, float uvScale)
{
	MeshData meshData;

	// Put a cap on the number of subdivisions.
	numSubdivisions = std::min<uint32>(numSubdivisions, 6u);

	// Approximate a sphere by tessellating an icosahedron.

	const float X = 0.525731f;
	const float Z = 0.850651f;

	glm::vec3 pos[12] =
		{
			glm::vec3(-X, 0.0f, Z), glm::vec3(X, 0.0f, Z),
			glm::vec3(-X, 0.0f, -Z), glm::vec3(X, 0.0f, -Z),
			glm::vec3(0.0f, Z, X), glm::vec3(0.0f, Z, -X),
			glm::vec3(0.0f, -Z, X), glm::vec3(0.0f, -Z, -X),
			glm::vec3(Z, X, 0.0f), glm::vec3(-Z, X, 0.0f),
			glm::vec3(Z, -X, 0.0f), glm::vec3(-Z, -X, 0.0f)};

	uint32 k[60] = {
			1, 4, 0, 4, 9, 0, 4, 5, 9, 8, 5, 4, 1, 8, 4,
			1, 10, 8, 10, 3, 8, 8, 3, 5, 3, 2, 5, 3, 7, 2,
			3, 10, 7, 10, 6, 7, 6, 11, 7, 6, 0, 11, 6, 1, 0,
			10, 1, 6, 11, 0, 9, 2, 11, 9, 5, 2, 9, 11, 2, 7};

	meshData.vertices.resize(12);
	meshData.indices32.assign(std::begin(k), std::end(k));

	for (uint32 i = 0; i < 12; ++i)
	{
		meshData.vertices[i].position = pos[i];
	}

	for (uint32 i = 0; i < numSubdivisions; ++i)
	{
		subdivide(meshData);
	}

	// Project vertices onto sphere and scale.
	for (uint32 i = 0; i < meshData.vertices.size(); ++i)
	{
		// Project onto unit sphere.
		meshData.vertices[i].normal = glm::normalize(meshData.vertices[i].position);

		// Project onto sphere.
		meshData.vertices[i].position = radius * meshData.vertices[i].normal;

		// Derive texture coordinates from spherical coordinates.
		float theta = glm::atan(meshData.vertices[i].position.z, meshData.vertices[i].position.x);

		// Put in [0, 2pi].
		if (theta < 0.0f)
			theta += glm::pi<float>();

		float phi = acosf(meshData.vertices[i].position.y / radius);

		meshData.vertices[i].texcoord.x = theta / glm::pi<float>();
		meshData.vertices[i].texcoord.y = phi / glm::pi<float>();

		// Partial derivative of P with respect to theta
		meshData.vertices[i].tangent.x = -radius * sinf(phi) * sinf(theta);
		meshData.vertices[i].tangent.y = 0.0f;
		meshData.vertices[i].tangent.z = +radius * sinf(phi) * cosf(theta);

		meshData.vertices[i].tangent = glm::normalize(meshData.vertices[i].tangent);
	}

	return meshData;
}

GeometryGenerator::MeshData GeometryGenerator::createCylinder(float bottomRadius, float topRadius, float height, uint32_t sliceCount, uint32_t stackCount, float uvScale)
{
	MeshData meshData;

	//
	// Build Stacks.
	//
	// 每层的高度
	float stackHeight = height / stackCount;

	// Amount to increment radius as we move up each stack level from bottom to top.
	// 每层之间的半径差(从底至顶)
	float radiusStep = (topRadius - bottomRadius) / stackCount;

	// 圆环的数量
	uint32_t ringCount = stackCount + 1;

	// Compute vertices for each stack ring starting at the bottom and moving up.
	for (uint32_t ringIndex = 0; ringIndex < ringCount; ++ringIndex)
	{
		// 第i环的高度值
		float y = -0.5f * height + ringIndex * stackHeight;
		// 第i环的半径值
		float r = bottomRadius + ringIndex * radiusStep;

		// vertices of ring
		// 圆环每个切片的角度值
		float dTheta = 2.0f * glm::pi<float>() / sliceCount;
		for (uint32_t sliceIndex = 0; sliceIndex <= sliceCount; ++sliceIndex)
		{
			Vertex vertex;

			float cosTheta = cosf(sliceIndex * dTheta);
			float sinTheta = sinf(sliceIndex * dTheta);

			// 求圆心位于圆点，处在x-z平面，半径为r的圆上点的坐标
			vertex.position = glm::vec3(r * cosTheta, y, r * sinTheta);

			// 计算纹理坐标
			vertex.texcoord.x = (float)sliceIndex / sliceCount;
			vertex.texcoord.y = 1.0f - (float)sliceIndex / stackCount;

			// Cylinder can be parameterized as follows, where we introduce v
			// parameter that goes in the same direction as the v tex-coord
			// so that the bitangent goes in the same direction as the v tex-coord.
			//   Let r0 be the bottom radius and let r1 be the top radius.
			//   y(v) = h - hv for v in [0,1].
			//   r(v) = r1 + (r0-r1)v
			//
			//   x(t, v) = r(v)*cos(t)
			//   y(t, v) = h - hv
			//   z(t, v) = r(v)*sin(t)
			//
			//  dx/dt = -r(v)*sin(t)
			//  dy/dt = 0
			//  dz/dt = +r(v)*cos(t)
			//
			//  dx/dv = (r0-r1)*cos(t)
			//  dy/dv = -h
			//  dz/dv = (r0-r1)*sin(t)

			// This is unit length.
			vertex.tangent = glm::vec3(-sinTheta, 0.0f, cosTheta);

			float dr = bottomRadius - topRadius;
			glm::vec3 bitangent(dr * cosTheta, -height, dr * sinTheta);

			glm::vec3 T = vertex.tangent;
			glm::vec3 B = bitangent;
			glm::vec3 N = glm::normalize(glm::cross(T, B));
			vertex.normal = N;

			meshData.vertices.push_back(vertex);
		}
	}

	// Add one because we duplicate the first and last vertex per ring
	// since the texture coordinates are different.
	uint32_t ringVertexCount = sliceCount + 1;

	// Compute indices for each stack.
	for (uint32_t i = 0; i < stackCount; ++i)
	{
		for (uint32_t j = 0; j < sliceCount; ++j)
		{
			meshData.indices32.push_back(i * ringVertexCount + j);
			meshData.indices32.push_back((i + 1) * ringVertexCount + j);
			meshData.indices32.push_back((i + 1) * ringVertexCount + j + 1);

			meshData.indices32.push_back(i * ringVertexCount + j);
			meshData.indices32.push_back((i + 1) * ringVertexCount + j + 1);
			meshData.indices32.push_back(i * ringVertexCount + j + 1);
		}
	}

	buildCylinderTopCap(bottomRadius, topRadius, height, sliceCount, stackCount, meshData);
	buildCylinderBottomCap(bottomRadius, topRadius, height, sliceCount, stackCount, meshData);

	return meshData;
}

GeometryGenerator::MeshData GeometryGenerator::createGrid(float width, float depth, uint32 m, uint32 n, float uvScale)
{
	MeshData meshData;

	uint32 vertexCount = m * n;
	uint32 faceCount = (m - 1) * (n - 1) * 2;

	//
	// Create the vertices.
	//

	float halfWidth = 0.5f * width;
	float halfDepth = 0.5f * depth;

	float dx = width / (n - 1);
	float dz = depth / (m - 1);

	float du = 1.0f / (n - 1) * uvScale;
	float dv = 1.0f / (m - 1) * uvScale;

	meshData.vertices.resize(vertexCount);
	for (uint32 i = 0; i < m; ++i)
	{
		float z = halfDepth - i * dz;
		for (uint32 j = 0; j < n; ++j)
		{
			float x = -halfWidth + j * dx;

			meshData.vertices[i * n + j].position = glm::vec3(x, 0.0f, z);
			meshData.vertices[i * n + j].normal = glm::vec3(0.0f, 1.0f, 0.0f);
			meshData.vertices[i * n + j].tangent = glm::vec3(1.0f, 0.0f, 0.0f);

			// Stretch texture over grid.
			meshData.vertices[i * n + j].texcoord.x = j * du;
			meshData.vertices[i * n + j].texcoord.y = i * dv;
		}
	}

	//
	// Create the indices.
	//

	meshData.indices32.resize(faceCount * 3); // 3 indices per face

	// Iterate over each quad and compute indices.
	uint32 k = 0;
	for (uint32 i = 0; i < m - 1; ++i)
	{
		for (uint32 j = 0; j < n - 1; ++j)
		{
			meshData.indices32[k] = i * n + j;
			meshData.indices32[k + 1] = i * n + j + 1;
			meshData.indices32[k + 2] = (i + 1) * n + j;

			meshData.indices32[k + 3] = (i + 1) * n + j;
			meshData.indices32[k + 4] = i * n + j + 1;
			meshData.indices32[k + 5] = (i + 1) * n + j + 1;

			k += 6; // next quad
		}
	}

	return meshData;
}

void GeometryGenerator::subdivide(MeshData& meshData) {
	// Save a copy of the input geometry.
	MeshData inputCopy = meshData;

	meshData.vertices.resize(0);
	meshData.indices32.resize(0);

	//       v1
	//       *
	//      / \
	//     /   \
	//  m0*-----*m1
	//   / \   / \
	//  /   \ /   \
	// *-----*-----*
	// v0    m2     v2

	uint32_t numTris = (uint32_t)inputCopy.indices32.size()/3;
	for(uint32_t i = 0; i < numTris; ++i)
	{
		Vertex v0 = inputCopy.vertices[ inputCopy.indices32[i*3+0] ];
		Vertex v1 = inputCopy.vertices[ inputCopy.indices32[i*3+1] ];
		Vertex v2 = inputCopy.vertices[ inputCopy.indices32[i*3+2] ];

		//
		// Generate the midpoints.
		//

        Vertex m0 = MidPoint(v0, v1);
        Vertex m1 = MidPoint(v1, v2);
        Vertex m2 = MidPoint(v0, v2);

		//
		// Add new geometry.
		//

		meshData.vertices.push_back(v0); // 0
		meshData.vertices.push_back(v1); // 1
		meshData.vertices.push_back(v2); // 2
		meshData.vertices.push_back(m0); // 3
		meshData.vertices.push_back(m1); // 4
		meshData.vertices.push_back(m2); // 5
 
		meshData.indices32.push_back(i * 6 + 0);
		meshData.indices32.push_back(i * 6 + 3);
		meshData.indices32.push_back(i * 6 + 5);

		meshData.indices32.push_back(i * 6 + 3);
		meshData.indices32.push_back(i * 6 + 4);
		meshData.indices32.push_back(i * 6 + 5);

		meshData.indices32.push_back(i * 6 + 5);
		meshData.indices32.push_back(i * 6 + 4);
		meshData.indices32.push_back(i * 6 + 2);

		meshData.indices32.push_back(i * 6 + 3);
		meshData.indices32.push_back(i * 6 + 1);
		meshData.indices32.push_back(i * 6 + 4);
	}
}

GeometryGenerator::Vertex GeometryGenerator::MidPoint(const Vertex& v0, const Vertex& v1) {
    glm::vec3 p0 = v0.position;
    glm::vec3 p1 = v1.position;

    glm::vec3 n0 = v0.normal;
    glm::vec3 n1 = v1.normal;

    glm::vec3 tan0 = v0.tangent;
    glm::vec3 tan1 = v1.tangent;

    glm::vec2 tex0 = v0.texcoord;
    glm::vec2 tex1 = v1.texcoord;

    // Compute the midpoints of all the attributes.  Vectors need to be normalized
    // since linear interpolating can make them not unit length.  
    glm::vec3 pos = 0.5f * (p0 + p1);
    glm::vec3 normal = glm::normalize(0.5f * (n0 + n1));
    glm::vec3 tangent = glm::normalize(0.5f * (tan0 + tan1));
    glm::vec2 tex = 0.5f * (tex0 + tex1);

    Vertex vertex;
    vertex.position = pos;
    vertex.normal = normal;
    vertex.tangent = tangent;
    vertex.texcoord = tex;

    return vertex;
}

void GeometryGenerator::buildCylinderTopCap(float bottomRadius, float topRadius, float height,
											uint32_t sliceCount, uint32_t stackCount, MeshData& meshData)
{
	uint32_t baseIndex = (uint32_t)meshData.vertices.size();

	float y = 0.5f*height;
	float dTheta = 2.0f * glm::pi<float>() / sliceCount;

	// Duplicate cap ring vertices because the texture coordinates and normals differ.
	for(uint32_t i = 0; i <= sliceCount; ++i)
	{
		float x = topRadius * cosf(i * dTheta);
		float z = topRadius * sinf(i * dTheta);

		// Scale down by the height to try and make top cap texture coord area
		// proportional to base.
		float u = x / height + 0.5f;
		float v = z / height + 0.5f;

		meshData.vertices.push_back( Vertex(x, y, z, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, u, v) );
	}

	// Cap center vertex.
	meshData.vertices.push_back( Vertex(0.0f, y, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.5f) );

	// Index of center vertex.
	uint32_t centerIndex = (uint32_t)meshData.vertices.size()-1;

	for(uint32_t i = 0; i < sliceCount; ++i)
	{
		meshData.indices32.push_back(centerIndex);
		meshData.indices32.push_back(baseIndex + i);
		meshData.indices32.push_back(baseIndex + i + 1);
	}
}

void GeometryGenerator::buildCylinderBottomCap(float bottomRadius, float topRadius, float height,
											   uint32_t sliceCount, uint32_t stackCount, MeshData& meshData)
{
	// 
	// Build bottom cap.
	//

	uint32_t baseIndex = (uint32_t)meshData.vertices.size();
	float y = -0.5f * height;

	// vertices of ring
	float dTheta = 2.0f * glm::pi<float>() / sliceCount;
	for(uint32_t i = 0; i <= sliceCount; ++i)
	{
		float x = bottomRadius*cosf(i * dTheta);
		float z = bottomRadius*sinf(i * dTheta);

		// Scale down by the height to try and make top cap texture coord area
		// proportional to base.
		float u = x / height + 0.5f;
		float v = z / height + 0.5f;

		meshData.vertices.push_back( Vertex(x, y, z, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, u, v) );
	}

	// Cap center vertex.
	meshData.vertices.push_back( Vertex(0.0f, y, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.5f) );

	// Cache the index of center vertex.
	uint32_t centerIndex = (uint32_t)meshData.vertices.size() - 1;

	for(uint32_t i = 0; i < sliceCount; ++i)
	{
		meshData.indices32.push_back(centerIndex);
		meshData.indices32.push_back(baseIndex + i + 1);
		meshData.indices32.push_back(baseIndex + i);
	}
}