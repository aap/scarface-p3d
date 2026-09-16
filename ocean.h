#pragma once

#include "entity.h"
#include "gmath.h"
#include "pddi.h"

namespace pure3d
{

using namespace core;
using namespace math;

class Shader;
class Texture;

// retail: pure3d::WaveModel, the object at Ocean+0x08 (0x128 bytes, ctor 0x6a8120) plus
// the sixteen 0x18-byte wave-train records at the head of OceanParams (Ocean+0x0c).
// SetParameters is 0x6a8370 -> the generator 0x6a8180, Initialize 0x689ad0 -> 0x6a83b0,
// Update 0x6a8420 and the per-frame pack for the surface 0x6a0a70.  re/notes/ocean.md §4
class WaveModel
{
public:
	enum { NUM_TRAINS = 16 };

	// the six OceanTuningTemplate floats, WaveModel +0x00..+0x14
	float minWaveLength;		// 0.1
	float maxWaveLength;		// 12.0
	float amplitudeRatio;		// 0.013 --- amplitude = waveLength*ratio
	float windDirectionMean;	// 90.0 degrees
	float windDirectionVariance;	// 30.0 degrees
	float speedScaleFactor;		// 1.2
	float trainLifeTime;		// +0x18, 15.0 s
	float trainFadeTime;		// +0x1c, 2.0 s

	// the generated train parameters, WaveModel +0x20/+0x60/+0xa0/+0xe0
	float amplitude[NUM_TRAINS];
	float waveNumber[NUM_TRAINS];	// k = 2pi/waveLength
	float omega[NUM_TRAINS];
	float direction[NUM_TRAINS];	// degrees

	// the two 16-bit multiply-with-carry streams at +0x120/+0x124. Retail's seeds are
	// in the ctor we did not decode; any pair gives the same kind of spread.      [?]
	u32 rngA, rngB;

	// one live train, OceanParams +0x00 + i*0x18
	struct Train {
		float faded;		// +0x00, the amplitude after the lifetime fade
		float amplitude;	// +0x04
		float direction;	// +0x08, degrees
		float omega;		// +0x0c
		float waveNumber;	// +0x10
		float age;		// +0x14, seconds
	};
	Train trains[NUM_TRAINS];
	float elapsed;			// OceanParams +0x180, seconds

	// what the surface is actually evaluated with, packed once a frame (retail packs
	// the same thing into vertex shader constants c10..c21)
	struct Packed { float amplitude, kx, kz, phase, waveLength; };
	Packed packed[NUM_TRAINS];

	WaveModel(void);
	float Rand(void);		// the PRNG inside 0x6a8180, in (-1, 1)
	void SetParameters(void);	// retail: 0x6a8370 -> 0x6a8180
	void Initialize(void);		// retail: 0x689ad0 -> 0x6a83b0
	void Update(float dt);		// retail: 0x6a8420
	void Pack(i32 numTrains);	// retail: 0x6a0a70
	float MaxHeight(void) const;	// retail: 0x6a8000
};

// retail: the pure3d ocean object that renderer::OceanPrimitive holds at +0x38 and whose
// Display is jumped to from OceanPrimitive::Display (0x4707a0 -> 0x689aa0). Its state
// object's ctor is 0x006a8040, the parameter setters are 0x689770..0x6898c0 and the
// d3d effects it draws with are "ocean_pass1", "ocean_pass1_spheremap" and "ocean_pass2".
// re/notes/ocean.md has the reversed field map and where every number below comes from.
//
// This is the stand-in for it: one camera-centred grid at sea level drawn through a pddi
// prim buffer in two passes --- a flat "reflection" pass and the `ocean_text` detail
// texture blended over it at DetailOpacity --- with an optional sum-of-sines height
// field (`waves`). No reflection render target, no foam, no specular.
class Ocean : public Entity
{
public:
	CLASSNAME(Ocean)

	// --- OceanTemplateDefault + OceanTuningTemplateDefault + the z04 OceanObject,
	// out of scriptc/templates/ocean.cso and scriptc/missions/z04/objects_static.dso
	// (re/notes/ocean.md §3). Every default here is the game's own value.       [V]
	Shader *shader;			// `ocean_text` --- DetailTextureName water_01.BMP
	Shader *baseShader;		// not retail: the untextured first pass
	Texture *reflectionTexture;	// ReflectionTextureName "skyTexture", the render target
	Texture *detailTexture;		// DetailTextureName "water_01.BMP"
	Texture *foamTexture;		// FoamTextureName "Water_Ocean_Foam.tga"
	float reflectionColourScale[4];	// 0.25, 0.23, 0.25, 0.98
	float detailOpacity;		// 0.225
	float detailTextureScale;	// 0.2 --- one tile of water_01 per 5 m
	float foamMinHeight;		// 0.15
	float foamMaxHeight;		// 1.0
	float foamMaxOpacity;		// 1.0
	pddiColour waterColour;		// 255, 255, 255, 255
	float seaLevel;			// 0.0 --- the ocean plane is world y = 0

	// --- OceanTuningTemplateDefault, the wave train parameters (WaveModel above) ---
	WaveModel waveModel;
	// how many of the sixteen trains displace the surface. Retail hands FOUR to the
	// vertex shader (0x6a0a70) and clamps GetHeight to four as well, and gets the ripples
	// you actually see out of an animated EMBM bump map instead. We have no bump map, so
	// all sixteen are evaluated by default --- with four the water is dead flat, because
	// the generator puts the SHORTEST waves (10..20 cm) in the first slots.
	i32 numSurfaceWaves;

	// --- not retail: what stands in for the reflection pass and how the grid is built
	// (re/notes/ocean.md "Deviations") ---
	// retail multiplies the sky render target by reflectionColourScale; we have no such
	// target, so this constant stands in for it. It is PRE-DIVIDED by the template's
	// 0.25 so that reflectionColourScale still reads through as the game wrote it.
	Vector reflectionColour;
	// retail: the grid is a projected grid in camera space and Ocean::SetLevelOfDetail
	// (0x689900) picks one of three static meshes, 50 / 110 / 170 quads across; the
	// default is the 170 one. We rebuild ours on the CPU every frame, so the middle one
	// is the default --- the wave evaluation, not the triangle count, is the cost.
	bool projectedGrid;
	i32 levelOfDetail;		// 0, 1, 2 -> 50, 110, 170
	// the old square world grid, kept as the fallback (and for a camera at or below the
	// water, where a projected grid has nothing to project onto)
	i32 gridCells;			// cells per side of the wavy inner grid
	float cellSize;			// metres
	float farExtent;		// the outermost ring / the flat skirt reaches this far
	float detailFadeStart, detailFadeEnd;	// the detail pass fades out over this band
	float detailGrazing;		// ...and with the sine of the view elevation times this
	float waveFadeEnd;		// no displacement past this (precision and speed)
	bool waves;			// false: a dead flat plane
	bool detailPass;		// false: the flat reflection colour only
	bool lighting;			// modulate the water colour with the game lights

	// the camera to centre on, in the ocean's own (native) coordinates; the renderer
	// writes both every frame, retail reads the pure3d View instead
	Vector cameraPosition;
	Vector cameraForward;
	float time;			// seconds, advances the wave phases

	// The lights the renderer picked for this frame, native, as 0..1 floats
	// (renderer::LightManager). Both ocean shaders are UNLIT pddi shaders, so the pddi
	// light slots never reach them and the day/night response has to go into the vertex
	// colour by hand --- with the same sum the GL vertex shader would have done.
	enum { MAX_LIGHTS = 4 };
	Vector lightAmbient;
	Vector lightColour[MAX_LIGHTS];
	Vector lightDirection[MAX_LIGHTS];	// the direction the light travels in
	i32 numLights;

	Ocean(void);
	~Ocean(void);

	void SetShader(Shader *sh);
	void SetBaseShader(Shader *sh);
	void SetTextures(Texture *reflection, Texture *detail, Texture *foam);

	// retail: pure3d::Ocean::GetSeaLevel 0x00689770 (it returns the float at +0x184,
	// which nothing in the image ever writes: the sea is at y = 0) and GetHeight(x, z),
	// which the leak calls all over the boat and swimming code
	float GetSeaLevel(void) const { return seaLevel; }
	float GetHeight(float x, float z);
	// `cell` is the local grid spacing: a wave shorter than a couple of cells cannot
	// be represented and is faded out instead of aliasing (retail hands the vertex
	// shader a 1/quad-size weight per vertex for the same job)
	void GetHeightAndNormal(float x, float z, float taper, float cell, float *h, Vector *n) const;
	// retail: pure3d::Ocean::GetMaxHeight 0x689750 -> 0x6a8000
	float GetMaxHeight(void) const { return seaLevel + waveModel.MaxHeight(); }

	// retail: pure3d::Ocean::Update 0x689c20 --- it advances OceanParams+0x180 once and
	// then calls WaveModel::Update TWICE with the same dt (0x689de1 and 0x689df2), so
	// the 15 s train lifetime really runs at 2x. Reproduced, quirk and all.
	void Tick(float dt);
	// retail: OceanPrimitive::CalcBounds 0x4707c0 --- the sphere follows the camera and
	// is 100 km across, so the ocean is never frustum culled
	Sphere GetBounds(void) const { return Sphere(cameraPosition, 100000.0f); }

	void Display(void);

	// the grid the last Display built, for the GUI
	i32 numVertices, numTriangles;

private:
	pddiPrimBuffer *primBuffer;
	i32 builtVertices, builtIndices;
	void NewPrimBuffer(i32 nvert, i32 nidx, const u16 *idx);
	void BuildWorldGrid(void);
	void BuildProjectedGrid(void);
	// shared by both, out of the per-frame shading state below
	void EmitVertex(pddiPrimBufferStream *stream, float x, float z, float taper, float cell);
	// the per-frame shading state Display sets up before it walks the grid
	Vector sBase;		// waterColour * reflectionColour * reflectionColourScale
	Vector sAmbient;
	i32 sNumLights;
	float sTexScale, sFadeSpan;
};

}
