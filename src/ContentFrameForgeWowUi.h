#ifndef CONTENT_FRAME_FORGE_WOW_UI_H
#define CONTENT_FRAME_FORGE_WOW_UI_H

#include "ContentPackage.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ContentFrameForgeWowUiPackage {

using Reader = std::function<bool(std::string const&,
                                  std::vector<std::uint8_t>&, std::string&)>;

// Validate one embedded portable export and describe the exact runtime files
// it will produce. No filesystem access or staging occurs here.
bool Validate(std::string const& source, std::string const& layoutTarget,
              Reader const& read, std::vector<ContentPackageEntry>& outputs,
              std::string& error);

// Deterministic 8-bit RGBA PNG -> uncompressed, top-origin 32-bit TGA.
bool ConvertPngToTga(std::vector<std::uint8_t> const& png,
                     std::vector<std::uint8_t>& tga, std::uint32_t& width,
                     std::uint32_t& height, std::string& error);

} // namespace ContentFrameForgeWowUiPackage

#endif
