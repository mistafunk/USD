#!/pxrpythonsubst
#
# Copyright 2026 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

import unittest

from pxr import Gf, Sdf, Tf, Usd, UsdGeom, UsdGeospatial


class TestUsdGeospatialSchemas(unittest.TestCase):

    def test_Registration(self):
        reg = Usd.SchemaRegistry()
        crsType = Tf.Type.FindByName("UsdGeospatialCRS")
        apiType = Tf.Type.FindByName("UsdGeospatialCRSBindingAPI")
        self.assertFalse(crsType.isUnknown)
        self.assertFalse(apiType.isUnknown)
        self.assertEqual(reg.GetSchemaTypeName(crsType), "GeospatialCRS")
        self.assertEqual(reg.GetSchemaTypeName(apiType),
                         "GeospatialCRSBindingAPI")
        self.assertTrue(reg.IsConcrete(crsType))
        self.assertEqual(reg.GetSchemaKind(apiType),
                         Usd.SchemaKind.SingleApplyAPI)

    def test_Definitions(self):
        reg = Usd.SchemaRegistry()
        crsDef = reg.FindConcretePrimDefinition("GeospatialCRS")
        self.assertIsNotNone(crsDef)
        wkt = crsDef.GetAttributeDefinition("crs:wkt")
        self.assertTrue(wkt)
        self.assertEqual(wkt.GetTypeName(), Sdf.ValueTypeNames.Token)
        self.assertEqual(wkt.GetVariability(), Sdf.VariabilityUniform)

        apiDef = reg.FindAppliedAPIPrimDefinition("GeospatialCRSBindingAPI")
        self.assertIsNotNone(apiDef)
        position = apiDef.GetAttributeDefinition("crs:position")
        orientation = apiDef.GetAttributeDefinition("crs:orientation")
        self.assertEqual(position.GetTypeName(), Sdf.ValueTypeNames.Double3)
        self.assertEqual(orientation.GetTypeName(),
                         Sdf.ValueTypeNames.Double3)
        self.assertEqual(position.GetVariability(), Sdf.VariabilityVarying)
        # Position has no fallback; orientation defaults to identity.
        self.assertIsNone(position.GetFallbackValue())
        self.assertEqual(orientation.GetFallbackValue(), Gf.Vec3d(0, 0, 0))

        self.assertEqual(UsdGeospatial.Tokens.crsWkt, "crs:wkt")
        self.assertEqual(UsdGeospatial.Tokens.crsPosition, "crs:position")
        self.assertEqual(UsdGeospatial.Tokens.crsOrientation,
                         "crs:orientation")

    def test_Authoring(self):
        stage = Usd.Stage.CreateInMemory()
        crs = UsdGeospatial.CRS.Define(stage, "/CRS/Utm")
        self.assertTrue(crs)
        crs.CreateWktAttr("PROJCRS[\"example\"]")
        self.assertEqual(crs.GetWktAttr().Get(), "PROJCRS[\"example\"]")

        anchor = UsdGeom.Xform.Define(stage, "/Model").GetPrim()
        self.assertTrue(UsdGeospatial.CRSBindingAPI.CanApply(anchor))
        api = UsdGeospatial.CRSBindingAPI.Apply(anchor)
        self.assertTrue(api)
        self.assertTrue(anchor.HasAPI(UsdGeospatial.CRSBindingAPI))
        self.assertIn("GeospatialCRSBindingAPI", anchor.GetAppliedSchemas())
        # Fallbacks through the API.
        self.assertIsNone(api.GetPositionAttr().Get())
        self.assertEqual(api.GetOrientationAttr().Get(), Gf.Vec3d(0, 0, 0))
        api.CreatePositionAttr(Gf.Vec3d(500000.0, 4000000.0, 12.5))
        self.assertEqual(api.GetPositionAttr().Get(),
                         Gf.Vec3d(500000.0, 4000000.0, 12.5))

        # The binding applies to Xformables only.
        scope = UsdGeom.Scope.Define(stage, "/Scope").GetPrim()
        self.assertFalse(UsdGeospatial.CRSBindingAPI.CanApply(scope))
        self.assertFalse(
            UsdGeospatial.CRSBindingAPI.CanApply(crs.GetPrim()))

    def test_CrsLibrary(self):
        stage = Usd.Stage.Open("crsLibrary.usda")
        names = ["WGS84_UTM11N", "EiffelTower_LTP", "CA5_NAVD88_ftUS",
                 "ITRF2020"]
        for name in names:
            prim = stage.GetPrimAtPath("/CRS/" + name)
            self.assertTrue(prim.IsA(UsdGeospatial.CRS), name)
            wkt = UsdGeospatial.CRS(prim).GetWktAttr().Get()
            self.assertTrue(wkt, name)
        self.assertTrue(UsdGeospatial.CRS(
            stage.GetPrimAtPath("/CRS/WGS84_UTM11N")).GetWktAttr().Get()
            .startswith('PROJCRS["WGS 84 / UTM zone 11N"'))

    def test_EiffelTower(self):
        stage = Usd.Stage.Open("eiffelTower.usda")
        self.assertEqual(UsdGeom.GetStageUpAxis(stage), UsdGeom.Tokens.y)
        self.assertEqual(UsdGeom.GetStageMetersPerUnit(stage), 1.0)

        anchor = stage.GetPrimAtPath("/EiffelTower")
        # The local def keeps its type; the reference adds the CRS.
        self.assertEqual(anchor.GetTypeName(), "Xform")
        self.assertTrue(anchor.HasAPI(UsdGeospatial.CRSBindingAPI))
        api = UsdGeospatial.CRSBindingAPI(anchor)
        self.assertEqual(api.GetPositionAttr().Get(), Gf.Vec3d(0, 0, 0))
        self.assertEqual(api.GetOrientationAttr().Get(), Gf.Vec3d(30, 0, 0))

        wkt = anchor.GetAttribute(UsdGeospatial.Tokens.crsWkt).Get()
        self.assertTrue(wkt.startswith(
            'GEODCRS["Local Tangent Plane at Eiffel Tower"'))
        library = Usd.Stage.Open("crsLibrary.usda")
        self.assertEqual(wkt, UsdGeospatial.CRS(library.GetPrimAtPath(
            "/CRS/EiffelTower_LTP")).GetWktAttr().Get())

        # Descendants are not direct bindings; resolving their inherited
        # CRS is the binding resolution's job (milestone C1).
        unbound = stage.GetPrimAtPath("/EiffelTower/Unbound")
        self.assertFalse(unbound.HasAPI(UsdGeospatial.CRSBindingAPI))
        self.assertFalse(unbound.GetAttribute("crs:wkt"))

    def test_Utm11n(self):
        stage = Usd.Stage.Open("utm11n.usda")
        site = stage.GetPrimAtPath("/Site")
        self.assertTrue(site.HasAPI(UsdGeospatial.CRSBindingAPI))
        self.assertIn('ID["EPSG",32611]',
                      site.GetAttribute("crs:wkt").Get())
        self.assertEqual(
            UsdGeospatial.CRSBindingAPI(site).GetPositionAttr().Get(),
            Gf.Vec3d(481957.95, 3768437.85, 0))

        # A nested direct binding is a second anchor with its own CRS.
        survey = stage.GetPrimAtPath("/Site/Survey")
        self.assertTrue(survey.HasAPI(UsdGeospatial.CRSBindingAPI))
        self.assertTrue(survey.GetAttribute("crs:wkt").Get().startswith(
            'COMPOUNDCRS["NAD83 / California zone 5 (ftUS) + NAVD88'))

        # A direct binding whose CRS prim is missing still loads, without a
        # crs:wkt. Resolution must report it, not fall back to /Site's CRS.
        broken = stage.GetPrimAtPath("/Site/Broken")
        self.assertTrue(broken.HasAPI(UsdGeospatial.CRSBindingAPI))
        self.assertFalse(broken.GetAttribute("crs:wkt"))


if __name__ == "__main__":
    unittest.main()
