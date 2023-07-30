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

#include "pxr/imaging/hd/geomSubset.h"
#include "pxr/imaging/hd/material.h"
#include "pxr/imaging/hd/materialBindingsSchema.h"
#include "pxr/imaging/hd/materialConnectionSchema.h"
#include "pxr/imaging/hd/materialNetworkSchema.h"
#include "pxr/imaging/hd/materialNodeSchema.h"
#include "pxr/imaging/hd/materialSchema.h"
#include "pxr/imaging/hd/meshSchema.h"
#include "pxr/imaging/hd/meshTopologySchema.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/vectorSchema.h"
#include "pxr/imaging/hd/xformSchema.h"

#include "pxr/usdImaging/usdImaging/tokens.h"

#include "prt/StringUtils.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <numeric>
#include <sstream>

namespace {

constexpr bool DBG = false;

void detectAndAppendCGACErrors(prt::CGAErrorLevel level, const wchar_t* message, CGACErrors& cgacErrors) {
	if (message != nullptr &&
	    (std::wcsstr(message, L"CGAC version") || std::wcsstr(message, L"Non-recognized builtin method"))) {
		const bool shouldBeLogged =
		        (std::wcsstr(message, L"newer than current") || (level == prt::CGAErrorLevel::CGAERROR));

		std::wstring stringMessage = message;
		prtu::replaceCGACWithCEVersion(stringMessage);
		cgacErrors.emplace_back(level, shouldBeLogged, stringMessage);
	}
}
} // namespace

prt::Status PrtCallbacks::generateError(size_t /*isIndex*/, prt::Status /*status*/, const wchar_t* message) {
	LOG_ERR << "GENERATE ERROR: " << message;
	detectAndAppendCGACErrors(prt::CGAErrorLevel::CGAERROR, message, cgacErrors);
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::assetError(size_t /*isIndex*/, prt::CGAErrorLevel level, const wchar_t* /*key*/,
                                     const wchar_t* /*uri*/, const wchar_t* message) {
	LOG_ERR << "ASSET ERROR: " << message;
	detectAndAppendCGACErrors(level, message, cgacErrors);
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::cgaError(size_t /*isIndex*/, int32_t /*shapeID*/, prt::CGAErrorLevel /*level*/,
                                   int32_t /*methodId*/, int32_t /*pc*/, const wchar_t* message) {
	LOG_ERR << "CGA ERROR: " << message;
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::cgaPrint(size_t /*isIndex*/, int32_t /*shapeID*/, const wchar_t* txt) {
	LOG_INF << "CGA PRINT: " << txt;
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::cgaReportBool(size_t /*isIndex*/, int32_t /*shapeID*/, const wchar_t* /*key*/,
                                        bool /*value*/) {
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::cgaReportFloat(size_t /*isIndex*/, int32_t /*shapeID*/, const wchar_t* /*key*/,
                                         double /*value*/) {
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::cgaReportString(size_t /*isIndex*/, int32_t /*shapeID*/, const wchar_t* /*key*/,
                                          const wchar_t* /*value*/) {
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::attrBool(size_t /*isIndex*/, int32_t /*shapeID*/, const wchar_t* key, bool value) {
	mAttributeMapBuilder->setBool(key, value);
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::attrFloat(size_t /*isIndex*/, int32_t /*shapeID*/, const wchar_t* key, double value) {
	mAttributeMapBuilder->setFloat(key, value);
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::attrString(size_t /*isIndex*/, int32_t /*shapeID*/, const wchar_t* key,
                                     const wchar_t* value) {
	mAttributeMapBuilder->setString(key, value);
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::attrBoolArray(size_t /*isIndex*/, int32_t /*shapeID*/, const wchar_t* key, const bool* values,
                                        size_t size, size_t /*nRows*/) {
	mAttributeMapBuilder->setBoolArray(key, values, size);
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::attrFloatArray(size_t /*isIndex*/, int32_t /*shapeID*/, const wchar_t* key,
                                         const double* values, size_t size, size_t /*nRows*/) {
	mAttributeMapBuilder->setFloatArray(key, values, size);
	return prt::STATUS_OK;
}

prt::Status PrtCallbacks::attrStringArray(size_t /*isIndex*/, int32_t /*shapeID*/, const wchar_t* key,
                                          const wchar_t* const* values, size_t size, size_t /*nRows*/) {
	mAttributeMapBuilder->setStringArray(key, values, size);
	return prt::STATUS_OK;
}

const CGACErrors& PrtCallbacks::getCGACErrors() const {
	return cgacErrors;
}

namespace {

using Vec3fArrayDataSource = pxr::HdRetainedTypedSampledDataSource<pxr::VtArray<pxr::GfVec3f>>;
using IntArrayDataSource = pxr::HdRetainedTypedSampledDataSource<pxr::VtIntArray>;
using TokenDataSource = pxr::HdRetainedTypedSampledDataSource<pxr::TfToken>;

pxr::HdContainerDataSourceHandle createMeshTopologyDataSource(const uint32_t* faceCounts, size_t faceCountsSize,
                                                              const uint32_t* vertexIndices, size_t vertexIndicesSize) {
	pxr::VtIntArray faceVertexCounts(faceCounts, faceCounts + faceCountsSize);
	pxr::VtIntArray faceVertexIndices(vertexIndices, vertexIndices + vertexIndicesSize);

	return pxr::HdMeshTopologySchema::Builder()
	        .SetFaceVertexCounts(IntArrayDataSource::New(faceVertexCounts))
	        .SetFaceVertexIndices(IntArrayDataSource::New(faceVertexIndices))
	        .Build();
}

pxr::HdContainerDataSourceHandle createMeshPrimvarDataSource(const double* vtx, size_t vtxSize, const double* nrm,
                                                             size_t nrmSize, const uint32_t* normalIndices,
                                                             size_t normalIndicesSize) {
	pxr::VtArray<pxr::GfVec3f> points;
	points.reserve(vtxSize / 3);
	for (size_t i = 0; i < vtxSize; i += 3) {
		points.emplace_back(vtx[i], vtx[i + 1], vtx[i + 2]);
	}

	pxr::VtArray<pxr::GfVec3f> normals;
	normals.reserve(nrmSize / 3);
	for (size_t i = 0; i < nrmSize; i += 3) {
		normals.emplace_back(nrm[i], nrm[i + 1], nrm[i + 2]);
	}

	pxr::VtIntArray normalIndicesArray(normalIndices, normalIndices + normalIndicesSize);

	pxr::HdContainerDataSourceHandle primvarsDs = pxr::HdRetainedContainerDataSource::New(
	        pxr::HdPrimvarsSchemaTokens->points,
	        pxr::HdPrimvarSchema::Builder()
	                .SetPrimvarValue(Vec3fArrayDataSource::New(points))
	                .SetInterpolation(
	                        pxr::HdPrimvarSchema::BuildInterpolationDataSource(pxr::HdPrimvarSchemaTokens->vertex))
	                .SetRole(pxr::HdPrimvarSchema::BuildRoleDataSource(pxr::HdPrimvarSchemaTokens->point))
	                .Build(),
	        pxr::HdPrimvarsSchemaTokens->normals,
	        pxr::HdPrimvarSchema::Builder()
	                .SetPrimvarValue(Vec3fArrayDataSource::New(normals))
	                .SetIndices(IntArrayDataSource::New(normalIndicesArray))
	                .SetInterpolation(
	                        pxr::HdPrimvarSchema::BuildInterpolationDataSource(pxr::HdPrimvarSchemaTokens->vertex))
	                .SetRole(pxr::HdPrimvarSchema::BuildRoleDataSource(pxr::HdPrimvarSchemaTokens->normal))
	                .Build());

	return primvarsDs;
}

auto createMaterialDataSource(const pxr::SdfPath& primPath, const prt::AttributeMap* material) {
	const wchar_t* rawMaterialName = material->getString(L"name");
	std::string materialName = prtu::toUTF8FromUTF16(rawMaterialName);
	pxr::SdfPath prtMaterialsPath = primPath.AppendChild(pxr::TfToken(materialName));

	static const pxr::HdTokenDataSourceHandle nodeIdentifierDataSource =
	        pxr::HdRetainedTypedSampledDataSource<pxr::TfToken>::New(pxr::UsdImagingTokens->UsdPreviewSurface);

	// details: pxr/usdImaging/usdImaging/drawModeStandin.cpp
	std::vector<pxr::TfToken> parameterNames;
	std::vector<pxr::HdDataSourceBaseHandle> parameters;

	size_t diffuseColorSize = 0;
	const double* rawDiffuseColor = material->getFloatArray(L"diffuseColor", &diffuseColorSize);
	assert(diffuseColorSize == 3);
	pxr::HdVec3fDataSourceHandle diffuseColor = pxr::HdRetainedTypedSampledDataSource<pxr::GfVec3f>::New(
	        pxr::GfVec3f(rawDiffuseColor[0], rawDiffuseColor[1], rawDiffuseColor[2]));

	const double rawOpacity = material->getFloat(L"opacity");
	const pxr::HdDataSourceBaseHandle opacity = pxr::HdRetainedTypedSampledDataSource<float>::New(rawOpacity);

	parameterNames.emplace_back("diffuseColor");
	parameters.push_back(diffuseColor);
	parameterNames.emplace_back("opacity");
	parameters.push_back(opacity);

	pxr::HdContainerDataSourceHandle materialNodeParameters =
	        pxr::HdRetainedContainerDataSource::New(parameterNames.size(), parameterNames.data(), parameters.data());
	pxr::HdContainerDataSourceHandle materialNode = pxr::HdMaterialNodeSchema::Builder()
	                                                        .SetNodeIdentifier(nodeIdentifierDataSource)
	                                                        .SetParameters(materialNodeParameters)
	                                                        .Build();

	pxr::TfToken previewSurfaceNodeName = prtMaterialsPath.GetAsToken();
	pxr::TfTokenVector nodeNames = {previewSurfaceNodeName};
	std::vector<pxr::HdDataSourceBaseHandle> nodeValues = {materialNode};
	pxr::HdContainerDataSourceHandle nodesDataSources =
	        pxr::HdRetainedContainerDataSource::New(nodeNames.size(), nodeNames.data(), nodeValues.data());

	const pxr::HdTokenDataSourceHandle nodePathName =
	        pxr::HdRetainedTypedSampledDataSource<pxr::TfToken>::New(previewSurfaceNodeName);

	static pxr::TfToken terminalName("surface");
	static const pxr::HdTokenDataSourceHandle outputName =
	        pxr::HdRetainedTypedSampledDataSource<pxr::TfToken>::New(terminalName);

	pxr::HdContainerDataSourceHandle terminalNode = pxr::HdMaterialConnectionSchema::Builder()
	                                                        .SetUpstreamNodeOutputName(outputName)
	                                                        .SetUpstreamNodePath(nodePathName)
	                                                        .Build();

	pxr::TfTokenVector terminalNames = {terminalName};
	std::vector<pxr::HdDataSourceBaseHandle> terminalValues = {terminalNode};
	pxr::HdContainerDataSourceHandle terminalDataSources =
	        pxr::HdRetainedContainerDataSource::New(terminalNames.size(), terminalNames.data(), terminalValues.data());

	pxr::HdContainerDataSourceHandle materialNetworkDataSource = pxr::HdMaterialNetworkSchema::Builder()
	                                                                     .SetNodes(nodesDataSources)
	                                                                     .SetTerminals(terminalDataSources)
	                                                                     .Build();

	pxr::TfTokenVector materialTokens = {pxr::HdMaterialSchemaTokens->universalRenderContext};
	std::vector<pxr::HdDataSourceBaseHandle> materialValues = {materialNetworkDataSource};
	assert(materialTokens.size() == materialValues.size());
	pxr::HdContainerDataSourceHandle materialDataSource =
	        pxr::HdMaterialSchema::BuildRetained(materialTokens.size(), materialTokens.data(), materialValues.data());

	return std::make_tuple(prtMaterialsPath, materialDataSource);
}

pxr::HdOverlayContainerDataSourceHandle createGeomSubsetDataSource(pxr::SdfPath materialId, uint32_t faceIndexStart,
                                                                   size_t faceIndexCount) {
	pxr::VtIntArray geomSubsetIndices;
	geomSubsetIndices.resize(faceIndexCount);
	std::iota(geomSubsetIndices.begin(), geomSubsetIndices.end(), faceIndexStart);

	auto geomSubsetTypeDs = TokenDataSource::New(pxr::HdGeomSubsetSchemaTokens->typeFaceSet);
	auto geomSubsetIndicesDs = IntArrayDataSource::New(geomSubsetIndices);
	auto geomSubsetDs = pxr::HdGeomSubsetSchema::BuildRetained(geomSubsetTypeDs, geomSubsetIndicesDs);

	std::array<pxr::TfToken, 1> purposeTokens = {pxr::HdMaterialBindingsSchemaTokens->allPurpose};
	std::array<pxr::HdDataSourceBaseHandle, 1> materialPaths = {
	        pxr::HdRetainedTypedSampledDataSource<pxr::SdfPath>::New(materialId)};
	static_assert(purposeTokens.size() == materialPaths.size());
	std::array<pxr::HdContainerDataSourceHandle, 2> containers = {
	        geomSubsetDs, pxr::HdRetainedContainerDataSource::New(
	                              pxr::HdMaterialBindingsSchemaTokens->materialBindings,
	                              pxr::HdMaterialBindingsSchema::BuildRetained(
	                                      materialPaths.size(), purposeTokens.data(), materialPaths.data()))};

	return pxr::HdOverlayContainerDataSource::New(containers.size(), containers.data());
}

} // namespace

void PrtCallbacks::addMesh(const wchar_t*, const double* vtx, size_t vtxSize, const double* nrm, size_t nrmSize,
                           const uint32_t* faceCounts, size_t faceCountsSize, const uint32_t* vertexIndices,
                           size_t vertexIndicesSize, const uint32_t* normalIndices, size_t normalIndicesSize,
                           double const* const* uvs, size_t const* uvsSizes, uint32_t const* const* uvCounts,
                           size_t const* uvCountsSizes, uint32_t const* const* uvIndices, size_t const* uvIndicesSizes,
                           size_t uvSetsCount, const uint32_t* faceRanges, size_t faceRangesSize,
                           const prt::AttributeMap** materials, const prt::AttributeMap** reports,
                           const int32_t* shapeIDs) {
	pxr::HdContainerDataSourceHandle meshTopologyDs =
	        createMeshTopologyDataSource(faceCounts, faceCountsSize, vertexIndices, vertexIndicesSize);
	pxr::HdContainerDataSourceHandle primvarDs =
	        createMeshPrimvarDataSource(vtx, vtxSize, nrm, nrmSize, normalIndices, normalIndicesSize);

	std::vector<pxr::TfToken> subsetNames;
	std::vector<pxr::HdDataSourceBaseHandle> subsets;
	for (size_t fri = 0; fri < faceRangesSize - 1; fri++) {
		auto [materialPath, materialDs] = createMaterialDataSource(mPrimPath, materials[fri]);

		auto materialContainerDs =
		        pxr::HdRetainedContainerDataSource::New(pxr::HdMaterialSchemaTokens->material, materialDs);
		mGeneratedData.emplace(materialPath,
		                       std::make_pair(materialContainerDs, pxr::HdMaterialSchemaTokens->material));
		mChildPrims[materialPath] = pxr::HdMaterialSchemaTokens->material;

		const uint32_t faceIndexStart = faceRanges[fri];
		const size_t faceIndexCount = faceRanges[fri + 1] - faceRanges[fri];
		auto geomSubsetDs = createGeomSubsetDataSource(materialPath, faceIndexStart, faceIndexCount);

		subsetNames.push_back(materialPath.GetNameToken());
		subsets.push_back(geomSubsetDs);
	}

	auto geomSubsetsDs =
	        pxr::HdRetainedContainerDataSource::New(subsetNames.size(), subsetNames.data(), subsets.data());

	auto meshDs = pxr::HdMeshSchema::Builder().SetTopology(meshTopologyDs).SetGeomSubsets(geomSubsetsDs).Build();

	auto geoDs = pxr::HdRetainedContainerDataSource::New(pxr::HdMeshSchemaTokens->mesh, meshDs,
	                                                     pxr::HdPrimvarsSchemaTokens->primvars, primvarDs);

	pxr::SdfPath prtMeshPath = mPrimPath.AppendChild(pxr::HdPrimTypeTokens->mesh);

	mGeneratedData.emplace(prtMeshPath, std::make_pair(geoDs, pxr::HdPrimTypeTokens->mesh));
	mChildPrims[prtMeshPath] = pxr::HdPrimTypeTokens->mesh;
}
