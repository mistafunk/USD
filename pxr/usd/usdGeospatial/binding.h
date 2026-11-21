//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_USD_USD_GEOSPATIAL_BINDING_H
#define PXR_USD_USD_GEOSPATIAL_BINDING_H

/// \file usdGeospatial/binding.h

#include "pxr/pxr.h"
#include "pxr/usd/usdGeospatial/api.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/base/tf/token.h"

#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// \struct UsdGeospatialBinding
///
/// The CRS binding that applies to a prim.
///
struct UsdGeospatialBinding
{
    /// The directly bound prim the binding comes from: the prim itself or
    /// its nearest ancestor with GeospatialCRSBindingAPI applied. Invalid
    /// if no prim in the hierarchy is bound.
    UsdPrim anchor;

    /// True if the prim itself is the anchor.
    bool isDirect = false;

    /// The anchor's composed crs:wkt. Empty for a broken binding.
    TfToken wkt;

    /// Why the binding is broken, if it is. A direct binding without a CRS
    /// definition is an error; it never falls back to an ancestor's CRS.
    std::string error;

    /// True if the prim has a usable binding.
    explicit operator bool() const { return anchor && error.empty(); }
};

/// Resolves the CRS binding of \p prim: the nearest directly bound prim at
/// or above it, analogous to material binding resolution.
USDGEOSPATIAL_API
UsdGeospatialBinding UsdGeospatialResolveBinding(const UsdPrim& prim);

/// Returns the problems with the geospatial placement data on \p prim at
/// \p time, as human-readable messages; empty if there are none:
/// - a direct binding without a composed crs:wkt,
/// - a direct model binding whose crs:position is missing, blocked or not
///   finite, or whose crs:orientation is blocked or not finite,
/// - crs:position or crs:orientation authored on a prim that is not
///   directly bound (placement fields on inherited-only descendants are
///   invalid; another absolute placement needs another direct binding).
USDGEOSPATIAL_API
std::vector<std::string> UsdGeospatialValidatePlacement(
    const UsdPrim& prim, UsdTimeCode time = UsdTimeCode::Default());

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_USD_USD_GEOSPATIAL_BINDING_H
