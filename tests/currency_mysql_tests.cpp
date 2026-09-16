#include "DatabaseEnv.h"
#include "ContentServerDeployment.h"
#include "ContentServerBundle.h"
#include "ContentCurrencyServer.h"
#include "ContentBuildHash.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

static void SQL(std::string const& sql){if(!WorldDatabase.Execute(sql))throw std::runtime_error(WorldDatabase.lastError);}
static std::uint64_t Count(std::string const& table){auto q=WorldDatabase.Query("SELECT COUNT(*) FROM "+table);assert(q);return q->Fetch()[0].Get<std::uint64_t>();}
int main(int argc,char** argv)
{
    assert(argc==3);WorldDatabase.Connect(argv[1]);CharacterDatabase.Connect(argv[1]);
    std::filesystem::path output=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(output);
    std::string hash(64,'a'),error,summary;
    auto text=ContentServerBundle::SqlIdentityText;
    ResolvedServerItem row;row.packageKey="mod-hunts";row.packageVersion="4.0.0";row.symbol="seal";
    row.id=56807;row.displayId=6418;row.client.classID=15;
    row.server.symbol="seal";row.server.name="Huntmaster's Seal";row.server.description="A token issued by the Huntmasters.";
    row.server.stackable=200;row.server.bagFamily=8192;row.currency={"seal-currency",row.id,22,4};
    ItemAllocation item{"Eitrigg","mod-hunts","seal",row.id,"reserved",1,8,hash};
    auto bit=item;bit.symbol="seal-currency";bit.value=4;bit.resourceKind="currency.known-bit";
    std::vector<ItemAllocation> leases={item,bit};
    std::vector<ResolvedCurrencyCategory> categories;
    auto stage=[&](unsigned build,std::vector<ResolvedServerItem> const& rows){
        std::string name="Eitrigg-Content-"+std::to_string(build)+".mpq",mpqHash,bundleHash,parityHash;
        auto save=[&](std::string const& suffix,std::string const& data,std::string& digest){
            auto file=output/(name+suffix);{std::ofstream f(file,std::ios::binary);f<<data;}
            assert(ContentBuildHash::Calculate(file,digest,error));
        };
        save("","SQL test fixture; not a playable MPQ",mpqHash);
        save(".server.json",ContentServerBundle::ServerJson("Eitrigg",rows),bundleHash);
        save(".parity.json",ContentServerBundle::ParityJson("Eitrigg",build,leases,rows,hash,hash,mpqHash,bundleHash,hash,categories.empty()?"":hash,categories),parityHash);
        SQL("INSERT INTO content_manager_build(build_number,realm_name,filename,package_count,file_count,state,sha256) VALUES ("
            +std::to_string(build)+",'Eitrigg',"+text(name)+",1,2,'STAGED',"+text(mpqHash)+")");
        SQL("INSERT INTO content_manager_server_build(build_number,bundle_filename,bundle_sha256,parity_filename,parity_sha256) VALUES ("
            +std::to_string(build)+","+text(name+".server.json")+","+text(bundleHash)+","+text(name+".parity.json")+","+text(parityHash)+")");
    };
    for(auto const& lease:leases)
        SQL("INSERT INTO content_manager_allocation VALUES ('Eitrigg','mod-hunts',"+text(lease.symbol)+","+text(lease.resourceKind)+","
            +std::to_string(lease.value)+",'reserved',1,8,"+text(hash)+",1,1)");
    // Begin with a Phase 3 owned ordinary item; upgrade the same allocation in place.
    auto previous=row;previous.currency={};previous.server.bagFamily=0;
    SQL(ContentServerBundle::InsertSql(previous));
    SQL("INSERT INTO content_manager_item_owner VALUES (56807,'Eitrigg','mod-hunts','seal','item.id',7,"+text(hash)+","+text(ContentServerBundle::RowJson(previous))+")");
    stage(8,{row});
    // An unowned overlay collision fails before any item mutation.
    SQL("INSERT INTO currencytypes_dbc VALUES (56807,56807,22,4)");
    assert(!ContentServerDeployment::Apply(8,"Eitrigg",output,summary,error));
    assert(Count("content_manager_currency_owner")==0);
    SQL("DELETE FROM currencytypes_dbc");
    // Concurrent drift after preflight must roll back the currency insert and item change.
    WorldDatabase.beforeCommit=[&]{SQL("UPDATE item_template SET name='concurrent drift' WHERE entry=56807");};
    assert(!ContentServerDeployment::Apply(8,"Eitrigg",output,summary,error));
    assert(Count("currencytypes_dbc")==0 && Count("content_manager_currency_owner")==0);
    auto state=WorldDatabase.Query("SELECT server_state FROM content_manager_server_build WHERE build_number=8");
    assert(state->Fetch()[0].Get<std::string>()=="STAGED");
    SQL("UPDATE item_template SET name="+text(previous.server.name)+" WHERE entry=56807");
    assert(ContentServerDeployment::Apply(8,"Eitrigg",output,summary,error));
    assert(Count("currencytypes_dbc")==1 && Count("content_manager_currency_owner")==1);
    assert(ContentServerDeployment::Apply(8,"Eitrigg",output,summary,error)); // Idempotent.
    std::set<std::uint32_t> bits,ids;
    assert(ContentCurrencyServer::Occupancy("Eitrigg",leases,bits,ids,error) && bits.empty() && ids.empty());
    // Ownership drift rejects rebuild occupancy and an APPLIED retry.
    SQL("UPDATE currencytypes_dbc SET BitIndex=5 WHERE ID=56807");
    assert(!ContentCurrencyServer::Occupancy("Eitrigg",leases,bits,ids,error));
    assert(!ContentServerDeployment::Apply(8,"Eitrigg",output,summary,error));
    SQL("UPDATE currencytypes_dbc SET BitIndex=4 WHERE ID=56807");
    stage(9,{row});
    assert(ContentServerDeployment::Apply(9,"Eitrigg",output,summary,error));
    // The accepted Phase 4 row changes category by UPDATE only. Triggers make delete/recreate fail the test.
    SQL("CREATE TRIGGER reject_currency_delete BEFORE DELETE ON currencytypes_dbc FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='delete forbidden'");
    SQL("CREATE TRIGGER reject_currency_insert BEFORE INSERT ON currencytypes_dbc FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='insert forbidden'");
    auto category=item;category.symbol="hunts";category.value=5;category.resourceKind="currency-category.id";
    leases.push_back(category);categories.push_back({"mod-hunts","hunts",5,{{"enUS","Hunts"}}});
    SQL("INSERT INTO content_manager_allocation VALUES ('Eitrigg','mod-hunts','hunts','currency-category.id',5,'reserved',10,10,"+text(hash)+",1,1)");
    row.packageVersion="4.1.0";row.currency.categorySymbol="hunts";row.currency.categoryId=5;
    stage(10,{row});
    WorldDatabase.beforeCommit=[&]{SQL("UPDATE currencytypes_dbc SET CategoryID=23 WHERE ID=56807");};
    assert(!ContentServerDeployment::Apply(10,"Eitrigg",output,summary,error));
    auto owner=WorldDatabase.Query("SELECT category_id,applied_build FROM content_manager_currency_owner WHERE entry=56807");
    assert(owner->Fetch()[0].Get<unsigned>()==22 && owner->Fetch()[1].Get<unsigned>()==9);
    SQL("UPDATE currencytypes_dbc SET CategoryID=22 WHERE ID=56807");
    assert(ContentServerDeployment::Apply(10,"Eitrigg",output,summary,error));
    assert(ContentServerDeployment::Apply(10,"Eitrigg",output,summary,error));
    auto upgraded=WorldDatabase.Query("SELECT c.ID,c.ItemID,c.CategoryID,c.BitIndex,o.category_id FROM currencytypes_dbc c JOIN content_manager_currency_owner o ON o.entry=c.ID");
    assert(upgraded->Fetch()[0].Get<unsigned>()==56807 && upgraded->Fetch()[1].Get<unsigned>()==56807
        && upgraded->Fetch()[2].Get<unsigned>()==5 && upgraded->Fetch()[3].Get<unsigned>()==4 && upgraded->Fetch()[4].Get<unsigned>()==5);
    std::set<std::uint32_t> occupiedCategories;
    assert(ContentCurrencyServer::CategoryOccupancy("Eitrigg",leases,occupiedCategories,error) && occupiedCategories.empty());
    // Losing ownership must never grant permission to overwrite the existing row.
    SQL("DELETE FROM content_manager_currency_owner WHERE entry=56807");
    stage(11,{row});assert(!ContentServerDeployment::Apply(11,"Eitrigg",output,summary,error));
    std::cout<<"PASS category upgrade: guarded owned UPDATE, no delete/insert, concurrent category drift rollback, unchanged item/bit IDs, idempotence, unowned protection\n";
    auto q=WorldDatabase.Query("SELECT @@collation_connection,@@collation_database");
    std::cout<<"PASS production Apply: Phase 3 upgrade, unowned collision, transactional drift rollback, idempotence, occupancy exclusion, owned drift, rebuild apply. Collations "
        <<q->Fetch()[0].Get<std::string>()<<" / "<<q->Fetch()[1].Get<std::string>()<<'\n';
}
