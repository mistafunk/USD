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

#include "pxr/pxr.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/js/value.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/usd/common.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace tiles3d {

/// Content reference within a tile
struct TileContent {
    std::string uri;  // Relative path to content file (typically .glb), may be a template URI
};

/// Subdivision scheme for implicit tiling
enum class SubdivisionScheme {
    QUADTREE,  // 4 children per tile (2D spatial subdivision)
    OCTREE     // 8 children per tile (3D spatial subdivision)
};

/// Implicit tiling configuration (3D Tiles 1.1)
struct ImplicitTiling {
    SubdivisionScheme subdivisionScheme = SubdivisionScheme::QUADTREE;
    int subtreeLevels = 0;      // Number of levels in each subtree
    int availableLevels = 0;    // Total number of levels with available tiles
    std::string subtreesUri;    // Template URI for subtree files (e.g., "subtrees/{level}.{x}.{y}.subtree")
};

/// Represents a single tile in the 3D Tiles hierarchy
struct Tile {
    std::optional<PXR_NS::GfMatrix4d> transform;  // 4x4 column-major transform
    std::optional<TileContent> content;            // Content for this tile (may be template URI)
    std::vector<TileContent> contents;             // Multiple contents (3D Tiles 1.1)
    std::vector<Tile> children;                    // Child tiles (empty for implicit tiling)
    double geometricError = 0.0;                   // LOD metric (for future use)
    std::optional<ImplicitTiling> implicitTiling;  // Implicit tiling configuration
};

/// Coordinate Reference System information
/// OGC 3D Tiles 1.1 default is EPSG:4978 (WGS 84 ECEF)
struct CoordinateReferenceSystem {
    std::string wkt;           // WKT2 string representation
    std::string authority;     // e.g., "EPSG"
    int code = 0;              // e.g., 4978

    /// Returns true if this is the default ECEF CRS (EPSG:4978)
    bool isDefault() const { return authority == "EPSG" && code == 4978; }

    /// Get the default OGC 3D Tiles 1.1 CRS (EPSG:4978 - WGS 84 ECEF)
    static CoordinateReferenceSystem getDefault();
};

/// Represents the root tileset structure
struct Tileset {
    std::string version;       // asset.version
    double geometricError;     // Root geometric error
    Tile root;                 // Root tile
    CoordinateReferenceSystem crs;  // Coordinate reference system
};

/// Tile coordinates for implicit tiling
struct TileCoord {
    int level = 0;
    int x = 0;
    int y = 0;
    int z = 0;  // Only used for OCTREE
};

/// Reference to a GLB file with its accumulated world transform
struct GlbReference {
    std::string uri;                // Relative path to GLB file
    PXR_NS::GfMatrix4d transform;   // Accumulated world transform
    std::string primPath;           // USD prim path (e.g., "/Tileset/tile_0/tile_1")
};

/// Callback type for loading subtree data
/// Takes the subtree URI and returns the raw binary data, or empty vector on failure
using SubtreeLoader = std::function<std::vector<uint8_t>(const std::string& uri)>;

/// Parse a tileset.json file from a JsValue
/// Returns true on success, false on failure with error message
bool parseTileset(const PXR_NS::JsValue& json, Tileset& tileset, std::string& errorMsg);

/// Recursively traverse the tile hierarchy and collect all GLB content references
/// with their accumulated world transforms
void collectAllGlbContent(
    const Tile& tile,
    const PXR_NS::GfMatrix4d& parentTransform,
    const std::string& parentPath,
    int& childIndex,
    std::vector<GlbReference>& refs,
    bool skipSelfTransform = false);

/// Expand a template URI by substituting {level}, {x}, {y}, {z} placeholders
std::string expandTemplateUri(const std::string& templateUri, const TileCoord& coord);

/// Collect GLB content from an implicit tileset
/// Uses the subtree loader to fetch subtree availability data
void collectImplicitGlbContent(
    const Tile& implicitRoot,
    const PXR_NS::GfMatrix4d& parentTransform,
    const std::string& parentPath,
    const std::string& baseDir,
    const SubtreeLoader& loadSubtree,
    std::vector<GlbReference>& refs,
    bool skipRootTransform = false);

/// Author or reuse a CRS prim for the tileset CRS and bind it to the tileset root prim.
bool
AuthorAndBindTilesetCrs(PXR_NS::SdfLayer* layer,
                        const PXR_NS::SdfPath& tilesetRootPath,
                        const CoordinateReferenceSystem& crs,
                        PXR_NS::SdfAbstractData* data);

/// Subtree availability information for testing
struct SubtreeAvailabilityInfo {
    bool tileAvailabilityConstant = false;
    bool tileAvailabilityConstantValue = false;
    int tileAvailabilityBitCount = 0;

    bool contentAvailabilityConstant = false;
    bool contentAvailabilityConstantValue = false;
    int contentAvailabilityBitCount = 0;

    bool childSubtreeAvailabilityConstant = false;
    bool childSubtreeAvailabilityConstantValue = false;
    int childSubtreeAvailabilityBitCount = 0;

    int numAvailableTiles = 0;
    int numAvailableContent = 0;
    int numAvailableChildSubtrees = 0;
};

/// Parse a subtree file and return availability information
/// This is primarily for testing purposes
bool parseSubtreeForTesting(
    const std::vector<uint8_t>& data,
    int subtreeLevels,
    SubdivisionScheme scheme,
    SubtreeAvailabilityInfo& info);

}  // namespace tiles3d
