# Generic managed Spell capability

Content Manager can own a paired client/server Spell definition for client build
12340. A declaration is optional in Schema 2 or 3 and rejected in Schema 1:

```json
"spells": [{
  "symbol": "informational-aura",
  "copyFrom": 19,
  "profile": "informational-self-aura-v1",
  "name": {"enUS": "Informational Aura"},
  "description": {"enUS": "No gameplay effect."},
  "auraDescription": {"enUS": "Informational only."},
  "iconCopyFromSpell": 19
}]
```

`symbol`, a stock `copyFrom`, the profile, and an authored `enUS` name are
required. The descriptions and `iconCopyFromSpell` are optional. Supported
locales are enUS, koKR, frFR, deDE, zhCN, zhTW, esES, esMX, and ruRU. Omitted
translations fall back to enUS. Icon selection copies a verified nonzero
`SpellIconID` from a stock Spell; Phase A does not manage SpellIcon.dbc or BLPs.

## Profile and canonical data

`informational-self-aura-v1` is a non-passive, positive, permanent self aura. It
uses Effect 6, Aura 4, TargetA 1, DurationIndex 21, and ActiveIconID 0. The
composer clears all other behavioral fields, including attributes, cooldowns,
categories, proc data, secondary effects, periodic amplitudes, triggers,
reagents, totems, equipment and stance requirements, family masks, mechanics,
and combat targets. All four localized groups—name, rank/subtext, description,
and aura description—are serialized in their exact build-12340 positions.

The one nonzero neutral requirement sentinel is `EquippedItemClass`: its raw DBC
word is `0xFFFFFFFF`, which AzerothCore loads as signed `-1`. `Spell::CheckItems`
calls `Player::HasItemFitToSpellRequirements`; a class of zero is a real item
class requirement, while a negative class disables that requirement. The
subclass and inventory-type masks remain zero and are ignored with class `-1`.
The server SQL projection writes the same canonical word as signed `-1`.

A bounded audit of the remaining normalized restriction/effect fields found no
other nonzero neutral sentinel. AzerothCore skips reagent entries `<= 0`; zero
totem and totem-category IDs, spell focus, aura-state/spell, creature-type,
stance/form, area-group, level, category and cooldown fields impose no
requirement; zero proc fields disable proc behavior. The only enabled effect is
the permanent self-target dummy aura, so its zero signed die/base/misc values,
zero radius/chain/item/trigger fields, and zero secondary effects add no combat
or item behavior. These values are normalized explicitly and are never copied
from either donor.

The exact descriptor is 234 32-bit fields (936 bytes): 16 float words, 64
localized string-offset words, and the remaining integer/opaque words. The
composer preserves baseline records and strings byte-for-byte, appends managed
rows in deterministic ID order, rejects collisions, serializes, and reparses
the result before accepting it.

The audited stock baseline is:

- SHA-256: `d5cce1a83550dcfa9eb2f0251dbb11fd24c272534b2b1a9b230924a44d817ab3`
- 49,839 records; IDs 1 through 80,864; no duplicates
- 234 fields; 936-byte records; 2,317,797 string bytes

Place an independently verified `Spell.dbc` in the configured read-only baseline
directory and run `.content dbc inspect Spell`. The first build accepts it through
the normal registry only when it matches the compiled verified SHA-256 above;
later replacement still uses the review/approval flow and must also satisfy that
pin for this bounded build-12340 capability. The repository's ignored `reference/`
directory is only a development input and is never an implicit trust source.

## Allocation and deployment

`spell.id` is a persistent, non-recycling symbolic lease. Allocation begins at
the accepted baseline maximum plus one and skips accepted baseline IDs, current
`spell_dbc.ID` rows, and all retained leases, including removed packages. A
verified CM-owned row for the same retained identity is excluded from external
occupancy; an ownership mismatch or live-row drift aborts the build.

The maximum generated ID is 4,194,303. This reserves at most 64 MiB for the two
dense pointer arrays involved on a 64-bit server: `DBCStorage<SpellEntry>` and
`SpellMgr::mSpellInfoMap`. Exhaustion fails rather than expanding an arbitrary
custom range.

Uninstalling or removing the declaring package from a later cumulative build
does not recycle its `spell.id` lease. Content Manager also does not implicitly
delete the last managed server row during uninstall; the retained identity stays
reserved so a later reinstall cannot alias another Spell.

The server bundle carries the same canonical 234-field row used for the client
DBC. Apply validates every `spell_dbc` column name, type, nullability, and order.
It then locks and checks the existing row and generic owner snapshot before a
guarded InnoDB transaction replaces an owned row or inserts a new row and its
provenance. Unowned collisions and changed owned rows fail closed. Reapplication
of an already matching applied artifact verifies idempotently.

## Runtime lifecycle

1. Build the cumulative patch and review the Spell baseline/composed hashes.
2. Activate the build; required server content must reach APPLIED before client
   publication can complete.
3. Install the generated client patch and restart the client.
4. Restart worldserver so AzerothCore reloads `spell_dbc` and rebuilds SpellInfo.

The resource API resolves `spell.id` only for the normal ACTIVE/APPLIED/parity
state and when the running `SpellMgr` has the expected loaded SpellInfo. Between
SQL apply and worldserver restart it returns Invalid and emits a restart-required
diagnostic. Loaded behavior mismatch also fails closed. Client-only localized
tooltip text is protected by the generated DBC and artifact hashes rather than
SpellInfo, which intentionally does not expose every client string.

A future consumer declares the Spell in its own generic EPF as above, vendors
`src/api/ContentResourceApiV1.h`, discovers the optional provider through the
existing `WorldScript` RTTI pattern, and calls
`ResolveResource(package, symbol, "spell.id", value, reason)`. It must use the
value only for `Result::Ready`; a retained lease by itself is deliberately inert.

Custom icons remain future scope: supporting them requires a separately designed
managed SpellIcon.dbc identity plus client BLP asset lifecycle. This Phase A
capability intentionally provides neither.
