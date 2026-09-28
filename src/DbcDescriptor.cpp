#include "DbcDescriptor.h"
#include <set>

namespace
{
std::vector<std::string> const& SpellFieldNames()
{
	static std::vector<std::string> const c=[] { std::vector<std::string> c={"ID","Category","DispelType","Mechanic","Attributes","AttributesEx","AttributesEx2","AttributesEx3","AttributesEx4","AttributesEx5","AttributesEx6","AttributesEx7","ShapeshiftMask","unk_320_2","ShapeshiftExclude","unk_320_3","Targets","TargetCreatureType","RequiresSpellFocus","FacingCasterFlags","CasterAuraState","TargetAuraState","ExcludeCasterAuraState","ExcludeTargetAuraState","CasterAuraSpell","TargetAuraSpell","ExcludeCasterAuraSpell","ExcludeTargetAuraSpell","CastingTimeIndex","RecoveryTime","CategoryRecoveryTime","InterruptFlags","AuraInterruptFlags","ChannelInterruptFlags","ProcTypeMask","ProcChance","ProcCharges","MaxLevel","BaseLevel","SpellLevel","DurationIndex","PowerType","ManaCost","ManaCostPerLevel","ManaPerSecond","ManaPerSecondPerLevel","RangeIndex","Speed","ModalNextSpell","CumulativeAura","Totem_1","Totem_2"};
	for(auto p:{"Reagent","ReagentCount"})for(int i=1;i<=8;++i)c.push_back(std::string(p)+"_"+std::to_string(i));
	for(auto x:{"EquippedItemClass","EquippedItemSubclass","EquippedItemInvTypes"})c.push_back(x);
	for(auto p:{"Effect","EffectDieSides","EffectRealPointsPerLevel","EffectBasePoints","EffectMechanic","ImplicitTargetA","ImplicitTargetB","EffectRadiusIndex","EffectAura","EffectAuraPeriod","EffectMultipleValue","EffectChainTargets","EffectItemType","EffectMiscValue","EffectMiscValueB","EffectTriggerSpell","EffectPointsPerCombo","EffectSpellClassMaskA","EffectSpellClassMaskB","EffectSpellClassMaskC"})for(int i=1;i<=3;++i)c.push_back(std::string(p)+"_"+std::to_string(i));
	for(auto x:{"SpellVisualID_1","SpellVisualID_2","SpellIconID","ActiveIconID","SpellPriority"})c.push_back(x);
	for(auto p:{"Name_Lang","NameSubtext_Lang","Description_Lang","AuraDescription_Lang"}){for(auto l:{"enUS","enGB","koKR","frFR","deDE","enCN","zhCN","enTW","zhTW","esES","esMX","ruRU","ptPT","ptBR","itIT","Unk"})c.push_back(std::string(p)+"_"+l);c.push_back(std::string(p)+"_Mask");}
	for(auto x:{"ManaCostPct","StartRecoveryCategory","StartRecoveryTime","MaxTargetLevel","SpellClassSet","SpellClassMask_1","SpellClassMask_2","SpellClassMask_3","MaxTargets","DefenseType","PreventionType","StanceBarOrder","EffectChainAmplitude_1","EffectChainAmplitude_2","EffectChainAmplitude_3","MinFactionID","MinReputation","RequiredAuraVision","RequiredTotemCategoryID_1","RequiredTotemCategoryID_2","RequiredAreasID","SchoolMask","RuneCostID","SpellMissileID","PowerDisplayID","EffectBonusMultiplier_1","EffectBonusMultiplier_2","EffectBonusMultiplier_3","SpellDescriptionVariableID","SpellDifficultyID"})c.push_back(x);
	return c; }();
	return c;
}
DbcDescriptor MakeSpell12340()
{
	DbcDescriptor d{12340, "Spell", 1, "DBFilesClient/Spell.dbc", "Spell.dbc", {}};
	static std::set<std::size_t> const floats = {47,77,78,79,101,102,103,119,120,121,216,217,218,229,230,231};
	static std::set<std::size_t> strings;
	if (strings.empty()) for (auto base : {136u,153u,170u,187u}) for (std::size_t i=0;i<16;++i) strings.insert(base+i);
	auto names=SpellFieldNames();
	for (std::size_t i=0;i<234;++i) {
		DbcFieldDescriptor f{names.at(i).c_str(), floats.count(i)?DbcFieldType::Float32:(strings.count(i)?DbcFieldType::StringOffset:DbcFieldType::UInt32)};
		if (i==0) { f.name="ID"; f.primaryKey=true; f.allocationNamespace="spell.id"; }
		d.fields.push_back(f);
	}
	return d;
}
DbcDescriptor const spell12340 = MakeSpell12340();
DbcDescriptor const extended12340 = {
    12340, "ItemExtendedCost", 1, "DBFilesClient/ItemExtendedCost.dbc", "ItemExtendedCost.dbc",
    {
        {"ID", DbcFieldType::Int32, true, nullptr, "item-extended-cost.id"},
        {"HonorPoints", DbcFieldType::Int32},
        {"ArenaPoints", DbcFieldType::Int32},
        {"ArenaBracket", DbcFieldType::Int32},
        {"ItemID_1", DbcFieldType::Int32, false, "Item"},
        {"ItemID_2", DbcFieldType::Int32, false, "Item"},
        {"ItemID_3", DbcFieldType::Int32, false, "Item"},
        {"ItemID_4", DbcFieldType::Int32, false, "Item"},
        {"ItemID_5", DbcFieldType::Int32, false, "Item"},
        {"ItemCount_1", DbcFieldType::Int32},
        {"ItemCount_2", DbcFieldType::Int32},
        {"ItemCount_3", DbcFieldType::Int32},
        {"ItemCount_4", DbcFieldType::Int32},
        {"ItemCount_5", DbcFieldType::Int32},
        {"RequiredArenaRating", DbcFieldType::Int32},
        {"ItemPurchaseGroup", DbcFieldType::Int32}
    }
};
DbcDescriptor const item12340 = {
    12340, "Item", 1, "DBFilesClient/Item.dbc", "Item.dbc",
    {
        {"ID", DbcFieldType::UInt32, true, nullptr, "item.id"},
        {"ClassID", DbcFieldType::UInt32},
        {"SubclassID", DbcFieldType::UInt32},
        {"SoundOverrideSubclassID", DbcFieldType::Int32},
        {"Material", DbcFieldType::Int32},
        {"DisplayInfoID", DbcFieldType::UInt32, false, "ItemDisplayInfo"},
        {"InventoryType", DbcFieldType::UInt32},
        {"SheatheType", DbcFieldType::UInt32}
    }
};
DbcDescriptor const currency12340 = {
    12340, "CurrencyTypes", 1, "DBFilesClient/CurrencyTypes.dbc", "CurrencyTypes.dbc",
    {
        {"ID", DbcFieldType::Int32, true},
        {"ItemID", DbcFieldType::Int32, false, "Item"},
        {"CategoryID", DbcFieldType::Int32, false, "CurrencyCategory"},
        {"BitIndex", DbcFieldType::Int32, false, nullptr, "currency.known-bit"}
    }
};
DbcDescriptor const category12340 = {
    12340, "CurrencyCategory", 1, "DBFilesClient/CurrencyCategory.dbc", "CurrencyCategory.dbc",
    {
        {"ID", DbcFieldType::Int32, true, nullptr, "currency-category.id"},
        {"Flags", DbcFieldType::Int32},
        {"Name_enUS", DbcFieldType::StringOffset}, {"Name_koKR", DbcFieldType::StringOffset},
        {"Name_frFR", DbcFieldType::StringOffset}, {"Name_deDE", DbcFieldType::StringOffset},
        {"Name_zhCN", DbcFieldType::StringOffset}, {"Name_zhTW", DbcFieldType::StringOffset},
        {"Name_esES", DbcFieldType::StringOffset}, {"Name_esMX", DbcFieldType::StringOffset},
        {"Name_ruRU", DbcFieldType::StringOffset}, {"Name_reserved9", DbcFieldType::StringOffset},
        {"Name_reserved10", DbcFieldType::StringOffset}, {"Name_reserved11", DbcFieldType::StringOffset},
        {"Name_reserved12", DbcFieldType::StringOffset}, {"Name_reserved13", DbcFieldType::StringOffset},
        {"Name_reserved14", DbcFieldType::StringOffset}, {"Name_reserved15", DbcFieldType::StringOffset},
        {"NameFlags", DbcFieldType::UInt32}
    }
};
}

DbcDescriptor const* FindDbcDescriptor(std::uint32_t clientBuild, std::string const& tableName)
{
    if (clientBuild != 12340) return nullptr;
    if (tableName == "ItemExtendedCost") return &extended12340;
    if (tableName == "Item") return &item12340;
    if (tableName == "CurrencyTypes") return &currency12340;
    if (tableName == "CurrencyCategory") return &category12340;
    if (tableName == "Spell") return &spell12340;
    return nullptr;
}

bool IsKnownDbcTable(std::string const& tableName)
{
    return tableName == "ItemExtendedCost" || tableName == "Item" || tableName == "CurrencyTypes" || tableName == "CurrencyCategory" || tableName == "Spell";
}
