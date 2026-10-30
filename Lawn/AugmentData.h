#ifndef __AUGMENTDATA_H__
#define __AUGMENTDATA_H__

#include "../SexyAppFramework/Common.h"
#include "../ConstEnums.h"
#include "../GameConstants.h"

// Shared source of truth for Augment names, effects and associated plant previews.
struct AugmentDef
{
    SeedType        mSeedType;
    const SexyChar* mName;
    const SexyChar* mEffect;
    const SexyChar* mId; // Stable save identifier; append new Augments without reusing IDs.
    // Variant family is ONLY an eligibility relationship. Each variant has its own ID/effect.
    const SexyChar* mFamily; // nullptr means this ID has no related variants.
    int mVariantLevel;       // 1, 2, 3...; never an owned Augment upgrade.
    unsigned int mStageMask; // 1=Stage 1, 2=Stage 2, 4=Stage 3; zero defaults to both stages.
    bool mAllowCoexist;      // Exception: Fortune's Favor I and II can both be selected.
};

extern const AugmentDef gAugmentDefs[RollAugment::NUM_AUGMENTS];

#endif
