#include "ContentExtendedCostServer.h"
#include "ContentServerBundle.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"
#include "third_party/json/json.hpp"
#include <algorithm>
namespace
{
auto T=ContentServerBundle::SqlIdentityText;
std::string N(std::uint32_t value){return std::to_string(value);}
std::string Columns(std::string const& prefix="")
{
    std::string s;
    for(auto const& f:FindDbcDescriptor(12340,"ItemExtendedCost")->fields)
    {if(!s.empty())s+=",";s+=prefix+"`"+f.name+"`";}
    return s;
}
std::string Match(ResolvedExtendedCost const& row)
{
    auto words=ItemExtendedCostDbc::Words(row);std::string s;std::size_t i=0;
    for(auto const& f:FindDbcDescriptor(12340,"ItemExtendedCost")->fields)
    {if(!s.empty())s+=" AND ";s+="c.`"+std::string(f.name)+"`="+N(words[i++]);}
    return s;
}
std::string Identity(ResolvedExtendedCost const& row,std::string const& realm)
{
    return "o.realm_name="+T(realm)+" AND o.package_key="+T(row.packageKey)+" AND o.symbol="+T(row.symbol)
        +" AND o.row_json="+T(ContentExtendedCostServer::RowJson(row));
}
bool OwnerSchema(std::string& error)
{
    auto q=WorldDatabase.Query("SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND "
        "TABLE_NAME IN ('itemextendedcost_dbc','content_manager_extended_cost_owner','content_manager_build_lock',"
        "'content_manager_allocation','content_manager_server_build','content_manager_build','npc_vendor','game_event_npc_vendor') AND ENGINE='InnoDB'");
    if(!q||q->Fetch()[0].Get<std::uint64_t>()!=8)
    {error="Extended-cost deployment/occupancy requires module Phase 5 SQL and InnoDB world tables";return false;}
    return true;
}
}

bool ContentExtendedCostServer::References(ExtendedCostReferences& refs,std::string& error)
{
    refs={};
    auto schema=WorldDatabase.Query("SELECT COLUMN_NAME,COLUMN_TYPE,IS_NULLABLE FROM information_schema.COLUMNS "
        "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='itemextendedcost_dbc' ORDER BY ORDINAL_POSITION");
    auto descriptor=FindDbcDescriptor(12340,"ItemExtendedCost");
    if(!schema||schema->GetRowCount()!=16){error="itemextendedcost_dbc must have the verified 16-column build-12340 schema";return false;}
    std::size_t i=0;
    do
    {
        auto f=schema->Fetch();
        if(f[0].Get<std::string>()!=descriptor->fields[i++].name||f[1].Get<std::string>()!="int"||f[2].Get<std::string>()!="NO")
        {error="itemextendedcost_dbc column layout mismatch";return false;}
    }while(schema->NextRow());
    auto overlay=WorldDatabase.Query("SELECT "+Columns()+" FROM itemextendedcost_dbc UNION ALL SELECT NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL");
    if(!overlay){error="Cannot inspect ItemExtendedCost SQL overlay";return false;}
    do
    {
        auto f=overlay->Fetch();if(f[0].IsNull())continue;
        for(unsigned j=0;j<16;++j)
            if(f[j].Get<std::int32_t>()<0){error="Negative ItemExtendedCost SQL overlay field";return false;}
        auto id=f[0].Get<std::uint32_t>();
        if(!id||!refs.overlay.insert(id).second){error="Duplicate/zero overlay cost identity";return false;}
        for(unsigned j=4;j<9;++j)if(auto item=f[j].Get<std::uint32_t>())refs.items.insert(item);
    }while(overlay->NextRow());
    for(auto source:{std::make_pair("npc_vendor",&refs.vendors),std::make_pair("game_event_npc_vendor",&refs.events)})
    {
        auto type=WorldDatabase.Query("SELECT COLUMN_TYPE FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME="
            +T(source.first)+" AND COLUMN_NAME='ExtendedCost'");
        if(!type||type->Fetch()[0].Get<std::string>()!="int unsigned"){error="Unexpected vendor ExtendedCost schema";return false;}
        auto q=WorldDatabase.Query("SELECT ExtendedCost FROM "+std::string(source.first)+" UNION ALL SELECT 0");
        if(!q){error="Cannot inspect vendor/event extended-cost references";return false;}
        do{if(auto id=q->Fetch()[0].Get<std::uint32_t>())source.second->insert(id);}while(q->NextRow());
    }
    auto type=CharacterDatabase.Query("SELECT COLUMN_TYPE FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() "
        "AND TABLE_NAME='item_refund_instance' AND COLUMN_NAME='paidExtendedCost'");
    if(!type||type->Fetch()[0].Get<std::string>()!="smallint unsigned"){error="Unexpected item_refund_instance.paidExtendedCost schema";return false;}
    auto refunds=CharacterDatabase.Query("SELECT CAST(paidExtendedCost AS UNSIGNED) FROM item_refund_instance UNION ALL SELECT CAST(0 AS UNSIGNED)");
    if(!refunds){error="Cannot inspect character refund extended-cost references";return false;}
    do{if(auto id=static_cast<std::uint32_t>(refunds->Fetch()[0].Get<std::uint64_t>()))refs.refunds.insert(id);}while(refunds->NextRow());
    return true;
}

bool ContentExtendedCostServer::Occupancy(std::string const& realm,std::vector<ItemAllocation> const& leases,
    std::set<std::uint32_t>& ids,std::set<std::uint32_t>& items,std::string& error)
{
    ExtendedCostReferences refs;
    if(!References(refs,error)||!OwnerSchema(error))return false;
    ids=refs.overlay;ids.insert(refs.vendors.begin(),refs.vendors.end());ids.insert(refs.events.begin(),refs.events.end());
    ids.insert(refs.refunds.begin(),refs.refunds.end());items=refs.items;
    auto q=WorldDatabase.Query("SELECT "+Columns("c.")+",o.realm_name,o.package_key,o.symbol,o.row_json FROM "
        "content_manager_extended_cost_owner o LEFT JOIN itemextendedcost_dbc c ON c.ID=o.entry "
        "UNION ALL SELECT NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL");
    if(!q){error="Cannot inspect extended-cost ownership";return false;}
    do
    {
        auto f=q->Fetch();if(f[16].IsNull())continue;
        if(f[0].IsNull()||f[16].Get<std::string>()!=realm){error="Missing/foreign owned extended-cost row";return false;}
        std::array<std::uint32_t,16> words{};
        for(unsigned j=0;j<16;++j)words[j]=f[j].Get<std::uint32_t>();
        auto package=f[17].Get<std::string>(),symbol=f[18].Get<std::string>();
        bool leased=std::any_of(leases.begin(),leases.end(),[&](auto const& a){return a.realm==realm&&a.packageKey==package
            &&a.symbol==symbol&&a.resourceKind=="item-extended-cost.id"&&a.value==words[0];});
        if(!leased||nlohmann::json(words).dump()!=f[19].Get<std::string>())
        {error="Owned extended-cost drift or missing lease";return false;}
        ids.erase(words[0]); // Its retained lease still reserves it; native references are legitimate after deployment.
    }while(q->NextRow());
    return true;
}

std::string ContentExtendedCostServer::RowJson(ResolvedExtendedCost const& row)
{return nlohmann::json(ItemExtendedCostDbc::Words(row)).dump();}
std::string ContentExtendedCostServer::Condition(ResolvedExtendedCost const& row,std::string const& realm,bool exists)
{
    if(exists)return "EXISTS(SELECT 1 FROM itemextendedcost_dbc c JOIN content_manager_extended_cost_owner o ON o.entry=c.ID WHERE "
        +Match(row)+" AND "+Identity(row,realm)+")";
    return "NOT EXISTS(SELECT 1 FROM itemextendedcost_dbc WHERE ID="+N(row.id)+") AND NOT EXISTS(SELECT 1 FROM "
        "content_manager_extended_cost_owner WHERE entry="+N(row.id)+") AND NOT EXISTS(SELECT 1 FROM npc_vendor WHERE ExtendedCost="
        +N(row.id)+") AND NOT EXISTS(SELECT 1 FROM game_event_npc_vendor WHERE ExtendedCost="+N(row.id)+")";
}
bool ContentExtendedCostServer::Check(ResolvedExtendedCost const& row,std::string const& realm,bool& exists,std::string& error)
{
    auto q=WorldDatabase.Query("SELECT CAST(("+Condition(row,realm,false)+") AS UNSIGNED),CAST(("+Condition(row,realm,true)+") AS UNSIGNED)");
    if(!q||(!q->Fetch()[0].Get<std::uint64_t>()&&!q->Fetch()[1].Get<std::uint64_t>()))
    {error="Extended-cost collision, ownership drift, or definition changed after apply; use a new cost symbol";return false;}
    exists=q->Fetch()[1].Get<std::uint64_t>()!=0;return true;
}
std::vector<std::string> ContentExtendedCostServer::ApplySql(ResolvedExtendedCost const& row,std::string const& realm,bool exists,
    std::uint32_t build,std::string const& hash)
{
    if(exists)return {"UPDATE content_manager_extended_cost_owner o JOIN itemextendedcost_dbc c ON c.ID=o.entry "
        "SET o.applied_build="+N(build)+",o.artifact_sha256="+T(hash)+" WHERE "+Match(row)+" AND "+Identity(row,realm)};
    std::string values;
    for(auto value:ItemExtendedCostDbc::Words(row)){if(!values.empty())values+=",";values+=N(value);}
    return {"INSERT INTO itemextendedcost_dbc ("+Columns()+") VALUES ("+values+")",
        "INSERT INTO content_manager_extended_cost_owner(entry,realm_name,package_key,symbol,row_json,applied_build,artifact_sha256) VALUES ("
        +N(row.id)+","+T(realm)+","+T(row.packageKey)+","+T(row.symbol)+","+T(RowJson(row))+","+N(build)+","+T(hash)+")"};
}
bool ContentExtendedCostServer::Verify(ResolvedExtendedCost const& row,std::string const& realm,std::uint32_t build,
    std::string const& hash,std::string& error)
{
    auto q=WorldDatabase.Query("SELECT CAST(("+Condition(row,realm,true)+") AND EXISTS(SELECT 1 FROM content_manager_extended_cost_owner WHERE entry="
        +N(row.id)+" AND applied_build="+N(build)+" AND artifact_sha256="+T(hash)+") AS UNSIGNED)");
    if(!q||!q->Fetch()[0].Get<std::uint64_t>()){error="Extended-cost post-apply provenance mismatch";return false;}
    return true;
}
