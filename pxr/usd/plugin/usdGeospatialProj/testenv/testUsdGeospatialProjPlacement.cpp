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

#include <algorithm>
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
    // WGS 84 with EGM96 heights: a compound output with a geographic
    // horizontal part, placed in an east/north/up frame like EPSG:4979.
    const UsdGeospatialPlacementQuery gravity("EPSG:4326+5773");
    TF_AXIOM(geographic.IsValid() && geocentric.IsValid() && gravity.IsValid());
    TF_AXIOM(geographic.GetOutputInfo().isGeographic);
    TF_AXIOM(gravity.GetOutputInfo().isGeographic);
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

        // Builds without a grid the expected result used (e.g. WebAssembly)
        // place it differently or not at all, and say which grids are
        // missing: not comparable there.
        bool usedGridMissing = false;
        if (a.count("gridsUsed")) {
            for (const JsValue& g : a.at("gridsUsed").GetJsArray()) {
                const std::vector<std::string>& missing = geo.GetMissingGrids();
                usedGridMissing |= std::find(missing.begin(), missing.end(),
                                             g.GetString()) != missing.end();
            }
        }
        if (status == "ok" && usedGridMissing) {
            printf("  not comparable in this build, grids missing: %s\n",
                   TfStringJoin(geo.GetMissingGrids(), ", ").c_str());
            continue;
        }

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

        // Origin in EPSG:4326+5773 (EGM96 heights), in the ENU frame. Builds
        // without the geoid grid (WebAssembly tests) cannot compare it.
        if (a.count("originEgm96")) {
            const UsdGeospatialPlacement g = gravity.Resolve(anchor);
            bool gridMissing = false;
            for (const JsValue& grid : a.at("gridsUsedEgm96").GetJsArray()) {
                const std::vector<std::string>& m = g.GetMissingGrids();
                gridMissing |= std::find(m.begin(), m.end(),
                                         grid.GetString()) != m.end();
            }
            if (gridMissing) {
                printf("  EGM96 output not comparable in this build, grids "
                       "missing: %s\n",
                       TfStringJoin(g.GetMissingGrids(), ", ").c_str());
            } else if (!g.IsOk() || !g.IsLocalFrame()) {
                printf("  FAIL: EGM96 output: %s\n", g.IsOk()
                       ? "not an east/north/up frame" : g.GetReason().c_str());
                ++failures;
            } else {
                const GfVec3d expected = _Vec(a.at("originEgm96"));
                const GfVec3d e = g.GetOrigin() - expected;
                const double error = std::hypot(
                    e[0] * metresPerDegree * std::cos(expected[1] * M_PI / 180.0),
                    e[1] * metresPerDegree, e[2]);
                printf("  EGM96 origin error %.6f m (height %.3f m, "
                       "ellipsoidal %.3f m)\n", error, g.GetOrigin()[2],
                       geo.GetOrigin()[2]);
                if (error > tolerance) {
                    ++failures;
                }
            }
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

    // Projection factors against pyproj on the same PROJ: UTM 32N far west
    // of its central meridian (a 3D object layer in Bergen) and California
    // zone 5 in US survey feet; compound and non-projected CRSs.
    const UsdGeospatialCoordinateOperationEngineSharedPtr engine =
        UsdGeospatialEngineRegistry::GetInstance().GetEngine();
    struct FactorsCase {
        const char* crs;
        GfVec3d position;
        double convergence, meridional, parallel;
    };
    const FactorsCase factorsCases[] = {
        {"EPSG:25832", GfVec3d(298549.51254411, 6709780.34498452, 0),
         -3.1904926749780755, 1.000097310931324, 1.0000973108945979},
        {"EPSG:25832+5773", GfVec3d(298549.51254411, 6709780.34498452, 0),
         -3.1904926749780755, 1.000097310931324, 1.0000973108945979},
        {"EPSG:2229", GfVec3d(6400000.0, 1900000.0, 0),
         -0.3048037470719006, 0.9999660692149703, 0.9999660692110507},
    };
    for (const FactorsCase& c : factorsCases) {
        const UsdGeospatialProjectionFactors f =
            engine->ComputeProjectionFactors(c.crs, c.position);
        printf("factors %s: %s convergence %.10f, scales %.12f %.12f\n",
               c.crs, f ? "ok" : f.error.c_str(), f.meridianConvergence,
               f.meridionalScale, f.parallelScale);
        if (!f || std::abs(f.meridianConvergence - c.convergence) > 1e-9 ||
            std::abs(f.meridionalScale - c.meridional) > 1e-12 ||
            std::abs(f.parallelScale - c.parallel) > 1e-12) {
            ++failures;
        }
    }
    for (const char* crs : {"EPSG:4326", "EPSG:4979", "EPSG:4326+5773"}) {
        const UsdGeospatialProjectionFactors f =
            engine->ComputeProjectionFactors(crs, GfVec3d(8.5, 47.4, 0));
        printf("factors %s: %s\n", crs, f ? "ok" : f.error.c_str());
        if (f) {
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
