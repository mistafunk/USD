//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdGeospatial/crsBindingAPI.h"
#include "pxr/usd/usd/schemaBase.h"

#include "pxr/usd/sdf/primSpec.h"

#include "pxr/usd/usd/pyConversions.h"
#include "pxr/base/tf/pyAnnotatedBoolResult.h"
#include "pxr/base/tf/pyContainerConversions.h"
#include "pxr/base/tf/pyResultConversions.h"
#include "pxr/base/tf/pyUtils.h"
#include "pxr/base/tf/wrapTypeHelpers.h"

#include "pxr/external/boost/python.hpp"

#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

using namespace pxr_boost::python;

namespace {

#define WRAP_CUSTOM                                                     \
    template <class Cls> static void _CustomWrapCode(Cls &_class)

// fwd decl.
WRAP_CUSTOM;

        
static UsdAttribute
_CreatePositionAttr(UsdGeospatialCRSBindingAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreatePositionAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Double3), writeSparsely);
}
        
static UsdAttribute
_CreateOrientationAttr(UsdGeospatialCRSBindingAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateOrientationAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Double3), writeSparsely);
}

static std::string
_Repr(const UsdGeospatialCRSBindingAPI &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    return TfStringPrintf(
        "UsdGeospatial.CRSBindingAPI(%s)",
        primRepr.c_str());
}

struct UsdGeospatialCRSBindingAPI_CanApplyResult : 
    public TfPyAnnotatedBoolResult<std::string>
{
    UsdGeospatialCRSBindingAPI_CanApplyResult(bool val, std::string const &msg) :
        TfPyAnnotatedBoolResult<std::string>(val, msg) {}
};

static UsdGeospatialCRSBindingAPI_CanApplyResult
_WrapCanApply(const UsdPrim& prim)
{
    std::string whyNot;
    bool result = UsdGeospatialCRSBindingAPI::CanApply(prim, &whyNot);
    return UsdGeospatialCRSBindingAPI_CanApplyResult(result, whyNot);
}

} // anonymous namespace

void wrapUsdGeospatialCRSBindingAPI()
{
    typedef UsdGeospatialCRSBindingAPI This;

    UsdGeospatialCRSBindingAPI_CanApplyResult::Wrap<UsdGeospatialCRSBindingAPI_CanApplyResult>(
        "_CanApplyResult", "whyNot");

    class_<This, bases<UsdAPISchemaBase> >
        cls("CRSBindingAPI");

    cls
        .def(init<UsdPrim>(arg("prim")))
        .def(init<UsdSchemaBase const&>(arg("schemaObj")))
        .def(TfTypePythonClass())

        .def("Get", &This::Get, (arg("stage"), arg("path")))
        .staticmethod("Get")

        .def("CanApply", &_WrapCanApply, (arg("prim")))
        .staticmethod("CanApply")

        .def("Apply", &This::Apply, (arg("prim")))
        .staticmethod("Apply")

        .def("GetSchemaAttributeNames",
             &This::GetSchemaAttributeNames,
             arg("includeInherited")=true,
             return_value_policy<TfPySequenceToList>())
        .staticmethod("GetSchemaAttributeNames")

        .def("_GetStaticTfType", (TfType const &(*)()) TfType::Find<This>,
             return_value_policy<return_by_value>())
        .staticmethod("_GetStaticTfType")

        .def(!self)

        
        .def("GetPositionAttr",
             &This::GetPositionAttr)
        .def("CreatePositionAttr",
             &_CreatePositionAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetOrientationAttr",
             &This::GetOrientationAttr)
        .def("CreateOrientationAttr",
             &_CreateOrientationAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))

        .def("__repr__", ::_Repr)
    ;

    _CustomWrapCode(cls);
}

// ===================================================================== //
// Feel free to add custom code below this line, it will be preserved by 
// the code generator.  The entry point for your custom code should look
// minimally like the following:
//
// WRAP_CUSTOM {
//     _class
//         .def("MyCustomMethod", ...)
//     ;
// }
//
// Of course any other ancillary or support code may be provided.
// 
// Just remember to wrap code in the appropriate delimiters:
// 'namespace {', '}'.
//
// ===================================================================== //
// --(BEGIN CUSTOM CODE)--

namespace {

WRAP_CUSTOM {
}

}
