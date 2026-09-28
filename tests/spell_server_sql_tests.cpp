#include "ContentServerBundle.h"
#include "ContentSpellServer.h"
#include "DatabaseEnv.h"

#include <cassert>
#include <iostream>

std::array<std::size_t, 64> SpellDbcComposer::StringFields()
{
	return {};
}

bool SpellDbcComposer::BehaviorMatches(ResolvedSpell const&)
{
	return true;
}

QueryResult TestDatabase::Query(std::string_view)
{
	return {};
}

std::string ContentServerBundle::SqlText(std::string const& value)
{
	static char const digits[] = "0123456789abcdef";
	std::string sql = "CONVERT(X'";
	for (unsigned char byte : value)
	{
		sql += digits[byte >> 4];
		sql += digits[byte & 15];
	}
	return sql + "' USING utf8mb4)";
}

std::string ContentServerBundle::SqlIdentityText(std::string const& value)
{
	return SqlText(value) + " COLLATE utf8mb4_bin";
}

int main()
{
	ResolvedSpell spell;
	spell.packageKey = "cm-managed-spell-ptr-test";
	spell.packageVersion = "1.0.0";
	spell.symbol = "managed-aura-test";
	spell.profile = "informational-self-aura-v1";
	spell.id = 80865;
	spell.copyFrom = 1243;
	spell.iconCopyFromSpell = 1243;
	spell.words[0] = spell.id;
	spell.localized[0][0] = "CM Managed Aura Test";
	spell.localized[0][3] = "Aura g\xC3\xA9" "r\xC3\xA9" "e";
	spell.localized[2][0] =
		"Disposable PTR validation spell for Content Manager.";
	spell.localized[3][0] =
		"Informational aura with no gameplay effect.";

	auto condition = ContentSpellServer::Condition(spell, "PTR", true);
	auto values = ContentSpellServer::ApplySql(
		spell, spell, "PTR", false, 44, std::string(64, 'a')).front();

	auto const unicode = " USING utf8mb4) COLLATE utf8mb4_unicode_ci";
	assert(condition.find("t.`Name_Lang_enUS`=CONVERT(X'") !=
		std::string::npos);
	assert(condition.find("t.`Name_Lang_frFR`=CONVERT(X'") !=
		std::string::npos);
	assert(condition.find("t.`Description_Lang_enUS`=CONVERT(X'") !=
		std::string::npos);
	assert(condition.find("t.`AuraDescription_Lang_enUS`=CONVERT(X'") !=
		std::string::npos);
	assert(condition.find(unicode) != std::string::npos);
	assert(values.find(unicode) != std::string::npos);
	assert(condition.find(" USING utf8mb4) AND") == std::string::npos);
	assert(condition.find("utf8mb4_0900_ai_ci") == std::string::npos);
	assert(values.find("utf8mb4_0900_ai_ci") == std::string::npos);
	assert(condition.find("o.row_json=CONVERT(X'") != std::string::npos);
	assert(condition.find(" USING utf8mb4) COLLATE utf8mb4_bin") !=
		std::string::npos);

	std::cout << "managed Spell SQL uses explicit utf8mb4_unicode_ci for all "
				 "localized values and utf8mb4_bin for ownership: PASS\n";
}
