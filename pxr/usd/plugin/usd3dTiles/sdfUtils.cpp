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
#include "sdfUtils.h"

#include "pxr/usd/sdf/payload.h"
#include "pxr/usd/sdf/schema.h"
#include "pxr/usd/usd/tokens.h"

#include <cassert>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
	template<typename T>
	void
	_appendChild(SdfAbstractData *data,
	             const SdfPath &specPath,
	             const TfToken &childKey,
	             const T &child) {
		std::vector<T> children;
		SdfAbstractDataTypedValue getter(&children);
		(void) data->Has(specPath, childKey, &getter);
		children.push_back(child);
		data->Set(specPath, childKey, SdfAbstractDataConstTypedValue(&children));
	}

	template<typename T>
	void
	_prependListOp(SdfAbstractData *data, const SdfPath &specPath, const TfToken &field, const T &item) {
		SdfListOp<T> listOp;
		SdfAbstractDataTypedValue getter(&listOp);
		(void) data->Has(specPath, field, &getter);
		typename SdfListOp<T>::ItemVector prependedItems = listOp.GetPrependedItems();
		prependedItems.insert(prependedItems.begin(), item);
		listOp.SetPrependedItems(prependedItems);
		data->Set(specPath, field, SdfAbstractDataConstTypedValue(&listOp));
	}
} // anonymous namespace

namespace tiles3d {
	void
	createPseudoRootSpec(SdfAbstractData *data) {
		data->CreateSpec(SdfPath::AbsoluteRootPath(), SdfSpecTypePseudoRoot);
	}

	void
	setLayerMetadata(SdfAbstractData *data, const TfToken &key, const VtValue &value) {
		data->Set(SdfPath::AbsoluteRootPath(), key, value);
	}

	SdfPath
	createPrimSpec(SdfAbstractData *data,
	               const SdfPath &parentPrimPath,
	               const TfToken &primName,
	               const TfToken &primType,
	               SdfSpecifier specifier,
	               bool append) {
		assert(parentPrimPath.IsAbsoluteRootPath() ||
			parentPrimPath.IsPrimOrPrimVariantSelectionPath());
		SdfPath primPath = parentPrimPath.AppendChild(primName);
		data->CreateSpec(primPath, SdfSpecTypePrim);
		data->Set(primPath, SdfFieldKeys->Specifier, SdfAbstractDataConstTypedValue(&specifier));
		if (!primType.IsEmpty()) {
			data->Set(primPath, SdfFieldKeys->TypeName, SdfAbstractDataConstTypedValue(&primType));
		}

		if (append)
			_appendChild(data, parentPrimPath, SdfChildrenKeys->PrimChildren, primName);

		return primPath;
	}

	void
	setPrimMetadata(SdfAbstractData *data,
	                const SdfPath &primPath,
	                const TfToken &key,
	                const VtValue &value) {
		assert(primPath.IsPrimPath());
		data->Set(primPath, key, value);
	}

	void
	addPrimPayload(SdfAbstractData *data, const SdfPath &primPath, const SdfPayload &payload) {
		assert(primPath.IsPrimOrPrimVariantSelectionPath());
		_prependListOp(data, primPath, SdfFieldKeys->Payload, payload);
	}

	void
	addPrimReference(SdfAbstractData *data,
	                 const SdfPath &primPath,
	                 const SdfReference &reference) {
		assert(primPath.IsPrimOrPrimVariantSelectionPath());
		_prependListOp(data, primPath, SdfFieldKeys->References, reference);
	}

	void
	addPrimApiSchema(SdfAbstractData *data,
	                 const SdfPath &primPath,
	                 const TfToken &apiSchema) {
		assert(primPath.IsPrimOrPrimVariantSelectionPath());
		SdfTokenListOp apiSchemas;
		SdfAbstractDataTypedValue getter(&apiSchemas);
		(void) data->Has(primPath, UsdTokens->apiSchemas, &getter);
		auto prepended = apiSchemas.GetPrependedItems();
		prepended.insert(prepended.begin(), apiSchema);
		apiSchemas.SetPrependedItems(prepended);
		data->Set(primPath, UsdTokens->apiSchemas, SdfAbstractDataConstTypedValue(&apiSchemas));
	}

	SdfPath
	createAttributeSpec(SdfAbstractData *data,
	                    const SdfPath &primPath,
	                    const TfToken &attrName,
	                    const SdfValueTypeName &typeName,
	                    SdfVariability variability) {
		assert(primPath.IsPrimOrPrimVariantSelectionPath());
		SdfPath propertyPath = primPath.AppendProperty(attrName);
		data->CreateSpec(propertyPath, SdfSpecTypeAttribute);

		TfToken typeNameToken = typeName.GetAsToken();
		data->Set(propertyPath, SdfFieldKeys->TypeName, SdfAbstractDataConstTypedValue(&typeNameToken));
		if (variability != SdfVariabilityVarying) {
			data->Set(
				propertyPath, SdfFieldKeys->Variability, SdfAbstractDataConstTypedValue(&variability));
		}

		_appendChild(data, primPath, SdfChildrenKeys->PropertyChildren, attrName);

		return propertyPath;
	}

	void
	setAttributeDefaultValue(SdfAbstractData *data, const SdfPath &propertyPath, const VtValue &value) {
		assert(propertyPath.IsPropertyPath());
		data->Set(propertyPath, SdfFieldKeys->Default, value);
	}

	void
	setAttributeDefaultValue(SdfAbstractData *data,
	                         const SdfPath &propertyPath,
	                         const SdfAbstractDataConstValue &value) {
		assert(propertyPath.IsPropertyPath());
		data->Set(propertyPath, SdfFieldKeys->Default, value);
	}
} // namespace tiles3d
