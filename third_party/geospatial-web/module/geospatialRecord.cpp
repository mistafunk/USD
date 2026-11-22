//
// geospatialRecord: places a stage through usdImaging and the geospatial
// placement scene index, and writes what UsdGeospatialImagingMeshObserver
// sees in the recorded format (docs/RECORDED_FORMAT.md), so the web app can
// draw the C++ result like the offline reference's export.
//
//     geospatialRecord <stage> <outDir> [name=CRS ...]
//
// Outputs default to global=EPSG:4979 and local-3857=EPSG:3857.
//

#include "testData.h"

#include "pxr/pxr.h"
#include "pxr/usdImaging/usdGeospatialImaging/meshObserver.h"
#include "pxr/usdImaging/usdGeospatialImaging/placementSceneIndex.h"
#include "pxr/usdImaging/usdGeospatialImaging/tokens.h"
#include "pxr/usdImaging/usdImaging/sceneIndices.h"
#include "pxr/usdImaging/usdImaging/stageSceneIndex.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/xformCache.h"
#include "pxr/usd/usdGeospatial/binding.h"
#include "pxr/usd/usdGeospatial/crsBindingAPI.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/base/js/json.h"
#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/stringUtils.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

struct _View
{
    size_t byteOffset = 0;
    size_t count = 0;
};

struct _MeshBuffers
{
    _View points, normals, indices;
};

// Appends little-endian 4-byte values (the host is little-endian on every
// platform we build for).
template <typename T>
_View
_Append(std::vector<char>* blob, const std::vector<T>& values,
        size_t components)
{
    static_assert(sizeof(T) == 4, "buffers hold 4-byte values");
    _View view{blob->size(), values.size() / components};
    const char* bytes = reinterpret_cast<const char*>(values.data());
    blob->insert(blob->end(), bytes, bytes + values.size() * sizeof(T));
    return view;
}

void
_WriteVec3(JsWriter& w, const GfVec3d& v)
{
    w.BeginArray();
    for (int k = 0; k < 3; ++k) {
        w.WriteValue(v[k]);
    }
    w.EndArray();
}

// Column-major column-vector matrix = row-major layout of USD's row-vector
// matrix.
void
_WriteMatrix(JsWriter& w, const GfMatrix4d& m)
{
    w.BeginArray();
    for (int i = 0; i < 16; ++i) {
        w.WriteValue(m[i / 4][i % 4]);
    }
    w.EndArray();
}

void
_WriteView(JsWriter& w, const char* name, const _View& view)
{
    w.WriteKey(name);
    w.BeginObject();
    w.WriteKeyValue("byteOffset", static_cast<int64_t>(view.byteOffset));
    w.WriteKeyValue("count", static_cast<int64_t>(view.count));
    w.EndObject();
}

// Control points and test cases are test data on the stage (geotest:
// attributes, reference/README.md); the harness reads them from here.
void
_WriteTestData(JsWriter& w, const GeospatialWebTestData& data)
{
    w.WriteKey("controlPoints");
    w.BeginArray();
    for (const GeospatialWebControlPoint& p : data.controlPoints) {
        w.BeginObject();
        w.WriteKeyValue("path", p.path);
        w.WriteKey("anchor");
        if (p.anchor.empty()) {
            w.WriteValue(nullptr);
        } else {
            w.WriteValue(p.anchor);
        }
        w.WriteKey("local");
        if (p.local) {
            _WriteVec3(w, *p.local);
        } else {
            w.WriteValue(nullptr);
        }
        w.WriteKey("reference");
        w.BeginObject();
        w.WriteKeyValue("crs", p.referenceCrs);
        w.WriteKey("position");
        _WriteVec3(w, p.referencePosition);
        w.EndObject();
        w.EndObject();
    }
    w.EndArray();

    w.WriteKey("testCases");
    w.BeginArray();
    for (const GeospatialWebTestCase& c : data.testCases) {
        w.BeginObject();
        w.WriteKeyValue("anchor", c.anchor);
        w.WriteKeyValue("expect", c.expect);
        w.WriteKeyValue("description", c.description);
        if (!c.expectLocal.empty()) {
            w.WriteKeyValue("expectLocal", c.expectLocal);
        }
        if (!c.expectBrowser.empty()) {
            w.WriteKeyValue("expectBrowser", c.expectBrowser);
        }
        w.EndObject();
    }
    w.EndArray();
}

bool
_Record(const UsdStageRefPtr& stage, const std::string& stageName,
        const std::string& outDir, const std::string& name,
        const std::string& crs, std::vector<char>* blob,
        std::map<SdfPath, _MeshBuffers>* buffers)
{
    const auto& tokens = UsdGeospatialImagingPlacementSceneIndexTokens;
    UsdImagingCreateSceneIndicesInfo info;
    info.stage = stage;
    const UsdImagingSceneIndices sceneIndices =
        UsdImagingCreateSceneIndices(info);
    const UsdGeospatialImagingPlacementSceneIndexRefPtr placement =
        UsdGeospatialImagingPlacementSceneIndex::New(
            sceneIndices.finalSceneIndex,
            HdRetainedContainerDataSource::New(
                tokens->outputCrs,
                HdRetainedTypedSampledDataSource<std::string>::New(crs),
                tokens->measureLinearization,
                HdRetainedTypedSampledDataSource<bool>::New(true)));
    const UsdGeospatialPlacementQuery& query = placement->GetQuery();
    if (!query.IsValid()) {
        fprintf(stderr, "output %s: %s\n", name.c_str(),
                query.GetError().c_str());
        return false;
    }
    const GeospatialWebTestData testData = GeospatialWebReadTestData(stage);
    if (testData.time) {
        sceneIndices.stageSceneIndex->SetTime(UsdTimeCode(*testData.time));
    }
    UsdGeospatialImagingMeshObserver observer(placement);
    observer.Sync();

    // Buffers are shared by all outputs; the first output fills them.
    for (const auto& [path, mesh] : observer.GetMeshes()) {
        if (!buffers->count(path)) {
            _MeshBuffers& b = (*buffers)[path];
            b.points = _Append(blob, mesh.points, 3);
            b.normals = _Append(blob, mesh.normals, 3);
            b.indices = _Append(blob, mesh.indices, 1);
        }
    }

    const std::string path =
        TfStringCatPaths(outDir, "manifest-" + name + ".json");
    std::ofstream out(path);
    JsWriter w(out, JsWriter::Style::Pretty);
    w.BeginObject();
    w.WriteKeyValue("format", "geospatial-web-recorded");
    w.WriteKeyValue("version", 1);
    w.WriteKeyValue("stage", stageName);
    if (testData.time) {
        w.WriteKeyValue("time", *testData.time);
    }
    w.WriteKey("output");
    w.BeginObject();
    w.WriteKeyValue("name", name);
    w.WriteKeyValue("crs", crs);
    w.WriteKeyValue("wkt", query.GetOutput());
    w.WriteKeyValue("frame",
                    query.GetOutputInfo().isGeographic ? "enu" : "crs");
    w.EndObject();
    w.WriteKey("engine");
    w.BeginObject();
    std::string engine;
    for (const auto& [anchorPath, anchor] : observer.GetAnchors()) {
        if (!anchor.engine.empty()) {
            engine = anchor.engine;
        }
    }
    w.WriteKeyValue("name", engine);
    w.EndObject();
    w.WriteKeyValue("stageMetersPerUnit", UsdGeomGetStageMetersPerUnit(stage));
    w.WriteKeyValue("stageUpAxis", UsdGeomGetStageUpAxis(stage).GetString());
    w.WriteKeyValue("buffers", "buffers.bin");

    w.WriteKey("anchors");
    w.BeginArray();
    for (const auto& [anchorPath, anchor] : observer.GetAnchors()) {
        w.BeginObject();
        w.WriteKeyValue("path", anchorPath.GetString());
        w.WriteKeyValue("status", anchor.status.GetString());
        if (anchor.status != "ok") {
            w.WriteKeyValue("reason", anchor.reason);
            w.EndObject();
            continue;
        }
        w.WriteKeyValue("sourceCrs", anchor.sourceCrs);
        w.WriteKey("position");
        _WriteVec3(w, anchor.position);
        w.WriteKey("orientation");
        _WriteVec3(w, anchor.orientation);
        w.WriteKey("origin");
        _WriteVec3(w, anchor.origin);
        w.WriteKey("matrix");
        _WriteMatrix(w, anchor.modelToFrame);
        w.WriteKeyValue("operation", anchor.operation);
        if (!anchor.skippedOperations.empty()) {
            w.WriteKey("skippedOperations");
            w.BeginArray();
            for (const std::string& op : anchor.skippedOperations) {
                w.WriteValue(op);
            }
            w.EndArray();
        }
        w.WriteKey("accuracy");
        if (anchor.accuracy) {
            w.WriteValue(*anchor.accuracy);
        } else {
            w.WriteValue(nullptr);
        }
        w.EndObject();
    }
    w.EndArray();

    w.WriteKey("meshes");
    w.BeginArray();
    for (const auto& [meshPath, mesh] : observer.GetMeshes()) {
        w.BeginObject();
        w.WriteKeyValue("path", meshPath.GetString());
        w.WriteKey("anchor");
        if (mesh.anchor.IsEmpty()) {
            w.WriteValue(nullptr);
        } else {
            w.WriteValue(mesh.anchor.GetString());
        }
        w.WriteKeyValue("status", mesh.status.GetString());
        if (!mesh.reason.empty()) {
            w.WriteKeyValue("reason", mesh.reason);
        }
        w.WriteKey("color");
        _WriteVec3(w, GfVec3d(mesh.color));
        if (mesh.status == "ok") {
            w.WriteKey("matrix");
            _WriteMatrix(w, mesh.modelToFrame);
            w.WriteKeyValue("linearizationError",
                            mesh.linearizationError.value_or(0.0));
        }
        const _MeshBuffers& b = buffers->at(meshPath);
        _WriteView(w, "points", b.points);
        _WriteView(w, "normals", b.normals);
        _WriteView(w, "indices", b.indices);
        w.EndObject();
    }
    w.EndArray();

    _WriteTestData(w, testData);
    w.EndObject();
    out << "\n";
    printf("wrote %s (%zu anchors, %zu meshes)\n", path.c_str(),
           observer.GetAnchors().size(), observer.GetMeshes().size());
    return bool(out);
}

} // anonymous namespace

int
main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <stage> <outDir> [name=CRS ...]\n",
                argv[0]);
        return 2;
    }
    const std::string stagePath = argv[1];
    const std::string outDir = argv[2];
    std::vector<std::pair<std::string, std::string>> outputs;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        const size_t eq = arg.find('=');
        if (eq == std::string::npos) {
            fprintf(stderr, "output '%s' is not name=CRS\n", argv[i]);
            return 2;
        }
        outputs.emplace_back(arg.substr(0, eq), arg.substr(eq + 1));
    }
    if (outputs.empty()) {
        outputs = {{"global", "EPSG:4979"}, {"local-3857", "EPSG:3857"}};
    }

    const UsdStageRefPtr stage = UsdStage::Open(stagePath);
    if (!stage) {
        fprintf(stderr, "cannot open %s\n", stagePath.c_str());
        return 1;
    }
    TfMakeDirs(outDir, -1, /* existOk = */ true);

    std::vector<char> blob;
    std::map<SdfPath, _MeshBuffers> buffers;
    bool ok = true;
    for (const auto& [name, crs] : outputs) {
        ok &= _Record(stage, TfGetBaseName(stagePath), outDir, name, crs,
                      &blob, &buffers);
    }
    std::ofstream bin(TfStringCatPaths(outDir, "buffers.bin"),
                      std::ios::binary);
    bin.write(blob.data(), blob.size());
    return ok && bin ? 0 : 1;
}
