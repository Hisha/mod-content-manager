# Shared native PvE menu

Content Manager schema 3 can assemble independent native module pages into the
stock build-12340 Dungeon Finder window. Content Manager generates exactly one
`Interface/FrameXML/ContentManagerNativePve.lua` and one composed
`Interface/FrameXML/FrameXML.toc`; gameplay packages continue to own their own
frames, Lua, and artwork.

## Package declaration

Each page is declared in `nativePvePages`:

```json
"nativePvePages": [
  {
    "id": "hunts",
    "name": "Hunts",
    "frame": "NativeHuntsFrame",
    "loadTarget": "Interface/FrameXML/NativeHuntsFrame.xml",
    "onShow": "NativeHuntsFrame_OnPveShow",
    "onHide": "NativeHuntsFrame_OnPveHide"
  }
]
```

`id` is a stable lowercase package symbol and must be unique across the build.
`name` is the displayed tab label. `frame`, `onShow`, and `onHide` are Lua global
identifiers; callbacks are optional. `loadTarget` must be owned by the same EPF
and present in that package's `clientFrameXml.loadEntries`. Anchor that entry
after the generated controller:

```json
{
  "target": "Interface/FrameXML/NativeHuntsFrame.xml",
  "after": "Interface/FrameXML/ContentManagerNativePve.lua"
}
```

No page package owns the generated controller or `FrameXML.toc`. The existing
protected FrameXML composer validates and supplies both.

## Runtime interface

The generated controller exposes:

```lua
ContentManager_RegisterNativePvePage({
  id = "example",
  name = "Example",
  frame = "ExampleFrame",
  onShow = "Example_OnPveShow",
  onHide = "Example_OnPveHide"
})
ContentManagerNativePve_SelectPage("example")
```

Manifest pages are registered through the same interface. Registration rejects
duplicate IDs and malformed definitions. Frames and callbacks resolve lazily,
so page files may load after the controller; selecting a page whose frame is
missing reports an actionable chat error and safely returns to Dungeon Finder.

Dungeon Finder is always tab 1. Additional pages are ordered by the ASCII
case-folded displayed name with stable ID tie-breaking, independent of EPF
discovery or registration order. The controller uses the native build-12340
`CharacterFrameTabButtonTemplate` and `PanelTemplates_*` helpers, preserves the
stock `LFDQueueFrame`, restores the selected page on reopen, hides pages when
the parent closes, and changes the micro-menu tooltip only when at least one
page was composed.

## Compatibility and removal

The field is an optional schema-3 extension. Packages without it retain their
existing behavior. Invalid declarations, duplicate build-wide IDs, raw target
collisions, absent page load targets, and bad ordering anchors fail the build.
Removing a package and regenerating the cumulative patch removes its generated
registration and tab because no persistent client registration state exists.

The feature is pinned to client build 12340 and requires the existing
`protected-framexml` capability. It does not replace `LFDFrame.xml` or any other
Blizzard interface file; only the already-supported composed `FrameXML.toc`
load list changes.
