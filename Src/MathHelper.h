//***************************************************************************************
// MathHelper.h by Frank Luna (C) 2011 All Rights Reserved.
//
// Helper math class.
//***************************************************************************************

#pragma once

#include <Windows.h>
#include <DirectXMath.h>
#include <cstdint>
#include "glm.h"

class MathHelper
{
public:
	// Returns random float in [0, 1).
	static float RandomF()
	{
		return (float)(rand()) / (float)RAND_MAX;
	}

	// Returns random float in [a, b).
	static float RandomF(float a, float b)
	{
		return a + RandomF()*(b-a);
	}

    static int Random(int a, int b)
    {
        return a + rand() % ((b - a) + 1);
    }

	template<typename T>
	static T Min(const T& a, const T& b)
	{
		return a < b ? a : b;
	}

	template<typename T>
	static T Max(const T& a, const T& b)
	{
		return a > b ? a : b;
	}
	 
	template<typename T>
	static T Lerp(const T& a, const T& b, float t)
	{
		return a + (b-a)*t;
	}

	template<typename T>
	static T Clamp(const T& x, const T& low, const T& high)
	{
		return x < low ? low : (x > high ? high : x); 
	}

	// Returns the polar angle of the point (x,y) in [0, 2*PI).
	static float AngleFromXY(float x, float y);

	static glm::vec4 SphericalToCartesian(float radius, float theta, float phi)
	{
		return glm::vec4(
			radius * std::sinf(phi) * std::cosf(theta),
			radius * std::cosf(phi),
			radius * std::sinf(phi) * std::sinf(theta),
			1.0f);
	}

	static glm::mat4 matrixReflect(glm::vec4 plane) {

		return glm::mat4{
			1 - 2 * plane.x * plane.x,    -2 * plane.x * plane.y,    -2 * plane.x * plane.z, -2 * plane.x * plane.w,
			   -2 * plane.y * plane.x, 1 - 2 * plane.y * plane.y,	 -2 * plane.y * plane.z, -2 * plane.y * plane.w,
			   -2 * plane.z * plane.x,  -  2 * plane.z * plane.y, 1 - 2 * plane.z * plane.z, -2 * plane.z * plane.w,
									0,						   0,                         0,                      1
		};
	}

    static DirectX::XMMATRIX InverseTranspose(DirectX::CXMMATRIX M)
	{
		// Inverse-transpose is just applied to normals.  So zero out 
		// translation row so that it doesn't get into our inverse-transpose
		// calculation--we don't want the inverse-transpose of the translation.
        DirectX::XMMATRIX A = M;
        A.r[3] = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

        DirectX::XMVECTOR det = DirectX::XMMatrixDeterminant(A);
        return DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(&det, A));
	}

    static DirectX::XMFLOAT4X4 Identity4x4()
    {
        static DirectX::XMFLOAT4X4 I(
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f);

        return I;
    }

    static DirectX::XMVECTOR RandUnitVec3();
    static DirectX::XMVECTOR RandHemisphereUnitVec3(DirectX::XMVECTOR n);

	static const float Infinity;
	static const float Pi;


};

