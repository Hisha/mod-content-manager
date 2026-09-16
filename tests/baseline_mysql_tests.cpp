#include "DatabaseEnv.h"
#include "ContentBaselineRegistry.h"
#include "ContentBuildHash.h"
#include "ContentServerBundle.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
static void SQL(std::string const& sql){if(!WorldDatabase.Execute(sql))throw std::runtime_error(WorldDatabase.lastError);}
static void Save(std::filesystem::path const& p,DbcDocument const& d)
{ auto bytes=DbcReader::Serialize(d);std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<char const*>(bytes.data()),bytes.size());assert(f.good()); }
int main(int argc,char** argv)
{
    assert(argc==3);WorldDatabase.Connect(argv[1]);
    auto dir=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(dir);
    auto descriptor=FindDbcDescriptor(12340,"CurrencyCategory");
    DbcDocument doc{1,19,76,7,{}, {0,'S','t','o','c','k',0}};
    doc.words.resize(19);doc.words[0]=1;doc.words[2]=1;
    auto file=dir/"CurrencyCategory.dbc";Save(file,doc);
    auto initial=ContentBaselineRegistry::Inspect(dir,*descriptor);
    std::string error,status;auto text=ContentServerBundle::SqlIdentityText;
    assert(ContentBaselineRegistry::Status(initial,status,error)&&status.find("UNREGISTERED")!=std::string::npos);
    assert(!ContentBaselineRegistry::Accept(initial,std::string(64,'f'),error));
    // Legacy leases cannot be silently imported under different bytes.
    SQL("INSERT INTO content_manager_allocation VALUES ('test','mod','cat','currency-category.id',5,'reserved',1,1,"+text(std::string(64,'f'))+",1,1)");
    assert(!ContentBaselineRegistry::Accept(initial,"",error));
    SQL("UPDATE content_manager_allocation SET baseline_sha256="+text(initial.hash));
    assert(ContentBaselineRegistry::Accept(initial,initial.hash,error));
    assert(ContentBaselineRegistry::Accept(initial,"",error));
    auto count=WorldDatabase.Query("SELECT COUNT(*) FROM content_manager_baseline_history");assert(count->Fetch()[0].Get<unsigned>()==1);
    doc.words[18]=123;Save(file,doc);auto replacement=ContentBaselineRegistry::Inspect(dir,*descriptor);
    assert(!ContentBaselineRegistry::Accept(replacement,"",error));
    assert(ContentBaselineRegistry::Status(replacement,status,error)&&status.find("MISMATCH")!=std::string::npos);
    std::uint64_t review=0;assert(ContentBaselineRegistry::Review(replacement,"test-admin",review,error));
    assert(!ContentBaselineRegistry::Approve(replacement,initial.hash,review,"test-admin",error));
    // A review is bound to the exact inspected candidate, not whichever bytes appear later.
    doc.words[18]=124;Save(file,doc);auto other=ContentBaselineRegistry::Inspect(dir,*descriptor);
    assert(!ContentBaselineRegistry::Approve(other,"",review,"test-admin",error));
    Save(file,replacement.document);
    assert(ContentBaselineRegistry::Approve(ContentBaselineRegistry::Inspect(dir,*descriptor),"",review,"test-admin",error));
    assert(ContentBaselineRegistry::Accept(replacement,"",error));
    assert(!ContentBaselineRegistry::Accept(initial,"",error));
    std::set<std::string> history;assert(ContentBaselineRegistry::History(replacement,history,error));
    assert(history==std::set<std::string>({initial.hash,replacement.hash}));
    auto lease=WorldDatabase.Query("SELECT baseline_sha256,allocated_value FROM content_manager_allocation");
    assert(lease->Fetch()[0].Get<std::string>()==initial.hash&&lease->Fetch()[1].Get<unsigned>()==5);
    // A stale review cannot roll back a more recently accepted revision.
    assert(!ContentBaselineRegistry::Approve(initial,"",review,"test-admin",error));
    std::uint64_t review2=0,review3=0;
    Save(file,initial.document);assert(ContentBaselineRegistry::Review(initial,"test-admin",review2,error));
    assert(ContentBaselineRegistry::Review(other,"test-admin",review3,error));
    Save(file,other.document);assert(ContentBaselineRegistry::Approve(other,"",review3,"test-admin",error));
    Save(file,initial.document);assert(!ContentBaselineRegistry::Approve(initial,"",review2,"test-admin",error));
    // Truncated/invalid files never reach registration.
    {std::ofstream f(file,std::ios::binary);f<<"WDBC";}
    bool failed=false;try{ContentBaselineRegistry::Inspect(dir,*descriptor);}catch(...){failed=true;}assert(failed);
    auto broken=initial;broken.descriptorVersion=99;assert(!ContentBaselineRegistry::Accept(broken,"",error));
    std::cout<<"PASS baseline registry: read-only inspection without pin, automatic registration, optional pin mismatch, legacy hash import, immutable leases/history, changed bytes/version refusal, reviewed approval, stale/candidate mismatch rejection\n";
}
