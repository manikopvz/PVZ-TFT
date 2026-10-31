from pathlib import Path
import shutil,re
def body(text, signature, replacement):
    start=text.index('{',text.index(signature));depth=0
    for m in re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27|[{}]',text[start:]):
        if m[0]=='{':depth+=1
        elif m[0]=='}':
            depth-=1
            if depth==0:return text[:start]+'{\n'+replacement+'\n}'+text[start+m.end():]
    raise RuntimeError(signature)
import argparse
parser=argparse.ArgumentParser(description='Stage original TFT gameplay over the pinned portable platform engine')
parser.add_argument('--engine',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
engine=args.engine.resolve()
out=args.output.resolve()
if out==root or out in root.parents or out==engine or out in engine.parents or engine in out.parents:
    raise ValueError('Output must be a separate generated directory')
out.mkdir(exist_ok=True)
shutil.copytree(engine,out,dirs_exist_ok=True,ignore=shutil.ignore_patterns('.git'))
src=out/'src'
shutil.rmtree(src/'Lawn')
shutil.copytree(root/'Lawn',src/'Lawn')
for name in ['LawnApp.cpp','LawnApp.h','ConstEnums.h','GameConstants.h','Resources.cpp','Resources.h']:
    shutil.copy2(root/name,src/name)
# Use portable music and pool rendering; keep gameplay and save serialization in this repo.
for name in ['Music.cpp','Music.h']:
    shutil.copy2(engine/'src/Lawn/System'/name,src/'Lawn/System'/name)
for name in ['PoolEffect.cpp','PoolEffect.h']:
    shutil.copy2(engine/'src/Lawn/System'/name,src/'Lawn/System'/name)
# Restore legacy include locations as forwarding headers.
fw=src/'SexyAppFramework'
for name in ['ButtonListener.h','CheckboxListener.h','SliderListener.h','EditListener.h','DialogListener.h','ListListener.h','Slider.cpp','Slider.h']:
    shutil.copy2(root/'SexyAppFramework'/name,fw/'widget'/name)
for p in list(fw.rglob('*.h')):
    if p.parent!=fw and 'SDL-Mixer-X' not in str(p) and not (fw/p.name).exists():
        (fw/p.name).write_text('#include "'+p.relative_to(fw).as_posix()+'"\n')
(src/'PakLib').mkdir(exist_ok=True)
(src/'PakLib/PakInterface.h').write_text('#include "../SexyAppFramework/paklib/PakInterface.h"\n')
(fw/'BassMusicInterface.h').write_text('#include "sound/SDLMusicInterface.h"\nusing BassMusicInterface = Sexy::SDLMusicInterface;\n')
(fw/'BassLoader.h').write_text('#pragma once\n')
(fw/'DDImage.h').write_text('#include "graphics/GLImage.h"\nnamespace Sexy { using DDImage = GLImage; }\n')
(fw/'D3DInterface.h').write_text('#include "graphics/GLInterface.h"\nnamespace Sexy { using D3DInterface = GLInterface; }\n')
(fw/'DDInterface.h').write_text('#include "graphics/GLInterface.h"\n')
(fw/'SysFont.h').write_text('#include "graphics/ImageFont.h"\n')
(src/'Lawn/System/PopDRMComm.h').write_text('#pragma once\nclass PopDRMComm { public: void EnableLocking() {} void BuyGame() {} bool DoIPC() { return false; } bool QueryData() { return true; } };\n')
(src/'Lawn/System/PopDRMComm.cpp').write_text('#include "PopDRMComm.h"\n')
# Apply portable source spelling and old string API aliases.
for p in list(src.rglob('*.cpp'))+list(src.rglob('*.h')):
    if 'SDL-Mixer-X' in str(p): continue
    s=p.read_text(errors='replace').lstrip('\ufeff')
    s=re.sub(r'(#\s*include\s*[<"])([^>"\n]+)([>"])',lambda m:m[1]+m[2].replace('\\','/')+m[3],s)
    if p.is_relative_to(src/'Lawn') or p.name in ['LawnApp.cpp','LawnApp.h','Resources.cpp','Resources.h']:
        s=re.sub(r'\bFont\b','_Font',s)
        s=s.replace('SexyAppFramework/_Font.h','SexyAppFramework/Font.h')
        s=s.replace('enum FoleyType;','enum FoleyType : int32_t;')
        s=re.sub(r'(?<=\d)i64\b','LL',s)
        s=re.sub(r'\bulong\b|\bunsigned long(?=\s*\*)|(?<=new )unsigned long(?=\s*\[)','uint32_t',s)
        s=s.replace('mTrackCount','mTracks.count').replace('mTransformCount','mTransforms.count')
        s=s.replace('mTracks[','mTracks.tracks[').replace('mTransforms[','mTransforms.mTransforms[')
        s=re.sub(r'->mTracks->',r'->mTracks.tracks->',s)
        s=re.sub(r'\bint(?=\s+(?:Sexy::)?SOUND_)','intptr_t',s)
        s=s.replace('../../ImageLib/zlib/zlib.h','zlib.h')
        s=s.replace('HMUSIC','Mix_Music*').replace('GetBassMusicHandle','GetMusicHandle')
        s=re.sub(r'new SysFont\([^;\n]+\)', 'Sexy::FONT_CONTINUUMBOLD14->Duplicate()',s)
        s=s.replace('../../SDL/include/SDL3/SDL.h','SDL.h')
        aliases={'SDL_Gamepad':'SDL_GameController','SDL_GamepadButton':'SDL_GameControllerButton','SDL_GamepadAxis':'SDL_GameControllerAxis','SDL_GamepadType':'SDL_GameControllerType','SDL_GAMEPAD_BUTTON_COUNT':'SDL_CONTROLLER_BUTTON_MAX','SDL_GAMEPAD_AXIS_LEFT_TRIGGER':'SDL_CONTROLLER_AXIS_TRIGGERLEFT','SDL_GAMEPAD_AXIS_RIGHT_TRIGGER':'SDL_CONTROLLER_AXIS_TRIGGERRIGHT','SDL_GAMEPAD_TYPE_UNKNOWN':'SDL_CONTROLLER_TYPE_UNKNOWN'}
        for a,b in aliases.items():s=s.replace(a,b)
        s=s.replace('SDL_GAMEPAD_BUTTON_','SDL_CONTROLLER_BUTTON_').replace('SDL_GAMEPAD_AXIS_','SDL_CONTROLLER_AXIS_').replace('SDL_GAMEPAD_TYPE_','SDL_CONTROLLER_TYPE_')
        for a,b in {'SOUTH':'A','EAST':'B','WEST':'X','NORTH':'Y','LEFT_STICK':'LEFTSTICK','RIGHT_STICK':'RIGHTSTICK','LEFT_SHOULDER':'LEFTSHOULDER','RIGHT_SHOULDER':'RIGHTSHOULDER','DPAD_UP':'DPAD_UP','START':'START'}.items():s=s.replace('SDL_CONTROLLER_BUTTON_'+a,'SDL_CONTROLLER_BUTTON_'+b)
        if p.name=='LawnApp.cpp':
            s=re.sub(r'mBigArrowCursor = LoadCursor\([^;]+;', 'mBigArrowCursor = nullptr;',s)
            s=body(s,'void LawnApp::EnforceCursor()', 'SexyAppBase::EnforceCursor();')
            s=body(s,'void LawnApp::StartDiscord()', '// Discord desktop IPC is unavailable in a browser.')
            s=s.replace('SendMessage(mHWnd, WM_CLOSE, NULL, NULL);','CloseRequestAsync();')
            s=s.replace('mDeferredMessages.clear();','')
            s=s.replace('#ifdef _DEBUG\n\tint aDuration = aTimer.GetDuration();', 'int aDuration = aTimer.GetDuration();\n#ifdef _DEBUG')
            s=s.replace('DefinitionLoadResourcePackImages();','')
            s=re.sub(r'Discord_(?:Shutdown|ClearPresence|UpdatePresence|RunCallbacks)\([^;]*\);','',s)
        if p.name=='SeedPacket.cpp':s='#include "../SexyAppFramework/WidgetManager.h"\n'+s
        if p.name=='Music.cpp':s=s.replace('mApp->mPlayerInfo->GetLevel()','mApp->mPlayerInfo->mLevel')
        if p.name=='CreditScreen.cpp':s=body(s,'void TodsHackyUnprotectedPerfTimer::SetStartTime(int theTimeMillisecondsAgo)', 'Sexy::PerfTimer::SetStartTime(theTimeMillisecondsAgo);')
        if p.name=='DataSync.h':s=s.replace('SyncLong(int& theNum);','SyncLong(int& theNum);\n void SyncLong(unsigned int& v) { SyncBytes(&v, sizeof(v)); }')
        if p.name=='SaveGame.cpp':s=s.replace('sizeof(DataArray<T>::DataArrayItem)','sizeof(typename DataArray<T>::DataArrayItem)')
    p.write_text(s)
compat='''#ifndef PVZ_TFT_WEB_COMPAT
#define PVZ_TFT_WEB_COMPAT
#include <SDL.h>
#include <emscripten.h>
#include <list>
#include <set>
#include <cmath>
#include <cfloat>
#include <ctime>
#include <cstdint>
#include <cstring>
#include <algorithm>
using SexyString = std::string;
using SexyChar = char;
using std::min; using std::max;
using DWORD = uint32_t; using BYTE = uint8_t; using HCURSOR=void*; using HICON=void*;
using __time64_t = int64_t;
using __time32_t = int32_t;
using StringWStringMap = std::map<std::string,std::string>;
inline int _localtime64_s(tm* out, const time_t* in) { return localtime_r(in,out) ? 0 : 1; }
template<class A,class B> requires (!std::is_same_v<A,B>) inline auto max(A a,B b) { using C=std::common_type_t<A,B>; return std::max<C>(a,b); }
template<class A,class B> requires (!std::is_same_v<A,B>) inline auto min(A a,B b) { using C=std::common_type_t<A,B>; return std::min<C>(a,b); }
#define TRUE true
#define FALSE false
#define _S(x) x
#define SexyStringToStringFast(x) (x)
#define StringToSexyStringFast(x) (x)
#define sexystricmp strcasecmp
#define sexystrcmp strcmp
#define sexystrncmp strncmp
#define sexysscanf sscanf
#define sexyatoi atoi
#define sexystrcpy strcpy
#define stricmp strcasecmp
#define strnicmp strncasecmp
#define _stricmp strcasecmp
#define _finite std::isfinite
#define _time64 time
#define _time32 time
#define sscanf_s sscanf
#define GetTickCount SDL_GetTicks
#define timeGetTime SDL_GetTicks
#define LOWORD(x) ((x) & 0xffff)
#define HIWORD(x) (((x) >> 16) & 0xffff)
#define OutputDebugString(x) SDL_Log("%s", x)
#define OutputDebugStringA(x) SDL_Log("%s", x)
#endif
'''
(fw/'Common.h').write_text((fw/'Common.h').read_text()+'\n'+compat.replace('#pragma once\n',''))
extra='''
public:
 bool mDiscordPresence=false, mBankKeybinds=false, mZeroNineBankFormat=false;
 bool mAutoCollectSuns=false, mAutoCollectCoins=false, mZombieHealthbars=false, mPlantHealthbars=false;
 int mQuickLevel=1, mSpeedModifier=2;
 float mMouseSensitivity=1.0f;
 bool mCrazySeeds=false, mIs3dAccel=true, mCustomCursor=false;
 std::string mLanguagePath="languages", mLanguage="english", mResourcePackPath="resourcepacks", mResourcePack="", mResourcesPath="properties/resources.xml";
 int mLanguageIndex=0, mResourcePackIndex=-1;
 std::map<std::string, std::map<std::string,std::string>> mLanguages;
 bool mBetaValidate=false;
 void ReloadLanguages(); void LoadCurrentLanguage(); void SwitchLanguage();
 void ReloadResourcePacks(); void LoadCurrentResourcePack(); void SwitchResourcePack();
 std::string GetResourcePackString();
'''
p=fw/'SexyAppBase.h';p.write_text(p.read_text().replace('class SexyAppBase : public ButtonListener, public DialogListener\n{','class SexyAppBase : public ButtonListener, public DialogListener\n{'+extra))
rp=fw/'misc/ResourceManager.h'
# Keep the optional pack map visible to the original application; default resources use mImageMap.
s=rp.read_text()
s=re.sub(r'(typedef[^;]+\bResMap;)',r'\1\n std::map<std::string, ResMap> mResourcePackImageMaps;',s,count=1);rp.write_text(s)
base=fw/'SexyAppBase.cpp';base.write_text('#include "../Sexy.TodLib/TodStringFile.h"\nextern bool TodStringListReadFile(const char*);\n'+base.read_text()+'''
void Sexy::SexyAppBase::ReloadLanguages() {
 mLanguages.clear();
 for (auto& entry : std::filesystem::directory_iterator(mLanguagePath)) {
  if (entry.path().extension() != ".lang") continue;
  mStringProperties.clear();
  if (TodStringListReadFile(entry.path().string().c_str())) mLanguages[entry.path().stem().string()] = mStringProperties;
 }
 if (mLanguages.empty()) throw std::runtime_error("No .lang files in packaged languages directory");
}
void Sexy::SexyAppBase::LoadCurrentLanguage() {
 auto it=mLanguages.find(mLanguage);if(it==mLanguages.end())it=mLanguages.begin();
 mLanguage=it->first;mStringProperties=it->second;
}
void Sexy::SexyAppBase::SwitchLanguage() {
 if(mLanguages.empty())return;
 auto it=mLanguages.find(mLanguage);if(it==mLanguages.end()||++it==mLanguages.end())it=mLanguages.begin();
 mLanguage=it->first;LoadCurrentLanguage();
}
void Sexy::SexyAppBase::ReloadResourcePacks() { Popup("The web build uses the bundled default resource pack."); }
void Sexy::SexyAppBase::LoadCurrentResourcePack() { /* Default resources are loaded by the portable engine. */ }
void Sexy::SexyAppBase::SwitchResourcePack() { Popup("The web build uses the bundled default resource pack."); }
std::string Sexy::SexyAppBase::GetResourcePackString() { return "[NO_RESOURCE_PACK]"; }
''')
(src/'Resources.h').write_text('#include <cstdint>\n'+(src/'Resources.h').read_text()+'\nextern bool (*gExtractResourcesByName)(Sexy::ResourceManager*, const char*);\n')
(src/'Resources.cpp').write_text((src/'Resources.cpp').read_text()+'\nbool (*gExtractResourcesByName)(Sexy::ResourceManager*, const char*);\n')
# Compile only gameplay translation units included by the Windows project.
shutil.copy2(root/'web/compat/GamepadManager.cpp',src/'Lawn/System/GamepadManager.cpp')
import xml.etree.ElementTree as ET
project=ET.parse(root/'SexyAppFramework/SexyAppBase.vcxproj')
ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'}
allowed={n.attrib['Include'].replace('\\','/').removeprefix('../').lower() for n in project.findall('.//m:ClCompile[@Include]',ns) if not n.findall('m:ExcludedFromBuild',ns)}
for p in (src/'Lawn').rglob('*.cpp'):
    if p.relative_to(src).as_posix().lower() not in allowed:p.unlink()
# Canonicalise include casing using the staged source inventory.
files={str(p.relative_to(src)).lower():p for p in src.rglob('*') if p.is_file()}
for p in list(src.rglob('*.cpp'))+list(src.rglob('*.h')):
    if 'SDL-Mixer-X' in str(p):continue
    s=p.read_text()
    def inc(m):
        name=m[1]
        candidate=(p.parent/name).resolve()
        try:key=str(candidate.relative_to(src.resolve())).lower()
        except ValueError:key=''
        real=files.get(key)
        if not real:
            real=files.get(name.lstrip('./').lower())
        if not real:
            for prefix in ['SexyAppFramework/','Sexy.TodLib/','PakLib/','Lawn/']:
                if prefix in name:
                    real=files.get(name[name.index(prefix):].lower())
                    if real:break
        if real:
            import os
            return '#include "'+os.path.relpath(real,p.parent)+'"'
        return m[0]
    p.write_text(re.sub(r'#include\s*"([^"]+)"',inc,s))
print(out)

# Keep platform-only desktop options out of the web UI.
p=src/'Lawn/Widget/NewOptionsDialog.cpp'
s=p.read_text()
for name in ['mDiscordCheckbox','mResourcePackButton','mReloadResourcePacksButton']:
    s=s.replace(name+'->SetVisible(true)',name+'->SetVisible(false)')
s='\n'.join(line for line in s.split('\n') if not ('TodDrawString(' in line and any(key in line for key in ['[OPTIONS_DISCORD_PRESENCE]','[OPTIONS_RESOURCE_PACK]'])))
p.write_text(s)

# Preserve the original TFT/QE settings using the engine's file-backed registry.
p=fw/'SexyAppBase.cpp';s=p.read_text()
settings={
 'QE_BankKeybinds':('Boolean','mBankKeybinds'), 'QE_ZeroNineBankFormat':('Boolean','mZeroNineBankFormat'),
 'QE_AutoCollectSuns':('Boolean','mAutoCollectSuns'), 'QE_AutoCollectCoins':('Boolean','mAutoCollectCoins'),
 'QE_ZombieHealthbars':('Boolean','mZombieHealthbars'), 'QE_PlantHealthbars':('Boolean','mPlantHealthbars'),
 'QE_SpeedModifier':('Integer','mSpeedModifier'), 'QE_QuickLevel':('Integer','mQuickLevel'),
 'QE_Language':('String','mLanguage')
}
for method,operation in [('ReadFromRegistry','Read'),('WriteToRegistry','Write')]:
    if operation=='Read':
        marker='regemu::SetRegFile(GetAppDataPath("registry.regemu"));'
        start=s.index(marker,s.index('void SexyAppBase::ReadFromRegistry('))+len(marker)
    else:
        start=s.index('{',s.index('void SexyAppBase::'+method+'('))+1
    calls='\n'.join(f' Registry{operation}{kind}("{key}", {"&" if operation=="Read" else ""}{field});' for key,(kind,field) in settings.items())
    if operation=='Read':calls+='\n mSpeedModifier=std::clamp(mSpeedModifier,2,9); mQuickLevel=std::clamp(mQuickLevel,1,50);'
    s=s[:start]+'\n'+calls+'\n'+s[start:]
p.write_text(s)
shutil.copy2(root/'web/runtime.cpp',src/'web_runtime.cpp')
shutil.copy2(root/'web/shell.html',out/'wasm/shell.html')
p=out/'CMakeLists.txt';p.write_text(p.read_text()+'''
if(NOT EMSCRIPTEN)
 message(FATAL_ERROR "The staged PVZ-TFT target is for Emscripten only")
endif()
target_compile_options(pvz-portable PRIVATE -fexceptions)
target_link_options(pvz-portable PRIVATE
 "SHELL:-fexceptions"
 "SHELL:-sSTACK_SIZE=8388608"
 "SHELL:-sINITIAL_MEMORY=134217728"
 "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets@/"
)
set_target_properties(pvz-portable PROPERTIES OUTPUT_NAME "index")
''')

