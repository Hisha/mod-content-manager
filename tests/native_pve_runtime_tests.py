#!/usr/bin/env python3
"""Execute the generated native PvE controller in a Lua 5.1 UI double."""

from pathlib import Path
import sys

try:
    from lupa.lua51 import LuaRuntime
except ImportError:
    print("native_pve_runtime: SKIP (lupa.lua51 unavailable)")
    raise SystemExit(0)

if len(sys.argv) != 2:
    raise SystemExit("usage: native_pve_runtime_tests.py <generated-lua>")

lua = LuaRuntime()
lua.execute(r'''
messages = {}
DEFAULT_CHAT_FRAME = {AddMessage=function(_, message) table.insert(messages,message) end}
local function Frame(name)
 local frame={name=name,visible=false,hooks={},scripts={}}
 function frame:Show() self.visible=true end
 function frame:Hide() self.visible=false end
 function frame:IsShown() return self.visible end
 function frame:SetID(value) self.id=value end
 function frame:SetText(value) self.text=value end
 function frame:ClearAllPoints() self.point=nil end
 function frame:SetPoint(...) self.point={...} end
 function frame:SetScript(kind,callback) self.scripts[kind]=callback end
 function frame:HookScript(kind,callback) self.hooks[kind]=callback end
 function frame:GetName() return self.name end
 return frame
end
UIParent=Frame("UIParent")
LFDParentFrame=Frame("LFDParentFrame")
LFDQueueFrame=Frame("LFDQueueFrame")
LFDParentFramePortraitTexture=Frame("PortraitTexture")
LFDParentFramePortraitIcon=Frame("PortraitIcon")
LFDMicroButton=Frame("LFDMicroButton")
LFDQueueFrame:Show(); LFDParentFramePortraitTexture:Show(); LFDParentFramePortraitIcon:Show()
function CreateFrame(_,name) local frame=Frame(name); _G[name]=frame; return frame end
function PanelTemplates_SetTab(frame,index) frame.selectedTab=index end
function PanelTemplates_SetNumTabs(frame,count) frame.numTabs=count end
function PanelTemplates_TabResize() end
function MicroButtonTooltipText(text) return text end
function LFDFrame_OnEvent() end
function hooksecurefunc() end
''')
lua.execute(Path(sys.argv[1]).read_text(encoding="utf-8"))

g = lua.globals()
assert g.LFDParentFrame.numTabs == 3
assert g.LFDParentFrameTab1.text == "Dungeon Finder"
assert g.LFDParentFrameTab2.text == "Appearances"
assert g.LFDParentFrameTab3.text == "Hunts"
assert g.LFDMicroButton.tooltipText == "Player vs. Environment"

lua.execute(r'''
NativeTransmogFrame=CreateFrame("Frame","NativeTransmogFrame",UIParent)
NativeHuntsFrame=CreateFrame("Frame","NativeHuntsFrame",LFDParentFrame)
appearancesShown,huntsShown,huntsHidden=0,0,0
function NativeTransmog_OpenCollections() appearancesShown=appearancesShown+1 end
function NativeHuntsFrame_OnPveShow() huntsShown=huntsShown+1 end
function NativeHuntsFrame_OnPveHide() huntsHidden=huntsHidden+1 end
''')

# Repeated switching preserves stock queue behavior and calls page lifecycles.
for _ in range(3):
    g.LFDParentFrameTab2.scripts["OnClick"]()
    assert g.NativeTransmogFrame.visible and not g.LFDQueueFrame.visible
    g.LFDParentFrameTab3.scripts["OnClick"]()
    assert g.NativeHuntsFrame.visible and not g.NativeTransmogFrame.visible
    g.LFDParentFrameTab1.scripts["OnClick"]()
    assert g.LFDQueueFrame.visible and not g.NativeHuntsFrame.visible
assert (g.appearancesShown, g.huntsShown, g.huntsHidden) == (3, 3, 3), (
    g.appearancesShown, g.huntsShown, g.huntsHidden
)

# Closing the parent hides the selected page; reopening restores it.
g.LFDParentFrameTab3.scripts["OnClick"]()
g.LFDParentFrame.hooks["OnHide"]()
assert not g.NativeHuntsFrame.visible
g.LFDParentFrame.hooks["OnShow"]()
assert g.NativeHuntsFrame.visible and g.huntsShown == 5

# Runtime duplicate registration fails closed without changing tab count.
duplicate = lua.table_from({"id": "hunts", "name": "Other", "frame": "OtherFrame"})
assert not g.ContentManager_RegisterNativePvePage(duplicate)
assert g.LFDParentFrame.numTabs == 3
assert any("duplicate page id 'hunts'" in message for message in g.messages.values())

# A missing contributed frame produces a diagnostic and returns to stock safely.
g.NativeHuntsFrame = None
assert not g.ContentManagerNativePve_SelectPage("hunts")
assert g.LFDQueueFrame.visible and g.LFDParentFrame.selectedTab == 1
assert any("missing frame NativeHuntsFrame" in message for message in g.messages.values())

print("native_pve_runtime: PASS")
