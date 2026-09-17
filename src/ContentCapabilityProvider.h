#ifndef CONTENT_CAPABILITY_PROVIDER_H
#define CONTENT_CAPABILITY_PROVIDER_H
#include "api/ContentCapabilityApiV1.h"
class ContentCapabilityProvider : public ContentCapabilitiesV1::Provider
{
public:
    ContentCapabilitiesV1::Result Resolve(std::string const&,std::string const&,
        std::vector<ContentCapabilitiesV1::Resource>&,ContentCapabilitiesV1::Vendor&,std::string&) const override;
};
#endif
