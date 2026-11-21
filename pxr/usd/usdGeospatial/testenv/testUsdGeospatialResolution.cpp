//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// Binding resolution and placement validation; no engine needed.

#include "pxr/pxr.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeospatial/binding.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/stringUtils.h"

#include <cstdio>

PXR_NAMESPACE_USING_DIRECTIVE

static bool
_Contains(const std::vector<std::string>& problems, const char* text)
{
    for (const std::string& p : problems) {
        if (TfStringContains(p, text)) {
            return true;
        }
    }
    return false;
}

int
main()
{
    UsdStageRefPtr stage = UsdStage::Open("bindings.usda");
    TF_AXIOM(stage);
    auto prim = [&](const char* path) {
        UsdPrim p = stage->GetPrimAtPath(SdfPath(path));
        TF_AXIOM(p);
        return p;
    };

    // Direct binding.
    UsdGeospatialBinding b = UsdGeospatialResolveBinding(prim("/World/Site"));
    TF_AXIOM(b && b.isDirect);
    TF_AXIOM(b.anchor.GetPath() == SdfPath("/World/Site"));
    TF_AXIOM(b.wkt == "PROJCRS[\"test\"]");

    // Inherited from the nearest ancestor.
    b = UsdGeospatialResolveBinding(prim("/World/Site/Building/Room"));
    TF_AXIOM(b && !b.isDirect);
    TF_AXIOM(b.anchor.GetPath() == SdfPath("/World/Site"));

    // A nested direct binding is a new anchor.
    b = UsdGeospatialResolveBinding(prim("/World/Site/Nested/Child"));
    TF_AXIOM(b && b.anchor.GetPath() == SdfPath("/World/Site/Nested"));

    // A broken direct binding is an error, with no fallback to /World/Site,
    // also for its descendants.
    for (const char* path : {"/World/Site/Broken", "/World/Site/Broken/Child"}) {
        b = UsdGeospatialResolveBinding(prim(path));
        TF_AXIOM(!b);
        TF_AXIOM(b.anchor.GetPath() == SdfPath("/World/Site/Broken"));
        TF_AXIOM(b.wkt.IsEmpty());
        TF_AXIOM(TfStringContains(b.error, "no fallback"));
    }

    // No binding at all.
    b = UsdGeospatialResolveBinding(prim("/World/Unbound"));
    TF_AXIOM(!b && !b.anchor && b.error.empty());

    // Validation.
    TF_AXIOM(UsdGeospatialValidatePlacement(prim("/World/Site")).empty());
    TF_AXIOM(UsdGeospatialValidatePlacement(prim("/World/Site/Building")).empty());
    TF_AXIOM(_Contains(UsdGeospatialValidatePlacement(
        prim("/World/Site/Broken")), "no crs:wkt"));
    TF_AXIOM(_Contains(UsdGeospatialValidatePlacement(
        prim("/World/Site/MisplacedPosition")), "not directly bound"));
    TF_AXIOM(_Contains(UsdGeospatialValidatePlacement(
        prim("/World/Site/NoPosition")), "no crs:position"));
    TF_AXIOM(_Contains(UsdGeospatialValidatePlacement(
        prim("/World/Site/BlockedPosition")), "blocked"));
    TF_AXIOM(_Contains(UsdGeospatialValidatePlacement(
        prim("/World/Site/NanPosition")), "not finite"));

    printf("OK\n");
    return 0;
}
