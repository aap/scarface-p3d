class CCamera
{
public:
	math::Vector m_position;
	math::Vector m_target;
	math::Vector m_up;
	math::Vector m_localup;
	math::Vector m_at;
	math::Matrix m_viewMat;
	math::Matrix m_projMat;

	float m_near, m_far;
	float m_fov, m_aspectRatio;

	void Process(void);
	void DrawTarget(void);

	void setTarget(math::Vector target);
	float getHeading(void);

	void turn(float yaw, float pitch);
	void orbit(float yaw, float pitch);
	void dolly(float dist);
	void zoom(float dist);
	void pan(float x, float y);
	void setDistanceFromTarget(float dist);

	void update(void);
	float distanceTo(math::Vector v);
	float distanceToTarget(void);
//	float minDistToSphere(float r);
	CCamera(void);

//	bool IsSphereVisible(rw::Sphere *sph, rw::Matrix *xform);
};
