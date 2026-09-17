#include "Chat.h"
#include "CommandScript.h"
#include "ContentManager.h"
#include "ContentBuildService.h"
#include "ContentBuildRegistry.h"
#include <charconv>
#include "World.h"
#include "WorldSession.h"
#include "Realm.h"
#include "ContentPackage.h"
#include "ContentPackageRegistry.h"
#include "MpqBuilder.h"
#include "RBAC.h"
#include "ContentBuildHash.h"
#include "ContentBuildPaths.h"
#include "DbcDescriptor.h"
#include "DbcReader.h"
#include "CurrencyDbcComposer.h"
#include "CurrencyCategoryDbcComposer.h"
#include "ContentAllocationRegistry.h"
#include "ContentBaselineRegistry.h"
#include "ContentExtendedCostServer.h"
#include "ContentServerDeployment.h"

#include <algorithm>
#include <set>
#include <stdexcept>

using namespace Acore::ChatCommands;

namespace
{
bool CleanupStagingDirectory(std::filesystem::path const& stagingDirectory,
    std::filesystem::path const& workDirectory, std::string& error)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (stagingDirectory.empty() || workDirectory.empty())
    {
        error = "Staging and work directory paths must not be empty.";
        return false;
    }

    auto work = fs::canonical(workDirectory, ec);
    if (ec)
    {
        error = "Could not resolve work directory: " + ec.message();
        return false;
    }

    auto staging = fs::absolute(stagingDirectory, ec);
    if (ec)
    {
        error = "Could not resolve staging directory: " + ec.message();
        return false;
    }

    // Refuse links before canonicalization so a redirected staging path cannot
    // cause cleanup of another package, even if its target is inside WorkDirectory.
    fs::path prefix;
    for (auto const& component : staging)
    {
        prefix /= component;
        auto status = fs::symlink_status(prefix, ec);
        if (ec || fs::is_symlink(status))
        {
            error = ec ? "Could not inspect staging path: " + ec.message()
                : "Refusing cleanup through a symlink.";
            return false;
        }
    }

    staging = fs::canonical(staging, ec);
    if (ec)
    {
        error = "Could not resolve staging directory: " + ec.message();
        return false;
    }
    auto relative = staging.lexically_relative(work);
    if (relative.empty() || relative == "." || relative.is_absolute()
        || *relative.begin() == "..")
    {
        error = "Refusing cleanup: staging directory must be strictly beneath WorkDirectory.";
        return false;
    }
    if (!fs::is_directory(staging, ec) || ec)
    {
        error = ec ? "Could not inspect staging directory: " + ec.message()
            : "Staging path is not a directory.";
        return false;
    }

    // Delete only the validated directory derived from the successful Stage result.
    fs::remove_all(staging, ec);
    if (ec)
    {
        error = "Could not remove staging directory: " + ec.message();
        return false;
    }
    return true;
}
}

class content_manager_commandscript : public CommandScript
{
public:
    content_manager_commandscript()
        : CommandScript("content_manager_commandscript")
    {
    }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable buildCommandTable =
        {
            { "list", HandleBuildListCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
            { "", HandleBuildCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        };
        static ChatCommandTable dbcCommandTable =
        {
            { "inspect", HandleDbcInspectCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
            { "review", HandleDbcReviewCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
            { "approve", HandleDbcApproveCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes }
        };
        static ChatCommandTable serverCommandTable =
        {
            { "apply", HandleServerApplyCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
            { "status", HandleServerStatusCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes }
        };
        static ChatCommandTable contentCommandTable =
        {
            { "activate", HandleActivateCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
            { "allocations", HandleAllocationsCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
            { "build", buildCommandTable },
            { "dbc", dbcCommandTable },
            { "server", serverCommandTable },
            { "install", HandleInstallCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
            { "uninstall", HandleUninstallCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
            {
                "status",
                HandleStatusCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::Yes
            },
            {
                "scan",
                HandleScanCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::Yes
            },
			{ 
				"stage",
				 HandleStageCommand,
				 rbac::RBAC_PERM_COMMAND_SERVER_INFO,
				 Console::Yes
			},
        };

        static ChatCommandTable commandTable =
        {
            {
                "content",
                contentCommandTable
            }
        };

        return commandTable;
    }

    static bool HandleStatusCommand(ChatHandler* handler)
    {
        handler->PSendSysMessage(
            "Content Manager: {}",
            sContentManager.IsEnabled()
                ? "enabled"
                : "disabled");

        handler->PSendSysMessage(
            "PatchHold: {}",
            sContentManager.GetPatchHoldDirectory());

        handler->PSendSysMessage(
            "Work: {}",
            sContentManager.GetWorkDirectory());

        handler->PSendSysMessage(
            "Output: {}",
            sContentManager.GetOutputDirectory());
        handler->PSendSysMessage("Publish Directory: {}", sContentManager.GetPublishDirectory());

        handler->PSendSysMessage("DBC baseline: {}", sContentManager.GetBaselineDbcDirectory().empty()
            ? "unconfigured" : sContentManager.GetBaselineDbcDirectory());
        handler->PSendSysMessage("DBC client build: {}", sContentManager.GetClientBuild());

        return true;
    }

    static bool HandleAllocationsCommand(ChatHandler* handler)
    {
        std::vector<ItemAllocation> rows;
        std::string error;
        if (!ContentAllocationRegistry().Read(realm.Name, rows, error))
        {
            handler->PSendSysMessage("Allocation lookup failed: {}", error);
            return true;
        }
        handler->PSendSysMessage("Retained resource allocations for realm {}: {}", realm.Name, rows.size());
        for (auto const& row : rows)
            handler->PSendSysMessage("{} / {} / {} = {} [{}], builds {}..{}, baseline {}",
                row.packageKey, row.symbol, row.resourceKind, row.value, row.state, row.firstBuild, row.lastBuild,
                row.baselineSha256);
        return true;
    }

    static bool ParseBuildNumber(std::string const& text, std::uint32_t& value)
    {
        if (text.empty()) return false;
        auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        return parsed.ec == std::errc() && parsed.ptr == text.data() + text.size() && value > 0;
    }

    static bool HandleServerStatusCommand(ChatHandler* handler, std::string argument)
    {
        std::uint32_t number = 0;
        if (!argument.empty() && !ParseBuildNumber(argument, number))
        { handler->SendSysMessage("Usage: .content server status [build-number]"); return true; }
        std::vector<ContentBuildRecord> builds;
        std::string error;
        if (!ContentBuildRegistry().GetBuilds(builds, error))
        { handler->PSendSysMessage("Server status failed: {}", error); return true; }
        if (!number)
            for (auto const& build : builds)
                if (build.realmName == realm.Name)
                {
                    bool exists = false;
                    ContentServerStatus candidate;
                    if (!ContentServerDeployment::ReadStatus(build.buildNumber, exists, candidate, error))
                    { handler->PSendSysMessage("Server status failed: {}", error); return true; }
                    if (exists) { number = build.buildNumber; break; }
                }
        auto build = std::find_if(builds.begin(), builds.end(), [&](auto const& row) {
            return row.buildNumber == number && row.realmName == realm.Name;
        });
        if (build == builds.end())
        { handler->SendSysMessage("No realm build found."); return true; }
        ContentServerStatus status;
        std::vector<ResolvedServerItem> rows;
        if (!ContentServerDeployment::Inspect(number, realm.Name, sContentManager.GetOutputDirectory(),
            status, rows, error))
        { handler->PSendSysMessage("Server status unavailable: {}", error); return true; }
        handler->PSendSysMessage("Build: {}  server: {}  client: {}", ContentBuildService::Number(number),
            status.state, build->state);
        handler->PSendSysMessage("Server item_template rows: {}", rows.size());
        for (auto const& row : rows)
        {
            handler->PSendSysMessage("{} / {} / item.id = {}; item_template.entry = {}",
                row.packageKey, row.symbol, row.id, row.id);
            if (row.currency.itemId)
                handler->PSendSysMessage("{} / {} / currency.known-bit = {}; CurrencyTypes / currencytypes_dbc ID=ItemID={}; CategoryID={}; BagFamily=8192",
                    row.packageKey, row.currency.symbol, row.currency.bitIndex, row.id, row.currency.categoryId);
        }
        handler->PSendSysMessage("Server bundle SHA-256: {}", status.bundleSha256);
        handler->PSendSysMessage("Parity manifest SHA-256: {}", status.paritySha256);
        return true;
    }

    static bool HandleServerApplyCommand(ChatHandler* handler, std::string argument)
    {
        std::uint32_t number = 0;
        if (!ParseBuildNumber(argument, number))
        { handler->SendSysMessage("Usage: .content server apply <build-number>"); return true; }
        std::string summary, error;
        if (!ContentServerDeployment::Apply(number, realm.Name,
            sContentManager.GetOutputDirectory(), summary, error))
            handler->PSendSysMessage("Server apply refused: {}", error);
        else
            handler->SendSysMessage(summary);
        return true;
    }

    static void ReportIds(ChatHandler* handler, char const* label, std::set<std::uint32_t> const& ids)
    {
        handler->PSendSysMessage("{}: {} distinct nonzero identities", label, ids.size());
        std::string line;
        for (auto value : ids)
        {
            if (line.size() > 350) { handler->PSendSysMessage("{}", line); line.clear(); }
            line += (line.empty() ? "" : ",") + std::to_string(value);
        }
        if (!line.empty()) handler->PSendSysMessage("{}", line);
    }

    static bool HandleDbcInspectCommand(ChatHandler* handler, std::string table)
    {
        if (table.empty())
        {
            handler->SendSysMessage("Usage: .content dbc inspect <table>");
            return true;
        }
        if (!IsKnownDbcTable(table))
        {
            handler->PSendSysMessage("Unsupported DBC table '{}'. Registered tables: Item, CurrencyTypes, CurrencyCategory, ItemExtendedCost.", table);
            return true;
        }
        auto build = sContentManager.GetClientBuild();
        auto descriptor = FindDbcDescriptor(build, table);
        if (!descriptor)
        {
            handler->PSendSysMessage("Unsupported DBC client build {}. This inspector supports build 12340 only.", build);
            return true;
        }
        auto const& configured = sContentManager.GetBaselineDbcDirectory();
        if (configured.empty())
        {
            handler->SendSysMessage("DBC baseline is unconfigured. Set ContentManager.BaselineDbcDirectory for inspection.");
            return true;
        }
        try
        {
            namespace fs = std::filesystem;
            auto inspected = ContentBaselineRegistry::Inspect(configured, *descriptor);
            DbcReadResult parsed{true, {}, inspected.document};
            fs::path source = inspected.source, directory = source.parent_path();
            auto sha256 = inspected.hash;
            auto currencyOccupancy = table == "CurrencyTypes"
                ? CurrencyDbcComposer::Inspect(parsed.document) : CurrencyOccupancy{};
            auto categoryIds = table == "CurrencyCategory"
                ? CurrencyCategoryDbcComposer::Inspect(parsed.document) : std::set<std::uint32_t>{};
            ExtendedCostReferences costReferences;
            ExtendedCostOccupancy costOccupancy;
            if (table == "ItemExtendedCost")
            {
                costOccupancy = ItemExtendedCostDbc::Inspect(parsed.document);
                std::string error;
                if (!ContentExtendedCostServer::References(costReferences,error)) throw std::runtime_error(error);
            }
            handler->PSendSysMessage("DBC inspection PASS: {} (descriptor v{})", table, descriptor->version);
            handler->PSendSysMessage("Client build: {}", build);
            handler->PSendSysMessage("Baseline directory: {}", directory.string());
            handler->PSendSysMessage("Source: {}", source.string());
            handler->PSendSysMessage("SHA-256: {}", sha256);
            handler->PSendSysMessage("Records: {}; fields: {}; record bytes: {}; string bytes: {}",
                parsed.document.recordCount, parsed.document.fieldCount,
                parsed.document.recordSize, parsed.document.stringBlockSize);
            if (table == "CurrencyTypes")
            {
                handler->SendSysMessage("Fields: ID int32, ItemID int32, CategoryID int32, BitIndex int32; no string fields.");
                handler->SendSysMessage("BitIndex is one-based: 1..64. ID is distinct from the ItemID lookup key.");
                std::string bits;
                for (auto bit : currencyOccupancy.bits) bits += (bits.empty() ? "" : ",") + std::to_string(bit);
                handler->PSendSysMessage("Baseline occupied known-bit indexes: {}", bits);
            }
            if (table == "CurrencyCategory")
            {
                handler->SendSysMessage("Fields: ID int32, Flags int32, 16 localized string offsets uint32, NameFlags uint32 (19 fields / 76 bytes).");
                std::string ids;
                for (auto id : categoryIds) ids += (ids.empty() ? "" : ",") + std::to_string(id);
                handler->PSendSysMessage("Physically occupied CurrencyCategory IDs: {}", ids);
                for (std::size_t i = 0; i < parsed.document.recordCount; ++i)
                    handler->PSendSysMessage("Category {}: enUS='{}', Flags={}, NameFlags={}",
                        parsed.document.words[i * 19], CurrencyCategoryDbcComposer::Name(parsed.document, i, 0),
                        parsed.document.words[i * 19 + 1], parsed.document.words[i * 19 + 18]);
                handler->SendSysMessage("No hash is required for inspection. Build also reserves baseline CurrencyTypes CategoryID references, including dangling references.");
            }
            if (table == "ItemExtendedCost")
            {
                handler->SendSysMessage("WDBC build 12340: 16 int32 words / 64 bytes, no string-offset fields.");
                for (std::size_t i=0;i<descriptor->fields.size();++i)
                    handler->PSendSysMessage("Word {} / byte {}: {} (int32)",i,i*4,descriptor->fields[i].name);
                handler->SendSysMessage("Words 4..8 are item IDs; words 9..13 are paired counts. Rating is word 14; ItemPurchaseGroup is word 15 (preserved, core ignores it).");
                handler->SendSysMessage("Generated ID domain: 1..65535, limited by the core's uint16 refundable paidExtendedCost persistence.");
                ReportIds(handler,"Physical baseline extended-cost IDs",costOccupancy.ids);
                ReportIds(handler,"Baseline required Item IDs",costOccupancy.items);
                ReportIds(handler,"Baseline ItemPurchaseGroup references",costOccupancy.purchaseGroups);
                ReportIds(handler,"SQL itemextendedcost_dbc IDs",costReferences.overlay);
                ReportIds(handler,"SQL overlay required Item IDs",costReferences.items);
                ReportIds(handler,"npc_vendor.ExtendedCost references",costReferences.vendors);
                ReportIds(handler,"game_event_npc_vendor.ExtendedCost references",costReferences.events);
                ReportIds(handler,"item_refund_instance.paidExtendedCost references",costReferences.refunds);
            }
            std::string registryStatus, registryError;
            if (ContentBaselineRegistry::Status(inspected, registryStatus, registryError))
                handler->PSendSysMessage("Baseline registry: {}", registryStatus);
            else handler->PSendSysMessage("Baseline registry unavailable: {}", registryError);
            handler->SendSysMessage("Inspection is read-only. Use review/approve for an intentional replacement; first build registers an unregistered validated baseline.");
        }
        catch (std::exception const& e)
        {
            handler->PSendSysMessage("DBC inspection failed for '{}': {}", table, e.what());
        }
        return true;
    }

    static std::string BaselinePin(std::string const& table)
    {
        return table == "Item" ? sContentManager.GetItemBaselineSha256()
            : table == "CurrencyTypes" ? sContentManager.GetCurrencyTypesBaselineSha256() : "";
    }

    static bool BaselineAdmin(ChatHandler* handler, std::string const& table, std::uint64_t review)
    {
        if (handler->GetSession() && handler->GetSession()->GetSecurity() < SEC_ADMINISTRATOR)
        { handler->SendSysMessage("Baseline review/approval requires administrator access."); return true; }
        try
        {
            auto descriptor = FindDbcDescriptor(sContentManager.GetClientBuild(), table);
            if (!descriptor) throw std::runtime_error("Unsupported table/client build");
            auto b = ContentBaselineRegistry::Inspect(sContentManager.GetBaselineDbcDirectory(), *descriptor);
            std::string error;
            std::string actor = handler->GetSession()
                ? "account:" + std::to_string(handler->GetSession()->GetAccountId()) : "console";
            if (review)
            {
                if (!ContentBaselineRegistry::Approve(b, BaselinePin(table), review, actor, error))
                    throw std::runtime_error(error);
                handler->PSendSysMessage("Baseline replacement approved: {} SHA-256 {}. Existing leases/artifacts are preserved; rebuild performs occupancy validation.", table, b.hash);
            }
            else
            {
                std::uint64_t number = 0;
                if (!ContentBaselineRegistry::Review(b, actor, number, error)) throw std::runtime_error(error);
                std::string status;
                if (!ContentBaselineRegistry::Status(b, status, error)) throw std::runtime_error(error);
                handler->PSendSysMessage("Accepted baseline: {}", status);
                handler->PSendSysMessage("Review {}: {} build {} descriptor v{}, source {}, SHA-256 {}; records {}, fields {}, record bytes {}, string bytes {}",
                    number, table, b.clientBuild, b.descriptorVersion, b.source, b.hash,
                    b.document.recordCount, b.document.fieldCount, b.document.recordSize, b.document.stringBlockSize);
                handler->PSendSysMessage("Approve these inspected bytes only with .content dbc approve {} {}. No baseline has changed.", table, number);
            }
        }
        catch (std::exception const& e) { handler->PSendSysMessage("Baseline operation refused: {}", e.what()); }
        return true;
    }

    static bool HandleDbcReviewCommand(ChatHandler* handler, std::string table)
    { return BaselineAdmin(handler, table, 0); }

    static bool HandleDbcApproveCommand(ChatHandler* handler, std::string table, std::string token)
    {
        std::uint64_t review = 0;
        auto parsed = std::from_chars(token.data(), token.data() + token.size(), review);
        if (!review || parsed.ec != std::errc() || parsed.ptr != token.data() + token.size())
        { handler->SendSysMessage("Usage: .content dbc approve <table> <review-number>"); return true; }
        return BaselineAdmin(handler, table, review);
    }

    static bool HandleScanCommand(ChatHandler* handler)
    {
        auto installed = ContentPackageRegistry().GetInstalledPackages();
        if (!installed.success)
        {
            handler->PSendSysMessage("Cannot read package states: {}", installed.error);
            return true;
        }
        auto packages = sContentManager.ScanAvailablePackages();
        handler->PSendSysMessage("Content Manager found {} EPF package(s).", packages.size());
        std::set<std::string> displayed;
        for (auto const& candidate : packages)
        {
            auto validation = ContentPackage(candidate.path).Validate();
            if (!validation.valid)
            {
                handler->PSendSysMessage(" - {}: INVALID: {}", candidate.path.string(), validation.error);
                continue;
            }
            auto const& manifest = validation.manifest;
            auto row = std::find_if(installed.packages.begin(), installed.packages.end(),
                [&](InstalledContentPackage const& package) { return package.packageKey == manifest.packageKey; });
            handler->PSendSysMessage("Package: {}", manifest.packageKey);
            if (row == installed.packages.end())
                handler->SendSysMessage("  State: AVAILABLE");
            else
            {
                ReportInstalledPackage(handler, *row);
                displayed.insert(row->packageKey);
            }
            handler->PSendSysMessage("  Name: {}", manifest.name);
            handler->PSendSysMessage("  Available version: {}", manifest.version);
            handler->PSendSysMessage("  Provider: {}", candidate.provider);
            handler->PSendSysMessage("  Source: {}", candidate.path.string());
            handler->PSendSysMessage("  Schema: {}", manifest.schema);
            handler->PSendSysMessage("  Content: {} item(s)", manifest.content.size());
            handler->PSendSysMessage("  DBC rows: {}", manifest.itemRows.size());
            handler->PSendSysMessage("  Server item_template rows: {}", manifest.serverItemRows.size());
            for (auto const& entry : manifest.content)
                handler->PSendSysMessage("    {} -> {}", entry.type, entry.target);
        }
        // Discovery cannot show a removed module. Still display its persistent row.
        for (auto const& row : installed.packages)
        {
            if (displayed.count(row.packageKey))
                continue;
            handler->PSendSysMessage("Package: {}", row.packageKey);
            ReportInstalledPackage(handler, row);
            handler->PSendSysMessage("  Installed name: {}", row.name);
            handler->SendSysMessage("  No valid EPF with this key is currently discovered.");
        }
        return true;
    }

    static void ReportInstalledPackage(ChatHandler* handler, InstalledContentPackage const& package)
    {
        std::error_code ec;
        auto status = std::filesystem::status(package.sourcePath, ec);
        if (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory
            || (!ec && !std::filesystem::exists(status)))
            handler->SendSysMessage("  State: INSTALLED - SOURCE MISSING");
        else if (ec || !std::filesystem::is_regular_file(status))
            handler->PSendSysMessage("  State: INSTALLED - SOURCE UNAVAILABLE: {}",
                ec ? ec.message() : "Source is not a regular file");
        else
            handler->SendSysMessage("  State: INSTALLED");
        handler->PSendSysMessage("  Installed version: {}", package.version);
        handler->PSendSysMessage("  Installed provider: {}", package.provider);
        handler->PSendSysMessage("  Installed source: {}", package.sourcePath);
        handler->PSendSysMessage("  Installed at: {}", package.installedAt);
    }

    static bool HandleInstallCommand(ChatHandler* handler, std::string packageKey)
    {
        if (packageKey.empty())
        {
            handler->SendSysMessage("Usage: .content install <package-key>");
            return true;
        }
        std::vector<ContentPackageCandidate> matches;
        for (auto const& candidate : sContentManager.ScanAvailablePackages())
        {
            auto validation = ContentPackage(candidate.path).Validate();
            // Count any readable manifest declaring this key, even when a later
            // content validation fails. Never choose arbitrarily between matches.
            if (validation.manifest.packageKey == packageKey)
                matches.push_back(candidate);
        }
        if (matches.empty())
        {
            handler->PSendSysMessage("Package '{}' is not available.", packageKey);
            return true;
        }
        if (matches.size() != 1)
        {
            handler->PSendSysMessage("Cannot install '{}': multiple discovered EPFs declare this package key.", packageKey);
            for (auto const& candidate : matches)
                handler->PSendSysMessage("  Conflict: {} (provider: {})", candidate.path.string(), candidate.provider);
            return true;
        }
        auto const& candidate = matches.front();
        // Validate again immediately before asking the registry to change desired state.
        auto validation = ContentPackage(candidate.path).Validate();
        if (!validation.valid || validation.manifest.packageKey != packageKey)
        {
            handler->PSendSysMessage("Cannot install '{}': {}", packageKey,
                validation.valid ? "Package key changed during validation" : validation.error);
            return true;
        }
        std::error_code ec;
        auto source = std::filesystem::canonical(candidate.path, ec);
        if (ec)
        {
            handler->PSendSysMessage("Cannot resolve package source: {}", ec.message());
            return true;
        }
        auto const& manifest = validation.manifest;
        auto result = ContentPackageRegistry().Install({manifest.packageKey, manifest.name,
            manifest.version, candidate.provider, source.string(), {}});
        if (!result.success)
        {
            handler->PSendSysMessage("Install failed: {}", result.error);
            return true;
        }
        if (!result.changed)
        {
            handler->PSendSysMessage("Package '{}' is already installed. Installed metadata was not changed.", packageKey);
            return true;
        }
        handler->SendSysMessage("Installed package:");
        handler->PSendSysMessage("  {} {}", packageKey, manifest.version);
        handler->PSendSysMessage("  Provider: {}", candidate.provider);
        handler->SendSysMessage("Only the desired package set changed. Run .content build to generate a cumulative MPQ; no patch was rebuilt or published.");
        return true;
    }

    static bool HandleUninstallCommand(ChatHandler* handler, std::string packageKey)
    {
        if (packageKey.empty())
        {
            handler->SendSysMessage("Usage: .content uninstall <package-key>");
            return true;
        }
        auto result = ContentPackageRegistry().Uninstall(packageKey);
        if (!result.success)
        {
            handler->PSendSysMessage("Uninstall failed: {}", result.error);
            return true;
        }
        if (!result.changed)
        {
            handler->PSendSysMessage("Package '{}' is not installed.", packageKey);
            return true;
        }
        handler->PSendSysMessage("Uninstalled package: {}", packageKey);
        handler->SendSysMessage("The EPF was preserved. Run .content build to generate a cumulative MPQ; no patch was rebuilt or published.");
        return true;
    }

    static bool HandleBuildListCommand(ChatHandler* handler)
    {
        std::vector<ContentBuildRecord> records;
        std::string error;
        if (!ContentBuildRegistry().GetBuilds(records, error))
            handler->PSendSysMessage("Cannot list builds: {}", error);
        else if (records.empty())
            handler->SendSysMessage("No realm content builds exist.");
        else
        {
            handler->SendSysMessage("Realm content builds:");
            for (auto const& row : records)
            {
                handler->PSendSysMessage("{}  {}", ContentBuildService::Number(row.buildNumber), row.state);
                handler->PSendSysMessage("  {}", row.filename);
                handler->PSendSysMessage("  Packages: {}", row.packageCount);
                handler->PSendSysMessage("  Files: {}", row.fileCount);
                handler->PSendSysMessage("  SHA256: {}", row.sha256);
            }
        }
        return true;
    }

    static bool HandleActivateCommand(ChatHandler* handler, std::string argument)
    {
        if (handler->GetSession() && handler->GetSession()->GetSecurity() < SEC_ADMINISTRATOR)
        {
            handler->SendSysMessage("Build activation requires administrator access.");
            return true;
        }
        if (!sContentManager.IsEnabled())
        {
            handler->SendSysMessage("Content Manager is disabled.");
            return true;
        }
        std::uint32_t number = 0;
        auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), number);
        if (parsed.ec != std::errc() || parsed.ptr != argument.data() + argument.size() || !number)
        {
            handler->SendSysMessage("Invalid build number: expected a positive 32-bit decimal integer.");
            return true;
        }
        std::string error;
        bool alreadyActive = false;
        ContentBuildRegistry registry;
        ContentPublicationResult publication;
        if (!registry.ActivateBuild(number, sContentManager.GetOutputDirectory(),
            sContentManager.GetPublishDirectory(), publication, alreadyActive, error))
            handler->PSendSysMessage("Activation refused: {}", error);
        else
        {
            handler->PSendSysMessage("Build {} verified.", ContentBuildService::Number(number));
            handler->PSendSysMessage("Published: {}", publication.path.string());
            handler->PSendSysMessage("SHA256: {}", publication.sha256);
            if (publication.reused)
                handler->SendSysMessage("Matching published artifact reused.");
            handler->PSendSysMessage("Build {} {}", ContentBuildService::Number(number),
                alreadyActive ? "is already ACTIVE." : "is now ACTIVE.");
        }
        return true;
    }

    static bool HandleBuildCommand(ChatHandler* handler)
    {
        auto result = ContentBuildService().Build(sContentManager, realm.Name,
            [handler](std::string const& message) { handler->PSendSysMessage("{}", message); });
        if (!result.success)
        {
            handler->PSendSysMessage("Content build failed: {}", result.error);
            if (!result.workspace.empty())
                handler->PSendSysMessage("Workspace preserved: {}", result.workspace.string());
            if (result.mpqCreated)
                handler->PSendSysMessage("Completed MPQ preserved: {}", result.outputPath.string());
            // The failure was reported above; do not append generic command usage.
            return true;
        }
        return true;
    }
	static bool HandleStageCommand(
	    ChatHandler* handler,
	    std::string packageKey)
	{
	    if (packageKey.empty())
	    {
	        handler->SendSysMessage(
	            "Usage: .content stage <package-key>");

	        return true;
	    }

        // The package key is used as a single staging/output path component.
        if (packageKey == "." || packageKey == ".."
            || packageKey.find_first_of("/\\:") != std::string::npos)
        {
            handler->SendSysMessage("Invalid package key: expected a single directory name.");
            return true;
        }
	    auto packages =
	        sContentManager.ScanAvailablePackages();

	    for (auto const& candidate : packages)
	    {
	        ContentPackage package(candidate.path);

	        ContentPackageValidationResult validation =
	            package.Validate();

	        if (!validation.valid)
	            continue;

	        if (validation.manifest.packageKey != packageKey)
	            continue;

            if (!validation.manifest.itemRows.empty() || !validation.manifest.extendedCosts.empty())
            {
                handler->SendSysMessage("Schema 2 DBC rows require an installed cumulative .content build for allocation and composition.");
                return true;
            }

	        handler->PSendSysMessage(
	            "Staging package '{}'...",
	            validation.manifest.name);

	        ContentPackageStageResult result =
	            package.Stage(
	                sContentManager.GetWorkDirectory());

	        if (!result.success)
	        {
	            handler->PSendSysMessage(
	                "Stage failed: {}",
	                result.error);

	            return true;
	        }

	        handler->PSendSysMessage(
	            "Staged {} content item(s).",
	            result.stagedFiles.size());

	        for (auto const& file :
	             result.stagedFiles)
	        {
	            handler->PSendSysMessage(
	                " - {}",
	                file.string());
	        }

	        handler->SendSysMessage(
	            "Package staged successfully.");

            handler->PSendSysMessage("Staged files: {}", result.stagedFiles.size());
            auto output = std::filesystem::path(sContentManager.GetOutputDirectory())
                / (validation.manifest.packageKey + "-test.mpq");
            auto build = MpqBuilder().Build(result.stagingDirectory, output);
            if (!build.success)
            {
                handler->PSendSysMessage("MPQ build failed: {}", build.error);
                return true;
            }
            handler->SendSysMessage("MPQ built successfully.");
            handler->PSendSysMessage("MPQ: {}", build.outputPath.string());
            handler->PSendSysMessage("MPQ files: {}", build.fileCount);
            std::string cleanupError;
            if (CleanupStagingDirectory(result.stagingDirectory,
                sContentManager.GetWorkDirectory(), cleanupError))
            {
                handler->SendSysMessage("Staging cleanup completed.");
            }
            else
            {
                handler->PSendSysMessage(
                    "WARNING: MPQ was built successfully, but staging cleanup failed: {}",
                    cleanupError);
            }
	        return true;
	    }

	    handler->PSendSysMessage(
	        "Content package '{}' was not found.",
	        packageKey);

	    return true;
	}
};

void AddSC_content_manager_commands()
{
    new content_manager_commandscript();
}
