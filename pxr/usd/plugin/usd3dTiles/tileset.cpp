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
#include "tileset.h"

#include "debugCodes.h"

#include "pxr/base/js/json.h"
#include "pxr/base/tf/debug.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeospatial/bindingAPI.h"
#include "pxr/usd/usdGeospatial/coordinateReferenceSystem.h"
#include "pxr/usd/usdGeospatial/tokens.h"
#include "pxr/usd/sdf/layerUtils.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace tiles3d {

// WKT2 representation of EPSG:4978 (WGS 84 ECEF - Earth-Centered, Earth-Fixed)
// This is the default CRS for OGC 3D Tiles 1.1
constexpr const char* kDefaultCrsWkt = R"WKT(GEOCCS["WGS 84",
    DATUM["World Geodetic System 1984",
        ELLIPSOID["WGS 84",6378137,298.257223563,
            LENGTHUNIT["metre",1]]],
    PRIMEM["Greenwich",0,
        ANGLEUNIT["degree",0.0174532925199433]],
    CS[Cartesian,3],
        AXIS["(X)",geocentricX,
            ORDER[1],
            LENGTHUNIT["metre",1]],
        AXIS["(Y)",geocentricY,
            ORDER[2],
            LENGTHUNIT["metre",1]],
        AXIS["(Z)",geocentricZ,
            ORDER[3],
            LENGTHUNIT["metre",1]],
    ID["EPSG",4978]])WKT";

CoordinateReferenceSystem CoordinateReferenceSystem::getDefault() {
    CoordinateReferenceSystem crs;
    crs.wkt = kDefaultCrsWkt;
    crs.authority = "EPSG";
    crs.code = 4978;
    return crs;
}

namespace {

/// Parse a 4x4 transform matrix from a JSON array (column-major order)
std::optional<GfMatrix4d> parseTransform(const JsArray& arr) {
    if (arr.size() != 16) {
        return std::nullopt;
    }

    GfMatrix4d mat;
    // 3D Tiles uses column-major order: [c0r0, c0r1, c0r2, c0r3, c1r0, ...]
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            const JsValue& val = arr[col * 4 + row];
            if (!val.IsReal() && !val.IsInt()) {
                return std::nullopt;
            }
            mat[row][col] = val.IsReal() ? val.GetReal() : static_cast<double>(val.GetInt());
        }
    }
    return mat;
}

/// Parse a tile content object
bool parseContent(const JsObject& obj, TileContent& content) {
    auto uriIt = obj.find("uri");
    if (uriIt == obj.end() || !uriIt->second.IsString()) {
        // Also check for deprecated "url" field
        uriIt = obj.find("url");
        if (uriIt == obj.end() || !uriIt->second.IsString()) {
            return false;
        }
    }
    content.uri = uriIt->second.GetString();
    return true;
}

/// Parse implicit tiling configuration
bool parseImplicitTiling(const JsObject& obj, ImplicitTiling& implicit) {
    // Parse subdivisionScheme (required)
    auto schemeIt = obj.find("subdivisionScheme");
    if (schemeIt == obj.end() || !schemeIt->second.IsString()) {
        return false;
    }
    std::string scheme = schemeIt->second.GetString();
    if (scheme == "QUADTREE") {
        implicit.subdivisionScheme = SubdivisionScheme::QUADTREE;
    } else if (scheme == "OCTREE") {
        implicit.subdivisionScheme = SubdivisionScheme::OCTREE;
    } else {
        return false;  // Unknown scheme
    }

    // Parse subtreeLevels (required)
    auto subtreeLevelsIt = obj.find("subtreeLevels");
    if (subtreeLevelsIt == obj.end() || !subtreeLevelsIt->second.IsInt()) {
        return false;
    }
    implicit.subtreeLevels = subtreeLevelsIt->second.GetInt();

    // Parse availableLevels (required)
    auto availableLevelsIt = obj.find("availableLevels");
    if (availableLevelsIt == obj.end() || !availableLevelsIt->second.IsInt()) {
        return false;
    }
    implicit.availableLevels = availableLevelsIt->second.GetInt();

    // Parse subtrees.uri (required)
    auto subtreesIt = obj.find("subtrees");
    if (subtreesIt == obj.end() || !subtreesIt->second.IsObject()) {
        return false;
    }
    const JsObject& subtreesObj = subtreesIt->second.GetJsObject();
    auto uriIt = subtreesObj.find("uri");
    if (uriIt == subtreesObj.end() || !uriIt->second.IsString()) {
        return false;
    }
    implicit.subtreesUri = uriIt->second.GetString();

    return true;
}

/// Recursively parse a tile and its children
bool parseTile(const JsObject& obj, Tile& tile, std::string& errorMsg) {
    // Parse geometricError (optional, defaults to 0)
    auto geIt = obj.find("geometricError");
    if (geIt != obj.end()) {
        const JsValue& geVal = geIt->second;
        if (geVal.IsReal()) {
            tile.geometricError = geVal.GetReal();
        } else if (geVal.IsInt()) {
            tile.geometricError = static_cast<double>(geVal.GetInt());
        }
    }

    // Parse transform (optional)
    auto transformIt = obj.find("transform");
    if (transformIt != obj.end() && transformIt->second.IsArray()) {
        tile.transform = parseTransform(transformIt->second.GetJsArray());
        if (!tile.transform) {
            // TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Invalid transform matrix, using identity\n");
        }
    }

    // Parse content (optional) - single content
    auto contentIt = obj.find("content");
    if (contentIt != obj.end() && contentIt->second.IsObject()) {
        TileContent content;
        if (parseContent(contentIt->second.GetJsObject(), content)) {
            tile.content = content;
        }
    }

    // Parse contents (optional) - multiple contents (3D Tiles 1.1)
    auto contentsIt = obj.find("contents");
    if (contentsIt != obj.end() && contentsIt->second.IsArray()) {
        for (const auto& contentVal : contentsIt->second.GetJsArray()) {
            if (contentVal.IsObject()) {
                TileContent content;
                if (parseContent(contentVal.GetJsObject(), content)) {
                    tile.contents.push_back(content);
                }
            }
        }
    }

    // Parse implicitTiling (optional) - 3D Tiles 1.1
    auto implicitIt = obj.find("implicitTiling");
    if (implicitIt != obj.end() && implicitIt->second.IsObject()) {
        ImplicitTiling implicit;
        if (parseImplicitTiling(implicitIt->second.GetJsObject(), implicit)) {
            tile.implicitTiling = implicit;
            TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Found implicit tiling: %s, %d subtree levels, %d available levels\n",
                         implicit.subdivisionScheme == SubdivisionScheme::QUADTREE ? "QUADTREE" : "OCTREE",
                         implicit.subtreeLevels, implicit.availableLevels);
        }
    }

    // Parse children (optional) - only for explicit tiling
    // For implicit tiling, children are defined by the subdivision scheme
    auto childrenIt = obj.find("children");
    if (childrenIt != obj.end() && childrenIt->second.IsArray()) {
        for (const auto& childVal : childrenIt->second.GetJsArray()) {
            if (childVal.IsObject()) {
                Tile childTile;
                if (!parseTile(childVal.GetJsObject(), childTile, errorMsg)) {
                    return false;
                }
                tile.children.push_back(std::move(childTile));
            }
        }
    }

    return true;
}

}  // anonymous namespace

bool parseTileset(const JsValue& json, Tileset& tileset, std::string& errorMsg) {
    if (!json.IsObject()) {
        errorMsg = "Tileset root must be a JSON object";
        return false;
    }

    const JsObject& obj = json.GetJsObject();

    // Parse asset.version (required)
    auto const assetIt = obj.find("asset");
    if (assetIt != obj.end() && assetIt->second.IsObject()) {
        const JsObject& assetObj = assetIt->second.GetJsObject();
        auto const versionIt = assetObj.find("version");
        if (versionIt != assetObj.end() && versionIt->second.IsString()) {
            tileset.version = versionIt->second.GetString();
        }
        TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Parsing 3D Tiles version %s\n", tileset.version.c_str());
    }

    if (tileset.version.empty()) {
        TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Unable to parse 3d tiles version, assuming 1.1\n");
        tileset.version = "1.1";
    }

    // Parse geometricError (required)
    auto geIt = obj.find("geometricError");
    if (geIt == obj.end()) {
        errorMsg = "Missing required 'geometricError' field";
        return false;
    }
    const JsValue& geVal = geIt->second;
    if (geVal.IsReal()) {
        tileset.geometricError = geVal.GetReal();
    } else if (geVal.IsInt()) {
        tileset.geometricError = static_cast<double>(geVal.GetInt());
    } else {
        errorMsg = "'geometricError' must be a number";
        return false;
    }

    // Parse root tile (required)
    auto rootIt = obj.find("root");
    if (rootIt == obj.end() || !rootIt->second.IsObject()) {
        errorMsg = "Missing required 'root' field";
        return false;
    }

    if (!parseTile(rootIt->second.GetJsObject(), tileset.root, errorMsg)) {
        return false;
    }

    // Parse CRS from metadata (3D Tiles 1.1)
    // Look for CRS in top-level tileset metadata per 1.1 spec
    tileset.crs = CoordinateReferenceSystem::getDefault();  // Start with default EPSG:4978

    auto metadataIt = obj.find("metadata");
    if (metadataIt != obj.end() && metadataIt->second.IsObject()) {
        const JsObject& metadataObj = metadataIt->second.GetJsObject();
        auto crsIt = metadataObj.find("crs");
        if (crsIt != metadataObj.end()) {
            if (crsIt->second.IsString()) {
                // WKT string or EPSG URI
                std::string crsValue = crsIt->second.GetString();
                if (crsValue.find("EPSG") != std::string::npos) {
                    size_t lastSlash = crsValue.rfind('/');
                    if (lastSlash != std::string::npos) {
                        tileset.crs.code = std::stoi(crsValue.substr(lastSlash + 1));
                        tileset.crs.authority = "EPSG";
                    }
                } else {
                    tileset.crs.wkt = crsValue;
                }
            } else if (crsIt->second.IsInt()) {
                tileset.crs.code = crsIt->second.GetInt();
                tileset.crs.authority = "EPSG";
            }
        }
    }

    TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Tileset CRS: %s:%d\n",
                 tileset.crs.authority.c_str(), tileset.crs.code);

    return true;
}

/// Convert a GLB URI to a valid USD prim name
/// Handles filenames like "tree-lime-3-0.glb" -> "tree_lime_3_0"
std::string glbUriToPrimName(const std::string& uri) {
    // Extract filename without extension
    std::string filename = uri;

    // Remove path prefix if present
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        filename = filename.substr(lastSlash + 1);
    }

    // Remove .glb extension
    if (filename.size() > 4) {
        std::string ext = filename.substr(filename.size() - 4);
        for (char& c : ext) c = std::tolower(c);
        if (ext == ".glb") {
            filename = filename.substr(0, filename.size() - 4);
        }
    }

    // Convert to valid USD prim name:
    // - Replace invalid characters with underscores
    // - Ensure it doesn't start with a digit
    std::string primName;
    primName.reserve(filename.size() + 1);

    for (size_t i = 0; i < filename.size(); ++i) {
        char c = filename[i];
        if (std::isalnum(c) || c == '_') {
            // Valid character - but check if first char is digit
            if (primName.empty() && std::isdigit(c)) {
                primName += '_';  // Prefix with underscore
            }
            primName += c;
        } else {
            // Replace invalid chars (like '-') with underscore
            primName += '_';
        }
    }

    // Ensure non-empty
    if (primName.empty()) {
        primName = "content";
    }

    return primName;
}

void collectAllGlbContent(
    const Tile& tile,
    const GfMatrix4d& parentTransform,
    const std::string& parentPath,
    int& childIndex,
    std::vector<GlbReference>& refs,
    bool skipSelfTransform)
{
    // Accumulate transform (optionally skipping this tile's local transform)
    GfMatrix4d localTransform = tile.transform.value_or(GfMatrix4d(1.0));
    GfMatrix4d worldTransform = skipSelfTransform ? parentTransform : (localTransform * parentTransform);

    // Determine this tile's path
    std::string tilePath = parentPath;

    // Helper to check if URI ends with a GLB extension
    auto isGlbUri = [](const std::string& uri) {
        if (uri.size() < 4) return false;
        std::string ext = uri.substr(uri.size() - 4);
        // Convert to lowercase for comparison
        for (char& c : ext) c = std::tolower(c);
        return ext == ".glb";
    };

    // Helper to add a GLB reference
    auto addGlbRef = [&](const std::string& uri, const std::string& path) {
        if (isGlbUri(uri)) {
            GlbReference ref;
            ref.uri = uri;
            ref.transform = worldTransform;
            ref.primPath = path;
            refs.push_back(ref);
            TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Found GLB content: %s at %s\n",
                         uri.c_str(), path.c_str());
        } else {
            TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Skipping non-GLB content: %s\n", uri.c_str());
        }
    };

    // Collect single content
    if (tile.content) {
        // Use the GLB filename to create a unique prim name
        std::string contentPrimName = glbUriToPrimName(tile.content->uri);
        std::string contentPath = tilePath + "/" + contentPrimName;
        addGlbRef(tile.content->uri, contentPath);
    }

    // Collect multiple contents (3D Tiles 1.1)
    for (const auto& content : tile.contents) {
        // Use the GLB filename to create a unique prim name
        std::string contentPrimName = glbUriToPrimName(content.uri);
        std::string contentPath = tilePath + "/" + contentPrimName;
        addGlbRef(content.uri, contentPath);
    }

    // Recursively process children
    int localChildIndex = 0;
    for (const auto& child : tile.children) {
        std::string childPath = tilePath + "/tile_" + std::to_string(localChildIndex);
        collectAllGlbContent(child, worldTransform, childPath, localChildIndex, refs, /*skipSelfTransform=*/false);
        localChildIndex++;
    }
}

std::string expandTemplateUri(const std::string& templateUri, const TileCoord& coord) {
    std::string result = templateUri;

    // Replace {level}
    size_t pos = result.find("{level}");
    while (pos != std::string::npos) {
        result.replace(pos, 7, std::to_string(coord.level));
        pos = result.find("{level}");
    }

    // Replace {x}
    pos = result.find("{x}");
    while (pos != std::string::npos) {
        result.replace(pos, 3, std::to_string(coord.x));
        pos = result.find("{x}");
    }

    // Replace {y}
    pos = result.find("{y}");
    while (pos != std::string::npos) {
        result.replace(pos, 3, std::to_string(coord.y));
        pos = result.find("{y}");
    }

    // Replace {z}
    pos = result.find("{z}");
    while (pos != std::string::npos) {
        result.replace(pos, 3, std::to_string(coord.z));
        pos = result.find("{z}");
    }

    return result;
}

namespace {

/// Calculate the Morton index for a tile within a subtree level
/// Morton order interleaves bits: for 2D, z = ...y1x1y0x0
uint64_t mortonIndex2D(int x, int y) {
    uint64_t z = 0;
    for (int i = 0; i < 32; ++i) {
        z |= ((uint64_t)(x & (1 << i)) << i) | ((uint64_t)(y & (1 << i)) << (i + 1));
    }
    return z;
}

/// Calculate the Morton index for 3D coordinates
uint64_t mortonIndex3D(int x, int y, int z) {
    uint64_t m = 0;
    for (int i = 0; i < 21; ++i) {
        m |= ((uint64_t)(x & (1 << i)) << (2 * i)) |
             ((uint64_t)(y & (1 << i)) << (2 * i + 1)) |
             ((uint64_t)(z & (1 << i)) << (2 * i + 2));
    }
    return m;
}

/// Calculate number of tiles at each level of a subtree
/// For quadtree: level 0 = 1, level 1 = 4, level 2 = 16, etc. Total = (4^n - 1) / 3
/// For octree: level 0 = 1, level 1 = 8, level 2 = 64, etc. Total = (8^n - 1) / 7
uint64_t tilesBeforeLevel(int level, SubdivisionScheme scheme) {
    if (level <= 0) return 0;
    if (scheme == SubdivisionScheme::QUADTREE) {
        // Sum of 4^0 + 4^1 + ... + 4^(level-1) = (4^level - 1) / 3
        return ((1ULL << (2 * level)) - 1) / 3;
    } else {
        // Sum of 8^0 + 8^1 + ... + 8^(level-1) = (8^level - 1) / 7
        return ((1ULL << (3 * level)) - 1) / 7;
    }
}

/// Get the bit index in the availability bitstream for a tile
uint64_t getTileAvailabilityIndex(const TileCoord& localCoord, SubdivisionScheme scheme) {
    uint64_t tilesBeforeThisLevel = tilesBeforeLevel(localCoord.level, scheme);
    uint64_t morton;
    if (scheme == SubdivisionScheme::QUADTREE) {
        morton = mortonIndex2D(localCoord.x, localCoord.y);
    } else {
        morton = mortonIndex3D(localCoord.x, localCoord.y, localCoord.z);
    }
    return tilesBeforeThisLevel + morton;
}

/// Check if a bit is set in a bitstream
bool isBitSet(const std::vector<uint8_t>& bitstream, uint64_t bitIndex) {
    uint64_t byteIndex = bitIndex / 8;
    uint8_t bitOffset = bitIndex % 8;
    if (byteIndex >= bitstream.size()) {
        return false;
    }
    return (bitstream[byteIndex] & (1 << bitOffset)) != 0;
}

/// Subtree binary format header (from 3D Tiles spec)
struct SubtreeHeader {
    uint8_t magic[4];        // "subt"
    uint32_t version;        // 1
    uint64_t jsonByteLength;
    uint64_t binaryByteLength;
};

/// Parse a subtree file and extract tile/content availability
struct SubtreeAvailability {
    std::vector<uint8_t> tileAvailability;
    std::vector<uint8_t> contentAvailability;
    std::vector<uint8_t> childSubtreeAvailability;
    bool tileAvailabilityConstant = false;
    bool tileAvailabilityConstantValue = false;
    bool contentAvailabilityConstant = false;
    bool contentAvailabilityConstantValue = false;
    bool childSubtreeAvailabilityConstant = false;
    bool childSubtreeAvailabilityConstantValue = false;
};

bool parseSubtree(const std::vector<uint8_t>& data, SubtreeAvailability& availability, int subtreeLevels, SubdivisionScheme scheme) {
    if (data.size() < sizeof(SubtreeHeader)) {
        TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Subtree data too small\n");
        return false;
    }

    const SubtreeHeader* header = reinterpret_cast<const SubtreeHeader*>(data.data());

    // Check magic
    if (header->magic[0] != 's' || header->magic[1] != 'u' ||
        header->magic[2] != 'b' || header->magic[3] != 't') {
        TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Invalid subtree magic\n");
        return false;
    }

    // Parse JSON portion
    size_t jsonStart = sizeof(SubtreeHeader);
    size_t jsonEnd = jsonStart + header->jsonByteLength;
    if (jsonEnd > data.size()) {
        TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Invalid subtree JSON length\n");
        return false;
    }

    std::string jsonStr(reinterpret_cast<const char*>(data.data() + jsonStart), header->jsonByteLength);

    JsParseError parseError;
    JsValue json = JsParseString(jsonStr, &parseError);
    if (json.IsNull() || !json.IsObject()) {
        TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Failed to parse subtree JSON\n");
        return false;
    }

    const JsObject& obj = json.GetJsObject();
    size_t binaryStart = jsonEnd;

    // Helper to read availability (either constant or bitstream)
    auto parseAvailability = [&](const std::string& fieldName, std::vector<uint8_t>& bitstreamOut,
                                  bool& isConstant, bool& constantValue) -> bool {
        auto it = obj.find(fieldName);
        if (it == obj.end() || !it->second.IsObject()) {
            return false;
        }
        const JsObject& availObj = it->second.GetJsObject();

        // Check for constant value
        auto constIt = availObj.find("constant");
        if (constIt != availObj.end() && constIt->second.IsInt()) {
            isConstant = true;
            constantValue = (constIt->second.GetInt() == 1);
            return true;
        }

        // Otherwise, read bitstream from buffer view
        auto bitstreamIt = availObj.find("bitstream");
        if (bitstreamIt == availObj.end() || !bitstreamIt->second.IsInt()) {
            return false;
        }
        int bufferViewIndex = bitstreamIt->second.GetInt();

        // Get buffer views
        auto bufferViewsIt = obj.find("bufferViews");
        if (bufferViewsIt == obj.end() || !bufferViewsIt->second.IsArray()) {
            return false;
        }
        const JsArray& bufferViews = bufferViewsIt->second.GetJsArray();
        if (bufferViewIndex < 0 || static_cast<size_t>(bufferViewIndex) >= bufferViews.size()) {
            return false;
        }

        const JsValue& bvVal = bufferViews[bufferViewIndex];
        if (!bvVal.IsObject()) {
            return false;
        }
        const JsObject& bv = bvVal.GetJsObject();

        auto byteOffsetIt = bv.find("byteOffset");
        auto byteLengthIt = bv.find("byteLength");
        if (byteLengthIt == bv.end() || !byteLengthIt->second.IsInt()) {
            return false;
        }
        size_t byteOffset = 0;
        if (byteOffsetIt != bv.end() && byteOffsetIt->second.IsInt()) {
            byteOffset = byteOffsetIt->second.GetInt();
        }
        size_t byteLength = byteLengthIt->second.GetInt();

        // Extract bitstream from binary data
        size_t start = binaryStart + byteOffset;
        if (start + byteLength > data.size()) {
            return false;
        }
        bitstreamOut.assign(data.begin() + start, data.begin() + start + byteLength);
        isConstant = false;
        return true;
    };

    // Parse tile availability (required)
    if (!parseAvailability("tileAvailability", availability.tileAvailability,
                           availability.tileAvailabilityConstant, availability.tileAvailabilityConstantValue)) {
        TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Failed to parse tileAvailability\n");
        return false;
    }

    // Parse content availability (optional - may not exist if no content)
    auto contentAvailIt = obj.find("contentAvailability");
    if (contentAvailIt != obj.end()) {
        if (contentAvailIt->second.IsArray()) {
            // Multiple content availabilities (for tiles with multiple contents)
            const JsArray& contentArr = contentAvailIt->second.GetJsArray();
            if (!contentArr.empty() && contentArr[0].IsObject()) {
                // Just use the first content availability for now
                const JsObject& firstContent = contentArr[0].GetJsObject();
                auto constIt = firstContent.find("constant");
                if (constIt != firstContent.end() && constIt->second.IsInt()) {
                    availability.contentAvailabilityConstant = true;
                    availability.contentAvailabilityConstantValue = (constIt->second.GetInt() == 1);
                }
            }
        } else if (contentAvailIt->second.IsObject()) {
            parseAvailability("contentAvailability", availability.contentAvailability,
                              availability.contentAvailabilityConstant, availability.contentAvailabilityConstantValue);
        }
    }

    // Parse childSubtree availability (optional)
    parseAvailability("childSubtreeAvailability", availability.childSubtreeAvailability,
                      availability.childSubtreeAvailabilityConstant,
                      availability.childSubtreeAvailabilityConstantValue);

    return true;
}

/// Check if a child subtree exists for a tile in the subtree
bool isChildSubtreeAvailable(const SubtreeAvailability& avail, const TileCoord& localCoord, SubdivisionScheme scheme) {
    if (avail.childSubtreeAvailabilityConstant) {
        return avail.childSubtreeAvailabilityConstantValue;
    }
    if (avail.childSubtreeAvailability.empty()) {
        return false;
    }
    uint64_t bitIndex = getTileAvailabilityIndex(localCoord, scheme);
    return isBitSet(avail.childSubtreeAvailability, bitIndex);
}

/// Check if a tile is available in the subtree
bool isTileAvailable(const SubtreeAvailability& avail, const TileCoord& localCoord, SubdivisionScheme scheme) {
    if (avail.tileAvailabilityConstant) {
        return avail.tileAvailabilityConstantValue;
    }
    uint64_t bitIndex = getTileAvailabilityIndex(localCoord, scheme);
    return isBitSet(avail.tileAvailability, bitIndex);
}

/// Check if content is available for a tile in the subtree
bool isContentAvailable(const SubtreeAvailability& avail, const TileCoord& localCoord, SubdivisionScheme scheme) {
    if (avail.contentAvailabilityConstant) {
        return avail.contentAvailabilityConstantValue;
    }
    if (avail.contentAvailability.empty()) {
        // If no content availability, assume content exists wherever tiles exist
        return isTileAvailable(avail, localCoord, scheme);
    }
    uint64_t bitIndex = getTileAvailabilityIndex(localCoord, scheme);
    return isBitSet(avail.contentAvailability, bitIndex);
}

/// Process a subtree and collect all GLB content references
void processSubtree(
    const TileCoord& subtreeRoot,
    const std::string& subtreePath,
    const ImplicitTiling& implicit,
    const std::string& baseDir,
    const SubtreeLoader& loadSubtree,
    const GfMatrix4d& worldRoot,
    const std::string& contentTemplate,
    std::vector<GlbReference>& refs)
{
    // Load subtree file
    std::string subtreeUri = expandTemplateUri(implicit.subtreesUri, subtreeRoot);
    std::string fullSubtreeUri = baseDir + subtreeUri;

    TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Loading subtree: %s\n", fullSubtreeUri.c_str());

    std::vector<uint8_t> subtreeData = loadSubtree(fullSubtreeUri);
    if (subtreeData.empty()) {
        TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Failed to load subtree: %s\n", fullSubtreeUri.c_str());
        return;
    }

    SubtreeAvailability availability;
    if (!parseSubtree(subtreeData, availability, implicit.subtreeLevels, implicit.subdivisionScheme)) {
        TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Failed to parse subtree: %s\n", fullSubtreeUri.c_str());
        return;
    }

    // Iterate through all tiles in this subtree
    for (int localLevel = 0; localLevel < implicit.subtreeLevels; ++localLevel) {
        int globalLevel = subtreeRoot.level + localLevel;
        if (globalLevel >= implicit.availableLevels) break;

        int tilesPerDim = 1 << localLevel;  // 2^localLevel tiles per dimension at this level

        // For octree, iterate in 3D; for quadtree, only 2D (z=0)
        int zMax = (implicit.subdivisionScheme == SubdivisionScheme::OCTREE) ? tilesPerDim : 1;

        for (int lz = 0; lz < zMax; ++lz) {
            for (int ly = 0; ly < tilesPerDim; ++ly) {
                for (int lx = 0; lx < tilesPerDim; ++lx) {
                    TileCoord localCoord{localLevel, lx, ly, lz};

                    if (!isTileAvailable(availability, localCoord, implicit.subdivisionScheme)) {
                        continue;
                    }

                    bool hasContent = isContentAvailable(availability, localCoord, implicit.subdivisionScheme);

                    // Calculate global coordinates
                    int scale = 1 << localLevel;
                    TileCoord globalCoord{
                        globalLevel,
                        subtreeRoot.x * scale + lx,
                        subtreeRoot.y * scale + ly,
                        subtreeRoot.z * scale + lz
                    };

                    // Expand content URI
                    if (hasContent) {
                        std::string contentUri = expandTemplateUri(contentTemplate, globalCoord);

                        // Create prim path
                        std::string primName = glbUriToPrimName(contentUri);
                        std::string primPath = subtreePath + "/L" + std::to_string(globalLevel) +
                                              "_" + std::to_string(globalCoord.x) +
                                              "_" + std::to_string(globalCoord.y);
                        if (implicit.subdivisionScheme == SubdivisionScheme::OCTREE) {
                            primPath += "_" + std::to_string(globalCoord.z);
                        }

                        GlbReference ref;
                        ref.uri = contentUri;
                        ref.transform = worldRoot;  // TODO: Calculate proper transform based on bounding volume
                        ref.primPath = primPath;
                        refs.push_back(ref);

                        TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Found implicit GLB: %s at %s\n",
                                     contentUri.c_str(), primPath.c_str());
                    }

                    // Recurse into child subtrees if available and we have more levels to cover
                    if (localLevel == implicit.subtreeLevels - 1 &&
                        globalLevel + 1 < implicit.availableLevels &&
                        isChildSubtreeAvailable(availability, localCoord, implicit.subdivisionScheme)) {

                        // Child subtree root is one level deeper; coordinates scale by 2
                        int childLevel = globalLevel + 1;
                        TileCoord childRoot{
                            childLevel,
                            globalCoord.x * 2,
                            globalCoord.y * 2,
                            globalCoord.z * 2};

                        processSubtree(childRoot, subtreePath, implicit, baseDir, loadSubtree,
                                       worldRoot, contentTemplate, refs);
                    }
                }
            }
        }
    }
}

}  // anonymous namespace

void collectImplicitGlbContent(
    const Tile& implicitRoot,
    const GfMatrix4d& parentTransform,
    const std::string& parentPath,
    const std::string& baseDir,
    const SubtreeLoader& loadSubtree,
    std::vector<GlbReference>& refs,
    bool skipRootTransform)
{
    if (!implicitRoot.implicitTiling) {
        TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "No implicit tiling configuration\n");
        return;
    }

    const ImplicitTiling& implicit = *implicitRoot.implicitTiling;
    GfMatrix4d rootTransform = implicitRoot.transform.value_or(GfMatrix4d(1.0));
    GfMatrix4d worldRoot = skipRootTransform ? parentTransform : (rootTransform * parentTransform);

    // Get content URI template
    std::string contentTemplate;
    if (implicitRoot.content) {
        contentTemplate = implicitRoot.content->uri;
    } else if (!implicitRoot.contents.empty()) {
        contentTemplate = implicitRoot.contents[0].uri;
    } else {
        TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "No content template for implicit tiling\n");
        return;
    }

    TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Processing implicit tileset: %d levels, subtree levels=%d, template=%s\n",
                 implicit.availableLevels, implicit.subtreeLevels, contentTemplate.c_str());

    // Start with the root subtree at (0, 0, 0)
    TileCoord rootCoord{0, 0, 0, 0};
    processSubtree(rootCoord, parentPath, implicit, baseDir, loadSubtree,
                   worldRoot, contentTemplate, refs);
}

bool parseSubtreeForTesting(
    const std::vector<uint8_t>& data,
    int subtreeLevels,
    SubdivisionScheme scheme,
    SubtreeAvailabilityInfo& info)
{
    SubtreeAvailability availability;
    if (!parseSubtree(data, availability, subtreeLevels, scheme)) {
        return false;
    }

    // Copy availability information
    info.tileAvailabilityConstant = availability.tileAvailabilityConstant;
    info.tileAvailabilityConstantValue = availability.tileAvailabilityConstantValue;
    info.tileAvailabilityBitCount = availability.tileAvailability.size() * 8;

    info.contentAvailabilityConstant = availability.contentAvailabilityConstant;
    info.contentAvailabilityConstantValue = availability.contentAvailabilityConstantValue;
    info.contentAvailabilityBitCount = availability.contentAvailability.size() * 8;

    info.childSubtreeAvailabilityConstant = availability.childSubtreeAvailabilityConstant;
    info.childSubtreeAvailabilityConstantValue = availability.childSubtreeAvailabilityConstantValue;
    info.childSubtreeAvailabilityBitCount = availability.childSubtreeAvailability.size() * 8;

    // Count available tiles
    info.numAvailableTiles = 0;
    info.numAvailableContent = 0;
    info.numAvailableChildSubtrees = 0;

    for (int localLevel = 0; localLevel < subtreeLevels; ++localLevel) {
        int tilesPerDim = 1 << localLevel;

        // For quadtree only (2D)
        for (int ly = 0; ly < tilesPerDim; ++ly) {
            for (int lx = 0; lx < tilesPerDim; ++lx) {
                TileCoord localCoord{localLevel, lx, ly, 0};

                if (isTileAvailable(availability, localCoord, scheme)) {
                    info.numAvailableTiles++;

                    if (isContentAvailable(availability, localCoord, scheme)) {
                        info.numAvailableContent++;
                    }

                    if (localLevel == subtreeLevels - 1 &&
                        isChildSubtreeAvailable(availability, localCoord, scheme)) {
                        info.numAvailableChildSubtrees++;
                    }
                }
            }
        }
    }

    return true;
}

/// Author or reuse CRS prim and bind via BindingAPI
bool
AuthorAndBindTilesetCrs(SdfLayer* layer,
                        const SdfPath& tilesetRootPath,
                        const CoordinateReferenceSystem& crs,
                        SdfAbstractData* data)
{
    if (!layer) {
        TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "No layer; cannot author CRS\n");
        return false;
    }

    // Create CRS prim as a sibling next to the tileset root
    SdfPath crsPath = tilesetRootPath.GetParentPath().AppendChild(TfToken("CRS"));

    // Determine desired WKT token
    TfToken desiredWktToken;
    if (!crs.wkt.empty()) {
        desiredWktToken = TfToken(crs.wkt);
    } else if (!crs.authority.empty() && crs.code > 0) {
        desiredWktToken = TfToken(crs.authority + std::string(":") + std::to_string(crs.code));
    }

    // Author CRS prim if it does not already exist
    VtValue typeNameValue;
    if (!layer->HasField(crsPath, SdfFieldKeys->TypeName, &typeNameValue)) {
        data->CreateSpec(crsPath, SdfSpecTypePrim);
        TfToken typeName("CoordinateReferenceSystem");
        data->Set(crsPath, SdfFieldKeys->TypeName, SdfAbstractDataConstTypedValue(&typeName));
        SdfSpecifier defSpec = SdfSpecifierDef;
        data->Set(crsPath, SdfFieldKeys->Specifier, SdfAbstractDataConstTypedValue(&defSpec));
        // Append to parent children list
        TfToken childName = crsPath.GetNameToken();
        SdfPath parentPath = crsPath.GetParentPath();
        std::vector<TfToken> primChildren;
        SdfAbstractDataTypedValue childGetter(&primChildren);
        (void)data->Has(parentPath, SdfChildrenKeys->PrimChildren, &childGetter);
        primChildren.push_back(childName);
        data->Set(parentPath, SdfChildrenKeys->PrimChildren, SdfAbstractDataConstTypedValue(&primChildren));
    }

    // Author wkt attribute on the CRS prim if provided
    if (!desiredWktToken.IsEmpty()) {
        SdfPath wktPath = crsPath.AppendProperty(UsdGeospatialTokens->crsWkt);
        if (!layer->HasSpec(wktPath)) {
            data->CreateSpec(wktPath, SdfSpecTypeAttribute);
            TfToken typeNameToken = SdfValueTypeNames->Token.GetAsToken();
            data->Set(wktPath, SdfFieldKeys->TypeName, SdfAbstractDataConstTypedValue(&typeNameToken));
            SdfVariability variability = SdfVariabilityUniform;
            data->Set(wktPath, SdfFieldKeys->Variability, SdfAbstractDataConstTypedValue(&variability));
            std::vector<TfToken> propChildren;
            SdfAbstractDataTypedValue propsGetter(&propChildren);
            (void)data->Has(crsPath, SdfChildrenKeys->PropertyChildren, &propsGetter);
            propChildren.push_back(UsdGeospatialTokens->crsWkt);
            data->Set(crsPath, SdfChildrenKeys->PropertyChildren, SdfAbstractDataConstTypedValue(&propChildren));
        }
        data->Set(wktPath, SdfFieldKeys->Default, SdfAbstractDataConstTypedValue(&desiredWktToken));
    }

    // Apply BindingAPI to tileset root and author internal reference to CRS
    addPrimApiSchema(data, tilesetRootPath, UsdGeospatialTokens->BindingAPI);

    // Author a reference from the tileset root to the CRS prim
    SdfReference crsRef(layer->GetIdentifier(), crsPath);
    addPrimReference(data, tilesetRootPath, crsRef);

    TF_DEBUG_MSG(FILE_FORMAT_3DTILES, "Bound CRS %s to tileset root %s (layer-local)\n",
                 crsPath.GetText(), tilesetRootPath.GetText());
    return true;
}

}  // namespace tiles3d
