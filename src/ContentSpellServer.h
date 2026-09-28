#ifndef CONTENT_SPELL_SERVER_H
#define CONTENT_SPELL_SERVER_H
#include "SpellDbcComposer.h"
#include "ContentResourceAllocator.h"
#include <set>
#include <string>
#include <vector>

class ContentSpellServer {
public:
	static std::vector<std::string> Columns();
	static std::string Snapshot(ResolvedSpell const&);
	static bool ValidateSchema(std::string& error);
	static bool Occupancy(std::string const& realm,
		std::vector<ItemAllocation> const& retained,
		std::set<std::uint32_t>& occupied, std::string& error);
	static bool Check(ResolvedSpell const&, std::string const& realm, bool& exists,
		ResolvedSpell& current, std::string& error);
	static std::string Condition(ResolvedSpell const& current,
		std::string const& realm, bool exists);
	static std::vector<std::string> ApplySql(ResolvedSpell const& desired,
		ResolvedSpell const& current, std::string const& realm, bool exists,
		std::uint32_t build, std::string const& artifactHash);
	static std::string VerificationSql(ResolvedSpell const&,
		std::string const& realm, std::uint32_t build,
		std::string const& artifactHash);
	static bool Verify(ResolvedSpell const&, std::string const& realm,
		std::uint32_t build, std::string const& artifactHash, std::string& error);
};
#endif
