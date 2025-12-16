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
#include "fileFormat.h"

#include "debugCodes.h"
#include "sdfUtils.h"
#include "tileset.h"

#include "pxr/base/arch/fileSystem.h"
#include "pxr/base/js/json.h"
#include "pxr/base/tf/debug.h"
#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/staticData.h"
#include "pxr/base/tf/stopwatch.h"
#include "pxr/usd/ar/asset.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/payload.h"
#include "pxr/usd/usd/common.h"
#include "pxr/usd/usdGeom/tokens.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>

namespace {
	constexpr bool DBG = false;
}

PXR_NAMESPACE_OPEN_SCOPE
	TF_DEFINE_PUBLIC_TOKENS(Usd3dTilesFileFormatTokens, USD3DTILES_FILE_FORMAT_TOKENS);

	TF_REGISTRY_FUNCTION(TfType) {
		SDF_DEFINE_FILE_FORMAT(Usd3dTilesFileFormat, SdfFileFormat);
	}

	Usd3dTilesFileFormat::Usd3dTilesFileFormat()
		: SdfFileFormat(Usd3dTilesFileFormatTokens->Id,
		                Usd3dTilesFileFormatTokens->Version,
		                Usd3dTilesFileFormatTokens->Target,
		                Usd3dTilesFileFormatTokens->Extension) {
		TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "3D Tiles file format plugin initialized\n");
	}


	SdfAbstractDataRefPtr
	Usd3dTilesFileFormat::InitData(const FileFormatArguments& args) const {
		Usd3dTilesDataRefPtr data(new Usd3dTilesData());
		return data;
	}

	bool
	Usd3dTilesFileFormat::CanRead(const std::string &filePath) const {
		// Quick check for 3D Tiles signature in JSON file
		// Must contain: "asset", "root", and "geometricError" fields

		ArResolver &resolver = ArGetResolver();
		std::shared_ptr<ArAsset> asset = resolver.OpenAsset(ArResolvedPath(filePath));
		if (!asset) {
			return false;
		}

		// Read first 4KB to check for signature
		size_t readSize = std::min(static_cast<size_t>(4096), asset->GetSize());
		std::shared_ptr<const char> buffer = asset->GetBuffer();
		if (!buffer) {
			return false;
		}

		std::string content(buffer.get(), readSize);

		// Simple heuristic: check for 3D Tiles signature fields
		bool hasAsset = content.find("\"asset\"") != std::string::npos;
		bool hasRoot = content.find("\"root\"") != std::string::npos;
		bool hasGeometricError = content.find("\"geometricError\"") != std::string::npos;

		return hasAsset && hasRoot && hasGeometricError;
	}

	bool
	Usd3dTilesFileFormat::Read(SdfLayer *layer,
	                           const std::string &resolvedPath,
	                           bool metadataOnly) const {
		TfStopwatch stopwatch;
		stopwatch.Start();

		TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Reading 3D Tiles: %s\n", resolvedPath.c_str());

		// Open the asset
		ArResolver &resolver = ArGetResolver();
		std::shared_ptr<ArAsset> asset = resolver.OpenAsset(ArResolvedPath(resolvedPath));
		if (!asset) {
			TF_RUNTIME_ERROR("Failed to open asset: %s", resolvedPath.c_str());
			return false;
		}

		// Read the entire file
		std::shared_ptr<const char> buffer = asset->GetBuffer();
		if (!buffer) {
			TF_RUNTIME_ERROR("Failed to read asset buffer: %s", resolvedPath.c_str());
			return false;
		}

		std::string jsonStr(buffer.get(), asset->GetSize());

		// Get the base directory for resolving relative URIs
		std::string baseDir = TfGetPathName(resolvedPath);
		if (baseDir.empty()) {
			baseDir = "./";
		}

		bool result = _ReadFromJson(layer, jsonStr, baseDir, resolvedPath);

		stopwatch.Stop();
		TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Total time: %ld ms\n",
		             static_cast<long int>(stopwatch.GetMilliseconds()));

		if constexpr (DBG) {
			std::ofstream dbgOut("/tmp/tileset_dbg.usda");
			std::string outStr;
			WriteToString(*layer, &outStr);
			dbgOut << outStr;
		}

		return result;
	}

	bool
	Usd3dTilesFileFormat::ReadFromString(SdfLayer *layer, const std::string &str) const {
		return _ReadFromJson(layer, str, "./", "<string>");
	}

	bool
	Usd3dTilesFileFormat::_ReadFromJson(SdfLayer *layer,
	                                    const std::string &jsonStr,
	                                    const std::string &baseDir,
	                                    const std::string &resolvedPath) const {
		// Parse JSON
		JsParseError parseError;
		JsValue root = JsParseString(jsonStr, &parseError);
		if (root.IsNull()) {
			TF_RUNTIME_ERROR("Failed to parse tileset.json: %s (line %d, col %d)",
			                 parseError.reason.c_str(),
			                 parseError.line,
			                 parseError.column);
			return false;
		}

		// Parse tileset structure
		tiles3d::Tileset tileset;
		std::string errorMsg;
		if (!tiles3d::parseTileset(root, tileset, errorMsg)) {
			TF_RUNTIME_ERROR("Invalid tileset: %s", errorMsg.c_str());
			return false;
		}

		TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Parsed tileset version %s, geometricError %.2f\n",
		             tileset.version.c_str(), tileset.geometricError);

		// Collect all GLB references with transforms
		std::vector<tiles3d::GlbReference> glbRefs;
		GfMatrix4d identity(1.0);
		int childIndex = 0;

		// Check if this is an implicit tileset
		if (tileset.root.implicitTiling) {
			TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Processing implicit tileset\n");

			// Create a subtree loader that uses the ArResolver
			ArResolver &resolver = ArGetResolver();
			tiles3d::SubtreeLoader subtreeLoader = [&resolver](const std::string& uri) -> std::vector<uint8_t> {
				std::shared_ptr<ArAsset> asset = resolver.OpenAsset(ArResolvedPath(uri));
				if (!asset) {
					TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Failed to open subtree asset: %s\n", uri.c_str());
					return {};
				}
				std::shared_ptr<const char> buffer = asset->GetBuffer();
				if (!buffer) {
					TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Failed to read subtree buffer: %s\n", uri.c_str());
					return {};
				}
				return std::vector<uint8_t>(buffer.get(), buffer.get() + asset->GetSize());
			};

			tiles3d::collectImplicitGlbContent(tileset.root, identity, "/Tileset", baseDir, subtreeLoader, glbRefs, /*skipRootTransform=*/true);
		} else {
			// Explicit tileset - use existing collection logic
			tiles3d::collectAllGlbContent(tileset.root, identity, "/Tileset", childIndex, glbRefs, /*skipSelfTransform=*/true);
		}

		TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Found %zu GLB content references\n", glbRefs.size());

		// Initialize layer data
		SdfAbstractDataRefPtr layerData = InitData(layer->GetFileFormatArguments());
		SdfAbstractData *data = layerData.operator->();

		// Set layer metadata
		tiles3d::setLayerMetadata(data, SdfFieldKeys->Documentation,
		                          VtValue(std::string("Generated from 3D Tiles: ") + resolvedPath));
		tiles3d::setLayerMetadata(data, UsdGeomTokens->upAxis, VtValue(TfToken("Z")));
		tiles3d::setLayerMetadata(data, UsdGeomTokens->metersPerUnit, VtValue(1.0));

		// Create root Xform prim
		SdfPath rootPath = tiles3d::createPrimSpec(data, SdfPath::AbsoluteRootPath(),
                                                   TfToken("Tileset"), UsdGeomTokens->Xform);

		// Author CRS prim and binding using usdGeospatial
		tiles3d::AuthorAndBindTilesetCrs(layer, rootPath, tileset.crs, data);

		// Track created prims to avoid duplicates
		std::set<SdfPath> createdPrims;
		createdPrims.insert(rootPath);

		// Create prim hierarchy and add payloads for each GLB
		for (const auto &ref: glbRefs) {
			// Parse the prim path and create intermediate Xforms
			SdfPath primPath(ref.primPath);

			// Create parent prims if they don't exist
			SdfPath currentPath = SdfPath::AbsoluteRootPath();
			for (const auto &element: primPath.GetPrefixes()) {
				if (createdPrims.find(element) == createdPrims.end()) {
					TfToken primName = element.GetNameToken();
					tiles3d::createPrimSpec(data, currentPath, primName, UsdGeomTokens->Xform);
					createdPrims.insert(element);
				}
				currentPath = element;
			}

			// Create xformOp:transform attribute unless the transform is identity
			static const GfMatrix4d identityMatrix(1.0);
			if (bool emitTransform = (ref.transform != identityMatrix); emitTransform) {
				SdfPath xformOpPath = tiles3d::createAttributeSpec(data, primPath,
																   TfToken("xformOp:transform"),
																   SdfValueTypeNames->Matrix4d);
				tiles3d::setAttributeDefaultValue(data, xformOpPath, ref.transform);

				// Set xformOpOrder
				VtTokenArray opOrder;
				opOrder.push_back(TfToken("xformOp:transform"));
				SdfPath opOrderPath = tiles3d::createAttributeSpec(data, primPath,
																   UsdGeomTokens->xformOpOrder,
																   SdfValueTypeNames->TokenArray,
																   SdfVariabilityUniform);
				tiles3d::setAttributeDefaultValue(data, opOrderPath, opOrder);
			}

			// Resolve the GLB path relative to the tileset.json (keep relative dot syntax)
			std::string glbPath;
			if (ref.uri.empty()) {
				continue;
			}

			// If already absolute or URL, keep as-is; otherwise keep relative (prepend ./ for clarity)
			if (ref.uri[0] == '/' || ref.uri.find("://") != std::string::npos) {
				glbPath = ref.uri;
			} else {
				glbPath = "./" + ref.uri;
			}

			TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Adding payload for %s -> %s\n",
			             ref.primPath.c_str(), glbPath.c_str());

			// Add reference pointing to the GLB file so it loads into this layer
            SdfReference glbRef(glbPath);
            tiles3d::addPrimReference(data, primPath, glbRef);
		}

		// Set the layer data
		_SetLayerData(layer, layerData);

		return true;
	}

	bool
	Usd3dTilesFileFormat::WriteToString(const SdfLayer &layer,
	                                    std::string *str,
	                                    const std::string &comment) const {
		// Delegate to USDA format for text output
		SdfFileFormatConstPtr usdaFormat = SdfFileFormat::FindById(TfToken("usda"));
		if (usdaFormat) {
			return usdaFormat->WriteToString(layer, str, comment);
		}
		return false;
	}

	bool
	Usd3dTilesFileFormat::WriteToStream(const SdfSpecHandle &spec,
	                                    std::ostream &out,
	                                    size_t indent) const {
		// Delegate to USDA format
		SdfFileFormatConstPtr usdaFormat = SdfFileFormat::FindById(TfToken("usda"));
		if (usdaFormat) {
			return usdaFormat->WriteToStream(spec, out, indent);
		}
		return false;
	}

	bool
	Usd3dTilesFileFormat::WriteToFile(const SdfLayer &layer,
	                                  const std::string &filePath,
	                                  const std::string &comment,
	                                  const FileFormatArguments &args) const {
		// Writing to 3D Tiles format is not supported
		TF_RUNTIME_ERROR("Writing to 3D Tiles format is not supported");
		return false;
	}

PXR_NAMESPACE_CLOSE_SCOPE
