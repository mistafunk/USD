#
# Copyright 2026 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#
[
    dict(
        SCHEMA_NAME = 'ALL_SCHEMAS',
        LIBRARY_PATH = 'pxr/usdImaging/usdGeospatialImaging',
    ),

    #--------------------------------------------------------------------------
    # usdGeospatialImaging/binding
    dict(
        SCHEMA_NAME = 'Binding',
        SCHEMA_TOKEN = 'geospatialBinding',
        ADD_DEFAULT_LOCATOR = True,
        DOC = '''
            The CRS binding of a prim, published on directly bound prims
            (UsdGeospatialCRSBindingAPI) and inherited by their descendants
            through the flattening scene index: the nearest directly bound
            prim at or above a prim, as in UsdGeospatialResolveBinding.''',
        MEMBERS = [
            ('ALL_MEMBERS', '', dict(ADD_LOCATOR=True)),

            ('anchor', T_PATH,
             dict(DOC='''
                The directly bound model anchor. Always set on a directly
                bound prim, so a nested binding hides the outer one.''')),
        ],
    ),

    #--------------------------------------------------------------------------
    # usdGeospatialImaging/anchor
    dict(
        SCHEMA_NAME = 'Anchor',
        SCHEMA_TOKEN = 'geospatialAnchor',
        ADD_DEFAULT_LOCATOR = True,
        DOC = '''
            The placement inputs of a directly bound model anchor at the
            current time, as read by UsdGeospatialComputePlacementInputs.
            Only on the anchor itself; not inherited.''',
        MEMBERS = [
            ('ALL_MEMBERS', '', dict(ADD_LOCATOR=True)),

            ('error', T_STRING,
             dict(DOC='''
                Why the inputs cannot be read (broken binding, missing or
                blocked crs:position, ...); empty if they can.''')),
            ('crsWkt', T_STRING,
             dict(DOC='''The anchor's composed crs:wkt.''')),
            ('position', T_VEC3D,
             dict(DOC='''crs:position in the source CRS.''')),
            ('orientation', T_VEC3D,
             dict(DOC='''crs:orientation (heading, pitch, roll) as
                authored at the current time, for reporting.''')),
            ('attitude', T_MATRIX,
             dict(DOC='''
                crs:orientation evaluated as a rotation of east/north/up
                vectors (UsdGeospatialEvaluateOrientation; slerped between
                time samples), in USD's row-vector convention.''')),
            ('metersPerUnit', T_DOUBLE, {}),
            ('upAxis', T_TOKEN, {}),
            ('hasOwnTransform', T_BOOL,
             dict(DOC='''
                True if the anchor has an ordinary transform relative to its
                parent (not supported yet).''')),
        ],
    ),

    #--------------------------------------------------------------------------
    # usdGeospatialImaging/placement
    dict(
        SCHEMA_NAME = 'Placement',
        SCHEMA_TOKEN = 'geospatialPlacement',
        ADD_DEFAULT_LOCATOR = True,
        DOC = '''
            The result of placing a prim through its model anchor in the
            output CRS of UsdGeospatialImagingPlacementSceneIndex. Present
            on every prim with a CRS binding.''',
        MEMBERS = [
            ('ALL_MEMBERS', '', dict(ADD_LOCATOR=True)),

            ('status', T_TOKEN,
             dict(DOC='''ok, unsupported or error. Prims that are not ok
                are made invisible: they are reported, not drawn.''')),
            ('reason', T_STRING,
             dict(DOC='''Why the anchor is not placed.''')),
            ('anchor', T_PATH, {}),
            ('engine', T_STRING,
             dict(DOC='''The coordinate operation engine used.''')),
            ('operation', T_STRING,
             dict(DOC='''The coordinate operations used, as the engine
                names them.''')),
            ('accuracy', T_DOUBLE,
             dict(DOC='''Accuracy of the operations in metres; absent if
                unknown.''')),
            ('sourceCrs', T_STRING,
             dict(DOC='''Name of the anchor's CRS.''')),
            ('origin', T_VEC3D,
             dict(DOC='''The anchor in the output CRS.''')),
            ('frame', T_TOKEN,
             dict(DOC='''enu: modelToFrame maps into metres east, north and
                up on the output ellipsoid at origin (geographic output);
                crs: into output CRS units relative to origin.''')),
            ('modelToFrame', T_MATRIX,
             dict(DOC='''
                Linearization at the anchor of the map from this prim's
                local space (stage units) into the frame at origin, in USD's
                row-vector convention: the prim's transform relative to the
                anchor followed by UsdGeospatialPlacement::GetModelToFrame.''')),
            ('linearizationError', T_DOUBLE,
             dict(DOC='''
                For meshes, when measuring is enabled: the largest distance
                over the points between the exact point map and
                modelToFrame, in frame units.''')),
        ],
        EXTRA_TOKENS = [
            'ok',
            'unsupported',
            'error',
            'enu',
            'crs',
        ],
    ),
]
