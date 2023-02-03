#include "gmath.h"

#include <math.h>

namespace math
{



void
Matrix::Identity(void)
{
	e[0]  = 1.0f; e[1]  = 0.0f; e[2]  = 0.0f; e[3]  = 0.0f;
	e[4]  = 0.0f; e[5]  = 1.0f; e[6]  = 0.0f; e[7]  = 0.0f;
	e[8]  = 0.0f; e[9]  = 0.0f; e[10] = 1.0f; e[11] = 0.0f;
	e[12] = 0.0f; e[13] = 0.0f; e[14] = 0.0f; e[15] = 1.0f;
}

void
Matrix::Identity3(void)
{
	e[0]  = 1.0f; e[1]  = 0.0f; e[2]  = 0.0f;
	e[4]  = 0.0f; e[5]  = 1.0f; e[6]  = 0.0f;
	e[8]  = 0.0f; e[9]  = 0.0f; e[10] = 1.0f;
}

void
Matrix::SetPosition(const Vector &pos)
{
	e[12] = pos.x;
	e[13] = pos.y;
	e[14] = pos.z;
}

void
Matrix::InvertOrtho(void)
{
	float t1 = e[1];
	float t2 = e[2];
	float t6 = e[6];
	e[1] = e[4];
	e[4] = t1;
	e[2] = e[8];
	e[8] = t2;
	e[6] = e[9];
	e[9] = t6;
	float x = -e[12];
	float y = -e[13];
	float z = -e[14];
	e[12] = x*e[0] + y*e[4] + z*e[8];
	e[13] = x*e[1] + y*e[5] + z*e[9];
	e[14] = x*e[2] + y*e[6] + z*e[10];
}

void
Matrix::LookAt(const Vector &eye, const Vector &target, const Vector &up)
{
	Vector fwd = Normalized(target - eye);
	Vector right = Normalized(Cross(fwd, up));
	Vector newup = Normalized(Cross(right, fwd));
	*GetX() = right;
	*GetY() = newup;
	*GetZ() = -fwd;
	*GetPosition() = eye;
	e[3] = e[7] = e[11] = 0.0f;
	e[15] = 1.0f;
}

void
Matrix::Perspective(float FOV, float aspect, float near, float far)
{
	// vertical
	e[5] = 1.0f/tanf((FOV/180.0f*M_PI)/2.0f);
	e[0] = e[5]/aspect;

//	e[0] = 1.0f/tanf((FOV/180.0f*M_PI)/2.0f);
	e[1] = 0.0f;
	e[2] = 0.0f;
	e[3] = 0.0f;

	e[4] = 0.0f;
//	e[5] = e[0]*aspect;
	e[6] = 0.0f;
	e[7] = 0.0f;

	e[8] = 0.0f;
	e[9] = 0.0f;
	e[10] = -(far + near)/(far - near);
	e[11] = -1.0f;

	e[12] = 0.0f;
	e[13] = 0.0f;
	e[14] = -2.0f*far*near/(far - near);
	e[15] = 0.0f;
}


// this seems backwards
Matrix
Multiply(const Matrix &m1, const Matrix &m2)
{
	Matrix m;

	m.e[ 0] = m1.e[0]*m2.e[0] + m1.e[1]*m2.e[4] + m1.e[2]*m2.e[8];
	m.e[ 1] = m1.e[0]*m2.e[1] + m1.e[1]*m2.e[5] + m1.e[2]*m2.e[9];
	m.e[ 2] = m1.e[0]*m2.e[2] + m1.e[1]*m2.e[6] + m1.e[2]*m2.e[10];
	m.e[ 3] = 0.0f;
	m.e[ 4] = m1.e[4]*m2.e[0] + m1.e[5]*m2.e[4] + m1.e[6]*m2.e[8];
	m.e[ 5] = m1.e[4]*m2.e[1] + m1.e[5]*m2.e[5] + m1.e[6]*m2.e[9];
	m.e[ 6] = m1.e[4]*m2.e[2] + m1.e[5]*m2.e[6] + m1.e[6]*m2.e[10];
	m.e[ 7] = 0.0f;
	m.e[ 8] = m1.e[8]*m2.e[0] + m1.e[9]*m2.e[4] + m1.e[10]*m2.e[8];
	m.e[ 9] = m1.e[8]*m2.e[1] + m1.e[9]*m2.e[5] + m1.e[10]*m2.e[9];
	m.e[10] = m1.e[8]*m2.e[2] + m1.e[9]*m2.e[6] + m1.e[10]*m2.e[10];
	m.e[11] = 0.0f;
	m.e[12] = m1.e[12]*m2.e[0] + m1.e[13]*m2.e[4] + m1.e[14]*m2.e[8]  + m2.e[12];
	m.e[13] = m1.e[12]*m2.e[1] + m1.e[13]*m2.e[5] + m1.e[14]*m2.e[9]  + m2.e[13];
	m.e[14] = m1.e[12]*m2.e[2] + m1.e[13]*m2.e[6] + m1.e[14]*m2.e[10] + m2.e[14];
	m.e[15] = 1.0f;

	return m;
}
Vector
Multiply(const Vector &v, const Matrix &m)
{
	Vector u;
	u.x = v.x*m.e[0] + v.y*m.e[4] + v.z*m.e[8] + m.e[12];
	u.y = v.x*m.e[1] + v.y*m.e[5] + v.z*m.e[9] + m.e[13];
	u.z = v.x*m.e[2] + v.y*m.e[6] + v.z*m.e[10] + m.e[14];
	return u;
}



void
Quaternion::SetMatrix(Matrix &mat)
{
	if(fabs(w) > 0.00001f) {
		mat.e[0] = 1.0f - 2.0f*y*y - 2.0f*z*z;
		mat.e[4] = 2.0f*x*y - 2.0f*z*w;
		mat.e[8] = 2.0f*x*z + 2.0f*y*w;

		mat.e[1] = 2.0f*x*y + 2.0f*z*w;
		mat.e[5] = 1.0f - 2.0f*x*x - 2.0f*z*z;
		mat.e[9] = 2.0f*y*z - 2.0f*x*w;

		mat.e[2] = 2.0f*x*z - 2.0f*y*w;
		mat.e[6] = 2.0f*y*z + 2.0f*x*w;
		mat.e[10] = 1.0f - 2.0f*x*x - 2.0f*y*y;
	} else
		mat.Identity3();
}

void
Quaternion::FromMatrix(const Matrix &mat)
{
	int next[] = { 1, 2, 0 };
	float q[4];
	float trace = mat.e[0] + mat.e[5] + mat.e[10];
	if(trace > -1.0f) {	// BUG? pure3d uses 0.0f
		int i = 0;	// find maximum on diagonal
		if(mat.e[5] > mat.e[0])
			i = 1;
		if(mat.e[10] > mat.e[i*5])
			i = 2;
		int j = next[i];
		int k = next[j];
		float r = sqrtf(1.0f + mat.e[i*5] - mat.e[j*5] - mat.e[k*5]);
		q[i] = 0.5f*r;
		r = 0.5f/r;
		q[3] = (mat.e[4*k + j] - mat.e[4*j + k]) * r;
		q[j] = (mat.e[4*j + i] + mat.e[4*i + j]) * r;
		q[k] = (mat.e[4*k + i] + mat.e[4*i + k]) * r;

		w = -q[3];
		x = q[0];
		y = q[1];
		z = q[2];
	} else {
		float r = sqrtf(1.0f + trace);
		w = -0.5f*r;
		r = 0.5f/r;
		x = (mat.e[9] - mat.e[6]) * r;
		y = (mat.e[2] - mat.e[8]) * r;
		z = (mat.e[4] - mat.e[1]) * r;
	}
}

void
Quaternion::FromAxisAngle(float ax, float ay, float az, float rad)
{
	w = cosf(rad/2.0f);
	float s = sinf(rad/2.0f);
	float n = 1.0f/sqrtf(ax*ax + ay*ay + az*az);
	x = s*ax*n;
	y = s*ay*n;
	z = s*az*n;
}


#define max(a,b) ((a) > (b) ? (a) : (b))
#define min(a,b) ((a) < (b) ? (a) : (b))

void
Box3D::ContainPoint(const Vector &p)
{
	low.x = min(low.x, p.x);
	low.y = min(low.y, p.y);
	low.z = min(low.z, p.z);
	high.x = max(high.x, p.x);
	high.y = max(high.y, p.y);
	high.z = max(high.z, p.z);
}

Sphere
Box3D::GetSphere(void)
{
	Sphere s;
	if(low.x > high.x || low.y > high.y || low.z > high.z) {
		s.centre = Vector(0.0f, 0.0f, 0.0f);
		s.radius = 0.0f;
	} else {
		s.centre = (high + low)*0.5f;
		s.radius = Norm((high - low)*0.5f);
	}
	return s;
}

Box3D
Box3D::Transform(const Matrix &m)
{
	// Not the real algorithm
	Vector newlow = Multiply(low, m);
	Vector newhigh = Multiply(high, m);
	Box3D b;
	b.Init();
	b.ContainPoint(newlow);
	b.ContainPoint(newhigh);
	return b;
}

}
