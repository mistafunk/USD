//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/usd/usdGeospatial/binding.h"
#include "pxr/usd/usdGeospatial/crsBindingAPI.h"
#include "pxr/usd/usdGeospatial/tokens.h"

#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/stringUtils.h"

#include <cmath>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

bool
_IsFinite(const GfVec3d& v)
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

TfToken
_ComposedWkt(const UsdPrim& prim)
{
    TfToken wkt;
    if (const UsdAttribute attr =
            prim.GetAttribute(UsdGeospatialTokens->crsWkt)) {
        attr.Get(&wkt);
    }
    return wkt;
}

} // anonymous namespace

UsdGeospatialBinding
UsdGeospatialResolveBinding(const UsdPrim& prim)
{
    UsdGeospatialBinding binding;
    for (UsdPrim p = prim; p && !p.IsPseudoRoot(); p = p.GetParent()) {
        if (p.HasAPI<UsdGeospatialCRSBindingAPI>()) {
            binding.anchor = p;
            break;
        }
    }
    if (!binding.anchor) {
        return binding;
    }
    binding.isDirect = binding.anchor == prim;
    binding.wkt = _ComposedWkt(binding.anchor);
    if (binding.wkt.IsEmpty()) {
        binding.error = TfStringPrintf(
            "<%s> is directly bound but has no crs:wkt definition (is the "
            "referenced GeospatialCRS prim missing?); there is no fallback "
            "to an ancestor CRS",
            binding.anchor.GetPath().GetText());
    }
    return binding;
}

std::vector<std::string>
UsdGeospatialValidatePlacement(const UsdPrim& prim, UsdTimeCode time)
{
    std::vector<std::string> problems;
    if (!prim) {
        return problems;
    }
    const UsdAttribute position =
        prim.GetAttribute(UsdGeospatialTokens->crsPosition);
    const UsdAttribute orientation =
        prim.GetAttribute(UsdGeospatialTokens->crsOrientation);
    const char* path = prim.GetPath().GetText();

    if (!prim.HasAPI<UsdGeospatialCRSBindingAPI>()) {
        for (const UsdAttribute& attr : {position, orientation}) {
            if (attr && attr.HasAuthoredValueOpinion()) {
                problems.push_back(TfStringPrintf(
                    "<%s> authors %s but is not directly bound; another "
                    "absolute placement needs its own "
                    "GeospatialCRSBindingAPI",
                    path, attr.GetName().GetText()));
            }
        }
        return problems;
    }

    if (_ComposedWkt(prim).IsEmpty()) {
        problems.push_back(TfStringPrintf(
            "<%s> is directly bound but has no crs:wkt definition", path));
    }

    GfVec3d value;
    if (!position || !position.HasAuthoredValueOpinion()) {
        problems.push_back(TfStringPrintf(
            "<%s> has no crs:position", path));
    } else if (!position.Get(&value, time)) {
        problems.push_back(TfStringPrintf(
            "<%s> crs:position is blocked", path));
    } else if (!_IsFinite(value)) {
        problems.push_back(TfStringPrintf(
            "<%s> crs:position is not finite", path));
    }

    if (orientation && orientation.HasAuthoredValueOpinion()) {
        if (!orientation.Get(&value, time)) {
            problems.push_back(TfStringPrintf(
                "<%s> crs:orientation is blocked", path));
        } else if (!_IsFinite(value)) {
            problems.push_back(TfStringPrintf(
                "<%s> crs:orientation is not finite", path));
        }
    }
    return problems;
}

PXR_NAMESPACE_CLOSE_SCOPE
