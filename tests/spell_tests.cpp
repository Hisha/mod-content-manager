#include "SpellDbcComposer.h"
#include "ContentResourceAllocator.h"
#include "DbcDescriptor.h"
#include "DbcReader.h"
#include <cassert>
#include <filesystem>
#include <iostream>

int main(int argc,char**argv){
	assert(argc==2);auto d=FindDbcDescriptor(12340,"Spell");assert(d&&d->fields.size()==234);
	auto read=DbcReader::Read(argv[1],*d);assert(read.valid);auto ids=SpellDbcComposer::Inspect(read.document);assert(read.document.recordCount==49839&&read.document.recordSize==936&&*ids.begin()==1&&*ids.rbegin()==80864);
	std::uint32_t donor=0;std::size_t donorRow=0;for(std::size_t i=0;i<read.document.recordCount;++i){auto id=read.document.words[i*234],icon=read.document.words[i*234+133];if(icon&&!donor){donor=id;donorRow=i;}}
	ContentSpellRow decl;decl.symbol="test-aura";decl.copyFrom=donor;decl.profile=SpellDbcComposer::Profile;decl.names={{"enUS","Managed Aura"},{"frFR","Aura geree"}};decl.descriptions={{"enUS","Informational only."}};decl.auraDescriptions={{"enUS","Managed by the server."}};
	auto donorWithRequirements=read.document;donorWithRequirements.words[donorRow*234+68]=2;donorWithRequirements.words[donorRow*234+69]=0x1234;donorWithRequirements.words[donorRow*234+70]=0x5678;
	auto row=SpellDbcComposer::Resolve(donorWithRequirements,decl,"test-package","1",80865);assert(SpellDbcComposer::BehaviorMatches(row));assert(row.words[34]==0&&row.words[35]==0&&row.words[36]==0&&row.words[68]==0xFFFFFFFFu&&row.words[69]==0&&row.words[70]==0&&row.words[71]==6&&row.words[72]==0&&row.words[95]==4&&row.words[98]==0&&row.words[116]==0);
	auto restricted=row;restricted.words[68]=0;assert(!SpellDbcComposer::BehaviorMatches(restricted));
	auto a=SpellDbcComposer::Compose(read.document,{row}),b=SpellDbcComposer::Compose(read.document,{row});assert(a==b);auto parsed=DbcReader::Parse(a,*d);assert(parsed.valid&&parsed.document.recordCount==49840);assert(std::equal(read.document.words.begin(),read.document.words.end(),parsed.document.words.begin()));assert(std::equal(read.document.strings.begin(),read.document.strings.end(),parsed.document.strings.begin()));assert(parsed.document.words[49839*234+68]==0xFFFFFFFFu);assert(SpellDbcComposer::String(parsed.document,49839,136)=="Managed Aura");assert(SpellDbcComposer::String(parsed.document,49839,139)=="Aura geree");
	bool failed=false;try{SpellDbcComposer::Compose(read.document,{row,row});}catch(...){failed=true;}assert(failed);decl.copyFrom=999999;failed=false;try{(void)SpellDbcComposer::Resolve(read.document,decl,"p","1",80865);}catch(...){failed=true;}assert(failed);decl.copyFrom=donor;auto noIcon=read.document;noIcon.words[donorRow*234+133]=0;failed=false;try{(void)SpellDbcComposer::Resolve(noIcon,decl,"p","1",80865);}catch(...){failed=true;}assert(failed);
	auto policy=ContentResourceAllocator::SpellIdPolicy(ids);assert(policy.firstCandidate==80865&&policy.lastCandidate==4194303);std::vector<ResourceAllocationRequest> req={{"p","a","spell.id"}};auto plan=ContentResourceAllocator::Plan("r",policy,req,{},std::set<std::uint32_t>{80865},1,std::string(64,'a'));assert(plan[0].value==80866);auto retained=plan;retained[0].state="retired";auto second=ContentResourceAllocator::Plan("r",policy,{{"p","b","spell.id"}},retained,{80865},2,std::string(64,'a'));assert(second[0].value==80867);
	std::cout<<"managed Spell descriptor/composition/allocation: PASS\n";
}
