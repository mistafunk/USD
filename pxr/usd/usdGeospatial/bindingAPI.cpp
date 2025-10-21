//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"
#include "pxr/usd/usdGeospatial/bindingAPI.h"

#include "proj.h"

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdGeospatialBindingAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdGeospatialBindingAPI::~UsdGeospatialBindingAPI()
{
}

/* static */
UsdGeospatialBindingAPI
UsdGeospatialBindingAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdGeospatialBindingAPI();
    }
    return UsdGeospatialBindingAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdGeospatialBindingAPI::_GetSchemaKind() const
{
    return UsdGeospatialBindingAPI::schemaKind;
}

/* static */
bool
UsdGeospatialBindingAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdGeospatialBindingAPI>(whyNot);
}

/* static */
UsdGeospatialBindingAPI
UsdGeospatialBindingAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdGeospatialBindingAPI>()) {
        return UsdGeospatialBindingAPI(prim);
    }
    return UsdGeospatialBindingAPI();
}

/* static */
const TfType &
UsdGeospatialBindingAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdGeospatialBindingAPI>();
    return tfType;
}

/* static */
bool
UsdGeospatialBindingAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdGeospatialBindingAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

/*static*/
const TfTokenVector&
UsdGeospatialBindingAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames;
    static TfTokenVector allNames =
        UsdAPISchemaBase::GetSchemaAttributeNames(true);

    if (includeInherited)
        return allNames;
    else
        return localNames;
}

PXR_NAMESPACE_CLOSE_SCOPE

// ===================================================================== //
// Feel free to add custom code below this line. It will be preserved by
// the code generator.
//
// Just remember to wrap code in the appropriate delimiters:
// 'PXR_NAMESPACE_OPEN_SCOPE', 'PXR_NAMESPACE_CLOSE_SCOPE'.
// ===================================================================== //
// --(BEGIN CUSTOM CODE)--

namespace {

bool resetXform(pxr::UsdPrim const& prim) {
    pxr::UsdGeomXform xform(prim);
    if (xform)
        return xform.SetResetXformStack(true);
    return false;
}

} // namespace

PXR_NAMESPACE_OPEN_SCOPE

bool
UsdGeospatialBindingAPI::Bind(UsdGeospatialCoordinateReferenceSystem const& crs) const {
    bool result = true;
    result &= resetXform(GetPrim());
    result &= GetPrim().GetReferences().AddInternalReference((crs.GetPath()));
    return result;
}

bool
UsdGeospatialBindingAPI::Bind(std::string const& layerPath, SdfPath const& crsPrimPath) const {
    bool result = true;
    result &= resetXform(GetPrim());
    result &= GetPrim().GetReferences().AddReference(layerPath, crsPrimPath);
    return result;
}

SdfLayerRefPtr
UsdGeospatialBindingAPI::ReprojectToLayer() const {
    auto layer = SdfLayer::CreateNew("reprojected.usda");

    UsdPrim defaultPrim = GetPrim().GetStage()->GetDefaultPrim();
    TfToken targetWkt = UsdGeospatialCoordinateReferenceSystem::ComputeCoordinateReferenceSystem(defaultPrim);

    PJ_CONTEXT *C = proj_context_create();

    UsdPrimRange range(GetPrim());
    for (UsdPrim const& prim: range) {
        std::cout << prim.GetPath().GetString() << "\n";
        if (prim.IsA<UsdGeomXformable>()) {
            UsdGeomXformable xformable(prim);

            GfMatrix4d toWorld = xformable.ComputeLocalToWorldTransform(UsdTimeCode::Default());
            GfVec3d t = toWorld.GetRow3(3);

            TfToken sourceWkt = UsdGeospatialCoordinateReferenceSystem::ComputeCoordinateReferenceSystem(prim);

            PJ* P = proj_create_crs_to_crs(C, sourceWkt.GetText(), targetWkt.GetText(), NULL);
            PJ* P_normalized = proj_normalize_for_visualization(C, P);
            proj_destroy(P);
            P = P_normalized;

            PJ_COORD input, output;
            input = proj_coord(t[0], t[1], t[2], 0);  // x, y, z, t
            output = proj_trans(P, PJ_FWD, input);
            std::cout << "Transformed coordinates: " << output.xy.x << ", " << output.xy.y << std::endl;

            proj_destroy(P);
        }
    }

    proj_context_destroy(C);

    return layer;
}

PXR_NAMESPACE_CLOSE_SCOPE
