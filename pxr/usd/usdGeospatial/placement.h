//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_USD_USD_GEOSPATIAL_PLACEMENT_H
#define PXR_USD_USD_GEOSPATIAL_PLACEMENT_H

/// \file usdGeospatial/placement.h

#include "pxr/pxr.h"
#include "pxr/usd/usdGeospatial/api.h"
#include "pxr/usd/usdGeospatial/engine.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quatd.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/token.h"

#include <array>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

class UsdAttribute;

/// \struct UsdGeospatialPlacementInputs
///
/// The authored data that places one directly bound model anchor, read at
/// one time. UsdGeospatialComputePlacementInputs() reads it from a prim;
/// clients without a UsdPrim (e.g. a Hydra scene index) fill it from their
/// own data and call UsdGeospatialPlacementQuery::Resolve() with it.
///
struct UsdGeospatialPlacementInputs
{
    /// The anchor, for messages.
    SdfPath anchorPath;

    /// Why the inputs could not be read; Resolve() reports it as an error.
    std::string error;

    /// The anchor's composed crs:wkt.
    std::string wkt;

    /// crs:position, in the source CRS's coordinate tuple.
    GfVec3d position;

    /// crs:orientation evaluated as a rotation acting on east/north/up
    /// vectors (see UsdGeospatialEvaluateOrientation()).
    GfQuatd attitude = GfQuatd::GetIdentity();

    /// The stage's metersPerUnit and upAxis (UsdGeomTokens->y or ->z).
    double metersPerUnit = 1.0;
    TfToken upAxis;

    /// True if the anchor has an ordinary transform of its own (relative
    /// to its parent), which is not supported yet.
    bool hasOwnTransform = false;
};

/// Reads the placement inputs of the directly bound model anchor \p anchor
/// at \p time. If the binding is broken or crs:position or
/// crs:orientation are missing, blocked or cannot be interpolated, the
/// result's error says why.
USDGEOSPATIAL_API
UsdGeospatialPlacementInputs UsdGeospatialComputePlacementInputs(
    const UsdPrim& anchor, UsdTimeCode time = UsdTimeCode::Default());

/// Evaluates crs:orientation (heading, pitch, roll in degrees) at \p time
/// as a rotation acting on east/north/up vectors: Core value resolution
/// selects the samples, then held interpolation or quaternion slerp along
/// the shorter arc. An unauthored attribute gives the identity. Returns
/// false, with \p error set, for a blocked or non-finite value and for
/// samples 180 degrees apart, whose interpolation path is not unique.
USDGEOSPATIAL_API
bool UsdGeospatialEvaluateOrientation(
    const UsdAttribute& orientation, UsdTimeCode time,
    GfQuatd* attitude, std::string* error);

/// The east/north/up frame at \p lonLatH (degrees, degrees, metres) on the
/// ellipsoid of the geographic CRS described by \p geographic: the matrix
/// from frame coordinates (metres) to geocentric coordinates, in USD's
/// row-vector convention.
USDGEOSPATIAL_API
GfMatrix4d UsdGeospatialComputeEnuToEcef(
    const UsdGeospatialCrsInfo& geographic, const GfVec3d& lonLatH);

/// \class UsdGeospatialPlacement
///
/// A directly bound model anchor resolved into an output CRS: the
/// proposal's model point map F evaluated for one anchor at one time.
///
/// For a point x in the anchor's model frame (stage units, after
/// descendant transforms), the map is
/// \code
///     v = metersPerUnit * axisMap(upAxis) * x        model east/north/up
///     G = ECEF(anchor) + B * R(heading, pitch, roll) * v
///     q = T(G)                                         output CRS
/// \endcode
/// where B is the source ellipsoid's east/north/up basis at the anchor
/// and T the engine's operation into the output CRS. Model distances are
/// physical lengths: projection scale is not applied to the model.
///
/// Results come in two forms. ComputeOutputPoints() evaluates the exact
/// map. GetModelToFrame() is its linearization at the anchor, for drawing:
/// it maps model points into the frame at GetOrigin(), which is metres
/// east/north/up on the output ellipsoid for a geographic output
/// (IsLocalFrame()) and output CRS units relative to the origin otherwise.
///
class UsdGeospatialPlacement
{
public:
    enum class Status {
        /// The anchor is placed.
        Ok,
        /// The engine or this implementation cannot place it; the reason
        /// says why. A reported failure, not a wrong placement.
        Unsupported,
        /// The authored data is invalid (broken binding, missing
        /// position, ...).
        Error
    };

    USDGEOSPATIAL_API
    UsdGeospatialPlacement();

    Status GetStatus() const { return _status; }
    bool IsOk() const { return _status == Status::Ok; }
    const std::string& GetReason() const { return _reason; }
    const SdfPath& GetAnchorPath() const { return _anchorPath; }

    /// The name of the anchor's CRS, once its definition has been read.
    const std::string& GetSourceCrs() const { return _sourceCrs; }

    /// The engine, operations and accuracy (metres, negative if unknown)
    /// behind the result, as the proposal requires every result to state.
    const std::string& GetEngine() const { return _engine; }
    const std::string& GetOperation() const { return _operation; }

    /// More accurate operations the engine skipped because it lacks data
    /// they need (e.g. grid files); see
    /// UsdGeospatialOperationResult::skippedOperations.
    const std::vector<std::string>& GetSkippedOperations() const {
        return _skippedOperations;
    }
    double GetAccuracy() const { return _accuracy; }

    /// The anchor (model origin) in the output CRS's semantic tuple.
    const GfVec3d& GetOrigin() const { return _origin; }

    /// True if GetModelToFrame() maps into metres east/north/up at the
    /// origin (geographic output), false if into output CRS units relative
    /// to the origin.
    bool IsLocalFrame() const { return _localFrame; }

    /// Linearization at the anchor of the map from model points to frame
    /// coordinates, in USD's row-vector convention (point * matrix). Its
    /// translation is zero.
    const GfMatrix4d& GetModelToFrame() const { return _modelToFrame; }

    /// Exact output CRS coordinates of \p modelPoints. Returns false, with
    /// \p error set, if this placement is not Ok or the engine fails.
    USDGEOSPATIAL_API
    bool ComputeOutputPoints(const std::vector<GfVec3d>& modelPoints,
                             std::vector<GfVec3d>* outputPoints,
                             std::string* error = nullptr) const;

    /// Exact frame coordinates of \p modelPoints, in the frame of
    /// GetModelToFrame(); the difference to the matrix is the error of the
    /// linearization.
    USDGEOSPATIAL_API
    bool ComputeFramePoints(const std::vector<GfVec3d>& modelPoints,
                            std::vector<GfVec3d>* framePoints,
                            std::string* error = nullptr) const;

private:
    friend class UsdGeospatialPlacementQuery;

    // Source-datum ECEF points to output CRS coordinates.
    bool _ToOutput(const std::vector<GfVec3d>& ecef,
                   std::vector<GfVec3d>* output, std::string* error) const;

    Status _status = Status::Error;
    std::string _reason;
    SdfPath _anchorPath;
    std::string _sourceCrs;
    std::string _engine;
    std::string _operation;
    std::vector<std::string> _skippedOperations;
    double _accuracy = -1.0;
    GfVec3d _origin;
    bool _localFrame = false;
    GfMatrix4d _modelToFrame;

    // Evaluation state.
    UsdGeospatialCoordinateOperationEngineSharedPtr _engineImpl;
    UsdGeospatialAreaOfInterest _area;
    std::string _sourceGeographic;
    std::string _output;
    double _srcA = 0.0, _srcE2 = 0.0;
    double _outA = 0.0, _outE2 = 0.0;
    GfVec3d _originEcef;                  // source datum
    std::array<GfVec3d, 3> _modelColumns; // model (stage units) -> ECEF
    GfVec3d _originOutEcef;               // output datum, local frame only
    std::array<GfVec3d, 3> _outBasis;     // output ENU at origin
};

/// \class UsdGeospatialPlacementQuery
///
/// Resolves directly bound model anchors into one output CRS with one
/// coordinate operation engine. The output CRS is chosen by the consumer
/// (proposal requirement 16) and must be 3D; a 2D geographic or projected
/// output CRS is promoted to 3D with ellipsoidal heights when the engine
/// supports it (UsdGeospatialCrsInfo::promoted3D).
///
class UsdGeospatialPlacementQuery
{
public:
    /// \p output is a CRS definition the engine accepts. \p engineName
    /// selects an engine from UsdGeospatialEngineRegistry; empty for the
    /// active engine.
    USDGEOSPATIAL_API
    explicit UsdGeospatialPlacementQuery(
        const std::string& output,
        const std::string& engineName = std::string());

    /// False if there is no engine or the output CRS cannot be used.
    bool IsValid() const { return _error.empty(); }
    const std::string& GetError() const { return _error; }
    const UsdGeospatialCrsInfo& GetOutputInfo() const { return _outputInfo; }

    /// The output CRS definition used: the one given, or its 3D promotion.
    const std::string& GetOutput() const { return _output; }

    /// Resolves the directly bound model anchor \p anchor at \p time.
    USDGEOSPATIAL_API
    UsdGeospatialPlacement Resolve(
        const UsdPrim& anchor,
        UsdTimeCode time = UsdTimeCode::Default()) const;

    /// Resolves a model anchor from its placement inputs.
    USDGEOSPATIAL_API
    UsdGeospatialPlacement Resolve(
        const UsdGeospatialPlacementInputs& inputs) const;

private:
    std::string _output;
    UsdGeospatialCoordinateOperationEngineSharedPtr _engine;
    UsdGeospatialCrsInfo _outputInfo;
    std::string _error;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_USD_USD_GEOSPATIAL_PLACEMENT_H
