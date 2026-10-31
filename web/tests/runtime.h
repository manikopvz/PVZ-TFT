// Compiled only for CI gameplay checks; excluded from every published build.
#include "Lawn/Cutscene.h"
#include "Lawn/SeedPacket.h"
#include "Lawn/Widget/AugmentEncyclopediaDialog.h"
#include "Lawn/Bush.h"
#include "Sexy.TodLib/Reanimator.h"
#include "SexyAppFramework/imagelib/ImageLib.h"

extern "C" {
EMSCRIPTEN_KEEPALIVE void pvz_web_test_setup(int scene) {
    if (!gLawnApp->mPlayerInfo) gLawnApp->mPlayerInfo=gLawnApp->mProfileMgr->AddProfile("Regression");
    if (gLawnApp->mTitleScreen) gLawnApp->LoadingCompleted();
    if (scene==0) { gLawnApp->DoAugmentEncyclopediaDialog(); return; }
    if (scene==3) { gLawnApp->ShowStoreScreen(); return; }
    gLawnApp->KillGameSelector();
    gLawnApp->PreNewGame(GameMode::GAMEMODE_ADVENTURE,false);
    auto* board=gLawnApp->mBoard;
    board->mCutScene->CancelIntro();
    board->EndRollTutorial(); board->mRollTutorialChecked=true;
    board->mTutorialState=TutorialState::TUTORIAL_OFF;
    board->mSunMoney=100; board->mZombieCountDown=100000;
}
EMSCRIPTEN_KEEPALIVE void pvz_web_test_command(int action) {
    auto* board=gLawnApp->mBoard;
    if(action==3) {
        board->SetRollTutorialStep(ROLL_TUT_WAIT_AUGMENT);
        board->mCurrentWave=2; board->mZombieCountDown=1;
        board->UpdateZombieSpawning();
    }
    else if(action==0)board->SetRollTutorialStep(ROLL_TUT_LOCK);
    else if(action==1)board->AddSeedToBench(SeedType::SEED_PEASHOOTER,1,0);
    else if(action==2)board->EndRollTutorial();
    else if(action==4) {
        auto* catalog=static_cast<AugmentEncyclopediaDialog*>(gLawnApp->GetDialog(Dialogs::DIALOG_AUGMENT_ENCYCLOPEDIA));
        catalog->mListRect.mHeight=100;catalog->mMaxScrollOffset=300;
    }
}
EMSCRIPTEN_KEEPALIVE int pvz_web_test_dave_alpha() {
    auto* image=ImageLib::GetImage("reanim/crazydave_body1.jpg");
    auto* mask=ImageLib::GetImage("reanim/CrazyDave_body1_.png",false);
    if(!image || !mask){delete image;delete mask;return -1;}
    int wrong=0;
    if(image->mWidth!=mask->mWidth || image->mHeight!=mask->mHeight)wrong=-1;
    else for(int i=0;i<image->mWidth*image->mHeight;++i)
        if((image->mBits[i]>>24)!=(mask->mBits[i]&255))++wrong;
    delete image;delete mask;return wrong;
}
EMSCRIPTEN_KEEPALIVE void pvz_web_test_state() {
    auto* board=gLawnApp->mBoard;
    EM_ASM({Module.testState={updates:$0};},gLawnApp->mUpdateCount);
    auto* catalog=static_cast<AugmentEncyclopediaDialog*>(gLawnApp->GetDialog(Dialogs::DIALOG_AUGMENT_ENCYCLOPEDIA));
    if(catalog) EM_ASM({Object.assign(Module.testState,{selected:$0,scroll:$1});},catalog->mSelectedAugment,catalog->mScrollOffset);
    if(!board)return;
    int bushes=0;Bush* bush=nullptr;
    while(board->IterateBushes(bush)) {
        auto* reanim=gLawnApp->ReanimationTryToGet(bush->mReanimID);
        if(reanim && reanim->mDefinition && reanim->mDefinition->mTracks.count>0)++bushes;
    }
    EM_ASM({Module.testState.bushes=$0;},bushes);
    EM_ASM({Object.assign(Module.testState,{playing:!!$0,boardX:$1,boardY:$2});},gLawnApp->mGameScene==GameScenes::SCENE_PLAYING,board->mX,board->mY);
    EM_ASM({Object.assign(Module.testState,{choosing:!!$0,timer:$1,active:$2,tutorial:$3,bench:$4,locked:$5,rolls:$6});},
        board->mAugmentChoosing,board->mAugmentTimer,board->mActiveAugment,board->mRollTutorialStep,
        board->mBenchCount,board->mSeedBank->CountLockedSlots(),board->mManualRollCount);
    for(int i=0;i<3;++i) {
        const Rect card=board->GetAugmentCardRect(i);
        const Rect roll=board->GetAugmentRerollButtonRect(i);
        EM_ASM({(Module.testState.cards??=[]).push({x:$0,y:$1,w:$2,h:$3,rollX:$4,rollY:$5});},
            card.mX,card.mY,card.mWidth,card.mHeight,roll.mX+roll.mWidth/2,roll.mY+roll.mHeight/2);
    }
    auto& packet=board->mSeedBank->mSeedPackets[1];
    EM_ASM({Module.testState.packet=({x:$0,y:$1});},board->mSeedBank->mX+packet.mX+packet.mOffsetX+packet.mWidth/2,
        board->mSeedBank->mY+packet.mY+packet.mHeight/2);
    EM_ASM({Module.testState.benchPoint=({x:$0,y:$1});},board->GetBenchSlotX(0)+SEED_PACKET_WIDTH/2,board->GetBenchRestY()+SEED_PACKET_HEIGHT/2);
}
}

