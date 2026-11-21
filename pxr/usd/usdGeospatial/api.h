//
// Copyright 2017 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDGEOSPATIAL_API_H
#define USDGEOSPATIAL_API_H

#include "pxr/base/arch/export.h"

#if defined(PXR_STATIC)
#   define USDGEOSPATIAL_API
#   define USDGEOSPATIAL_API_TEMPLATE_CLASS(...)
#   define USDGEOSPATIAL_API_TEMPLATE_STRUCT(...)
#   define USDGEOSPATIAL_LOCAL
#else
#   if defined(USDGEOSPATIAL_EXPORTS)
#       define USDGEOSPATIAL_API ARCH_EXPORT
#       define USDGEOSPATIAL_API_TEMPLATE_CLASS(...) ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDGEOSPATIAL_API_TEMPLATE_STRUCT(...) ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#   else
#       define USDGEOSPATIAL_API ARCH_IMPORT
#       define USDGEOSPATIAL_API_TEMPLATE_CLASS(...) ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDGEOSPATIAL_API_TEMPLATE_STRUCT(...) ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#   endif
#   define USDGEOSPATIAL_LOCAL ARCH_HIDDEN
#endif

#endif
