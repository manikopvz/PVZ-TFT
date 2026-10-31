#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/LawnCommon.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/ProfileMgr.h"
#include "Lawn/System/SaveGame.h"
#include <emscripten.h>

extern "C" {
EMSCRIPTEN_KEEPALIVE void pvz_web_save() {
    if (!gLawnApp || !gLawnApp->mLoaded || gLawnApp->mLoadingFailed) return;
    gLawnApp->WriteToRegistry();
    gLawnApp->WriteCurrentUserConfig();
    // The exit-save path pauses music and can complete the end-level sequence.
    // A browser checkpoint must leave the running game and its music unchanged.
    auto board = gLawnApp->mBoard;
    if (board && gLawnApp->mPlayerInfo && board->NeedSaveGame() && board->mBoardFadeOutCounter <= 0) {
        Sexy::MkDir(Sexy::GetAppDataFolder() + "userdata");
        LawnSaveGame(board, GetSavedGameName(gLawnApp->mGameMode, gLawnApp->mPlayerInfo->mId));
    }
    if (gLawnApp->mProfileMgr) gLawnApp->mProfileMgr->Save();
}

EMSCRIPTEN_KEEPALIVE int pvz_web_ready() {
    if (!gLawnApp || gLawnApp->mLoadingFailed) return -1;
    return gLawnApp->mLoaded ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void pvz_web_resume_audio() {
    EM_ASM({
        if (typeof SDL2 !== 'undefined' && SDL2.audioContext && SDL2.audioContext.state === 'suspended') {
            SDL2.audioContext.resume().catch(function(e) { console.warn('Audio resume:', e); });
        }
    });
}
}

