#include <math.h>
#include <string.h>

#include "ocean.h"
#include "shader.h"
#include "texture.h"

namespace pure3d
{

// The vertex colour list is kept in the file's byte order, which is (b, g, r, a) ---
// geometry.cpp says so where it loads the 0x1000a colour offsets, and the GL vertex
// shader swizzles it back with `col.bgr`. Anything built by hand has to match.
static inline pddiColour
VertexColour(float r, float g, float b, float a)
{
	if(r < 0.0f) r = 0.0f;
	if(r > 1.0f) r = 1.0f;
	if(g < 0.0f) g = 0.0f;
	if(g > 1.0f) g = 1.0f;
	if(b < 0.0f) b = 0.0f;
	if(b > 1.0f) b = 1.0f;
	if(a < 0.0f) a = 0.0f;
	if(a > 1.0f) a = 1.0f;
	return pddiColour((u8)(b*255.0f), (u8)(g*255.0f), (u8)(r*255.0f), (u8)(a*255.0f));
}

// ---------------------------------------------------------------- the wave model

// retail: the smoothstep at 0x47aad0, used for both the train fade and the shading
// parameter transitions
static inline float
SmoothStep(float x)
{
	if(x <= 0.0f) return 0.0f;
	if(x >= 1.0f) return 1.0f;
	return (3.0f - 2.0f*x)*x*x;
}

WaveModel::WaveModel(void)
 : minWaveLength(0.1f),
   maxWaveLength(12.0f),
   amplitudeRatio(0.013f),
   windDirectionMean(90.0f),
   windDirectionVariance(30.0f),
   speedScaleFactor(1.2f),
   trainLifeTime(15.0f),
   trainFadeTime(2.0f),
   rngA(0x0badf00du),
   rngB(0x12345678u),
   elapsed(0.0f)
{
	SetParameters();
	Initialize();
}

// retail: the PRNG folded into 0x6a8180 --- two 16-bit multiply-with-carry streams
// combined into a float in (-1, 1)
float
WaveModel::Rand(void)
{
	rngA = (rngA & 0xffff)*0x9069u + (rngA >> 16);
	rngB = (rngB & 0xffff)*0x4650u + (rngB >> 16);
	u32 r = (rngA << 16) + (rngB & 0xffff);
	union { u32 u; float f; } v;
	v.u = (r & 0x7fffffu) | 0x3f800000u;	// 1.0 .. 2.0
	v.f -= 1.0f;				// [0, 1)
	v.u |= r & 0x80000000u;			// (-1, 1)
	return v.f;
}

// retail: pure3d::WaveModel::SetWaveParameters 0x6a8370 -> the generator 0x6a8180.
// Note the CUBE: the wavelengths bunch up at the short end, so the first four trains ---
// the only ones retail's shader ever sees --- are 10..20 cm ripples.
void
WaveModel::SetParameters(void)
{
	const float g = 9.8f;
	const float twoPi = 6.2831855f;
	for(i32 i = NUM_TRAINS-1; i >= 0; i--) {
		float u = (float)i*(1.0f/15.0f) + Rand()*(1.0f/64.0f);
		float lambda = minWaveLength + u*u*u*(maxWaveLength - minWaveLength);
		if(lambda < 0.01f) lambda = 0.01f;
		waveNumber[i] = twoPi/lambda;
		amplitude[i] = lambda*amplitudeRatio;
		omega[i] = speedScaleFactor*sqrtf(g*waveNumber[i]);
		direction[i] = windDirectionMean + Rand()*windDirectionVariance;
	}
}

// retail: pure3d::Ocean::Initialize 0x689ad0 -> 0x6a83b0. The ages are staggered over
// the lifetime so the sixteen trains do not all fade together.
void
WaveModel::Initialize(void)
{
	for(i32 i = 0; i < NUM_TRAINS; i++) {
		Train *t = &trains[i];
		t->faded = 0.0f;
		t->amplitude = amplitude[i];
		t->direction = direction[i];
		t->omega = omega[i];
		t->waveNumber = waveNumber[i];
		t->age = (float)i*trainLifeTime*(1.0f/(float)NUM_TRAINS);
	}
}

// retail: pure3d::WaveModel::Update 0x6a8420 with the amplitude fade of 0x6a8300 ---
// a train lives trainLifeTime seconds, fades in and out over trainFadeTime at either
// end and then respawns with the same parameters.
void
WaveModel::Update(float dt)
{
	for(i32 i = 0; i < NUM_TRAINS; i++) {
		Train *t = &trains[i];
		t->age += dt;
		if(t->age <= trainLifeTime) {
			if(t->age < trainFadeTime)
				t->faded = SmoothStep(t->age/trainFadeTime)*t->amplitude;
			else if(t->age > trainLifeTime - trainFadeTime)
				t->faded = SmoothStep((trainLifeTime - t->age)/trainFadeTime)*t->amplitude;
			else
				t->faded = t->amplitude;
		} else {
			t->age = 0.0f;
			t->faded = 0.0f;
			t->amplitude = amplitude[i];
			t->waveNumber = waveNumber[i];
			t->omega = omega[i];
			t->direction = direction[i];
		}
	}
}

// retail: win32OceanRenderer's per-frame constant pack 0x6a0a70, which hands the shader
// amplitude, k*cos(dir), k*sin(dir) and omega*t for the first four trains
void
WaveModel::Pack(i32 numTrains)
{
	const float toRad = 0.017453292f;
	if(numTrains > NUM_TRAINS) numTrains = NUM_TRAINS;
	for(i32 i = 0; i < numTrains; i++) {
		const Train *t = &trains[i];
		float d = t->direction*toRad;
		packed[i].amplitude = t->faded;
		packed[i].kx = t->waveNumber*cosf(d);
		packed[i].kz = t->waveNumber*sinf(d);
		packed[i].phase = t->omega*elapsed;
		packed[i].waveLength = 6.2831855f/t->waveNumber;
	}
}

// retail: 0x6a8000, the sum of all sixteen amplitudes
float
WaveModel::MaxHeight(void) const
{
	float h = 0.0f;
	for(i32 i = 0; i < NUM_TRAINS; i++)
		h += trains[i].amplitude;
	return h;
}


// ---------------------------------------------------------------- Ocean

Ocean::Ocean(void)
 : shader(nil),
   baseShader(nil),
   reflectionTexture(nil),
   detailTexture(nil),
   foamTexture(nil),
   detailOpacity(0.225f),
   detailTextureScale(0.2f),
   foamMinHeight(0.15f),
   foamMaxHeight(1.0f),
   foamMaxOpacity(1.0f),
   waterColour(255, 255, 255, 255),
   seaLevel(0.0f),
   numSurfaceWaves(WaveModel::NUM_TRAINS),
   // the sky the reflection pass would sample, divided by the template's 0.25 so that
   // reflectionColourScale still reads through as the game wrote it
   reflectionColour(1.6f, 2.4f, 3.6f),
   projectedGrid(true),
   levelOfDetail(1),
   gridCells(128),
   cellSize(2.0f),
   farExtent(20000.0f),
   detailFadeStart(80.0f),
   detailFadeEnd(250.0f),
   detailGrazing(3.0f),
   waveFadeEnd(600.0f),
   waves(true),
   detailPass(true),
   lighting(true),
   cameraPosition(0.0f, 0.0f, 0.0f),
   cameraForward(0.0f, 0.0f, 1.0f),
   time(0.0f),
   lightAmbient(0.25f, 0.21f, 0.13f),
   numLights(1),
   numVertices(0),
   numTriangles(0),
   primBuffer(nil),
   builtVertices(0),
   builtIndices(0),
   sBase(0.0f, 0.0f, 0.0f),
   sAmbient(0.0f, 0.0f, 0.0f),
   sNumLights(0),
   sTexScale(0.2f),
   sFadeSpan(1.0f)
{
	reflectionColourScale[0] = 0.25f;
	reflectionColourScale[1] = 0.23f;
	reflectionColourScale[2] = 0.25f;
	reflectionColourScale[3] = 0.98f;
	// the noon sun, until the renderer hands us the frame's own lights
	lightColour[0] = Vector(0.39f, 0.38f, 0.28f);
	lightDirection[0] = Vector(-0.47f, -0.74f, 0.47f);
	for(i32 i = 1; i < MAX_LIGHTS; i++) {
		lightColour[i] = Vector(0.0f, 0.0f, 0.0f);
		lightDirection[i] = Vector(0.0f, -1.0f, 0.0f);
	}
}

Ocean::~Ocean(void)
{
	Release(shader);
	Release(baseShader);
	Release(reflectionTexture);
	Release(detailTexture);
	Release(foamTexture);
	Release(primBuffer);
}

void
Ocean::SetShader(Shader *sh)
{
	Assign(shader, sh);
}

void
Ocean::SetBaseShader(Shader *sh)
{
	Assign(baseShader, sh);
}

// retail: 0x689790 binds the three objects renderer::OceanRenderable_CreateInstance
// looked up by name into OceanPrimitive +0x3c/+0x40/+0x44.
void
Ocean::SetTextures(Texture *reflection, Texture *detail, Texture *foam)
{
	Assign(reflectionTexture, reflection);
	Assign(detailTexture, detail);
	Assign(foamTexture, foam);
}


// ---------------------------------------------------------------- the height field

// retail: pure3d::Ocean::Update 0x689c20. It advances OceanParams+0x180 (the travelling
// phase clock) once and then calls WaveModel::Update TWICE with the same dt --- so the
// 15 s train lifetime really runs at 2x while the phase does not. Quirk and all.
void
Ocean::Tick(float dt)
{
	time += dt;
	waveModel.elapsed = time;
	waveModel.Update(dt);
	waveModel.Update(dt);
}

// The surface, as retail's vertex shader evaluates it out of the packed constants
// (0x6a0a70) and as pure3d::Ocean::GetHeight (0x689710 -> 0x6a0bc0) evaluates it on the
// CPU --- the two agree by construction, which is what buoyancy needs:
//	h = seaLevel + sum amp_i * cos(omega_i*t - (x*kx_i + z*kz_i))
// The analytic normal falls out of the same terms.
void
Ocean::GetHeightAndNormal(float x, float z, float taper, float cell, float *h, Vector *n) const
{
	float y = seaLevel;
	float dhdx = 0.0f, dhdz = 0.0f;
	if(waves && taper > 0.0f) {
		i32 nw = numSurfaceWaves < WaveModel::NUM_TRAINS ? numSurfaceWaves : WaveModel::NUM_TRAINS;
		float invCell = cell > 0.0001f ? 1.0f/cell : 10000.0f;
		for(i32 i = 0; i < nw; i++) {
			const WaveModel::Packed *w = &waveModel.packed[i];
			// a wave the grid cannot resolve would only alias: fade it out between
			// 1.5 and 3.5 cells per wavelength (Nyquist is 2 samples per wave)
			float res = (w->waveLength*invCell - 1.5f)*0.5f;
			if(res <= 0.0f) continue;
			if(res > 1.0f) res = 1.0f;
			float a = w->amplitude*taper*res;
			float p = w->phase - (x*w->kx + z*w->kz);
			y += a*cosf(p);
			float s = a*sinf(p);
			dhdx += s*w->kx;
			dhdz += s*w->kz;
		}
	}
	*h = y;
	if(n) {
		Vector v(-dhdx, 1.0f, -dhdz);
		float len = Norm(v);
		*n = len > 0.0f ? v/len : Vector(0.0f, 1.0f, 0.0f);
	}
}

// retail: pure3d::Ocean::GetHeight 0x689710 --- what the boats and the swim code ask.
// Retail clamps the train count to four here; we use the same count the surface uses so
// that a boat floats on the water you can see.
float
Ocean::GetHeight(float x, float z)
{
	float h;
	waveModel.Pack(numSurfaceWaves);
	GetHeightAndNormal(x, z, 1.0f, 0.0f, &h, nil);
	return h;
}


// ---------------------------------------------------------------- the grid

void
Ocean::NewPrimBuffer(i32 nvert, i32 nidx, const u16 *idx)
{
	if(primBuffer && builtVertices == nvert && builtIndices == nidx)
		return;
	Release(primBuffer);
	builtVertices = nvert;
	builtIndices = nidx;
	primBuffer = device->NewPrimBuffer(PDDI_PRIM_TRIANGLES,
		PDDI_V_POSITION|PDDI_V_NORMAL|PDDI_V_COLOUR|PDDI_V_UVCOUNT1, nvert, nidx);
	primBuffer->SetIndices((void*)idx);
}

// One grid vertex: the wave height and its analytic normal, the shading the unlit pddi
// shader cannot do, the detail texture's uv and its per-vertex opacity.
void
Ocean::EmitVertex(pddiPrimBufferStream *stream, float x, float z, float taper, float cell)
{
	float dx = x - cameraPosition.x, dz = z - cameraPosition.z;
	float dist = sqrtf(dx*dx + dz*dz);
	if(dist > waveFadeEnd)
		taper = 0.0f;
	float y;
	Vector nrm;
	GetHeightAndNormal(x, z, taper, cell, &y, &nrm);

	// the GL vertex shader's own lighting never runs for an unlit shader: do its job
	// here (ambient + the sum of N.L over the directional lights, doubled)
	Vector lit = sAmbient;
	for(i32 k = 0; k < sNumLights; k++) {
		const Vector &d = lightDirection[k];
		float l = -(nrm.x*d.x + nrm.y*d.y + nrm.z*d.z);
		if(l > 0.0f) lit = lit + lightColour[k]*l;
	}
	// The detail texture tiles every 1/DetailTextureScale metres (5 m with the game's
	// 0.2) and the GL backend uploads no mip maps, so it has to be faded out before it
	// turns into noise. Distance is only half of it: what really decides the texel
	// footprint on a flat plane is the GRAZING ANGLE, and at a metre or two above the
	// water almost the whole screen is grazing. So the opacity carries the sine of the
	// view elevation as well --- which is also roughly where the real surface stops
	// showing its own colour and turns into a mirror. Past the band the water is the
	// flat reflection colour, which the distance fog takes over from anyway.
	float fade = (detailFadeEnd - dist)/sFadeSpan;
	if(fade > 1.0f) fade = 1.0f;
	if(fade < 0.0f) fade = 0.0f;
	float hy = cameraPosition.y - y;
	if(hy < 0.0f) hy = -hy;
	float elev = hy/sqrtf(hy*hy + dist*dist + 0.0001f);
	elev *= detailGrazing;
	if(elev < 1.0f) fade *= elev;

	stream->Normal(nrm.x, nrm.y, nrm.z);
	stream->Colour(VertexColour(sBase.x*lit.x*2.0f, sBase.y*lit.y*2.0f,
	                            sBase.z*lit.z*2.0f, detailOpacity*fade));
	stream->TexCoord2(x*sTexScale, z*sTexScale);
	stream->Position(x, y, z);
}

// The fallback: a square, camera-centred, axis-aligned world grid of (N+1)^2 vertices
// plus the 8 corners of a flat skirt --- four trapezoids from the grid's edge out to
// farExtent. The waves are tapered to zero over the outermost fifth of the grid so the
// two meet exactly.
void
Ocean::BuildWorldGrid(void)
{
	if(gridCells < 4) gridCells = 4;
	if(gridCells > 250) gridCells = 250;	// (251+1)^2 + 8 still fits a u16 index
	i32 N = gridCells;
	i32 nvert = (N+1)*(N+1) + 8;
	i32 nidx = N*N*6 + 4*6;

	if(primBuffer == nil || builtVertices != nvert || builtIndices != nidx) {
		u16 *idx = new u16[nidx];
		i32 n = 0;
		for(i32 j = 0; j < N; j++)
			for(i32 i = 0; i < N; i++) {
				u16 a = (u16)(j*(N+1) + i);
				idx[n++] = a;             idx[n++] = (u16)(a + N+1); idx[n++] = (u16)(a + 1);
				idx[n++] = (u16)(a + 1);  idx[n++] = (u16)(a + N+1); idx[n++] = (u16)(a + N+2);
			}
		// the skirt: inner corners at base+0..3, outer at base+4..7, both in the
		// order (-,-), (+,-), (+,+), (-,+)
		u16 base = (u16)((N+1)*(N+1));
		for(i32 s = 0; s < 4; s++) {
			idx[n++] = (u16)(base + s);
			idx[n++] = (u16)(base + 4 + s);
			idx[n++] = (u16)(base + 4 + (s+1)%4);
			idx[n++] = (u16)(base + s);
			idx[n++] = (u16)(base + 4 + (s+1)%4);
			idx[n++] = (u16)(base + (s+1)%4);
		}
		NewPrimBuffer(nvert, nidx, idx);
		delete[] idx;
	}

	float c = cellSize;
	float half = (float)N*c*0.5f;
	// snap the grid to the cell lattice so the mesh does not crawl under the waves
	float ox = floorf(cameraPosition.x/c)*c;
	float oz = floorf(cameraPosition.z/c)*c;
	float ext = farExtent > half*1.5f ? farExtent : half*1.5f;
	float halfCells = (float)N*0.5f;

	pddiPrimBufferStream *stream = primBuffer->Lock();
	for(i32 j = 0; j <= N; j++)
		for(i32 i = 0; i <= N; i++) {
			float rx = fabsf((float)i - halfCells)/halfCells;
			float rz = fabsf((float)j - halfCells)/halfCells;
			float r = rx > rz ? rx : rz;
			float taper = (1.0f - r)*5.0f;
			if(taper > 1.0f) taper = 1.0f;
			EmitVertex(stream, ox + ((float)i - halfCells)*c,
			                   oz + ((float)j - halfCells)*c, taper, c);
		}
	static const float sx[4] = { -1.0f,  1.0f, 1.0f, -1.0f };
	static const float sz[4] = { -1.0f, -1.0f, 1.0f,  1.0f };
	for(i32 s = 0; s < 8; s++) {
		float d = s < 4 ? half : ext;
		EmitVertex(stream, ox + sx[s&3]*d, oz + sz[s&3]*d, 0.0f, c);
	}
	primBuffer->Unlock(stream);
	numVertices = nvert;
	numTriangles = nidx/3;
}

// Retail's grid, rebuilt on the CPU: a PROJECTED grid (0x6adde0 + the transform of
// 0x6a0940, re/notes/ocean.md §5). The rows are angles from straight down, evenly
// spaced, so the tessellation is uniform on SCREEN --- dense right in front of the
// camera, stretching to the horizon, and it always covers the whole frame out to the
// far plane with no skirt. Retail builds the three meshes once and lets a
// Scale(cameraHeight)*RotateY(cameraYaw)*Translate(camXZ) matrix and the vertex shader
// do the rest; we have to write the vertices, so the same maths happens here.
void
Ocean::BuildProjectedGrid(void)
{
	static const i32 lodSize[3] = { 50, 110, 170 };
	if(levelOfDetail < 0) levelOfDetail = 0;
	if(levelOfDetail > 2) levelOfDetail = 2;
	i32 n = lodSize[levelOfDetail];
	i32 segs = n - 2;			// 48 / 108 / 168 points across a row
	float half = (float)(segs/2);

	// the vertical fov and the aspect ratio out of the projection matrix
	// (Matrix::Perspective: e[5] = 1/tan(fovY/2), e[0] = e[5]/aspect)
	const Matrix &proj = context->GetProjectionMatrix();
	float fovY = proj.e[5] > 0.001f ? 2.0f*atanf(1.0f/proj.e[5]) : 1.2f;
	float aspect = proj.e[0] > 0.001f ? proj.e[5]/proj.e[0] : 1.777f;
	float S = fovY*1.4f/(float)n;			// the angular step
	float T = tanf(fovY*1.4f*0.5f)*aspect;		// half width per unit depth

	// the camera height above the water is the whole scale of the grid; a camera at or
	// below the surface has nothing to project onto, so it falls back (Display)
	float h = cameraPosition.y - seaLevel;
	float maxTan = farExtent/h;

	// count the rows: from a little behind the nadir out to just short of the horizon,
	// plus one clamped row at the far extent
	i32 rings = 0;
	for(float A = -3.0f*S; A < 1.5707963f - S/3.0f; A += S)
		rings++;
	if(rings < 2) rings = 2;
	if(rings > 400) rings = 400;
	i32 rows = rings + 1;
	i32 nvert = segs*rows;
	i32 nidx = (segs-1)*(rows-1)*6;

	if(primBuffer == nil || builtVertices != nvert || builtIndices != nidx) {
		u16 *idx = new u16[nidx];
		i32 m = 0;
		for(i32 j = 0; j < rows-1; j++)
			for(i32 i = 0; i < segs-1; i++) {
				u16 a = (u16)(j*segs + i);
				idx[m++] = a;             idx[m++] = (u16)(a + segs); idx[m++] = (u16)(a + 1);
				idx[m++] = (u16)(a + 1);  idx[m++] = (u16)(a + segs); idx[m++] = (u16)(a + segs + 1);
			}
		NewPrimBuffer(nvert, nidx, idx);
		delete[] idx;
	}

	// the grid's own axes: +z along the camera's horizontal heading, +x to its right
	Vector fwd(cameraForward.x, 0.0f, cameraForward.z);
	float len = Norm(fwd);
	if(len < 0.0001f) fwd = Vector(0.0f, 0.0f, 1.0f);
	else fwd = fwd/len;
	Vector right(fwd.z, 0.0f, -fwd.x);

	pddiPrimBufferStream *stream = primBuffer->Lock();
	float A = -3.0f*S;
	float prevZ = 0.0f;
	for(i32 j = 0; j < rows; j++, A += S) {
		float t, w;
		if(j == rows-1) {
			// the clamped far row: everything from the last ring out to the far
			// extent, the sliver that reaches the horizon
			t = maxTan;
			w = T*maxTan;		// T/cos(A) -> T*tan(A) as A -> 90 degrees
		} else {
			t = tanf(A);
			float ca = cosf(A);
			w = T/(ca > 0.0005f ? ca : 0.0005f);
			if(t > maxTan) { t = maxTan; w = T*maxTan; }
		}
		float z = t*h;
		float wx = w*h;
		// the local quad size, which is what decides how short a wave this row can
		// still carry (retail's own vertex has 1/quad-diagonal in its y for this)
		float cell = wx/half;
		float dz = j > 0 ? z - prevZ : cell;
		if(dz > cell) cell = dz;
		prevZ = z;
		for(i32 i = 0; i < segs; i++) {
			float x = wx*((float)i/half) - wx;
			EmitVertex(stream, cameraPosition.x + right.x*x + fwd.x*z,
			                   cameraPosition.z + right.z*x + fwd.z*z, 1.0f, cell);
		}
	}
	primBuffer->Unlock(stream);
	numVertices = nvert;
	numTriangles = nidx/3;
}

void
Ocean::Display(void)
{
	if(shader == nil && baseShader == nil)
		return;
	waveModel.Pack(numSurfaceWaves);

	sAmbient = lightAmbient;
	sNumLights = numLights < MAX_LIGHTS ? numLights : MAX_LIGHTS;
	if(!lighting) {
		sAmbient = Vector(0.5f, 0.5f, 0.5f);
		sNumLights = 0;
	}
	// waterColour * the sky the reflection pass would have sampled * the template's
	// reflection colour scale
	sBase = Vector(waterColour.R()/255.0f * reflectionColour.x * reflectionColourScale[0],
	               waterColour.G()/255.0f * reflectionColour.y * reflectionColourScale[1],
	               waterColour.B()/255.0f * reflectionColour.z * reflectionColourScale[2]);
	sTexScale = detailTextureScale;
	sFadeSpan = detailFadeEnd - detailFadeStart;
	if(sFadeSpan < 1.0f) sFadeSpan = 1.0f;

	// a projected grid needs the camera to be above the water by enough that the rows
	// do not collapse onto each other
	if(projectedGrid && cameraPosition.y - seaLevel > 1.0f)
		BuildProjectedGrid();
	else
		BuildWorldGrid();

	// The display list has already pushed the node matrix (the identity for the ocean)
	// and the viewer's x flip; the grid is built in those coordinates, so nothing else
	// goes on the matrix stack.
	//
	// Two passes, where retail's PC ocean is one (`ocean_pass2` is loaded and never
	// used) --- re/notes/ocean.md §6:
	//   1. the flat reflection colour, opaque, Z WRITE OFF
	//   2. the `ocean_text` detail texture over it, alpha blended at DetailOpacity,
	//      z write on
	// Pass 1 leaves the depth buffer alone so that pass 2 --- the same geometry at the
	// same depth --- still passes the z test (pddi never sets a compare function, so
	// GL's default GL_LESS is in force and a second pass at equal depth would be
	// dropped). Pass 2 then lays the water's depth down.
	bool zwrite = context->GetZWrite();
	bool second = shader && detailPass && baseShader;
	if(baseShader) {
		context->SetZWrite(!second);
		context->DrawPrimBuffer(baseShader->GetShader(), primBuffer);
	}
	if(second || (baseShader == nil && shader)) {
		context->SetZWrite(true);
		context->DrawPrimBuffer(shader->GetShader(), primBuffer);
	}
	context->SetZWrite(zwrite);
}

}
