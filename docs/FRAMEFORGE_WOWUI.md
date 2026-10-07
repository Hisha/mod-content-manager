# FrameForge WoWUI packages

Content Manager consumes portable FrameForge WoW 3.3.5a exports through the
existing EPF discovery and build pipeline:

    FrameForge Export
        -> module/content/WoWUI
        -> module EPF builder
        -> module.epf
        -> existing module content/*.epf discovery
        -> validated/converted cumulative client staging
        -> normal MPQ builder

Content Manager never recursively scans modules for a `WoWUI` directory. The
EPF is the portable declaration and the only discovery boundary.

## Schema 3 declaration

A schema-3 package may declare one or more exports:

```json
"frameForgeWowUi": [
  {
    "source": "WoWUI",
    "layoutTarget": "Interface/FrameXML/ExampleFrameForgeLayout.xml"
  }
]
```

`source` is an EPF-relative directory. `layoutTarget` is the safe client
target for the generated `FrameForgeLayout.xml`. The package must also name
that target in the existing `clientFrameXml.loadEntries` list, normally after
the stock foundation XML. This deliberately reuses the protected FrameXML TOC
composer; FrameForge has no second loader.

The extension is optional and remains schema 3 because it adds no incompatible
meaning to existing fields. Older EPFs and schema-3 EPFs without the field keep
their established behavior. Arrays allow multiple independent exports.

## Validation and conversion

Before staging, the parser requires manifest version 2 of both
`frameforge-wow335-manifest.json` and `assets-manifest.json`, a build-12340
WoW target, the declared layout, safe relative member paths, every project asset,
matching SHA-256, safe logical `Interface/...` texture paths, and a matching
FrameXML load entry. Generated targets are inserted into the ordinary package
content set, so case-insensitive collisions with raw EPF files or another
package fail in the existing ownership preflight.

PNG files beneath `WoWUI/assets` are source artwork and may be embedded in an
EPF. They are not runtime WoW textures. Content Manager validates non-interlaced
8-bit RGBA PNG data (including chunk CRCs), preserves its dimensions and alpha,
and deterministically writes uncompressed top-origin 32-bit TGA. The final MPQ
receives `<logicalTexture>.tga`; it does not receive the PNG.

Stock logical references such as
`Interface\\TargetingFrame\\UI-StatusBar` stay references to client-owned
content. They are neither expected in the asset manifest nor copied into the
EPF or MPQ. FrameForge layout XML is staged byte-for-byte at `layoutTarget`.

Because the generated layout and texture targets are ordinary validated package
outputs, they participate in the existing staging count, output ownership,
case-insensitive collision checks, deterministic MPQ assembly, whole-MPQ hash,
parity artifact, activation, and publication lifecycle.

