#include "GamepadManager.h"
#include "../../LawnApp.h"
#include "../../SexyAppFramework/WidgetManager.h"
#include <cmath>

Gamepad::Gamepad() : mApp(nullptr), mIndex(-1), mSDLGamepad(nullptr), mThreshold(0.4f) {
    memset(mButtons, 0, sizeof mButtons); memset(mLastButtons, 0, sizeof mLastButtons);
}
Gamepad::~Gamepad() { if (mSDLGamepad) SDL_GameControllerClose(mSDLGamepad); }
bool Gamepad::GetButton(SDL_GameControllerButton b) { return mApp->mHasFocus && b >= 0 && b < SDL_CONTROLLER_BUTTON_MAX && mButtons[b]; }
bool Gamepad::GetButtonDown(SDL_GameControllerButton b) { return GetButton(b) && !mLastButtons[b]; }
bool Gamepad::GetButtonUp(SDL_GameControllerButton b) { return mApp->mHasFocus && !GetButton(b) && mLastButtons[b]; }
int Gamepad::GetAxisRawValue(SDL_GameControllerAxis a) { return mApp->mHasFocus ? SDL_GameControllerGetAxis(mSDLGamepad, a) : 0; }
float Gamepad::GetAxisValue(SDL_GameControllerAxis a) {
    float v = GetAxisRawValue(a) / 32767.0f;
    return std::abs(v) < mThreshold ? 0.0f : std::clamp(v, -1.0f, 1.0f);
}
float Gamepad::GetTriggerAxisValue(SDL_GameControllerAxis a) { return std::clamp(GetAxisRawValue(a) / 32767.0f, 0.0f, 1.0f); }
void Gamepad::Rumble(float lo, float hi, int ms) { SDL_GameControllerRumble(mSDLGamepad, std::clamp(lo,0.0f,1.0f)*65535, std::clamp(hi,0.0f,1.0f)*65535, ms); }

GamepadManager::GamepadManager(LawnApp* app) : mApp(app), mInit(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)==0), mCurrentMouse(-1), mLastUsedType(SDL_CONTROLLER_TYPE_UNKNOWN) { }
GamepadManager::~GamepadManager() {
    for (auto& [id, pad] : mGamepads) delete pad;
    if (mInit) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}
void GamepadManager::Update() {
    if (!mInit) return;
    // Do not poll SDL events here: the engine owns keyboard, mouse and focus events.
    for (auto it=mGamepads.begin(); it!=mGamepads.end();) {
        if (!SDL_GameControllerGetAttached(it->second->mSDLGamepad)) {
            if (mCurrentMouse==it->second->mIndex) mCurrentMouse=-1;
            delete it->second; it=mGamepads.erase(it);
        } else ++it;
    }
    for (int device=0; device<SDL_NumJoysticks(); ++device) {
        auto id=SDL_JoystickGetDeviceInstanceID(device);
        if (!SDL_IsGameController(device) || mGamepads.count(id)) continue;
        int index=0;
        for (; index<4; ++index) {
            bool used=false; for (auto& [key,pad] : mGamepads) if(pad->mIndex==index) used=true;
            if (!used) break;
        }
        if (index==4) break;
        auto handle=SDL_GameControllerOpen(device); if (!handle) continue;
        auto pad=new Gamepad; pad->mApp=mApp; pad->mIndex=index; pad->mSDLGamepad=handle;
        mGamepads[id]=pad;
    }
    SDL_GameControllerUpdate();
    auto wm=mApp->mWidgetManager;
    for (auto& [id,pad] : mGamepads) {
        memcpy(pad->mLastButtons,pad->mButtons,sizeof pad->mButtons);
        for(int b=0; b<SDL_CONTROLLER_BUTTON_MAX; ++b) {
            pad->mButtons[b]=mApp->mHasFocus && SDL_GameControllerGetButton(pad->mSDLGamepad,(SDL_GameControllerButton)b);
            if(pad->mButtons[b]) mLastUsedType=SDL_GameControllerGetType(pad->mSDLGamepad);
        }
        if(pad->GetButtonDown(SDL_CONTROLLER_BUTTON_LEFTSTICK)) mCurrentMouse=mCurrentMouse==pad->mIndex ? -1 : pad->mIndex;
        if(mCurrentMouse!=pad->mIndex || !mApp->mHasFocus)continue;
        float dx=pad->GetAxisValue(SDL_CONTROLLER_AXIS_LEFTX),dy=pad->GetAxisValue(SDL_CONTROLLER_AXIS_LEFTY);
        if(pad->GetButton(SDL_CONTROLLER_BUTTON_DPAD_LEFT))dx=-1;
        if(pad->GetButton(SDL_CONTROLLER_BUTTON_DPAD_RIGHT))dx=1;
        if(pad->GetButton(SDL_CONTROLLER_BUTTON_DPAD_UP))dy=-1;
        if(pad->GetButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN))dy=1;
        wm->MouseMove(std::clamp(wm->mLastMouseX+(int)(dx*7),0,mApp->mWidth-1),std::clamp(wm->mLastMouseY+(int)(dy*7),0,mApp->mHeight-1));
        for (auto [button,click] : {std::pair{SDL_CONTROLLER_BUTTON_A,1},std::pair{SDL_CONTROLLER_BUTTON_B,-1}}) {
            if(pad->GetButtonDown(button))wm->MouseDown(wm->mLastMouseX,wm->mLastMouseY,click);
            if(pad->GetButtonUp(button))wm->MouseUp(wm->mLastMouseX,wm->mLastMouseY,click);
        }
    }
}
Gamepad* GamepadManager::GetGamepad(int index) { for(auto& [id,pad] : mGamepads)if(pad->mIndex==index)return pad; return nullptr; }
SDL_GameControllerType GamepadManager::GetLastUsedType() { return mLastUsedType; }
void GamepadManager::RumbleAll(float lo,float hi,int ms) { for(auto& [id,pad] : mGamepads)pad->Rumble(lo,hi,ms); }
