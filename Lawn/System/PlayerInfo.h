#ifndef __PLAYERINFO_H__
#define __PLAYERINFO_H__

#define MAX_POTTED_PLANTS 200
#define PURCHASE_COUNT_OFFSET 1000

#include "../../ConstEnums.h"

class PottedPlant
{
public:
    enum FacingDirection
    {
        FACING_RIGHT,
        FACING_LEFT
    };

public:
    SeedType            mSeedType;                  
    GardenType          mWhichZenGarden;            
    int                 mX;                         
    int                 mY;                         
    FacingDirection     mFacing;                    

    __time64_t          mLastWateredTime;           
    DrawVariation       mDrawVariation;             
    PottedPlantAge      mPlantAge;                  
    int                 mTimesFed;                  
    int                 mFeedingsPerGrow;           
    PottedPlantNeed     mPlantNeed;                 

    __time64_t          mLastNeedFulfilledTime;     
    __time64_t          mLastFertilizedTime;        
    __time64_t          mLastChocolateTime;         
    __time64_t          mFutureAttribute[1];        

public:
    void                InitializePottedPlant(SeedType theSeedType);
};

class DataSync;
class PlayerInfo
{
public:
    SexyString          mName;                              
    ulong               mUseSeq;                            
    ulong               mId;                                
    int                 mLevel;                             
    int                 mCoins;                              // Currency is stored directly as dollars.
    int                 mCoinRemainder; // Legacy v15 migration field; always zero in the current format.
    int                 mRollSlotRoundsRemaining; // Number of Roll Shop rounds remaining in the purchased bonus.
    bool                mCurrencyMigrationPending; // Runtime-only flag; not serialized.
    int                 mFinishedAdventure;                 
    int                 mChallengeRecords[100];             
    int                 mPurchases[80];                     
    int                 mPlayTimeActivePlayer;              
    int                 mPlayTimeInactivePlayer;            
    int                 mHasUsedCheatKeys;                  
    int                 mHasWokenStinky;                    
    int                 mDidntPurchasePacketUpgrade;        
    long                mLastStinkyChocolateTime;           
    int                 mStinkyPosX;                        
    int                 mStinkyPosY;                        
    int                 mHasUnlockedMinigames;              
    int                 mHasUnlockedPuzzleMode;             
    int                 mHasNewMiniGame;                    
    int                 mHasNewScaryPotter;                 
    int                 mHasNewIZombie;                     
    int                 mHasNewSurvival;                    
    int                 mHasUnlockedSurvivalMode;           
    int                 mNeedsMessageOnGameSelector;        
    int                 mNeedsMagicTacoReward;              
    int                 mHasSeenStinky;                     
    int                 mHasSeenUpsell;                     
    int                 mPlaceHolderPlayerStats;            
    int                 mNumPottedPlants;                   
    PottedPlant         mPottedPlant[MAX_POTTED_PLANTS];    
    bool                mEarnedAchievements[NUM_ACHIEVEMENTS];
    bool                mShownedAchievements[NUM_ACHIEVEMENTS];
    SexyString          mDiscoveredAugments; // Delimited stable Augment IDs; grows with the Augment registry.
    SexyString          mAugmentDraftKey;
    SexyString          mAugmentDraftData;

public:
    PlayerInfo();

    void                Reset();
    bool                HasDiscoveredAugment(const SexyString& theAugmentId) const;
    bool                MarkAugmentDiscovered(const SexyString& theAugmentId);
    /*inline*/ void     AddCoins(int theAmount);
    int                 GetMoneyDollars() const;
    void                AddMoneyDollars(int theAmount);
    void                SyncSummary(DataSync& theSync);
    void                SyncDetails(DataSync& theSync);
    void                DeleteUserFiles();
    void                LoadDetails();
    void                SaveDetails();
    /*inline*/ void     ResetChallengeRecord(GameMode theGameMode);
};

#endif
