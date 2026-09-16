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

	// --- OceanTuningTemplateDefault, the wave train parameters ---
	float minWaveLength;		// 0.1
	float maxWaveLength;		// 12.0
	float amplitudeRatio;		// 0.013 --- amplitude = waveLength*ratio, so the
					// biggest wave in the game is 16 cm high
	float windDirectionMean;	// 90.0 degrees
	float windDirectionVariance;	// 30.0 degrees
	float speedScaleFactor;		// 1.2
	i32 numWaves;			// 16 wave trains in retail (Ocean::Initialize 0x6a83b0)

	// --- not retail: what stands in for the reflection pass and how the grid is built
	// (re/notes/ocean.md "Deviations") ---
	// retail multiplies the sky render target by reflectionColourScale; we have no such
	// target, so this constant stands in for it. It is PRE-DIVIDED by the template's
	// 0.25 so that reflectionColourScale still reads through as the game wrote it.
	Vector reflectionColour;
	i32 gridCells;			// cells per side of the wavy inner grid
	float cellSize;			// metres
	float farExtent;		// the flat outer skirt reaches this far
	float detailFadeStart, detailFadeEnd;	// the detail pass fades out over this band
	bool waves;			// false: a dead flat plane
	bool detailPass;		// false: the flat reflection colour only
	bool lighting;			// modulate the water colour with the game lights

	// the camera to centre on, in the ocean's own (native) coordinates; the renderer
	// writes it every frame, retail reads the pure3d View instead
	Vector cameraPosition;
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
	void GetHeightAndNormal(float x, float z, float taper, float *h, Vector *n) const;

	void Tick(float dt) { time += dt; }
	// retail: OceanPrimitive::CalcBounds 0x4707c0 --- the sphere follows the camera and
	// is 100 km across, so the ocean is never frustum culled
	Sphere GetBounds(void) const { return Sphere(cameraPosition, 100000.0f); }

	void Display(void);

private:
	struct Wave {
		float dx, dz;		// unit direction
		float k;		// 2pi/wavelength
		float amplitude;
		float omega;		// angular frequency
		float phase;
	};
	enum { MAX_WAVES = 16 };
	Wave waveSet[MAX_WAVES];
	i32 numWavesBuilt;
	float builtMinLen, builtMaxLen, builtRatio, builtDir, builtVar, builtSpeed;
	void BuildWaves(void);

	pddiPrimBuffer *primBuffer;
	i32 builtCells;
	void BuildIndices(void);
};

}
