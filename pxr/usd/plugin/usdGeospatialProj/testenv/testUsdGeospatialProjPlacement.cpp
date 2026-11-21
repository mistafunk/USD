//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// Placement with the PROJ engine against an independent reference: the
// offline Python reference (usd-core + pyproj) wrote expected.json for the
// anchors in placement.usda and eiffelTower.usda (see
// third_party/geospatial-web/reference, "proj-expected"). Placeable anchors
// must match within its tolerance (1 mm); the others must be reported as
// unsupported, with a reason.

#include "pxr/pxr.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeospatial/engineRegistry.h"
#include "pxr/usd/usdGeospatial/placement.h"
#include "pxr/base/js/json.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/stringUtils.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

PXR_NAMESPACE_USING_DIRECTIVE

static GfVec3d
_Vec(const JsValue& v)
{
    const JsArray& a = v.GetJsArray();
    return GfVec3d(a[0].GetReal(), a[1].GetReal(), a[2].GetReal());
}

int
main()
{
    std::ifstream file("expected.json");
    std::stringstream text;
    text << file.rdbuf();
    JsParseError parseError;
    const JsValue expected = JsParseString(text.str(), &parseError);
    TF_AXIOM(expected.IsObject());
    const JsObject& root = expected.GetJsObject();
    const double tolerance = root.at("tolerance").GetReal();

    TF_AXIOM(UsdGeospatialEngineRegistry::GetInstance().GetActiveEngineName()
             == "proj");
    const UsdGeospatialPlacementQuery geographic("EPSG:4979");
    const UsdGeospatialPlacementQuery geocentric("EPSG:4978");
    TF_AXIOM(geographic.IsValid() && geocentric.IsValid());
    TF_AXIOM(geographic.GetOutputInfo().isGeographic);
    TF_AXIOM(geocentric.GetOutputInfo().isGeocentric);

    int failures = 0;
    for (const JsValue& entry : root.at("anchors").GetJsArray()) {
        const JsObject& a = entry.GetJsObject();
        const std::string path = a.at("path").GetString();
        const std::string status = a.at("status").GetString();
        UsdStageRefPtr stage = UsdStage::Open(a.at("stage").GetString());
        TF_AXIOM(stage);
        const UsdPrim anchor = stage->GetPrimAtPath(SdfPath(path));
        TF_AXIOM(anchor);

        const UsdGeospatialPlacement geo = geographic.Resolve(anchor);
        const UsdGeospatialPlacement ecef = geocentric.Resolve(anchor);
        printf("%-22s expected %-11s got %s%s%s\n", path.c_str(),
               status.c_str(), geo.IsOk() ? "ok: " : "not ok: ",
               geo.IsOk() ? geo.GetOperation().c_str()
                          : geo.GetReason().c_str(),
               geo.IsOk() ? TfStringPrintf(" (accuracy %g m)",
                                           geo.GetAccuracy()).c_str() : "");
        TF_AXIOM(geo.GetEngine() == "proj");

        if (status != "ok") {
            // Reported, not drawn: the engine must say why.
            if (geo.GetStatus() != UsdGeospatialPlacement::Status::Unsupported
                || geo.GetReason().empty()) {
                printf("  FAIL: expected unsupported\n");
                ++failures;
            }
            continue;
        }
        if (!geo.IsOk() || !ecef.IsOk()) {
            printf("  FAIL: %s\n", ecef.GetReason().c_str());
            ++failures;
            continue;
        }

        // Origin in EPSG:4979: lon, lat in degrees, height in metres.
        const GfVec3d origin = _Vec(a.at("origin4979"));
        const double metresPerDegree = 111320.0;
        const GfVec3d d = geo.GetOrigin() - origin;
        const double originError = std::hypot(
            d[0] * metresPerDegree * std::cos(origin[1] * M_PI / 180.0),
            d[1] * metresPerDegree, d[2]);
        printf("  origin error %.6f m\n", originError);
        if (originError > tolerance) {
            ++failures;
        }

        // Exact model points in EPSG:4978 (geocentric metres).
        std::vector<GfVec3d> model, expectedPoints, got;
        for (const JsValue& p : a.at("points").GetJsArray()) {
            model.push_back(_Vec(p));
        }
        for (const JsValue& p : a.at("points4978").GetJsArray()) {
            expectedPoints.push_back(_Vec(p));
        }
        std::string error;
        TF_AXIOM(ecef.ComputeOutputPoints(model, &got, &error));
        double worst = 0.0;
        for (size_t i = 0; i < got.size(); ++i) {
            worst = std::max(worst, (got[i] - expectedPoints[i]).GetLength());
        }
        printf("  points error %.6f m over %zu points\n", worst, got.size());
        if (worst > tolerance) {
            ++failures;
        }

        // The drawing matrix (ENU metres) against the exact map.
        std::vector<GfVec3d> frame;
        TF_AXIOM(geo.ComputeFramePoints(model, &frame, &error));
        double linearization = 0.0;
        for (size_t i = 0; i < model.size(); ++i) {
            const GfVec3d m = geo.GetModelToFrame().TransformDir(model[i]);
            linearization = std::max(linearization, (m - frame[i]).GetLength());
        }
        printf("  linearization error %.6f m\n", linearization);
        if (linearization > tolerance) {
            ++failures;
        }
    }

    if (failures) {
        printf("FAILED: %d check(s)\n", failures);
        return 1;
    }
    printf("OK\n");
    return 0;
}
