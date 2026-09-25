#include "ContentCapabilityProvider.h"
#include "ContentManager.h"
#include "ContentBuildRegistry.h"
#include "ContentServerDeployment.h"
#include "ContentAllocationRegistry.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"
#include "Realm.h"
#include "ObjectMgr.h"
#include "Log.h"
#include "DBCStores.h"
#include "World.h"
#include <algorithm>
#include <optional>
ContentResourcesV1::Result ContentCapabilityProvider::ResolveResource(std::string const& package,
    std::string const& symbol,std::string const& kind,std::uint32_t& value,std::string& reason) const
{
    using R=ContentResourcesV1::Result;value=0;
    if(!sContentManager.IsEnabled()){reason="Content Manager disabled";return R::Inactive;}
    std::optional<ContentBuildRecord> active;if(!ContentBuildRegistry().GetActiveBuild(active,reason))return R::Invalid;
    if(!active){reason="No native content activation";return R::Inactive;}
    auto realmResult=LoginDatabase.Query("SELECT name FROM realmlist WHERE id = {}",realm.Id.Realm);
    if(!realmResult||realmResult->GetFieldCount()!=1||realmResult->Fetch()[0].IsNull()){reason="Cannot resolve current realm identity";return R::Invalid;}
    auto currentRealm=realmResult->Fetch()[0].Get<std::string>();if(active->realmName!=currentRealm){reason="Active content belongs to another realm";return R::Invalid;}
    ContentServerStatus status;std::vector<ResolvedServerItem> items;std::vector<ResolvedExtendedCost> costs;std::vector<ResolvedVendorRow> vendors;
    std::vector<ResolvedCreatureTemplate> creatures;std::vector<ResolvedGameObjectTemplate> gameObjects;std::vector<ResolvedCreatureSpawn> spawns;
    if(!ContentServerDeployment::Inspect(active->buildNumber,active->realmName,sContentManager.GetOutputDirectory(),status,items,reason,&costs,&vendors,&creatures,&gameObjects,&spawns))return R::Invalid;
    if(status.state!="APPLIED"){reason="Active native content is not server APPLIED";return R::Invalid;}
    bool declared=false;
    if(kind=="item.id")declared=std::any_of(items.begin(),items.end(),[&](auto const& r){return r.packageKey==package&&r.symbol==symbol;});
    else if(kind=="item-extended-cost.id")declared=std::any_of(costs.begin(),costs.end(),[&](auto const& r){return r.packageKey==package&&r.symbol==symbol;});
    else if(kind=="currency.known-bit")declared=std::any_of(items.begin(),items.end(),[&](auto const& r){return r.packageKey==package&&r.currency.symbol==symbol&&r.currency.bitIndex;});
    else if(kind=="currency-category.id")declared=std::any_of(items.begin(),items.end(),[&](auto const& r){return r.packageKey==package&&r.currency.categorySymbol==symbol&&r.currency.categoryId;});
    else if(kind=="creature-template.id")declared=std::any_of(creatures.begin(),creatures.end(),[&](auto const& r){return r.packageKey==package&&r.symbol==symbol;});
    else if(kind=="gameobject-template.id")declared=std::any_of(gameObjects.begin(),gameObjects.end(),[&](auto const& r){return r.packageKey==package&&r.symbol==symbol;});
    else if(kind=="creature-spawn.guid")declared=std::any_of(spawns.begin(),spawns.end(),[&](auto const& r){return r.packageKey==package&&r.symbol==symbol;});
    else {reason="Unsupported resource kind";return R::Invalid;}
    if(!declared){reason="Resource is not declared by the ACTIVE/APPLIED build";return R::Inactive;}
    std::vector<ItemAllocation> leases;if(!ContentAllocationRegistry().Read(active->realmName,leases,reason))return R::Invalid;
    auto lease=std::find_if(leases.begin(),leases.end(),[&](auto const& a){return a.packageKey==package&&a.symbol==symbol&&a.resourceKind==kind;});
    if(lease==leases.end()){reason="Declared resource lacks retained allocation";return R::Invalid;}
    value=lease->value;reason="Validated ACTIVE/APPLIED managed resource";return R::Ready;
}
using namespace ContentCapabilitiesV1;
Result ContentCapabilityProvider::Resolve(std::string const& package,std::string const& vendorSymbol,
    std::vector<Resource>& requests,Vendor& vendor,std::string& reason) const
{
    vendor={};for(auto& r:requests)r.value=0;
    if(!sContentManager.IsEnabled()){reason="Content Manager disabled";return Result::Inactive;}
    std::optional<ContentBuildRecord> active;
    if(!ContentBuildRegistry().GetActiveBuild(active,reason))return Result::Invalid;
    if(!active){reason="No native content activation";return Result::Inactive;}
	
	auto const realmId = realm.Id.Realm;
	auto realmResult = LoginDatabase.Query("SELECT name FROM realmlist WHERE id = {}", realmId);

	if (!realmResult || realmResult->GetFieldCount() != 1 || realmResult->Fetch()[0].IsNull())
	{
	    reason = "Cannot resolve current realm identity from auth realmlist";
	    return Result::Invalid;
	}

	std::string const currentRealmName = realmResult->Fetch()[0].Get<std::string>();

	if (active->realmName != currentRealmName)
	{
	    reason = "Active content belongs to another realm";
	    return Result::Invalid;
	}
	
    ContentServerStatus status;std::vector<ResolvedServerItem> rows;
    std::vector<ResolvedExtendedCost> costs;std::vector<ResolvedVendorRow> vendors;
    if(!ContentServerDeployment::Inspect(active->buildNumber,active->realmName,sContentManager.GetOutputDirectory(),status,rows,reason,&costs,&vendors))
        return Result::Invalid;
    auto v=std::find_if(vendors.begin(),vendors.end(),[&](auto const& r){return r.packageKey==package && r.symbol==vendorSymbol;});
    if(v==vendors.end()){reason="Native vendor capability has not been activated";return Result::Inactive;}
    if(status.state!="APPLIED"){reason="Active native content is not server APPLIED";return Result::Invalid;}
    std::vector<ItemAllocation> leases;
    if(!ContentAllocationRegistry().Read(active->realmName,leases,reason))return Result::Invalid;
    for(auto& request:requests)
    {
        auto a=std::find_if(leases.begin(),leases.end(),[&](auto const& x){return x.packageKey==package && x.symbol==request.symbol && x.resourceKind==request.kind;});
        if(a==leases.end()){reason="Required resource unresolved: "+request.symbol;return Result::Invalid;}
        bool declared=false;
        if(request.kind=="item.id")
            for(auto const& r:rows)if(r.packageKey==package && r.symbol==request.symbol && r.id==a->value)
            {
                auto item=sObjectMgr->GetItemTemplate(r.id);
                auto currency=sCurrencyTypesStore.LookupEntry(r.id);
                declared=item && item->DisplayInfoID==r.displayId && item->Class==r.client.classID
                    && item->SubClass==r.client.subclassID && item->BagFamily==std::uint32_t(r.server.bagFamily)
                    && item->Stackable==r.server.stackable && item->Bonding==r.server.bonding
                    && (!r.currency.itemId || (currency && currency->ItemId==r.id && currency->BitIndex==r.currency.bitIndex));
            }
        if(request.kind=="item-extended-cost.id")
            for(auto const& c:costs)if(c.packageKey==package && c.symbol==request.symbol && c.id==a->value)
            {
                auto loaded=sItemExtendedCostStore.LookupEntry(c.id);auto w=ItemExtendedCostDbc::Words(c);
                declared=loaded && loaded->ID==c.id && loaded->reqhonorpoints==w[1] && loaded->reqarenapoints==w[2]
                    && loaded->reqarenaslot==w[3] && loaded->reqpersonalarenarating==w[14];
                for(unsigned i=0;declared && i<5;++i)declared=loaded->reqitem[i]==w[4+i] && loaded->reqitemcount[i]==w[9+i];
            }
        if(!declared){reason="Required active/loaded resource invalid: "+request.symbol+" (restart after apply)";return Result::Invalid;}
        request.value=a->value;
    }
    auto creature=sObjectMgr->GetCreatureTemplate(v->creatureEntry);
    auto list=sObjectMgr->GetNpcVendorItemList(v->creatureEntry);
    auto merchandise=sObjectMgr->GetItemTemplate(v->itemEntry);
    auto item=list && list->GetItemCount()==1?list->GetItem(0):nullptr;
    if(!creature || creature->npcflag!=(v->originalFlags|128) || !merchandise || !item
        || item->item!=v->itemEntry || item->ExtendedCost!=v->costId || item->maxcount || item->incrtime)
    {reason="Native vendor is not loaded exactly as applied; restart required";return Result::Invalid;}
    vendor={v->creatureEntry,v->itemEntry,v->costId};reason="Validated ACTIVE/APPLIED native content";
    return Result::Ready;
}
