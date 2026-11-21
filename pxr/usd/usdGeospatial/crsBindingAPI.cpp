//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdGeospatial/crsBindingAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdGeospatialCRSBindingAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdGeospatialCRSBindingAPI::~UsdGeospatialCRSBindingAPI()
{
}

/* static */
UsdGeospatialCRSBindingAPI
UsdGeospatialCRSBindingAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdGeospatialCRSBindingAPI();
    }
    return UsdGeospatialCRSBindingAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdGeospatialCRSBindingAPI::_GetSchemaKind() const
{
    return UsdGeospatialCRSBindingAPI::schemaKind;
}

/* static */
bool
UsdGeospatialCRSBindingAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdGeospatialCRSBindingAPI>(whyNot);
}

/* static */
UsdGeospatialCRSBindingAPI
UsdGeospatialCRSBindingAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdGeospatialCRSBindingAPI>()) {
        return UsdGeospatialCRSBindingAPI(prim);
    }
    return UsdGeospatialCRSBindingAPI();
}

/* static */
const TfType &
UsdGeospatialCRSBindingAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdGeospatialCRSBindingAPI>();
    return tfType;
}

/* static */
bool 
UsdGeospatialCRSBindingAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdGeospatialCRSBindingAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdGeospatialCRSBindingAPI::GetPositionAttr() const
{
    return GetPrim().GetAttribute(UsdGeospatialTokens->crsPosition);
}

UsdAttribute
UsdGeospatialCRSBindingAPI::CreatePositionAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdGeospatialTokens->crsPosition,
                       SdfValueTypeNames->Double3,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdGeospatialCRSBindingAPI::GetOrientationAttr() const
{
    return GetPrim().GetAttribute(UsdGeospatialTokens->crsOrientation);
}

UsdAttribute
UsdGeospatialCRSBindingAPI::CreateOrientationAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdGeospatialTokens->crsOrientation,
                       SdfValueTypeNames->Double3,
                       /* custom = */ false,
                       SdfVariabilityVarying,
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
UsdGeospatialCRSBindingAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdGeospatialTokens->crsPosition,
        UsdGeospatialTokens->crsOrientation,
    };
    static TfTokenVector allNames =
        _ConcatenateAttributeNames(
            UsdAPISchemaBase::GetSchemaAttributeNames(true),
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
