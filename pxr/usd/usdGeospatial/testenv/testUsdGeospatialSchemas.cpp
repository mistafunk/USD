//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// C++ counterpart of testUsdGeospatialSchemas.py, so that builds without
// Python (WASM) also check schema registration and the proposal examples.

#include "pxr/pxr.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeospatial/crs.h"
#include "pxr/usd/usdGeospatial/crsBindingAPI.h"
#include "pxr/usd/usdGeospatial/tokens.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/stringUtils.h"

PXR_NAMESPACE_USING_DIRECTIVE

static void
TestRegistration()
{
    const UsdSchemaRegistry &reg = UsdSchemaRegistry::GetInstance();
    TF_AXIOM(reg.FindConcretePrimDefinition(TfToken("GeospatialCRS")));
    TF_AXIOM(reg.FindAppliedAPIPrimDefinition(
        TfToken("GeospatialCRSBindingAPI")));
    TF_AXIOM(UsdSchemaRegistry::GetSchemaTypeName<UsdGeospatialCRS>() ==
             "GeospatialCRS");
    TF_AXIOM(UsdGeospatialTokens->crsWkt == "crs:wkt");
}

static void
TestEiffelTower()
{
    UsdStageRefPtr stage = UsdStage::Open("eiffelTower.usda");
    TF_AXIOM(stage);
    UsdPrim anchor = stage->GetPrimAtPath(SdfPath("/EiffelTower"));
    TF_AXIOM(anchor.IsA<UsdGeomXform>());
    TF_AXIOM(anchor.HasAPI<UsdGeospatialCRSBindingAPI>());

    UsdGeospatialCRSBindingAPI api(anchor);
    GfVec3d position, orientation;
    TF_AXIOM(api.GetPositionAttr().Get(&position));
    TF_AXIOM(api.GetOrientationAttr().Get(&orientation));
    TF_AXIOM(position == GfVec3d(0, 0, 0));
    TF_AXIOM(orientation == GfVec3d(30, 0, 0));

    TfToken wkt;
    TF_AXIOM(anchor.GetAttribute(UsdGeospatialTokens->crsWkt).Get(&wkt));
    TF_AXIOM(TfStringStartsWith(
        wkt.GetString(), "GEODCRS[\"Local Tangent Plane at Eiffel Tower\""));
}

static void
TestUtm11n()
{
    UsdStageRefPtr stage = UsdStage::Open("utm11n.usda");
    TF_AXIOM(stage);
    UsdPrim site = stage->GetPrimAtPath(SdfPath("/Site"));
    TF_AXIOM(site.HasAPI<UsdGeospatialCRSBindingAPI>());
    TfToken wkt;
    TF_AXIOM(site.GetAttribute(UsdGeospatialTokens->crsWkt).Get(&wkt));
    TF_AXIOM(TfStringContains(wkt.GetString(), "ID[\"EPSG\",32611]"));

    UsdStageRefPtr libraryStage = UsdStage::Open("crsLibrary.usda");
    TF_AXIOM(libraryStage);
    UsdPrim library =
        libraryStage->GetPrimAtPath(SdfPath("/CRS/WGS84_UTM11N"));
    TF_AXIOM(library.IsA<UsdGeospatialCRS>());
    TfToken libraryWkt;
    TF_AXIOM(UsdGeospatialCRS(library).GetWktAttr().Get(&libraryWkt));
    TF_AXIOM(wkt == libraryWkt);

    UsdPrim broken = stage->GetPrimAtPath(SdfPath("/Site/Broken"));
    TF_AXIOM(broken.HasAPI<UsdGeospatialCRSBindingAPI>());
    TF_AXIOM(!broken.GetAttribute(UsdGeospatialTokens->crsWkt));
}

int
main()
{
    TestRegistration();
    TestEiffelTower();
    TestUtm11n();
    printf("OK\n");
    return 0;
}
