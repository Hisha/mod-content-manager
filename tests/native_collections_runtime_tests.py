#!/usr/bin/env python3
"""Execute the generated Collections controller in a Lua 5.1 UI double."""

from pathlib import Path
import sys

try:
    from lupa.lua51 import LuaRuntime
except ImportError:
    print("native_collections_runtime: SKIP (lupa.lua51 unavailable)")
    raise SystemExit(0)

if len(sys.argv) != 2:
    raise SystemExit("usage: native_collections_runtime_tests.py <generated-lua>")

lua = LuaRuntime()
lua.execute(r'''
messages, UISpecialFrames, secureHooks = {}, {}, {}
DEFAULT_CHAT_FRAME = {AddMessage=function(_,message) table.insert(messages,message) end}
local function Frame(name,parent)
 local frame={name=name,parent=parent,visible=false,scripts={},hooks={},textures={},points={}}
 function frame:SetWidth(v) self.width=v end
 function frame:SetHeight(v) self.height=v end
 function frame:SetPoint(...) self.point={...}; table.insert(self.points,{...}) end
 function frame:ClearAllPoints() self.point=nil; self.points={} end
 function frame:SetHitRectInsets(...) self.hitRectInsets={...} end
 function frame:SetFrameStrata(v) self.strata=v end
 function frame:SetToplevel(v) self.toplevel=v end
 function frame:SetMovable(v) self.movable=v end
 function frame:EnableMouse(v) self.mouse=v end
 function frame:SetBackdrop(v) self.backdrop=v end
 function frame:SetAllPoints(v) self.allPoints=v or true end
 function frame:SetTexture(v) self.texture=v end
 function frame:SetBlendMode(v) self.blendMode=v end
 function frame:SetNormalTexture(v) self.normalTexture=v end
 function frame:SetPushedTexture(v) self.pushedTexture=v end
 function frame:SetHighlightTexture(v,m) self.highlightTexture=v; self.highlightMode=m end
 function frame:SetButtonState(v,locked) self.buttonState=v; self.buttonLocked=locked end
 function frame:SetText(v) self.text=v end
 function frame:SetID(v) self.id=v end
 function frame:GetName() return self.name end
 function frame:GetParent() return self.parent end
 function frame:IsShown() return self.visible end
 function frame:SetScript(k,v) self.scripts[k]=v end
 function frame:HookScript(k,v) self.hooks[k]=v end
 function frame:CreateFontString() return Frame(nil,self) end
 function frame:CreateTexture(_,layer) local texture=Frame(nil,self); texture.layer=layer; table.insert(self.textures,texture); return texture end
 function frame:Show() self.visible=true; if self.scripts.OnShow then self.scripts.OnShow() end end
 function frame:Hide() self.visible=false; if self.scripts.OnHide then self.scripts.OnHide() end end
 return frame
end
UIParent=Frame("UIParent")
MainMenuBarArtFrame=Frame("MainMenuBarArtFrame",UIParent)
LFDMicroButton=Frame("LFDMicroButton",MainMenuBarArtFrame)
MainMenuMicroButton=Frame("MainMenuMicroButton",MainMenuBarArtFrame)
GameTooltip={SetOwner=function() end,SetText=function() end,AddLine=function() end,Show=function() end,Hide=function() end}
function CreateFrame(_,name,parent) local frame=Frame(name,parent); if name then _G[name]=frame end; return frame end
function PanelTemplates_SetTab(frame,index) frame.selectedTab=index end
function PanelTemplates_SetNumTabs(frame,count) frame.numTabs=count end
function PanelTemplates_TabResize() end
function UpdateMicroButtons() end
function MoveMicroButtons() end
function hooksecurefunc(name,callback) secureHooks[name]=callback end
''')
lua.execute(Path(sys.argv[1]).read_text(encoding="utf-8"))
g = lua.globals()

assert g.ContentManagerCollectionsMicroButton.visible
assert g.ContentManagerCollectionsMicroButton.point[2].name == "LFDMicroButton"
assert g.MainMenuMicroButton.point[2].name == "ContentManagerCollectionsMicroButton"
assert g.ContentManagerCollectionsFrame.numTabs == 2
assert g.ContentManagerCollectionsFrame.width == 940
assert g.ContentManagerCollectionsFrame.height == 590
assert g.ContentManagerCollectionsFrameTab1.text == "Appearances"
assert g.ContentManagerCollectionsFrameTab2.text == "Toys"
assert g.ContentManagerCollectionsMicroButton.tooltipText == "Collections"
button = g.ContentManagerCollectionsMicroButton
icon = button.textures[1]
assert button.width == 28 and button.height == 58
assert list(button.hitRectInsets.values()) == [0, 0, 18, 0]
assert button.normalTexture == r"Interface\Buttons\UI-MicroButtonCharacter-Up"
assert button.pushedTexture == r"Interface\Buttons\UI-MicroButtonCharacter-Down"
assert button.highlightTexture == r"Interface\Buttons\UI-MicroButton-Hilight"
assert button.highlightMode == "ADD"
assert icon.layer == "OVERLAY"
assert icon.width == 20 and icon.height == 20
assert icon.point[1] == "BOTTOM" and icon.point[2].name == button.name
assert icon.point[3] == "BOTTOM" and icon.point[4] == 0 and icon.point[5] == 7
button.scripts["OnMouseDown"]()
assert icon.point[4] == 1 and icon.point[5] == 6
button.scripts["OnMouseUp"]()
assert icon.point[4] == 0 and icon.point[5] == 7

# An addon-managed micro-menu owns anchors after it reparents the button.
managedParent = lua.table_from({"name": "ManagedMicroMenu"})
button.parent = managedParent
button.point = lua.table_from({"managed"})
g.MainMenuMicroButton.point = lua.table_from({"managed"})
g.secureHooks["UpdateMicroButtons"]()
assert button.point[1] == "managed"
assert g.MainMenuMicroButton.point[1] == "managed"

lua.execute(r'''
NativeTransmogCollectionsFrame=CreateFrame("Frame","NativeTransmogCollectionsFrame",ContentManagerCollectionsFrame)
ToysFrame=CreateFrame("Frame","ToysFrame",ContentManagerCollectionsFrame)
appearancesShown,appearancesHidden=0,0
function NativeTransmog_OpenCollections() appearancesShown=appearancesShown+1 end
function NativeTransmog_CloseCollections() appearancesHidden=appearancesHidden+1 end
''')
g.ContentManagerCollectionsMicroButton.scripts["OnClick"]()
assert g.ContentManagerCollectionsFrame.visible
assert g.ContentManagerCollectionsFrame.width == 704
assert g.ContentManagerCollectionsFrame.height == 616
assert g.ContentManagerCollectionsFrame.TitleText.text == "Appearances"
assert button.buttonState == "PUSHED" and button.buttonLocked
assert icon.point[4] == 1 and icon.point[5] == 6
assert g.NativeTransmogCollectionsFrame.visible
assert not g.ToysFrame.visible
assert g.appearancesShown == 1
assert len(g.NativeTransmogCollectionsFrame.points) == 2
assert g.NativeTransmogCollectionsFrame.points[1][1] == "TOPLEFT"
assert g.NativeTransmogCollectionsFrame.points[1][2].name == "ContentManagerCollectionsFrame"
assert g.NativeTransmogCollectionsFrame.points[2][1] == "BOTTOMRIGHT"

g.ContentManagerCollectionsFrameTab2.scripts["OnClick"]()
assert not g.NativeTransmogCollectionsFrame.visible
assert g.ToysFrame.visible
assert g.appearancesHidden == 1
assert g.ContentManagerCollectionsFrame.width == 940
assert g.ContentManagerCollectionsFrame.height == 590
assert g.ContentManagerCollectionsFrame.TitleText.text == "Toys"
assert len(g.ToysFrame.points) == 2

# Switching back restores page-specific metadata and replaces, rather than
# accumulating, the page and host anchors.
g.ContentManagerCollectionsFrameTab1.scripts["OnClick"]()
assert g.ContentManagerCollectionsFrame.width == 704
assert g.ContentManagerCollectionsFrame.height == 616
assert g.ContentManagerCollectionsFrame.TitleText.text == "Appearances"
assert len(g.NativeTransmogCollectionsFrame.points) == 2
assert len(g.ContentManagerCollectionsFrame.points) == 1

g.ContentManagerCollectionsMicroButton.scripts["OnClick"]()
assert not g.ContentManagerCollectionsFrame.visible
assert button.buttonState == "NORMAL"
assert icon.point[4] == 0 and icon.point[5] == 7

# Repeated open/close runs the same deterministic layout without adding
# anchors, extra controls, or offsets.
for _ in range(3):
    g.ContentManagerCollectionsMicroButton.scripts["OnClick"]()
    assert g.ContentManagerCollectionsFrame.visible
    assert len(g.NativeTransmogCollectionsFrame.points) == 2
    assert len(g.ContentManagerCollectionsFrame.points) == 1
    g.ContentManagerCollectionsMicroButton.scripts["OnClick"]()
    assert not g.ContentManagerCollectionsFrame.visible
assert g.ContentManagerCollectionsCloseButton.parent.name == "ContentManagerCollectionsFrame"

duplicate = lua.table_from({"id": "appearances", "name": "Other", "order": 1,
                            "frame": "OtherFrame"})
assert not g.ContentManager_RegisterCollectionPage(duplicate)
assert g.ContentManagerCollectionsFrame.numTabs == 2
assert any("duplicate page id 'appearances'" in message for message in g.messages.values())

print("native_collections_runtime: PASS")
