/*
Copyright 2024 Esri. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#pragma once

#include "api.h"

#include "pxr/pxr.h"
#include "pxr/usd/sdf/abstractData.h"
#include "pxr/usd/sdf/data.h"
#include "pxr/usd/sdf/fileFormat.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/reference.h"

namespace tiles3d {

/// Creates the pseudo spec in the SdfAbstractData, which is the root of all other specs
void
createPseudoRootSpec(PXR_NS::SdfAbstractData* data);

/// Set metadata on the layer
void
setLayerMetadata(PXR_NS::SdfAbstractData* data,
                 const PXR_NS::TfToken& key,
                 const PXR_NS::VtValue& value);

/// Create a prim spec
PXR_NS::SdfPath
createPrimSpec(PXR_NS::SdfAbstractData* data,
               const PXR_NS::SdfPath& parentPrimPath,
               const PXR_NS::TfToken& primName,
               const PXR_NS::TfToken& primType = PXR_NS::TfToken(),
               PXR_NS::SdfSpecifier specifier = PXR_NS::SdfSpecifier::SdfSpecifierDef,
               bool append = true);

/// Set metadata on a prim spec
void
setPrimMetadata(PXR_NS::SdfAbstractData* data,
                const PXR_NS::SdfPath& primPath,
                const PXR_NS::TfToken& key,
                const PXR_NS::VtValue& value);

/// Add payload to a prim spec
void
addPrimPayload(PXR_NS::SdfAbstractData* data,
               const PXR_NS::SdfPath& primPath,
               const PXR_NS::SdfPayload& payload);

/// Add a reference to a prim spec
void
addPrimReference(PXR_NS::SdfAbstractData* data,
                 const PXR_NS::SdfPath& primPath,
                 const PXR_NS::SdfReference& reference);

/// Add an API schema to a prim spec (prepended to apiSchemas listOp)
void
addPrimApiSchema(PXR_NS::SdfAbstractData* data,
                 const PXR_NS::SdfPath& primPath,
                 const PXR_NS::TfToken& apiSchema);

/// Create an attribute spec
PXR_NS::SdfPath
createAttributeSpec(PXR_NS::SdfAbstractData* data,
                    const PXR_NS::SdfPath& primPath,
                    const PXR_NS::TfToken& attrName,
                    const PXR_NS::SdfValueTypeName& typeName,
                    PXR_NS::SdfVariability variability = PXR_NS::SdfVariabilityVarying);

/// Set the default value of an attribute
void
setAttributeDefaultValue(PXR_NS::SdfAbstractData* data,
                         const PXR_NS::SdfPath& propertyPath,
                         const PXR_NS::VtValue& value);

/// Set the default value of an attribute (typed version)
void
setAttributeDefaultValue(PXR_NS::SdfAbstractData* data,
                         const PXR_NS::SdfPath& propertyPath,
                         const PXR_NS::SdfAbstractDataConstValue& value);

/// Set the default value of an attribute (template version)
template<typename T>
void
setAttributeDefaultValue(PXR_NS::SdfAbstractData* data,
                         const PXR_NS::SdfPath& propertyPath,
                         const T& value)
{
    const PXR_NS::SdfAbstractDataConstTypedValue<T> inValue(&value);
    const PXR_NS::SdfAbstractDataConstValue& untypedInValue = inValue;
    setAttributeDefaultValue(data, propertyPath, untypedInValue);
}

}  // namespace tiles3d

PXR_NAMESPACE_OPEN_SCOPE

/// SdfData specialization for 3D Tiles file format
class USD3DTILES_API Usd3dTilesData : public SdfData
{
  public:
    Usd3dTilesData()
    {
        tiles3d::createPseudoRootSpec(this);
    }
};

PXR_NAMESPACE_CLOSE_SCOPE
