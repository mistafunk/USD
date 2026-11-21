//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdGeospatial/crs.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdGeospatialCRS,
        TfType::Bases< UsdTyped > >();
    
    // Register the usd prim typename as an alias under UsdSchemaBase. This
    // enables one to call
    // TfType::Find<UsdSchemaBase>().FindDerivedByName("GeospatialCRS")
    // to find TfType<UsdGeospatialCRS>, which is how IsA queries are
    // answered.
    TfType::AddAlias<UsdSchemaBase, UsdGeospatialCRS>("GeospatialCRS");
}

/* virtual */
UsdGeospatialCRS::~UsdGeospatialCRS()
{
}

/* static */
UsdGeospatialCRS
UsdGeospatialCRS::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdGeospatialCRS();
    }
    return UsdGeospatialCRS(stage->GetPrimAtPath(path));
}

/* static */
UsdGeospatialCRS
UsdGeospatialCRS::Define(
    const UsdStagePtr &stage, const SdfPath &path)
{
    static TfToken usdPrimTypeName("GeospatialCRS");
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdGeospatialCRS();
    }
    return UsdGeospatialCRS(
        stage->DefinePrim(path, usdPrimTypeName));
}

/* virtual */
UsdSchemaKind UsdGeospatialCRS::_GetSchemaKind() const
{
    return UsdGeospatialCRS::schemaKind;
}

/* static */
const TfType &
UsdGeospatialCRS::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdGeospatialCRS>();
    return tfType;
}

/* static */
bool 
UsdGeospatialCRS::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdGeospatialCRS::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdGeospatialCRS::GetWktAttr() const
{
    return GetPrim().GetAttribute(UsdGeospatialTokens->crsWkt);
}

UsdAttribute
UsdGeospatialCRS::CreateWktAttr(VtValue const &defaultValue, bool writeSparsely) const
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
UsdGeospatialCRS::GetSchemaAttributeNames(bool includeInherited)
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
