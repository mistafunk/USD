/**
 * Serlio - Esri CityEngine Plugin for Autodesk Maya
 *
 * See https://github.com/esri/serlio for build and usage instructions.
 *
 * Copyright (c) 2012-2019 Esri R&D Center Zurich
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "prtCallbacks.h"
#include "prtContext.h"

#include "pxr/imaging/hd/material.h"
#include "pxr/imaging/hd/materialBindingSchema.h"
#include "pxr/imaging/hd/materialConnectionSchema.h"
#include "pxr/imaging/hd/materialNetworkSchema.h"
#include "pxr/imaging/hd/materialNodeSchema.h"
#include "pxr/imaging/hd/materialSchema.h"
#include "pxr/imaging/hd/meshSchema.h"
#include "pxr/imaging/hd/meshTopologySchema.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/vectorSchema.h"
#include "pxr/imaging/hd/xformSchema.h"

#include "pxr/usdImaging/usdImaging/tokens.h"

#include "prt/StringUtils.h"

#include <cassert>
#include <sstream>

namespace {

constexpr bool DBG = false;

void detectAndAppendCGACErrors(prt::CGAErrorLevel level, const wchar_t* message,
                               CGACErrors& cgacErrors) {
	if (message != nullptr && (std::wcsstr(message, L"CGAC version") ||
	                           std::wcsstr(message, L"Non-recognized builtin method"))) {
		const bool shouldBeLogged = (std::wcsstr(message, L"newer than current") ||
		                             (level == prt::CGAErrorLevel::CGAERROR));

		std::wstring stringMessage = message;
		prtu::replaceCGACWithCEVersion(stringMessage);
		cgacErrors.emplace_back(level, shouldBeLogged, stringMessage);
	}
}
} // namespace

prt::Status PrtCallbacks::generateError(size_t /*isIndex*/, prt::Status /*status*/,
                                        const wchar_t* message) {
	LOG_ERR << "GENERATE ERROR: " << message;
	detectAndAppendCGACErrors(prt::CGAErrorLevel::CGAERROR, message, cgacErrors);
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::assetError(size_t /*isIndex*/, prt::CGAErrorLevel level,
                                     const wchar_t* /*key*/, const wchar_t* /*uri*/,
                                     const wchar_t* message) {
	LOG_ERR << "ASSET ERROR: " << message;
	detectAndAppendCGACErrors(level, message, cgacErrors);
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::cgaError(size_t /*isIndex*/, int32_t /*shapeID*/,
                                   prt::CGAErrorLevel /*level*/, int32_t /*methodId*/,
                                   int32_t /*pc*/, const wchar_t* message) {
	LOG_ERR << "CGA ERROR: " << message;
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::cgaPrint(size_t /*isIndex*/, int32_t /*shapeID*/, const wchar_t* txt) {
	LOG_INF << "CGA PRINT: " << txt;
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::cgaReportBool(size_t /*isIndex*/, int32_t /*shapeID*/,
                                        const wchar_t* /*key*/, bool /*value*/) {
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::cgaReportFloat(size_t /*isIndex*/, int32_t /*shapeID*/,
                                         const wchar_t* /*key*/, double /*value*/) {
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::cgaReportString(size_t /*isIndex*/, int32_t /*shapeID*/,
                                          const wchar_t* /*key*/, const wchar_t* /*value*/) {
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::attrBool(size_t /*isIndex*/, int32_t /*shapeID*/, const wchar_t* key,
                                   bool value) {
	mAttributeMapBuilder->setBool(key, value);
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::attrFloat(size_t /*isIndex*/, int32_t /*shapeID*/, const wchar_t* key,
                                    double value) {
	mAttributeMapBuilder->setFloat(key, value);
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::attrString(size_t /*isIndex*/, int32_t /*shapeID*/, const wchar_t* key,
                                     const wchar_t* value) {
	mAttributeMapBuilder->setString(key, value);
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::attrBoolArray(size_t /*isIndex*/, int32_t /*shapeID*/, const wchar_t* key,
                                        const bool* values, size_t size, size_t /*nRows*/) {
	mAttributeMapBuilder->setBoolArray(key, values, size);
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::attrFloatArray(size_t /*isIndex*/, int32_t /*shapeID*/,
                                         const wchar_t* key, const double* values, size_t size,
                                         size_t /*nRows*/) {
	mAttributeMapBuilder->setFloatArray(key, values, size);
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::attrStringArray(size_t /*isIndex*/, int32_t /*shapeID*/,
                                          const wchar_t* key, const wchar_t* const* values,
                                          size_t size, size_t /*nRows*/) {
	mAttributeMapBuilder->setStringArray(key, values, size);
	return prt::STATUS_OK;
}

const CGACErrors& PrtCallbacks::getCGACErrors() const {
	return cgacErrors;
}

void PrtCallbacks::addMesh(const wchar_t*, const double* vtx, size_t vtxSize, const double* nrm,
                           size_t nrmSize, const uint32_t* faceCounts, size_t faceCountsSize,
                           const uint32_t* vertexIndices, size_t vertexIndicesSize,
                           const uint32_t* normalIndices, size_t normalIndicesSize,
                           double const* const* uvs, size_t const* uvsSizes,
                           uint32_t const* const* uvCounts, size_t const* uvCountsSizes,
                           uint32_t const* const* uvIndices, size_t const* uvIndicesSizes,
                           size_t uvSetsCount, const uint32_t* faceRanges, size_t faceRangesSize,
                           const prt::AttributeMap** materials, const prt::AttributeMap** reports,
                           const int32_t*) {
	using PointArrayDataSource = pxr::HdRetainedTypedSampledDataSource<pxr::VtArray<pxr::GfVec3f>>;
	using IntArrayDataSource = pxr::HdRetainedTypedSampledDataSource<pxr::VtIntArray>;

	pxr::VtArray<pxr::GfVec3f> points;
	points.reserve(vtxSize / 3);
	for (size_t i = 0; i < vtxSize; i += 3) {
		points.emplace_back(vtx[i], vtx[i + 1], vtx[i + 2]);
	}

	pxr::HdContainerDataSourceHandle primvarsDs = pxr::HdRetainedContainerDataSource::New(
	        pxr::HdPrimvarsSchemaTokens->points,
	        pxr::HdPrimvarSchema::Builder()
	                .SetPrimvarValue(PointArrayDataSource::New(points))
	                .SetInterpolation(pxr::HdPrimvarSchema::BuildInterpolationDataSource(
	                        pxr::HdPrimvarSchemaTokens->vertex))
	                .SetRole(pxr::HdPrimvarSchema::BuildRoleDataSource(
	                        pxr::HdPrimvarSchemaTokens->point))
	                .Build());

	pxr::VtIntArray faceVertexCounts(faceCounts, faceCounts + faceCountsSize);
	pxr::VtIntArray faceVertexIndices(vertexIndices, vertexIndices + vertexIndicesSize);

	pxr::HdContainerDataSourceHandle meshDs =
	        pxr::HdMeshSchema::Builder()
	                .SetTopology(
	                        pxr::HdMeshTopologySchema::Builder()
	                                .SetFaceVertexCounts(IntArrayDataSource::New(faceVertexCounts))
	                                .SetFaceVertexIndices(
	                                        IntArrayDataSource::New(faceVertexIndices))
	                                .Build())
	                .Build();

	pxr::SdfPath prtMeshPath = mPrimPath.AppendChild(pxr::HdPrimTypeTokens->mesh);
	pxr::SdfPath prtMaterialsPath = mPrimPath.AppendChild(pxr::HdMaterialSchemaTokens->material);

	static const pxr::HdTokenDataSourceHandle nodeIdentifierDataSource =
	        pxr::HdRetainedTypedSampledDataSource<pxr::TfToken>::New(
	                pxr::UsdImagingTokens->UsdPreviewSurface);
	static const pxr::HdDataSourceBaseHandle one =
	        pxr::HdRetainedTypedSampledDataSource<float>::New(1.0f);

	// details: pxr/usdImaging/usdImaging/drawModeStandin.cpp
	std::vector<pxr::TfToken> parameterNames;
	std::vector<pxr::HdDataSourceBaseHandle> parameters;

	pxr::HdVec3fDataSourceHandle diffuseColor =
	        pxr::HdRetainedTypedSampledDataSource<pxr::GfVec3f>::New(pxr::GfVec3f(1, 0, 0));

	parameterNames.emplace_back("diffuseColor");
	parameters.push_back(diffuseColor);
	parameterNames.emplace_back("opacity");
	parameters.push_back(one);

	pxr::HdContainerDataSourceHandle materialNodeParameters =
	        pxr::HdRetainedContainerDataSource::New(parameterNames.size(), parameterNames.data(),
	                                                parameters.data());
	pxr::HdContainerDataSourceHandle materialNode =
	        pxr::HdMaterialNodeSchema::Builder()
	                .SetNodeIdentifier(nodeIdentifierDataSource)
	                .SetParameters(materialNodeParameters)
	                .Build();

	pxr::TfToken previewSurfaceNodeName("IHaveNoClue");
	pxr::TfTokenVector nodeNames = {previewSurfaceNodeName};
	std::vector<pxr::HdDataSourceBaseHandle> nodeValues = {materialNode};
	pxr::HdContainerDataSourceHandle nodesDataSources = pxr::HdRetainedContainerDataSource::New(
	        nodeNames.size(), nodeNames.data(), nodeValues.data());

	const pxr::HdTokenDataSourceHandle nodePathName =
	        pxr::HdRetainedTypedSampledDataSource<pxr::TfToken>::New(previewSurfaceNodeName);

	pxr::TfToken terminalName("surface");
	const pxr::HdTokenDataSourceHandle outputName =
	        pxr::HdRetainedTypedSampledDataSource<pxr::TfToken>::New(terminalName);

	pxr::HdContainerDataSourceHandle terminalNode = pxr::HdMaterialConnectionSchema::Builder()
	                                                        .SetUpstreamNodeOutputName(outputName)
	                                                        .SetUpstreamNodePath(nodePathName)
	                                                        .Build();

	pxr::TfTokenVector terminalNames = {terminalName};
	std::vector<pxr::HdDataSourceBaseHandle> terminalValues = {terminalNode};
	pxr::HdContainerDataSourceHandle terminalDataSources = pxr::HdRetainedContainerDataSource::New(
	        terminalNames.size(), terminalNames.data(), terminalValues.data());

	pxr::HdContainerDataSourceHandle materialNetworkDataSource =
	        pxr::HdMaterialNetworkSchema::Builder()
	                .SetNodes(nodesDataSources)
	                .SetTerminals(terminalDataSources)
	                .Build();

	pxr::TfTokenVector materialTokens = {pxr::TfToken()};
	std::vector<pxr::HdDataSourceBaseHandle> materialValues = {materialNetworkDataSource};
	assert(materialTokens.size() == materialValues.size());
	pxr::HdContainerDataSourceHandle materialDataSource = pxr::HdMaterialSchema::BuildRetained(
	        materialTokens.size(), materialTokens.data(), materialValues.data());

	pxr::TfTokenVector bindingTokens = {pxr::HdMaterialBindingSchemaTokens->materialBinding};
	pxr::HdPathDataSourceHandle bindingPathDataSource =
	        pxr::HdRetainedTypedSampledDataSource<pxr::SdfPath>::New(prtMaterialsPath);
	std::vector<pxr::HdDataSourceBaseHandle> bindingValues = {
	        std::static_pointer_cast<pxr::HdDataSourceBase>(bindingPathDataSource)};
	assert(bindingTokens.size() == bindingValues.size());
	pxr::HdContainerDataSourceHandle materialBindingDataSource =
	        pxr::HdMaterialBindingSchema::BuildRetained(bindingTokens.size(), bindingTokens.data(),
	                                                    bindingValues.data());

	pxr::HdRetainedContainerDataSourceHandle geoDataSource =
	        pxr::HdRetainedContainerDataSource::New(
	                pxr::HdMeshSchemaTokens->mesh, meshDs, pxr::HdPrimvarsSchemaTokens->primvars,
	                primvarsDs, pxr::HdMaterialBindingSchemaTokens->materialBinding,
	                materialBindingDataSource);

	mChildPrims[prtMeshPath] = pxr::HdPrimTypeTokens->mesh;
	mChildPrims[prtMaterialsPath] = pxr::HdMaterialSchemaTokens->material;

	mGeneratedData.emplace(prtMeshPath, std::make_pair(geoDataSource, pxr::HdPrimTypeTokens->mesh));
	mGeneratedData.emplace(prtMaterialsPath, std::make_pair(materialDataSource,
	                                                        pxr::HdMaterialSchemaTokens->material));
}

// void PrtCallbacks::addAsset(const wchar_t* uri, const wchar_t* fileName, const uint8_t* buffer,
// size_t size,
//                              wchar_t* result, size_t& resultSize) {
//	if (uri == nullptr || std::wcslen(uri) == 0 || fileName == nullptr || std::wcslen(fileName) ==
// 0) { 		LOG_WRN << "Skipping asset caching for invalid uri '" << uri << "' or filename '" <<
// fileName
//<< '"'; 		resultSize = 0; 		return;
//	}
//
//	std::filesystem::path assetDir = getAssetDir();
//
//	const std::filesystem::path& assetPath =
//	        (!assetDir.empty()) ? PRTContext::get().mAssetCache.put(uri, fileName, assetDir, buffer,
// size) 	                            : std::filesystem::path();
//
//	if (assetPath.empty()) {
//		resultSize = 0;
//		return;
//	}
//
//	const std::wstring pathStr = assetPath.generic_wstring();
//
//	if (resultSize <= pathStr.size()) {  // also check for null-terminator
//		resultSize = pathStr.size() + 1; // ask for space for null-terminator
//		return;
//	}
//
//	copyStringToWCharPtr(pathStr, result, resultSize);
// }
