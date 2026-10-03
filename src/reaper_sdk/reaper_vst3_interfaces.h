/*
 * Adapted subset of the REAPER plug-in SDK VST3 interface declarations.
 * Copyright Cockos Incorporated. Distributed under the adjacent LICENSE.
 */
#pragma once

class IReaperUIEmbedInterface : public FUnknown
{
public:
    virtual Steinberg::TPtrInt embed_message(
        int message, Steinberg::TPtrInt parameter2,
        Steinberg::TPtrInt parameter3) = 0;

    static const FUID iid;
};

DECLARE_CLASS_IID(
    IReaperUIEmbedInterface,
    0x049bf9e7, 0xbc74ead0, 0xc4101e86, 0x7f725981)
