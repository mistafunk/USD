//
// geospatialModule: the browser module (C4). OpenUSD with usdImaging, the
// geospatial placement scene index and the PROJ engine, behind a small
// embind API that the web app's worker calls (app/src/usd/worker.ts).
//
//     const m = await createGeospatialModule();
//     m.FS.writeFile("/data/scene.usda", bytes);   // and its dependencies
//     const scene = new m.GeospatialScene();
//     scene.open("/data/scene.usda");              // "" or an error
//     scene.setOutput("EPSG:4979", false, false);  // "" or an error
//     scene.sync();                                // changed prim paths
//     scene.anchors(); scene.meshes();             // see below
//
// meshes() returns typed arrays that view the module's memory; copy them
// (slice()) before the next call into the module.
//

#include "testData.h"

#include "pxr/pxr.h"
#include "pxr/usdImaging/usdGeospatialImaging/meshObserver.h"
#include "pxr/usdImaging/usdGeospatialImaging/placementSceneIndex.h"
#include "pxr/usdImaging/usdGeospatialImaging/tokens.h"
#include "pxr/usdImaging/usdImaging/sceneIndices.h"
#include "pxr/usdImaging/usdImaging/stageSceneIndex.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeospatial/binding.h"
#include "pxr/usd/usdGeospatial/engineRegistry.h"
#include "pxr/usd/usdGeospatial/placement.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/work/threadLimits.h"

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <proj.h>

#include <memory>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

using emscripten::val;

namespace {

val
_Vec3(const GfVec3d& v)
{
    val a = val::array();
    for (int k = 0; k < 3; ++k) {
        a.call<void>("push", v[k]);
    }
    return a;
}

// Column-major column-vector matrix = row-major layout of USD's row-vector
// matrix (docs/RECORDED_FORMAT.md).
val
_Matrix(const GfMatrix4d& m)
{
    val a = val::array();
    for (int i = 0; i < 16; ++i) {
        a.call<void>("push", m[i / 4][i % 4]);
    }
    return a;
}

std::vector<GfVec3d>
_Points(const val& points)
{
    std::vector<GfVec3d> result;
    const unsigned n = points["length"].as<unsigned>();
    for (unsigned i = 0; i < n; ++i) {
        const val p = points[i];
        result.emplace_back(p[0].as<double>(), p[1].as<double>(),
                            p[2].as<double>());
    }
    return result;
}

std::string
_Errors(const TfErrorMark& mark)
{
    std::string text;
    for (const TfError& e : mark) {
        text += (text.empty() ? "" : "; ") + e.GetCommentary();
    }
    return text;
}

} // anonymous namespace

/// One stage, placed in one output CRS at a time.
namespace {

/// "EPSG:4326" for a PROJ object with an identifier, else empty.
std::string
_CrsId(const PJ* obj)
{
    const char* auth = proj_get_id_auth_name(obj, 0);
    const char* code = proj_get_id_code(obj, 0);
    return auth && code ? std::string(auth) + ":" + code : std::string();
}

/// The EPSG identifier of a CRS PROJ matches with full confidence (e.g. a
/// WKT without ID[]), else empty.
std::string
_Identify(PJ_CONTEXT* ctx, const PJ* crs)
{
    int* confidence = nullptr;
    PJ_OBJ_LIST* list = proj_identify(ctx, crs, "EPSG", nullptr, &confidence);
    std::string id;
    if (list && proj_list_get_count(list) > 0 && confidence &&
        confidence[0] == 100) {
        PJ* best = proj_list_get(ctx, list, 0);
        id = _CrsId(best);
        proj_destroy(best);
    }
    proj_int_list_destroy(confidence);
    proj_list_destroy(list);
    return id;
}

/// _CrsId, else _Identify.
std::string
_IdOrIdentify(PJ_CONTEXT* ctx, const PJ* crs)
{
    const std::string id = _CrsId(crs);
    return id.empty() ? _Identify(ctx, crs) : id;
}

int
_AxisCount(PJ_CONTEXT* ctx, const PJ* crs)
{
    if (proj_get_type(crs) == PJ_TYPE_COMPOUND_CRS) {
        int n = 0;
        for (int i = 0; i < 2; ++i) {
            if (PJ* sub = proj_crs_get_sub_crs(ctx, crs, i)) {
                n += _AxisCount(ctx, sub);
                proj_destroy(sub);
            }
        }
        return n;
    }
    PJ* cs = proj_crs_get_coordinate_system(ctx, crs);
    const int n = cs ? proj_cs_get_axis_count(ctx, cs) : 0;
    proj_destroy(cs);
    return n;
}

const char*
_CrsType(PJ_TYPE type)
{
    switch (type) {
    case PJ_TYPE_GEOGRAPHIC_2D_CRS: return "geographic 2D";
    case PJ_TYPE_GEOGRAPHIC_3D_CRS: return "geographic 3D";
    case PJ_TYPE_GEOCENTRIC_CRS: return "geocentric";
    case PJ_TYPE_PROJECTED_CRS: return "projected";
    case PJ_TYPE_COMPOUND_CRS: return "compound";
    case PJ_TYPE_VERTICAL_CRS: return "vertical";
    case PJ_TYPE_ENGINEERING_CRS: return "engineering";
    case PJ_TYPE_DERIVED_PROJECTED_CRS: return "derived projected";
    case PJ_TYPE_OTHER_CRS: return "other";
    default: return "unknown";
    }
}

val
_NameAndId(PJ_CONTEXT* ctx, const PJ* obj)
{
    val o = val::object();
    const char* name = proj_get_name(obj);
    o.set("name", std::string(name ? name : ""));
    o.set("id", _IdOrIdentify(ctx, obj));
    return o;
}

/// A CRS for the app's diagnostics (D27): name, identifier, type, axis count,
/// horizontal and vertical parts, area of use and WKT2. A 2D CRS gets the
/// proposal's default height (EPSG:3855, EGM2008), as placement applies it.
val
_DescribeCrs(const std::string& definition)
{
    val o = val::object();
    PJ_CONTEXT* ctx = proj_context_create();
    PJ* crs = proj_create(ctx, definition.c_str());
    if (!crs || !proj_is_crs(crs)) {
        o.set("error", std::string(
            proj_context_errno_string(ctx, proj_context_errno(ctx))));
        proj_destroy(crs);
        proj_context_destroy(ctx);
        return o;
    }
    const PJ_TYPE type = proj_get_type(crs);
    const int dimension = _AxisCount(ctx, crs);
    const char* name = proj_get_name(crs);
    const char* wkt = proj_as_wkt(ctx, crs, PJ_WKT2_2019, nullptr);
    o.set("name", std::string(name ? name : ""));
    std::string id = _IdOrIdentify(ctx, crs);
    o.set("type", std::string(_CrsType(type)));
    o.set("dimension", dimension);
    o.set("wkt", std::string(wkt ? wkt : definition));
    if (type == PJ_TYPE_COMPOUND_CRS) {
        PJ* horizontal = proj_crs_get_sub_crs(ctx, crs, 0);
        PJ* vertical = proj_crs_get_sub_crs(ctx, crs, 1);
        const val hpart = horizontal ? _NameAndId(ctx, horizontal) : val::undefined();
        const val vpart = vertical ? _NameAndId(ctx, vertical) : val::undefined();
        if (horizontal) {
            o.set("horizontal", hpart);
        }
        if (vertical) {
            o.set("vertical", vpart);
        }
        // A compound without its own code: "EPSG:2229+6360" from its parts.
        const std::string hid = horizontal ? hpart["id"].as<std::string>() : "";
        const std::string vid = vertical ? vpart["id"].as<std::string>() : "";
        if (id.empty() && TfStringStartsWith(hid, "EPSG:") &&
            TfStringStartsWith(vid, "EPSG:")) {
            id = hid + "+" + vid.substr(5);
        }
        proj_destroy(horizontal);
        proj_destroy(vertical);
    } else {
        // The horizontal part of a 3D CRS (e.g. a projected CRS promoted to
        // 3D) is its 2D form, which EPSG usually has.
        PJ* demoted = _AxisCount(ctx, crs) == 3 && type != PJ_TYPE_GEOCENTRIC_CRS
            ? proj_crs_demote_to_2D(ctx, nullptr, crs) : nullptr;
        o.set("horizontal", _NameAndId(ctx, demoted ? demoted : crs));
        proj_destroy(demoted);
        if (dimension == 3 && type != PJ_TYPE_GEOCENTRIC_CRS) {
            val v = val::object();
            v.set("name", std::string("ellipsoidal height"));
            v.set("id", std::string());
            o.set("vertical", v);
        } else if (dimension == 2) {
            o.set("defaultHeight", std::string("EPSG:3855"));
        }
    }
    double west, south, east, north;
    const char* area = nullptr;
    if (proj_get_area_of_use(ctx, crs, &west, &south, &east, &north, &area)) {
        val a = val::object();
        a.set("name", std::string(area ? area : ""));
        a.set("west", west);
        a.set("south", south);
        a.set("east", east);
        a.set("north", north);
        o.set("areaOfUse", a);
    }
    o.set("id", id);
    o.set("error", std::string());
    proj_destroy(crs);
    proj_context_destroy(ctx);
    return o;
}

} // anonymous namespace

class GeospatialScene
{
public:
    /// Opens the stage at \p path in the module's file system. Returns an
    /// empty string, or why it could not be opened.
    std::string Open(const std::string& path)
    {
        Close();
        TfErrorMark mark;
        _stage = UsdStage::Open(path);
        if (!_stage) {
            const std::string errors = _Errors(mark);
            return "cannot open " + path + (errors.empty() ? "" : ": " + errors);
        }
        return std::string();
    }

    void Close()
    {
        _time = UsdTimeCode::Default();
        _observer.reset();
        _placement.Reset();
        _sceneIndices = UsdImagingSceneIndices();
        _stage.Reset();
    }

    /// Places the stage in \p outputCrs (e.g. "EPSG:4979" for a global
    /// view, the view's spatial reference for a local one), optionally
    /// measuring the matrix shortcut and publishing exact per-vertex frame
    /// points (measurement mode). Returns an empty string, or why the output
    /// CRS cannot be used.
    std::string SetOutput(const std::string& outputCrs,
                          bool measureLinearization, bool exactPoints)
    {
        if (!_stage) {
            return "no stage";
        }
        const auto& tokens = UsdGeospatialImagingPlacementSceneIndexTokens;

        _observer.reset();
        UsdImagingCreateSceneIndicesInfo info;
        info.stage = _stage;
        _sceneIndices = UsdImagingCreateSceneIndices(info);
        _sceneIndices.stageSceneIndex->SetTime(_time);
        _placement = UsdGeospatialImagingPlacementSceneIndex::New(
            _sceneIndices.finalSceneIndex,
            HdRetainedContainerDataSource::New(
                tokens->outputCrs,
                HdRetainedTypedSampledDataSource<std::string>::New(outputCrs),
                tokens->measureLinearization,
                HdRetainedTypedSampledDataSource<bool>::New(
                    measureLinearization),
                tokens->exactPoints,
                HdRetainedTypedSampledDataSource<bool>::New(exactPoints)));
        const UsdGeospatialPlacementQuery& query = _placement->GetQuery();
        if (!query.IsValid()) {
            const std::string error = query.GetError();
            _placement.Reset();
            return error;
        }
        _observer =
            std::make_unique<UsdGeospatialImagingMeshObserver>(_placement);
        return std::string();
    }

    /// The output CRS definition in use (3D; a 2D output is promoted).
    std::string GetOutputWkt() const
    {
        return _placement ? _placement->GetQuery().GetOutput() : std::string();
    }

    /// True if the output is geographic, so matrices map into metres
    /// east/north/up at each anchor ("enu"), else into output CRS units
    /// ("crs").
    std::string GetFrame() const
    {
        return _placement && _placement->GetQuery().GetOutputInfo().isGeographic
            ? "enu" : "crs";
    }

    /// The time the stage is evaluated at, by the scene chain and
    /// resolve(); applies to chains set up later too.
    void SetTime(double time)
    {
        _time = UsdTimeCode(time);
        if (_sceneIndices.stageSceneIndex) {
            _sceneIndices.stageSceneIndex->SetTime(_time);
        }
    }

    /// Applies stage edits and reads changed prims. Returns the paths of the
    /// meshes and anchors that changed or were removed.
    val Sync()
    {
        val changed = val::array();
        if (!_observer) {
            return changed;
        }
        _sceneIndices.stageSceneIndex->ApplyPendingUpdates();
        for (const SdfPath& path : _observer->Sync()) {
            changed.call<void>("push", path.GetString());
        }
        return changed;
    }

    double GetMetersPerUnit() const
    {
        return _stage ? UsdGeomGetStageMetersPerUnit(_stage) : 1.0;
    }

    std::string GetUpAxis() const
    {
        return _stage ? UsdGeomGetStageUpAxis(_stage).GetString() : "Z";
    }

    /// Model anchors: path, status, reason, engine, operation, accuracy
    /// (null if unknown), sourceCrs, position, orientation, origin, matrix.
    val Anchors() const
    {
        val result = val::array();
        if (!_observer) {
            return result;
        }
        for (const auto& [path, a] : _observer->GetAnchors()) {
            val o = val::object();
            o.set("path", path.GetString());
            o.set("status", a.status.GetString());
            o.set("reason", a.reason);
            o.set("engine", a.engine);
            o.set("operation", a.operation);
            o.set("accuracy", a.accuracy ? val(*a.accuracy) : val::null());
            val skipped = val::array();
            for (const std::string& s : a.skippedOperations) {
                skipped.call<void>("push", s);
            }
            o.set("skippedOperations", skipped);
            val grids = val::array();
            for (const std::string& g : a.missingGrids) {
                grids.call<void>("push", g);
            }
            o.set("missingGrids", grids);
            o.set("sourceCrs", a.sourceCrs);
            o.set("position", _Vec3(a.position));
            o.set("orientation", _Vec3(a.orientation));
            o.set("origin", _Vec3(a.origin));
            o.set("matrix", _Matrix(a.modelToFrame));
            result.call<void>("push", o);
        }
        return result;
    }

    /// Meshes: path, anchor (null without a binding), status, reason, color,
    /// matrix, linearizationError (null unless measured), and points,
    /// normals (Float32Array) and indices (Uint32Array) viewing module
    /// memory.
    val Meshes() const
    {
        val result = val::array();
        if (!_observer) {
            return result;
        }
        for (const auto& [path, m] : _observer->GetMeshes()) {
            val o = val::object();
            o.set("path", path.GetString());
            o.set("anchor", m.anchor.IsEmpty()
                ? val::null() : val(m.anchor.GetString()));
            o.set("status", m.status.GetString());
            o.set("reason", m.reason);
            o.set("color", _Vec3(GfVec3d(m.color)));
            o.set("matrix", _Matrix(m.modelToFrame));
            o.set("linearizationError", m.linearizationError
                ? val(*m.linearizationError) : val::null());
            o.set("points", val(emscripten::typed_memory_view(
                m.points.size(), m.points.data())));
            o.set("normals", val(emscripten::typed_memory_view(
                m.normals.size(), m.normals.data())));
            o.set("indices", val(emscripten::typed_memory_view(
                m.indices.size(), m.indices.data())));
            o.set("framePoints", m.framePoints.empty() ? val::null()
                : val(emscripten::typed_memory_view(
                    m.framePoints.size(), m.framePoints.data())));
            result.call<void>("push", o);
        }
        return result;
    }

    /// Test data on the stage (testData.h): controlPoints, testCases and
    /// time (null if the stage does not set one), in the recorded format.
    val TestData() const
    {
        val o = val::object();
        val points = val::array();
        val cases = val::array();
        o.set("time", val::null());
        if (!_stage) {
            o.set("controlPoints", points);
            o.set("testCases", cases);
            return o;
        }
        const GeospatialWebTestData data = GeospatialWebReadTestData(_stage);
        for (const GeospatialWebControlPoint& p : data.controlPoints) {
            val cp = val::object();
            cp.set("path", p.path);
            cp.set("anchor", p.anchor.empty() ? val::null() : val(p.anchor));
            cp.set("local", p.local ? _Vec3(*p.local) : val::null());
            val reference = val::object();
            reference.set("crs", p.referenceCrs);
            reference.set("position", _Vec3(p.referencePosition));
            cp.set("reference", reference);
            points.call<void>("push", cp);
        }
        for (const GeospatialWebTestCase& c : data.testCases) {
            val tc = val::object();
            tc.set("anchor", c.anchor);
            tc.set("expect", c.expect);
            tc.set("description", c.description);
            if (!c.expectLocal.empty()) {
                tc.set("expectLocal", c.expectLocal);
            }
            if (!c.expectBrowser.empty()) {
                tc.set("expectBrowser", c.expectBrowser);
            }
            cases.call<void>("push", tc);
        }
        o.set("controlPoints", points);
        o.set("testCases", cases);
        if (data.time) {
            o.set("time", *data.time);
        }
        return o;
    }

    /// The C1 query on the stage, independent of Hydra: places the anchor at
    /// \p anchorPath in \p outputCrs and maps \p points ([[x, y, z], ...],
    /// anchor model frame, stage units) exactly. Returns status, reason,
    /// engine, operation, accuracy, origin, matrix and outputPoints.
    val Resolve(const std::string& anchorPath, const std::string& outputCrs,
                const val& points) const
    {
        val o = val::object();
        if (!_stage) {
            o.set("status", std::string("error"));
            o.set("reason", std::string("no stage"));
            return o;
        }
        const UsdGeospatialPlacementQuery query(outputCrs);
        const UsdGeospatialPlacement placement = query.Resolve(
            _stage->GetPrimAtPath(SdfPath(anchorPath)), _time);
        o.set("status", std::string(
            placement.IsOk() ? "ok"
            : placement.GetStatus() ==
                UsdGeospatialPlacement::Status::Unsupported ? "unsupported"
            : "error"));
        o.set("reason", placement.GetReason());
        o.set("engine", placement.GetEngine());
        val grids = val::array();
        for (const std::string& g : placement.GetMissingGrids()) {
            grids.call<void>("push", g);
        }
        o.set("missingGrids", grids);
        if (!placement.IsOk()) {
            return o;
        }
        o.set("operation", placement.GetOperation());
        o.set("accuracy", placement.GetAccuracy() >= 0.0
            ? val(placement.GetAccuracy()) : val::null());
        o.set("origin", _Vec3(placement.GetOrigin()));
        o.set("matrix", _Matrix(placement.GetModelToFrame()));
        std::vector<GfVec3d> output;
        std::string error;
        val outputPoints = val::array();
        if (placement.ComputeOutputPoints(_Points(points), &output, &error)) {
            for (const GfVec3d& p : output) {
                outputPoints.call<void>("push", _Vec3(p));
            }
        } else {
            o.set("pointsError", error);
        }
        o.set("outputPoints", outputPoints);
        return o;
    }

    /// The source CRS of each anchor in \p paths, from its composed
    /// crs:wkt (D27): { path: description } (see _DescribeCrs); a broken
    /// binding gives { error }.
    val AnchorCrs(const val& paths) const
    {
        val o = val::object();
        const unsigned n = paths["length"].as<unsigned>();
        for (unsigned i = 0; i < n; ++i) {
            const std::string path = paths[i].as<std::string>();
            const UsdPrim prim = _stage
                ? _stage->GetPrimAtPath(SdfPath(path)) : UsdPrim();
            const UsdGeospatialBinding binding =
                UsdGeospatialResolveBinding(prim);
            if (binding) {
                o.set(path, _DescribeCrs(binding.wkt.GetString()));
            } else {
                val e = val::object();
                e.set("error", binding.error.empty()
                    ? std::string("not bound") : binding.error);
                o.set(path, e);
            }
        }
        return o;
    }

private:
    UsdStageRefPtr _stage;
    UsdTimeCode _time = UsdTimeCode::Default();
    UsdImagingSceneIndices _sceneIndices;
    UsdGeospatialImagingPlacementSceneIndexRefPtr _placement;
    std::unique_ptr<UsdGeospatialImagingMeshObserver> _observer;
};

/// Module-wide information: the active engine, the registered engines and
/// the work thread limit.
val
GetInfo()
{
    val o = val::object();
    UsdGeospatialEngineRegistry& registry =
        UsdGeospatialEngineRegistry::GetInstance();
    o.set("engine", registry.GetActiveEngineName());
    val engines = val::array();
    for (const std::string& name : registry.GetEngineNames()) {
        engines.call<void>("push", name);
    }
    o.set("engines", engines);
    o.set("threads", WorkGetConcurrencyLimit());
    return o;
}

/// The WKT2 (2019) of a CRS definition PROJ accepts, e.g. "EPSG:4326" or a
/// compound "EPSG:4326+5773", for authoring crs:wkt (wrapper layers for
/// ArcGIS 3D object layers). With \p promote3D a 2D geographic or projected
/// CRS gets an ellipsoidal height axis. Returns { wkt, error }.
val
CrsWkt(const std::string& definition, bool promote3D)
{
    val o = val::object();
    PJ_CONTEXT* ctx = proj_context_create();
    PJ* crs = proj_create(ctx, definition.c_str());
    if (crs && promote3D) {
        const PJ_TYPE type = proj_get_type(crs);
        if (type == PJ_TYPE_GEOGRAPHIC_2D_CRS || type == PJ_TYPE_PROJECTED_CRS) {
            if (PJ* promoted = proj_crs_promote_to_3D(ctx, nullptr, crs)) {
                proj_destroy(crs);
                crs = promoted;
            }
        }
    }
    const char* wkt = crs && proj_is_crs(crs)
        ? proj_as_wkt(ctx, crs, PJ_WKT2_2019, nullptr) : nullptr;
    o.set("wkt", std::string(wkt ? wkt : ""));
    o.set("error", wkt ? std::string() : std::string(
        proj_context_errno_string(ctx, proj_context_errno(ctx))));
    proj_destroy(crs);
    proj_context_destroy(ctx);
    return o;
}

/// New engine instances, which see grid files added to PROJ's search path
/// (PROJ_DATA, e.g. /proj) since; call setOutput again afterwards.
void
ResetEngines()
{
    UsdGeospatialEngineRegistry::GetInstance().ResetEngines();
}

/// The map projection factors of the projected CRS \p definition (or the
/// projected part of a compound) at each of \p positions ([easting,
/// northing] in its units), from the active engine: [{ meridianConvergence,
/// meridionalScale, parallelScale } or { error }]. A 3D object layer's
/// models are in grid directions and lengths; the app turns them into the
/// placement's east/north/up frame with these.
val
ProjectionFactors(const std::string& definition, const val& positions)
{
    val out = val::array();
    const UsdGeospatialCoordinateOperationEngineSharedPtr engine =
        UsdGeospatialEngineRegistry::GetInstance().GetEngine();
    const unsigned n = positions["length"].as<unsigned>();
    for (unsigned i = 0; i < n; ++i) {
        val o = val::object();
        if (!engine) {
            o.set("error", std::string("no coordinate operation engine"));
        } else {
            const UsdGeospatialProjectionFactors f =
                engine->ComputeProjectionFactors(
                    definition, GfVec3d(positions[i][0].as<double>(),
                                        positions[i][1].as<double>(), 0.0));
            if (f) {
                o.set("meridianConvergence", f.meridianConvergence);
                o.set("meridionalScale", f.meridionalScale);
                o.set("parallelScale", f.parallelScale);
            } else {
                o.set("error", f.error);
            }
        }
        out.call<void>("push", o);
    }
    return out;
}

/// Caps USD's work threads, e.g. to the module's pthread pool.
void
SetThreadLimit(int n)
{
    WorkSetConcurrencyLimitArgument(n);
}

EMSCRIPTEN_BINDINGS(geospatialModule)
{
    emscripten::class_<GeospatialScene>("GeospatialScene")
        .constructor<>()
        .function("open", &GeospatialScene::Open)
        .function("close", &GeospatialScene::Close)
        .function("setOutput", &GeospatialScene::SetOutput)
        .function("outputWkt", &GeospatialScene::GetOutputWkt)
        .function("frame", &GeospatialScene::GetFrame)
        .function("setTime", &GeospatialScene::SetTime)
        .function("sync", &GeospatialScene::Sync)
        .function("metersPerUnit", &GeospatialScene::GetMetersPerUnit)
        .function("upAxis", &GeospatialScene::GetUpAxis)
        .function("anchors", &GeospatialScene::Anchors)
        .function("meshes", &GeospatialScene::Meshes)
        .function("resolve", &GeospatialScene::Resolve)
        .function("testData", &GeospatialScene::TestData)
        .function("anchorCrs", &GeospatialScene::AnchorCrs);
    emscripten::function("info", &GetInfo);
    emscripten::function("crsWkt", &CrsWkt);
    emscripten::function("resetEngines", &ResetEngines);
    emscripten::function("describeCrs", &_DescribeCrs);
    emscripten::function("projectionFactors", &ProjectionFactors);
    emscripten::function("setThreadLimit", &SetThreadLimit);
}
