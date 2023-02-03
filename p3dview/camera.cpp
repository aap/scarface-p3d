#include "p3dview.h"

#define PI 3.14159265359f
#ifndef min
#define min(a, b) (a < b ? a : b)
#endif

#include "camera.h"

using namespace math;

#define DEGTORAD(r) ((r)/180.0f*M_PI)

void
CCamera::Process(void)
{
	float scale = avgTimeStep/1000.0f;
	float mouseSens = scale*20.0f;
//	float sensitivity = 1.0f;

	// Mouse
	// first person
	float dx = -input.MouseDX();
	float dy = -input.MouseDY();
	if(input.MouseDown(1)){
		if(/*input.AltDown() && */input.CtrlDown()){
			dolly(dy*mouseSens);
		}else{
			turn(DEGTORAD(dx)/2.0f*mouseSens, DEGTORAD(dy)/2.0f*mouseSens);
		}
	}

	// roughly 3ds max controls
	if(input.MouseDown(2)){
		if(/*input.AltDown() && */input.CtrlDown()){
			zoom(dy*mouseSens);
		}else if(input.AltDown()){
			orbit(DEGTORAD(dx)/2.0f*mouseSens, -DEGTORAD(dy)/2.0f*mouseSens);
		}else{
			float dist = distanceToTarget();
			pan(dx*mouseSens*dist/100.0f, -dy*mouseSens*dist/100.0f);
		}
	}

	float maxSpeed = 500.0f;
	float speedInc = 2.0f;

	// Keyboard
	static float speed = 0.0f;
	if(input.KeyDown('W'))
		speed += speedInc;
	else if(input.KeyDown('S'))
		speed -= speedInc;
	else
		speed = 0.0f;
	if(speed > maxSpeed) speed = maxSpeed;
	if(speed < -maxSpeed) speed = -maxSpeed;
	dolly(speed*scale);

	static float sidespeed = 0.0f;
	if(input.KeyDown('A'))
		sidespeed -= speedInc;
	else if(input.KeyDown('D'))
		sidespeed += speedInc;
	else
		sidespeed = 0.0f;
	if(sidespeed > maxSpeed) sidespeed = maxSpeed;
	if(sidespeed < -maxSpeed) sidespeed = -maxSpeed;
	pan(sidespeed*scale, 0.0f);


	if(input.KeyJustDown('P'))
		printf("campos: %.2f, %.2f, %.2f\n", m_position.x, m_position.y, m_position.z);
/*
	// Pad
	CPad *pad = CPad::GetPad(0);
	sensitivity = 1.0f;
	if(pad->NewState.r2){
		sensitivity = 2.0f;
		if(pad->NewState.l2)
			sensitivity = 4.0f;
	}else if(pad->NewState.l2)
		sensitivity = 0.5f;
	if(pad->NewState.square) zoom(0.4f*sensitivity*scale);
	if(pad->NewState.Cross) zoom(-0.4f*sensitivity*scale);
	orbit(pad->NewState.getLeftX()/25.0f*sensitivity*scale,
	                -pad->NewState.getLeftY()/25.0f*sensitivity*scale);
	turn(-pad->NewState.getRightX()/25.0f*sensitivity*scale,
	               pad->NewState.getRightY()/25.0f*sensitivity*scale);
	if(pad->NewState.up)
		dolly(2.0f*sensitivity*scale);
	if(pad->NewState.down)
		dolly(-2.0f*sensitivity*scale);

//	if(IsButtonJustDown(pad, start)){
//		printf("cam.position: %f, %f, %f\n", m_position.x, m_position.y, m_position.z);
//		printf("cam.target: %f, %f, %f\n", m_target.x, m_target.y, m_target.z);
//	}
*/
}

/*
void
CCamera::DrawTarget(void)
{
	float dist = distanceToTarget()/20.0f;
	rw::Vector x = { dist, 0.0f, 0.0f };
	rw::Vector y = { 0.0f, dist, 0.0f };
	rw::Vector z = { 0.0f, 0.0f, dist };
	RenderAxesWidget(this->m_target, x, y, z);
}
*/

void
CCamera::update(void)
{
	m_viewMat.LookAt(m_position, m_target, m_localup);
	m_viewMat.InvertOrtho();
	m_projMat.Perspective(m_fov, m_aspectRatio, m_near, m_far);
}

void
CCamera::setTarget(Vector target)
{
	m_position = m_position - (m_target - target);
	m_target = target;
}

float
CCamera::getHeading(void)
{
	Vector dir = m_target - m_position;
	float a = atan2(dir.y, dir.x)-PI/2.0f;
	return m_localup.y < 0.0f ? a-PI : a;
}

void
CCamera::turn(float yaw, float pitch)
{
	Vector dir = m_target - m_position;
	Vector yaxis = { 0.0f, 1.0f, 0.0f };
	Quaternion r = Rotation(yaw, yaxis);
	dir = Rotate(dir, r);
	m_localup = Rotate(m_localup, r);

	Vector right = Normalized(Cross(dir, m_localup));
	r = Rotation(pitch, right);
	dir = Rotate(dir, r);
	m_localup = Normalized(Cross(right, dir));
	if(m_localup.y >= 0.0) m_up.y = 1.0;
	else m_up.y = -1.0f;

	m_target = m_position + dir;
}

void
CCamera::orbit(float yaw, float pitch)
{
	Vector dir = m_target - m_position;
	Vector yaxis = { 0.0f, 1.0f, 0.0f };
	Quaternion r = Rotation(yaw, yaxis);
	dir = Rotate(dir, r);
	m_localup = Rotate(m_localup, r);

	Vector right = Normalized(Cross(dir, m_localup));
	r = Rotation(-pitch, right);
	dir = Rotate(dir, r);
	m_localup = Normalized(Cross(right, dir));
	if(m_localup.y >= 0.0) m_up.y = 1.0;
	else m_up.y = -1.0f;

	m_position = m_target - dir;
}

void
CCamera::dolly(float dist)
{
	Vector dir = Normalized(m_target - m_position)*dist;
	m_position = m_position + dir;
	m_target = m_target + dir;
}

void
CCamera::zoom(float dist)
{
	Vector dir = m_target - m_position;
	float curdist = Norm(dir);
	if(dist >= curdist)
		dist = curdist-0.3f;
	dir = Normalized(dir)*dist;
	m_position = m_position + dir;
}

void
CCamera::pan(float x, float y)
{
	Vector dir = Normalized(m_target - m_position);
	Vector right = Normalized(Cross(dir, m_up));
	Vector localup = Normalized(Cross(right, dir));
	dir = right*x + localup*y;
	m_position = m_position + dir;
	m_target = m_target + dir;
}

void
CCamera::setDistanceFromTarget(float dist)
{
	Vector dir = m_position - m_target;
	dir = Normalized(dir)*dist;
	m_position = m_target + dir;
}

float
CCamera::distanceTo(Vector v)
{
	return Norm(m_position - v);
}

float
CCamera::distanceToTarget(void)
{
	return Norm(m_position - m_target);
}

// calculate minimum distance to a sphere at the target
// so the whole sphere is visible
/*
float
CCamera::minDistToSphere(float r)
{
	float t = min(m_rwcam->viewWindow.x, m_rwcam->viewWindow.y);
	float a = atan(t);	// half FOV angle
	return r/sin(a);
}
*/

CCamera::CCamera()
{
	m_position = Vector(0.0f, 6.0f, 0.0f);
	m_target = Vector(0.0f, 0.0f, 0.0f);

	m_up = Vector(0.0f, 1.0f, 0.0f);
	m_localup = m_up;
	m_fov = 70.0f;
	m_aspectRatio = 1.0f;
	m_near = 0.1f;
	m_far = 2000.0f;
}

/*
bool
CCamera::IsSphereVisible(rw::Sphere *sph, rw::Matrix *xform)
{
	rw::Sphere sphere = *sph;
	rw::Vector::transformPoints(&sphere.center, &sphere.center, 1, xform);
	return m_rwcam->frustumTestSphere(&sphere) != rw::Camera::SPHEREOUTSIDE;
}
*/
