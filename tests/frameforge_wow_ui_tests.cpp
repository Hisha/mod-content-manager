#include "ContentFrameForgeWowUi.h"
#include "ContentPackage.h"
#include "third_party/json/json.hpp"
#include "third_party/miniz/miniz.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {
std::vector<std::uint8_t> Decode(std::string const& input) {
    static std::string const alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<std::uint8_t> out; int value=0, bits=-8;
    for (unsigned char c : input) {
        if (c == '=') break;
        auto at=alphabet.find(c); if(at==std::string::npos) continue;
        value=(value<<6)|static_cast<int>(at); bits+=6;
        if(bits>=0){out.push_back(static_cast<std::uint8_t>(value>>bits));bits-=8;}
    }
    return out;
}

using Members = std::map<std::string,std::vector<std::uint8_t>>;

void Save(std::filesystem::path const& path, json const& manifest,
          Members const& members) {
    mz_zip_archive zip{};
    if(!mz_zip_writer_init_file(&zip,path.string().c_str(),0))
        throw std::runtime_error("zip init failed");
    auto body=manifest.dump();
    if(!mz_zip_writer_add_mem(&zip,"manifest.json",body.data(),body.size(),MZ_BEST_COMPRESSION))
        throw std::runtime_error("zip manifest failed");
    for(auto const& [name,bytes]:members)
        if(!mz_zip_writer_add_mem(&zip,name.c_str(),bytes.data(),bytes.size(),MZ_BEST_COMPRESSION))
            throw std::runtime_error("zip member failed");
    if(!mz_zip_writer_finalize_archive(&zip)) throw std::runtime_error("zip finish failed");
    mz_zip_writer_end(&zip);
}

json PackageManifest(std::string source="WoWUI",
                     std::string target="Interface/FrameXML/TestLayout.xml") {
    return {{"schema",3},{"package","frameforge-test"},{"name","FrameForge test"},
        {"version","1"},{"frameForgeWowUi",json::array({{{"source",source},{"layoutTarget",target}}})},
        {"clientFrameXml",{{"stockTocSource","upstream/FrameXML.toc"},
          {"stockTocSha256","3158bea13225ae51137a389f0f3ab8566e94b6be84196dd2c1fda27024677754"},
          {"loadEntries",json::array({{{"target",target},{"after","Interface/FrameXML/LFDFrame.xml"}}})}}}};
}

json FrameManifest() {
    return {{"schema","frameforge-wow335-manifest"},{"version",2},
        {"target",{{"game","World of Warcraft"},{"version","3.3.5a"},{"build",12340}}},
        {"layout","FrameForgeLayout.xml"}};
}

json AssetManifest() {
    return {{"schema","frameforge-wow335-assets"},{"version",2},
        {"assets",json::array({{{"source","assets/test.png"},
          {"contentSha256","6412597E6F97F94C77C9ECBD6815F9ADBEBD0D8BD7646C025FB0931298491F08"},
          {"logicalTexture","Interface\\FrameForge\\Test\\pixel"},
          {"packagingTarget","textures/pixel.tga"},
          {"conversion","png-to-wow-compatible-tga-or-blp"},
          {"consumers",json::array({"Pixel"})}}})}};
}

Members ValidMembers() {
    auto frame=FrameManifest().dump(), assets=AssetManifest().dump();
    std::string layout="<Ui><Frame name=\"Root\" parent=\"LFDParentFrame\"><Layers><Layer>"
        "<Texture name=\"Pixel\" file=\"Interface\\FrameForge\\Test\\pixel\"/>"
        "<Texture name=\"Stock\" file=\"Interface\\TargetingFrame\\UI-StatusBar\"/>"
        "</Layer></Layers></Frame></Ui>";
    return {{"WoWUI/frameforge-manifest.json",{frame.begin(),frame.end()}},
        {"WoWUI/assets-manifest.json",{assets.begin(),assets.end()}},
        {"WoWUI/FrameForgeLayout.xml",{layout.begin(),layout.end()}},
        {"WoWUI/assets/test.png",Decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAAAANSURBVBhXY+ASkXMAAADlAH0nxX5iAAAAAElFTkSuQmCC")},
        {"upstream/FrameXML.toc",{'x'}}};
}
}

int main() {
    auto path=std::filesystem::temp_directory_path()/"content-frameforge-test.epf";
    auto work=std::filesystem::temp_directory_path()/"content-frameforge-work";
    std::filesystem::remove_all(work);
    auto members=ValidMembers();

    Save(path,PackageManifest(),members);
    auto valid=ContentPackage(path).Validate();
    assert(valid.valid);
    assert(valid.manifest.frameForgeWowUi.size()==1);
    assert(valid.manifest.content.size()==2);
    assert(valid.manifest.content[0].type=="frameforge-layout");
    assert(valid.manifest.content[1].target=="Interface/FrameForge/Test/pixel.tga");
    auto staged=ContentPackage(path).Stage(work);
    assert(staged.success&&staged.stagedFiles.size()==2);
    auto tga=work/"frameforge-test"/"Interface/FrameForge/Test/pixel.tga";
    std::ifstream in(tga,std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
    assert(bytes.size()==22&&bytes[2]==2&&bytes[12]==1&&bytes[14]==1&&
           bytes[16]==32&&bytes[17]==0x28);
    assert(bytes[18]==30&&bytes[19]==20&&bytes[20]==10&&bytes[21]==64);
    auto first=bytes;
    std::vector<std::uint8_t> converted;std::uint32_t width=0,height=0;std::string error;
    assert(ContentFrameForgeWowUiPackage::ConvertPngToTga(
        members.at("WoWUI/assets/test.png"),converted,width,height,error));
    assert(converted==first&&width==1&&height==1);

    // Legacy packages remain valid.
    json legacy={{"schema",3},{"package","legacy"},{"name","Legacy"},{"version","1"},
        {"content",json::array({{{"type","file"},{"source","x"},{"target","Documentation/x"}}})}};
    Save(path,legacy,{{"x",{'x'}}});assert(ContentPackage(path).Validate().valid);

    // Every required package member fails explicitly.
    for(auto const& missing:{"WoWUI/FrameForgeLayout.xml","WoWUI/frameforge-manifest.json",
                             "WoWUI/assets-manifest.json","WoWUI/assets/test.png"}){
        auto copy=members;copy.erase(missing);Save(path,PackageManifest(),copy);
        auto v=ContentPackage(path).Validate();assert(!v.valid&&!v.error.empty());
    }

    auto badMembers=members;
    badMembers["WoWUI/frameforge-manifest.json"]={'{'};
    Save(path,PackageManifest(),badMembers);assert(!ContentPackage(path).Validate().valid);
    auto unsupported=FrameManifest();unsupported["version"]=99;auto text=unsupported.dump();
    badMembers=members;badMembers["WoWUI/frameforge-manifest.json"]={text.begin(),text.end()};
    Save(path,PackageManifest(),badMembers);assert(!ContentPackage(path).Validate().valid);

    auto badAssets=AssetManifest();badAssets["assets"][0]["contentSha256"]=std::string(64,'0');
    text=badAssets.dump();badMembers=members;badMembers["WoWUI/assets-manifest.json"]={text.begin(),text.end()};
    Save(path,PackageManifest(),badMembers);assert(!ContentPackage(path).Validate().valid);
    badAssets=AssetManifest();badAssets["assets"][0]["source"]="../escape.png";
    text=badAssets.dump();badMembers=members;badMembers["WoWUI/assets-manifest.json"]={text.begin(),text.end()};
    Save(path,PackageManifest(),badMembers);assert(!ContentPackage(path).Validate().valid);

    Save(path,PackageManifest("../WoWUI"),members);assert(!ContentPackage(path).Validate().valid);
    Save(path,PackageManifest("C:/WoWUI"),members);assert(!ContentPackage(path).Validate().valid);
    Save(path,PackageManifest("WoWUI","../Layout.xml"),members);assert(!ContentPackage(path).Validate().valid);

    // A generated target colliding with an ordinary file is refused.
    auto collision=PackageManifest();
    collision["content"]=json::array({{{"type","file"},{"source","ordinary"},
        {"target","Interface/FrameXML/TestLayout.xml"}}});
    auto collisionMembers=members;collisionMembers["ordinary"]={'x'};
    Save(path,collision,collisionMembers);assert(!ContentPackage(path).Validate().valid);

    // Stock client texture references never become outputs.
    Save(path,PackageManifest(),members);valid=ContentPackage(path).Validate();
    for(auto const& entry:valid.manifest.content)
        assert(entry.target.find("TargetingFrame")==std::string::npos);

    std::filesystem::remove_all(work);std::filesystem::remove(path);
    return 0;
}

