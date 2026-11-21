//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdGeospatial/tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

UsdGeospatialTokensType::UsdGeospatialTokensType() :
    crsOrientation("crs:orientation", TfToken::Immortal),
    crsPosition("crs:position", TfToken::Immortal),
    crsWkt("crs:wkt", TfToken::Immortal),
    GeospatialCRS("GeospatialCRS", TfToken::Immortal),
    GeospatialCRSBindingAPI("GeospatialCRSBindingAPI", TfToken::Immortal),
    allTokens({
        crsOrientation,
        crsPosition,
        crsWkt,
        GeospatialCRS,
        GeospatialCRSBindingAPI
    })
{
}

TfStaticData<UsdGeospatialTokensType> UsdGeospatialTokens;

PXR_NAMESPACE_CLOSE_SCOPE
