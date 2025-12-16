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
#include "sdfUtils.h"

#include "pxr/pxr.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/usd/sdf/fileFormat.h"

#include <string>

#define FILE_FORMAT_3DTILES_VERSION "1.0.0"

PXR_NAMESPACE_OPEN_SCOPE
#define USD3DTILES_FILE_FORMAT_TOKENS \
    ((Id, "3dtiles")) \
    ((Extension, "json")) \
    ((Version, FILE_FORMAT_3DTILES_VERSION)) \
    ((Target, "usd"))

	TF_DECLARE_PUBLIC_TOKENS(Usd3dTilesFileFormatTokens, USD3DTILES_FILE_FORMAT_TOKENS);

	TF_DECLARE_WEAK_AND_REF_PTRS(Usd3dTilesData);
	TF_DECLARE_WEAK_AND_REF_PTRS(Usd3dTilesFileFormat);

	/// \brief SdfFileFormat specialization for reading 3D Tiles tileset.json files.
	///
	/// This plugin reads OGC 3D Tiles tileset.json files and converts them to USD.
	/// It traverses the tile hierarchy and loads all GLB content using the usdGltf
	/// plugin via payload references.
	///
	/// The plugin registers for the .json extension but uses CanRead() to detect
	/// whether a file is actually a 3D Tiles tileset (by checking for required
	/// fields: "asset", "root", and "geometricError").
	class USD3DTILES_API Usd3dTilesFileFormat : public SdfFileFormat {
	public:


		/// Initialize the layer data with any file format arguments.
		SdfAbstractDataRefPtr InitData(const FileFormatArguments &args) const override;

		/// Check if the file at the given path is a valid 3D Tiles tileset.
		/// Returns true if the file contains "asset", "root", and "geometricError" fields.
		bool CanRead(const std::string &file) const override;

		/// Read the tileset.json file and populate the layer with USD content.
		/// Creates an Xform hierarchy matching the tile structure with payloads
		/// pointing to GLB files.
		bool Read(SdfLayer *layer,
		          const std::string &resolvedPath,
		          bool metadataOnly) const override;

		/// Read from a string containing tileset JSON.
		bool ReadFromString(SdfLayer *layer, const std::string &str) const override;

		/// Write the layer to a string (delegates to USDA format).
		bool WriteToString(const SdfLayer &layer,
		                   std::string *str,
		                   const std::string &comment = std::string()) const override;

		/// Write the layer to a stream.
		bool WriteToStream(const SdfSpecHandle &spec,
		                   std::ostream &out,
		                   size_t indent) const override;

		/// Write the layer to a file (not supported for 3D Tiles).
		bool WriteToFile(
			const SdfLayer &layer,
			const std::string &filePath,
			const std::string &comment = std::string(),
			const FileFormatArguments &args = FileFormatArguments()) const override;

	protected:
		SDF_FILE_FORMAT_FACTORY_ACCESS;

		Usd3dTilesFileFormat();
		~Usd3dTilesFileFormat() override = default;

	private:
		/// Internal implementation of Read from parsed JSON
		bool _ReadFromJson(SdfLayer *layer,
		                   const std::string &jsonStr,
		                   const std::string &baseDir,
		                   const std::string &resolvedPath) const;
	};

PXR_NAMESPACE_CLOSE_SCOPE
