#include "SpellDbcComposer.h"
#include "DbcDescriptor.h"
#include "third_party/json/json.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace {
constexpr std::size_t ID=0, ATTR=4, DURATION=40, CAST=28, RANGE=46,
	EFFECT=71, DIE=74, REAL=77, BASE=80, MECHANIC=83, TARGET_A=86,
	TARGET_B=89, RADIUS=92, AURA=95, AMPLITUDE=98, MULTIPLE=101,
	CHAIN=104, ITEM=107, MISC=110, MISCB=113, TRIGGER=116, COMBO=119,
	CLASSMASK=122, VISUAL=131, ICON=133, ACTIVE_ICON=134, PRIORITY=135,
	NAME=136, RANK=153, DESCRIPTION=170, AURA_DESCRIPTION=187,
	MANA_PCT=204, FAMILY=208, FAMILY_MASK=209, MAX_TARGETS=212,
	CHAIN_AMPLITUDE=216, SCHOOL=225, RUNE=226, BONUS=229;
std::array<std::pair<char const*,std::size_t>, 9> const Locales={{{"enUS",0},{"koKR",2},{"frFR",3},{"deDE",4},{"zhCN",6},{"zhTW",8},{"esES",9},{"esMX",10},{"ruRU",11}}};
void ValidateText(std::map<std::string,std::string> const& values, bool required,
	std::size_t maximum)
{
	if (required && (!values.count("enUS") || values.at("enUS").empty()))
		throw std::runtime_error("Managed spell requires an authored enUS name");
	for (auto const& [locale,value]:values) {
		if (std::none_of(Locales.begin(),Locales.end(),[&](auto const& p){return locale==p.first;}) || value.size()>maximum ||
			value.find('\0')!=std::string::npos || std::any_of(value.begin(),value.end(),[](unsigned char c){return c<32 && c!='\t' && c!='\n';}))
			throw std::runtime_error("Invalid managed spell locale/text");
		(void)nlohmann::json(value).dump();
	}
}
}

std::array<std::size_t,64> SpellDbcComposer::StringFields()
{
	std::array<std::size_t,64> r{}; std::size_t n=0;
	for (auto base:{NAME,RANK,DESCRIPTION,AURA_DESCRIPTION}) for(std::size_t i=0;i<16;++i) r[n++]=base+i;
	return r;
}
std::string SpellDbcComposer::String(DbcDocument const& d,std::size_t row,std::size_t field)
{
	if(row>=d.recordCount||field>=FieldCount) throw std::runtime_error("Spell string address out of range");
	auto o=d.words[row*FieldCount+field]; if(o>=d.strings.size()) throw std::runtime_error("Spell string offset out of range");
	auto b=d.strings.begin()+o,e=std::find(b,d.strings.end(),0); if(e==d.strings.end()) throw std::runtime_error("Unterminated Spell string");
	return {b,e};
}
std::set<std::uint32_t> SpellDbcComposer::Inspect(DbcDocument const& d)
{
	if(d.fieldCount!=FieldCount||d.recordSize!=RecordSize||d.words.size()!=d.recordCount*FieldCount||d.strings.empty()||d.strings.front()!=0)
		throw std::runtime_error("Invalid build-12340 Spell.dbc dimensions/string block");
	std::set<std::uint32_t> ids; auto sf=StringFields();
	for(std::size_t r=0;r<d.recordCount;++r){auto id=d.words[r*FieldCount];if(!id||!ids.insert(id).second)throw std::runtime_error("Duplicate/invalid Spell ID");for(auto f:sf)(void)String(d,r,f);}
	return ids;
}
void SpellDbcComposer::ValidateDeclaration(ContentSpellRow const& r)
{
	if(r.symbol.empty()||!r.copyFrom||r.profile!=Profile) throw std::runtime_error("Managed spell requires symbol, stock copyFrom and informational-self-aura-v1 profile");
	ValidateText(r.names,true,100); ValidateText(r.descriptions,false,65535);
	ValidateText(r.auraDescriptions,false,550);
}
bool SpellDbcComposer::BehaviorMatches(ResolvedSpell const& r)
{
	auto const&w=r.words;
	if(!r.id||w[ID]!=r.id||w[CAST]!=1||w[DURATION]!=PermanentDuration||w[RANGE]!=1||w[EFFECT]!=6||w[TARGET_A]!=1||w[AURA]!=4||w[ACTIVE_ICON]!=0||!w[ICON]||w[SCHOOL]!=1) return false;
	for(std::size_t i=0;i<w.size();++i)if(w[i]&&i!=ID&&i!=CAST&&i!=DURATION&&i!=RANGE&&i!=EFFECT&&i!=TARGET_A&&i!=AURA&&i!=ICON&&i!=152&&i!=169&&i!=186&&i!=203&&i!=SCHOOL)return false;
	return true;
}
ResolvedSpell SpellDbcComposer::Resolve(DbcDocument const& d,ContentSpellRow const& a,std::string const&p,std::string const&v,std::uint32_t id)
{
	ValidateDeclaration(a); auto ids=Inspect(d); if(!id||ids.count(id))throw std::runtime_error("Allocated Spell ID collides with baseline");
	auto find=[&](std::uint32_t x){for(std::size_t i=0;i<d.recordCount;++i)if(d.words[i*FieldCount]==x)return i;throw std::runtime_error("Managed spell donor is absent from baseline");};
	auto donor=find(a.copyFrom), iconDonor=find(a.iconCopyFromSpell?a.iconCopyFromSpell:a.copyFrom);
	ResolvedSpell r; r.packageKey=p;r.packageVersion=v;r.symbol=a.symbol;r.profile=a.profile;r.id=id;r.copyFrom=a.copyFrom;r.iconCopyFromSpell=a.iconCopyFromSpell?a.iconCopyFromSpell:a.copyFrom;
	// Start from the donor so opaque build metadata is structurally available,
	// then deliberately normalize every field. Only locale flag words and a
	// verified stock icon survive donor selection.
	r.words.fill(0);
	r.words[ID]=id; r.words[CAST]=1; r.words[DURATION]=PermanentDuration; r.words[RANGE]=1; r.words[EFFECT]=6; r.words[TARGET_A]=1; r.words[AURA]=4;
	r.words[ICON]=d.words[iconDonor*FieldCount+ICON]; r.words[152]=d.words[donor*FieldCount+152];r.words[169]=d.words[donor*FieldCount+169];r.words[186]=d.words[donor*FieldCount+186];r.words[203]=d.words[donor*FieldCount+203]; r.words[SCHOOL]=1;
	if(!r.words[ICON])throw std::runtime_error("Managed spell icon donor has no stock SpellIconID");
	auto fill=[&](std::map<std::string,std::string> const&m,std::array<std::string,16>&out){auto fallback=m.count("enUS")?m.at("enUS"):std::string();for(auto const& [locale,index]:Locales){auto f=m.find(locale);out[index]=f==m.end()?fallback:f->second;}};
	fill(a.names,r.localized[0]); fill({},r.localized[1]); fill(a.descriptions,r.localized[2]); fill(a.auraDescriptions,r.localized[3]);
	if(!BehaviorMatches(r))throw std::runtime_error("Managed spell profile normalization failed"); return r;
}
std::vector<std::uint8_t> SpellDbcComposer::Compose(DbcDocument const& baseline,std::vector<ResolvedSpell> additions)
{
	auto occupied=Inspect(baseline); auto out=baseline; std::sort(additions.begin(),additions.end(),[](auto const&a,auto const&b){return a.id<b.id;});
	std::map<std::string,std::uint32_t> interned; auto intern=[&](std::string const&s){if(s.empty())return 0u;auto f=interned.find(s);if(f!=interned.end())return f->second;if(out.strings.size()+s.size()+1>std::numeric_limits<std::uint32_t>::max())throw std::runtime_error("Spell string block too large");auto o=static_cast<std::uint32_t>(out.strings.size());out.strings.insert(out.strings.end(),s.begin(),s.end());out.strings.push_back(0);interned.emplace(s,o);return o;};
	std::set<std::pair<std::string,std::string>> identities;
	for(auto const&r:additions){if(!BehaviorMatches(r)||!occupied.insert(r.id).second||!identities.emplace(r.packageKey,r.symbol).second)throw std::runtime_error("Invalid/colliding managed Spell row");auto w=r.words;for(std::size_t g=0;g<4;++g){auto base=std::array<std::size_t,4>{NAME,RANK,DESCRIPTION,AURA_DESCRIPTION}[g];for(std::size_t i=0;i<16;++i)w[base+i]=intern(r.localized[g][i]);}out.words.insert(out.words.end(),w.begin(),w.end());++out.recordCount;}
	out.stringBlockSize=static_cast<std::uint32_t>(out.strings.size());auto bytes=DbcReader::Serialize(out);auto d=FindDbcDescriptor(12340,"Spell");auto check=d?DbcReader::Parse(bytes,*d):DbcReadResult{};if(!check.valid||check.document.recordCount!=baseline.recordCount+additions.size()||!std::equal(baseline.words.begin(),baseline.words.end(),check.document.words.begin())||!std::equal(baseline.strings.begin(),baseline.strings.end(),check.document.strings.begin()))throw std::runtime_error("Composed Spell.dbc failed preservation/readback validation: "+check.error);return bytes;
}
