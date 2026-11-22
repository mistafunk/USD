//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// Places the geospatial web prototype's test scene (three placeable model
// anchors in UTM 11N and EPSG:4979, one 2D CRS, one broken binding) through
// usdImaging and UsdGeospatialImagingPlacementSceneIndex, and compares the
// observer's meshes and matrices with the offline pyproj reference's export
// (manifest-*.json, buffers.bin; reference.py imaging-expected).
//
// Also checks that both models land on each other in the shared display
// frame, and that edits and time changes re-place the scene like
// UsdGeospatialPlacementQuery does on the stage.
//
// With --plugin (USDGEOSPATIALIMAGING_OUTPUT_CRS and _DISPLAY_ORIGIN set by
// the test runner), checks what any
// usdImaging client such as usdview sees: the scene index plugin rewrites
// transforms into the display frame and hides what cannot be placed.
//

#include "pxr/pxr.h"
#include "pxr/usdImaging/usdGeospatialImaging/meshObserver.h"
#include "pxr/usdImaging/usdGeospatialImaging/placementSceneIndex.h"
#include "pxr/usdImaging/usdGeospatialImaging/tokens.h"
#include "pxr/usdImaging/usdImaging/sceneIndices.h"
#include "pxr/usdImaging/usdImaging/stageSceneIndex.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeospatial/placement.h"
#include "pxr/usd/usdGeospatial/tokens.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/visibilitySchema.h"
#include "pxr/imaging/hd/xformSchema.h"
#include "pxr/base/js/json.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/stringUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

// Building corner in Site A's model frame, where Site B's mast stands
// (reference.py: BUILDING = 40 x 20 x 12 m, centred).
const GfVec3d buildingCorner(20.0, 10.0, 0.0);

JsObject
_ReadJson(const std::string& path)
{
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    JsParseError error;
    const JsValue value = JsParseString(text.str(), &error);
    TF_AXIOM(value.IsObject());
    return value.GetJsObject();
}

std::vector<char>
_ReadBytes(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(in), {});
}

GfVec3d
_Vec3(const JsValue& v)
{
    const JsArray& a = v.GetJsArray();
    return GfVec3d(a[0].GetReal(), a[1].GetReal(), a[2].GetReal());
}

// The manifest's column-major column-vector matrix, flattened, equals the
// row-major layout of USD's row-vector matrix.
double
_MaxMatrixDiff(const JsValue& expected, const GfMatrix4d& m)
{
    const JsArray& a = expected.GetJsArray();
    double worst = 0.0;
    for (int i = 0; i < 16; ++i) {
        worst = std::max(worst, std::abs(a[i].GetReal() - m[i / 4][i % 4]));
    }
    return worst;
}

template <typename T>
bool
_SameBuffer(const std::vector<char>& blob, const JsObject& view,
            const std::vector<T>& values, int components, double tolerance)
{
    const size_t offset = view.at("byteOffset").GetInt();
    const size_t count = view.at("count").GetInt() * components;
    if (count != values.size() || offset + count * sizeof(T) > blob.size()) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        T expected;
        std::memcpy(&expected, blob.data() + offset + i * sizeof(T),
                    sizeof(T));
        if (std::abs(double(expected) - double(values[i])) > tolerance) {
            return false;
        }
    }
    return true;
}

struct _Chain
{
    UsdImagingSceneIndices usdImaging;
    UsdGeospatialImagingPlacementSceneIndexRefPtr placement;
};

_Chain
_MakeChain(const UsdStageRefPtr& stage, const std::string& outputCrs,
           const GfVec3d& displayOrigin)
{
    UsdImagingCreateSceneIndicesInfo info;
    info.stage = stage;
    _Chain chain;
    chain.usdImaging = UsdImagingCreateSceneIndices(info);
    chain.placement = UsdGeospatialImagingPlacementSceneIndex::New(
        chain.usdImaging.finalSceneIndex,
        HdRetainedContainerDataSource::New(
            UsdGeospatialImagingPlacementSceneIndexTokens->outputCrs,
            HdRetainedTypedSampledDataSource<std::string>::New(outputCrs),
            UsdGeospatialImagingPlacementSceneIndexTokens->applyToXform,
            HdRetainedTypedSampledDataSource<bool>::New(true),
            UsdGeospatialImagingPlacementSceneIndexTokens->displayOrigin,
            HdRetainedTypedSampledDataSource<GfVec3d>::New(displayOrigin),
            UsdGeospatialImagingPlacementSceneIndexTokens->measureLinearization,
            HdRetainedTypedSampledDataSource<bool>::New(true)));
    TF_AXIOM(chain.placement->GetQuery().IsValid());
    return chain;
}

GfMatrix4d
_Xform(const HdSceneIndexBaseRefPtr& sceneIndex, const char* path)
{
    const HdMatrixDataSourceHandle ds = HdXformSchema::GetFromParent(
        sceneIndex->GetPrim(SdfPath(path)).dataSource).GetMatrix();
    TF_AXIOM(ds);
    return ds->GetTypedValue(0.0f);
}

bool
_IsVisible(const HdSceneIndexBaseRefPtr& sceneIndex, const char* path)
{
    const HdBoolDataSourceHandle ds = HdVisibilitySchema::GetFromParent(
        sceneIndex->GetPrim(SdfPath(path)).dataSource).GetVisibility();
    return !ds || ds->GetTypedValue(0.0f);
}

bool
_CheckOutput(const UsdStageRefPtr& stage, const std::string& name)
{
    bool ok = true;
    auto check = [&ok](bool good, const std::string& what) {
        printf("  %s %s\n", good ? "ok  " : "FAIL", what.c_str());
        ok &= good;
    };

    const JsObject manifest = _ReadJson("manifest-" + name + ".json");
    const std::vector<char> blob = _ReadBytes("buffers.bin");
    const JsObject& output = manifest.at("output").GetJsObject();
    const bool enu = output.at("frame").GetString() == "enu";
    printf("output %s (%s, %s frame)\n", name.c_str(),
           output.at("crs").GetString().c_str(),
           output.at("frame").GetString().c_str());

    // Display origin: Site A's origin, so the scene sits near zero.
    GfVec3d displayOrigin(0.0);
    for (const JsValue& a : manifest.at("anchors").GetJsArray()) {
        const JsObject& anchor = a.GetJsObject();
        if (anchor.at("path").GetString() == "/World/SiteA") {
            displayOrigin = _Vec3(anchor.at("origin"));
        }
    }

    // By code: EPSG:3857 is 2D, so this also checks that the query
    // promotes it to 3D like the reference (pyproj to_3d) does.
    const _Chain chain =
        _MakeChain(stage, output.at("crs").GetString(), displayOrigin);
    // The reference evaluates the scene at its geotest:time.
    if (manifest.count("time")) {
        chain.usdImaging.stageSceneIndex->SetTime(
            UsdTimeCode(manifest.at("time").GetReal()));
    }
    UsdGeospatialImagingMeshObserver observer(chain.placement);
    observer.Sync();

    std::set<std::string> incomparable;

    // Anchors: status, origin within 1 mm, matrix within 1 um per metre.
    const double originTolerance[3] = {
        enu ? 1e-8 : 1e-3, enu ? 1e-8 : 1e-3, 1e-3 };
    for (const JsValue& a : manifest.at("anchors").GetJsArray()) {
        const JsObject& expected = a.GetJsObject();
        const std::string path = expected.at("path").GetString();
        const auto it = observer.GetAnchors().find(SdfPath(path));
        if (it == observer.GetAnchors().end()) {
            check(false, path + " is observed as an anchor");
            continue;
        }
        const UsdGeospatialImagingMeshObserver::Anchor& anchor = it->second;
        const std::string status = expected.at("status").GetString();
        // Builds without a grid the expected result used (e.g. WebAssembly)
        // place it differently or not at all, and say which are missing.
        bool usedGridMissing = false;
        if (expected.count("gridsUsed")) {
            for (const JsValue& g : expected.at("gridsUsed").GetJsArray()) {
                usedGridMissing |= std::find(anchor.missingGrids.begin(),
                                             anchor.missingGrids.end(),
                                             g.GetString()) !=
                    anchor.missingGrids.end();
            }
        }
        if (status == "ok" && usedGridMissing) {
            printf("  --   %s not comparable in this build, grids missing: %s\n",
                   path.c_str(), TfStringJoin(anchor.missingGrids, ", ").c_str());
            incomparable.insert(path);
            continue;
        }
        check(anchor.status.GetString() == status,
              TfStringPrintf("%s %s (%s)", path.c_str(),
                             anchor.status.GetText(),
                             anchor.status == "ok"
                                 ? anchor.operation.c_str()
                                 : anchor.reason.c_str()));
        if (status != "ok" || anchor.status != "ok") {
            continue;
        }
        const GfVec3d origin = _Vec3(expected.at("origin"));
        bool originOk = true;
        for (int k = 0; k < 3; ++k) {
            originOk &= std::abs(origin[k] - anchor.origin[k]) <=
                originTolerance[k];
        }
        check(originOk && anchor.sourceCrs ==
                  expected.at("sourceCrs").GetString(),
              "  origin and source CRS");
        const double d = _MaxMatrixDiff(expected.at("matrix"),
                                        anchor.modelToFrame);
        check(d < 1e-6, TfStringPrintf("  matrix (max diff %.2g)", d));
        // T3: with its grids NAD27 is placed with the grid-based NADCON
        // operation (without them it reports them missing, see above).
        if (path == "/World/SiteG") {
            check(TfStringContains(anchor.operation, "NAD27 to NAD83"),
                  "  NAD27 through NADCON: " + anchor.operation);
        }
    }

    // Meshes: status, matrix, linearization error and buffers.
    for (const JsValue& m : manifest.at("meshes").GetJsArray()) {
        const JsObject& expected = m.GetJsObject();
        const std::string path = expected.at("path").GetString();
        const auto it = observer.GetMeshes().find(SdfPath(path));
        if (it == observer.GetMeshes().end()) {
            check(false, path + " is observed as a mesh");
            continue;
        }
        const UsdGeospatialImagingMeshObserver::Mesh& mesh = it->second;
        const std::string status = expected.at("status").GetString();
        if (incomparable.count(mesh.anchor.GetString())) {
            continue;
        }
        check(mesh.status.GetString() == status,
              path + " " + mesh.status.GetString());
        check(_SameBuffer(blob, expected.at("points").GetJsObject(),
                          mesh.points, 3, 1e-6) &&
              _SameBuffer(blob, expected.at("normals").GetJsObject(),
                          mesh.normals, 3, 1e-6) &&
              _SameBuffer(blob, expected.at("indices").GetJsObject(),
                          mesh.indices, 1, 0.0),
              TfStringPrintf("  %zu triangles", mesh.indices.size() / 3));
        const GfVec3d color = _Vec3(expected.at("color"));
        check((GfVec3d(mesh.color) - color).GetLength() < 1e-6, "  color");
        if (status != "ok" || mesh.status != "ok") {
            check(!_IsVisible(chain.placement, path.c_str()),
                  "  not drawn");
            continue;
        }
        const double d = _MaxMatrixDiff(expected.at("matrix"),
                                        mesh.modelToFrame);
        check(d < 1e-6, TfStringPrintf("  matrix (max diff %.2g)", d));
        const double expectedError =
            expected.at("linearizationError").GetReal();
        check(mesh.linearizationError &&
              std::abs(*mesh.linearizationError - expectedError) < 1e-5,
              TfStringPrintf("  linearization error %.3g (reference %.3g)",
                             mesh.linearizationError.value_or(-1.0),
                             expectedError));
    }

    // Display frame: Site B's mast (EPSG:4979) stands on Site A's building
    // corner (UTM 11N), and Site A is at the display origin.
    const GfVec3d corner = _Xform(chain.placement, "/World/SiteA/Building")
        .Transform(buildingCorner);
    const GfVec3d mast = _Xform(chain.placement, "/World/SiteB")
        .Transform(GfVec3d(0.0));
    const double gap = (corner - mast).GetLength();
    // In a projected display the frame is scaled by the projection.
    check(gap < (enu ? 1e-3 : 2e-3),
          TfStringPrintf("display: mast on building corner (%.2g %s)", gap,
                         enu ? "m" : "units"));
    const GfVec3d siteA = _Xform(chain.placement, "/World/SiteA")
        .Transform(GfVec3d(0.0));
    check(siteA.GetLength() < 1e-6, "display: Site A at the display origin");
    return ok;
}

// Edits and time changes re-place the scene, with the same result as
// UsdGeospatialPlacementQuery on the stage.
bool
_CheckUpdates(const UsdStageRefPtr& stage)
{
    bool ok = true;
    auto check = [&ok](bool good, const std::string& what) {
        printf("  %s %s\n", good ? "ok  " : "FAIL", what.c_str());
        ok &= good;
    };
    printf("updates (EPSG:4979)\n");

    const std::string output = "EPSG:4979";
    _Chain chain = _MakeChain(stage, output, GfVec3d(-117.1955, 34.0565, 0));
    UsdGeospatialImagingMeshObserver observer(chain.placement);
    observer.Sync();
    const UsdGeospatialPlacementQuery query(output);
    const SdfPath siteA("/World/SiteA");
    const UsdPrim anchor = stage->GetPrimAtPath(siteA);
    const UsdAttribute orientation =
        anchor.GetAttribute(UsdGeospatialTokens->crsOrientation);

    auto matches = [&](UsdTimeCode time) {
        const UsdGeospatialPlacement expected = query.Resolve(anchor, time);
        const GfMatrix4d& got =
            observer.GetAnchors().at(siteA).modelToFrame;
        return expected.IsOk() &&
            GfIsClose(got, expected.GetModelToFrame(), 1e-9);
    };

    const GfMatrix4d before =
        observer.GetMeshes().at(SdfPath("/World/SiteA/Building"))
            .modelToFrame;

    // A new heading.
    orientation.Set(GfVec3d(75.0, 0.0, 0.0));
    chain.usdImaging.stageSceneIndex->ApplyPendingUpdates();
    SdfPathVector changed = observer.Sync();
    const bool buildingChanged = std::find(
        changed.begin(), changed.end(),
        SdfPath("/World/SiteA/Building")) != changed.end();
    check(buildingChanged, "heading edit re-places the building");
    check(!GfIsClose(before,
                     observer.GetMeshes().at(
                         SdfPath("/World/SiteA/Building")).modelToFrame,
                     1e-6),
          "  building matrix changed");
    check(matches(UsdTimeCode::Default()), "  anchor matches the query");

    // A time-sampled heading, slerped between samples.
    orientation.Clear();
    orientation.Set(GfVec3d(0.0, 0.0, 0.0), 0.0);
    orientation.Set(GfVec3d(90.0, 0.0, 0.0), 10.0);
    chain.usdImaging.stageSceneIndex->ApplyPendingUpdates();
    observer.Sync();
    chain.usdImaging.stageSceneIndex->SetTime(UsdTimeCode(5.0));
    changed = observer.Sync();
    check(std::find(changed.begin(), changed.end(),
                    SdfPath("/World/SiteA/Building")) != changed.end(),
          "time change re-places the building");
    check(matches(UsdTimeCode(5.0)), "  anchor matches the query at time 5");
    return ok;
}

// usdImaging alone, with the scene index plugin enabled by the environment.
bool
_CheckPlugin(const UsdStageRefPtr& stage)
{
    bool ok = true;
    auto check = [&ok](bool good, const std::string& what) {
        printf("  %s %s\n", good ? "ok  " : "FAIL", what.c_str());
        ok &= good;
    };
    printf("scene index plugin (USDGEOSPATIALIMAGING_OUTPUT_CRS)\n");

    UsdImagingCreateSceneIndicesInfo info;
    info.stage = stage;
    const HdSceneIndexBaseRefPtr sceneIndex =
        UsdImagingCreateSceneIndices(info).finalSceneIndex;

    const GfVec3d corner = _Xform(sceneIndex, "/World/SiteA/Building")
        .Transform(buildingCorner);
    const GfVec3d mast = _Xform(sceneIndex, "/World/SiteB")
        .Transform(GfVec3d(0.0));
    const double gap = (corner - mast).GetLength();
    check(gap < 1e-3,
          TfStringPrintf("mast on building corner (%.2g m)", gap));
    const GfVec3d siteA = _Xform(sceneIndex, "/World/SiteA")
        .Transform(GfVec3d(0.0));
    check(siteA.GetLength() < 1e-6, "Site A at the display origin");
    // Site E: direct binding to a missing CRS prim.
    check(!_IsVisible(sceneIndex, "/World/SiteE/Box"),
          "unplaceable models are not drawn");
    check(_IsVisible(sceneIndex, "/World/SiteC/Box"), "placed models are");
    return ok;
}

} // anonymous namespace

int
main(int argc, char** argv)
{
    const UsdStageRefPtr stage = UsdStage::Open("scene.usda");
    TF_AXIOM(stage);

    if (argc > 1 && std::string(argv[1]) == "--plugin") {
        TF_AXIOM(_CheckPlugin(stage));
        printf("OK\n");
        return 0;
    }

    bool ok = true;
    ok &= _CheckOutput(stage, "global");
    ok &= _CheckOutput(stage, "local-3857");
    ok &= _CheckUpdates(stage);
    TF_AXIOM(ok);

    printf("OK\n");
    return 0;
}
