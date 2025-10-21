//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDGEOSPATIAL_TOKENS_H
#define USDGEOSPATIAL_TOKENS_H

/// \file usdGeospatial/tokens.h

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// 
// This is an automatically generated file (by usdGenSchema.py).
// Do not hand-edit!
// 
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

#include "pxr/pxr.h"
#include "pxr/usd/usdGeospatial/api.h"
#include "pxr/base/tf/staticData.h"
#include "pxr/base/tf/token.h"
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE


/// \class UsdGeospatialTokensType
///
/// \link UsdGeospatialTokens \endlink provides static, efficient
/// \link TfToken TfTokens\endlink for use in all public USD API.
///
/// These tokens are auto-generated from the module's schema, representing
/// property names, for when you need to fetch an attribute or relationship
/// directly by name, e.g. UsdPrim::GetAttribute(), in the most efficient
/// manner, and allow the compiler to verify that you spelled the name
/// correctly.
///
/// UsdGeospatialTokens also contains all of the \em allowedTokens values
/// declared for schema builtin attributes of 'token' scene description type.
/// Use UsdGeospatialTokens like so:
///
/// \code
///     gprim.GetMyTokenValuedAttr().Set(UsdGeospatialTokens->crsBinding);
/// \endcode
struct UsdGeospatialTokensType {
    USDGEOSPATIAL_API UsdGeospatialTokensType();
    /// \brief "crs:binding"
    /// 
    /// foobar
    const TfToken crsBinding;
    /// \brief "crs:wkt"
    /// 
    /// UsdGeospatialCoordinateReferenceSystem
    const TfToken crsWkt;
    /// \brief "BindingAPI"
    /// 
    /// Schema identifer and family for UsdGeospatialBindingAPI
    const TfToken BindingAPI;
    /// \brief "CoordinateReferenceSystem"
    /// 
    /// Schema identifer and family for UsdGeospatialCoordinateReferenceSystem
    const TfToken CoordinateReferenceSystem;
    /// A vector of all of the tokens listed above.
    const std::vector<TfToken> allTokens;
};

/// \var UsdGeospatialTokens
///
/// A global variable with static, efficient \link TfToken TfTokens\endlink
/// for use in all public USD API.  \sa UsdGeospatialTokensType
extern USDGEOSPATIAL_API TfStaticData<UsdGeospatialTokensType> UsdGeospatialTokens;

PXR_NAMESPACE_CLOSE_SCOPE

#endif
