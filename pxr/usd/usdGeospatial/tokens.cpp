//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdGeospatial/tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

UsdGeospatialTokensType::UsdGeospatialTokensType() :
    crsBinding("crs:binding", TfToken::Immortal),
    crsWkt("crs:wkt", TfToken::Immortal),
    BindingAPI("BindingAPI", TfToken::Immortal),
    CoordinateReferenceSystem("CoordinateReferenceSystem", TfToken::Immortal),
    allTokens({
        crsBinding,
        crsWkt,
        BindingAPI,
        CoordinateReferenceSystem
    })
{
}

TfStaticData<UsdGeospatialTokensType> UsdGeospatialTokens;

PXR_NAMESPACE_CLOSE_SCOPE
