// Compiled only for CI gameplay checks; excluded from every published build.
#include "Lawn/Cutscene.h"
#include "Lawn/SeedPacket.h"
#include "Lawn/Widget/AugmentEncyclopediaDialog.h"

extern "C" {
EMSCRIPTEN_KEEPALIVE void pvz_web_test_setup(int scene) {
    if (!gLawnApp->mPlayerInfo) gLawnApp->mPlayerInfo=gLawnApp->mProfileMgr->AddProfile("Regression");
    if (gLawnApp->mTitleScreen) gLawnApp->LoadingCompleted();
    if (scene==0) { gLawnApp->DoAugmentEncyclopediaDialog(); return; }
    gLawnApp->KillGameSelector();
    gLawnApp->PreNewGame(GameMode::GAMEMODE_ADVENTURE,false);
    auto* board=gLawnApp->mBoard;
    board->mCutScene->CancelIntro();
    if(gLawnApp->mGameScene!=GameScenes::SCENE_PLAYING)gLawnApp->StartPlaying();
    board->EndRollTutorial(); board->mRollTutorialChecked=true;
    board->mSunMoney=100; board->mZombieCountDown=100000;
    if(scene==2) {
        board->mCurrentWave=2; board->mZombieCountDown=1;
        board->UpdateZombieSpawning();
    }
}
EMSCRIPTEN_KEEPALIVE void pvz_web_test_state() {
    auto* board=gLawnApp->mBoard;
    EM_ASM({Module.testState={updates:$0};},gLawnApp->mUpdateCount);
    auto* catalog=static_cast<AugmentEncyclopediaDialog*>(gLawnApp->GetDialog(Dialogs::DIALOG_AUGMENT_ENCYCLOPEDIA));
    if(catalog) EM_ASM({Module.testState.selected=$0;},catalog->mSelectedAugment);
    if(!board)return;
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
    EM_ASM({Module.testState.packet={x:$0,y:$1};},board->mSeedBank->mX+packet.mX+packet.mOffsetX+packet.mWidth/2,
        board->mSeedBank->mY+packet.mY+packet.mHeight/2);
}
}
