//
// geospatialModule: the browser module (C4). OpenUSD with usdImaging, the
// geospatial placement scene index and the PROJ engine, behind a small
// embind API that the web app's worker calls (app/src/usd/worker.ts).
//
//     const m = await createGeospatialModule();
//     m.FS.writeFile("/data/scene.usda", bytes);   // and its dependencies
//     const scene = new m.GeospatialScene();
//     scene.open("/data/scene.usda");              // "" or an error
//     scene.setOutput("EPSG:4979", false);         // "" or an error
//     scene.sync();                                // changed prim paths
//     scene.anchors(); scene.meshes();             // see below
//
// meshes() returns typed arrays that view the module's memory; copy them
// (slice()) before the next call into the module.
//

#include "pxr/pxr.h"
#include "pxr/usdImaging/usdGeospatialImaging/meshObserver.h"
#include "pxr/usdImaging/usdGeospatialImaging/placementSceneIndex.h"
#include "pxr/usdImaging/usdGeospatialImaging/tokens.h"
#include "pxr/usdImaging/usdImaging/sceneIndices.h"
#include "pxr/usdImaging/usdImaging/stageSceneIndex.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeospatial/engineRegistry.h"
#include "pxr/usd/usdGeospatial/placement.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/work/threadLimits.h"

#include <emscripten/bind.h>
#include <emscripten/val.h>

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
        _observer.reset();
        _placement.Reset();
        _sceneIndices = UsdImagingSceneIndices();
        _stage.Reset();
    }

    /// Places the stage in \p outputCrs (e.g. "EPSG:4979" for a global
    /// view, the view's spatial reference for a local one). Returns an empty
    /// string, or why the output CRS cannot be used.
    std::string SetOutput(const std::string& outputCrs,
                          bool measureLinearization)
    {
        if (!_stage) {
            return "no stage";
        }
        const auto& tokens = UsdGeospatialImagingPlacementSceneIndexTokens;

        _observer.reset();
        UsdImagingCreateSceneIndicesInfo info;
        info.stage = _stage;
        _sceneIndices = UsdImagingCreateSceneIndices(info);
        _placement = UsdGeospatialImagingPlacementSceneIndex::New(
            _sceneIndices.finalSceneIndex,
            HdRetainedContainerDataSource::New(
                tokens->outputCrs,
                HdRetainedTypedSampledDataSource<std::string>::New(outputCrs),
                tokens->measureLinearization,
                HdRetainedTypedSampledDataSource<bool>::New(
                    measureLinearization)));
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

    void SetTime(double time)
    {
        if (_sceneIndices.stageSceneIndex) {
            _sceneIndices.stageSceneIndex->SetTime(UsdTimeCode(time));
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
            result.call<void>("push", o);
        }
        return result;
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
            _stage->GetPrimAtPath(SdfPath(anchorPath)));
        o.set("status", std::string(
            placement.IsOk() ? "ok"
            : placement.GetStatus() ==
                UsdGeospatialPlacement::Status::Unsupported ? "unsupported"
            : "error"));
        o.set("reason", placement.GetReason());
        o.set("engine", placement.GetEngine());
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

private:
    UsdStageRefPtr _stage;
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
        .function("resolve", &GeospatialScene::Resolve);
    emscripten::function("info", &GetInfo);
    emscripten::function("setThreadLimit", &SetThreadLimit);
}
