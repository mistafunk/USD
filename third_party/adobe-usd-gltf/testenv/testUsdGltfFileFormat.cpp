//
// Basic confidence tests of the glTF file format plugin as ported for the
// geospatial web prototype (fixtures: makeFixtures.py):
//   1. glb and gltf resolve to the plugin's file format,
//   2. a glb box imports with glTF's stage metadata and its exact mesh,
//   3. the same box as glTF JSON with an external buffer,
//   4. a node's translation, rotation and scale place the mesh (T * R * S),
//   5. a USD layer referencing the glb composes it under the referencing
//      prim and its transform, as the app's wrapper layers do.
// The plugin is found through its plugInfo.json, not linked.
//

#include "pxr/pxr.h"
#include "pxr/usd/sdf/fileFormat.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/usd/usdGeom/xformCache.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/rotation.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/stringUtils.h"

#include <cstdio>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

const std::vector<GfVec3f> boxPoints = {
    {-2, 0, -0.5}, {2, 0, -0.5}, {2, 0, 0.5}, {-2, 0, 0.5},
    {-2, 1, -0.5}, {2, 1, -0.5}, {2, 1, 0.5}, {-2, 1, 0.5}};
const std::vector<int> boxIndices = {
    0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
    1, 5, 6, 1, 6, 2, 2, 6, 7, 2, 7, 3, 3, 7, 4, 3, 4, 0};

bool ok = true;

void
_Check(bool good, const std::string& what)
{
    printf("  %s %s\n", good ? "ok  " : "FAIL", what.c_str());
    ok &= good;
}

std::vector<UsdGeomMesh>
_Meshes(const UsdStageRefPtr& stage)
{
    std::vector<UsdGeomMesh> meshes;
    for (const UsdPrim& prim : stage->Traverse()) {
        if (prim.IsA<UsdGeomMesh>()) {
            meshes.emplace_back(prim);
        }
    }
    return meshes;
}

// The box's points and triangles, exactly as written.
void
_CheckBoxMesh(const UsdGeomMesh& mesh)
{
    VtVec3fArray points;
    VtIntArray counts, indices;
    mesh.GetPointsAttr().Get(&points);
    mesh.GetFaceVertexCountsAttr().Get(&counts);
    mesh.GetFaceVertexIndicesAttr().Get(&indices);
    _Check(std::vector<GfVec3f>(points.begin(), points.end()) == boxPoints,
           TfStringPrintf("%zu points as written", points.size()));
    bool triangles = counts.size() == 12;
    for (const int n : counts) {
        triangles &= n == 3;
    }
    _Check(triangles, TfStringPrintf("%zu triangles", counts.size()));
    _Check(std::vector<int>(indices.begin(), indices.end()) == boxIndices,
           "triangle indices as written");
}

UsdStageRefPtr
_Open(const std::string& path)
{
    const UsdStageRefPtr stage = UsdStage::Open(path);
    _Check(bool(stage), "opens " + path);
    return stage;
}

void
_TestFileFormat()
{
    printf("file format\n");
    for (const char* extension : {"glb", "gltf"}) {
        const SdfFileFormatConstPtr format =
            SdfFileFormat::FindByExtension(extension);
        _Check(format && format->GetFormatId() == "gltf",
               TfStringPrintf(".%s: format '%s'", extension,
                              format ? format->GetFormatId().GetText() : ""));
    }
}

void
_TestBox(const std::string& path)
{
    printf("%s\n", path.c_str());
    const UsdStageRefPtr stage = _Open(path);
    if (!stage) {
        return;
    }
    _Check(UsdGeomGetStageUpAxis(stage) == UsdGeomTokens->y, "upAxis Y");
    _Check(UsdGeomGetStageMetersPerUnit(stage) == 1.0, "metersPerUnit 1");
    _Check(bool(stage->GetDefaultPrim()), "default prim");
    const std::vector<UsdGeomMesh> meshes = _Meshes(stage);
    _Check(meshes.size() == 1, TfStringPrintf("%zu mesh", meshes.size()));
    if (meshes.size() == 1) {
        _CheckBoxMesh(meshes[0]);
        UsdGeomXformCache cache;
        _Check(GfIsClose(cache.GetLocalToWorldTransform(meshes[0].GetPrim()),
                         GfMatrix4d(1.0), 1e-12),
               "no transform");
    }
}

// Mesh points to world space against an expected transform.
void
_CheckPlacement(const UsdStageRefPtr& stage, const GfMatrix4d& expected,
                const std::string& what)
{
    const std::vector<UsdGeomMesh> meshes = _Meshes(stage);
    _Check(meshes.size() == 1, TfStringPrintf("%zu mesh", meshes.size()));
    if (meshes.size() != 1) {
        return;
    }
    UsdGeomXformCache cache;
    const GfMatrix4d world =
        cache.GetLocalToWorldTransform(meshes[0].GetPrim());
    double worst = 0.0;
    for (const GfVec3f& p : boxPoints) {
        worst = std::max(worst, (world.Transform(GfVec3d(p)) -
                                 expected.Transform(GfVec3d(p))).GetLength());
    }
    _Check(worst < 1e-6, TfStringPrintf("%s (max %.2g)", what.c_str(), worst));
}

void
_TestNodeTransform()
{
    printf("nodes.glb\n");
    const UsdStageRefPtr stage = _Open("nodes.glb");
    if (!stage) {
        return;
    }
    // glTF: world = T * R * S (column vectors); USD's row vectors: S * R * T.
    GfMatrix4d s, r, t;
    s.SetScale(GfVec3d(2, 1, 1));
    r.SetRotate(GfRotation(GfVec3d(0, 1, 0), 90.0));
    t.SetTranslate(GfVec3d(10, 0, -5));
    _CheckPlacement(stage, s * r * t,
                    "node translation, rotation about Y, scale (2, 1, 1)");
    // The corner (2, 1, 0.5): scaled to (4, 1, 0.5), turned to (0.5, 1, -4),
    // moved to (10.5, 1, -9).
    UsdGeomXformCache cache;
    const GfVec3d corner = cache.GetLocalToWorldTransform(
        _Meshes(stage)[0].GetPrim()).Transform(GfVec3d(2, 1, 0.5));
    _Check((corner - GfVec3d(10.5, 1, -9)).GetLength() < 1e-6,
           TfStringPrintf("corner at (%g, %g, %g)", corner[0], corner[1],
                          corner[2]));
}

void
_TestReference()
{
    printf("wrapper.usda (references box.glb)\n");
    const UsdStageRefPtr stage = _Open("wrapper.usda");
    if (!stage) {
        return;
    }
    const std::vector<UsdGeomMesh> meshes = _Meshes(stage);
    _Check(meshes.size() == 1 &&
           meshes[0].GetPath().HasPrefix(SdfPath("/Wrapper/Model")),
           "the glb's mesh composes under /Wrapper/Model");
    if (meshes.size() == 1) {
        _CheckBoxMesh(meshes[0]);
    }
    GfMatrix4d t;
    t.SetTranslate(GfVec3d(100, 0, 0));
    _CheckPlacement(stage, t, "follows the referencing prim's transform");
}

} // anonymous namespace

int
main(int argc, char** argv)
{
    _TestFileFormat();
    _TestBox("box.glb");
    _TestBox("box.gltf");
    _TestNodeTransform();
    _TestReference();
    TF_AXIOM(ok);
    printf("OK\n");
    return 0;
}
