#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/ProfileMgr.h"
#include <emscripten.h>

extern "C" {
EMSCRIPTEN_KEEPALIVE void pvz_web_save() {
    if (!gLawnApp || !gLawnApp->mLoaded || gLawnApp->mLoadingFailed) return;
    gLawnApp->WriteToRegistry();
    gLawnApp->WriteCurrentUserConfig();
    if (gLawnApp->mBoard) gLawnApp->mBoard->TryToSaveGame();
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
