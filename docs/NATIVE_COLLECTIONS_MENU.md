# Native Collections menu

Content Manager schema 3 owns the top-level Collections surface. Gameplay
packages contribute pages; they do not create or position micro-menu buttons.

```json
"nativeCollectionPages": [
  {
    "id": "appearances",
    "name": "Appearances",
    "title": "Appearances",
    "order": 100,
    "width": 704,
    "height": 616,
    "frame": "NativeTransmogCollectionsFrame",
    "loadTarget": "Interface/FrameXML/NativeTransmogFrame.xml",
    "onShow": "NativeTransmog_OpenCollections",
    "onHide": "NativeTransmog_CloseCollections"
  }
]
```

`id` is a schema symbol and must be unique across all installed contributors in
one build. `name` is the displayed tab text; optional `title` is the host window
title and falls back to `name`. Optional `width` (320..1600) and `height`
(240..1200) are the preferred host dimensions. Pages that omit them retain the
legacy 940x590 defaults. `order` is an unsigned integer at most 1,000,000;
pages sort by order, case-folded name, then ID. `frame`, `onShow`, and `onHide`
are Lua global names. Callbacks are optional. The
`loadTarget` must be both package-owned and present in
`clientFrameXml.loadEntries`.

The page load entry should anchor to
`Interface/FrameXML/ContentManagerCollections.lua`. Content Manager inserts
that controller after stock `LFDFrame.xml`, then resolves contributor ordering
deterministically. A contributor owns its page frame only. Content Manager owns:

- `Interface/FrameXML/ContentManagerCollections.lua`
- `Interface/Buttons/ContentManagerCollections.tga`
- the composed `Interface/FrameXML/FrameXML.toc`
- the Collections host, tabs, micro-menu button, tooltip, routing, and layout

These generated targets exist only when at least one installed package in the
selected cumulative build has a valid `nativeCollectionPages` declaration.
Removing the final contributor removes the controller, icon, and button on the
next build. It does not alter the currently ACTIVE build or delete any published
artifact; the replacement build must be activated explicitly. Source EPFs that
are merely present but not installed do not participate.

Activation retains the prior versioned publication as a historical artifact and
marks its registry row `SUPERSEDED`. Portalkeeper must continue to advertise the
realm content under the same realm identity and WowPatch key (for example,
`realm-content`) while changing its URL and SHA-256 to the newly activated build.
That stable key causes the verified replacement to reuse the existing `patch-N.MPQ`
slot. Publishing the replacement under a new key, or removing the old realm entry
without explicitly removing its installed patch, can leave both cumulative MPQs in
the client `Data` directory; Content Manager cannot prevent that client-side
configuration error.

The generated-resource selection is a named set, not positional booleans. The
same set drives ownership registration, staging, cumulative counts, hashes,
parity allowlisting, and activation validation. Parity continues to require the
exact target-to-SHA-256 map.

The active page metadata sizes and titles the host. On every selection the host
centers itself and replaces the page anchors so the page fills the content area
below the 32-pixel host header. Contributors should parent their page frame to
`ContentManagerCollectionsFrame`, keep it initially hidden, and avoid outer
backgrounds, borders, titles, close buttons, or micro-menu controls. Do not
register empty or placeholder pages.

Shift+P is intentionally deferred. Build 12340 requires a real binding
declaration and conflict review; a Lua binding label alone would not safely
install a default shortcut without risking an existing binding.
