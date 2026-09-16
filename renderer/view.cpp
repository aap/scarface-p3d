#include "view.h"

#include <math.h>

namespace renderer
{

using namespace math;

// math::Multiply is affine only (it forces the last column), which is no good for a
// projection matrix, so do the full row-vector product here: clip = v * (m1 * m2).
static Matrix
Multiply4x4(const Matrix &m1, const Matrix &m2)
{
	Matrix m;
	for(int i = 0; i < 4; i++)
		for(int j = 0; j < 4; j++) {
			float a = 0.0f;
			for(int k = 0; k < 4; k++)
				a += m1.e[i*4+k] * m2.e[k*4+j];
			m.e[i*4+j] = a;
		}
	return m;
}

Camera::Camera(void)
 : position(0.0f, 0.0f, 0.0f), drawDistScale(1.0f)
{
	for(int i = 0; i < 6; i++) {
		planes[i][0] = planes[i][1] = planes[i][2] = 0.0f;
		planes[i][3] = 1.0e30f;	// everything visible until we are given a matrix
	}
}

// The six frustum planes of a world->clip matrix, in world space. With the row-vector
// convention gmath uses, clip.c = dot(v, column c of m) + m[3][c], so the half spaces
// -w <= x,y,z <= w give the planes directly as sums and differences of the columns.
void
Camera::SetViewProjection(const Matrix &view, const Matrix &proj)
{
	Matrix m = Multiply4x4(view, proj);
	static const int col[6][2] = {
		{ 0, +1 },	// left:   clip.x + clip.w >= 0
		{ 0, -1 },	// right:  clip.w - clip.x >= 0
		{ 1, +1 },	// bottom
		{ 1, -1 },	// top
		{ 2, +1 },	// near
		{ 2, -1 },	// far
	};
	for(int i = 0; i < 6; i++) {
		int c = col[i][0];
		float s = (float)col[i][1];
		float p[4];
		for(int j = 0; j < 4; j++)
			p[j] = m.e[j*4+3] + s*m.e[j*4+c];
		float len = sqrtf(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]);
		if(len > 0.0f) {
			float inv = 1.0f/len;
			p[0] *= inv; p[1] *= inv; p[2] *= inv; p[3] *= inv;
		}
		planes[i][0] = p[0]; planes[i][1] = p[1];
		planes[i][2] = p[2]; planes[i][3] = p[3];
	}
}

// retail: Camera::SphereVisible 0x69e580 / TestSphereCameraSpace 0x69e400 --- the same
// six half spaces (near, far and the four sides), just kept in world space here.
bool
Camera::SphereVisible(const Vector &centre, float radius) const
{
	for(int i = 0; i < 6; i++) {
		float d = planes[i][0]*centre.x + planes[i][1]*centre.y +
		          planes[i][2]*centre.z + planes[i][3];
		if(d < -radius)
			return false;
	}
	return true;
}


// retail: g_defaultCamera g[0x8111d4], g_renderCamera g[0x8111d8], g_cullCamera g[0x8111dc],
// all three initialised to the default camera by renderer::Init (0x465120).
static Camera defaultCamera;
static Camera *renderCamera = &defaultCamera;
static Camera *cullCamera = &defaultCamera;

Camera *View_GetRenderingCamera(void) { return renderCamera; }		// retail: 0x461ac0
Camera *View_GetCullingCamera(void) { return cullCamera; }		// retail: 0x461ad0
void View_SetRenderingCamera(Camera *cam) { renderCamera = cam; }	// retail: 0x464ca0
void View_SetCullingCamera(Camera *cam) { cullCamera = cam; }		// retail: 0x464d00

}
