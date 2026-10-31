#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/LawnCommon.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/ProfileMgr.h"
#include "Lawn/System/SaveGame.h"
#include "Lawn/System/Music.h"
#include "SexyAppFramework/widget/WidgetManager.h"
#include "SexyAppFramework/widget/EditWidget.h"
#include "SexyAppFramework/sound/SDLMusicInterface.h"
#include <emscripten.h>

static unsigned int musicSamples = 0;
static Sexy::EditWidget* focusedEdit() {
    if (!gLawnApp || !gLawnApp->mWidgetManager) return nullptr;
    return dynamic_cast<Sexy::EditWidget*>(gLawnApp->mWidgetManager->mFocusWidget);
}

extern "C" {
// Called only for the tracker's decoded music; SFX cannot satisfy this probe.
void pvz_web_music_pcm(const void* data, int bytes) {
    const auto* samples = static_cast<const int16_t*>(data);
    for (int i=0; i<bytes/2; ++i) if (samples[i] != 0) ++musicSamples;
}
EMSCRIPTEN_KEEPALIVE unsigned int pvz_web_music_samples() { return musicSamples; }
EMSCRIPTEN_KEEPALIVE int pvz_web_music_playing() {
    if (!gLawnApp || !gLawnApp->mMusic || gLawnApp->mMusic->mMusicDisabled) return -1;
    auto* music = static_cast<Sexy::SDLMusicInterface*>(gLawnApp->mMusicInterface);
    int playing=0;
    for (const auto& [id, info] : music->mMusicMap)
        if (Mix_PlayingMusicStream(info.mHMusic) && !Mix_PausedMusicStream(info.mHMusic)) ++playing;
    return playing;
}

EMSCRIPTEN_KEEPALIVE void pvz_web_keyboard() {
    auto* edit=focusedEdit();
    if (!edit) return;
    EM_ASM({
        var input=document.getElementById('pvz-soft-keyboard');
        input.value=UTF8ToString($0);
        input.focus({preventScroll:true});
        input.setSelectionRange(Math.min($1,$2),Math.max($1,$2));
    }, edit->mString.c_str(), edit->mCursorPos, edit->mHilitePos<0?edit->mCursorPos:edit->mHilitePos);
}
EMSCRIPTEN_KEEPALIVE int pvz_web_text_active() { return focusedEdit() != nullptr; }
EMSCRIPTEN_KEEPALIVE void pvz_web_set_text(const char* value, int start, int end) {
    auto* edit=focusedEdit();
    if (!edit) return;
    // Reuse the game's validation and length limit, including paste and IME edits.
    edit->mHilitePos=0; edit->mCursorPos=static_cast<int>(edit->mString.size());
    edit->KeyDown(Sexy::KEYCODE_BACK);
    for (const unsigned char* c=reinterpret_cast<const unsigned char*>(value); *c; ++c)
        if (*c>=32 && *c<127) edit->KeyChar(static_cast<char>(*c));
    const int length=static_cast<int>(edit->mString.size());
    edit->mCursorPos=std::clamp(end,0,length);
    edit->mHilitePos=start==end?-1:std::clamp(start,0,length);
    edit->MarkDirty();
    EM_ASM({
        var input=document.getElementById('pvz-soft-keyboard');
        input.value=UTF8ToString($0); input.setSelectionRange(Math.min($1,$2),Math.max($1,$2));
    },edit->mString.c_str(),edit->mCursorPos,edit->mHilitePos<0?edit->mCursorPos:edit->mHilitePos);
}
EMSCRIPTEN_KEEPALIVE void pvz_web_text_key(int key) {
    if (auto* edit=focusedEdit()) edit->KeyDown(static_cast<Sexy::KeyCode>(key));
}

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
    if (gLawnApp->mShutdown && !gLawnApp->mInitialized) return -2;
    return gLawnApp->mLoaded ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void pvz_web_resume_audio() {
    EM_ASM({
        var sdl=Module['SDL2'];
        if (sdl && sdl.audioContext && sdl.audioContext.state === 'suspended') {
            sdl.audioContext.resume().catch(function(e) { console.warn('Audio resume:', e); });
        }
    });
}
EMSCRIPTEN_KEEPALIVE int pvz_web_audio_running() {
    return EM_ASM_INT({ var sdl=Module['SDL2']; return sdl && sdl.audioContext && sdl.audioContext.state === 'running' ? 1 : 0; });
}
}


