#include "ContentFrameForgeWowUi.h"

#include "ContentBuildPaths.h"
#include "third_party/json/json.hpp"
#include "third_party/miniz/miniz.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <regex>
#include <set>
#include <sstream>

using json = nlohmann::json;

namespace {
std::uint32_t Be32(std::uint8_t const* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | p[3];
}

std::string Join(std::string const& root, std::string const& leaf) {
    return root.empty() ? leaf : root + "/" + leaf;
}

bool Safe(std::string const& path) {
    try { return ContentBuildPaths::Target(path) == path; }
    catch (...) { return false; }
}

std::string Slash(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

// Small self-contained SHA-256 keeps EPF validation available to the standalone
// package parser without introducing an OpenSSL dependency there.
std::string Sha256(std::vector<std::uint8_t> const& input) {
    static constexpr std::uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    auto r = [](std::uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); };
    std::vector<std::uint8_t> data = input;
    auto bits = static_cast<std::uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while ((data.size() % 64) != 56) data.push_back(0);
    for (int i = 7; i >= 0; --i) data.push_back(static_cast<std::uint8_t>(bits >> (i * 8)));
    std::array<std::uint32_t,8> h{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                  0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for (std::size_t off = 0; off < data.size(); off += 64) {
        std::uint32_t w[64]{};
        for (int i=0;i<16;++i) w[i]=Be32(data.data()+off+i*4);
        for (int i=16;i<64;++i) {
            auto s0=r(w[i-15],7)^r(w[i-15],18)^(w[i-15]>>3);
            auto s1=r(w[i-2],17)^r(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for(int i=0;i<64;++i){
            auto s1=r(e,6)^r(e,11)^r(e,25), ch=(e&f)^((~e)&g);
            auto t1=hh+s1+ch+k[i]+w[i];
            auto s0=r(a,2)^r(a,13)^r(a,22), maj=(a&b)^(a&c)^(b&c);
            auto t2=s0+maj; hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
    static char const hex[]="0123456789ABCDEF";
    std::string out; out.reserve(64);
    for(auto v:h) for(int shift=28;shift>=0;shift-=4) out.push_back(hex[(v>>shift)&15]);
    return out;
}

bool JsonBytes(ContentFrameForgeWowUiPackage::Reader const& read,
               std::string const& member, json& out, std::string& error) {
    std::vector<std::uint8_t> bytes;
    if (!read(member, bytes, error)) return false;
    try { out = json::parse(std::string(bytes.begin(), bytes.end())); }
    catch (std::exception const& e) {
        error = "Malformed JSON member '" + member + "': " + e.what(); return false;
    }
    return true;
}
} // namespace

namespace ContentFrameForgeWowUiPackage {
bool ConvertPngToTga(std::vector<std::uint8_t> const& png,
                     std::vector<std::uint8_t>& tga, std::uint32_t& width,
                     std::uint32_t& height, std::string& error) {
    tga.clear(); width = height = 0;
    static std::uint8_t const signature[8] = {137,80,78,71,13,10,26,10};
    if (png.size() < 33 || std::memcmp(png.data(), signature, 8) != 0) {
        error = "Source is not a PNG"; return false;
    }
    std::vector<std::uint8_t> compressed;
    bool ihdr=false, iend=false;
    for (std::size_t at=8; at+12<=png.size();) {
        auto length=Be32(png.data()+at); at+=4;
        if (length > png.size()-at-8) { error="Truncated PNG chunk"; return false; }
        std::string type(reinterpret_cast<char const*>(png.data()+at),4); at+=4;
        auto data=png.data()+at;
        auto crcExpected=Be32(png.data()+at+length);
        auto crc=mz_crc32(MZ_CRC32_INIT,reinterpret_cast<unsigned char const*>(type.data()),4);
        crc=mz_crc32(crc,data,length);
        if (crc != crcExpected) { error="PNG chunk CRC mismatch: "+type; return false; }
        if(type=="IHDR"){
            if(ihdr||length!=13){error="Invalid PNG IHDR";return false;}
            ihdr=true;width=Be32(data);height=Be32(data+4);
            if(!width||!height||width>65535||height>65535||data[8]!=8||data[9]!=6||
               data[10]!=0||data[11]!=0||data[12]!=0){
                error="PNG must be non-interlaced 8-bit RGBA and at most 65535x65535";return false;
            }
        } else if(type=="IDAT") compressed.insert(compressed.end(),data,data+length);
        else if(type=="IEND") { iend=true; break; }
        at += length + 4;
    }
    if(!ihdr||!iend||compressed.empty()){error="PNG is missing IHDR, IDAT, or IEND";return false;}
    std::uint64_t row64=std::uint64_t(width)*4, raw64=(row64+1)*height;
    if(raw64>std::numeric_limits<mz_ulong>::max()||raw64>std::numeric_limits<std::size_t>::max()){
        error="PNG dimensions are too large";return false;
    }
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(raw64));
    mz_ulong rawSize=static_cast<mz_ulong>(raw.size());
    if(mz_uncompress(raw.data(),&rawSize,compressed.data(),static_cast<mz_ulong>(compressed.size()))!=MZ_OK||rawSize!=raw.size()){
        error="Could not decompress PNG image data";return false;
    }
    auto row=static_cast<std::size_t>(row64);
    std::vector<std::uint8_t> rgba(row*height);
    auto paeth=[](int a,int b,int c){int p=a+b-c,pa=std::abs(p-a),pb=std::abs(p-b),pc=std::abs(p-c);return pa<=pb&&pa<=pc?a:(pb<=pc?b:c);};
    for(std::uint32_t y=0;y<height;++y){
        auto filter=raw[std::size_t(y)*(row+1)], *src=raw.data()+std::size_t(y)*(row+1)+1;
        if(filter>4){error="Unsupported PNG row filter";return false;}
        auto* dst=rgba.data()+std::size_t(y)*row;
        for(std::size_t x=0;x<row;++x){
            int left=x>=4?dst[x-4]:0, up=y?rgba[std::size_t(y-1)*row+x]:0;
            int ul=(y&&x>=4)?rgba[std::size_t(y-1)*row+x-4]:0, add=0;
            if(filter==1)add=left; else if(filter==2)add=up; else if(filter==3)add=(left+up)/2; else if(filter==4)add=paeth(left,up,ul);
            dst[x]=static_cast<std::uint8_t>(src[x]+add);
        }
    }
    tga.assign(18+rgba.size(),0); tga[2]=2;
    tga[12]=width&255;tga[13]=(width>>8)&255;tga[14]=height&255;tga[15]=(height>>8)&255;
    tga[16]=32;tga[17]=0x28; // eight alpha bits, top-left origin
    for(std::size_t i=0;i<rgba.size();i+=4){tga[18+i]=rgba[i+2];tga[19+i]=rgba[i+1];tga[20+i]=rgba[i];tga[21+i]=rgba[i+3];}
    return true;
}

bool Validate(std::string const& source, std::string const& layoutTarget,
              Reader const& read, std::vector<ContentPackageEntry>& outputs,
              std::string& error) {
    outputs.clear();
    if(!Safe(source)||!Safe(layoutTarget)){error="Unsafe FrameForge source or layout target";return false;}
    if(Lower(layoutTarget).size()<4||Lower(layoutTarget).substr(Lower(layoutTarget).size()-4)!=".xml"){
        error="FrameForge layoutTarget must be an XML path";return false;
    }
    json frame, assets;
    auto frameMember=Join(source,"frameforge-manifest.json"), assetsMember=Join(source,"assets-manifest.json");
    if(!JsonBytes(read,frameMember,frame,error)||!JsonBytes(read,assetsMember,assets,error))return false;
    if(!frame.is_object()||frame.value("schema",std::string())!="frameforge-wow335-manifest"||
       frame.value("version",0)!=2||!frame.contains("target")||!frame["target"].is_object()||
       frame["target"].value("game",std::string())!="World of Warcraft"||
       frame["target"].value("version",std::string())!="3.3.5a"||frame["target"].value("build",0)!=12340||
       !frame.contains("layout")||!frame["layout"].is_string()){
        error="Unsupported or malformed FrameForge WoW manifest: "+frameMember;return false;
    }
    auto layoutName=frame["layout"].get<std::string>();
    if(!Safe(layoutName)||layoutName.find('/')!=std::string::npos){error="Unsafe FrameForge layout member: "+layoutName;return false;}
    auto layoutMember=Join(source,layoutName); std::vector<std::uint8_t> layout;
    if(!read(layoutMember,layout,error))return false;
    std::string xml(layout.begin(),layout.end());
    if(xml.find("<Ui")==std::string::npos||xml.find("</Ui>")==std::string::npos||xml.find('\0')!=std::string::npos){
        error="Invalid FrameForge layout XML: "+layoutMember;return false;
    }
    if(!assets.is_object()||assets.value("schema",std::string())!="frameforge-wow335-assets"||
       assets.value("version",0)!=2||!assets.contains("assets")||!assets["assets"].is_array()){
        error="Unsupported or malformed FrameForge assets manifest: "+assetsMember;return false;
    }
    outputs.push_back({"frameforge-layout",layoutMember,layoutTarget});
    std::set<std::string> targets, logical;
    targets.insert(ContentBuildPaths::Fold(layoutTarget));
    for(auto const& asset:assets["assets"]){
        if(!asset.is_object()||!asset.contains("source")||!asset["source"].is_string()||
           !asset.contains("contentSha256")||!asset["contentSha256"].is_string()||
           !asset.contains("logicalTexture")||!asset["logicalTexture"].is_string()||
           asset.value("conversion",std::string())!="png-to-wow-compatible-tga-or-blp"){
            error="Malformed project asset in "+assetsMember;return false;
        }
        auto local=asset["source"].get<std::string>(), texture=Slash(asset["logicalTexture"].get<std::string>());
        if(!Safe(local)||Lower(local).size()<4||Lower(local).substr(Lower(local).size()-4)!=".png"){
            error="Unsafe or unsupported FrameForge asset source: "+local;return false;
        }
        if(!Safe(texture)||Lower(texture).rfind("interface/",0)!=0||texture.find('.')!=std::string::npos){
            error="Invalid logical WoW texture path: "+texture;return false;
        }
        auto member=Join(source,local);std::vector<std::uint8_t> bytes;
        if(!read(member,bytes,error)){error="FrameForge package '"+source+"': "+error;return false;}
        auto expected=asset["contentSha256"].get<std::string>();
        if(expected.size()!=64||Lower(Sha256(bytes))!=Lower(expected)){error="FrameForge asset SHA-256 mismatch: "+member;return false;}
        std::vector<std::uint8_t> tga;std::uint32_t w=0,h=0;
        if(!ConvertPngToTga(bytes,tga,w,h,error)){error="FrameForge asset '"+member+"': "+error;return false;}
        auto target=texture+".tga", folded=ContentBuildPaths::Fold(target);
        if(!targets.insert(folded).second){error="Duplicate FrameForge logical output: "+target;return false;}
        logical.insert(Lower(Slash(asset["logicalTexture"].get<std::string>())));
        outputs.push_back({"frameforge-png-tga",member,target});
    }
    std::regex texturePattern("file=\\\"([^\\\"]+)\\\"");
    for(std::sregex_iterator it(xml.begin(),xml.end(),texturePattern),end;it!=end;++it){
        auto ref=Lower(Slash((*it)[1].str()));
        if(ref.rfind("interface/frameforge/",0)==0&&!logical.count(ref)){
            error="FrameForge layout references undeclared project texture: "+(*it)[1].str();return false;
        }
    }
    return true;
}
} // namespace ContentFrameForgeWowUiPackage
