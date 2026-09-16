// Inventory / map explorer for p3dview: browse loaded files and objects,
// pick map objects with ctrl+click, inspect and jump to them.

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include <map>

#include "imgui.h"
#include "glad/glad.h"

#include "p3dview.h"
#include "streaming.h"
#include "camera.h"
#include "../renderer/display_list.h"

using namespace core;
using namespace pure3d;

extern CCamera camera;
extern float windowWidth, windowHeight;
namespace renderer { extern bool displistvisible[NUM_DISPLAY_LISTS]; extern int displistsize[NUM_DISPLAY_LISTS]; }

// the viewer renders the world with x flipped; camera coordinates are in that
// flipped space, object data is in native (file) coordinates.
static Vector ToCam(const Vector &v) { return Vector(-v.x, v.y, v.z); }
static Vector ToNative(const Vector &v) { return Vector(-v.x, v.y, v.z); }

struct Selection {
	IRefCount *obj;		// selected object (AddRef'd)
	Sphere sphere;		// native coords, for jump/highlight
	Matrix matrix;		// native coords box transform
	Box3D box;
	bool hasBox;
	int location;		// instance placement index or -1
};
static Selection sel = { nil, Sphere(), Matrix(), Box3D(), false, -1 };
static char filter[64];

static void
Select(IRefCount *obj, const Sphere *sphere = nil, const Box3D *box = nil, const Matrix *m = nil, int location = -1)
{
	if(obj) obj->AddRef();
	if(sel.obj) sel.obj->Release();
	sel.obj = obj;
	sel.location = location;
	sel.hasBox = box != nil;
	if(sphere) sel.sphere = *sphere;
	if(box) sel.box = *box;
	if(m) sel.matrix = *m; else sel.matrix.Identity();
}

static void
JumpTo(const Sphere &sph)
{
	Vector target = ToCam(sph.centre);
	float dist = sph.radius > 1.0f ? sph.radius*2.5f : 5.0f;
	// keep the current heading but look down at the object from above
	Vector dir = camera.m_position - camera.m_target;
	dir.y = 0.0f;
	if(NormSq(dir) < 0.001f) dir = Vector(0.0f, 0.0f, 1.0f);
	dir = Normalized(dir)*0.8f + Vector(0.0f, 0.6f, 0.0f);
	camera.m_target = target;
	camera.m_position = target + dir*dist;
}

static bool
MatchFilter(const char *name)
{
	if(filter[0] == '\0') return true;
	// case-insensitive substring
	std::string a(name), b(filter);
	for(auto &c : a) c = tolower(c);
	for(auto &c : b) c = tolower(c);
	return a.find(b) != std::string::npos;
}

// ---------------------------------------------------------------- selection panels

static void
Label(const char *k, const char *fmt, ...)
{
	char buf[256];
	va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
	ImGui::TextDisabled("%s", k); ImGui::SameLine(140); ImGui::TextUnformatted(buf);
}

static void
SelectableObject(const char *label, IRefCount *obj, const Sphere *sph = nil, const Box3D *box = nil, const Matrix *m = nil)
{
	char id[300]; snprintf(id, sizeof(id), "%s##%p", label, (void*)obj);
	if(ImGui::Selectable(id, obj == sel.obj))
		Select(obj, sph, box, m);
}

static void
ShowShader(Shader *sh)
{
	static const char *blend[] = { "none", "alpha", "add", "sub", "modulate", "modulate2", "addmodalpha", "?7" };
	Label("type", "%d", sh->GetType());
	Label("blend", "%s", sh->GetBlendMode() < 8 ? blend[sh->GetBlendMode()] : "?");
	Label("lit", "%s", sh->GetIsLit() ? "yes" : "no");
	Label("alpha test", "%s", sh->GetAlphaTest() ? "yes" : "no");
	Label("ALUM", "%s", sh->GetALUM() ? "yes" : "no");
}

static void
ShowContainer(DrawableContainer *dc, const Matrix *m)
{
	Label("elements", "%d", dc->GetNumElements());
	Label("sphere", "%.1f %.1f %.1f r %.1f", dc->sphere.centre.x, dc->sphere.centre.y, dc->sphere.centre.z, dc->sphere.radius);
	Label("lit/ALUM/alpha", "%d/%d/%d", dc->isLit, dc->isALUM, dc->alphaBlend);
	if(ImGui::TreeNodeEx("primitives", ImGuiTreeNodeFlags_DefaultOpen)) {
		for(i32 i = 0; i < dc->GetNumElements(); i++) {
			DrawablePrimitive *prim = dc->GetElement(i)->prim;
			if(prim == nil) { ImGui::Text("%d: (not loaded)", i); continue; }
			Shader *sh = prim->GetShader();
			char label[256];
			snprintf(label, sizeof(label), "%d: layer %d  %s", i, prim->GetLayer(), sh ? sh->GetName() : "-");
			if(sh) SelectableObject(label, sh, &prim->sphere, &prim->box, m);
			else ImGui::TextUnformatted(label);
		}
		ImGui::TreePop();
	}
}

static void
ShowComposite(CompositeDrawable *comp)
{
	CompositeDrawable::ActivePrimitiveList *pl = comp->GetPrimitiveList();
	Label("primitives", "%d (%d used)", pl->GetNumPrimitives(), pl->GetNumUsedPrimitives());
	Label("sphere", "%.1f %.1f %.1f r %.1f", comp->sphere.centre.x, comp->sphere.centre.y, comp->sphere.centre.z, comp->sphere.radius);
	for(u32 i = 0; i < pl->GetNumPrimitives(); i++) {
		CompositeDrawable::ActivePrimitive *ap = pl->GetPrimitive(i);
		DrawableContainer *dc = ap->GetDrawable();
		char label[256];
		snprintf(label, sizeof(label), "%d (id %d%s): %s", i, ap->id, ap->isVisible ? "" : ", hidden", dc ? dc->GetName() : "(none)");
		if(dc) SelectableObject(label, dc, &dc->sphere, &dc->box);
		else ImGui::TextUnformatted(label);
	}
}

static void
ShowRenderable(renderer::Renderable *r)
{
	bool vis = r->isVisible;
	if(ImGui::Checkbox("visible", &vis)) r->SetVisible(vis);
	ImGui::SameLine();
	if(ImGui::Button("jump") && r->elements.Size() > 0 && r->elements[0].prim.GetDrawable())
		JumpTo(r->elements[0].prim.GetDrawable()->sphere);
	Label("typeMask", "0x%x", r->typeMask);
	Label("scene", "%d", r->sceneId);
	// renderer::Renderable flags80/flags81, see re/notes/rendercore.md §6.1
	Label("flags", "%s%s%s%s%s", r->doDistanceTest ? "distTest " : "", r->doFade ? "fade " : "",
		r->hasHandle ? "handle " : "", r->isInsideRoom ? "inRoom " : "",
		r->shareLastElementFarDist ? "shareFarDist" : "");
	if(r->fade > 0.0f || r->fade2 > 0.0f)
		Label("fade", "%.2f / %.2f  target %.2f  time %.0f", r->fade, r->fade2, r->fadeTarget, r->fadeTime);
	for(u32 i = 0; i < r->elements.Size(); i++) {
		renderer::DisplayListElement &e = r->elements[i];
		DrawableHierarchy *d = e.prim.GetDrawable();
		ImGui::Text("element %d: draw %.0f..%.0f fade %.0f%s", i, e.drawDistMin, e.drawDistMax, e.drawDistFade, e.isFading ? "  (fading)" : "");
		if(d) SelectableObject(d->GetName(), d, &d->sphere, &d->box);
	}
}

static void
ShowWorldGeo(renderer::WorldGeoRenderable *wg)
{
	ShowRenderable(wg);
	Label("flags", "%s%s%s%s%s", wg->isDetails ? "details " : "", wg->isSkyline ? "skyline " : "",
		wg->drawFirst ? "drawFirst " : "", wg->isLowLOD ? "lowLOD " : "", wg->useOtherPosition ? "useOtherPosition" : "");
	if(wg->useOtherPosition)
		Label("otherPosition", "%.1f %.1f", wg->otherPosition.x, wg->otherPosition.z);
	Label("sub-primitives", "%d", wg->numPrimitives);
}

static void
ShowZonePkg(renderer::ZonePkgRenderable *zp)
{
	ShowRenderable(zp);
	for(u32 i = 0; i < zp->worldGeos.Size(); i++) {
		renderer::WorldGeoRenderable *wg = zp->worldGeos[i];
		if(wg) SelectableObject(wg->GetName(), wg, wg->elements.Size() ? &wg->elements[0].prim.GetDrawable()->sphere : nil);
	}
}

static void
ShowInstance(renderer::InstanceRenderable *ir)
{
	bool vis = ir->isVisible;
	if(ImGui::Checkbox("visible", &vis)) ir->SetVisible(vis);
	Label("model", "%s", ir->modelName.c_str());
	if(!ir->attractName.empty()) Label("attract", "%s", ir->attractName.c_str());
	Label("cull", "%.0f..%.0f fade %.0f", ir->cullMin, ir->cullMax > 0.0f ? ir->cullMax : renderer::InstanceRenderable::defaultCullMax, ir->fadeDist);
	if(ir->shape) SelectableObject(ir->shape->GetName(), ir->shape, &ir->shape->sphere, &ir->shape->box);
	else ImGui::TextDisabled("no InstanceShape");
	if(ir->lodShape) SelectableObject(ir->lodShape->GetName(), ir->lodShape, &ir->lodShape->sphere, &ir->lodShape->box);
	Label("placements", "%zu", ir->locations.size());
	if(ImGui::BeginTable("locs", 5, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg, ImVec2(0, 260))) {
		ImGui::TableSetupColumn("#"); ImGui::TableSetupColumn("position"); ImGui::TableSetupColumn("rot");
		ImGui::TableSetupColumn("scale"); ImGui::TableSetupColumn("tint");
		ImGui::TableHeadersRow();
		ImGuiListClipper clip; clip.Begin(ir->locations.size());
		while(clip.Step())
		for(int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
			renderer::InstanceLocation &loc = ir->locations[i];
			ImGui::TableNextRow(); ImGui::TableNextColumn();
			char id[32]; snprintf(id, sizeof(id), "%d", i);
			if(ImGui::Selectable(id, sel.location == i, ImGuiSelectableFlags_SpanAllColumns)) {
				Sphere sph = loc.sphere;
				if(sph.radius <= 0.0f) sph.radius = 3.0f;
				Select(ir, &sph, ir->shape ? &ir->shape->box : nil, &loc.matrix, i);
				JumpTo(sph);
			}
			ImGui::TableNextColumn(); ImGui::Text("%.1f %.1f %.1f", loc.position.x, loc.position.y, loc.position.z);
			ImGui::TableNextColumn(); ImGui::Text("%.0f %.0f %.0f", loc.rotation.x, loc.rotation.y, loc.rotation.z);
			ImGui::TableNextColumn(); ImGui::Text("%.2f %.2f %.2f", loc.scale.x, loc.scale.y, loc.scale.z);
			ImGui::TableNextColumn(); ImGui::Text("%d", loc.tint);
		}
		ImGui::EndTable();
	}
}

static void
SelectionTab(void)
{
	if(sel.obj == nil) { ImGui::TextDisabled("nothing selected (ctrl+click in the view, or pick from the tree)"); return; }
	Entity *e = dynamic_cast<Entity*>(sel.obj);
	ImGui::Text("%s", sel.obj->GetClassName());
	if(e) { Label("name", "%s", e->GetName()); Label("uid", "%08x", e->GetUID()); }
	Label("refs", "%d", sel.obj->GetRef());
	if(sel.sphere.radius > 0.0f || NormSq(sel.sphere.centre) > 0.0f) {
		if(ImGui::Button("jump to")) JumpTo(sel.sphere);
		ImGui::SameLine(); ImGui::TextDisabled("%.1f %.1f %.1f r %.1f", sel.sphere.centre.x, sel.sphere.centre.y, sel.sphere.centre.z, sel.sphere.radius);
	}
	ImGui::Separator();
	if(auto *x = dynamic_cast<renderer::InstanceRenderable*>(sel.obj)) ShowInstance(x);
	else if(auto *x = dynamic_cast<renderer::WorldGeoRenderable*>(sel.obj)) { ShowWorldGeo(x); }
	else if(auto *x = dynamic_cast<renderer::ZonePkgRenderable*>(sel.obj)) ShowZonePkg(x);
	else if(auto *x = dynamic_cast<renderer::Renderable*>(sel.obj)) ShowRenderable(x);
	else if(auto *x = dynamic_cast<CompositeDrawable*>(sel.obj)) ShowComposite(x);
	else if(auto *x = dynamic_cast<DrawableContainer*>(sel.obj)) ShowContainer(x, &sel.matrix);
	else if(auto *x = dynamic_cast<Shader*>(sel.obj)) ShowShader(x);
}

// ---------------------------------------------------------------- left side: trees

struct ClassGroup { std::vector<std::pair<std::string, IRefCount*>> objs; };

static void
FilesTab(void)
{
	for(u32 f = 0; f < loadedFiles.size(); f++) {
		LoadedFile &lf = loadedFiles[f];
		if(!ImGui::TreeNode(lf.name.c_str())) continue;
		std::map<std::string, ClassGroup> groups;
		lf.inv->ForEach([&](u32 uid, IRefCount *obj) {
			Entity *e = dynamic_cast<Entity*>(obj);
			groups[obj->GetClassName()].objs.push_back(std::make_pair(e ? e->GetName() : "?", obj));
		});
		for(auto &g : groups) {
			char label[128]; snprintf(label, sizeof(label), "%s (%zu)", g.first.c_str(), g.second.objs.size());
			if(!ImGui::TreeNode(label)) continue;
			for(auto &o : g.second.objs) {
				if(!MatchFilter(o.first.c_str())) continue;
				const Sphere *sph = nil; const Box3D *box = nil;
				if(auto *d = dynamic_cast<DrawableHierarchy*>(o.second)) { sph = &d->sphere; box = &d->box; }
				else if(auto *r = dynamic_cast<renderer::Renderable*>(o.second)) {
					if(r->elements.Size() && r->elements[0].prim.GetDrawable()) sph = &r->elements[0].prim.GetDrawable()->sphere;
				}
				SelectableObject(o.first.c_str(), o.second, sph, box);
			}
			ImGui::TreePop();
		}
		ImGui::TreePop();
	}
}

static void
RenderablesTab(void)
{
	ImGuiListClipper clip;
	std::vector<renderer::Renderable*> shown;
	for(u32 i = 0; i < renderables.size(); i++)
		if(MatchFilter(renderables[i]->GetName()) || MatchFilter(renderables[i]->GetClassName()))
			shown.push_back(renderables[i]);
	ImGui::TextDisabled("%zu of %zu", shown.size(), renderables.size());
	clip.Begin(shown.size());
	while(clip.Step())
	for(int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
		renderer::Renderable *r = shown[i];
		ImGui::PushID(r);
		bool vis = r->isVisible;
		if(ImGui::Checkbox("", &vis)) r->SetVisible(vis);
		ImGui::SameLine();
		char label[256]; snprintf(label, sizeof(label), "%s  [%s]", r->GetName(), r->GetClassName());
		const Sphere *sph = (r->elements.Size() && r->elements[0].prim.GetDrawable()) ? &r->elements[0].prim.GetDrawable()->sphere : nil;
		if(ImGui::Selectable(label, r == sel.obj)) Select(r, sph);
		ImGui::PopID();
	}
}

// ---------------------------------------------------------------- view tab


static void
ViewTab(void)
{
	Vector p = camera.m_position, t = camera.m_target;
	ImGui::Text("camera (native x = -x)");
	if(ImGui::DragFloat3("position", &p.x, 1.0f)) camera.m_position = p;
	if(ImGui::DragFloat3("target", &t.x, 1.0f)) camera.m_target = t;
	ImGui::DragFloat("far", &camera.m_far, 10.0f, 100.0f, 20000.0f);
	ImGui::SliderFloat("instance cull", &renderer::InstanceRenderable::defaultCullMax, 50.0f, 1500.0f);
	ImGui::SeparatorText("render");
	ImGui::Checkbox("no textures", &pddiDebug.noTextures); ImGui::SameLine();
	ImGui::Checkbox("no lighting", &pddiDebug.noLighting);
	ImGui::Checkbox("no vertex colours", &pddiDebug.noVertexColours); ImGui::SameLine();
	ImGui::Checkbox("wireframe", &pddiDebug.wireframe);
	if(ImGui::TreeNode("display lists")) {
		if(ImGui::Button("all")) for(int i = 0; i < renderer::NUM_DISPLAY_LISTS; i++) renderer::displistvisible[i] = true;
		ImGui::SameLine();
		if(ImGui::Button("none")) for(int i = 0; i < renderer::NUM_DISPLAY_LISTS; i++) renderer::displistvisible[i] = false;
		for(int i = 0; i < renderer::NUM_DISPLAY_LISTS; i++) {
			if(renderer::displistsize[i] == 0) continue;
			char lbl[64]; snprintf(lbl, sizeof(lbl), "%2d  (%d)", i, renderer::displistsize[i]);
			ImGui::Checkbox(lbl, &renderer::displistvisible[i]);
		}
		ImGui::TreePop();
	}
	if(ImGui::TreeNode("shaders")) {
		for(u32 i = 0; i < 17; i++)
			ImGui::Checkbox(shaderRenderable[i].shader, &shaderRenderable[i].visible);
		ImGui::TreePop();
	}
	ImGui::Separator();
	StreamingGUI();
}

void
ExplorerGUI(void)
{
	// P3D_SELECT=<renderable name>: select (and jump to) an object at startup, for scripted checks
	static bool first = true;
	if(first) {
		first = false;
		// P3D_DEBUGRENDER=notex,nolight,novcol,wire
		if(const char *dr = getenv("P3D_DEBUGRENDER")) {
			pddiDebug.noTextures = strstr(dr, "notex") != nil;
			pddiDebug.noLighting = strstr(dr, "nolight") != nil;
			pddiDebug.noVertexColours = strstr(dr, "novcol") != nil;
			pddiDebug.wireframe = strstr(dr, "wire") != nil;
		}
		if(const char *want = getenv("P3D_SELECT"))
			for(u32 i = 0; i < renderables.size(); i++)
				if(strcmp(renderables[i]->GetName(), want) == 0 && renderables[i]->elements.Size() && renderables[i]->elements[0].prim.GetDrawable()) {
					Select(renderables[i], &renderables[i]->elements[0].prim.GetDrawable()->sphere);
					if(getenv("P3D_CAMPOS") == nil) JumpTo(sel.sphere);
					break;
				}
	}
	ImGui::SetNextWindowPos(ImVec2(windowWidth > 700.0f ? windowWidth - 620.0f : 20.0f, 20.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(600, 620), ImGuiCond_FirstUseEver);
	if(!ImGui::Begin("Explorer")) { ImGui::End(); return; }
	ImGui::InputText("filter", filter, sizeof(filter));
	float w = ImGui::GetContentRegionAvail().x;
	ImGui::BeginChild("left", ImVec2(w*0.45f, 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
	if(ImGui::BeginTabBar("left_tab")) {
		if(ImGui::BeginTabItem("Files")) { FilesTab(); ImGui::EndTabItem(); }
		if(ImGui::BeginTabItem("Renderables")) { RenderablesTab(); ImGui::EndTabItem(); }
		ImGui::EndTabBar();
	}
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::BeginChild("right", ImVec2(0, 0), ImGuiChildFlags_Borders);
	if(ImGui::BeginTabBar("right_tab")) {
		if(ImGui::BeginTabItem("Selection")) { SelectionTab(); ImGui::EndTabItem(); }
		if(ImGui::BeginTabItem("View")) { ViewTab(); ImGui::EndTabItem(); }
		ImGui::EndTabBar();
	}
	ImGui::EndChild();
	ImGui::End();
}

// ---------------------------------------------------------------- picking

static bool
RaySphere(const Vector &o, const Vector &d, const Sphere &s, float &t)
{
	Vector m = o - s.centre;
	float b = Dot(m, d), c = Dot(m, m) - s.radius*s.radius;
	if(c > 0.0f && b > 0.0f) return false;
	float disc = b*b - c;
	if(disc < 0.0f) return false;
	t = -b - sqrtf(disc);
	if(t < 0.0f) t = 0.0f;
	return true;
}

void
ExplorerPick(int mx, int my)
{
	// ray in camera space, then to native
	float ndcx = (2.0f*mx/windowWidth - 1.0f), ndcy = (1.0f - 2.0f*my/windowHeight);
	float tanY = tanf(camera.m_fov*0.5f*M_PI/180.0f);
	Vector fwd = Normalized(camera.m_target - camera.m_position);
	Vector right = Normalized(Cross(fwd, camera.m_up));
	Vector up = Cross(right, fwd);
	Vector dir = Normalized(fwd + right*(ndcx*tanY*camera.m_aspectRatio) + up*(ndcy*tanY));
	Vector o = ToNative(camera.m_position), d = ToNative(dir);

	float best = 1e30f; bool found = false;
	IRefCount *obj = nil; Sphere bsph; const Box3D *bbox = nil; const Matrix *bm = nil; int bloc = -1;
	for(u32 i = 0; i < renderables.size(); i++) {
		renderer::Renderable *r = renderables[i];
		if(!r->isVisible) continue;
		if(auto *ir = dynamic_cast<renderer::InstanceRenderable*>(r)) {
			if(ir->shape == nil) continue;
			for(u32 j = 0; j < ir->locations.size(); j++) {
				renderer::InstanceLocation &loc = ir->locations[j];
				float t;
				if(loc.sphere.radius > 0.0f && RaySphere(o, d, loc.sphere, t) && t < best) {
					best = t; found = true; obj = ir; bsph = loc.sphere; bbox = &ir->shape->box; bm = &loc.matrix; bloc = j;
				}
			}
			continue;
		}
		// world geometry: prefer the sub-drawables of the composite so picks are precise
		for(u32 e = 0; e < r->elements.Size(); e++) {
			DrawableHierarchy *dh = r->elements[e].prim.GetDrawable();
			if(dh == nil) continue;
			CompositeDrawable *comp = dynamic_cast<CompositeDrawable*>(dh);
			if(comp) {
				CompositeDrawable::ActivePrimitiveList *pl = comp->GetPrimitiveList();
				for(u32 p = 0; p < pl->GetNumPrimitives(); p++) {
					DrawableContainer *dc = pl->GetPrimitive(p)->GetDrawable();
					float t;
					if(dc && RaySphere(o, d, dc->sphere, t) && t < best) {
						best = t; found = true; obj = dc; bsph = dc->sphere; bbox = &dc->box; bm = nil; bloc = -1;
					}
				}
			} else {
				float t;
				if(RaySphere(o, d, dh->sphere, t) && t < best) {
					best = t; found = true; obj = r; bsph = dh->sphere; bbox = &dh->box; bm = nil; bloc = -1;
				}
			}
		}
	}
	if(found) Select(obj, &bsph, bbox, bm, bloc);
	else Select(nil);
}

// ---------------------------------------------------------------- highlight overlay

static GLuint lineProg, lineVbo, lineVao;

static void
InitLines(void)
{
	static const char *vs = "#version 120\nattribute vec3 in_pos; uniform mat4 u_mvp; void main(){ gl_Position = u_mvp*vec4(in_pos,1.0); }";
	static const char *fs = "#version 120\nuniform vec4 u_col; void main(){ gl_FragColor = u_col; }";
	GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(v, 1, &vs, nil); glCompileShader(v);
	glShaderSource(f, 1, &fs, nil); glCompileShader(f);
	lineProg = glCreateProgram();
	glBindAttribLocation(lineProg, 0, "in_pos");
	glAttachShader(lineProg, v); glAttachShader(lineProg, f); glLinkProgram(lineProg);
	GLint ok; glGetProgramiv(lineProg, GL_LINK_STATUS, &ok);
	if(!ok) { char log[1024]; glGetProgramInfoLog(lineProg, sizeof(log), nil, log); fprintf(stderr, "explorer line shader: %s\n", log); }
	glGetShaderiv(v, GL_COMPILE_STATUS, &ok); if(!ok) { char log[1024]; glGetShaderInfoLog(v, sizeof(log), nil, log); fprintf(stderr, "explorer vs: %s\n", log); }
	glGetShaderiv(f, GL_COMPILE_STATUS, &ok); if(!ok) { char log[1024]; glGetShaderInfoLog(f, sizeof(log), nil, log); fprintf(stderr, "explorer fs: %s\n", log); }
	glGenVertexArrays(1, &lineVao); glGenBuffers(1, &lineVbo);
}

void
ExplorerDrawOverlay(void)
{
	if(sel.obj == nil) return;
	if(lineProg == 0) InitLines();

	// corners of the box (or a cube around the sphere) in native coords
	Vector lo, hi;
	if(sel.hasBox) { lo = sel.box.low; hi = sel.box.high; }
	else { Vector r(sel.sphere.radius, sel.sphere.radius, sel.sphere.radius); lo = sel.sphere.centre - r; hi = sel.sphere.centre + r; }
	Vector c[8];
	for(int i = 0; i < 8; i++) {
		Vector p(i&1 ? hi.x : lo.x, i&2 ? hi.y : lo.y, i&4 ? hi.z : lo.z);
		c[i] = sel.hasBox && sel.location >= 0 ? Multiply(p, sel.matrix) : p;
	}
	static const int edges[12][2] = {{0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{3,7},{2,6}};
	float v[12*2*3]; int n = 0;
	for(int i = 0; i < 12; i++) for(int k = 0; k < 2; k++) {
		Vector p = ToCam(c[edges[i][k]]);
		v[n++] = p.x; v[n++] = p.y; v[n++] = p.z;
	}
	// gmath's Multiply is affine-only, so do the full 4x4 product here (row vectors: v*view*proj)
	Matrix mvp;
	for(int i = 0; i < 4; i++) for(int j = 0; j < 4; j++) {
		float a = 0.0f;
		for(int k = 0; k < 4; k++) a += camera.m_viewMat.e[i*4+k]*camera.m_projMat.e[k*4+j];
		mvp.e[i*4+j] = a;
	}
	if(getenv("P3D_DEBUGOVERLAY")) {
		static int cnt; if(cnt++ < 3) {
			float x = v[0], y = v[1], z = v[2];
			float cx = x*mvp.e[0]+y*mvp.e[4]+z*mvp.e[8]+mvp.e[12], cy = x*mvp.e[1]+y*mvp.e[5]+z*mvp.e[9]+mvp.e[13];
			float cw = x*mvp.e[3]+y*mvp.e[7]+z*mvp.e[11]+mvp.e[15];
			printf("overlay corner (%.1f %.1f %.1f) -> ndc %.2f %.2f  w %.1f  prog %u vao %u  cam %.1f %.1f %.1f\n", x, y, z, cx/cw, cy/cw, cw, lineProg, lineVao, camera.m_position.x, camera.m_position.y, camera.m_position.z);
		}
	}

	GLint prevProg, prevVao, prevBuf;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuf);
	glUseProgram(lineProg);
	glUniformMatrix4fv(glGetUniformLocation(lineProg, "u_mvp"), 1, GL_FALSE, mvp.e);
	glUniform4f(glGetUniformLocation(lineProg, "u_col"), 1.0f, 0.2f, 0.2f, 1.0f);
	glBindVertexArray(lineVao);
	glBindBuffer(GL_ARRAY_BUFFER, lineVbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nil);
	glDisable(GL_DEPTH_TEST);
	glDrawArrays(GL_LINES, 0, 24);
	glEnable(GL_DEPTH_TEST);
	glBindBuffer(GL_ARRAY_BUFFER, prevBuf);
	glBindVertexArray(prevVao);
	glUseProgram(prevProg);
	static bool reported;
	GLenum err = glGetError();
	if(err && !reported) { reported = true; fprintf(stderr, "explorer overlay GL error 0x%x\n", err); }
}
