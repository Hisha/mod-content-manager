#include "ContentVendorServer.h"
#include "ContentServerBundle.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"
#include <algorithm>
#include <set>
#include <tuple>
namespace
{
using nlohmann::json;
auto T=ContentServerBundle::SqlIdentityText;
std::string N(std::uint32_t n){return std::to_string(n);}
std::string Snapshot(ResolvedVendorRow const& r)
{ auto j=ContentVendorServer::Objects({r})[0]; j.erase("packageVersion"); return j.dump(); }
std::string Identity(ResolvedVendorRow const& r,std::string const& realm)
{
    return "o.realm_name="+T(realm)+" AND o.package_key="+T(r.packageKey)+" AND o.symbol="+T(r.symbol)
        +" AND o.row_json="+T(Snapshot(r));
}
}
bool ContentVendorServer::Prepare(ResolvedVendorRow& r,std::string const& realm,std::string& error)
{
    auto schema=WorldDatabase.Query("SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND "
        "TABLE_NAME IN ('content_manager_vendor_owner','npc_vendor','creature_template','item_template') AND ENGINE='InnoDB'");
    if(!schema||schema->Fetch()[0].Get<std::uint64_t>()!=4){error="Vendor relationships require new module SQL and InnoDB tables";return false;}
    auto q=WorldDatabase.Query("SELECT c.npcflag,o.original_flags FROM creature_template c LEFT JOIN content_manager_vendor_owner o "
        "ON o.creature_entry=c.entry WHERE c.entry="+N(r.creatureEntry));
    if(!q){error="Declared existing vendor creature is missing";return false;}
    r.originalFlags=q->Fetch()[1].IsNull()?q->Fetch()[0].Get<std::uint32_t>():q->Fetch()[1].Get<std::uint32_t>();
    bool exists=false;return Check(r,realm,exists,error);
}
std::string ContentVendorServer::Condition(ResolvedVendorRow const& r,std::string const& realm,bool exists)
{
    std::string base="EXISTS(SELECT 1 FROM item_template WHERE entry="+N(r.itemEntry)+") AND "
        "NOT EXISTS(SELECT 1 FROM game_event_npc_vendor e JOIN creature c ON c.guid=e.guid WHERE c.id1="+N(r.creatureEntry)+" OR c.id2="+N(r.creatureEntry)+" OR c.id3="+N(r.creatureEntry)+") AND ";
    if(!exists)return base+"EXISTS(SELECT 1 FROM creature_template WHERE entry="+N(r.creatureEntry)+" AND npcflag="+N(r.flagsManaged?(r.originalFlags|128):r.originalFlags)+") AND "
        "NOT EXISTS(SELECT 1 FROM npc_vendor WHERE entry="+N(r.creatureEntry)+") AND NOT EXISTS(SELECT 1 FROM content_manager_vendor_owner "
        "WHERE creature_entry="+N(r.creatureEntry)+" OR (realm_name="+T(realm)+" AND package_key="+T(r.packageKey)+" AND symbol="+T(r.symbol)+"))";
    return base+"(SELECT COUNT(*) FROM npc_vendor WHERE entry="+N(r.creatureEntry)+")=1 AND EXISTS(SELECT 1 FROM "
        "npc_vendor v JOIN content_manager_vendor_owner o ON o.creature_entry=v.entry JOIN creature_template c ON c.entry=v.entry WHERE "
        "v.entry="+N(r.creatureEntry)+" AND v.item="+N(r.itemEntry)+" AND v.ExtendedCost="+N(r.costId)
        +" AND v.slot=0 AND v.maxcount=0 AND v.incrtime=0 AND v.VerifiedBuild=12340 AND c.npcflag="+N(r.originalFlags|128)
        +" AND o.original_flags="+N(r.originalFlags)+" AND "+Identity(r,realm)+")";
}
bool ContentVendorServer::Check(ResolvedVendorRow const& r,std::string const& realm,bool& exists,std::string& error)
{
    auto q=WorldDatabase.Query("SELECT CAST(("+Condition(r,realm,false)+") AS UNSIGNED),CAST(("+Condition(r,realm,true)+") AS UNSIGNED)");
    if(!q||(!q->Fetch()[0].Get<std::uint64_t>()&&!q->Fetch()[1].Get<std::uint64_t>()))
    {error="Vendor relationship collision, missing template, event vendor, ownership or NPC flag drift";return false;}
    exists=q->Fetch()[1].Get<std::uint64_t>()!=0;return true;
}
std::vector<std::string> ContentVendorServer::ApplySql(ResolvedVendorRow const& r,std::string const& realm,bool exists,std::uint32_t build,std::string const& hash)
{
    // Keep the semantic owner snapshot stable across package-version-only updates.
    if(exists)return {"UPDATE content_manager_vendor_owner SET applied_build="+N(build)+",artifact_sha256="+T(hash)+" WHERE creature_entry="+N(r.creatureEntry)};
    std::vector<std::string> sql;
    if(!r.flagsManaged)sql.push_back("UPDATE creature_template SET npcflag="+N(r.originalFlags|128)+" WHERE entry="+N(r.creatureEntry)+" AND npcflag="+N(r.originalFlags));
    sql.insert(sql.end(),{
        "INSERT INTO npc_vendor(entry,slot,item,maxcount,incrtime,ExtendedCost,VerifiedBuild) VALUES ("+N(r.creatureEntry)+",0,"+N(r.itemEntry)+",0,0,"+N(r.costId)+",12340)",
        "INSERT INTO content_manager_vendor_owner(creature_entry,realm_name,package_key,symbol,original_flags,row_json,applied_build,artifact_sha256) VALUES ("
        +N(r.creatureEntry)+","+T(realm)+","+T(r.packageKey)+","+T(r.symbol)+","+N(r.originalFlags)+","+T(Snapshot(r))+","+N(build)+","+T(hash)+")"});return sql;
}
bool ContentVendorServer::Verify(ResolvedVendorRow const& r,std::string const& realm,std::uint32_t build,std::string const& hash,std::string& error)
{
    auto q=WorldDatabase.Query("SELECT CAST(("+Condition(r,realm,true)+") AND EXISTS(SELECT 1 FROM content_manager_vendor_owner WHERE creature_entry="
        +N(r.creatureEntry)+" AND applied_build="+N(build)+" AND artifact_sha256="+T(hash)+") AS UNSIGNED)");
    if(!q||!q->Fetch()[0].Get<std::uint64_t>()){error="Vendor post-apply provenance mismatch";return false;}return true;
}
