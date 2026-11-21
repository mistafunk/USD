//
// Test data on a stage (reference/README.md): control points (prims with
// geotest:referencePosition, geotest:referenceCrs), test cases (anchors with
// geotest:expect, geotest:description and optionally geotest:expectLocal, the
// outcome in a local view with matrix placement) and the evaluation time
// (root layer customLayerData geotest:time). Not proposal schema; read by
// geospatialRecord and geospatialModule like the offline reference reads it.
//
#ifndef GEOSPATIAL_WEB_TEST_DATA_H
#define GEOSPATIAL_WEB_TEST_DATA_H

#include "pxr/pxr.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/xformCache.h"
#include "pxr/usd/usdGeospatial/binding.h"
#include "pxr/usd/usdGeospatial/crsBindingAPI.h"
#include "pxr/base/vt/dictionary.h"

#include <optional>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

struct GeospatialWebControlPoint
{
    std::string path;
    /// Empty without a CRS binding.
    std::string anchor;
    /// The point in its anchor's model frame (stage units).
    std::optional<GfVec3d> local;
    std::string referenceCrs;
    GfVec3d referencePosition;
};

struct GeospatialWebTestCase
{
    std::string anchor;
    std::string expect;
    std::string description;
    /// Outcome in a local view with matrix placement, if it differs.
    std::string expectLocal;
};

struct GeospatialWebTestData
{
    std::vector<GeospatialWebControlPoint> controlPoints;
    std::vector<GeospatialWebTestCase> testCases;
    /// The time the scene is evaluated at, if it says.
    std::optional<double> time;
};

namespace GeospatialWeb_Internal {

// A string or token attribute's value; empty if it has none.
inline std::string
GetString(const UsdPrim& prim, const char* name)
{
    VtValue value;
    prim.GetAttribute(TfToken(name)).Get(&value);
    if (value.IsHolding<TfToken>()) {
        return value.UncheckedGet<TfToken>().GetString();
    }
    return value.IsHolding<std::string>()
        ? value.UncheckedGet<std::string>() : std::string();
}

} // namespace GeospatialWeb_Internal

/// Reads the test data on \p stage. Control points are placed in their
/// anchor's frame at the evaluation time.
inline GeospatialWebTestData
GeospatialWebReadTestData(const UsdStageRefPtr& stage)
{
    using GeospatialWeb_Internal::GetString;
    GeospatialWebTestData data;

    const VtDictionary layerData = stage->GetRootLayer()->GetCustomLayerData();
    const auto it = layerData.find("geotest:time");
    if (it != layerData.end() && it->second.CanCast<double>()) {
        data.time = VtValue::Cast<double>(it->second).UncheckedGet<double>();
    }
    const UsdTimeCode time =
        data.time ? UsdTimeCode(*data.time) : UsdTimeCode::Default();

    UsdGeomXformCache cache(time);
    for (const UsdPrim& prim : stage->Traverse()) {
        const UsdAttribute position =
            prim.GetAttribute(TfToken("geotest:referencePosition"));
        GfVec3d reference;
        if (!position || !position.Get(&reference)) {
            continue;
        }
        GeospatialWebControlPoint point;
        point.path = prim.GetPath().GetString();
        point.referencePosition = reference;
        point.referenceCrs = GetString(prim, "geotest:referenceCrs");
        if (point.referenceCrs.empty()) {
            point.referenceCrs = "EPSG:4979";
        }
        if (const UsdPrim anchor = UsdGeospatialResolveBinding(prim).anchor) {
            bool resets = false;
            point.anchor = anchor.GetPath().GetString();
            point.local = cache.ComputeRelativeTransform(prim, anchor, &resets)
                .ExtractTranslation();
        }
        data.controlPoints.push_back(point);
    }

    for (const UsdPrim& prim : stage->Traverse()) {
        const std::string expect = GetString(prim, "geotest:expect");
        if (!prim.HasAPI<UsdGeospatialCRSBindingAPI>() || expect.empty()) {
            continue;
        }
        data.testCases.push_back({prim.GetPath().GetString(), expect,
                                  GetString(prim, "geotest:description"),
                                  GetString(prim, "geotest:expectLocal")});
    }
    return data;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
