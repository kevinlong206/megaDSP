// PreSonus Plug-In Extensions
// Written and placed in the PUBLIC DOMAIN by PreSonus Software Ltd.
#pragma once

#include <pluginterfaces/base/funknown.h>

namespace Presonus
{
struct IGainReductionInfo : Steinberg::FUnknown
{
    virtual double PLUGIN_API getGainReductionValueInDb() = 0;

    static const Steinberg::FUID iid;
};

DECLARE_CLASS_IID(
    IGainReductionInfo,
    0x8e3c292c, 0x95924f9d, 0xb2590b1e, 0x100e4198)
} // namespace Presonus
