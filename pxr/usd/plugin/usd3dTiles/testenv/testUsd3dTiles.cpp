#include "pxr/pxr.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primRange.h"
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

void TestTilesetReading() {
    std::string const tilesetPath = "./MetadataGranularities/tileset.json";
    UsdStageRefPtr stage = UsdStage::Open(tilesetPath);
    TF_VERIFY(stage, "Unable to open %s", tilesetPath.c_str());

    if (stage) {
        std::cout << "Traversing stage from: " << tilesetPath << std::endl;
        for (const UsdPrim& prim : stage->Traverse()) {
            std::cout << prim.GetPath() << std::endl;
        }
    }
}

int main() {
    TestTilesetReading();
    return 0;
}
