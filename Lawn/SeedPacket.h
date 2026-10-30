#ifndef __SEEDPACKET_H__
#define __SEEDPACKET_H__

#include "GameObject.h"
#include "../GameConstants.h"

constexpr const int SLOT_MACHINE_TIME = 400;
constexpr const int CONVEYOR_SPEED = 4;

class HitResult;
class SeedPacket : public GameObject
{
public:
    int                 mRefreshCounter;                            
    int                 mRefreshTime;                               
    int                 mIndex;                                     
    int                 mOffsetX;                                   
    SeedType            mPacketType;                                
    SeedType            mImitaterType;                              
    int                 mSlotMachineCountDown;                      
    SeedType            mSlotMachiningNextSeed;                     
    float               mSlotMachiningPosition;                     
    bool                mActive;                                    
    bool                mRefreshing;                                
    int                 mTimesUsed;                                 
    int                 mChosenPlantIndex;
    bool                mLocked;
    int                 mRollAnim;
    // Roll Shop / Bench ownership. A merged 2-star packet represents two pool cards;
    // a merged 3-star packet represents four, so keep the copy count rather than a bool.
    bool                mIsBenchPacket;
    int                 mStarLevel;
    int                 mPoolCardCount;
    int                 mSunSpent;
    bool                mBenchMoving;
    unsigned int        mBenchMoveStartTick;
    int                 mBenchMoveFromX;
    int                 mBenchMoveFromY;
    int                 mBenchTargetX;
    int                 mBenchTargetY;
    int                 mBenchMoveDurationMs;

public:
    SeedPacket();

    void                Update();
    void                Draw(Graphics* g);
    void                MouseDown(int x, int y, int theClickCount);
    bool                MouseHitTest(int theX, int theY, HitResult* theHitResult);
    void                Deactivate();
    void                Activate();
    /*inline*/ void     SetActivate(bool theActivate);
    void                PickNextSlotMachineSeed();
    void                WasPlanted();
    /*inline*/ void     SlotMachineStart();
    void                FlashIfReady();
    bool                CanPickUp();
    void                SetPacketType(SeedType theSeedType, SeedType theImitaterType = SeedType::SEED_NONE);
    void                SetFromChosenPlant(int theChosenPlantIndex);
    void                EmptySlot();
};

class SeedBank : public GameObject
{
public:
    int				    mNumPackets;                    
    SeedPacket		    mSeedPackets[SEEDBANK_MAX];     
    int				    mCutSceneDarken;                
    int				    mConveyorBeltCounter;           

public:
    SeedBank();

    void			    Draw(Graphics* g);
    bool			    MouseHitTest(int x, int y, HitResult* theHitResult);
    inline void		    Move(int x, int y) { mX = x; mY = y; }
    bool			    ContainsPoint(int theX, int theY);
    void			    AddSeed(SeedType theSeedType, bool thePlaceOnLeft = false);
    void			    RemoveSeed(int theIndex);
    int				    GetNumSeedsOnConveyorBelt();
    /*inline*/ int		CountOfTypeOnConveyorBelt(SeedType theSeedType);
    void			    UpdateConveyorBelt();
    void			    UpdateWidth();
    void			    RefreshAllPackets();
    void                RollAllSlots(bool theAnimate = true, bool theGuaranteeSun = false, bool theForcePeashooterFirstSlot = false);
    int                 CountLockedSlots();
    bool                AreAllSlotsEmpty();
    Rect                GetRollButtonRect();
    void                DrawRollButton(Graphics* g);

};

void				    SeedPacketDrawSeed(Graphics* g, float x, float y, SeedType theSeedType, SeedType theImitaterType, float theOffsetX, float theOffsetY, float theScale);
void				    DrawSeedPacket(Graphics* g, float x, float y, SeedType theSeedType, SeedType theImitaterType, float thePercentDark, int theGrayness, bool theDrawCost, bool theUseCurrentCost, int theRollTier = 0);


#endif