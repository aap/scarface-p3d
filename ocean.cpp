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
   minWaveLength(0.1f),
   maxWaveLength(12.0f),
   amplitudeRatio(0.013f),
   windDirectionMean(90.0f),
   windDirectionVariance(30.0f),
   speedScaleFactor(1.2f),
   numWaves(8),
   // the sky the reflection pass would sample, divided by the template's 0.25 so that
   // reflectionColourScale still reads through as the game wrote it
   reflectionColour(1.6f, 2.4f, 3.6f),
   gridCells(128),
   cellSize(2.0f),
   farExtent(20000.0f),
   detailFadeStart(80.0f),
   detailFadeEnd(250.0f),
   waves(true),
   detailPass(true),
   lighting(true),
   cameraPosition(0.0f, 0.0f, 0.0f),
   time(0.0f),
   lightAmbient(0.25f, 0.21f, 0.13f),
   numLights(1),
   numWavesBuilt(0),
   builtMinLen(0.0f), builtMaxLen(0.0f), builtRatio(0.0f),
   builtDir(0.0f), builtVar(0.0f), builtSpeed(0.0f),
   primBuffer(nil),
   builtCells(0)
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
	memset(waveSet, 0, sizeof(waveSet));
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

// Retail seeds 16 wave trains from the tuning template in Ocean::Initialize (0x006a83b0);
// what it does with them is a scrolling height/normal texture, not a vertex displacement
// (re/notes/ocean.md §4). This is the vertex-displacement reading of the same six
// numbers: wavelengths spread geometrically between MinWaveLength and MaxWaveLength,
// directions spread around WindDirectionMean by +-WindDirectionVariance, amplitude =
// wavelength*AmplitudeRatio and the phase speed out of the deep-water dispersion
// relation w = sqrt(g*k), scaled by SpeedScaleFactor. With the shipped values the
// tallest wave in the game is 12.0*0.013 = 16 cm.
void
Ocean::BuildWaves(void)
{
	if(numWaves < 1) numWaves = 1;
	if(numWaves > MAX_WAVES) numWaves = MAX_WAVES;
	if(numWavesBuilt == numWaves &&
	   builtMinLen == minWaveLength && builtMaxLen == maxWaveLength &&
	   builtRatio == amplitudeRatio && builtDir == windDirectionMean &&
	   builtVar == windDirectionVariance && builtSpeed == speedScaleFactor)
		return;
	numWavesBuilt = numWaves;
	builtMinLen = minWaveLength; builtMaxLen = maxWaveLength;
	builtRatio = amplitudeRatio; builtDir = windDirectionMean;
	builtVar = windDirectionVariance; builtSpeed = speedScaleFactor;

	const float g = 9.81f;
	const float twoPi = 6.283185307f;
	const float toRad = 3.14159265f/180.0f;
	float lo = minWaveLength > 0.05f ? minWaveLength : 0.05f;
	float hi = maxWaveLength > lo ? maxWaveLength : lo*2.0f;
	for(i32 i = 0; i < numWaves; i++) {
		float t = numWaves > 1 ? (float)i/(float)(numWaves-1) : 1.0f;
		float len = lo*powf(hi/lo, t);
		// a deterministic zig-zag around the mean wind direction; the longest waves
		// run closest to the wind, the short chop is spread widest
		float s = ((i&1) ? -1.0f : 1.0f)*(1.0f - 0.75f*t);
		float ang = (windDirectionMean + windDirectionVariance*s)*toRad;
		Wave *w = &waveSet[i];
		w->dx = cosf(ang);
		w->dz = sinf(ang);
		w->k = twoPi/len;
		w->amplitude = len*amplitudeRatio;
		w->omega = sqrtf(g*w->k)*speedScaleFactor;
		w->phase = (float)i*1.7f;
	}
}

void
Ocean::GetHeightAndNormal(float x, float z, float taper, float *h, Vector *n) const
{
	float y = seaLevel;
	float dx = 0.0f, dz = 0.0f;
	if(waves && taper > 0.0f) {
		for(i32 i = 0; i < numWavesBuilt; i++) {
			const Wave *w = &waveSet[i];
			float a = w->amplitude*taper;
			float p = (x*w->dx + z*w->dz)*w->k - w->omega*time + w->phase;
			y += a*sinf(p);
			float d = a*w->k*cosf(p);
			dx += d*w->dx;
			dz += d*w->dz;
		}
	}
	*h = y;
	if(n) {
		Vector v(-dx, 1.0f, -dz);
		float len = Norm(v);
		*n = len > 0.0f ? v/len : Vector(0.0f, 1.0f, 0.0f);
	}
}

// retail: pure3d::Ocean::GetHeight(x, z)
float
Ocean::GetHeight(float x, float z)
{
	float h;
	BuildWaves();
	GetHeightAndNormal(x, z, 1.0f, &h, nil);
	return h;
}


// ---------------------------------------------------------------- the grid

// (N+1)^2 vertices of the wavy inner grid plus the 8 corners of the flat outer skirt.
// The skirt is four trapezoids from the inner square's edge out to farExtent; the waves
// are tapered to zero over the outermost fifth of the inner grid, so the two meet
// exactly. Retail keeps a 41 x 32 patch grid instead (Ocean state +0x188/+0x18c,
// Ocean::SetLevelOfDetail 0x689900) --- see re/notes/ocean.md "Deviations".
void
Ocean::BuildIndices(void)
{
	i32 N = gridCells;
	i32 nvert = (N+1)*(N+1) + 8;
	i32 nidx = N*N*6 + 4*6;
	if(primBuffer && builtCells == N)
		return;
	Release(primBuffer);
	builtCells = N;
	primBuffer = device->NewPrimBuffer(PDDI_PRIM_TRIANGLES,
		PDDI_V_POSITION|PDDI_V_NORMAL|PDDI_V_COLOUR|PDDI_V_UVCOUNT1, nvert, nidx);
	u16 *idx = new u16[nidx];
	i32 n = 0;
	for(i32 j = 0; j < N; j++)
		for(i32 i = 0; i < N; i++) {
			u16 a = (u16)(j*(N+1) + i);
			u16 b = (u16)(a + 1);
			u16 c = (u16)(a + N+1);
			u16 d = (u16)(c + 1);
			idx[n++] = a; idx[n++] = c; idx[n++] = b;
			idx[n++] = b; idx[n++] = c; idx[n++] = d;
		}
	// the skirt: inner corners at base+0..3, outer corners at base+4..7, both in the
	// order (-,-), (+,-), (+,+), (-,+)
	u16 base = (u16)((N+1)*(N+1));
	for(i32 s = 0; s < 4; s++) {
		u16 i0 = (u16)(base + s);
		u16 i1 = (u16)(base + (s+1)%4);
		u16 o0 = (u16)(base + 4 + s);
		u16 o1 = (u16)(base + 4 + (s+1)%4);
		idx[n++] = i0; idx[n++] = o0; idx[n++] = o1;
		idx[n++] = i0; idx[n++] = o1; idx[n++] = i1;
	}
	primBuffer->SetIndices(idx);
	delete[] idx;
}

void
Ocean::Display(void)
{
	if(shader == nil && baseShader == nil)
		return;
	if(gridCells < 4) gridCells = 4;
	if(gridCells > 250) gridCells = 250;	// (251+1)^2 + 8 still fits a u16 index
	BuildWaves();
	BuildIndices();

	i32 N = gridCells;
	float c = cellSize;
	float half = N*c*0.5f;
	// snap the grid to the cell lattice so the mesh does not crawl under the waves
	float ox = floorf(cameraPosition.x/c)*c;
	float oz = floorf(cameraPosition.z/c)*c;
	float ext = farExtent > half*1.5f ? farExtent : half*1.5f;

	Vector amb = lightAmbient;
	i32 nl = numLights < MAX_LIGHTS ? numLights : MAX_LIGHTS;
	if(!lighting) {
		amb = Vector(0.5f, 0.5f, 0.5f);
		nl = 0;
	}
	// waterColour * the sky the reflection pass would have sampled * the template's
	// reflection colour scale
	float wr = waterColour.R()/255.0f * reflectionColour.x * reflectionColourScale[0];
	float wg = waterColour.G()/255.0f * reflectionColour.y * reflectionColourScale[1];
	float wb = waterColour.B()/255.0f * reflectionColour.z * reflectionColourScale[2];
	float ts = detailTextureScale;
	// the detail texture tiles every 1/ts metres and has no mip maps, so it has to be
	// faded out before it turns into noise; past the band the water is the flat
	// reflection colour, which the distance fog takes over from anyway
	float fadeSpan = detailFadeEnd - detailFadeStart;
	if(fadeSpan < 1.0f) fadeSpan = 1.0f;

	pddiPrimBufferStream *stream = primBuffer->Lock();
	float halfCells = (float)N*0.5f;
	for(i32 j = 0; j <= N; j++)
		for(i32 i = 0; i <= N; i++) {
			float x = ox + ((float)i - halfCells)*c;
			float z = oz + ((float)j - halfCells)*c;
			// 1 in the middle, 0 at the border ring
			float rx = fabsf((float)i - halfCells)/halfCells;
			float rz = fabsf((float)j - halfCells)/halfCells;
			float r = rx > rz ? rx : rz;
			float taper = (1.0f - r)*5.0f;
			if(taper > 1.0f) taper = 1.0f;
			float y;
			Vector nrm;
			GetHeightAndNormal(x, z, taper, &y, &nrm);
			// both ocean shaders are unlit, so the GL vertex shader's own lighting
			// never runs: do its job here (ambient + sum of N.L, doubled, as in
			// gl/shaders/shader.vert)
			Vector lit = amb;
			for(i32 k = 0; k < nl; k++) {
				const Vector &d = lightDirection[k];
				float l = -(nrm.x*d.x + nrm.y*d.y + nrm.z*d.z);
				if(l > 0.0f) lit = lit + lightColour[k]*l;
			}
			float dx = x - cameraPosition.x, dz = z - cameraPosition.z;
			float dist = sqrtf(dx*dx + dz*dz);
			float fade = (detailFadeEnd - dist)/fadeSpan;
			if(fade > 1.0f) fade = 1.0f;
			if(fade < 0.0f) fade = 0.0f;
			float op = detailOpacity*fade;
			stream->Normal(nrm.x, nrm.y, nrm.z);
			stream->Colour(VertexColour(wr*lit.x*2.0f, wg*lit.y*2.0f, wb*lit.z*2.0f, op));
			stream->TexCoord2(x*ts, z*ts);
			stream->Position(x, y, z);
		}
	// the flat skirt out to the far plane; no waves and no detail texture out there
	Vector litFlat = amb;
	for(i32 k = 0; k < nl; k++) {
		float l = -lightDirection[k].y;
		if(l > 0.0f) litFlat = litFlat + lightColour[k]*l;
	}
	pddiColour flat = VertexColour(wr*litFlat.x*2.0f, wg*litFlat.y*2.0f,
	                               wb*litFlat.z*2.0f, 0.0f);
	static const float sx[4] = { -1.0f,  1.0f, 1.0f, -1.0f };
	static const float sz[4] = { -1.0f, -1.0f, 1.0f,  1.0f };
	for(i32 s = 0; s < 8; s++) {
		float d = s < 4 ? half : ext;
		float x = ox + sx[s&3]*d;
		float z = oz + sz[s&3]*d;
		stream->Normal(0.0f, 1.0f, 0.0f);
		stream->Colour(flat);
		stream->TexCoord2(x*ts, z*ts);
		stream->Position(x, seaLevel, z);
	}
	primBuffer->Unlock(stream);

	// The display list has already pushed the node matrix (the identity for the ocean)
	// and the viewer's x flip; the grid is built in those coordinates, so nothing else
	// goes on the matrix stack.
	//
	// Two passes, like retail's ocean_pass1 / ocean_pass2 (re/notes/ocean.md §5):
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
