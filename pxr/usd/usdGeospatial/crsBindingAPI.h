//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDGEOSPATIAL_GENERATED_CRSBINDINGAPI_H
#define USDGEOSPATIAL_GENERATED_CRSBINDINGAPI_H

/// \file usdGeospatial/crsBindingAPI.h

#include "pxr/pxr.h"
#include "pxr/usd/usdGeospatial/api.h"
#include "pxr/usd/usd/apiSchemaBase.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeospatial/tokens.h"

#include "pxr/base/vt/value.h"

#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4d.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE

class SdfAssetPath;

// -------------------------------------------------------------------------- //
// GEOSPATIALCRSBINDINGAPI                                                    //
// -------------------------------------------------------------------------- //

/// \class UsdGeospatialCRSBindingAPI
///
/// Binds model content to a geospatial CRS and places it.
/// 
/// Applied to a UsdGeomXformable prim that composes in a GeospatialCRS prim
/// (usually with a reference), the API makes that prim a directly bound
/// model anchor: crs:position locates the anchor's model origin in the
/// bound CRS and crs:orientation gives the model's physical attitude
/// against the local ellipsoid-normal east/north/up frame at that position.
/// The anchor's own ordinary transform is a post-placement adjustment;
/// descendants keep their ordinary model-local meaning and inherit the
/// binding.
/// 
/// A directly bound prim without a valid composed crs:wkt, or without a
/// finite crs:position, is an error. Resolution never falls back to an
/// ancestor's CRS.
///
class UsdGeospatialCRSBindingAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdGeospatialCRSBindingAPI on UsdPrim \p prim .
    /// Equivalent to UsdGeospatialCRSBindingAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdGeospatialCRSBindingAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdGeospatialCRSBindingAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdGeospatialCRSBindingAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdGeospatialCRSBindingAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    USDGEOSPATIAL_API
    virtual ~UsdGeospatialCRSBindingAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDGEOSPATIAL_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdGeospatialCRSBindingAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdGeospatialCRSBindingAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDGEOSPATIAL_API
    static UsdGeospatialCRSBindingAPI
    Get(const UsdStagePtr &stage, const SdfPath &path);


    /// Returns true if this <b>single-apply</b> API schema can be applied to 
    /// the given \p prim. If this schema can not be a applied to the prim, 
    /// this returns false and, if provided, populates \p whyNot with the 
    /// reason it can not be applied.
    /// 
    /// Note that if CanApply returns false, that does not necessarily imply
    /// that calling Apply will fail. Callers are expected to call CanApply
    /// before calling Apply if they want to ensure that it is valid to 
    /// apply a schema.
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
    USDGEOSPATIAL_API
    static bool 
    CanApply(const UsdPrim &prim, std::string *whyNot=nullptr);

    /// Applies this <b>single-apply</b> API schema to the given \p prim.
    /// This information is stored by adding "GeospatialCRSBindingAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid UsdGeospatialCRSBindingAPI object is returned upon success. 
    /// An invalid (or empty) UsdGeospatialCRSBindingAPI object is returned upon 
    /// failure. See \ref UsdPrim::ApplyAPI() for conditions 
    /// resulting in failure. 
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
    USDGEOSPATIAL_API
    static UsdGeospatialCRSBindingAPI 
    Apply(const UsdPrim &prim);

protected:
    /// Returns the kind of schema this class belongs to.
    ///
    /// \sa UsdSchemaKind
    USDGEOSPATIAL_API
    UsdSchemaKind _GetSchemaKind() const override;

private:
    // needs to invoke _GetStaticTfType.
    friend class UsdSchemaRegistry;
    USDGEOSPATIAL_API
    static const TfType &_GetStaticTfType();

    static bool _IsTypedSchema();

    // override SchemaBase virtuals.
    USDGEOSPATIAL_API
    const TfType &_GetTfType() const override;

public:
    // --------------------------------------------------------------------- //
    // POSITION 
    // --------------------------------------------------------------------- //
    /// Absolute position of the model placement origin, in the
    /// bound CRS's units and height reference: easting, northing, up for a
    /// projected CRS; longitude, latitude, height for a geographic 3D CRS;
    /// X, Y, Z for a geocentric CRS. The tuple order is independent of the
    /// WKT axis order and of the stage upAxis. There is no fallback: a
    /// missing position is an error.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `double3 crs:position` |
    /// | C++ Type | GfVec3d |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Double3 |
    USDGEOSPATIAL_API
    UsdAttribute GetPositionAttr() const;

    /// See GetPositionAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDGEOSPATIAL_API
    UsdAttribute CreatePositionAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // ORIENTATION 
    // --------------------------------------------------------------------- //
    /// Heading, pitch and roll in degrees, in that order: the
    /// model's physical attitude against the ellipsoid-normal east/north/up
    /// frame of the source datum at crs:position. Model forward is +north;
    /// heading is clockwise from true north, pitch raises forward, roll
    /// follows the right-hand rule about forward. In Core row-vector form
    /// the rotation is R_N(roll) * R_E(pitch) * R_U(-heading).
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `double3 crs:orientation = (0, 0, 0)` |
    /// | C++ Type | GfVec3d |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Double3 |
    USDGEOSPATIAL_API
    UsdAttribute GetOrientationAttr() const;

    /// See GetOrientationAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDGEOSPATIAL_API
    UsdAttribute CreateOrientationAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // ===================================================================== //
    // Feel free to add custom code below this line, it will be preserved by 
    // the code generator. 
    //
    // Just remember to: 
    //  - Close the class declaration with }; 
    //  - Close the namespace with PXR_NAMESPACE_CLOSE_SCOPE
    //  - Close the include guard with #endif
    // ===================================================================== //
    // --(BEGIN CUSTOM CODE)--
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
