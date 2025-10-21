//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// GENERATED FILE.  DO NOT EDIT.
#include "pxr/external/boost/python/class.hpp"
#include "pxr/usd/usdGeospatial/tokens.h"

PXR_NAMESPACE_USING_DIRECTIVE

#define _ADD_TOKEN(cls, name) \
    cls.add_static_property(#name, +[]() { return UsdGeospatialTokens->name.GetString(); });

void wrapUsdGeospatialTokens()
{
    pxr_boost::python::class_<UsdGeospatialTokensType, pxr_boost::python::noncopyable>
        cls("Tokens", pxr_boost::python::no_init);
    _ADD_TOKEN(cls, crsBinding);
    _ADD_TOKEN(cls, crsWkt);
    _ADD_TOKEN(cls, BindingAPI);
    _ADD_TOKEN(cls, CoordinateReferenceSystem);
}
