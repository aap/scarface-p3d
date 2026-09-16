#pragma once

#include "../core.h"
#include "../gmath.h"

namespace renderer
{

using namespace core;
using namespace math;

// retail: the engine's TimeInfo. Renderable::Tick (0x4740c0) reads two different
// fields of it: +0x08 for the age and +0x0c for the fade step.
struct TimeInfo
{
	float dt;	// +0x08
	float fadeDt;	// +0x0c
};

// retail: pure3d::Camera, vtable 0x0076a684, 0x104 bytes, ctor 0x68b850.
// renderer:: only ever asks it three things (notes/renderspine.md §4.1):
// where it is, how much to scale draw distances by, and whether a sphere is inside
// the frustum. Retail keeps near/far/fov and rebuilds six camera-space planes lazily
// (vslot 30); we take the same six planes out of the world->clip matrix instead.
class Camera
{
	Vector position;
	float planes[6][4];	// world space, normalised {nx, ny, nz, d}
	float drawDistScale;
public:
	Camera(void);

	// retail: Camera::GetPosition, vslot 24 [vtbl+0x60], 0x69e690
	void GetPosition(Vector *out) const { *out = position; }
	const Vector &GetPosition(void) const { return position; }
	void SetPosition(const Vector &p) { position = p; }

	// retail: Camera vslot 8 [vtbl+0x20], 0x69e250 (draw-distance scale + LOD bias);
	// Renderable::Display clamps it to <= 1
	float GetDrawDistanceScale(void) const { return drawDistScale; }
	void SetDrawDistanceScale(float s) { drawDistScale = s; }

	// retail: Camera::SphereVisible, vslot 22 [vtbl+0x58], 0x69e580, which transforms
	// the sphere into camera space and calls TestSphereCameraSpace (0x69e400)
	bool SphereVisible(const Vector &centre, float radius) const;

	void SetViewProjection(const Matrix &view, const Matrix &proj);
};

// retail: the two settable camera slots, g[0x8111d8] and g[0x8111dc]. They are
// distinct: Renderable::Display and Display_List's per-node culling use the CULLING
// camera, SortAllLists and RenderCameraLocked76 use the RENDERING camera.
// View_GetRenderingCamera 0x461ac0, View_GetCullingCamera 0x461ad0,
// View_SetRenderingCamera 0x464ca0, View_SetCullingCamera 0x464d00.
Camera *View_GetRenderingCamera(void);
Camera *View_GetCullingCamera(void);
void View_SetRenderingCamera(Camera *cam);
void View_SetCullingCamera(Camera *cam);

}
