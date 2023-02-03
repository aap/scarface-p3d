#pragma once

#include <math.h>

namespace math
{

class Quaternion;
class Matrix;

class Vector2
{
public:
	float x, y;
};

class Vector
{
public:
	float x, y, z;
	Vector(void) {}
	Vector(float x, float y, float z) : x(x), y(y), z(z) {}
	Vector(const Quaternion &q);
};
inline Vector operator+(const Vector &u, const Vector &v) { return Vector(u.x+v.x, u.y+v.y, u.z+v.z); }
inline Vector operator-(const Vector &u, const Vector &v) { return Vector(u.x-v.x, u.y-v.y, u.z-v.z); }
inline Vector operator*(const Vector &v, float r) { return Vector(v.x*r, v.y*r, v.z*r); }
inline Vector operator*(float r, const Vector &v) { return Vector(v.x*r, v.y*r, v.z*r); }
inline Vector operator/(const Vector &v, float r) { return v * (1.0f/r); }
inline Vector operator-(const Vector &v) { return Vector(-v.x, -v.y, -v.z); }
inline float NormSq(const Vector &v) { return v.x*v.x + v.y*v.y + v.z*v.z; }
inline float Norm(const Vector &v) { return sqrtf(NormSq(v)); }
inline Vector Normalized(const Vector &v) { return v/Norm(v); }
inline float Dot(const Vector &u, const Vector &v) { return u.x*v.x + u.y*v.y + u.z*v.z; }
inline Vector Cross(const Vector &u, const Vector &v) {
	return Vector(u.y*v.z - u.z*v.y,
	              u.z*v.x - u.x*v.z,
	              u.x*v.y - u.y*v.x);
}

class Vector4
{
public:
	float x, y, z, w;
	Vector4(void) {}
	Vector4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
};

class Quaternion
{
public:
	float x, y, z, w;
	Quaternion(void) {}
	Quaternion(float x, float y, float z, float w = 0.0f) : x(x), y(y), z(z), w(w) {}
	Quaternion(const Vector &v, float w = 0.0f) : x(v.x), y(v.y), z(v.z), w(w) {}
	void SetMatrix(Matrix &rot);
	void FromMatrix(const Matrix &mat);
	void FromAxisAngle(float x, float y, float z, float rad);
};
inline Vector::Vector(const Quaternion &q) : x(q.x), y(q.y), z(q.z) {}
inline Quaternion operator+(const Quaternion &p, const Quaternion &q) { return Quaternion(p.x+q.x, p.y+q.y, p.z+q.z, p.w+q.w); }
inline Quaternion operator-(const Quaternion &p, const Quaternion &q) { return Quaternion(p.x-q.x, p.y-q.y, p.z-q.z, p.w-q.w); }
inline Quaternion operator*(const Quaternion &q, float r) { return Quaternion(q.x*r, q.y*r, q.z*r, q.w*r); }
inline Quaternion operator*(float r, const Quaternion &q) { return Quaternion(q.x*r, q.y*r, q.z*r, q.w*r); }
inline Quaternion operator/(const Quaternion &q, float r) { return q * (1.0f/r); }
inline Quaternion operator-(const Quaternion &q) { return Quaternion(-q.x, -q.y, -q.z, -q.w); }
inline Quaternion operator~(const Quaternion &q) { return Quaternion(-q.x, -q.y, -q.z, q.w); }
inline Quaternion operator*(const Quaternion &p, const Quaternion &q) {
	return Quaternion(
		p.w*q.x + p.x*q.w + p.y*q.z - p.z*q.y,
		p.w*q.y + p.y*q.w + p.z*q.x - p.x*q.z,
		p.w*q.z + p.z*q.w + p.x*q.y - p.y*q.x,
		p.w*q.w - p.x*q.x - p.y*q.y - p.z*q.z
	);
}
inline Vector Rotate(const Vector &v, const Quaternion &q) { return Vector(q*Quaternion(v)*~q); }
inline Quaternion Rotation(float rad, const Vector &axis) { Quaternion q; q.FromAxisAngle(axis.x, axis.y, axis.z, rad); return q; }

class Matrix
{
public:
	float e[16];
	void Identity(void);
	void Identity3(void);
	Vector *GetX(void) { return (Vector*)&e[0]; }
	Vector *GetY(void) { return (Vector*)&e[4]; }
	Vector *GetZ(void) { return (Vector*)&e[8]; }
	Vector *GetPosition(void) { return (Vector*)&e[12]; }
	void SetPosition(const Vector &pos);
	void InvertOrtho(void);

	void LookAt(const Vector &eye, const Vector &target, const Vector &up);
	void Perspective(float FOV, float aspect, float near, float far);
};
Matrix Multiply(const Matrix &m1, const Matrix &m2);
Vector Multiply(const Vector &vec, const Matrix &mat);

class Sphere
{
public:
	Vector centre;
	float radius;

	Sphere(void) : centre(0.0f, 0.0f, 0.0f), radius(0.0f) {}
	Sphere(const Vector &centre, float radius) : centre(centre), radius(radius) {}
};

class Box3D
{
public:
	Vector low;
	Vector high;

	Box3D(void) : low(0.0f, 0.0f, 0.0f), high(0.0f, 0.0f, 0.0f) {}
	Box3D(const Vector &low, const Vector &high) : low(low), high(high) {}
	void Init(void) { low=Vector(1.0e12f,1.0e12f,1.0e12f); high=Vector(-1.0e12f,-1.0e12f,-1.0e12f); }
	void ContainPoint(const Vector &point);
	Sphere GetSphere(void);
	Box3D Transform(const Matrix &m);
};

}
