//
// Copyright 2017 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/pxr.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeospatial/coordinateReferenceSystem.h"
#include "pxr/usd/usdGeospatial/bindingAPI.h"
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

void TestCoordinateReferenceSystem() {
    UsdStageRefPtr stage = UsdStage::Open("./crs.usda");
    TF_VERIFY(stage, "Unable to open crs.usda");

    auto crs = UsdGeospatialCoordinateReferenceSystem::Get(stage, SdfPath("/CRS/UTM17N"));
    TfToken wkt;
    TF_VERIFY(crs.GetWellKnownTextAttr().Get(&wkt), "Could not get the wkt attr");
    TF_VERIFY(wkt.GetString().find("PROJCS[\"NAD83 / UTM zone 17N\"") == 0, "Wrong content");

    UsdPrim crsPrim = crs.GetPrim();
    TF_VERIFY(crsPrim.IsA<UsdGeospatialCoordinateReferenceSystem>(),
              "IsA<UsdGeospatialCoordinateReferenceSystem> failed");
}

void TestHasCRS() {
    UsdStageRefPtr stage = UsdStage::Open("./world.usda");
    TF_VERIFY(stage, "Unable to open world.usda");

    auto xformWorld = UsdGeomXform::Get(stage, SdfPath("/World"));
    TF_VERIFY(xformWorld, "unable to get the world xform");

    UsdPrim xformWorldPrim = xformWorld.GetPrim();
    TF_VERIFY(xformWorldPrim.HasAPI<UsdGeospatialBindingAPI>(), "missing binding API");

    UsdReferences ref = xformWorldPrim.GetReferences();
    TF_VERIFY(ref, "invalid reference");

    UsdAttribute wktAttrWorld = xformWorld.GetPrim().GetAttribute(UsdGeospatialTokens->crsWkt);
    TF_VERIFY(wktAttrWorld, "invalid wkt attr");

    TfToken wkt;
    TF_VERIFY(wktAttrWorld.Get(&wkt));
    TF_VERIFY(wkt.GetString().find("PROJCS[\"WGS 84 / UTM zone 30N\"") == 0, "Wrong content");

    auto meshMoMaBuilding = UsdGeomMesh::Get(stage, SdfPath("/World/NewYork/MoMa/MoMaBuilding"));
    TF_VERIFY(meshMoMaBuilding, "invalid mesh");

    wkt = UsdGeospatialCoordinateReferenceSystem::ComputeCoordinateReferenceSystem(meshMoMaBuilding.GetPrim());
    TF_VERIFY(wkt.GetString().find("PROJCS[\"NAD83 / UTM zone 17N\"") == 0, "Wrong content");

    std::string stageStr;
    stage->ExportToString(&stageStr);
    std::cout << stageStr;
}

void TestAuthorCoordinateReferenceSystem() {
    auto stage = UsdStage::CreateInMemory("TestCRSAuthoring.usd");

    auto path = SdfPath("/crs/MyCRS");
    auto crs = UsdGeospatialCoordinateReferenceSystem::Define(stage, path);
    TF_VERIFY(crs.CreateWellKnownTextAttr().Set(VtValue(TfToken("foobar"))), "failed to set wkt attr");

    auto prim = stage->GetPrimAtPath(path);
    TF_VERIFY(prim.IsA<UsdGeospatialCoordinateReferenceSystem>(), "invalid TypedSchema");

    std::string stageStr;
    stage->ExportToString(&stageStr);
    std::cout << stageStr;
}

void TestAuthorCRSBinding() {
    auto stage = UsdStage::CreateInMemory("TestCRSBinding.usd");

    auto crsPath = SdfPath("/MyCRS");
    auto crs = UsdGeospatialCoordinateReferenceSystem::Define(stage, crsPath);
    TF_VERIFY(crs.CreateWellKnownTextAttr().Set(VtValue(TfToken("foobar"))), "failed to set wkt attr");

    auto xformPath = SdfPath("/MyPrim");
    auto xform = UsdGeomXform::Define(stage, xformPath);
    UsdPrim xformPrim = xform.GetPrim();

    TF_VERIFY(UsdGeospatialBindingAPI::CanApply(xformPrim), "CanApply failed");
    UsdGeospatialBindingAPI api = UsdGeospatialBindingAPI::Apply(xformPrim);
    TF_VERIFY(api.Bind(crs), "failed to add crs reference");

    TF_VERIFY(xformPrim.HasAPI<UsdGeospatialBindingAPI>(), "missing binding API");

    std::string stageStr;
    stage->ExportToString(&stageStr);
    std::cout << stageStr;

    TF_VERIFY(xformPrim.HasAttribute(UsdGeospatialTokens->crsWkt));

    auto wkt = UsdGeospatialCoordinateReferenceSystem::ComputeCoordinateReferenceSystem(xformPrim);
    TF_VERIFY(wkt == "foobar", "Wrong content");
}

void TestReprojection() {
    UsdStageRefPtr stage = UsdStage::Open("./world.usda");
    auto meshMoMaBuilding = UsdGeomMesh::Get(stage, SdfPath("/World/NewYork/MoMa/MoMaBuilding"));
    auto api = UsdGeospatialBindingAPI::Apply(meshMoMaBuilding.GetPrim());
    SdfLayerRefPtr layer = api.ReprojectToLayer();
    std::string layerStr;
    layer->ExportToString(&layerStr);
    std::cout << layerStr;
}

int main() {
    TestCoordinateReferenceSystem();
    TestHasCRS();
    TestAuthorCoordinateReferenceSystem();
    TestAuthorCRSBinding();
    TestReprojection();
}
