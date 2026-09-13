import referenceSpec from './style-spec-reference/v8.json' with { type: "json" };

/** @type {any} */
let modifiedReferenceSpec = referenceSpec;

// https://github.com/maplibre/maplibre-native/issues/250
delete modifiedReferenceSpec['layout_symbol']['text-rotation-alignment']["values"]['viewport-glyph']

// https://github.com/maplibre/maplibre-native/issues/251
delete modifiedReferenceSpec['layout_symbol']['icon-overlap'];
delete modifiedReferenceSpec['layout_symbol']['text-overlap'];
modifiedReferenceSpec["layout_symbol"]["text-allow-overlap"]["requires"] = referenceSpec["layout_symbol"]["text-allow-overlap"]["requires"].filter(val => JSON.stringify(val) !== '{"!":"text-overlap"}');
modifiedReferenceSpec["layout_symbol"]["text-allow-overlap"]["requires"] = referenceSpec["layout_symbol"]["icon-allow-overlap"]["requires"].filter(val => JSON.stringify(val) !== '{"!":"icon-overlap"}');

modifiedReferenceSpec.layer.type.values["location-indicator"] = {};
modifiedReferenceSpec["layout_location-indicator"] = {
  "top-image": {
      "type": "resolvedImage",
      "property-type": "data-constant",
      "expression": {
          "interpolated": false,
          "parameters": [
              "zoom"
          ]
      },
      "doc": "Name of image in sprite to use as the top of the location indicator."
  },
  "bearing-image": {
      "type": "resolvedImage",
      "property-type": "data-constant",
      "expression": {
          "interpolated": false,
          "parameters": [
              "zoom"
          ]
      },
      "doc": "Name of image in sprite to use as the middle of the location indicator."
  },
  "shadow-image": {
      "type": "resolvedImage",
      "property-type": "data-constant",
      "expression": {
          "interpolated": false,
          "parameters": [
              "zoom"
          ]
      },
      "doc": "Name of image in sprite to use as the background of the location indicator."
  }
};

modifiedReferenceSpec["paint_location-indicator"] = {
  "perspective-compensation": {
      "type": "number",
      "default": "0.85",
      "property-type": "data-constant",
      "expression": {
          "interpolated": true,
          "parameters": [
              "zoom"
          ]
      },
      "doc": "The amount of the perspective compensation, between 0 and 1. A value of 1 produces a location indicator of constant width across the screen. A value of 0 makes it scale naturally according to the viewing projection."
  },
  "image-tilt-displacement": {
      "type": "number",
      "property-type": "data-constant",
      "default": "0",
      "units": "pixels",
      "expression": {
          "interpolated": true,
          "parameters": [
              "zoom"
          ]
      },
      "doc": "The displacement off the center of the top image and the shadow image when the pitch of the map is greater than 0. This helps producing a three-dimensional appearance."
  },
  "bearing": {
      "type": "number",
      "default": 0,
      "period": 360,
      "units": "degrees",
      "property-type": "data-constant",
      "expression": {
          "interpolated": false,
          "parameters": [ ]
      },
      "transition": true,
      "doc": "The bearing of the location indicator."
  },
  "location": {
      "type": "array",
      "default": [
          0.0,
          0.0,
          0.0
      ],
      "length": 3,
      "value": "number",
      "property-type": "data-constant",
      "expression": {
          "interpolated": true,
          "parameters": []
      },
      "transition": true,
      "doc": "An array of [latitude, longitude, altitude] position of the location indicator."
  },
  "accuracy-radius": {
      "type": "number",
      "units": "meters",
      "default": 0,
      "property-type": "data-constant",
      "expression": {
          "interpolated": true,
          "parameters": [
              "zoom"
          ]
      },
      "transition": true,
      "doc": "The accuracy, in meters, of the position source used to retrieve the position of the location indicator."
  },
  "top-image-size": {
      "type": "number",
      "units": "factor of the original icon size",
      "property-type": "data-constant",
      "default": 1,
      "expression": {
          "interpolated": true,
          "parameters": [
              "zoom"
          ]
      },
      "transition": true,
      "doc": "The size of the top image, as a scale factor applied to the size of the specified image."
  },
  "bearing-image-size": {
      "type": "number",
      "units": "factor of the original icon size",
      "property-type": "data-constant",
      "default": 1,
      "expression": {
          "interpolated": true,
          "parameters": [
              "zoom"
          ]
      },
      "transition": true,
      "doc": "The size of the bearing image, as a scale factor applied to the size of the specified image."
  },
  "shadow-image-size": {
      "type": "number",
      "units": "factor of the original icon size",
      "property-type": "data-constant",
      "default": 1,
      "expression": {
          "interpolated": true,
          "parameters": [
              "zoom"
          ]
      },
      "transition": true,
      "doc": "The size of the shadow image, as a scale factor applied to the size of the specified image."
  },
  "accuracy-radius-color": {
      "type": "color",
      "property-type": "data-constant",
      "default": "#ffffff",
      "expression": {
          "interpolated": true,
          "parameters": [
              "zoom"
          ]
      },
      "transition": true,
      "doc": "The color for drawing the accuracy radius, as a circle. To adjust transparency, set the alpha component of the color accordingly."

  },
  "accuracy-radius-border-color": {
      "type": "color",
      "property-type": "data-constant",
      "default": "#ffffff",
      "expression": {
          "interpolated": true,
          "parameters": [
              "zoom"
          ]
      },
      "transition": true,
      "doc": "The color for drawing the accuracy radius border. To adjust transparency, set the alpha component of the color accordingly."
  }
};

// internal use
modifiedReferenceSpec["layout_symbol"]["symbol-screen-space"] = {
    "type": "boolean",
    "default": false,
    "property-type": "data-constant",
    "doc": "Internal use only"
};

// DuckMaps fork only, not upstream: an elevated ribbon line drawn in real 3D world space on
// the terrain surface (never draped), for trails/routes/tracks. Added the same way
// "location-indicator" is above - as an override here rather than a hand-edit of the vendored
// scripts/style-spec-reference/v8.json, so that file stays a clean mirror of upstream and future
// upstream syncs never conflict with a fork-only layer type. One ribbon, one colour, one width,
// one dash per layer (the family passes the web engine draws in one custom layer become several
// styled instances of this layer type instead - see docs/plans/2026-09-11-engine-layer-plumbing.md).
// All twelve paint properties are non-data-driven ("data-constant": constant-or-zoom-interpolatable
// only, PropertyValue<T>, never DataDrivenPropertyValue<T> - no paint vertex attributes/binders).
modifiedReferenceSpec.layer.type.values["terrain-line"] = {
  "doc": "An elevated ribbon line (trail/route/track) drawn in real 3D world space on the terrain, DuckMaps fork only."
};

modifiedReferenceSpec["layout_terrain-line"] = {
  "visibility": {
      "type": "enum",
      "values": {
        "visible": { "doc": "The layer is shown." },
        "none": { "doc": "The layer is not shown." }
      },
      "default": "visible",
      "doc": "Whether this layer is displayed.",
      "property-type": "constant"
  }
};

modifiedReferenceSpec["paint_terrain-line"] = {
  "terrain-line-color": {
      "type": "color",
      "default": "#000000",
      "transition": true,
      "doc": "The ribbon colour.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-line-opacity": {
      "type": "number",
      "default": 1,
      "minimum": 0,
      "maximum": 1,
      "transition": true,
      "doc": "Alpha multiplier for the ribbon (shader u_alpha).",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-line-width": {
      "type": "number",
      "default": 1,
      "minimum": 0,
      "units": "pixels",
      "transition": true,
      "doc": "Full ribbon width in CSS pixels (points) at the map centre - the same coordinate space PaintParameters::units_to_pixels operates in (state.getSize()'s LOGICAL size, not the device-pixel framebuffer). Half-width, still in CSS pixels, is shader u_half_px; task 2.2b removed a pixelRatio multiply here that had made every ribbon pixelRatio times too wide on screen.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-line-blur": {
      "type": "number",
      // Task: feather-vs-geometry settlement (12 Sept 2026), superseding task 2.2b's own fixed
      // 0.5 CSS px default below (kept in history for the record of why that number was chosen,
      // and why it turned out not to be enough): a style-spec default cannot read the device's
      // own pixelRatio, so ANY fixed CSS-point number here renders as a DIFFERENT device-pixel
      // feather on every dpr - 0.5 CSS px was tuned to look right at dpr 2 and came out three
      // times too wide (1.5 device px) at dpr 3 (an iPhone), thinning every ribbon's pure-colour
      // core well below the ordinary flat `line` layer's own antialiasing edge, which is pinned
      // to exactly half a device pixel at ANY pixelRatio (line.vertex.glsl's own
      // `ANTIALIASING = 1.0 / DEVICE_PIXEL_RATIO / 2.0`) because it is computed in the SHADER,
      // which does know the live pixelRatio, rather than baked into a style constant that does
      // not. The fix moves that always-on, pixelRatio-correct minimum into
      // TerrainLineLayerTweaker::execute (terrain_line_layer_tweaker.cpp) instead, which adds it
      // to whatever this property evaluates to every frame. This property's own default is
      // therefore 0 now, matching line-blur's own default exactly: it is purely an OPTIONAL
      // extra amount of blur a style author asks for on top of the always-on minimum, not a way
      // to set the minimum itself - the halo continues to pass the flat style's own line-blur
      // value through unchanged (container/server/app/map/native_lines.py), now landing as
      // additional blur on top of the same runtime-correct minimum the body gets.
      //
      // Task 2.2b's own comment, for the record: "default lowered from 1 to 0.5 CSS pixel...
      // 0.5 CSS px is chosen as the closest single value to the web's look on the most common
      // target (dpr 2, where it matches exactly)... the old default of 1... fed into the
      // shader's `a = clamp((half_px - d) / edge_px + 0.5, 0, 1)` coverage ramp and washed a
      // body-width ribbon out to little more than its own feather."
      "default": 0,
      "minimum": 0,
      "units": "pixels",
      "transition": true,
      "doc": "Additional anti-aliasing edge feather in CSS pixels (points), matching terrain-line-width's own units (shader u_edge_px), ON TOP OF an always-on, pixelRatio-correct minimum of half a device pixel added at runtime (see this property's own comment). 0 (the default) is a crisp line at that runtime minimum; a larger value asks for visibly more blur, e.g. for a halo layer.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-line-dasharray": {
      "type": "array",
      "value": "number",
      // Task 2.2b: no "length": 2 here any more. It used to be a fixed-length
      // std::array<float, 2>, deliberately typed differently from line-dasharray
      // (std::vector<float>) per 2.2a's own design note - but that is exactly what made the
      // darwin peer generator's mbglType()/arrayType() (which hardcode every "*-dasharray"-named
      // property to std::vector<float>, matching line-dasharray) fail to generate
      // MLNTerrainLineStyleLayer for this one property (task 2.2b-i, reverted in commit
      // 1246a22d; the other eight properties generated cleanly). Typing it std::vector<float>
      // here instead - a plain, non-cross-faded, non-data-driven PaintProperty<std::vector<float>>,
      // exactly like line-dasharray's own evaluated type, just without the cross-fade machinery -
      // matches the darwin generator's existing hardcoded assumption for free, unblocking the
      // peer (see platform/darwin/scripts/generate-style-code.mjs). The tweaker
      // (TerrainLineLayerTweaker::execute) reads only the first two entries as [on, off] and
      // ignores anything past index 1 (a vector of length 0 or 1 is treated as "no dash", the
      // same as [0, 0] - see the tweaker's comment at the point that reads this property).
      // [0, 0] rather than [] as the default for the same ambiguous-constructor reason 2.2a's
      // note described (still applies to a length-less array's PropertyValue<T> constant vs.
      // expression overload resolution).
      "default": [0, 0],
      "minimum": 0,
      "units": "line widths",
      "transition": true,
      "doc": "Dash on/off lengths in width units, exactly like line-dasharray. Only the first two entries are read (on, off); [0, 0] (the default) means a continuous line.",
      "expression": {
          "interpolated": false,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-line-offset": {
      "type": "number",
      "default": 0,
      "units": "pixels",
      "transition": true,
      "doc": "Constant sideways offset in CSS pixels (points), matching terrain-line-width's own units (shader u_rail_offset), for the track ladder's two rails.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-line-ghost-opacity": {
      "type": "number",
      "default": 0,
      "minimum": 0,
      "maximum": 1,
      "transition": true,
      "doc": "Alpha for fragments behind the terrain, once terrain occlusion is added. Parsed and evaluated in 2.2a but has no effect in the shader yet - occlusion testing is deferred to 2.2b.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-line-fade": {
      "type": "number",
      "default": 0,
      "minimum": 0,
      "maximum": 1,
      "transition": true,
      "doc": "Distance-fade amount, 0 to 1, once distance fade is added. Parsed and evaluated in 2.2a but has no effect in the shader yet - distance fade is deferred to 2.2b.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-line-fade-distance": {
      "type": "number",
      "default": 8000,
      "minimum": 0,
      "units": "meters",
      "transition": true,
      "doc": "Metres at which terrain-line-fade reaches full effect, once distance fade is added. Parsed and evaluated in 2.2a but has no effect in the shader yet - distance fade is deferred to 2.2b.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  // The halo, ported from the web engine's second drawFamilyPasses pass (assets/routes3d.js
  // ~1084-1101 in the server repo): same ribbon geometry, a wider u_half_px, its own colour and
  // its own u_edge_px, composited under the body in one fragment-shader pass rather than drawn as
  // a second layer. Defaulted so that a style which sets none of the three renders identically to
  // today: halo-width 0 makes the halo pass contribute nothing (see terrain_line.hpp's
  // fragmentMain), so halo-color's own default is irrelevant until halo-width is set.
  "terrain-line-halo-color": {
      "type": "color",
      "default": "#000000",
      "transition": true,
      "doc": "The halo colour, drawn under the ribbon body. Irrelevant while terrain-line-halo-width is 0.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-line-halo-width": {
      "type": "number",
      "default": 0,
      "minimum": 0,
      "units": "pixels",
      "transition": true,
      "doc": "Full halo width in CSS pixels (points), same units and same half-width convention as terrain-line-width. 0 (the default) draws no halo.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-line-halo-blur": {
      "type": "number",
      "default": 0,
      "minimum": 0,
      "units": "pixels",
      "transition": true,
      "doc": "Additional anti-aliasing edge feather for the halo, in CSS pixels (points) - same meaning and default as terrain-line-blur.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  }
};

// DuckMaps fork only, task 2.4a: a contour-line layer computed per-fragment from the terrain
// DEM itself, drawn over RenderTerrain's own mesh (renderToTerrain=false, real 3D world space,
// never draped) - ported from the web engine's contours3d.js (see
// container/server/app/map/assets/contours3d.js and
// docs/plans/2026-09-11-engine-layer-plumbing.md). Like background, this layer has NO source
// and NO bucket/geometry of its own: it reuses RenderTerrain::getMesh()'s shared vertex/index
// buffers directly, one drawable per tile RenderTerrain already has a terrain drawable for. All
// ten paint properties are plain PropertyValue<T> (zoom functions allowed, never data-driven -
// there is no per-feature geometry to drive them from).
modifiedReferenceSpec.layer.type.values["terrain-contour"] = {
  "doc": "Contour lines computed per-fragment from the terrain DEM, drawn in real 3D world space on the terrain surface, DuckMaps fork only."
};

modifiedReferenceSpec["layout_terrain-contour"] = {
  "visibility": {
      "type": "enum",
      "values": {
        "visible": { "doc": "The layer is shown." },
        "none": { "doc": "The layer is not shown." }
      },
      "default": "visible",
      "doc": "Whether this layer is displayed.",
      "property-type": "constant"
  }
};

modifiedReferenceSpec["paint_terrain-contour"] = {
  "terrain-contour-minor-color": {
      "type": "color",
      "default": "hsl(36, 45%, 60%)",
      "transition": true,
      "doc": "Colour of the minor contour lines.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-contour-index-color": {
      "type": "color",
      "default": "hsl(31, 41%, 65%)",
      "transition": true,
      "doc": "Colour of the index contour lines.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-contour-minor-opacity": {
      "type": "number",
      "default": 1,
      "minimum": 0,
      "maximum": 1,
      "transition": true,
      "doc": "Alpha multiplier for the minor contour lines.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-contour-index-opacity": {
      "type": "number",
      "default": 1,
      "minimum": 0,
      "maximum": 1,
      "transition": true,
      "doc": "Alpha multiplier for the index contour lines.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-contour-minor-interval": {
      "type": "number",
      "default": 20,
      "minimum": 0,
      "units": "meters",
      "transition": false,
      "doc": "Vertical interval between minor contour lines, in metres. 0 disables the minor lines.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-contour-index-interval": {
      "type": "number",
      "default": 100,
      "minimum": 0,
      "units": "meters",
      "transition": false,
      "doc": "Vertical interval between index contour lines, in metres. 0 disables the index lines.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-contour-minor-width": {
      "type": "number",
      // DuckMaps: the style spec requires a default, so this number appears here as well as in
      // the one definition it comes from (DuckMaps' style.py CONTOUR3D_*, served at
      // /style-tokens.json and read by the web engine's contours3d.js and by the native style
      // alike). It is a FALLBACK, not a second definition: the DuckMaps style always sets this
      // property explicitly, so this value is never the one that ships. Keep it equal to
      // style.py's if you touch either.
      "default": 1.3,
      "minimum": 0,
      "units": "pixels",
      "transition": false,
      "doc": "Half-width of the minor contour lines, in raw pixels at a reference render ratio of 2 (see terrain-contour-index-width and the tweaker's pixelRatio/2 scale).",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-contour-index-width": {
      "type": "number",
      // DuckMaps: the style spec requires a default, so this number appears here as well as in
      // the one definition it comes from (DuckMaps' style.py CONTOUR3D_*, served at
      // /style-tokens.json and read by the web engine's contours3d.js and by the native style
      // alike). It is a FALLBACK, not a second definition: the DuckMaps style always sets this
      // property explicitly, so this value is never the one that ships. Keep it equal to
      // style.py's if you touch either.
      "default": 2.1,
      "minimum": 0,
      "units": "pixels",
      "transition": false,
      "doc": "Half-width of the index contour lines, in raw pixels at a reference render ratio of 2.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-contour-fade-lo": {
      "type": "number",
      // DuckMaps: the style spec requires a default, so this number appears here as well as in
      // the one definition it comes from (DuckMaps' style.py CONTOUR3D_*, served at
      // /style-tokens.json and read by the web engine's contours3d.js and by the native style
      // alike). It is a FALLBACK, not a second definition: the DuckMaps style always sets this
      // property explicitly, so this value is never the one that ships. Keep it equal to
      // style.py's if you touch either.
      "default": 0.6,
      "minimum": 0,
      "units": "pixels",
      "transition": false,
      "doc": "On-screen line spacing, in pixels at a reference render ratio of 2, below which a contour family is fully faded out.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-contour-fade-hi": {
      "type": "number",
      // DuckMaps: the style spec requires a default, so this number appears here as well as in
      // the one definition it comes from (DuckMaps' style.py CONTOUR3D_*, served at
      // /style-tokens.json and read by the web engine's contours3d.js and by the native style
      // alike). It is a FALLBACK, not a second definition: the DuckMaps style always sets this
      // property explicitly, so this value is never the one that ships. Keep it equal to
      // style.py's if you touch either.
      "default": 2.2,
      "minimum": 0,
      "units": "pixels",
      "transition": false,
      "doc": "On-screen line spacing, in pixels at a reference render ratio of 2, above which a contour family is fully visible.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "terrain-contour-reference-ratio": {
      "type": "number",
      // DuckMaps: the style spec requires a default, so this number appears here as well as in
      // the one definition it comes from (DuckMaps' style.py CONTOUR3D_*, served at
      // /style-tokens.json and read by the web engine's contours3d.js and by the native style
      // alike). It is a FALLBACK, not a second definition: the DuckMaps style always sets this
      // property explicitly, so this value is never the one that ships. Keep it equal to
      // style.py's if you touch either.
      "default": 2,
      "minimum": 1,
      "transition": false,
      "doc": "The render ratio the four dials above (the two widths and the two fade thresholds) are calibrated at. The tweaker multiplies all four by (live pixel ratio / this) before the shader sees them, so the calibrated CSS-pixel appearance holds at any device pixel ratio. This exists as a style property rather than a constant in the renderer because the same four numbers are read by the DuckMaps web engine (contours3d.js), which does the identical scaling against its own REF_RATIO: both read one definition, style.py's CONTOUR3D_REF_RATIO, served at /style-tokens.json.",
      "expression": {
          "interpolated": false,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  }
};

// DuckMaps fork only, task 2.6: slope/aspect area fill, computed per-fragment from the same
// terrain DEM terrain-contour reads, drawn over RenderTerrain's own mesh BEFORE terrain-contour
// so the contour lines sit on top of the fill - ported from the web engine's contours3d.js
// drawSlope()/FS_SLOPE (see container/server/app/map/assets/contours3d.js ~336-560 and
// docs/plans/2026-09-11-engine-layer-plumbing.md for the plumbing this reuses). Like
// terrain-contour, this layer has NO source and NO bucket/geometry of its own: it reuses
// RenderTerrain::getMesh()'s shared vertex/index buffers directly.
//
// David chose three built-in band configurations and no editor, 9 September 2026 (contours3d.js's
// own SLOPE_PRESETS comment): "avalanche" (the seven standard avalanche bands), "aspect" (a
// sixteen-sector compass wheel) and "flat" (0-1 degree ground only). Which one is selected and
// how transparent the fill is are the only two live dials - the band boundaries and colours
// belong to the preset ITSELF, not to a tunable value, so they are ported engine-side exactly
// like terrain-contour's own coverage()/density() formulas are, not read from style.py. Both
// dials are still read from ONE definition, David's account tuning document (server key
// map_tuning.slope, read into style_tokens() and emitted by native_lines.py) - see that module's
// own comment for how "preset" (a string there) becomes this property's small integer id here.
modifiedReferenceSpec.layer.type.values["slope-shading"] = {
  "doc": "Slope/aspect area fill computed per-fragment from the terrain DEM, drawn in real 3D world space on the terrain surface, DuckMaps fork only."
};

modifiedReferenceSpec["layout_slope-shading"] = {
  "visibility": {
      "type": "enum",
      "values": {
        "visible": { "doc": "The layer is shown." },
        "none": { "doc": "The layer is not shown." }
      },
      "default": "visible",
      "doc": "Whether this layer is displayed.",
      "property-type": "constant"
  }
};

modifiedReferenceSpec["paint_slope-shading"] = {
  "slope-shading-preset": {
      "type": "number",
      // 0 = avalanche, 1 = aspect, 2 = flat - SlopeShadingLayerTweaker::presetIndexToId(). A
      // plain small-integer id rather than a style-spec "enum" (a string keyed to a real C++ enum
      // type): the style-spec/engine plumbing for a first-class string enum PropertyValue reaches
      // into several engine-wide conversion registries (types.hpp, conversion/constant.cpp,
      // conversion/function.cpp, conversion/property_value.cpp) that no other DuckMaps-fork paint
      // property has ever needed to touch, and is out of scope for making this layer draw - see
      // development/app-bench/journal.md, task 2.6, for the record of that decision.
      "default": 0,
      "minimum": 0,
      "maximum": 2,
      "transition": false,
      "doc": "Which built-in slope band configuration to draw: 0 avalanche, 1 aspect, 2 flat.",
      "expression": {
          "interpolated": false,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  },
  "slope-shading-opacity": {
      "type": "number",
      // DuckMaps: the style spec requires a default, so this number appears here as well as in
      // the one definition it comes from (DuckMaps' style.py SLOPE_* constants, served at
      // /style-tokens.json and read by the web engine's contours3d.js and by the native style
      // alike). It is a FALLBACK, not a second definition - the DuckMaps style always sets this
      // property explicitly from David's saved tuning (map_tuning.slope.opacity), so this value
      // is never the one that ships. Keep it equal to style.py's default if you touch either.
      "default": 0.55,
      "minimum": 0,
      "maximum": 1,
      "transition": true,
      "doc": "Alpha multiplier for the slope/aspect fill, applied premultiplied like the contour layer.",
      "expression": {
          "interpolated": true,
          "parameters": ["zoom"]
      },
      "property-type": "data-constant"
  }
};

export default modifiedReferenceSpec;
