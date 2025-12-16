#include "pxr/pxr.h"
#include "../tileset.h"
#include <iostream>
#include <fstream>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

void TestSubtreeParsing() {
    std::cout << "\n=== Testing Subtree Parsing ===" << std::endl;

    // Path to the root subtree (0_0_0.subtree)
    std::string const subtreePath = "./3dcitydb_shaders_ingolstadt/subtrees/0_0_0.subtree";

    // Read the subtree file
    std::ifstream file(subtreePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open subtree file: " << subtreePath << std::endl;
        TF_VERIFY(false, "Failed to open subtree file");
        return;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        std::cerr << "Failed to read subtree file" << std::endl;
        TF_VERIFY(false, "Failed to read subtree file");
        return;
    }

    std::cout << "Loaded subtree file: " << subtreePath << " (" << size << " bytes)" << std::endl;

    // Parse the subtree
    // Based on the tileset.json: subtreeLevels = 2, subdivisionScheme = QUADTREE
    tiles3d::SubtreeAvailabilityInfo info;
    bool success = tiles3d::parseSubtreeForTesting(
        buffer,
        2,  // subtreeLevels
        tiles3d::SubdivisionScheme::QUADTREE,
        info);

    TF_VERIFY(success, "Failed to parse subtree");

    if (success) {
        std::cout << "\nSubtree Availability Information:" << std::endl;
        std::cout << "-----------------------------------" << std::endl;

        std::cout << "Tile Availability:" << std::endl;
        if (info.tileAvailabilityConstant) {
            std::cout << "  Constant value: " << (info.tileAvailabilityConstantValue ? "true" : "false") << std::endl;
        } else {
            std::cout << "  Bitstream size: " << info.tileAvailabilityBitCount << " bits" << std::endl;
        }
        std::cout << "  Available tiles: " << info.numAvailableTiles << std::endl;

        std::cout << "\nContent Availability:" << std::endl;
        if (info.contentAvailabilityConstant) {
            std::cout << "  Constant value: " << (info.contentAvailabilityConstantValue ? "true" : "false") << std::endl;
        } else {
            std::cout << "  Bitstream size: " << info.contentAvailabilityBitCount << " bits" << std::endl;
        }
        std::cout << "  Available content: " << info.numAvailableContent << std::endl;

        std::cout << "\nChild Subtree Availability:" << std::endl;
        if (info.childSubtreeAvailabilityConstant) {
            std::cout << "  Constant value: " << (info.childSubtreeAvailabilityConstantValue ? "true" : "false") << std::endl;
        } else {
            std::cout << "  Bitstream size: " << info.childSubtreeAvailabilityBitCount << " bits" << std::endl;
        }
        std::cout << "  Available child subtrees: " << info.numAvailableChildSubtrees << std::endl;

        // Verify expected values for this specific subtree
        // For a 2-level quadtree: level 0 has 1 tile, level 1 has 4 tiles = 5 total
        std::cout << "\nVerification:" << std::endl;
        std::cout << "  Expected max tiles for 2-level quadtree: 5 (1 + 4)" << std::endl;
        TF_VERIFY(info.numAvailableTiles <= 5,
                  "Available tiles (%d) exceeds maximum for 2-level quadtree (5)",
                  info.numAvailableTiles);

        // Content should be <= available tiles
        TF_VERIFY(info.numAvailableContent <= info.numAvailableTiles,
                  "Available content (%d) exceeds available tiles (%d)",
                  info.numAvailableContent, info.numAvailableTiles);

        // Child subtrees should be <= 4 (one per tile at level 1)
        TF_VERIFY(info.numAvailableChildSubtrees <= 4,
                  "Available child subtrees (%d) exceeds maximum (4)",
                  info.numAvailableChildSubtrees);

        std::cout << "  All verification checks passed!" << std::endl;
    }
}

int main() {
    std::cout << "=== 3D Tiles Subtree Parsing Test ===" << std::endl;
    TestSubtreeParsing();
    std::cout << "\nTest completed successfully!" << std::endl;
    return 0;
}
