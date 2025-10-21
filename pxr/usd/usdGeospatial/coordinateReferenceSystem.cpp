//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdGeospatial/coordinateReferenceSystem.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdGeospatialCoordinateReferenceSystem,
        TfType::Bases< UsdTyped > >();
    
    // Register the usd prim typename as an alias under UsdSchemaBase. This
    // enables one to call
    // TfType::Find<UsdSchemaBase>().FindDerivedByName("CoordinateReferenceSystem")
    // to find TfType<UsdGeospatialCoordinateReferenceSystem>, which is how IsA queries are
    // answered.
    TfType::AddAlias<UsdSchemaBase, UsdGeospatialCoordinateReferenceSystem>("CoordinateReferenceSystem");
}

/* virtual */
UsdGeospatialCoordinateReferenceSystem::~UsdGeospatialCoordinateReferenceSystem()
{
}

/* static */
UsdGeospatialCoordinateReferenceSystem
UsdGeospatialCoordinateReferenceSystem::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdGeospatialCoordinateReferenceSystem();
    }
    return UsdGeospatialCoordinateReferenceSystem(stage->GetPrimAtPath(path));
}

/* static */
UsdGeospatialCoordinateReferenceSystem
UsdGeospatialCoordinateReferenceSystem::Define(
    const UsdStagePtr &stage, const SdfPath &path)
{
    static TfToken usdPrimTypeName("CoordinateReferenceSystem");
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdGeospatialCoordinateReferenceSystem();
    }
    return UsdGeospatialCoordinateReferenceSystem(
        stage->DefinePrim(path, usdPrimTypeName));
}

/* virtual */
UsdSchemaKind UsdGeospatialCoordinateReferenceSystem::_GetSchemaKind() const
{
    return UsdGeospatialCoordinateReferenceSystem::schemaKind;
}

/* static */
const TfType &
UsdGeospatialCoordinateReferenceSystem::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdGeospatialCoordinateReferenceSystem>();
    return tfType;
}

/* static */
bool 
UsdGeospatialCoordinateReferenceSystem::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdGeospatialCoordinateReferenceSystem::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdGeospatialCoordinateReferenceSystem::GetWellKnownTextAttr() const
{
    return GetPrim().GetAttribute(UsdGeospatialTokens->crsWkt);
}

UsdAttribute
UsdGeospatialCoordinateReferenceSystem::CreateWellKnownTextAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdGeospatialTokens->crsWkt,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

namespace {
static inline TfTokenVector
_ConcatenateAttributeNames(const TfTokenVector& left,const TfTokenVector& right)
{
    TfTokenVector result;
    result.reserve(left.size() + right.size());
    result.insert(result.end(), left.begin(), left.end());
    result.insert(result.end(), right.begin(), right.end());
    return result;
}
}

/*static*/
const TfTokenVector&
UsdGeospatialCoordinateReferenceSystem::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdGeospatialTokens->crsWkt,
    };
    static TfTokenVector allNames =
        _ConcatenateAttributeNames(
            UsdTyped::GetSchemaAttributeNames(true),
            localNames);

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

PXR_NAMESPACE_OPEN_SCOPE

//std::string ComputeCRS(UsdPrim const& prim) {
//    for (SdfPath const& path: prim.GetPath().GetAncestorsRange()) {
//        UsdPrim p = prim.GetPrimAtPath(path);
//        if (UsdAttribute crsAttr = p.GetAttribute(UsdGeospatialTokens->crsWkt); crsAttr) {
//            std::string wkt;
//            if (crsAttr.Get(&wkt))
//                return wkt;
//        }
//    }
//    return {};
//}


TfToken
UsdGeospatialCoordinateReferenceSystem::ComputeCoordinateReferenceSystem(UsdPrim const& prim) {
    for (SdfPath const& path: prim.GetPath().GetAncestorsRange()) {
        UsdPrim p = prim.GetPrimAtPath(path);
        if (UsdAttribute crsAttr = p.GetAttribute(UsdGeospatialTokens->crsWkt); crsAttr) {
            if (TfToken wkt; crsAttr.Get(&wkt)) {
                return wkt;
            }
        }
    }
    return {};
}

PXR_NAMESPACE_CLOSE_SCOPE
