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
messages, UISpecialFrames = {}, {}
DEFAULT_CHAT_FRAME = {AddMessage=function(_,message) table.insert(messages,message) end}
local function Frame(name,parent)
 local frame={name=name,parent=parent,visible=false,scripts={},hooks={}}
 function frame:SetWidth(v) self.width=v end
 function frame:SetHeight(v) self.height=v end
 function frame:SetPoint(...) self.point={...} end
 function frame:ClearAllPoints() self.point=nil end
 function frame:SetFrameStrata(v) self.strata=v end
 function frame:SetToplevel(v) self.toplevel=v end
 function frame:SetMovable(v) self.movable=v end
 function frame:EnableMouse(v) self.mouse=v end
 function frame:SetBackdrop(v) self.backdrop=v end
 function frame:SetAllPoints(v) self.allPoints=v or true end
 function frame:SetTexture(v) self.texture=v end
 function frame:SetHighlightTexture(v,m) self.highlight={v,m} end
 function frame:SetText(v) self.text=v end
 function frame:SetID(v) self.id=v end
 function frame:GetName() return self.name end
 function frame:IsShown() return self.visible end
 function frame:SetScript(k,v) self.scripts[k]=v end
 function frame:HookScript(k,v) self.hooks[k]=v end
 function frame:CreateFontString() return Frame(nil,self) end
 function frame:CreateTexture() return Frame(nil,self) end
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
function hooksecurefunc() end
''')
lua.execute(Path(sys.argv[1]).read_text(encoding="utf-8"))
g = lua.globals()

assert g.ContentManagerCollectionsMicroButton.visible
assert g.ContentManagerCollectionsMicroButton.point[2].name == "LFDMicroButton"
assert g.MainMenuMicroButton.point[2].name == "ContentManagerCollectionsMicroButton"
assert g.ContentManagerCollectionsFrame.numTabs == 2
assert g.ContentManagerCollectionsTab1.text == "Appearances"
assert g.ContentManagerCollectionsTab2.text == "Toys"
assert g.ContentManagerCollectionsMicroButton.tooltipText == "Collections"

lua.execute(r'''
NativeTransmogCollectionsFrame=CreateFrame("Frame","NativeTransmogCollectionsFrame",ContentManagerCollectionsFrame)
ToysFrame=CreateFrame("Frame","ToysFrame",ContentManagerCollectionsFrame)
appearancesShown,appearancesHidden=0,0
function NativeTransmog_OpenCollections() appearancesShown=appearancesShown+1 end
function NativeTransmog_CloseCollections() appearancesHidden=appearancesHidden+1 end
''')
g.ContentManagerCollectionsMicroButton.scripts["OnClick"]()
assert g.ContentManagerCollectionsFrame.visible
assert g.NativeTransmogCollectionsFrame.visible
assert not g.ToysFrame.visible
assert g.appearancesShown == 1

g.ContentManagerCollectionsTab2.scripts["OnClick"]()
assert not g.NativeTransmogCollectionsFrame.visible
assert g.ToysFrame.visible
assert g.appearancesHidden == 1

duplicate = lua.table_from({"id": "appearances", "name": "Other", "order": 1,
                            "frame": "OtherFrame"})
assert not g.ContentManager_RegisterCollectionPage(duplicate)
assert g.ContentManagerCollectionsFrame.numTabs == 2
assert any("duplicate page id 'appearances'" in message for message in g.messages.values())

print("native_collections_runtime: PASS")
