#ifndef CONTENT_SPELL_DBC_COMPOSER_H
#define CONTENT_SPELL_DBC_COMPOSER_H

#include "ContentPackage.h"
#include "DbcReader.h"
#include <array>
#include <map>
#include <set>

struct ResolvedSpell {
	std::string packageKey, packageVersion, symbol, profile;
	std::uint32_t id = 0, copyFrom = 0, iconCopyFromSpell = 0;
	std::array<std::uint32_t, 234> words{}; // String fields are canonical zeroes.
	std::array<std::array<std::string, 16>, 4> localized{};
};

class SpellDbcComposer {
public:
	static constexpr char const *Profile = "informational-self-aura-v1";
	static constexpr char const *VerifiedBaselineSha256 =
		"d5cce1a83550dcfa9eb2f0251dbb11fd24c272534b2b1a9b230924a44d817ab3";
	static constexpr std::uint32_t FieldCount = 234;
	static constexpr std::uint32_t RecordSize = 936;
	static constexpr std::uint32_t PermanentDuration = 21;
	static std::array<std::size_t, 64> StringFields();
	static std::string String(DbcDocument const &, std::size_t row,
							 std::size_t field);
	static std::set<std::uint32_t> Inspect(DbcDocument const &);
	static void ValidateDeclaration(ContentSpellRow const &);
	static ResolvedSpell Resolve(DbcDocument const &, ContentSpellRow const &,
		std::string const &package, std::string const &version, std::uint32_t id);
	static std::vector<std::uint8_t> Compose(DbcDocument const &,
		std::vector<ResolvedSpell> additions);
	static bool BehaviorMatches(ResolvedSpell const &);
};

#endif
