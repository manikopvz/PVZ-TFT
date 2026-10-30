#include <time.h>
#include <stdio.h>
#include <map>
#include <vector>
#include <string>
#include "ZenGarden.h"
#include "BoardInclude.h"
#include "../GameConstants.h"
#include "System/Music.h"
#include "System/SaveGame.h"
#include "Widget/LawnDialog.h"
#include "System/PlayerInfo.h"
#include "System/PoolEffect.h"
#include "System/PopDRMComm.h"
#include "System/TypingCheck.h"
#include "Widget/StoreScreen.h"
#include "Widget/AwardScreen.h"
#include "../Sexy.TodLib/Trail.h"
#include "Widget/ChallengeScreen.h"
#include "../Sexy.TodLib/TodDebug.h"
#include "../Sexy.TodLib/TodFoley.h"
#include "Widget/SeedChooserScreen.h"
#include "AugmentData.h"
#include "../Sexy.TodLib/Attachment.h"
#include "../Sexy.TodLib/Reanimator.h"
#include "../SexyAppFramework/Dialog.h"
#include "../SexyAppFramework/MTRand.h"
#include "../Sexy.TodLib/TodParticle.h"
#include "../SexyAppFramework/SysFont.h"
#include "../Sexy.TodLib/EffectSystem.h"
#include "../Sexy.TodLib/TodStringFile.h"
#include "../SexyAppFramework/ImageFont.h"
#include "../SexyAppFramework/BassLoader.h"
#include "../SexyAppFramework/SoundManager.h"
#include "../SexyAppFramework/ButtonWidget.h"
#include "../SexyAppFramework/WidgetManager.h"
#include "../SexyAppFramework/SoundInstance.h"

#define SEXY_PERF_ENABLED
#include "../SexyAppFramework/PerfTimer.h"

//#define SEXY_MEMTRACE
//#include "../SexyAppFramework/memmgr.h"

bool gShownMoreSunTutorial = false;

static SexyString GetActiveAugmentLabel(int theAugment);
// Keep the Augment stone button aligned with the native Menu button.
// Both the hit box and the artwork use this same rectangle.
static Rect GetAugmentGalleryButtonRect(int theMenuButtonX, int theMenuButtonY)
{
    const int aButtonWidth = 117;
    const int aButtonHeight = 46;
    const int aGapFromMenu = 10;
    return Rect(theMenuButtonX - aGapFromMenu - aButtonWidth,
        theMenuButtonY, aButtonWidth, aButtonHeight);
}

// View-only gallery entrance. This is transient UI state, deliberately kept
// outside Board's raw-binary save structure.
// One timeline, played forwards when the gallery opens and backwards when it
// closes. This is transient UI state; it does not alter Board's binary save layout.
static int gAugmentGalleryProgressMs = 0;
static unsigned int gAugmentGalleryLastTick = 0;
static const int AUGMENT_GALLERY_STAGGER_MS = 110;
static const int AUGMENT_GALLERY_SLIDE_MS = 220;
static const int AUGMENT_GALLERY_TOTAL_MS = AUGMENT_GALLERY_SLIDE_MS + 2 * AUGMENT_GALLERY_STAGGER_MS;

static Rect GetAugmentGalleryPreviousRect()
{
    return Rect(BOARD_WIDTH / 2 - 118, BOARD_HEIGHT - 49, 92, 28);
}

static Rect GetAugmentGalleryNextRect()
{
    return Rect(BOARD_WIDTH / 2 + 26, BOARD_HEIGHT - 49, 92, 28);
}

static Rect GetActiveAugmentTextRect(int theAugment, int theMenuButtonX);
static Rect GetActiveAugmentTooltipCardRect(int theAugment, int theMenuButtonX);
static void ClearActiveAugmentTooltipPlant();
static void UpdateActiveAugmentTooltipPlant();
static void ClearGalleryAugmentPlants();
static void PrepareGalleryAugmentPlants(const bool* theOwnedAugments);
static void UpdateGalleryAugmentPlants();

// Advance by real elapsed milliseconds, so changing direction halfway through
// an animation preserves every card's current position and opacity.
static void AdvanceAugmentGalleryAnimation(bool theOpening)
{
    const unsigned int aNow = GetTickCount();
    if (gAugmentGalleryLastTick == 0)
    {
        gAugmentGalleryLastTick = aNow;
        return;
    }
    const unsigned int aElapsed = aNow - gAugmentGalleryLastTick;
    gAugmentGalleryLastTick = aNow;
    if (theOpening)
    {
        gAugmentGalleryProgressMs = min(AUGMENT_GALLERY_TOTAL_MS,
            gAugmentGalleryProgressMs + (int)min(aElapsed, (unsigned int)AUGMENT_GALLERY_TOTAL_MS));
    }
    else if (gAugmentGalleryProgressMs > 0)
    {
        gAugmentGalleryProgressMs = max(0,
            gAugmentGalleryProgressMs - (int)min(aElapsed, (unsigned int)AUGMENT_GALLERY_TOTAL_MS));
        if (gAugmentGalleryProgressMs == 0)
            ClearGalleryAugmentPlants();
    }
}

Board::Board(LawnApp* theApp)
{
	mApp = theApp;
	mApp->mBoard = this;
	mHologramPacket.mApp = theApp;
	mHologramPacket.mBoard = this;
	mHologramPacket.mWidth = SEED_PACKET_WIDTH;
	mHologramPacket.mHeight = SEED_PACKET_HEIGHT;
	mHologramPacket.mIndex = -1;
	mHologramPacket.mOffsetX = 0;

	TodHesitationTrace("preboard");

	mZombies.DataArrayInitialize(10240U, "zombies"); // 10x simultaneous zombie capacity
	mPlants.DataArrayInitialize(1024U, "plants");
	mProjectiles.DataArrayInitialize(1024U, "projectiles");
	mCoins.DataArrayInitialize(1024U, "coins");
	mLawnMowers.DataArrayInitialize(32U, "lawnmowers");
	mGridItems.DataArrayInitialize(512U, "griditems");
	mBushes.DataArrayInitialize(16U, "bushes");
	mGamepadPlayers.DataArrayInitialize(512U, "gamepadplayers");
	TodHesitationTrace("board dataarrays");

	mApp->mEffectSystem->EffectSystemFreeAll();
	mBoardRandSeed = mApp->mAppRandSeed;
	if (mApp->IsSurvivalMode())
	{
		mBoardRandSeed = Rand();
	}
	mCoinBankFadeCount = 0;
	mLevel = 0;
	mCursorObject = new CursorObject();
	mCursorPreview = new CursorPreview();
	mSeedBank = new SeedBank();
	mNumChosenPlants = 0;
    mBenchCount = 0;
    mBenchMergeActive = false;
    mBenchMergeLeftIndex = -1;
    mBenchMergeStartTick = 0;
    memset(mSeedPoolRemaining, 0, sizeof(mSeedPoolRemaining));
    memset(mSeedPoolMaximum, 0, sizeof(mSeedPoolMaximum));
    for (int i = 0; i < ROLL_BENCH_SLOTS; ++i)
    {
        mBenchPackets[i].mApp = theApp;
        mBenchPackets[i].mBoard = this;
        mBenchPackets[i].mIsBenchPacket = true;
        mBenchPackets[i].mIndex = i;
        mBenchPackets[i].mX = GetBenchSlotX(i);
        mBenchPackets[i].mY = GetBenchRestY();
        mBenchPackets[i].mOffsetX = 0;
    }
	memset(mRollBannedSeeds, 0, sizeof(mRollBannedSeeds));
	mFreeRolls = 0;
	mManualRollCount = 0;
	mPaidRollCount = 0;
	mRollTutorialChecked = false;
	mRollTutorialStep = ROLL_TUT_NONE;
	mRollTutorialTimer = 0;
	mActiveAugment = RollAugment::AUGMENT_NONE;
    memset(mAugmentPendingPackets, 0, sizeof(mAugmentPendingPackets));
    mAugmentPendingSilverCoins = 0;
    mAugmentPendingGoldCoins = 0;
    mPendingAugmentSunDrops = 0;
    memset(mAugmentFireTicks, 0, sizeof(mAugmentFireTicks));
    for (int x = 0; x < MAX_GRID_SIZE_X; ++x)
        for (int y = 0; y < MAX_GRID_SIZE_Y; ++y)
            mAugmentFireReanim[x][y] = ReanimationID::REANIMATIONID_NULL;
    memset(mEnabledAugments, 0, sizeof(mEnabledAugments));
    mActiveFlagCount = 0;
    mActiveAugmentHoverStartTick = 0;
    mActiveAugmentTooltipVisible = false;
	mAugmentOffered = false;
    mAugmentStage2Offered = false;
    mAugmentStage = 1;
    memset(mStage1Shown, 0, sizeof(mStage1Shown));
    mSeedDraftChoosing = false;
    mSeedDraftRemainingMs = 0;
    mSeedDraftLastUpdateTick = 0;
    mSeedDraftAutoChoiceIndex = 0;
    mSeedDraftCopiesReserved = false;
    mSeedDraftPendingReward = SeedType::SEED_NONE;
    mPendingMiniCherryBombs = 0;
    mShiftingBenchPendingBonusMerges = 0;
    mShiftingBenchTransformPending = false;
    for (int k = 0; k < 3; ++k) mSeedDraftChoices[k] = SeedType::SEED_NONE;
    memset(mSeedDraftExcluded, 0, sizeof(mSeedDraftExcluded));
    mShovelCooldownUntil = mGloveCooldownUntil = 0;
	mAugmentChoosing = false;
    mAugmentSelectionPending = false;
    mAugmentSelectedIndex = -1;
    mAugmentSelectionStartTick = 0;
	mAugmentChoices[0] = mAugmentChoices[1] = mAugmentChoices[2] = RollAugment::AUGMENT_NONE;
	mAugmentInitialChoices[0] = mAugmentInitialChoices[1] = mAugmentInitialChoices[2] = RollAugment::AUGMENT_NONE;
	mAugmentResetPending = false;
	mAugmentRerollResults[0] = mAugmentRerollResults[1] = mAugmentRerollResults[2] = -1;
	mAugmentHover = -1;
	mAugmentTimer = 0;
	mAugmentCherryUsed = false;
    mBalancedBudgetWavesRemaining = 0;
    mFinalReservesClaimed = false;
    mHologramEnabled = false;
    mHologramPlanting = false;
    mHologramLastPlant = SeedType::SEED_NONE;
    mHologramCooldown = 0;
	mAugmentHidden = false;
	mAugmentRerolled[0] = mAugmentRerolled[1] = mAugmentRerolled[2] = false;
	memset(mAugmentSeen, 0, sizeof(mAugmentSeen));
	mAugmentRestoreFastMode = false;
	mAugmentStartTick = 0;
	mAugmentLastPlantUpdateTick = 0;
	mShopClosedWaves = 0;
	mShopWasClosed = false;
    mGardenGloveEnabled = false;
    mAugmentGalleryOpen = false;
    mAugmentGalleryPage = 0;
    gAugmentGalleryProgressMs = 0;
    gAugmentGalleryLastTick = 0;
    mBenchFullNoticeTicks = 0;
	for (int i = 0; i < NUM_SEEDS_IN_CHOOSER; i++)
	{
		mChosenPlants[i].mSeedType = SeedType::SEED_NONE;
		mChosenPlants[i].mImitaterType = SeedType::SEED_NONE;
	}
	mCutScene = new CutScene();
	mSpecialGraveStoneX = -1;
	mSpecialGraveStoneY = -1;
	for (int i = 0; i < MAX_GRID_SIZE_X; i++)
	{
		for (int j = 0; j < MAX_GRID_SIZE_Y; j++)
		{
			mGridSquareType[i][j] = GridSquareType::GRIDSQUARE_GRASS;
			mGridCelLook[i][j] = Rand(20);
			mGridCelOffset[i][j][0] = Rand(10) - 5;
			mGridCelOffset[i][j][1] = Rand(10) - 5;
		}

		for (int k = 0; k < MAX_GRID_SIZE_Y + 1; k++)
		{
			mGridCelFog[i][k] = 0;
		}
	}
	memset(mBushList, 0, sizeof(mBushList));
	memset(mGamepadPlayerList, 0, sizeof(mGamepadPlayerList));
	mFogOffset = 0.0f;
	mSunCountDown = 0;
	mShakeCounter = 0;
	mShakeAmountX = 0;
	mShakeAmountY = 0;
	mPaused = false;
	mLevelAwardSpawned = false;
	mFlagRaiseCounter = 0;
	mIceTrapCounter = 0;
	mLevelComplete = false;
	mBoardFadeOutCounter = -1;
	mNextSurvivalStageCounter = 0;
	mScoreNextMowerCounter = 0;
	mProgressMeterWidth = 0;
	mPoolSparklyParticleID = ParticleSystemID::PARTICLESYSTEMID_NULL;
	mFogBlownCountDown = 0;
	mFwooshCountDown = 0;
	mTimeStopCounter = 0;
	mCobCannonCursorDelayCounter = 0;
	mCobCannonMouseX = 0;
	mCobCannonMouseY = 0;
	mDroppedFirstCoin = false;
	mBonusLawnMowersRemaining = 0;
	mEnableGraveStones = false;
	mHelpIndex = AdviceType::ADVICE_NONE;
	mEffectCounter = 0;
	mDrawCount = 0;
	mRiseFromGraveCounter = 0;
	mFinalWaveSoundCounter = 0;
	mKilledYeti = false;
	mTriggeredLawnMowers = 0;
	mPlayTimeActiveLevel = 0;
	mPlayTimeInactiveLevel = 0;
	mMaxSunPlants = 0;
	mStartDrawTime = 0;
	mIntervalDrawTime = 0;
	mIntervalDrawCountStart = 0;
	mPreloadTime = 0;
	mGameID = _time32(nullptr);
	mMinFPS = 1000.0f;
	mGravesCleared = 0;
	mPlantsEaten = 0;
	mPlantsShoveled = 0;
	mCoinsCollected = 0;
	mDiamondsCollected = 0;
	mPottedPlantsCollected = 0;
	mChocolateCollected = 0;
	for (int y = 0; y < MAX_GRID_SIZE_Y; y++)
	{
		for (int x = 0; x < 12; x++)
		{
			mFwooshID[y][x] = ReanimationID::REANIMATIONID_NULL;
		}
	}
	mPrevMouseX = -1;
	mPrevMouseY = -1;
	mFinalBossKilled = false;
	mMustacheMode = mApp->mMustacheMode;
	mSuperMowerMode = mApp->mSuperMowerMode;
	mFutureMode = mApp->mFutureMode;
	mPinataMode = mApp->mPinataMode;
	mDanceMode = mApp->mDanceMode;
	mDaisyMode = mApp->mDaisyMode;
	mSukhbirMode = mApp->mSukhbirMode;
	mShowShovel = false;
	mToolTip = new ToolTipWidget();
	mDebugFont = new SysFont("Arial Unicode MS", 10, true, false, false);
	mAdvice = new MessageWidget(mApp);
	mBackground = BackgroundType::BACKGROUND_1_DAY;
	mMainCounter = 0;
	mTutorialState = TutorialState::TUTORIAL_OFF;
	mTutorialTimer = -1;
	mTutorialParticleID = ParticleSystemID::PARTICLESYSTEMID_NULL;
	mChallenge = new Challenge();
	mClip = false;
	mDebugTextMode = DebugTextMode::DEBUG_TEXT_NONE;
	mMenuButton = new GameButton(0);
	mMenuButton->mDrawStoneButton = true;
	mMenuButton->mParentWidget = this;
	int aButtonOffsetX = BOARD_ADDITIONAL_WIDTH * 2;
	mFastButton = new GameButton(2);
	mFastButton->Resize(740 + aButtonOffsetX, 30, IMAGE_FASTBUTTON->mWidth, 46);
	mFastButton->mParentWidget = this;
	mStoreButton = nullptr;
	mIgnoreMouseUp = false;
	mPeashootersUsed = false;
	mMushroomsUsed = false;
	mMushroomsNCoffeeUsed = false;
	mUsedNonMushrooms = false;
	mCatapultsUsed = false;
	mCoinFaded = false;
	mAchievementCoinCount = 0;
	mGargantuarsKilled = 0;
	mRoofPoleOffset = ROOF_POLE_START;
	mRoofTreeOffset = ROOF_TREE_START;

	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN || mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM)
	{
		mMenuButton->mLabel = _S("[MAIN_MENU_BUTTON]");
		mMenuButton->Resize(628 + aButtonOffsetX, -10, 163, 46);

		mStoreButton = new GameButton(1);
		mStoreButton->mButtonImage = IMAGE_ZENSHOPBUTTON;
		mStoreButton->mOverImage = IMAGE_ZENSHOPBUTTON_HIGHLIGHT;
		mStoreButton->mDownImage = IMAGE_ZENSHOPBUTTON_HIGHLIGHT;
		mStoreButton->Resize(678 + aButtonOffsetX, 33, IMAGE_ZENSHOPBUTTON->mWidth, 40);
		mStoreButton->mParentWidget = this;
	}
	else
	{
		mMenuButton->mLabel = _S("[MENU_BUTTON]");
		mMenuButton->Resize(681 + aButtonOffsetX, -10, 117, 46);
		mFastButton->mBtnNoDraw = true;
	}

	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND)
	{
		mStoreButton = new GameButton(1);
		mStoreButton->mDrawStoneButton = true;
		mStoreButton->mBtnNoDraw = true;
		mStoreButton->mDisabled = true;
		mStoreButton->mParentWidget = this;
	}

	if (mApp->mGameMode == GameMode::GAMEMODE_UPSELL)
	{
		mMenuButton->mLabel = _S("[MAIN_MENU_BUTTON]");
		mMenuButton->Resize(628 + aButtonOffsetX, -10, 163, 46);
		mMenuButton->mBtnNoDraw = true;

		mStoreButton = new GameButton(1);
		mStoreButton->mDrawStoneButton = true;
		mStoreButton->mBtnNoDraw = true;
		mStoreButton->mLabel = _S("[GET_FULL_VERSION_BUTTON]");
		mStoreButton->mParentWidget = this;
	}
}

Board::~Board()
{
	ClearAugmentPlants();
    ClearActiveAugmentTooltipPlant();
    ClearGalleryAugmentPlants();
    gAugmentGalleryProgressMs = 0;
    gAugmentGalleryLastTick = 0;
	delete mAdvice;
	delete mCursorObject;
	delete mCursorPreview;
	delete mSeedBank;
	if (mMenuButton)
	{
		delete mMenuButton;
	}
	if (mStoreButton)
	{
		delete mStoreButton;
	}
	if (mFastButton)
	{
		delete mFastButton;
	}
	mZombies.DataArrayDispose();
	mPlants.DataArrayDispose();
	mProjectiles.DataArrayDispose();
	mCoins.DataArrayDispose();
	mLawnMowers.DataArrayDispose();
	mGridItems.DataArrayDispose();
	mBushes.DataArrayDispose();
	mGamepadPlayers.DataArrayDispose();
	if (mToolTip)
	{
		delete mToolTip;
	}
	if (mDebugFont)
	{
		delete mDebugFont;
	}
	delete mCutScene;
	delete mChallenge;
	mApp->UpdateDiscordState();
}

void BoardInitForPlayer()
{
	gShownMoreSunTutorial = false;
}

void Board::DisposeBoard()
{
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
		mApp->mZenGarden->LeaveGarden();
	if (mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM)
		mChallenge->TreeOfWisdomLeave();

	mApp->mSoundSystem->StopFoley(FoleyType::FOLEY_RAIN);
	mApp->mZenGarden->mBoard = nullptr;
	mApp->CrazyDaveDie();
	mApp->mEffectSystem->EffectSystemFreeAll();
}

bool Board::AreEnemyZombiesOnScreen()
{
	Zombie* aZombie = nullptr;
	while (IterateZombies(aZombie))
	{
		if (aZombie->mHasHead && !aZombie->IsDeadOrDying() && !aZombie->mMindControlled)
		{
			return true;
		}
	}
	return false;
}

int Board::CountZombiesOnScreen()
{
	int aCount = 0;
	Zombie* aZombie = nullptr;
	while (IterateZombies(aZombie))
	{
		if (aZombie->mHasHead && !aZombie->IsDeadOrDying() && !aZombie->mMindControlled && aZombie->IsOnBoard())
		{
			aCount++;
		}
	}
	return aCount;
}

int Board::CountUntriggerLawnMowers()
{
	int aCount = 0;
	LawnMower* aLawnMower = nullptr;
	while (IterateLawnMowers(aLawnMower))
	{
		if (aLawnMower->mMowerState != LawnMowerState::MOWER_TRIGGERED && aLawnMower->mMowerState != LawnMowerState::MOWER_SQUISHED)
		{
			aCount++;
		}
	}
	return aCount;
}

void Board::TryToSaveGame()
{
	std::string aFileName = GetSavedGameName(mApp->mGameMode, mApp->mPlayerInfo->mId);

	if (NeedSaveGame())
	{
		if (mBoardFadeOutCounter > 0)
		{
			CompleteEndLevelSequenceForSaving();
			return;
		}

		MkDir(GetAppDataFolder() + "userdata");
		mApp->mMusic->GameMusicPause(true);
		LawnSaveGame(this, aFileName);
		mApp->ClearUpdateBacklog();
		SurvivalSaveScore();
	}
}

bool Board::NeedSaveGame()
{
    // Developer Augment sessions are transient and must not overwrite normal saved runs.
    if (mApp->mDeveloperDebugSession && IsRollSeedBank()) return false;
	return
		!mApp->mPlayingQuickplay &&
		mApp->mGameMode != GameMode::GAMEMODE_CHALLENGE_ICE && 
		mApp->mGameMode != GameMode::GAMEMODE_UPSELL && 
		mApp->mGameMode != GameMode::GAMEMODE_INTRO && 
		mApp->mGameMode != GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN && 
		mApp->mGameMode != GameMode::GAMEMODE_TREE_OF_WISDOM && 
		mApp->mGameScene == GameScenes::SCENE_PLAYING;
}

void Board::SaveGame(const string& theFileName)
{ 
    if (mApp->mDeveloperDebugSession && IsRollSeedBank()) return;
	LawnSaveGame(this, theFileName);
}

void Board::ResetFPSStats()
{
	DWORD aTickCount = GetTickCount();
	mStartDrawTime = aTickCount;
	mIntervalDrawTime = aTickCount;
	mDrawCount = 1;
	mIntervalDrawCountStart = 1;
}

bool Board::LoadGame(const string& theFileName)
{
	if (!LawnLoadGame(this, theFileName))
		return false;

	LoadBackgroundImages();
	mApp->ClearUpdateBacklog();
	ResetFPSStats();
	UpdateLayers();
	if (mApp->mGameScene == GameScenes::SCENE_PLAYING)
		mFastButton->mBtnNoDraw = false;
	for (int i = 0; i < MAX_GRID_SIZE_Y; i++)
	{
		if (!StageHasBushes())
			break;
		Bush* aBush = nullptr;
		while (IterateBushes(aBush))
		{
			if (aBush->mIndex == i)
				mBushList[i] = aBush;
		}
		if (!mBushList[i])
			AddBush(i);
	}
	for (int i = 0; i < MAX_GAMEPADS; i++)
	{
		GamepadPlayer* aGamepadPlayer = nullptr;
		while (IterateGamepadPlayers(aGamepadPlayer))
		{
			if (aGamepadPlayer->mIndex == i)
				mGamepadPlayerList[i] = aGamepadPlayer;
		}
		if (!mGamepadPlayerList[i])
			AddGamepadPlayer(i);
	}
	return true;
}

GridItem* Board::GetGridItemAt(GridItemType theGridItemType, int theGridX, int theGridY)
{
	GridItem* aGridItem = nullptr;
	while (IterateGridItems(aGridItem))
	{
		if (aGridItem->mGridX == theGridX && aGridItem->mGridY == theGridY && aGridItem->mGridItemType == theGridItemType)
		{
			return aGridItem;
		}
	}
	return nullptr;
}

GridItem* Board::GetRake()
{
	GridItem* aGridItem = nullptr;
	while (IterateGridItems(aGridItem))
	{
		if (aGridItem->mGridItemType == GridItemType::GRIDITEM_RAKE)
		{
			return aGridItem;
		}
	}
	return nullptr;
}

GridItem* Board::GetCraterAt(int theGridX, int theGridY)
{
	return GetGridItemAt(GridItemType::GRIDITEM_CRATER, theGridX, theGridY);
}

GridItem* Board::GetGraveStoneAt(int theGridX, int theGridY)
{
	return GetGridItemAt(GridItemType::GRIDITEM_GRAVESTONE, theGridX, theGridY);
}

GridItem* Board::GetLadderAt(int theGridX, int theGridY)
{
	return GetGridItemAt(GridItemType::GRIDITEM_LADDER, theGridX, theGridY);
}

GridItem* Board::GetScaryPotAt(int theGridX, int theGridY)
{
	return GetGridItemAt(GridItemType::GRIDITEM_SCARY_POT, theGridX, theGridY);
}

GridItem* Board::GetSquirrelAt(int theGridX, int theGridY)
{
	return GetGridItemAt(GridItemType::GRIDITEM_SQUIRREL, theGridX, theGridY);
}

GridItem* Board::GetZenToolAt(int theGridX, int theGridY)
{
	return GetGridItemAt(GridItemType::GRIDITEM_ZEN_TOOL, theGridX, theGridY);
}

bool Board::CanAddGraveStoneAt(int theGridX, int theGridY)
{
	if (mGridSquareType[theGridX][theGridY] != GridSquareType::GRIDSQUARE_GRASS && mGridSquareType[theGridX][theGridY] != GridSquareType::GRIDSQUARE_HIGH_GROUND)
	{
		return false;
	}

	GridItem* aGridItem = nullptr;
	while (IterateGridItems(aGridItem))
	{
		if (aGridItem->mGridX == theGridX && aGridItem->mGridY == theGridY)
		{
			if (aGridItem->mGridItemType == GridItemType::GRIDITEM_GRAVESTONE || 
				aGridItem->mGridItemType == GridItemType::GRIDITEM_CRATER || 
				aGridItem->mGridItemType == GridItemType::GRIDITEM_LADDER)
				return false;
		}
	}
	return true;
}

int Board::MakeRenderOrder(RenderLayer theRenderLayer, int theRow, int theLayerOffset)
{
	return theRow * (int)RenderLayer::RENDER_LAYER_ROW_OFFSET + theRenderLayer + theLayerOffset;
}

GridItem* Board::AddALadder(int theGridX, int theGridY)
{
	GridItem* aLadder = mGridItems.DataArrayAlloc();
	aLadder->mGridItemType = GridItemType::GRIDITEM_LADDER;
	aLadder->mRenderOrder = MakeRenderOrder(RenderLayer::RENDER_LAYER_PLANT, theGridY, 800);
	aLadder->mGridX = theGridX;
	aLadder->mGridY = theGridY;
	return aLadder;
}

GridItem* Board::AddACrater(int theGridX, int theGridY)
{
	GridItem* aCrater = mGridItems.DataArrayAlloc();
	aCrater->mGridItemType = GridItemType::GRIDITEM_CRATER;
	aCrater->mRenderOrder = MakeRenderOrder(RenderLayer::RENDER_LAYER_GROUND, theGridY, 1);
	aCrater->mGridX = theGridX;
	aCrater->mGridY = theGridY;
	return aCrater;
}

GridItem* Board::AddAGraveStone(int theGridX, int theGridY)
{
	GridItem* aGraveStone = mGridItems.DataArrayAlloc();
	aGraveStone->mGridItemType = GridItemType::GRIDITEM_GRAVESTONE;
	aGraveStone->mGridItemCounter = -Rand(50);
	aGraveStone->mGridItemHealth = 1800;
	aGraveStone->mRenderOrder = MakeRenderOrder(RenderLayer::RENDER_LAYER_GRAVE_STONE, theGridY, 3);
	aGraveStone->mGridX = theGridX;
	aGraveStone->mGridY = theGridY;
	return aGraveStone;
}

void Board::AddGraveStones(int theGridX, int theCount, MTRand& theLevelRNG)
{
	TOD_ASSERT(theCount <= MAX_GRID_SIZE_Y);

	GridItem* aGridItem = nullptr;
	//bool aAllowGraveStone[MAX_GRID_SIZE_Y] = { false };
	int aGridAllowGraveStonesCount = 0;
	for (int y = 0; y < MAX_GRID_SIZE_Y; y++)
	{
		if (CanAddGraveStoneAt(theGridX, y))
		{
			aGridAllowGraveStonesCount++;
		}
	}
	theCount = min(theCount, aGridAllowGraveStonesCount);

	int i = 0;
	while (i < theCount)
	{
		int aGridY = theLevelRNG.Next((unsigned long)MAX_GRID_SIZE_Y);
		//if (aAllowGraveStone[aGridY])
		//{
		//	aAllowGraveStone[aGridY] = false;
		//	GridItem* aGraveStone = AddAGraveStone(theGridX, aGridY);
		//	++i;
		//}
		if (CanAddGraveStoneAt(theGridX, aGridY))
		{
			GridItem* aGraveStone = AddAGraveStone(theGridX, aGridY);
			++i;
		}
	}
}

int Board::GetNumWavesPerFlag()
{
	return (mApp->IsFirstTimeAdventureMode() && mNumWaves < 10) ? mNumWaves : 10;
}

bool Board::IsFlagWave(int theWaveNumber)
{
	if (mApp->IsFirstTimeAdventureMode() && mLevel == 1)
		return false;

	int aWavesPerFlag = GetNumWavesPerFlag();
	return theWaveNumber % aWavesPerFlag == aWavesPerFlag - 1;
}

void ZombiePickerInitForWave(ZombiePicker* theZombiePicker)
{
	memset(theZombiePicker, 0, sizeof(ZombiePicker));
}

void ZombiePickerInit(ZombiePicker* theZombiePicker)
{
	ZombiePickerInitForWave(theZombiePicker);
	memset(theZombiePicker->mAllWavesZombieTypeCount, 0, sizeof(theZombiePicker->mAllWavesZombieTypeCount));
}

void Board::PutZombieInWave(ZombieType theZombieType, int theWaveNumber, ZombiePicker* theZombiePicker)
{
	TOD_ASSERT(theWaveNumber < MAX_ZOMBIE_WAVES && theZombiePicker->mZombieCount < MAX_ZOMBIES_IN_WAVE);
	mZombiesInWave[theWaveNumber][theZombiePicker->mZombieCount++] = theZombieType;
	if (theZombiePicker->mZombieCount < MAX_ZOMBIES_IN_WAVE)
	{
		mZombiesInWave[theWaveNumber][theZombiePicker->mZombieCount] = ZombieType::ZOMBIE_INVALID;
	}
	theZombiePicker->mZombiePoints -= GetZombieDefinition(theZombieType).mZombieValue;
	theZombiePicker->mZombieTypeCount[theZombieType]++;
	theZombiePicker->mAllWavesZombieTypeCount[theZombieType]++;
}

void Board::PutInMissingZombies(int theWaveNumber, ZombiePicker* theZombiePicker)
{
	for (ZombieType aZombieType = ZombieType::ZOMBIE_NORMAL; aZombieType < ZombieType::NUM_ZOMBIE_TYPES; aZombieType = (ZombieType)((int)aZombieType + 1))
	{
		if (theZombiePicker->mZombieTypeCount[(int)aZombieType] <= 0 && aZombieType != ZombieType::ZOMBIE_YETI && CanZombieSpawnOnLevel(aZombieType, mLevel))
		{
			PutZombieInWave(aZombieType, theWaveNumber, theZombiePicker);
		}
	}
}

void Board::PickZombieWaves()
{
	if (mApp->IsAdventureMode())
	{
		if (mApp->IsWhackAZombieLevel())
		{
			mNumWaves = 8;
		}
		else
		{
			mNumWaves = gZombieWaves[ClampInt(mLevel - 1, 0, NUM_LEVELS - 1)];
			if (!mApp->IsFirstTimeAdventureMode() && !mApp->IsMiniBossLevel())
			{
				mNumWaves = mNumWaves < 10 ? 20 : mNumWaves + 10;
			}
		}
	}
	else
	{
		GameMode aGameMode = mApp->mGameMode;
		if (mApp->IsSurvivalMode() || aGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND)
			mNumWaves = GetNumWavesPerSurvivalStage();
		else if (aGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN || aGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM || mApp->IsSquirrelLevel())
			mNumWaves = 0;
		else if (aGameMode == GameMode::GAMEMODE_CHALLENGE_WHACK_A_ZOMBIE)
			mNumWaves = 12;
		else if (aGameMode == GameMode::GAMEMODE_CHALLENGE_WALLNUT_BOWLING || aGameMode == GameMode::GAMEMODE_CHALLENGE_AIR_RAID ||
				 aGameMode == GameMode::GAMEMODE_CHALLENGE_GRAVE_DANGER || aGameMode == GameMode::GAMEMODE_CHALLENGE_HIGH_GRAVITY ||
				 aGameMode == GameMode::GAMEMODE_CHALLENGE_PORTAL_COMBAT || aGameMode == GameMode::GAMEMODE_CHALLENGE_WAR_AND_PEAS ||
				 aGameMode == GameMode::GAMEMODE_CHALLENGE_INVISIGHOUL)
			mNumWaves = 20;
		else if (mApp->IsStormyNightLevel() || mApp->IsLittleTroubleLevel() || mApp->IsBungeeBlitzLevel() ||
				 aGameMode == GameMode::GAMEMODE_CHALLENGE_COLUMN || mApp->IsShovelLevel() || aGameMode == GameMode::GAMEMODE_CHALLENGE_WAR_AND_PEAS_2 ||
				 aGameMode == GameMode::GAMEMODE_CHALLENGE_WALLNUT_BOWLING_2 || aGameMode == GameMode::GAMEMODE_CHALLENGE_POGO_PARTY)
			mNumWaves = 30;
		else
			mNumWaves = 40;
	}

	ZombiePicker aZombiePicker;
	ZombiePickerInit(&aZombiePicker);
	ZombieType aIntroZombieType = GetIntroducedZombieType();
	TOD_ASSERT(mNumWaves <= MAX_ZOMBIE_WAVES);

	for (int aWave = 0; aWave < mNumWaves; aWave++)
	{
		ZombiePickerInitForWave(&aZombiePicker);
		mZombiesInWave[aWave][0] = ZombieType::ZOMBIE_INVALID;

		bool aIsFlagWave = IsFlagWave(aWave);
		bool aIsFinalWave = aWave == mNumWaves - 1;

		if (mApp->IsBungeeBlitzLevel() && aIsFlagWave)
		{
			for (int _i = 0; _i < 5; _i++)
				PutZombieInWave(ZombieType::ZOMBIE_BUNGEE, aWave, &aZombiePicker);

			if (!aIsFinalWave)
				continue;
		}

		int& aZombiePoints = aZombiePicker.mZombiePoints;
		if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND)
		{
			aZombiePoints = (mChallenge->mSurvivalStage * GetNumWavesPerSurvivalStage() + aWave + 10) * 2 / 5 + 1;
		}
		else if (mApp->IsSurvivalMode() && mChallenge->mSurvivalStage > 0)
		{
			aZombiePoints = (mChallenge->mSurvivalStage * GetNumWavesPerSurvivalStage() + aWave) * 2 / 5 + 1;
		}
		else if (mApp->IsAdventureMode() && mApp->HasFinishedAdventure() && mLevel != 5)
		{
			aZombiePoints = aWave * 2 / 5 + 1;
		}
		else
		{
			aZombiePoints = aWave / 3 + 1;
		}

        // Adventure only: raise the base zombie-point budget 2.5x, rounded UP,
        // before applying the original flag/miniboss/level-specific multipliers.
        if (mApp->IsAdventureMode())
            aZombiePoints = (aZombiePoints * 5 + 1) / 2;

		if (aIsFlagWave)
		{
			// Do not force Basic Zombies into flag waves; let the weighted picker fill the budget.
			aZombiePoints *= 2.5f;

			if (mApp->mGameMode != GameMode::GAMEMODE_CHALLENGE_WAR_AND_PEAS && mApp->mGameMode != GameMode::GAMEMODE_CHALLENGE_WAR_AND_PEAS_2)
			{
				PutZombieInWave(ZombieType::ZOMBIE_FLAG, aWave, &aZombiePicker);
			}
		}

		if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_COLUMN)
		{
			aZombiePoints *= 6;
		}
		else if (mApp->IsLittleTroubleLevel() || mApp->IsWallnutBowlingLevel())
		{
			aZombiePoints *= 4;
		}
		else if (mApp->IsMiniBossLevel())
		{
			aZombiePoints *= 3;
		}
		else if (mApp->IsStormyNightLevel() && mApp->IsAdventureMode())
		{
			aZombiePoints *= 3;
		}
		else if (mApp->IsShovelLevel() || mApp->IsBungeeBlitzLevel() || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_PORTAL_COMBAT || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_INVISIGHOUL)
		{
			aZombiePoints *= 2;
		}
		
		if (aIntroZombieType != ZombieType::ZOMBIE_INVALID && aIntroZombieType != ZombieType::ZOMBIE_DUCKY_TUBE)
		{
			bool aSpawnIntro = false;
			if ((aIntroZombieType == ZombieType::ZOMBIE_DIGGER || aIntroZombieType == ZombieType::ZOMBIE_BALLOON))
			{
				if (aWave + 1== 7 || aIsFinalWave)
				{
					aSpawnIntro = true;
				}
			}
			else if (aIntroZombieType == ZombieType::ZOMBIE_YETI)
			{
				if (aWave == mNumWaves / 2 && !mApp->mSawYeti)
				{
					aSpawnIntro = true;
				}
			}
			else if (aWave == mNumWaves / 2 || aIsFinalWave)
			{
				aSpawnIntro = true;
			}

			if (aSpawnIntro)
			{
				PutZombieInWave(aIntroZombieType, aWave, &aZombiePicker);
			}
		}

		if (mLevel == 50 && aIsFinalWave)
		{
			PutZombieInWave(ZombieType::ZOMBIE_GARGANTUAR, aWave, &aZombiePicker);
		}
		if (mApp->IsAdventureMode() && aIsFinalWave)
		{
			PutInMissingZombies(aWave, &aZombiePicker);
		}
		if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_COLUMN)
		{
			if (aWave % 10 == 5)
			{
				for (int _i = 0; _i < 10; _i++)
				{
					PutZombieInWave(ZombieType::ZOMBIE_LADDER, aWave, &aZombiePicker);
				}
			}

			if (aWave % 10 == 8)
			{
				for (int _i = 0; _i < 10; _i++)
				{
					PutZombieInWave(ZombieType::ZOMBIE_JACK_IN_THE_BOX, aWave, &aZombiePicker);
				}
			}

			if (aWave == 19)
			{
				for (int _i = 0; _i < 3; _i++)
				{
					PutZombieInWave(ZombieType::ZOMBIE_GARGANTUAR, aWave, &aZombiePicker);
				}
			}
			if (aWave == 29)
			{
				for (int _i = 0; _i < 5; _i++)
				{
					PutZombieInWave(ZombieType::ZOMBIE_GARGANTUAR, aWave, &aZombiePicker);
				}
			}
		}
		
		while (aZombiePoints > 0 && aZombiePicker.mZombieCount < MAX_ZOMBIES_IN_WAVE)
		{
			ZombieType aZombieType = PickZombieType(aZombiePoints, aWave, &aZombiePicker);
			PutZombieInWave(aZombieType, aWave, &aZombiePicker);
		}
	}
}

int Board::GetLevelRandSeed()
{
	int aRndSeed = mApp->mPlayerInfo->mId + mBoardRandSeed;
	if (mApp->IsAdventureMode())
	{
		aRndSeed += mApp->mPlayerInfo->mFinishedAdventure * 101 + mLevel;
	}
	else
	{
		aRndSeed += mChallenge->mSurvivalStage * 101 + mApp->mGameMode;
	}
	return aRndSeed;
}

void Board::LoadBackgroundImages()
{
	switch (mBackground)
	{
	case BackgroundType::BACKGROUND_1_DAY:
		TodLoadResources("DelayLoad_Background1");
		if ((mApp->IsAdventureMode() && mLevel <= 4) || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_RESODDED)
		{
			TodLoadResources("DelayLoad_BackgroundUnsodded");
		}
		break;

	case BackgroundType::BACKGROUND_2_NIGHT:
		TodLoadResources("DelayLoad_Background2");
		break;

	case BackgroundType::BACKGROUND_3_POOL:
		TodLoadResources("DelayLoad_Background3");
		break;

	case BackgroundType::BACKGROUND_4_FOG:
		TodLoadResources("DelayLoad_Background4");
		break;

	case BackgroundType::BACKGROUND_5_ROOF:
		TodLoadResources("DelayLoad_Background5");
		break;

	case BackgroundType::BACKGROUND_6_BOSS:
		TodLoadResources("DelayLoad_Background6");
		break;

	case BackgroundType::BACKGROUND_GREENHOUSE:
		TodLoadResources("DelayLoad_GreenHouseGarden");
		TodLoadResources("DelayLoad_GreenHouseOverlay");
		break;

	case BackgroundType::BACKGROUND_TREEOFWISDOM:
		ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_TREEOFWISDOM, true);
		break;

	case BackgroundType::BACKGROUND_ZOMBIQUARIUM:
		TodLoadResources("DelayLoad_Zombiquarium");
		TodLoadResources("DelayLoad_GreenHouseOverlay");
		break;

	case BackgroundType::BACKGROUND_MUSHROOM_GARDEN:
		TodLoadResources("DelayLoad_MushroomGarden");
		break;

	default:
		TOD_ASSERT();
		break;
	}
}

void Board::PickBackground()
{
	switch (mApp->mGameMode)
	{
	case GameMode::GAMEMODE_ADVENTURE:
		if (mLevel <= 1 * LEVELS_PER_AREA)
		{
			mBackground = BackgroundType::BACKGROUND_1_DAY;
		}
		else if (mLevel <= 2 * LEVELS_PER_AREA)
		{
			mBackground = BackgroundType::BACKGROUND_2_NIGHT;
		}
		else if (mLevel <= 3 * LEVELS_PER_AREA)
		{
			mBackground = BackgroundType::BACKGROUND_3_POOL;
		}
		else if (mApp->IsScaryPotterLevel())
		{
			mBackground = BackgroundType::BACKGROUND_2_NIGHT;
		}
		else if (mLevel <= 4 * LEVELS_PER_AREA)
		{
			mBackground = BackgroundType::BACKGROUND_4_FOG;
		}
		else if (mLevel < FINAL_LEVEL)
		{
			mBackground = BackgroundType::BACKGROUND_5_ROOF;
		}
		else if (mLevel == FINAL_LEVEL)
		{
			mBackground = BackgroundType::BACKGROUND_6_BOSS;
		}
		else
		{
			mBackground = BackgroundType::BACKGROUND_1_DAY;
		}
		break;

	case GameMode::GAMEMODE_SURVIVAL_NORMAL_STAGE_1:
	case GameMode::GAMEMODE_SURVIVAL_HARD_STAGE_1:
	case GameMode::GAMEMODE_SURVIVAL_ENDLESS_STAGE_1:
	case GameMode::GAMEMODE_CHALLENGE_WAR_AND_PEAS:
	case GameMode::GAMEMODE_CHALLENGE_WALLNUT_BOWLING:
	case GameMode::GAMEMODE_CHALLENGE_SLOT_MACHINE:
	case GameMode::GAMEMODE_CHALLENGE_SEEING_STARS:
	case GameMode::GAMEMODE_CHALLENGE_WALLNUT_BOWLING_2:
	case GameMode::GAMEMODE_CHALLENGE_ART_CHALLENGE_WALLNUT:
	case GameMode::GAMEMODE_CHALLENGE_SUNNY_DAY:
	case GameMode::GAMEMODE_CHALLENGE_RESODDED:
	case GameMode::GAMEMODE_CHALLENGE_BIG_TIME:
	case GameMode::GAMEMODE_CHALLENGE_ART_CHALLENGE_SUNFLOWER:
	case GameMode::GAMEMODE_CHALLENGE_ICE:
	case GameMode::GAMEMODE_CHALLENGE_SHOVEL:
	case GameMode::GAMEMODE_CHALLENGE_SQUIRREL:
		mBackground = BackgroundType::BACKGROUND_1_DAY;
		break;

	case GameMode::GAMEMODE_SURVIVAL_NORMAL_STAGE_2:
	case GameMode::GAMEMODE_SURVIVAL_HARD_STAGE_2:
	case GameMode::GAMEMODE_SURVIVAL_ENDLESS_STAGE_2:
	case GameMode::GAMEMODE_CHALLENGE_BEGHOULED:
	case GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST:
	case GameMode::GAMEMODE_CHALLENGE_PORTAL_COMBAT:
	case GameMode::GAMEMODE_CHALLENGE_WHACK_A_ZOMBIE:
	case GameMode::GAMEMODE_CHALLENGE_GRAVE_DANGER:
	case GameMode::GAMEMODE_SCARY_POTTER_1:
	case GameMode::GAMEMODE_SCARY_POTTER_2:
	case GameMode::GAMEMODE_SCARY_POTTER_3:
	case GameMode::GAMEMODE_SCARY_POTTER_4:
	case GameMode::GAMEMODE_SCARY_POTTER_5:
	case GameMode::GAMEMODE_SCARY_POTTER_6:
	case GameMode::GAMEMODE_SCARY_POTTER_7:
	case GameMode::GAMEMODE_SCARY_POTTER_8:
	case GameMode::GAMEMODE_SCARY_POTTER_9:
	case GameMode::GAMEMODE_SCARY_POTTER_ENDLESS:
	case GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_1:
	case GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_2:
	case GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_3:
	case GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_4:
	case GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_5:
	case GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_6:
	case GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_7:
	case GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_8:
	case GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_9:
	case GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_ENDLESS:
		mBackground = BackgroundType::BACKGROUND_2_NIGHT;
		break;

	case GameMode::GAMEMODE_SURVIVAL_NORMAL_STAGE_3:
	case GameMode::GAMEMODE_SURVIVAL_HARD_STAGE_3:
	case GameMode::GAMEMODE_SURVIVAL_ENDLESS_STAGE_3:
	case GameMode::GAMEMODE_CHALLENGE_LITTLE_TROUBLE:
	case GameMode::GAMEMODE_CHALLENGE_BOBSLED_BONANZA:
	case GameMode::GAMEMODE_CHALLENGE_SPEED:
	case GameMode::GAMEMODE_CHALLENGE_LAST_STAND:
	case GameMode::GAMEMODE_CHALLENGE_WAR_AND_PEAS_2:
	case GameMode::GAMEMODE_UPSELL:
	case GameMode::GAMEMODE_INTRO:
		mBackground = BackgroundType::BACKGROUND_3_POOL;
		break;

	case GameMode::GAMEMODE_SURVIVAL_NORMAL_STAGE_4:
	case GameMode::GAMEMODE_SURVIVAL_HARD_STAGE_4:
	case GameMode::GAMEMODE_SURVIVAL_ENDLESS_STAGE_4:
	case GameMode::GAMEMODE_CHALLENGE_RAINING_SEEDS:
	case GameMode::GAMEMODE_CHALLENGE_INVISIGHOUL:
	case GameMode::GAMEMODE_CHALLENGE_AIR_RAID:
	case GameMode::GAMEMODE_CHALLENGE_STORMY_NIGHT:
		mBackground = BackgroundType::BACKGROUND_4_FOG;
		break;

	case GameMode::GAMEMODE_SURVIVAL_NORMAL_STAGE_5:
	case GameMode::GAMEMODE_SURVIVAL_HARD_STAGE_5:
	case GameMode::GAMEMODE_SURVIVAL_ENDLESS_STAGE_5:
	case GameMode::GAMEMODE_CHALLENGE_COLUMN:
	case GameMode::GAMEMODE_CHALLENGE_POGO_PARTY:
	case GameMode::GAMEMODE_CHALLENGE_HIGH_GRAVITY:
	case GameMode::GAMEMODE_CHALLENGE_BUNGEE_BLITZ:
		mBackground = BackgroundType::BACKGROUND_5_ROOF;
		break;

	case GameMode::GAMEMODE_CHALLENGE_FINAL_BOSS:
		mBackground = BackgroundType::BACKGROUND_6_BOSS;
		break;

	case GameMode::GAMEMODE_CHALLENGE_ZOMBIQUARIUM:
		mBackground = BackgroundType::BACKGROUND_ZOMBIQUARIUM;
		break;

	case GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN:
		mBackground = BackgroundType::BACKGROUND_GREENHOUSE;
		break;

	case GameMode::GAMEMODE_TREE_OF_WISDOM:
		mBackground = BackgroundType::BACKGROUND_TREEOFWISDOM;
		break;

	default:
		TOD_ASSERT();
		break;
	}
	LoadBackgroundImages();

	if (mBackground == BackgroundType::BACKGROUND_1_DAY || mBackground == BackgroundType::BACKGROUND_GREENHOUSE || mBackground == BackgroundType::BACKGROUND_TREEOFWISDOM)
	{
		mPlantRow[0] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[1] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[2] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[3] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[4] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[5] = PlantRowType::PLANTROW_DIRT;

		if (mApp->IsAdventureMode() && mApp->IsFirstTimeAdventureMode())
		{
			if (mLevel == 1)
			{
				mPlantRow[0] = PlantRowType::PLANTROW_DIRT;
				mPlantRow[1] = PlantRowType::PLANTROW_DIRT;
				mPlantRow[3] = PlantRowType::PLANTROW_DIRT;
				mPlantRow[4] = PlantRowType::PLANTROW_DIRT;
			}
			else if (mLevel == 2 || mLevel == 3)
			{
				mPlantRow[0] = PlantRowType::PLANTROW_DIRT;
				mPlantRow[4] = PlantRowType::PLANTROW_DIRT;
			}
		}
		else if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_RESODDED)
		{
			mPlantRow[0] = PlantRowType::PLANTROW_DIRT;
			mPlantRow[4] = PlantRowType::PLANTROW_DIRT;
		}
	}
	else if (mBackground == BackgroundType::BACKGROUND_2_NIGHT)
	{
		mPlantRow[0] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[1] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[2] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[3] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[4] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[5] = PlantRowType::PLANTROW_DIRT;
	}
	else if (mBackground == BackgroundType::BACKGROUND_3_POOL || mBackground == BackgroundType::BACKGROUND_ZOMBIQUARIUM || mBackground == BackgroundType::BACKGROUND_4_FOG)
	{
		mPlantRow[0] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[1] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[2] = PlantRowType::PLANTROW_POOL;
		mPlantRow[3] = PlantRowType::PLANTROW_POOL;
		mPlantRow[4] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[5] = PlantRowType::PLANTROW_NORMAL;
	}
	else if (mBackground == BackgroundType::BACKGROUND_5_ROOF || mBackground == BackgroundType::BACKGROUND_6_BOSS)
	{
		mPlantRow[0] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[1] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[2] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[3] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[4] = PlantRowType::PLANTROW_NORMAL;
		mPlantRow[5] = PlantRowType::PLANTROW_DIRT;
	}
	else
	{
		TOD_ASSERT();
	}

	for (int x = 0; x < MAX_GRID_SIZE_X; x++)
	{
		for (int y = 0; y < MAX_GRID_SIZE_Y; y++)
		{
			if (mPlantRow[y] == PlantRowType::PLANTROW_DIRT)
			{
				mGridSquareType[x][y] = GridSquareType::GRIDSQUARE_DIRT;
			}
			else if (mPlantRow[y] == PlantRowType::PLANTROW_POOL && x >= 0 && x <= 8)
			{
				mGridSquareType[x][y] = GridSquareType::GRIDSQUARE_POOL;
			}
			else if (mPlantRow[y] == PlantRowType::PLANTROW_HIGH_GROUND && x >= 4 && x <= 8)
			{
				mGridSquareType[x][y] = GridSquareType::GRIDSQUARE_HIGH_GROUND;
			}
		}
	}

	MTRand aLevelRNG(GetLevelRandSeed());
	if (StageHasGraveStones())
	{
		if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_GRAVE_DANGER)
		{
			AddGraveStones(6, RandRangeInt(1, 2), aLevelRNG);
			AddGraveStones(7, RandRangeInt(1, 3), aLevelRNG);
			AddGraveStones(8, RandRangeInt(2, 3), aLevelRNG);
		}
		else if (mApp->IsWhackAZombieLevel())
		{
			mChallenge->WhackAZombiePlaceGraves(9);
		}
		else if (mBackground == BackgroundType::BACKGROUND_2_NIGHT)
		{
			if (mApp->IsSurvivalNormal(mApp->mGameMode))
			{
				AddGraveStones(5, 1, aLevelRNG);
				AddGraveStones(6, 1, aLevelRNG);
				AddGraveStones(7, 1, aLevelRNG);
				AddGraveStones(8, 2, aLevelRNG);
			}
			else if (!mApp->IsAdventureMode())
			{
				AddGraveStones(4, 1, aLevelRNG);
				AddGraveStones(5, 1, aLevelRNG);
				AddGraveStones(6, 2, aLevelRNG);
				AddGraveStones(7, 2, aLevelRNG);
				AddGraveStones(8, 3, aLevelRNG);
			}
			else if (mLevel == 11 || mLevel == 12 || mLevel == 13)
			{
				AddGraveStones(6, 1, aLevelRNG);
				AddGraveStones(7, 1, aLevelRNG);
				AddGraveStones(8, 2, aLevelRNG);
			}
			else if (mLevel == 14 || mLevel == 16)
			{
				AddGraveStones(5, 1, aLevelRNG);
				AddGraveStones(6, 1, aLevelRNG);
				AddGraveStones(7, 2, aLevelRNG);
				AddGraveStones(8, 3, aLevelRNG);
			}
			else if (mLevel == 17 || mLevel == 18 || mLevel == 19)
			{
				AddGraveStones(4, 1, aLevelRNG);
				AddGraveStones(5, 2, aLevelRNG);
				AddGraveStones(6, 2, aLevelRNG);
				AddGraveStones(7, 3, aLevelRNG);
				AddGraveStones(8, 3, aLevelRNG);
			}
			else if (mLevel >= 20)
			{
				AddGraveStones(3, 1, aLevelRNG);
				AddGraveStones(4, 2, aLevelRNG);
				AddGraveStones(5, 2, aLevelRNG);
				AddGraveStones(6, 2, aLevelRNG);
				AddGraveStones(7, 3, aLevelRNG);
				AddGraveStones(8, 3, aLevelRNG);
			}
			else
			{
				TOD_ASSERT();
			}
		}
	}
	PickSpecialGraveStone();
	if (StageHasBushes())
	{
		for (int i = 0; i < MAX_GRID_SIZE_Y; i++)
			AddBush(i);
	}
	for (int i = 0; i < MAX_GAMEPADS; i++)
		AddGamepadPlayer(i);
}

void Board::InitZombieWavesForLevel(int theForLevel)
{
	if (mApp->IsWhackAZombieLevel() || (mApp->IsWallnutBowlingLevel() && !mApp->IsFirstTimeAdventureMode()))
	{
		mChallenge->InitZombieWaves();
		return;
	}

	for (int aZombieType = ZombieType::ZOMBIE_NORMAL; aZombieType < ZombieType::NUM_ZOMBIE_TYPES; aZombieType++)
	{
		mZombieAllowed[aZombieType] = CanZombieSpawnOnLevel((ZombieType)aZombieType, theForLevel);
	}
}

bool Board::IsZombieWaveDistributionOk()
{
	if (!mApp->IsAdventureMode())
		return true;

	int aZombieTypeCount[(int)ZombieType::NUM_ZOMBIE_TYPES] = { 0 };
	for (int aWave = 0; aWave < mNumWaves; aWave++)
	{
		for (int aIndex = 0; aIndex < MAX_ZOMBIES_IN_WAVE; aIndex++)
		{
			ZombieType aZombieType = mZombiesInWave[aWave][aIndex];
			if (aZombieType == ZombieType::ZOMBIE_INVALID)
			{
				break;
			}

			TOD_ASSERT(aZombieType >= 0 && aZombieType < ZombieType::NUM_ZOMBIE_TYPES);
			aZombieTypeCount[(int)aZombieType]++;
		}
	}

	for (ZombieType aZombieType = ZombieType::ZOMBIE_NORMAL; aZombieType < ZombieType::NUM_ZOMBIE_TYPES; aZombieType = (ZombieType)((int)aZombieType + 1))
	{
		if (aZombieType != ZombieType::ZOMBIE_YETI && CanZombieSpawnOnLevel(aZombieType, mLevel) && aZombieTypeCount[(int)aZombieType] == 0)
		{
			TodTraceAndLog("Didn't spawn required zombie %s, level %d", SexyStringToStringFast(GetZombieDefinition(aZombieType).mZombieName), mLevel);
			return false;
		}
	}
	return true;
}

void Board::InitZombieWaves()
{
	memset(mZombieAllowed, false, sizeof(mZombieAllowed));
	if (mApp->IsAdventureMode())
	{
		InitZombieWavesForLevel(mLevel);
	}
	else
	{
		mChallenge->InitZombieWaves();
	}
	PickZombieWaves();
	TOD_ASSERT(IsZombieWaveDistributionOk());

	mCurrentWave = 0;
	mTotalSpawnedWaves = 0;
	mApp->mSawYeti = false;
	if (mApp->IsFirstTimeAdventureMode() && mLevel == 2)
	{
		mZombieCountDown = ZOMBIE_COUNTDOWN * 2;
	}
	else if (mApp->IsSurvivalMode() && mChallenge->mSurvivalStage > 0)
	{
		mZombieCountDown = ZOMBIE_COUNTDOWN_RANGE;
	}
	else
	{
		mZombieCountDown = ZOMBIE_COUNTDOWN_FIRST_WAVE;
	}

	mZombieHealthWaveStart = 0;
	mLastBungeeWave = 0;
	mProgressMeterWidth = 0;
	mHugeWaveCountDown = 0;
	mLevelAwardSpawned = false;
	mZombieCountDownStart = mZombieCountDown;
	mZombieHealthToNextWave = -1;
}

void Board::FreezeEffectsForCutscene(bool theFreeze)
{
	TodParticleSystem* aParticle = nullptr;
	while (IterateParticles(aParticle))
	{
		if (aParticle->mEffectType == ParticleEffect::PARTICLE_GRAVE_BUSTER)
		{
			if (aParticle->mEffectType == ParticleEffect::PARTICLE_POOL_SPARKLY && mIceTrapCounter == 0)
			{
				aParticle->mDontUpdate = theFreeze;
			}
		}
	}

	Reanimation* aReanim = nullptr;
	while (IterateReanimations(aReanim))
	{
		if (aReanim->mReanimationType == ReanimationType::REANIM_SLEEPING)
		{
			aReanim->mAnimRate = theFreeze ? 0.0f : RandRangeFloat(6, 8);
		}
	}
}

void Board::InitSurvivalStage()
{
	RefreshSeedPacketFromCursor();
	mApp->mSoundSystem->GamePause(true);
	FreezeEffectsForCutscene(true);
	mLevelComplete = false;
	InitZombieWaves();
	mApp->mGameScene = GameScenes::SCENE_LEVEL_INTRO;
	mApp->ShowSeedChooserScreen();
	mCutScene->StartLevelIntro();
	mSeedBank->UpdateWidth();

	for (int i = 0; i < SEEDBANK_MAX; i++)
	{
		SeedPacket* aPacket = &mSeedBank->mSeedPackets[i];
		aPacket->mX = GetSeedPacketPositionX(i);
		aPacket->mPacketType = SeedType::SEED_NONE;
		aPacket->mChosenPlantIndex = -1;
	}

	if (StageHasFog())
	{
		mFogBlownCountDown = FOG_BLOW_RETURN_TIME;
	}
	for (int j = 0; j < MAX_GRID_SIZE_Y; j++)
	{
		mWaveRowGotLawnMowered[j] = -100;
	}
}

Rect Board::GetGloveButtonRect()
{
    Rect aRect = GetShovelButtonRect();
    aRect.mX += aRect.mWidth + 4;
    return aRect;
}

Rect Board::GetShovelButtonRect()
{
	Rect aRect(GetSeedBankExtraWidth() + 456, 0, Sexy::IMAGE_SHOVELBANK->GetWidth(), Sexy::IMAGE_SEEDBANK->GetHeight());
	if (mApp->IsSlotMachineLevel() || mApp->IsSquirrelLevel())
	{
		aRect.mX = 600;
	}
	return aRect;
}

void Board::GetZenButtonRect(GameObjectType theObjectType, Rect& theRect)
{
	if (mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM)
	{
		if (theObjectType == GameObjectType::OBJECT_TYPE_NEXT_GARDEN)
		{
			theRect.mX = 30;
		}
	}

	theRect.mX = 30;
	bool usable = true;
	for (int anObject = GameObjectType::OBJECT_TYPE_WATERING_CAN; anObject <= GameObjectType::OBJECT_TYPE_NEXT_GARDEN; anObject++)
	{
		if (!CanUseGameObject((GameObjectType)anObject))
		{
			usable = false;
		}
	}
	if (usable)
	{
		theRect.mX = 0;
	}

	for (int anObject = GameObjectType::OBJECT_TYPE_WATERING_CAN; anObject < theObjectType; anObject++)
	{
		if (CanUseGameObject((GameObjectType)anObject))
		{
			theRect.mX += 70;//Sexy::IMAGE_SHOVELBANK->GetWidth();
		}
	}
	//return theRect;
}

void Board::InitLevel()
{
	mMainCounter = 0;
	mEnableGraveStones = false;
	mSodPosition = 0;
	mPrevBoardResult = mApp->mBoardResult;
	if (mApp->mPlayingQuickplay)
		mLevel = mApp->mQuickLevel;
	else
		mLevel = mApp->IsAdventureMode() ? mApp->mPlayerInfo->mLevel : 0;
	if (mApp->IsWhackAZombieLevel())
	{
		ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_HAMMER, true);
		Reanimation* aHammerReanim = mApp->AddReanimation(-25.0f, 16.0f, 0, ReanimationType::REANIM_HAMMER);
		aHammerReanim->mIsAttachment = true;
		aHammerReanim->PlayReanim("anim_whack_zombie", ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD, 0, 24.0f);
		aHammerReanim->mAnimTime = 1.0f;
		mCursorObject->mReanimCursorID = mApp->ReanimationGetID(aHammerReanim);
	}
	GameMode aGameMode = mApp->mGameMode;
	if (aGameMode != GameMode::GAMEMODE_TREE_OF_WISDOM && aGameMode != GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
	{
		mApp->mMusic->StopAllMusic();
	}
	PickBackground();
	// Roll Seed Bank uses images from two lazily loaded resource groups:
	// IMAGE_LOCK is in DelayLoad_ChallengeScreen, while the Augment cards use
	// IMAGE_ALMANAC_PLANTCARD, IMAGE_ALMANAC_GROUNDDAY, and IMAGE_PRESENT from
	// DelayLoad_Almanac. Load both before the shop or Augment UI can draw/update.
	if (IsRollSeedBank())
	{
		TodLoadResources("DelayLoad_ChallengeScreen");
		TodLoadResources("DelayLoad_Almanac");
	}
	InitZombieWaves();
	if (aGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED || aGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST ||
		mApp->IsScaryPotterLevel() || mApp->IsWhackAZombieLevel())
	{
		mSunMoney = 0;
	}
	else if (aGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND)
	{
		mSunMoney = 200;
	}
	else if (mApp->IsIZombieLevel())
	{
		mSunMoney = 6;
	}
	else if (mApp->IsAdventureMode() && mLevel == 1)
	{
		mSunMoney = 25;
	}
	else
	{
		mSunMoney = ROLL_START_SUN; // five Sun in other standard levels
	}

	memset(mRowPickingArray, 0, sizeof(mRowPickingArray));
	for (int aRow = 0; aRow < MAX_GRID_SIZE_Y; aRow++)
	{
		mWaveRowGotLawnMowered[aRow] = -100;
		mIceMinX[aRow] = BOARD_ICE_START;
		mIceTimer[aRow] = 0;
		mIceParticleID[aRow] = ParticleSystemID::PARTICLESYSTEMID_NULL;
		mRowPickingArray[aRow].mItem = aRow;
	}
	mNumSunsFallen = 0;
	if (!StageIsNight())
	{
		mSunCountDown = RandRangeInt(425, 700);
	}
	memset(mHelpDisplayed, 0, sizeof(mHelpDisplayed));
	mNumChosenPlants = 0;
    // A new level gets a fresh card pool and an empty Bench. Discard ownership tokens
    // from the previous level before resetting the per-level pool counters.
    for (int i = 0; i < SEEDBANK_MAX; ++i)
        mSeedBank->mSeedPackets[i].mPoolCardCount = 0;
    for (int i = 0; i < ROLL_BENCH_SLOTS; ++i)
    {
        mBenchPackets[i].mPoolCardCount = 0;
        mBenchPackets[i].EmptySlot();
        mBenchPackets[i].mIsBenchPacket = true;
        mBenchPackets[i].mIndex = i;
        mBenchPackets[i].mX = GetBenchSlotX(i);
        mBenchPackets[i].mY = GetBenchRestY();
    }
    mBenchCount = 0;
    mBenchMergeActive = false;
    mBenchMergeLeftIndex = -1;
    mBenchMergeStartTick = 0;
    memset(mSeedPoolRemaining, 0, sizeof(mSeedPoolRemaining));
    memset(mSeedPoolMaximum, 0, sizeof(mSeedPoolMaximum));
	memset(mRollBannedSeeds, 0, sizeof(mRollBannedSeeds));
	// Rebuild the shared card pool from eligible Tier plants each level; temporary bans reset here.
	if (IsRollSeedBank())
	{
		for (SeedType aSeedType = SeedType::SEED_PEASHOOTER; aSeedType < NUM_SEEDS_IN_CHOOSER && mNumChosenPlants < NUM_SEEDS_IN_CHOOSER; aSeedType = (SeedType)(aSeedType + 1))
		{
			// The Roll Shop keeps the first eight base plants available from the start,
			// but only the 40 explicitly-tiered plants participate in the card pool.
			bool aIsStarterPlant = (int)aSeedType < (int)SeedType::SEED_PEASHOOTER + ROLL_STARTER_PLANTS;
			int aTier = GetPlantTier(aSeedType);
			if (aTier > 0 && (aIsStarterPlant || mApp->SeedTypeAvailable(aSeedType)) && aSeedType != SeedType::SEED_IMITATER)
			{
				ChosenPlant& aChosenPlant = mChosenPlants[mNumChosenPlants++];
				aChosenPlant.mSeedType = aSeedType;
				aChosenPlant.mImitaterType = SeedType::SEED_NONE;
                int aPoolSize = GetPlantTierPoolSize(aSeedType);
                mSeedPoolMaximum[(int)aSeedType] = aPoolSize;
                mSeedPoolRemaining[(int)aSeedType] = aPoolSize;
			}
		}
	}

	mFreeRolls = 0;
	if (IsRollSeedBank() && mApp->mPlayerInfo && mApp->mPlayerInfo->mRollSlotRoundsRemaining > 0)
	{
		// The purchased Roll Slot bonus applies to the next three Roll Shop rounds.
		// Reset/retry calls InitLevel again and intentionally consumes another round.
		GrantFreeRolls(3);
		--mApp->mPlayerInfo->mRollSlotRoundsRemaining;
		mApp->WriteCurrentUserConfig();
	}
	mManualRollCount = 0;
	mPaidRollCount = 0;
	mRollTutorialChecked = false;
	mRollTutorialStep = ROLL_TUT_NONE;
	mRollTutorialTimer = 0;
	mActiveAugment = RollAugment::AUGMENT_NONE;
    memset(mAugmentPendingPackets, 0, sizeof(mAugmentPendingPackets));
    mAugmentPendingSilverCoins = 0;
    mAugmentPendingGoldCoins = 0;
    mPendingAugmentSunDrops = 0;
    memset(mAugmentFireTicks, 0, sizeof(mAugmentFireTicks));
    for (int x = 0; x < MAX_GRID_SIZE_X; ++x)
        for (int y = 0; y < MAX_GRID_SIZE_Y; ++y)
            mAugmentFireReanim[x][y] = ReanimationID::REANIMATIONID_NULL;
    memset(mEnabledAugments, 0, sizeof(mEnabledAugments));
    mActiveFlagCount = 0;
    ClearActiveAugmentTooltipPlant();
    UpdateInfiniteFumesLaunchRates();
    mActiveAugmentHoverStartTick = 0;
    mActiveAugmentTooltipVisible = false;
	mAugmentOffered = false;
    mAugmentStage2Offered = false;
    mAugmentStage = 1;
    memset(mStage1Shown, 0, sizeof(mStage1Shown));
    mSeedDraftChoosing = false;
    mSeedDraftRemainingMs = 0;
    mSeedDraftLastUpdateTick = 0;
    mSeedDraftAutoChoiceIndex = 0;
    mSeedDraftCopiesReserved = false;
    mSeedDraftPendingReward = SeedType::SEED_NONE;
    mPendingMiniCherryBombs = 0;
    mShiftingBenchPendingBonusMerges = 0;
    mShiftingBenchTransformPending = false;
    for (int k = 0; k < 3; ++k) mSeedDraftChoices[k] = SeedType::SEED_NONE;
    memset(mSeedDraftExcluded, 0, sizeof(mSeedDraftExcluded));
    mShovelCooldownUntil = mGloveCooldownUntil = 0;
	mAugmentChoosing = false;
	mAugmentChoices[0] = mAugmentChoices[1] = mAugmentChoices[2] = RollAugment::AUGMENT_NONE;
	mAugmentInitialChoices[0] = mAugmentInitialChoices[1] = mAugmentInitialChoices[2] = RollAugment::AUGMENT_NONE;
	mAugmentResetPending = true;
	mAugmentRerollResults[0] = mAugmentRerollResults[1] = mAugmentRerollResults[2] = -1;
	mAugmentHover = -1;
	mAugmentTimer = 0;
	mAugmentCherryUsed = false;
    mBalancedBudgetWavesRemaining = 0;
    mFinalReservesClaimed = false;
    mHologramEnabled = false;
    mHologramPlanting = false;
    mHologramLastPlant = SeedType::SEED_NONE;
    mHologramCooldown = 0;
	mAugmentHidden = false;
	mAugmentRerolled[0] = mAugmentRerolled[1] = mAugmentRerolled[2] = false;
	memset(mAugmentSeen, 0, sizeof(mAugmentSeen));
	mAugmentRestoreFastMode = false;
	mAugmentStartTick = 0;
	mAugmentLastPlantUpdateTick = 0;
	mShopClosedWaves = 0;
	mShopWasClosed = false;
    mGardenGloveEnabled = false;
	mSeedBank->mNumPackets = GetNumSeedBankSlots();
	mSeedBank->UpdateWidth();
	for (int i = 0; i < SEEDBANK_MAX; i++)
	{
		SeedPacket* aPacket = &mSeedBank->mSeedPackets[i];
		aPacket->mIndex = i;
		// On early Roll Shop levels there is no chooser-close pass to reposition the
		// live packets. Keep the reroll button at slot 0 and plant slots at 1..5.
		// During Remove Your Seed setup, keep the legacy chooser bank coordinates until it closes.
		aPacket->mX = (IsRollSeedBank() && !IsRemoveYourSeedMode()) ? GetSeedBankSlotX(i) : GetSeedPacketPositionX(i);
		aPacket->mY = 8;
		aPacket->mPacketType = SeedType::SEED_NONE;
		aPacket->mChosenPlantIndex = -1;
	}
	if (mApp->IsSlotMachineLevel())
	{
		TOD_ASSERT(mSeedBank->mNumPackets == 3);
		mSeedBank->mSeedPackets[0].SetPacketType(SeedType::SEED_SUNFLOWER);
		mSeedBank->mSeedPackets[1].SetPacketType(SeedType::SEED_PEASHOOTER);
		mSeedBank->mSeedPackets[2].SetPacketType(SeedType::SEED_SNOWPEA);
	}
	else if (aGameMode == GameMode::GAMEMODE_CHALLENGE_ICE)
	{
		TOD_ASSERT(mSeedBank->mNumPackets == 6);
		mSeedBank->mSeedPackets[0].SetPacketType(SeedType::SEED_PEASHOOTER);
		mSeedBank->mSeedPackets[1].SetPacketType(SeedType::SEED_CHERRYBOMB);
		mSeedBank->mSeedPackets[2].SetPacketType(SeedType::SEED_WALLNUT);
		mSeedBank->mSeedPackets[3].SetPacketType(SeedType::SEED_REPEATER);
		mSeedBank->mSeedPackets[4].SetPacketType(SeedType::SEED_SNOWPEA);
		mSeedBank->mSeedPackets[5].SetPacketType(SeedType::SEED_CHOMPER);
	}
	else if (aGameMode == GameMode::GAMEMODE_CHALLENGE_ZOMBIQUARIUM)
	{
		TOD_ASSERT(mSeedBank->mNumPackets == 2);
		mSeedBank->mSeedPackets[0].SetPacketType(SeedType::SEED_ZOMBIQUARIUM_SNORKLE);
		mSeedBank->mSeedPackets[1].SetPacketType(SeedType::SEED_ZOMBIQUARIUM_TROPHY);
	}
	else if (aGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_1)
	{
		TOD_ASSERT(mSeedBank->mNumPackets == 3);
		mSeedBank->mSeedPackets[0].SetPacketType(SeedType::SEED_ZOMBIE_NORMAL);
		mSeedBank->mSeedPackets[1].SetPacketType(SeedType::SEED_ZOMBIE_PAIL);
		mSeedBank->mSeedPackets[2].SetPacketType(SeedType::SEED_ZOMBIE_FOOTBALL);
	}
	else if (aGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_2)
	{
		TOD_ASSERT(mSeedBank->mNumPackets == 3);
		mSeedBank->mSeedPackets[0].SetPacketType(SeedType::SEED_ZOMBIE_NORMAL);
		mSeedBank->mSeedPackets[1].SetPacketType(SeedType::SEED_ZOMBIE_SCREEN_DOOR);
		mSeedBank->mSeedPackets[2].SetPacketType(SeedType::SEED_ZOMBIE_PAIL);
	}
	else if (aGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_3)
	{
		TOD_ASSERT(mSeedBank->mNumPackets == 3);
		mSeedBank->mSeedPackets[0].SetPacketType(SeedType::SEED_ZOMBIE_NORMAL);
		mSeedBank->mSeedPackets[1].SetPacketType(SeedType::SEED_ZOMBIE_PAIL);
		mSeedBank->mSeedPackets[2].SetPacketType(SeedType::SEED_ZOMBIE_DIGGER);
	}
	else if (aGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_4)
	{
		TOD_ASSERT(mSeedBank->mNumPackets == 3);
		mSeedBank->mSeedPackets[0].SetPacketType(SeedType::SEED_ZOMBIE_NORMAL);
		mSeedBank->mSeedPackets[1].SetPacketType(SeedType::SEED_ZOMBIE_PAIL);
		mSeedBank->mSeedPackets[2].SetPacketType(SeedType::SEED_ZOMBIE_LADDER);
	}
	else if (aGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_5)
	{
		TOD_ASSERT(mSeedBank->mNumPackets == 4);
		mSeedBank->mSeedPackets[0].SetPacketType(SeedType::SEED_ZOMBIE_NORMAL);
		mSeedBank->mSeedPackets[1].SetPacketType(SeedType::SEED_ZOMBIE_PAIL);
		mSeedBank->mSeedPackets[2].SetPacketType(SeedType::SEED_ZOMBIE_BUNGEE);
		mSeedBank->mSeedPackets[3].SetPacketType(SeedType::SEED_ZOMBIE_BALLOON);
	}
	else if (aGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_6)
	{
		TOD_ASSERT(mSeedBank->mNumPackets == 4);
		mSeedBank->mSeedPackets[0].SetPacketType(SeedType::SEED_ZOMBIE_NORMAL);
		mSeedBank->mSeedPackets[1].SetPacketType(SeedType::SEED_ZOMBIE_POLEVAULTER);
		mSeedBank->mSeedPackets[2].SetPacketType(SeedType::SEED_ZOMBIE_PAIL);
		mSeedBank->mSeedPackets[3].SetPacketType(SeedType::SEED_ZOMBIE_GARGANTUAR);
	}
	else if (aGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_7)
	{
		TOD_ASSERT(mSeedBank->mNumPackets == 4);
		mSeedBank->mSeedPackets[0].SetPacketType(SeedType::SEED_ZOMBIE_NORMAL);
		mSeedBank->mSeedPackets[1].SetPacketType(SeedType::SEED_ZOMBIE_POLEVAULTER);
		mSeedBank->mSeedPackets[2].SetPacketType(SeedType::SEED_ZOMBIE_PAIL);
		mSeedBank->mSeedPackets[3].SetPacketType(SeedType::SEED_ZOMBIE_DANCER);
	}
	else if (aGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_8)
	{
		TOD_ASSERT(mSeedBank->mNumPackets == 6);
		mSeedBank->mSeedPackets[0].SetPacketType(SeedType::SEED_ZOMBIE_IMP);
		mSeedBank->mSeedPackets[1].SetPacketType(SeedType::SEED_ZOMBIE_TRAFFIC_CONE);
		mSeedBank->mSeedPackets[2].SetPacketType(SeedType::SEED_ZOMBIE_PAIL);
		mSeedBank->mSeedPackets[3].SetPacketType(SeedType::SEED_ZOMBIE_BUNGEE);
		mSeedBank->mSeedPackets[4].SetPacketType(SeedType::SEED_ZOMBIE_DIGGER);
		mSeedBank->mSeedPackets[5].SetPacketType(SeedType::SEED_ZOMBIE_LADDER);
	}
	else if (aGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_9)
	{
		TOD_ASSERT(mSeedBank->mNumPackets == 8);
		mSeedBank->mSeedPackets[0].SetPacketType(SeedType::SEED_ZOMBIE_IMP);
		mSeedBank->mSeedPackets[1].SetPacketType(SeedType::SEED_ZOMBIE_TRAFFIC_CONE);
		mSeedBank->mSeedPackets[2].SetPacketType(SeedType::SEED_ZOMBIE_POLEVAULTER);
		mSeedBank->mSeedPackets[3].SetPacketType(SeedType::SEED_ZOMBIE_PAIL);
		mSeedBank->mSeedPackets[4].SetPacketType(SeedType::SEED_ZOMBIE_BUNGEE);
		mSeedBank->mSeedPackets[5].SetPacketType(SeedType::SEED_ZOMBIE_DIGGER);
		mSeedBank->mSeedPackets[6].SetPacketType(SeedType::SEED_ZOMBIE_LADDER);
		mSeedBank->mSeedPackets[7].SetPacketType(SeedType::SEED_ZOMBIE_FOOTBALL);
	}
	else if (aGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_ENDLESS)
	{
		TOD_ASSERT(mSeedBank->mNumPackets == 9);
		mSeedBank->mSeedPackets[0].SetPacketType(SeedType::SEED_ZOMBIE_IMP);
		mSeedBank->mSeedPackets[1].SetPacketType(SeedType::SEED_ZOMBIE_TRAFFIC_CONE);
		mSeedBank->mSeedPackets[2].SetPacketType(SeedType::SEED_ZOMBIE_POLEVAULTER);
		mSeedBank->mSeedPackets[3].SetPacketType(SeedType::SEED_ZOMBIE_PAIL);
		mSeedBank->mSeedPackets[4].SetPacketType(SeedType::SEED_ZOMBIE_BUNGEE);
		mSeedBank->mSeedPackets[5].SetPacketType(SeedType::SEED_ZOMBIE_DIGGER);
		mSeedBank->mSeedPackets[6].SetPacketType(SeedType::SEED_ZOMBIE_LADDER);
		mSeedBank->mSeedPackets[7].SetPacketType(SeedType::SEED_ZOMBIE_FOOTBALL);
		mSeedBank->mSeedPackets[8].SetPacketType(SeedType::SEED_ZOMBIE_DANCER);
	}
	else if (mApp->IsScaryPotterLevel())
	{
		TOD_ASSERT(mSeedBank->mNumPackets == 1);
		mSeedBank->mSeedPackets[0].SetPacketType(SeedType::SEED_CHERRYBOMB);
	}
	else if (mApp->IsWhackAZombieLevel())
	{
		TOD_ASSERT(mSeedBank->mNumPackets == 3);
		mSeedBank->mSeedPackets[0].SetPacketType(SeedType::SEED_POTATOMINE);
		mSeedBank->mSeedPackets[1].SetPacketType(SeedType::SEED_GRAVEBUSTER);
		mSeedBank->mSeedPackets[2].SetPacketType(mApp->IsAdventureMode() ? SeedType::SEED_CHERRYBOMB : SeedType::SEED_ICESHROOM);
	}
	else if (!IsRollSeedBank() && !ChooseSeedsOnCurrentLevel() && !HasConveyorBeltSeedBank())
	{
		mSeedBank->mNumPackets = GetNumSeedsInBank();
		for (int i = 0; i < mSeedBank->mNumPackets; i++)
		{
			mSeedBank->mSeedPackets[i].SetPacketType((SeedType)i);
		}
	}
	if (IsRollSeedBank() && !ChooseSeedsOnCurrentLevel())
		mSeedBank->RollAllSlots(false, true);
	MarkAllDirty();
	
	mPaused = false;
	mOutOfMoneyCounter = 0;
	if (StageHasFog())
	{
		mFogBlownCountDown = 200;
		mFogOffset = 1065 - LeftFogColumn() * 70;
	}
	mChallenge->InitLevel();
}

Reanimation* Board::CreateRakeReanim(float theRakeX, float theRakeY, int theRenderOrder)
{
	Reanimation* aReanim = mApp->AddReanimation(GridToPixelX(theRakeX, theRakeY) + 20, GridToPixelY(theRakeX, theRakeY), theRenderOrder, REANIM_RAKE);
	aReanim->mAnimRate = 0;
	aReanim->mLoopType = ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD;
	aReanim->mIsAttachment = true;
	return aReanim;
}

void Board::PlaceRake()
{
	if (!mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_RAKE])
		return;

	int aGridX = 7;
	if (mApp->IsScaryPotterLevel())
	{
		GridItem* aGridItem = nullptr;
		while (IterateGridItems(aGridItem))
		{
			if (aGridItem->mGridItemType == GridItemType::GRIDITEM_SCARY_POT && aGridItem->mGridX <= aGridX && aGridItem->mGridX > 0)
			{
				aGridX = aGridItem->mGridX - 1;
			}
		}
	}
	else
	{
		if (!StageHasZombieWalkInFromRight() || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED ||
			mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BOBSLED_BONANZA)
			return;
	}

	int aPickCount = 0;
	TodWeightedArray aPickArray[MAX_GRID_SIZE_Y];
	for (int aRow = 0; aRow < MAX_GRID_SIZE_Y; aRow++)
	{
		if (aRow != 5 && mPlantRow[aRow] == PlantRowType::PLANTROW_NORMAL)
		{
			aPickArray[aPickCount].mWeight = 1;
			aPickArray[aPickCount].mItem = aRow;
			aPickCount++;
		}
	}
	if (aPickCount == 0)
		return;

	int aGridY = TodPickFromWeightedArray(aPickArray, aPickCount);
	mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_RAKE]--;
	GridItem* aRake = mGridItems.DataArrayAlloc();
	aRake->mGridItemType = GridItemType::GRIDITEM_RAKE;
	aRake->mGridX = aGridX;
	aRake->mGridY = aGridY;
	aRake->mPosX = GridToPixelX(aGridX, aGridY);
	aRake->mPosY = GridToPixelY(aGridX, aGridY);
	aRake->mRenderOrder = MakeRenderOrder(RenderLayer::RENDER_LAYER_GRAVE_STONE, aGridY, 9);
	aRake->mGridItemReanimID = mApp->ReanimationGetID(CreateRakeReanim(aGridX, aGridY, 0));
	aRake->mGridItemState = GridItemState::GRIDITEM_STATE_RAKE_ATTRACTING;
}

void Board::InitLawnMowers()
{
	GameMode aGameMode = mApp->mGameMode;
	if (aGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED || aGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST ||
		aGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN || aGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM ||
		aGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND || aGameMode == GameMode::GAMEMODE_CHALLENGE_ZOMBIQUARIUM ||
		mApp->IsSquirrelLevel() || mApp->IsIZombieLevel() || (StageHasRoof() && !mApp->mPlayerInfo->mPurchases[StoreItem::STORE_ITEM_ROOF_CLEANER]))
		return;

	for (int aRow = 0; aRow < MAX_GRID_SIZE_Y; aRow++)
	{
		if (aGameMode == GameMode::GAMEMODE_CHALLENGE_RESODDED && aRow >= 5)
			continue;
		if ((!mApp->IsScaryPotterLevel() || (mApp->IsAdventureMode() && mLevel == 35)) && (aGameMode == GameMode::GAMEMODE_CHALLENGE_RESODDED || mPlantRow[aRow] != PlantRowType::PLANTROW_DIRT))
		{
			LawnMower* aLawnMower = mLawnMowers.DataArrayAlloc();
			aLawnMower->LawnMowerInitialize(aRow);
			aLawnMower->mVisible = false;
		}
	}
}

bool Board::ChooseSeedsOnCurrentLevel()
{
	if (mApp->IsChallengeWithoutSeedBank() || HasConveyorBeltSeedBank())
		return false;

	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ICE || 
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST || 
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZOMBIQUARIUM)
		return false;

	if (mApp->IsIZombieLevel() || mApp->IsSlotMachineLevel())
		return false;

	return (!mApp->IsAdventureMode() || mLevel >= ROLL_REMOVE_SEED_START_LEVEL);
}

void Board::StartLevel()
{
	mCoinBankFadeCount = 0;
	mApp->mLastLevelStats->Reset();
	mChallenge->StartLevel();

    // CutScene::StartLevelIntro deliberately places the SeedBank above the screen.
    // Its slide-in is driven by a narrow cutscene-time interval, so a skipped or
    // resumed intro can enter SCENE_PLAYING while the bank is still offscreen.
    // Finalize ONLY the visible bank position when entering gameplay. Never
    // rebuild or reroll packets here: their Plant Pool reservations belong to
    // the existing level and must survive this transition.
    if (IsRollSeedBank() && mSeedBank != nullptr)
    {
        mSeedBank->Move(SEED_BANK_OFFSET_X_END, 0);
        mSeedBank->mVisible = true;
        mSeedBank->mCutSceneDarken = 128; // same endpoint as CutScene::AnimateBoard
    }

	if (mApp->IsSurvivalEndless(mApp->mGameMode) && GetSurvivalFlagsCompleted() >= 20)
	{
		mApp->GetAchievement(AchievementType::ACHIEVEMENT_IMMORTAL);
	}

	if (mApp->IsSurvivalMode() && mChallenge->mSurvivalStage > 0)
	{
		mApp->EraseFile(GetSavedGameName(mApp->mGameMode, mApp->mPlayerInfo->mId));
		FreezeEffectsForCutscene(false);
		mApp->mSoundSystem->GamePause(false);
	}
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ICE || 
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN || 
		mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM ||
		mApp->mGameMode == GameMode::GAMEMODE_UPSELL || 
		mApp->mGameMode == GameMode::GAMEMODE_INTRO || 
		mApp->IsFinalBossLevel())
		return;

	mApp->mMusic->StartGameMusic();
}

LawnMower* Board::GetBottomLawnMower()
{
	LawnMower* aLawnMower = nullptr;
	LawnMower* aBottomMower = nullptr;
	while (IterateLawnMowers(aLawnMower))
	{
		if (aLawnMower->mMowerState == LawnMowerState::MOWER_TRIGGERED || aLawnMower->mMowerState == LawnMowerState::MOWER_SQUISHED)
			continue;

		if (aBottomMower == nullptr || aBottomMower->mRow < aLawnMower->mRow)
		{
			aBottomMower = aLawnMower;
		}
	}
	return aBottomMower;
}

void Board::UpdateLevelEndSequence()
{
	if (mNextSurvivalStageCounter > 0)
	{
		if (!IsScaryPotterDaveTalking())
		{
			mNextSurvivalStageCounter--;
			if (mApp->IsAdventureMode() && mApp->IsScaryPotterLevel() && mNextSurvivalStageCounter == 300)
			{
				mApp->CrazyDaveEnter();
				mApp->CrazyDaveTalkIndex(mChallenge->mSurvivalStage == 0 ? 2700 : 2800);
				mChallenge->PuzzleNextStageClear();
				mNextSurvivalStageCounter = 100;
			}
		}

		if (mNextSurvivalStageCounter == 1 && mApp->IsSurvivalMode())
		{
			TryToSaveGame();
		}

		if (!mNextSurvivalStageCounter)
		{
			if (mApp->IsScaryPotterLevel())
			{
				if (mApp->IsAdventureMode())
					return;

				if (!IsFinalScaryPotterStage())
				{
					mChallenge->PuzzleNextStageClear();
					mChallenge->ScaryPotterPopulate();
				}
			}
			else if (LawnApp::IsEndlessIZombie(mApp->mGameMode))
			{
				mChallenge->PuzzleNextStageClear();
				mChallenge->IZombieInitLevel();
			}
			else if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND)
			{
				ClearAdvice(AdviceType::ADVICE_NONE);
			}
			else
			{
				mLevelComplete = true;
				RemoveZombiesForRepick();
			}
			return;
		}
	}

	if (mBoardFadeOutCounter < 0)
		return;

	mBoardFadeOutCounter--;
	if (mBoardFadeOutCounter == 0)
	{
		if (mApp->mPlayingQuickplay && mLevel != FINAL_LEVEL)
		{
			LawnDialog* aDialog = (LawnDialog*)mApp->DoDialog(DIALOG_MESSAGE, true, _S("[QUICK_PLAY_HEADER]"), _S("[QUICK_PLAY]"), "", Dialog::BUTTONS_YES_NO);
			if (aDialog->WaitForResult(true) == Dialog::ID_YES)
			{
				mApp->mQuickLevel++;
				mApp->StartQuickPlay();
			}
		}
		mLevelComplete = true;
		return;
	}
	if (mBoardFadeOutCounter == 300)
	{
		if (!IsSurvivalStageWithRepick() && !(mLevel == 9 || mLevel == 19 || mLevel == 29 || mLevel == 39 || mLevel == 49))
		{
			mApp->PlaySample(Sexy::SOUND_LIGHTFILL);
		}
	}

	if (mScoreNextMowerCounter > 0)
	{
		mScoreNextMowerCounter--;
		if (mScoreNextMowerCounter)
		{
			return;
		}
	}

	if (CanDropLoot() && !IsSurvivalStageWithRepick() && !mApp->mPlayingQuickplay)
	{
		mScoreNextMowerCounter = 40;
		LawnMower* aLawnMower = GetBottomLawnMower();
		if (aLawnMower)
		{
			AddCoin(aLawnMower->mPosX + 40, aLawnMower->mPosY + 40, CoinType::COIN_GOLD, CoinMotion::COIN_MOTION_LAWNMOWER_COIN);
			SoundInstance* aSoundInstance = mApp->mSoundManager->GetSoundInstance(Sexy::SOUND_POINTS);
			if (aSoundInstance)
			{
				aSoundInstance->Play(false, true);
				float aPitch = ClampFloat(6 - CountUntriggerLawnMowers(), 0.0f, 6.0f);
				aSoundInstance->AdjustPitch(aPitch);
			}
			aLawnMower->Die();
		}
	}
}

void Board::CompleteEndLevelSequenceForSaving()
{
	if (CanDropLoot())
	{
		LawnMower* aLawnMower = nullptr;
		while (IterateLawnMowers(aLawnMower))
		{
			if (aLawnMower->mMowerState != LawnMowerState::MOWER_TRIGGERED && aLawnMower->mMowerState != LawnMowerState::MOWER_SQUISHED)
			{
				int aCoinValue = Coin::GetCoinValue(CoinType::COIN_GOLD);
				mApp->mPlayerInfo->AddCoins(aCoinValue);
				mCoinsCollected += aCoinValue;
			}
		}
	}

	Coin* aCoin = nullptr;
	while (IterateCoins(aCoin))
	{
		if (aCoin->mIsBeingCollected)
		{
			aCoin->ScoreCoin();
		}
		else
		{
			aCoin->Die();
		}
	}
	
	mApp->UpdatePlayerProfileForFinishingLevel();
    ClearWonAugmentDraft();
}

void Board::FadeOutLevel()
{
	if (mApp->mGameScene != GameScenes::SCENE_PLAYING)
	{
		RefreshSeedPacketFromCursor();
		mApp->mLastLevelStats->Reset();
		mLevelComplete = true;
	}

	bool aNeedSoundEffect = true;
	if (mApp->IsScaryPotterLevel() && !IsFinalScaryPotterStage())
	{
		aNeedSoundEffect = false;
	}
	else if (IsSurvivalStageWithRepick() || IsLastStandStageWithRepick() || mApp->IsEndlessIZombie(mApp->mGameMode))
	{
		aNeedSoundEffect = false;
	}
	if (aNeedSoundEffect)
	{
		mApp->mMusic->StopAllMusic();
		if (mApp->IsAdventureMode() && mLevel == 50)
		{
			mApp->PlayFoley(FoleyType::FOLEY_FINAL_FANFARE);
		}
		else if (mApp->TrophiesNeedForGoldSunflower() == 1)
		{
			mApp->PlayFoley(FoleyType::FOLEY_FINAL_FANFARE);
		}
		else
		{
			mApp->PlayFoley(FoleyType::FOLEY_WINMUSIC);
		}
	}

	if (mApp->IsScaryPotterLevel() && !IsFinalScaryPotterStage())
	{
		mNextSurvivalStageCounter = 500;
		if (mApp->IsAdventureMode())
		{
			ClearAdvice(AdviceType::ADVICE_NONE);
		}
		else
		{
			mLevelAwardSpawned = true;
			SexyString aStreakStr = mApp->IsEndlessScaryPotter(mApp->mGameMode) ? _S("[ADVICE_MORE_SCARY_POTS]") : _S("[ADVICE_3_IN_A_ROW]");
			SexyString aMessage = TodReplaceNumberString(aStreakStr, _S("{STREAK}"), mChallenge->mSurvivalStage + 1);
			PuzzleSaveStreak();
			ClearAdvice(AdviceType::ADVICE_NONE);
			DisplayAdvice(aMessage, MessageStyle::MESSAGE_STYLE_BIG_MIDDLE, AdviceType::ADVICE_NONE);
		}
		return;
	}

	if (mApp->IsEndlessIZombie(mApp->mGameMode))
	{
		mNextSurvivalStageCounter = 500;
		SexyString aMessage = TodReplaceNumberString(_S("[ADVICE_MORE_IZOMBIE]"), _S("{STREAK}"), mChallenge->mSurvivalStage + 1);
		PuzzleSaveStreak();
		ClearAdvice(AdviceType::ADVICE_NONE);
		DisplayAdvice(aMessage, MessageStyle::MESSAGE_STYLE_BIG_MIDDLE, AdviceType::ADVICE_NONE);
		return;
	}

	if (IsLastStandStageWithRepick())
	{
		mNextSurvivalStageCounter = 500;
		mChallenge->LastStandCompletedStage();
		return;
	}

	if (!IsSurvivalStageWithRepick())
	{
		RefreshSeedPacketFromCursor();
		mApp->mLastLevelStats->mUnusedLawnMowers = CountUntriggerLawnMowers();

		mBoardFadeOutCounter = 600;
		if (mLevel == 9 || mLevel == 19 || mLevel == 29 || mLevel == 39 || mLevel == 49)
		{
			mBoardFadeOutCounter = 500;
		}

		if (CanDropLoot())
		{
			mScoreNextMowerCounter = 200;
		}

		Coin* aCoin = nullptr;
		while (IterateCoins(aCoin))
		{
			aCoin->TryAutoCollectAfterLevelAward();
		}
	}
	else
	{
		TOD_ASSERT(mApp->IsSurvivalMode());
		mNextSurvivalStageCounter = 500;
		DisplayAdvice(_S("[ADVICE_MORE_ZOMBIES]"), MessageStyle::MESSAGE_STYLE_BIG_MIDDLE, AdviceType::ADVICE_NONE);
		mApp->mMusic->FadeOut(500);
		mApp->PlaySample(Sexy::SOUND_HUGE_WAVE);
		for (int aRow = 0; aRow < MAX_GRID_SIZE_Y; aRow++)
		{
			mIceTimer[aRow] = mNextSurvivalStageCounter;
		}
	}
	mApp->mIsFastMode = false;
	mApp->SetCursor(CURSOR_POINTER);
}

void Board::DisplayAdvice(const SexyString& theAdvice, MessageStyle theMessageStyle, AdviceType theHelpIndex)
{
	if (theHelpIndex != AdviceType::ADVICE_NONE)
	{
		if (mHelpDisplayed[theHelpIndex])
			return;

		mHelpDisplayed[theHelpIndex] = true;
	}

	mAdvice->SetLabel(theAdvice, theMessageStyle);
	mHelpIndex = theHelpIndex;
}

void Board::DisplayAdviceAgain(const SexyString& theAdvice, MessageStyle theMessageStyle, AdviceType theHelpIndex)
{
	if (theHelpIndex != AdviceType::ADVICE_NONE)
	{
		mHelpDisplayed[theHelpIndex] = false;
	}
	DisplayAdvice(theAdvice, theMessageStyle, theHelpIndex);
}

void Board::ClearAdviceImmediately()
{
	ClearAdvice(AdviceType::ADVICE_NONE);
	mAdvice->mDuration = 0;
}

void Board::ClearAdvice(AdviceType theHelpIndex)
{
	if (theHelpIndex == AdviceType::ADVICE_NONE || theHelpIndex == mHelpIndex)
	{
		mAdvice->ClearLabel();
		mHelpIndex = AdviceType::ADVICE_NONE;
	}
}

Coin* Board::AddCoin(int theX, int theY, CoinType theCoinType, CoinMotion theCoinMotion)
{
	Coin* aCoin = mCoins.DataArrayAlloc();
	aCoin->CoinInitialize(theX, theY, theCoinType, theCoinMotion);
	if (mApp->IsFirstTimeAdventureMode() && mLevel == 1 && !(mApp->IsAdventureMode() && !mApp->mPlayingQuickplay && IsRollSeedBank() && mLevel <= 2))
	{
		DisplayAdvice(_S("[ADVICE_CLICK_ON_SUN]"), MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1_STAY, AdviceType::ADVICE_CLICK_ON_SUN);
	}
	return aCoin;
}

bool Board::IsPlantInCursor()
{
	return 
		mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_BANK || 
		mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_USABLE_COIN || 
		mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_GLOVE || 
		mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_DUPLICATOR || 
		mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_WHEEL_BARROW;
}

SeedType Board::GetSeedTypeInCursor()
{
	if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_WHEEELBARROW)
	{
		PottedPlant* aPottedPlant = mApp->mZenGarden->GetPottedPlantInWheelbarrow();
		if (aPottedPlant)
		{
			return aPottedPlant->mSeedType;
		}
	}

	if (!IsPlantInCursor())
	{
		return SeedType::SEED_NONE;
	}
	return mCursorObject->mType == SeedType::SEED_IMITATER ? mCursorObject->mImitaterType : mCursorObject->mType;
}

void Board::RefreshSeedPacketFromCursor()
{
    if (mHologramPlanting)
    {
        mHologramPlanting = false;
        ClearCursor();
        return;
    }
	if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_USABLE_COIN)
	{
		mCoins.DataArrayTryToGet(mCursorObject->mCoinID)->DroppedUsableSeed();
	}
	else if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_BANK)
	{
        // Hologram borrows the bank-plant cursor type but deliberately uses index -1.
        // Cancelling that cursor must not access the SeedBank with an invalid index.
        const int aSeedBankIndex = mCursorObject->mSeedBankIndex;
        if (aSeedBankIndex >= 0 && aSeedBankIndex < mSeedBank->mNumPackets)
        {
            mSeedBank->mSeedPackets[aSeedBankIndex].Activate();
        }
        else if (aSeedBankIndex <= -2)
        {
            int aBenchIndex = -aSeedBankIndex - 2;
            if (aBenchIndex >= 0 && aBenchIndex < mBenchCount)
                mBenchPackets[aBenchIndex].Activate();
        }
	}
	ClearCursor();
}

bool Board::IsPoolSquare(int theGridX, int theGridY)
{
	if (theGridX >= 0 && theGridY >= 0)
	{
		TOD_ASSERT(theGridX < MAX_GRID_SIZE_X && theGridY < MAX_GRID_SIZE_Y);
		return mGridSquareType[theGridX][theGridY] == GridSquareType::GRIDSQUARE_POOL;
	}
	return false;
}

Plant* Board::NewPlant(int theGridX, int theGridY, SeedType theSeedType, SeedType theImitaterType)
{
	Plant* aPlant = mPlants.DataArrayAlloc();
	aPlant->mIsOnBoard = true;
	aPlant->PlantInitialize(theGridX, theGridY, theSeedType, theImitaterType);
	return aPlant;
}

void Board::DoPlantingEffects(int theGridX, int theGridY, Plant* thePlant)
{
	int aXPos = GridToPixelX(theGridX, theGridY) + 41;
	int aYPos = GridToPixelY(theGridX, theGridY) + 74;
	if (thePlant)
	{
		if (thePlant->mSeedType == SeedType::SEED_LILYPAD)
		{
			aYPos += 15;
		}
		else if (thePlant->mSeedType == SeedType::SEED_FLOWERPOT)
		{
			aYPos += 30;
		}
	}
	
	if (mBackground == BackgroundType::BACKGROUND_GREENHOUSE)
	{
		mApp->PlayFoley(FoleyType::FOLEY_CERAMIC);
		return;
	}
	if (mBackground == BackgroundType::BACKGROUND_ZOMBIQUARIUM)
	{
		mApp->PlayFoley(FoleyType::FOLEY_PLANT_WATER);
		return;
	}
	if (Plant::IsFlying(thePlant->mSeedType))
	{
		mApp->PlayFoley(FoleyType::FOLEY_PLANT);
		return;
	}

	if (IsPoolSquare(theGridX, theGridY))
	{
		mApp->PlayFoley(FoleyType::FOLEY_PLANT_WATER);
		mApp->AddTodParticle(aXPos, aYPos, RenderLayer::RENDER_LAYER_TOP, ParticleEffect::PARTICLE_PLANTING_POOL);
	}
	else
	{
		mApp->PlayFoley(FoleyType::FOLEY_PLANT);
		mApp->AddTodParticle(aXPos, aYPos, RenderLayer::RENDER_LAYER_TOP, ParticleEffect::PARTICLE_PLANTING);
	}
}

Plant* Board::AddPlant(int theGridX, int theGridY, SeedType theSeedType, SeedType theImitaterType)
{
	Plant* aPlant = NewPlant(theGridX, theGridY, theSeedType, theImitaterType);
	DoPlantingEffects(theGridX, theGridY, aPlant);
	mChallenge->PlantAdded(aPlant);

	int aSunPlantsCount = CountPlantByType(SeedType::SEED_SUNSHROOM) + CountPlantByType(SeedType::SEED_SUNFLOWER);
	if (aSunPlantsCount > mMaxSunPlants)
	{
		mMaxSunPlants = aSunPlantsCount;  //mMaxSunPlants = max(aSunPlantsCount, mMaxSunPlants);
	}

	if (theSeedType == SeedType::SEED_PEASHOOTER ||
		theSeedType == SeedType::SEED_SNOWPEA ||
		theSeedType == SeedType::SEED_REPEATER ||
		theSeedType == SeedType::SEED_THREEPEATER ||
		theSeedType == SeedType::SEED_SPLITPEA ||
		theSeedType == SeedType::SEED_GATLINGPEA)
	{
		mPeashootersUsed = true;
	}

	if (theSeedType == SeedType::SEED_CABBAGEPULT ||
		theSeedType == SeedType::SEED_KERNELPULT ||
		theSeedType == SeedType::SEED_MELONPULT ||
		theSeedType == SeedType::SEED_WINTERMELON)
	{
		mCatapultsUsed = true;
	}

	bool aIsFungi = Plant::IsNocturnal(theSeedType);

	if (Plant::IsFlying(theSeedType) && LawnHasNocturnal()) {
		mMushroomsNCoffeeUsed = true;
	}
	if (aIsFungi) {
		mMushroomsUsed = true;
	}
	if(!aIsFungi && theSeedType != SeedType::SEED_INSTANT_COFFEE)
	{
		mUsedNonMushrooms = true;
	}

	return aPlant;
}

Plant* Board::GetPumpkinAt(int theGridX, int theGridY)
{
	Plant* aPlant = nullptr;
	while (IteratePlants(aPlant))
	{
		if (aPlant->mPlantCol == theGridX && aPlant->mRow == theGridY && !aPlant->NotOnGround() && aPlant->mSeedType == SeedType::SEED_PUMPKINSHELL)
		{
			return aPlant;
		}
	}
	return nullptr;
}

Plant* Board::GetFlowerPotAt(int theGridX, int theGridY)
{
	Plant* aPlant = nullptr;
	while (IteratePlants(aPlant))
	{
		if (aPlant->mPlantCol == theGridX && aPlant->mRow == theGridY && !aPlant->NotOnGround() && aPlant->mSeedType == SeedType::SEED_FLOWERPOT)
		{
			return aPlant;
		}
	}
	return nullptr;
}

bool Board::LawnHasNocturnal()
{
	Plant* aPlant = nullptr;
	while (IteratePlants(aPlant))
	{
		if (aPlant->IsNocturnal(aPlant->mSeedType))
			return true;
	}
	return false;
}

void Board::GetPlantsOnLawn(int theGridX, int theGridY, PlantsOnLawn* thePlantOnLawn)
{
	thePlantOnLawn->mUnderPlant = nullptr;
	thePlantOnLawn->mPumpkinPlant = nullptr;
	thePlantOnLawn->mFlyingPlant = nullptr;
	thePlantOnLawn->mNormalPlant = nullptr;

	if (theGridX < 0 || theGridX >= MAX_GRID_SIZE_X || theGridY < 0 || theGridY >= MAX_GRID_SIZE_Y)
		return;

	if (mApp->IsWallnutBowlingLevel() && !mCutScene->IsInShovelTutorial())
		return;

	Plant* aPlant = nullptr;
	while (IteratePlants(aPlant))
	{
		SeedType aSeedType = aPlant->mSeedType;
		if (aSeedType == SeedType::SEED_IMITATER && aPlant->mImitaterType != SeedType::SEED_NONE)
		{
			aSeedType = aPlant->mImitaterType;
		}

		if (aPlant->mRow != theGridY)
		{
			continue;
		}
		if (aSeedType == SeedType::SEED_COBCANNON)
		{
			if (aPlant->mPlantCol < theGridX - 1 || aPlant->mPlantCol > theGridX)
			{
				continue;
			}
		}
		else
		{
			if (aPlant->mPlantCol != theGridX)
			{
				continue;
			}
		}
		if (aPlant->NotOnGround())
		{
			continue;
		}

		if (Plant::IsFlying(aPlant->mSeedType))
		{
			TOD_ASSERT(!thePlantOnLawn->mFlyingPlant);
			thePlantOnLawn->mFlyingPlant = aPlant;
		}
		else if (aSeedType == SeedType::SEED_FLOWERPOT || (aSeedType == SeedType::SEED_LILYPAD && mApp->mGameMode != GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN))
		{
			thePlantOnLawn->mUnderPlant = aPlant;
		}
		else if (aSeedType == SeedType::SEED_PUMPKINSHELL)
		{
			TOD_ASSERT(!thePlantOnLawn->mPumpkinPlant);
			thePlantOnLawn->mPumpkinPlant = aPlant;
		}
		else
		{
			thePlantOnLawn->mNormalPlant = aPlant;
		}
	}
}

Plant* Board::GetTopPlantAt(int theGridX, int theGridY, PlantPriority thePriority)
{
	if (theGridX < 0 || theGridX >= MAX_GRID_SIZE_X || theGridY < 0 || theGridY >= MAX_GRID_SIZE_Y)
		return nullptr;

	if (mApp->IsWallnutBowlingLevel() && !mCutScene->IsInShovelTutorial())
		return nullptr;

    PlantsOnLawn aPlantOnLawn;
    GetPlantsOnLawn(theGridX, theGridY, &aPlantOnLawn);

	switch (thePriority)
	{
	case PlantPriority::TOPPLANT_EATING_ORDER:
		if (aPlantOnLawn.mPumpkinPlant)							return aPlantOnLawn.mPumpkinPlant;
		else if (aPlantOnLawn.mNormalPlant)						return aPlantOnLawn.mNormalPlant;
		else													return aPlantOnLawn.mUnderPlant;
	case PlantPriority::TOPPLANT_DIGGING_ORDER:
		if (aPlantOnLawn.mNormalPlant)							return aPlantOnLawn.mNormalPlant;
		else													return aPlantOnLawn.mUnderPlant;
	case PlantPriority::TOPPLANT_BUNGEE_ORDER:
	case PlantPriority::TOPPLANT_CATAPULT_ORDER:
	case PlantPriority::TOPPLANT_ANY:
		if (aPlantOnLawn.mFlyingPlant)							return aPlantOnLawn.mFlyingPlant;
		else if (aPlantOnLawn.mNormalPlant)						return aPlantOnLawn.mNormalPlant;
		else if (aPlantOnLawn.mPumpkinPlant)					return aPlantOnLawn.mPumpkinPlant;
		else													return aPlantOnLawn.mUnderPlant;
	case PlantPriority::TOPPLANT_ZEN_TOOL_ORDER:
		if (aPlantOnLawn.mFlyingPlant)							return aPlantOnLawn.mFlyingPlant;
		else if (aPlantOnLawn.mPumpkinPlant)					return aPlantOnLawn.mPumpkinPlant;
		else if (aPlantOnLawn.mNormalPlant)						return aPlantOnLawn.mNormalPlant;
		else													return aPlantOnLawn.mUnderPlant;
	case PlantPriority::TOPPLANT_ONLY_NORMAL_POSITION:			return aPlantOnLawn.mNormalPlant;
	case PlantPriority::TOPPLANT_ONLY_FLYING:					return aPlantOnLawn.mFlyingPlant;
	case PlantPriority::TOPPLANT_ONLY_PUMPKIN:					return aPlantOnLawn.mPumpkinPlant;
	case PlantPriority::TOPPLANT_ONLY_UNDER_PLANT:				return aPlantOnLawn.mUnderPlant;
	default:													TOD_ASSERT();
	}
}

int Board::CountSunFlowers()
{
	int aCount = 0;
	Plant* aPlant = nullptr;
	while (IteratePlants(aPlant))
	{
		if (aPlant->MakesSun())
		{
			aCount++;
		}
	}
	return aCount;
}

int Board::CountPlantByType(SeedType theSeedType)
{
	int aCount = 0;
	Plant* aPlant = nullptr;
	while (IteratePlants(aPlant))
	{
		if (aPlant->mSeedType == theSeedType)
		{
			aCount++;
		}
	}
	return aCount;
}

int Board::CountEmptyPotsOrLilies(SeedType theSeedType)
{
	int aCount = 0;
	Plant* aPlant = nullptr;
	while (IteratePlants(aPlant))
	{
		if (aPlant->mSeedType == theSeedType && !GetTopPlantAt(aPlant->mPlantCol, aPlant->mRow, PlantPriority::TOPPLANT_ONLY_NORMAL_POSITION))
		{
			aCount++;
		}
	}
	return aCount;
}

bool Board::IsValidCobCannonSpotHelper(int theGridX, int theGridY)
{
	PlantsOnLawn aPlantOnLawn;
	GetPlantsOnLawn(theGridX, theGridY, &aPlantOnLawn);
	if (aPlantOnLawn.mPumpkinPlant)
		return false;

	if (aPlantOnLawn.mNormalPlant && aPlantOnLawn.mNormalPlant->mSeedType == SeedType::SEED_KERNELPULT)
		return true;

	return mApp->mEasyPlantingCheat && CanPlantAt(theGridX, theGridY, SeedType::SEED_KERNELPULT) == PlantingReason::PLANTING_OK;
}

bool Board::IsValidCobCannonSpot(int theGridX, int theGridY)
{
	if (!IsValidCobCannonSpotHelper(theGridX, theGridY) || !IsValidCobCannonSpotHelper(theGridX + 1, theGridY))
		return false;

	return !GetFlowerPotAt(theGridX, theGridY) == !GetFlowerPotAt(theGridX + 1, theGridY);
}

bool Board::HasValidCobCannonSpot()
{
	Plant* aPlant = nullptr;
	while (IteratePlants(aPlant))
	{
		if (aPlant->mSeedType == SeedType::SEED_KERNELPULT && IsValidCobCannonSpot(aPlant->mPlantCol, aPlant->mRow))
		{
			return true;
		}
	}
	return false;
}

Projectile* Board::AddProjectile(int theX, int theY, int theRenderOrder, int theRow, ProjectileType theProjectileType)
{
	Projectile* aProjectile = mProjectiles.DataArrayAlloc();
	aProjectile->ProjectileInitialize(theX, theY, theRenderOrder, theRow, theProjectileType);
	return aProjectile;
}

bool Board::CanZombieSpawnOnLevel(ZombieType theZombieType, int theLevel)
{
	const ZombieDefinition& aZombieDef = GetZombieDefinition(theZombieType);
	if (theZombieType == ZombieType::ZOMBIE_YETI)
	{
		return gLawnApp->CanSpawnYetis();
	}

	if (theLevel < aZombieDef.mStartingLevel || aZombieDef.mPickWeight == 0)
	{
		return false;
	}

	TOD_ASSERT(gZombieAllowedLevels[theZombieType].mZombieType == theZombieType);
	return gZombieAllowedLevels[theZombieType].mAllowedOnLevel[ClampInt(theLevel - 1, 0, NUM_LEVELS - 1)];
}

ZombieType Board::GetIntroducedZombieType()
{
	if (!mApp->IsAdventureMode() || mLevel == 1)
	{
		return ZombieType::ZOMBIE_INVALID;
	}

	for (ZombieType aZombieType = ZombieType::ZOMBIE_NORMAL; aZombieType < ZombieType::NUM_ZOMBIE_TYPES; aZombieType = (ZombieType)((int)aZombieType + 1))
	{
		const ZombieDefinition& aZombieDef = GetZombieDefinition(aZombieType);
		if ((aZombieType != ZombieType::ZOMBIE_YETI || mApp->CanSpawnYetis()) && aZombieDef.mStartingLevel == mLevel)
		{
			return aZombieType;
		}
	}
	return ZombieType::ZOMBIE_INVALID;
}

ZombieType Board::PickGraveRisingZombieType(int theZombiePoints)
{
	TodWeightedArray aZombieWeightArray[(int)ZombieType::NUM_ZOMBIE_TYPES];
	int aCount = 2;
	aZombieWeightArray[0].mItem = ZombieType::ZOMBIE_NORMAL;
	aZombieWeightArray[0].mWeight = GetZombieDefinition(ZombieType::ZOMBIE_NORMAL).mPickWeight;
	aZombieWeightArray[1].mItem = ZombieType::ZOMBIE_TRAFFIC_CONE;
	aZombieWeightArray[1].mWeight = GetZombieDefinition(ZombieType::ZOMBIE_TRAFFIC_CONE).mPickWeight;
	if (!StageHasGraveStones())
	{
		aZombieWeightArray[2].mItem = ZombieType::ZOMBIE_PAIL;
		aZombieWeightArray[2].mWeight = GetZombieDefinition(ZombieType::ZOMBIE_PAIL).mPickWeight;
		aCount++;
	}

	for (int i = 0; i < aCount; i++)
	{
		ZombieType aZombieType = (ZombieType)aZombieWeightArray[i].mItem;
		const ZombieDefinition& aZombieDef = GetZombieDefinition(aZombieType);
		if ((mApp->IsFirstTimeAdventureMode() && mLevel < aZombieDef.mStartingLevel) || (!mZombieAllowed[aZombieType] && aZombieType != ZombieType::ZOMBIE_NORMAL))
		{
			aZombieWeightArray[i].mWeight = 0;
		}
	}

	return (ZombieType)TodPickFromWeightedArray(aZombieWeightArray, aCount);
}

ZombieType Board::PickZombieType(int theZombiePoints, int theWaveIndex, ZombiePicker* theZombiePicker)
{
	int aPickCount = 0;
	TodWeightedArray aZombieWeightArray[ZombieType::NUM_ZOMBIE_TYPES];
	for (int aZombieType = ZombieType::ZOMBIE_NORMAL; aZombieType < ZombieType::NUM_ZOMBIE_TYPES; aZombieType++)
	{
		if (!mZombieAllowed[aZombieType])
			continue;

		const ZombieDefinition& aZombieDef = GetZombieDefinition((ZombieType)aZombieType);

		GameMode aGameMode = mApp->mGameMode;
		if (aZombieType == ZombieType::ZOMBIE_BUNGEE && mApp->IsSurvivalEndless(aGameMode))
		{
			if (!IsFlagWave(theWaveIndex))
			{
				continue;
			}
		}
		else if (aGameMode != GameMode::GAMEMODE_CHALLENGE_POGO_PARTY && aGameMode != GameMode::GAMEMODE_CHALLENGE_BOBSLED_BONANZA && aGameMode != GameMode::GAMEMODE_CHALLENGE_AIR_RAID)
		{
			int aFirstAllowedWave = aZombieDef.mFirstAllowedWave;
			if (mApp->IsSurvivalEndless(aGameMode))
			{
				int aFlags = GetSurvivalFlagsCompleted();
				int aAllowedWave = aFirstAllowedWave - TodAnimateCurve(18, 50, aFlags, 0, 15, TodCurves::CURVE_LINEAR);
				aFirstAllowedWave = max(aAllowedWave, 1);
			}
			if (theWaveIndex + 1 < aFirstAllowedWave || theZombiePoints < aZombieDef.mZombieValue)
			{
				continue;
			}
		}

		int aPickWeight = aZombieDef.mPickWeight;
		if (mApp->IsSurvivalMode())
		{
			int aFlags = GetSurvivalFlagsCompleted();
			if (aZombieType == ZombieType::ZOMBIE_GARGANTUAR || aZombieType == ZombieType::ZOMBIE_ZAMBONI)
			{
				if (theZombiePicker->mZombieTypeCount[aZombieType] >= TodAnimateCurve(10, 50, aFlags, 2, 50, TodCurves::CURVE_LINEAR))
				{
					continue;
				}
			}
			else if (aZombieType == ZombieType::ZOMBIE_REDEYE_GARGANTUAR)
			{
				if (IsFlagWave(theWaveIndex))
				{
					if (theZombiePicker->mZombieTypeCount[aZombieType] >= TodAnimateCurve(14, 100, aFlags, 1, 50, TodCurves::CURVE_LINEAR))
					{
						continue;
					}
				}
				else
				{
					if (theZombiePicker->mAllWavesZombieTypeCount[aZombieType] >= TodAnimateCurve(10, 110, aFlags, 1, 50, TodCurves::CURVE_LINEAR))
					{
						continue;
					}
					aPickWeight = 1000;
				}
			}
			else if (aZombieType == ZombieType::ZOMBIE_NORMAL)
			{
				aPickWeight = TodAnimateCurve(10, 50, aFlags, aPickWeight, aPickWeight / 10, TodCurves::CURVE_LINEAR);
			}
			else if (aZombieType == ZombieType::ZOMBIE_TRAFFIC_CONE)
			{
				aPickWeight = TodAnimateCurve(10, 50, aFlags, aPickWeight, aPickWeight / 4, TodCurves::CURVE_LINEAR);
			}
		}
		aZombieWeightArray[aPickCount].mItem = aZombieType;
		aZombieWeightArray[aPickCount].mWeight = aPickWeight;
		aPickCount++;
	}

	return (ZombieType)TodPickFromWeightedArray(aZombieWeightArray, aPickCount);
}

bool Board::IsZombieTypePoolOnly(ZombieType theZombieType)
{
	return (theZombieType == ZombieType::ZOMBIE_SNORKEL || theZombieType == ZombieType::ZOMBIE_DOLPHIN_RIDER);
}

bool Board::RowCanHaveZombieType(int theRow, ZombieType theZombieType)
{
	if (!RowCanHaveZombies(theRow))
	{
		return false;
	}

	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_RESODDED && mPlantRow[theRow] == PlantRowType::PLANTROW_DIRT && mCurrentWave < 5)
	{
		return false;  
	}
	if (mPlantRow[theRow] == PlantRowType::PLANTROW_POOL && !Zombie::ZombieTypeCanGoInPool(theZombieType))
	{
		return false;  
	}
	if (mPlantRow[theRow] == PlantRowType::PLANTROW_HIGH_GROUND && !Zombie::ZombieTypeCanGoOnHighGround(theZombieType))
	{
		return false;  
	}

	int aCurrentWave = mCurrentWave;
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND)
	{
		aCurrentWave += mChallenge->mSurvivalStage * GetNumWavesPerSurvivalStage();
	}
	if (mPlantRow[theRow] == PlantRowType::PLANTROW_POOL)
	{
		if (aCurrentWave < 5 && !IsZombieTypePoolOnly(theZombieType))
		{
			return false;
		}
	}
	else if (IsZombieTypePoolOnly(theZombieType))
	{
		return false;
	}
	if (theZombieType == ZOMBIE_BOBSLED && !mIceTimer[theRow])
	{
		return false;
	}
	if (theRow == 0 && !mApp->IsSurvivalMode())
	{
		if (theZombieType == ZombieType::ZOMBIE_GARGANTUAR || theZombieType == ZombieType::ZOMBIE_REDEYE_GARGANTUAR)
		{
			return false;
		}
	}
	if (theZombieType != ZombieType::ZOMBIE_DANCER || StageHasPool())
	{
		return true;
	}
	return RowCanHaveZombies(theRow - 1) && RowCanHaveZombies(theRow + 1);
}

int Board::PickRowForNewZombie(ZombieType theZombieType)
{
	GridItem* aRake = GetRake();
	if (aRake && aRake->mGridItemState == GridItemState::GRIDITEM_STATE_RAKE_ATTRACTING && RowCanHaveZombieType(aRake->mGridY, theZombieType))
	{
		aRake->mGridItemState = GridItemState::GRIDITEM_STATE_RAKE_WAITING;
		TodUpdateSmoothArrayPick(mRowPickingArray, MAX_GRID_SIZE_Y, aRake->mGridY);
		return aRake->mGridY;
	}

	for (int aRow = 0; aRow < MAX_GRID_SIZE_Y; aRow++)
	{
		if (!RowCanHaveZombieType(aRow, theZombieType))
		{
			mRowPickingArray[aRow].mWeight = 0;
		}
		else if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_PORTAL_COMBAT)
		{
			mRowPickingArray[aRow].mWeight = mChallenge->PortalCombatRowSpawnWeight(aRow);
		}
		else if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_INVISIGHOUL && mCurrentWave <= 3 && aRow == 5)
		{
			mRowPickingArray[aRow].mWeight = 0;
		}
		else
		{
			int aWavesMowered = mCurrentWave - mWaveRowGotLawnMowered[aRow];
			if (mApp->IsContinuousChallenge() && mCurrentWave == mNumWaves - 1)
			{
				aWavesMowered = 100;
			}

			if (aWavesMowered <= 1)
			{
				mRowPickingArray[aRow].mWeight = 0.01f;
			}
			else if (aWavesMowered <= 2)
			{
				mRowPickingArray[aRow].mWeight = 0.5f;
			}
			else
			{
				mRowPickingArray[aRow].mWeight = 1.0f;
			}
		}
	}

	// Some boards or challenge states can leave every eligible spawn row with zero weight.
	// Do not pass an empty weighted array to TodPickFromSmoothArray, which asserts in debug builds.
	float aTotalWeight = 0.0f;
	for (int aRow = 0; aRow < MAX_GRID_SIZE_Y; aRow++)
	{
		aTotalWeight += mRowPickingArray[aRow].mWeight;
	}
	if (aTotalWeight <= 0.0f)
	{
		return -1;
	}

	return TodPickFromSmoothArray(mRowPickingArray, MAX_GRID_SIZE_Y);
}

bool Board::CanAddBobSled()
{
	for (int aRow = 0; aRow < MAX_GRID_SIZE_Y; aRow++)
	{
		if (mIceTimer[aRow] > 0 && mIceMinX[aRow] < 700 + BOARD_ADDITIONAL_WIDTH)
		{
			return true;
		}
	}
	return false;
}

Zombie* Board::AddZombieInRow(ZombieType theZombieType, int theRow, int theFromWave, bool theSkipBushRustling)
{
	if (mZombies.mSize + (theZombieType == ZombieType::ZOMBIE_BOBSLED ? 4U : 1U) > mZombies.mMaxSize)
	{
		TodTrace("Too many zombies!!");
		return nullptr;
	}
	if (theZombieType == ZombieType::ZOMBIE_YETI && !mApp->mPlayingQuickplay)
		mApp->GetAchievement(ACHIEVEMENT_ZOMBOLOGIST);
	bool aVariant = !Rand(5);
	Zombie* aZombie = mZombies.DataArrayAlloc();
	aZombie->ZombieInitialize(theRow, theZombieType, aVariant, nullptr, theFromWave, !theSkipBushRustling && StageHasBushes() && mApp->mGameScene == GameScenes::SCENE_PLAYING);
	if (theZombieType == ZombieType::ZOMBIE_BOBSLED && aZombie->IsOnBoard())
	{
		for (int _i = 0; _i < 3; _i++)
		{
			mZombies.DataArrayAlloc()->ZombieInitialize(theRow, ZombieType::ZOMBIE_BOBSLED, false, aZombie, theFromWave);
		}
	}
	return aZombie;
}

Zombie* Board::AddZombie(ZombieType theZombieType, int theFromWave, bool skipBushAnimation)
{
	int aRow = PickRowForNewZombie(theZombieType);
	if (aRow < 0)
	{
		return nullptr;
	}
	return AddZombieInRow(theZombieType, aRow, theFromWave, skipBushAnimation);
}

Bush* Board::AddBush(int theRow)
{
	if (mBushes.mSize >= mBushes.mMaxSize)
	{
		TodTrace("Too many bushes!!");
		return nullptr;
	}
	if (mBushList[theRow])
		return nullptr;
	Bush* aBush = mBushes.DataArrayAlloc();
	aBush->BushInitialize(theRow);
	mBushList[theRow] = aBush;
	return aBush;
}

void Board::RustleBush(int mRow) 
{
	if (!StageHasBushes() || mApp->mGameMode == GAMEMODE_CHALLENGE_INVISIGHOUL)
		return;
	Bush* aBush = mBushList[mRow];
	if (aBush == nullptr)
		return;
	aBush->Rustle();
	if (!StageHas6Rows() && mRow == 4)
	{
		aBush = mBushList[mRow + 1];
		aBush->Rustle();
	}
}
GamepadPlayer* Board::AddGamepadPlayer(int theIndex)
{
	if (mGamepadPlayers.mSize >= mGamepadPlayers.mMaxSize)
	{
		TodTrace("Too many gamepad players!!");
		return nullptr;
	}
	if (mGamepadPlayerList[theIndex])
		return nullptr;
	GamepadPlayer* aGamepadPlayer = mGamepadPlayers.DataArrayAlloc();
	aGamepadPlayer->GamepadPlayerInitialize(theIndex);
	mGamepadPlayerList[theIndex] = aGamepadPlayer;
	return aGamepadPlayer;
}
void Board::RemoveAllZombies()
{
	Zombie* aZombie = nullptr;
	while (IterateZombies(aZombie))
	{
		if (!aZombie->IsDeadOrDying())
		{
			aZombie->DieNoLoot();
		}
	}
}

void Board::RemoveZombiesForRepick()
{
	Zombie* aZombie = nullptr;
	while (IterateZombies(aZombie))
	{
		if (!aZombie->IsDeadOrDying() && aZombie->mMindControlled && aZombie->mPosX > BOARD_WIDTH - 80)
		{
			aZombie->DieNoLoot();
		}
	}
}

void Board::RemoveCutsceneZombies()
{
	Zombie* aZombie = nullptr;
	while (IterateZombies(aZombie))
	{
		if (aZombie->mFromWave == Zombie::ZOMBIE_WAVE_CUTSCENE)
		{
			aZombie->DieNoLoot();
		}
	}
}

bool Board::IsIceAt(int theGridX, int theGridY)
{
	TOD_ASSERT(theGridY >= 0 && theGridY < MAX_GRID_SIZE_Y);
	if (mIceTimer[theGridY] == 0 || mIceMinX[theGridY] > 750 + BOARD_ADDITIONAL_WIDTH)
		return false;

	return theGridX >= PixelToGridXKeepOnBoard(mIceMinX[theGridY] + 12, 0);
}

PlantingReason Board::CanPlantAt(int theGridX, int theGridY, SeedType theSeedType)
{
	if (theGridX < 0 || theGridX >= MAX_GRID_SIZE_X || theGridY < 0 || theGridY >= MAX_GRID_SIZE_Y)
	{
		return PlantingReason::PLANTING_NOT_HERE;
	}

    if (theSeedType == SeedType::SEED_GRAVEBUSTER && IsRollSeedBank() &&
        GetGraveStoneAt(theGridX, theGridY) != nullptr)
    {
        PlantsOnLawn plants;
        GetPlantsOnLawn(theGridX, theGridY, &plants);
        return plants.mNormalPlant ? PlantingReason::PLANTING_NOT_HERE : PlantingReason::PLANTING_OK;
    }
	PlantingReason aReason = mChallenge->CanPlantAt(theGridX, theGridY, theSeedType);
	if (aReason != PlantingReason::PLANTING_OK || Challenge::IsZombieSeedType(theSeedType))
	{
		return aReason;
	}

	PlantsOnLawn aPlantOnLawn;
	GetPlantsOnLawn(theGridX, theGridY, &aPlantOnLawn);
	// Roll Shop limits all planted Sun producers to five combined.
	// Twin Sunflower upgrades an existing Sunflower, so it does not
	// increase the number of producer plants occupying the lawn.
	if (IsRollSeedBank() && CountSunPlants() >= ROLL_MAX_SUN_PLANTS &&
		(theSeedType == SeedType::SEED_SUNFLOWER ||
		 theSeedType == SeedType::SEED_SUNSHROOM ||
		 theSeedType == SeedType::SEED_TWINSUNFLOWER))
	{
		const bool aReplacesProducer =
			theSeedType == SeedType::SEED_TWINSUNFLOWER &&
			aPlantOnLawn.mNormalPlant != nullptr &&
			aPlantOnLawn.mNormalPlant->mSeedType == SeedType::SEED_SUNFLOWER &&
			aPlantOnLawn.mNormalPlant->IsUpgradableTo(theSeedType);
		if (!aReplacesProducer)
			return PlantingReason::PLANTING_NOT_HERE;
	}

	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
	{
		if (aPlantOnLawn.mUnderPlant || aPlantOnLawn.mPumpkinPlant || aPlantOnLawn.mFlyingPlant || aPlantOnLawn.mNormalPlant)
		{
			return PlantingReason::PLANTING_NOT_HERE;
		}
		if (mApp->mZenGarden->mGardenType == GARDEN_AQUARIUM && !Plant::IsAquatic(theSeedType))
		{
			return PlantingReason::PLANTING_NOT_ON_WATER;
		}

		return PlantingReason::PLANTING_OK;
	}

	bool aHasGrave = GetGraveStoneAt(theGridX, theGridY);
	if (theSeedType == SeedType::SEED_GRAVEBUSTER)
	{
		if (aPlantOnLawn.mNormalPlant)
		{
			return PlantingReason::PLANTING_NOT_HERE;
		}

		return aHasGrave ? PlantingReason::PLANTING_OK : PlantingReason::PLANTING_ONLY_ON_GRAVES;
	}
	if (theSeedType == SeedType::SEED_INSTANT_COFFEE)
	{
		if (aPlantOnLawn.mFlyingPlant)
		{
			return PlantingReason::PLANTING_NOT_HERE;
		}

		if (!aPlantOnLawn.mNormalPlant || !aPlantOnLawn.mNormalPlant->mIsAsleep || aPlantOnLawn.mNormalPlant->mWakeUpCounter > 0 ||
			aPlantOnLawn.mNormalPlant->mOnBungeeState == PlantOnBungeeState::GETTING_GRABBED_BY_BUNGEE)
		{
			return PlantingReason::PLANTING_NEEDS_SLEEPING;
		}

		return PlantingReason::PLANTING_OK;
	}
	if (aHasGrave)
	{
		return Plant::IsFlying(theSeedType) ? PlantingReason::PLANTING_OK : PlantingReason::PLANTING_NOT_ON_GRAVE;
	}
	
	Plant* aUnderPlant = aPlantOnLawn.mUnderPlant;
	bool aHasLilypad, aHasFlowerPot;
	if (!aUnderPlant || aUnderPlant->mOnBungeeState == PlantOnBungeeState::GETTING_GRABBED_BY_BUNGEE)
	{
		aHasLilypad = false;
		aHasFlowerPot = false;
	}
	else
	{
		aHasLilypad = aUnderPlant->mSeedType == SeedType::SEED_LILYPAD;
		aHasFlowerPot = aUnderPlant->mSeedType == SeedType::SEED_FLOWERPOT;
	}
	if (GetCraterAt(theGridX, theGridY))
	{
		return PlantingReason::PLANTING_NOT_ON_CRATER;
	}
	if (GetScaryPotAt(theGridX, theGridY) || IsIceAt(theGridX, theGridY))
	{
		return PlantingReason::PLANTING_NOT_HERE;
	}
	GridSquareType aGridSquare = mGridSquareType[theGridX][theGridY];
	if (aGridSquare == GridSquareType::GRIDSQUARE_DIRT || aGridSquare == GridSquareType::GRIDSQUARE_NONE)
	{
		return PlantingReason::PLANTING_NOT_HERE;
	}
	Plant* aNormalPlant = aPlantOnLawn.mNormalPlant;
	if (theSeedType == SeedType::SEED_LILYPAD || theSeedType == SeedType::SEED_TANGLEKELP || theSeedType == SeedType::SEED_SEASHROOM)
	{
		if (!IsPoolSquare(theGridX, theGridY))
		{
			return PlantingReason::PLANTING_ONLY_IN_POOL;
		}

		return (aNormalPlant || aUnderPlant) ? PlantingReason::PLANTING_NOT_HERE : PlantingReason::PLANTING_OK;
	}
	if (Plant::IsFlying(theSeedType))
	{
		return aPlantOnLawn.mFlyingPlant ? PlantingReason::PLANTING_NOT_HERE : PlantingReason::PLANTING_OK;
	}
	if (theSeedType == SeedType::SEED_SPIKEWEED || theSeedType == SeedType::SEED_SPIKEROCK)
	{
		if (aGridSquare == GridSquareType::GRIDSQUARE_POOL || StageHasRoof() || aUnderPlant)
		{
			return PlantingReason::PLANTING_NEEDS_GROUND;
		}
	}
	Plant* aPumpkinPlant = aPlantOnLawn.mPumpkinPlant;
	if (aGridSquare == GridSquareType::GRIDSQUARE_POOL && !aHasLilypad && theSeedType != SeedType::SEED_CATTAIL)
	{
		if (!aNormalPlant || aNormalPlant->mSeedType != SeedType::SEED_CATTAIL || theSeedType != SeedType::SEED_PUMPKINSHELL)
		{
			return PlantingReason::PLANTING_NOT_ON_WATER;
		}
	}
	if (theSeedType == SeedType::SEED_FLOWERPOT)
	{
		return (aNormalPlant || aUnderPlant || aPumpkinPlant) ? PlantingReason::PLANTING_NOT_HERE : PlantingReason::PLANTING_OK;
	}
	if (StageHasRoof() && !aHasFlowerPot)
	{
		return PlantingReason::PLANTING_NEEDS_POT;
	}
	bool aAidPurchased = mApp->mPlayerInfo->mPurchases[StoreItem::STORE_ITEM_FIRSTAID] > 0;
	if (theSeedType == SeedType::SEED_PUMPKINSHELL)
	{
		if (aNormalPlant && aNormalPlant->mSeedType == SeedType::SEED_COBCANNON)
		{
			return PlantingReason::PLANTING_NOT_HERE;
		}
		if (!aPumpkinPlant)
		{
			return PlantingReason::PLANTING_OK;
		}
		if (aAidPurchased && aPumpkinPlant->mPlantHealth < aPumpkinPlant->mPlantMaxHealth * 2 / 3 &&
			aPumpkinPlant->mSeedType == SeedType::SEED_PUMPKINSHELL && aPumpkinPlant->mOnBungeeState != PlantOnBungeeState::GETTING_GRABBED_BY_BUNGEE)
		{
			return PlantingReason::PLANTING_OK;
		}

		return PlantingReason::PLANTING_NOT_HERE;
	}
	if (aHasLilypad && theSeedType == SeedType::SEED_POTATOMINE)
	{
		return PlantingReason::PLANTING_ONLY_ON_GROUND;
	}

	if (aUnderPlant)
	{
		if (theSeedType == SeedType::SEED_CATTAIL)
		{
			if (aNormalPlant)
			{
				return PlantingReason::PLANTING_NOT_HERE;
			}
			if (aUnderPlant->IsUpgradableTo(theSeedType) && aUnderPlant->mOnBungeeState != PlantOnBungeeState::GETTING_GRABBED_BY_BUNGEE)
			{
				return PlantingReason::PLANTING_OK;
			}
			if (Plant::IsUpgrade(theSeedType))
			{
				return PlantingReason::PLANTING_NEEDS_UPGRADE;
			}
		}
		else
		{
			if (aUnderPlant->mSeedType == SeedType::SEED_IMITATER)
			{
				return PlantingReason::PLANTING_NOT_HERE;
			}
		}
	}

	if (aNormalPlant)
	{
		if (aNormalPlant->IsUpgradableTo(theSeedType) && aNormalPlant->mOnBungeeState != PlantOnBungeeState::GETTING_GRABBED_BY_BUNGEE)
		{
			return PlantingReason::PLANTING_OK;
		}
		if (Plant::IsUpgrade(theSeedType))
		{
			return PlantingReason::PLANTING_NEEDS_UPGRADE;
		}

		if ((theSeedType == SeedType::SEED_WALLNUT || theSeedType == SeedType::SEED_TALLNUT) && aAidPurchased)
		{
			if (aNormalPlant->mPlantHealth < aNormalPlant->mPlantMaxHealth * 2 / 3 &&
				aNormalPlant->mSeedType == theSeedType && aNormalPlant->mOnBungeeState != PlantOnBungeeState::GETTING_GRABBED_BY_BUNGEE)
			{
				return PlantingReason::PLANTING_OK;
			}
		}

		return PlantingReason::PLANTING_NOT_HERE;
	}

	if (!mApp->mEasyPlantingCheat && Plant::IsUpgrade(theSeedType))
	{
		return PlantingReason::PLANTING_NEEDS_UPGRADE;
	}
	if (theSeedType == SeedType::SEED_COBCANNON && !IsValidCobCannonSpot(theGridX, theGridY))
	{
		return PlantingReason::PLANTING_NEEDS_UPGRADE;
	}
	else if (theSeedType == SeedType::SEED_CATTAIL && aGridSquare != GridSquareType::GRIDSQUARE_POOL)
	{
		return PlantingReason::PLANTING_NOT_HERE;
	}

	return PlantingReason::PLANTING_OK;
}

void Board::UpdateCursor()
{
	int aMouseX = mApp->mWidgetManager->mLastMouseX - mX;
	int aMouseY = mApp->mWidgetManager->mLastMouseY - mY;
	bool aShowFinger = false;
	bool aShowDrag = false;
	bool aHideCursor = false;

	if (mApp->mSeedChooserScreen && mApp->mSeedChooserScreen->Contains(aMouseX + mX, aMouseY + mY))
		return;

	if (mApp->GetDialogCount() > 0)
		return;

	if (mPaused || mBoardFadeOutCounter >= 0 || mTimeStopCounter > 0 || mApp->mGameScene == GameScenes::SCENE_ZOMBIES_WON)
	{
		mApp->SetCursor(Sexy::CURSOR_POINTER);
		return;
	}

	HitResult aHitResult;
	MouseHitTest(aMouseX, aMouseY, &aHitResult);
	switch (aHitResult.mObjectType)
	{
	case GameObjectType::OBJECT_TYPE_MENU_BUTTON:
	case GameObjectType::OBJECT_TYPE_STORE_BUTTON:
	case GameObjectType::OBJECT_TYPE_SHOVEL:
	case GameObjectType::OBJECT_TYPE_WATERING_CAN:
	case GameObjectType::OBJECT_TYPE_FERTILIZER:
	case GameObjectType::OBJECT_TYPE_BUG_SPRAY:
	case GameObjectType::OBJECT_TYPE_PHONOGRAPH:
	case GameObjectType::OBJECT_TYPE_CHOCOLATE:
	case GameObjectType::OBJECT_TYPE_GLOVE:
	case GameObjectType::OBJECT_TYPE_MONEY_SIGN:
	case GameObjectType::OBJECT_TYPE_NEXT_GARDEN:
	case GameObjectType::OBJECT_TYPE_WHEELBARROW:
	case GameObjectType::OBJECT_TYPE_SLOT_MACHINE_HANDLE:
	case GameObjectType::OBJECT_TYPE_TREE_FOOD:
	case GameObjectType::OBJECT_TYPE_STINKY:
	case GameObjectType::OBJECT_TYPE_TREE_OF_WISDOM:
	case GameObjectType::OBJECT_TYPE_COIN:
	case GameObjectType::OBJECT_TYPE_FASTMODE_BUTTON:
	case GameObjectType::OBJECT_TYPE_PROJECTILE:
		aShowFinger = true;
		break;

	case GameObjectType::OBJECT_TYPE_SEEDPACKET:
		aShowFinger = ((SeedPacket*)aHitResult.mObject)->CanPickUp();
		break;

	case GameObjectType::OBJECT_TYPE_SCARY_POT:
		if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_NORMAL)
		{
			aShowFinger = true;
		}
		else if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_HAMMER)
		{
			aHideCursor = true;
		}
		break;

	case GameObjectType::OBJECT_TYPE_PLANT:
		if ((mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST) && !HasLevelAwardDropped())
		{
			aShowFinger = true;
		}
		if (((Plant*)aHitResult.mObject)->mState == PlantState::STATE_COBCANNON_READY)
		{
			aShowFinger = true;
		}
		break;

	default:
		if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_HAMMER)
		{
			aHideCursor = true;
		}
		break;
	}

	if (mChallenge->mBeghouledMouseCapture || aShowDrag)
	{
		mApp->SetCursor(Sexy::CURSOR_DRAGGING);
	}
	else if (aShowFinger)
	{
		mApp->SetCursor(Sexy::CURSOR_HAND);
	}
	else if (aHideCursor)
	{
		mApp->SetCursor(Sexy::CURSOR_NONE);
	}
	else
	{
		mApp->SetCursor(Sexy::CURSOR_POINTER);
	}
}

void Board::MouseMove(int x, int y)
{
	Widget::MouseMove(x, y);
	mChallenge->MouseMove(x, y);
}

void Board::MouseDrag(int x, int y)
{
	Widget::MouseDrag(x, y);
	mChallenge->MouseMove(x, y);
}

bool Board::IsPlantInGoldWateringCanRange(int theMouseX, int theMouseY, Plant* thePlant)
{
	/*
	Rect aRect = Rect(theMouseX - 70, theMouseY - 80, 160, 160);
	if (GetTopPlantAt(thePlant->mPlantCol, thePlant->mRow, PlantPriority::TOPPLANT_ZEN_TOOL_ORDER) == thePlant)
	{
		return aRect.Contains(thePlant->mX + 40, thePlant->mY + 40);
	}
	return false;
	*/

	int aMinX = theMouseX - 70;
	int aMaxX = theMouseX + 90;
	int aMinY = theMouseY - 80;
	int aMaxY = theMouseY + 80;

	if (GetTopPlantAt(thePlant->mPlantCol, thePlant->mRow, PlantPriority::TOPPLANT_ZEN_TOOL_ORDER) == thePlant)
	{
		return thePlant->mX + 40 >= aMinX && thePlant->mX + 40 < aMaxX && thePlant->mY + 40 >= aMinY && thePlant->mY + 40 < aMaxY;
	}
	return false;
}

void Board::HighlightPlantsForMouse(int theMouseX, int theMouseY)
{
	if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_WATERING_CAN && mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_GOLD_WATERINGCAN])
	{
		Plant* aPlant = nullptr;
		while (IteratePlants(aPlant))
		{
			if (IsPlantInGoldWateringCanRange(theMouseX, theMouseY, aPlant))
			{
				aPlant->mHighlighted = true;
				Plant* aFlowerPot = GetTopPlantAt(aPlant->mPlantCol, aPlant->mRow, PlantPriority::TOPPLANT_ONLY_UNDER_PLANT);
				if (aFlowerPot)
				{
					aFlowerPot->mHighlighted = true;
				}
			}
		}
	}
	else
	{
		Plant* aPlant = ToolHitTest(theMouseX, theMouseY);
		if (aPlant)
		{
			aPlant->mHighlighted = true;
			if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
			{
				Plant* aFlowerPot = GetTopPlantAt(aPlant->mPlantCol, aPlant->mRow, PlantPriority::TOPPLANT_ONLY_UNDER_PLANT);
				if (aFlowerPot)
				{
					aFlowerPot->mHighlighted = true;
				}
			}
		}
	}
}

void Board::UpdateMousePosition()
{
	UpdateCursor();
	UpdateToolTip();
	Plant* aPlant = nullptr;
	while (IteratePlants(aPlant))
	{
		aPlant->mHighlighted = false;
	}

	SeedType aCursorSeedType = GetSeedTypeInCursor();
	int aMouseX = mApp->mWidgetManager->mLastMouseX - mX;
	int aMouseY = mApp->mWidgetManager->mLastMouseY - mY;

	if (mApp->IsScaryPotterLevel())
	{
		GridItem* aGridItem = nullptr;
		while (IterateGridItems(aGridItem))
		{
			if (aGridItem->mGridItemType == GridItemType::GRIDITEM_SCARY_POT)
			{
				aGridItem->mHighlighted = false;
			}
		}

		HitResult aHitResult;
		MouseHitTest(aMouseX, aMouseY, &aHitResult);
		if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_SCARY_POT)
		{
			GridItem* aScaryPot = (GridItem*)aHitResult.mObject;
			aScaryPot->mHighlighted = true;
			return;
		}
	}

	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
	{
		GridItem* aStinky = mApp->mZenGarden->GetStinky();
		if (aStinky)
		{
			HitResult aHitResult;
			MouseHitTest(aMouseX, aMouseY, &aHitResult);
			aStinky->mHighlighted = aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_STINKY;
		}
	}

	if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_SHOVEL || 
		mCursorObject->mCursorType == CursorType::CURSOR_TYPE_WATERING_CAN || 
		mCursorObject->mCursorType == CursorType::CURSOR_TYPE_FERTILIZER ||
		mCursorObject->mCursorType == CursorType::CURSOR_TYPE_BUG_SPRAY || 
		mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PHONOGRAPH || 
		mCursorObject->mCursorType == CursorType::CURSOR_TYPE_CHOCOLATE ||
		mCursorObject->mCursorType == CursorType::CURSOR_TYPE_GLOVE || 
		mCursorObject->mCursorType == CursorType::CURSOR_TYPE_MONEY_SIGN ||
		(mCursorObject->mCursorType == CursorType::CURSOR_TYPE_WHEEELBARROW && !mApp->mZenGarden->GetPottedPlantInWheelbarrow()))
	{
		HighlightPlantsForMouse(aMouseX, aMouseY);
		return;
	}

	if (aCursorSeedType == SeedType::SEED_INSTANT_COFFEE)
	{
		int aGridX = PlantingPixelToGridX(aMouseX, aMouseY, aCursorSeedType);
		int aGridY = PlantingPixelToGridY(aMouseX, aMouseY, aCursorSeedType);

		Plant* aPlant = GetTopPlantAt(aGridX, aGridY, PlantPriority::TOPPLANT_ONLY_NORMAL_POSITION);
		if (aPlant && aPlant->mIsAsleep && CanPlantAt(aGridX, aGridY, SeedType::SEED_INSTANT_COFFEE) == PlantingReason::PLANTING_OK)
		{
			aPlant->mHighlighted = true;
		}
	}
	else if (aCursorSeedType == SeedType::SEED_WALLNUT || aCursorSeedType == SeedType::SEED_TALLNUT)
	{
		int aGridX = PlantingPixelToGridX(aMouseX, aMouseY, aCursorSeedType);
		int aGridY = PlantingPixelToGridY(aMouseX, aMouseY, aCursorSeedType);

		Plant* aPlant = GetTopPlantAt(aGridX, aGridY, PlantPriority::TOPPLANT_ONLY_PUMPKIN);
		if (aPlant && aPlant->mSeedType == aCursorSeedType && CanPlantAt(aGridX, aGridY, aCursorSeedType) == PlantingReason::PLANTING_OK)
		{
			aPlant->mHighlighted = true;
		}
	}
	else if (aCursorSeedType == SeedType::SEED_PUMPKINSHELL)
	{
		int aGridX = PlantingPixelToGridX(aMouseX, aMouseY, aCursorSeedType);
		int aGridY = PlantingPixelToGridY(aMouseX, aMouseY, aCursorSeedType);

		Plant* aPlant = GetTopPlantAt(aGridX, aGridY, PlantPriority::TOPPLANT_ONLY_NORMAL_POSITION);
		if (aPlant && aPlant->mSeedType == SeedType::SEED_PUMPKINSHELL && CanPlantAt(aGridX, aGridY, SeedType::SEED_PUMPKINSHELL) == PlantingReason::PLANTING_OK)
		{
			aPlant->mHighlighted = true;
		}
	}
}

void Board::UpdateToolTip()
{
	if (!mApp->mWidgetManager->mMouseIn || !mApp->mActive || mTimeStopCounter > 0 || mApp->GetDialogCount() > 0 || mApp->mGameScene == GameScenes::SCENE_ZOMBIES_WON || mApp->mGameScene != GameScenes::SCENE_PLAYING)
	{
		mToolTip->mVisible = false;
        mActiveAugmentHoverStartTick = 0;
        mActiveAugmentTooltipVisible = false;
		return;
	}

	int aMouseX = mApp->mWidgetManager->mLastMouseX - mX;
	int aMouseY = mApp->mWidgetManager->mLastMouseY - mY;

	if (!CanInteractWithBoardButtons())
	{
		mToolTip->mVisible = false;
        mActiveAugmentHoverStartTick = 0;
        mActiveAugmentTooltipVisible = false;
		return;
	}

	mToolTip->SetTitle(_S(""));
	mToolTip->SetLabel(_S(""));
	mToolTip->SetWarningText(_S(""));
	mToolTip->mCenter = false;
    int aMenuButtonX = mMenuButton != nullptr ? mMenuButton->mX : BOARD_WIDTH - 160;
    mActiveAugmentHoverStartTick = 0;
    mActiveAugmentTooltipVisible = false;

    // An empty Hologram is intentionally excluded from SeedPacket::MouseHitTest.
    // While it is cooling down, provide a hover tooltip here without making the
    // empty packet clickable or sending SEED_NONE into normal seed-cost handling.
    if (mHologramEnabled && mHologramLastPlant == SeedType::SEED_NONE && mHologramCooldown > 0)
    {
        Rect aHologramRect(BOARD_WIDTH - 260, BOARD_HEIGHT - 135, SEED_PACKET_WIDTH, SEED_PACKET_HEIGHT);
        if (aHologramRect.Contains(aMouseX, aMouseY))
        {
            mToolTip->SetTitle(_S("Hologram"));
            mToolTip->SetLabel(_S(""));
            mToolTip->SetWarningText(_S("[WAITING_FOR_SEED]"));
            mToolTip->mX = aHologramRect.mX + (SEED_PACKET_WIDTH - mToolTip->mWidth) / 2;
            mToolTip->mY = aHologramRect.mY - mToolTip->mHeight - 5;
            mToolTip->mCenter = false;
            mToolTip->mVisible = true;
            return;
        }
    }
	if (mChallenge->UpdateToolTip(aMouseX, aMouseY))
	{
		return;
	}

	if (CanUseRollButton() && IsOverRollButton(aMouseX, aMouseY))
	{
		mToolTip->SetTitle(_S("Reroll Shop"));
		int aRollCost = GetRollCost();
		SexyString aRollLabel = aRollCost == 0 ? StrFormat(_S("Free reroll (x%d) [D]"), mFreeRolls) : StrFormat(_S("Costs %d Sun [D]"), aRollCost);
		mToolTip->SetLabel(aRollLabel + _S("\nRight-click a slot to lock it"));
		if (aRollCost > 0 && !CanTakeSunMoney(aRollCost))
		{
			mToolTip->SetWarningText(_S("[NOT_ENOUGH_SUN]"));
		}
		Rect aRollButtonRect = GetRollButtonRect();
		mToolTip->mX = aRollButtonRect.mX;
		mToolTip->mY = aRollButtonRect.mY + aRollButtonRect.mHeight + 4;
		mToolTip->mVisible = true;
		return;
	}

	HitResult aHitResult;
	MouseHitTest(aMouseX, aMouseY, &aHitResult);

	if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_SHOVEL)
	{
        mToolTip->SetLabel(_S("[SHOVEL_TOOLTIP]"));
        if (IsRollToolRecharging(GameObjectType::OBJECT_TYPE_SHOVEL))
            mToolTip->SetWarningText(_S("Recharging..."));
		Rect aShovelButtonRect = GetShovelButtonRect();
		mToolTip->mX = aShovelButtonRect.mX + 35;
		mToolTip->mY = aShovelButtonRect.mY + 72;
		mToolTip->mCenter = true;
		mToolTip->mVisible = true;
		return;
	}

	if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_NEXT_GARDEN)
	{
		mToolTip->SetLabel(_S("[NEXT_GARDEN_TOOLTIP]"));
		Rect aButtonRect = GetShovelButtonRect();
		mToolTip->mX = 599 + BOARD_ADDITIONAL_WIDTH;
		mToolTip->mY = aButtonRect.mY + 52;
		mToolTip->mCenter = true;
		mToolTip->mVisible = true;
		return;
	}

	if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_WATERING_CAN ||
		aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_FERTILIZER ||
		aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_BUG_SPRAY ||
		aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_PHONOGRAPH ||
		aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_CHOCOLATE ||
		aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_GLOVE ||
		aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_MONEY_SIGN ||
		aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_WHEELBARROW ||
		aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_TREE_FOOD)
	{
		if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_WATERING_CAN)
		{
			mToolTip->SetLabel(_S("[WATERING_CAN_TOOLTIP]"));
		}
		else if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_FERTILIZER)
		{
			mToolTip->SetLabel(_S("[FERTILIZER_TOOLTIP]"));
		}
		else if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_BUG_SPRAY)
		{
			mToolTip->SetLabel(_S("[BUG_SPRAY_TOOLTIP]"));
		}
		else if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_PHONOGRAPH)
		{
			mToolTip->SetLabel(_S("[PHONOGRAPH_TOOLTIP]"));
		}
		else if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_CHOCOLATE)
		{
			mToolTip->SetLabel(_S("[CHOCOLATE_TOOLTIP]"));
		}
		else if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_GLOVE)
		{
            mToolTip->SetLabel(_S("[GLOVE_TOOLTIP]"));
            if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_GLOVE &&
                IsRollToolRecharging(GameObjectType::OBJECT_TYPE_GLOVE))
                mToolTip->SetWarningText(_S("Recharging..."));
		}
		else if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_MONEY_SIGN)
		{
			mToolTip->SetLabel(_S("[MONEY_SIGN_TOOLTIP]"));
		}
		else if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_WHEELBARROW)
		{
			mToolTip->SetLabel(_S("[WHEELBARROW_TOOLTIP]"));
		}
		else if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_TREE_FOOD)
		{
			mToolTip->SetLabel(_S("[TREE_FERTILIZER_TOOLTIP]"));
		}

		Rect aButtonRect = GetShovelButtonRect();
        const bool aRollGlove = aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_GLOVE &&
            IsRollSeedBank() && mGardenGloveEnabled;
        if (aRollGlove)
            aButtonRect = GetGloveButtonRect();
        else
            GetZenButtonRect(aHitResult.mObjectType, aButtonRect);
        // Position the Roll Glove tooltip directly below its real hit box.
		this->mToolTip->mX = aRollGlove ? aButtonRect.mX + aButtonRect.mWidth / 2 : aButtonRect.mX + 35;
		this->mToolTip->mY = aRollGlove ? aButtonRect.mY + aButtonRect.mHeight + 2 : aButtonRect.mY + 72;
		this->mToolTip->mCenter = true;
		this->mToolTip->mVisible = true;
		return;
	}

	if (aHitResult.mObjectType != GameObjectType::OBJECT_TYPE_SEEDPACKET)
	{
		mToolTip->mVisible = false;
		return;
	}

	SeedPacket* aSeedPacket = (SeedPacket*)aHitResult.mObject;
	SeedType aUseSeedType = aSeedPacket->mPacketType;
	if (aSeedPacket->mPacketType == SeedType::SEED_IMITATER && aSeedPacket->mImitaterType != SeedType::SEED_NONE)
	{
		aUseSeedType = aSeedPacket->mImitaterType;
	}

	if (gLawnApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED || gLawnApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST)
	{
		if (aUseSeedType == SeedType::SEED_REPEATER)
		{
			mToolTip->SetLabel(_S("[BEGHOULED_REPEATER_UPGRADE_TOOLTIP]"));
		}
		else if (aUseSeedType == SeedType::SEED_FUMESHROOM)
		{
			mToolTip->SetLabel(_S("[BEGHOULED_FUMESHROOM_UPGRADE_TOOLTIP]"));
		}
		else if (aUseSeedType == SeedType::SEED_TALLNUT)
		{
			mToolTip->SetLabel(_S("[BEGHOULED_TALLNUT_UPGRADE_TOOLTIP]"));
		}
		else if (aUseSeedType == SeedType::SEED_BEGHOULED_BUTTON_SHUFFLE)
		{
			mToolTip->SetLabel(_S("[BEGHOULED_SHUFFLE_TOOLTIP]"));
		}
		else if (aUseSeedType == SeedType::SEED_BEGHOULED_BUTTON_CRATER)
		{
			mToolTip->SetLabel(_S("[BEGHOULED_CRATER_TOOLTIP]"));
		}
	}
	else if (aUseSeedType == SeedType::SEED_SLOT_MACHINE_SUN)
	{
		mToolTip->SetLabel(_S("[SLOT_MACHINE_SUN_TOOLTIP]"));
	}
	else if (aUseSeedType == SeedType::SEED_SLOT_MACHINE_DIAMOND)
	{
		mToolTip->SetLabel(_S("[SLOT_MACHINE_DIAMOND_TOOLTIP]"));
	}
	else if (aUseSeedType == SeedType::SEED_ZOMBIQUARIUM_SNORKLE)
	{
		mToolTip->SetLabel(_S("[ZOMBIQUARIUM_SNORKEL_TOOLTIP]"));
	}
	else if (aUseSeedType == SeedType::SEED_ZOMBIQUARIUM_TROPHY)
	{
		mToolTip->SetLabel(_S("[ZOMBIQUARIUM_TROPHY_TOOLTIP]"));
	}
	else if (aUseSeedType == SeedType::SEED_ZOMBIE_NORMAL)
	{
		mToolTip->SetLabel(_S("[ZOMBIE]"));
	}
	else if (aUseSeedType == SeedType::SEED_ZOMBIE_TRAFFIC_CONE)
	{
		mToolTip->SetLabel(_S("[CONEHEAD_ZOMBIE]"));
	}
	else if (aUseSeedType == SeedType::SEED_ZOMBIE_POLEVAULTER)
	{
		mToolTip->SetLabel(_S("[POLE_VAULTING_ZOMBIE]"));
	}
	else if (aUseSeedType == SeedType::SEED_ZOMBIE_PAIL)
	{
		mToolTip->SetLabel(_S("[BUCKETHEAD_ZOMBIE]"));
	}
	else if (aUseSeedType == SeedType::SEED_ZOMBIE_LADDER)
	{
		mToolTip->SetLabel(_S("[LADDER_ZOMBIE]"));
	}
	else if (aUseSeedType == SeedType::SEED_ZOMBIE_DIGGER)
	{
		mToolTip->SetLabel(_S("[DIGGER_ZOMBIE]"));
	}
	else if (aUseSeedType == SeedType::SEED_ZOMBIE_BUNGEE)
	{
		mToolTip->SetLabel(_S("[BUNGEE_ZOMBIE]"));
	}
	else if (aUseSeedType == SeedType::SEED_ZOMBIE_FOOTBALL)
	{
		mToolTip->SetLabel(_S("[FOOTBALL_ZOMBIE]"));
	}
	else if (aUseSeedType == SeedType::SEED_ZOMBIE_BALLOON)
	{
		mToolTip->SetLabel(_S("[BALLOON_ZOMBIE]"));
	}
	else if (aUseSeedType == SeedType::SEED_ZOMBIE_SCREEN_DOOR)
	{
		mToolTip->SetLabel(_S("[SCREEN_DOOR_ZOMBIE]"));
	}
	else if (aUseSeedType == SeedType::SEED_ZOMBONI)
	{
		mToolTip->SetLabel(_S("[ZOMBONI]"));
	}
	else if (aUseSeedType == SeedType::SEED_ZOMBIE_POGO)
	{
		mToolTip->SetLabel(_S("[POGO_ZOMBIE]"));
	}
	else if (aUseSeedType == SeedType::SEED_ZOMBIE_DANCER)
	{
		mToolTip->SetLabel(_S("[DANCING_ZOMBIE]"));
	}
	else if (aUseSeedType == SeedType::SEED_ZOMBIE_GARGANTUAR)
	{
		mToolTip->SetLabel(_S("[GARGANTUAR]"));
	}
	else if (aUseSeedType == SeedType::SEED_ZOMBIE_IMP)
	{
		mToolTip->SetLabel(_S("[IMP]"));
	}
	else
	{
		mToolTip->SetLabel(Plant::GetNameString(aSeedPacket->mPacketType, aSeedPacket->mImitaterType));
	}

	int aPlantCost = GetCurrentPlantCost(aSeedPacket->mPacketType, aSeedPacket->mImitaterType);
	if (mApp->mEasyPlantingCheat)
	{
		mToolTip->SetWarningText(_S("FREE_PLANTING_CHEAT"));
	}
	else if (!aSeedPacket->mActive && (gLawnApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED || gLawnApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST))
	{
		if (aSeedPacket->mPacketType == SeedType::SEED_BEGHOULED_BUTTON_CRATER)
		{
			mToolTip->SetWarningText(_S("[BEGHOULED_NO_CRATERS]"));
		}
		else
		{
			mToolTip->SetWarningText(_S("[BEGHOULED_SEED_ALREADY_PURCHASED]"));
		}
	}
	else if (!aSeedPacket->mActive)
	{
		mToolTip->SetWarningText(_S("[WAITING_FOR_SEED]"));
	}
	else if (!aSeedPacket->mIsBenchPacket && !CanTakeSunMoney(aPlantCost) && !HasConveyorBeltSeedBank() && !mApp->IsSlotMachineLevel())
	{
		mToolTip->SetWarningText(_S("[NOT_ENOUGH_SUN]"));
	}
	else if (aUseSeedType == SeedType::SEED_GATLINGPEA)
	{
		if (!PlantingRequirementsMet(aUseSeedType))
		{
			mToolTip->SetWarningText(_S("[REQUIRES_REPEATER]"));
		}
	}
	else if (aUseSeedType == SeedType::SEED_WINTERMELON)
	{
		if (!PlantingRequirementsMet(aUseSeedType))
		{
			mToolTip->SetWarningText(_S("[REQUIRES_MELONPULT]"));
		}
	}
	else if (aUseSeedType == SeedType::SEED_TWINSUNFLOWER)
	{
		if (!PlantingRequirementsMet(aUseSeedType))
		{
			mToolTip->SetWarningText(_S("[REQUIRES_SUNFLOWER]"));
		}
	}
	else if (aUseSeedType == SeedType::SEED_SPIKEROCK)
	{
		if (!PlantingRequirementsMet(aUseSeedType))
		{
			mToolTip->SetWarningText(_S("[REQUIRES_SPIKEWEED]"));
		}
	}
	else if (aUseSeedType == SeedType::SEED_COBCANNON)
	{
		if (!PlantingRequirementsMet(aUseSeedType))
		{
			mToolTip->SetWarningText(_S("[REQUIRES_KERNELPULTS]"));
		}
	}
	else if (aUseSeedType == SeedType::SEED_GOLD_MAGNET)
	{
		if (!PlantingRequirementsMet(aUseSeedType))
		{
			mToolTip->SetWarningText(_S("[REQUIRES_MAGNETSHROOM]"));
		}
	}
	else if (aUseSeedType == SeedType::SEED_GLOOMSHROOM)
	{
		if (!PlantingRequirementsMet(aUseSeedType))
		{
			mToolTip->SetWarningText(_S("[REQUIRES_FUMESHROOM]"));
		}
	}
	else if (aUseSeedType == SeedType::SEED_CATTAIL)
	{
		if (!PlantingRequirementsMet(aUseSeedType))
		{
			mToolTip->SetWarningText(_S("[REQUIRES_LILY_PAD]"));
		}
	}

    if (aSeedPacket == &mHologramPacket || aSeedPacket->mIsBenchPacket)
    {
        mToolTip->mX = aSeedPacket->mX + (SEED_PACKET_WIDTH - mToolTip->mWidth) / 2;
        mToolTip->mY = aSeedPacket->mY - mToolTip->mHeight - 5;
    }
    else
    {
        mToolTip->mX = (SEED_PACKET_WIDTH - mToolTip->mWidth) / 2 + mSeedBank->mX + aSeedPacket->mOffsetX + aSeedPacket->mX;
        mToolTip->mY = mSeedBank->mY + aSeedPacket->mY + 70;
    }
	mToolTip->mVisible = true;
}

void Board::MouseDownCobcannonFire(int x, int y, int theClickCount)
{
	if (theClickCount >= 0 && y >= 80)
	{
		if (mCobCannonCursorDelayCounter > 0 && Distance2D(x, y, mCobCannonMouseX, mCobCannonMouseY) < 100.0f)
		{
			return;  
		}

		if (mCursorObject->mCursorType != CursorType::CURSOR_TYPE_PLANT_FROM_DUPLICATOR)
		{
			Plant* aCobcannon = mPlants.DataArrayTryToGet(mCursorObject->mCobCannonPlantID);
			if (aCobcannon)
			{
				aCobcannon->CobCannonFire(x, y);
			}
		}
	}
	ClearCursor();
}

void Board::MouseDownWithPlant(int x, int y, int theClickCount)
{
	if (theClickCount < 0)
	{
        if (mHologramPlanting)
        {
            mHologramPlanting = false;
            ClearCursor();
        }
        else
        {
            RefreshSeedPacketFromCursor();
        }
		mApp->PlayFoley(FoleyType::FOLEY_DROP);
		return;
	}

    // Teaching a specific SeedType must also gate alternate planting input paths.
    if (IsRollTutorialActive() &&
        ((mRollTutorialStep == ROLL_TUT_PLANT_SUNFLOWER && GetSeedTypeInCursor() != SeedType::SEED_SUNFLOWER) ||
         (mRollTutorialStep == ROLL_TUT_PLANT_PEASHOOTER && GetSeedTypeInCursor() != SeedType::SEED_PEASHOOTER) ||
         (mRollTutorialStep == ROLL_TUT_PLANT_MERGED && GetSeedTypeInCursor() != SeedType::SEED_SUNFLOWER)))
    {
        mApp->PlaySample(Sexy::SOUND_BUZZER);
        return;
    }

	if (mApp->IsIZombieLevel())
	{
		mChallenge->IZombieMouseDownWithZombie(x, y, theClickCount);
		return;
	}

	SeedType aPlantingSeedType = GetSeedTypeInCursor();
	int aGridX = PlantingPixelToGridX(x, y, aPlantingSeedType);
	int aGridY = PlantingPixelToGridY(x, y, aPlantingSeedType);

	if (aGridX < 0 || aGridX >= MAX_GRID_SIZE_X || aGridY < 0 || aGridY > MAX_GRID_SIZE_Y)
	{
		RefreshSeedPacketFromCursor();
		mApp->PlayFoley(FoleyType::FOLEY_DROP);
		return;
	}

	PlantingReason aReason = CanPlantAt(aGridX, aGridY, aPlantingSeedType);
	if (aReason != PlantingReason::PLANTING_OK)
	{
		if (aReason == PlantingReason::PLANTING_ONLY_ON_GRAVES)
		{
			DisplayAdvice(_S("[ADVICE_GRAVEBUSTERS_ON_GRAVES]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_GRAVEBUSTERS_ON_GRAVES);
		}
		else if (aPlantingSeedType == SeedType::SEED_LILYPAD)
		{
			if (aReason == PlantingReason::PLANTING_ONLY_IN_POOL)
			{
				DisplayAdvice(_S("[ADVICE_LILYPAD_ON_WATER]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_LILYPAD_ON_WATER);
			}
		}
		else if (aPlantingSeedType == SeedType::SEED_TANGLEKELP)
		{
			if (aReason == PlantingReason::PLANTING_ONLY_IN_POOL)
			{
				DisplayAdvice(_S("[ADVICE_TANGLEKELP_ON_WATER]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_TANGLEKELP_ON_WATER);
			}
		}
		else if (aPlantingSeedType == SeedType::SEED_SEASHROOM)
		{
			if (aReason == PlantingReason::PLANTING_ONLY_IN_POOL)
			{
				DisplayAdvice(_S("[ADVICE_SEASHROOM_ON_WATER]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_SEASHROOM_ON_WATER);
			}
		}
		else if (aReason == PlantingReason::PLANTING_ONLY_ON_GROUND)
		{
			DisplayAdvice(_S("[ADVICE_POTATO_MINE_ON_LILY]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_POTATOE_MINE_ON_LILY);
		}
		else if (aReason == PlantingReason::PLANTING_NOT_PASSED_LINE)
		{
			DisplayAdvice(_S("[ADVICE_NOT_PASSED_LINE]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_NOT_PASSED_LINE);
		}
		else if (aReason == PlantingReason::PLANTING_NEEDS_UPGRADE)
		{
			switch (aPlantingSeedType)
			{
			case SeedType::SEED_GATLINGPEA:
				DisplayAdvice(_S("[ADVICE_ONLY_ON_REPEATERS]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_ONLY_ON_REPEATERS);
				break;

			case SeedType::SEED_TWINSUNFLOWER:
				DisplayAdvice(_S("[ADVICE_ONLY_ON_SUNFLOWER]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_ONLY_ON_SUNFLOWER);
				break;

			case SeedType::SEED_GLOOMSHROOM:
				DisplayAdvice(_S("[ADVICE_ONLY_ON_FUMESHROOM]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_ONLY_ON_FUMESHROOM);
				break;

			case SeedType::SEED_CATTAIL:
				DisplayAdvice(_S("[ADVICE_ONLY_ON_LILYPAD]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_ONLY_ON_LILYPAD);
				break;

			case SeedType::SEED_WINTERMELON:
				DisplayAdvice(_S("[ADVICE_ONLY_ON_MELONPULT]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_ONLY_ON_MELONPULT);
				break;

			case SeedType::SEED_GOLD_MAGNET:
				DisplayAdvice(_S("[ADVICE_ONLY_ON_MAGNETSHROOM]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_ONLY_ON_MAGNETSHROOM);
				break;

			case SeedType::SEED_SPIKEROCK:
				DisplayAdvice(_S("[ADVICE_ONLY_ON_SPIKEWEED]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_ONLY_ON_SPIKEWEED);
				break;

			case SeedType::SEED_COBCANNON:
				DisplayAdvice(_S("[ADVICE_ONLY_ON_KERNELPULT]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_ONLY_ON_KERNELPULT);
				break;
			}
		}
		else if (aReason == PlantingReason::PLANTING_NOT_ON_ART)
		{
			SexyString aSeedName = Plant::GetNameString(mChallenge->GetArtChallengeSeed(aGridX, aGridY), SeedType::SEED_NONE);
			SexyString aMessage = TodReplaceString(_S("[ADVICE_WRONG_ART_TYPE]"), _S("{SEED}"), aSeedName);
			DisplayAdvice(aMessage, MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_WRONG_ART_TYPE);
		}
		else if (aReason == PlantingReason::PLANTING_NEEDS_POT)
		{
			if (mApp->IsFirstTimeAdventureMode() && mLevel == 41)
			{
				DisplayAdvice(_S("[ADVICE_PLANT_NEED_POT1]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_NEED_POT);
			}
			else
			{
				DisplayAdvice(_S("[ADVICE_PLANT_NEED_POT2]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_NEED_POT);
			}
		}
		else if (aReason == PlantingReason::PLANTING_NOT_ON_GRAVE)
		{
			DisplayAdvice(_S("[ADVICE_PLANT_NOT_ON_GRAVE]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_NOT_ON_GRAVE);
		}
		else if (aReason == PlantingReason::PLANTING_NOT_ON_CRATER)
		{
			if (IsPoolSquare(aGridX, aGridY))
			{
				DisplayAdvice(_S("[ADVICE_CANT_PLANT_THERE]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_CANT_PLANT_THERE);
			}
			else
			{
				DisplayAdvice(_S("[ADVICE_PLANT_NOT_ON_CRATER]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_NOT_ON_CRATER);
			}
		}
		else if (aReason == PlantingReason::PLANTING_NOT_ON_WATER)
		{
			if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN && mApp->mZenGarden->mGardenType == GardenType::GARDEN_AQUARIUM)
			{
				DisplayAdvice(_S("[ZEN_ONLY_AQUATIC_PLANTS]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_NONE);
			}
			else if (aPlantingSeedType == SeedType::SEED_POTATOMINE)
			{
				DisplayAdvice(_S("[ADVICE_POTATO_MINE_ON_LILY]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_POTATOE_MINE_ON_LILY);
			}
			else
			{
				DisplayAdvice(_S("[ADVICE_PLANT_NOT_ON_WATER]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANT_NOT_ON_WATER);
			}
		}
		else if (aReason == PlantingReason::PLANTING_NEEDS_GROUND)
		{
			DisplayAdvice(_S("[ADVICE_PLANTING_NEEDS_GROUND]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANTING_NEEDS_GROUND);
		}
		else if (aReason == PlantingReason::PLANTING_NEEDS_SLEEPING)
		{
			DisplayAdvice(_S("[ADVICE_PLANTING_NEED_SLEEPING]"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_PLANTING_NEED_SLEEPING);
		}

		if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_GLOVE || mApp->IsWhackAZombieLevel())
		{
			RefreshSeedPacketFromCursor();
			mApp->PlayFoley(FoleyType::FOLEY_DROP);
		}
		return;
	}
	
	ClearAdvice(AdviceType::ADVICE_PLANTING_NEED_SLEEPING);
	ClearAdvice(AdviceType::ADVICE_CANT_PLANT_THERE);
	ClearAdvice(AdviceType::ADVICE_PLANTING_NEEDS_GROUND);
	ClearAdvice(AdviceType::ADVICE_PLANT_NOT_ON_WATER);
	ClearAdvice(AdviceType::ADVICE_PLANT_NOT_ON_CRATER);
	ClearAdvice(AdviceType::ADVICE_PLANT_NOT_ON_GRAVE);
	ClearAdvice(AdviceType::ADVICE_PLANT_NEED_POT);
	ClearAdvice(AdviceType::ADVICE_PLANT_WRONG_ART_TYPE);
	ClearAdvice(AdviceType::ADVICE_PLANT_ONLY_ON_LILYPAD);
	ClearAdvice(AdviceType::ADVICE_PLANT_ONLY_ON_MAGNETSHROOM);
	ClearAdvice(AdviceType::ADVICE_PLANT_ONLY_ON_FUMESHROOM);
	ClearAdvice(AdviceType::ADVICE_PLANT_ONLY_ON_KERNELPULT);
	ClearAdvice(AdviceType::ADVICE_PLANT_ONLY_ON_SUNFLOWER);
	ClearAdvice(AdviceType::ADVICE_PLANT_ONLY_ON_SPIKEWEED);
	ClearAdvice(AdviceType::ADVICE_PLANT_ONLY_ON_MELONPULT);
	ClearAdvice(AdviceType::ADVICE_PLANT_ONLY_ON_REPEATERS);
	ClearAdvice(AdviceType::ADVICE_PLANT_NOT_PASSED_LINE);
	ClearAdvice(AdviceType::ADVICE_PLANT_GRAVEBUSTERS_ON_GRAVES);
	ClearAdvice(AdviceType::ADVICE_PLANT_LILYPAD_ON_WATER);
	ClearAdvice(AdviceType::ADVICE_PLANT_TANGLEKELP_ON_WATER);
	ClearAdvice(AdviceType::ADVICE_PLANT_SEASHROOM_ON_WATER);
	ClearAdvice(AdviceType::ADVICE_PLANT_POTATOE_MINE_ON_LILY);
	ClearAdvice(AdviceType::ADVICE_SURVIVE_FLAGS);

    int aBenchCursorIndex = -1;
    bool aPlantingFromBench = IsBenchCursorIndex(mCursorObject->mSeedBankIndex, aBenchCursorIndex) && !mHologramPlanting;
    bool aHologramPoolReserved = false;
    if (mHologramPlanting && IsRollSeedBank())
    {
        if (!ReservePlantPoolCard(aPlantingSeedType))
        {
            DisplayAdvice(_S("Hologram: no copies of this plant remain in the Pool."),
                MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_NONE);
            mApp->PlaySample(Sexy::SOUND_BUZZER);
            mHologramPlanting = false;
            ClearCursor();
            return;
        }
        aHologramPoolReserved = true;
    }
	if (!mApp->mEasyPlantingCheat && !aPlantingFromBench && (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_BANK || mHologramPlanting) && !HasConveyorBeltSeedBank())
	{
        if (!TakeSunMoney(GetCurrentPlantCost(aPlantingSeedType, SeedType::SEED_NONE)))
        {
            if (aHologramPoolReserved)
                ReturnPlantPoolCard(aPlantingSeedType);
            if (mHologramPlanting)
            {
                mHologramPlanting = false;
                ClearCursor();
            }
			return;
		}
	}
	
	bool aIsAwake = false;
	int aWakeUpCounter = 0;
	PlantsOnLawn aPlantOnLawn;
	GetPlantsOnLawn(aGridX, aGridY, &aPlantOnLawn);
	Plant* aNormalPlant = aPlantOnLawn.mNormalPlant;
	Plant* aPumpkinPlant = aPlantOnLawn.mPumpkinPlant;
	if (aNormalPlant && aNormalPlant->IsUpgradableTo(aPlantingSeedType))
	{
		if (aPlantingSeedType == SeedType::SEED_GLOOMSHROOM)
		{
			aIsAwake = !aNormalPlant->mIsAsleep;
			aWakeUpCounter = aNormalPlant->mWakeUpCounter;
		}
		aNormalPlant->Die();
	}
	if ((aPlantingSeedType == SeedType::SEED_WALLNUT || aPlantingSeedType == SeedType::SEED_TALLNUT) && aNormalPlant)
	{
		if (aNormalPlant->mSeedType == aPlantingSeedType)
		{
			aNormalPlant->Die();
		}
	}
	if (aPlantingSeedType == SeedType::SEED_PUMPKINSHELL && aPumpkinPlant)
	{
		if (aPumpkinPlant->mSeedType == SeedType::SEED_PUMPKINSHELL)
		{
			aPumpkinPlant->Die();
		}
	}
	if (aPlantingSeedType == SeedType::SEED_COBCANNON)
	{
		Plant* aRightPlant = GetTopPlantAt(aGridX + 1, aGridY, PlantPriority::TOPPLANT_ONLY_NORMAL_POSITION);
		if (aRightPlant)
		{
			aRightPlant->Die();
		}
	}
	if (aPlantingSeedType == SeedType::SEED_CATTAIL)
	{
		if (aPlantOnLawn.mUnderPlant)
		{
			aPlantOnLawn.mUnderPlant->Die();
		}
		if (aNormalPlant)
		{
			aNormalPlant->Die();
		}
	}

	if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_GLOVE)
	{
		mApp->mZenGarden->MovePlant(mPlants.DataArrayTryToGet(mCursorObject->mGlovePlantID), aGridX, aGridY);
	}
	else if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_WHEEL_BARROW)
	{
		mApp->mZenGarden->MouseDownWithFullWheelBarrow(x, y);
	}
    else if (mHologramPlanting)
    {
        Plant* aPlant = AddPlant(aGridX, aGridY, mCursorObject->mType, mCursorObject->mImitaterType);
        if (aPlant != nullptr && aHologramPoolReserved)
            aPlant->mRollPoolCardCount = 1;
        else if (aHologramPoolReserved)
            ReturnPlantPoolCard(aPlantingSeedType);
        if (aIsAwake)
            aPlant->SetSleeping(false);
        else
            aPlant->mWakeUpCounter = aWakeUpCounter;
        ClearCursor();
    }
	else if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_USABLE_COIN)
    {
        Coin* aCoin = mCoins.DataArrayTryToGet(mCursorObject->mCoinID);
        const bool aMiniCherry = aCoin && aCoin->mUsableSeedType == SeedType::SEED_CHERRYBOMB &&
            aCoin->mUsableStarLevel == 0;
        Plant* aPlant = AddPlant(aGridX, aGridY, mCursorObject->mType, mCursorObject->mImitaterType);
        if (aPlant && aMiniCherry)
            aPlant->SetupMiniCherryBomb();
        mCursorObject->mCoinID = CoinID::COINID_NULL;
        if (aCoin) aCoin->Die();
    }
	else if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_BANK)
	{
		Plant* aPlant = AddPlant(aGridX, aGridY, mCursorObject->mType, mCursorObject->mImitaterType);
		if (aIsAwake)
		{
			aPlant->SetSleeping(false);
		}
		else
		{
			aPlant->mWakeUpCounter = aWakeUpCounter;
		}

        if (aPlantingFromBench && aBenchCursorIndex >= 0 && aBenchCursorIndex < mBenchCount)
        {
            SeedPacket& aBenchPacket = mBenchPackets[aBenchCursorIndex];
            aPlant->mRollStarLevel = max(1, min(3, aBenchPacket.mStarLevel));
            aPlant->mRollPoolCardCount = aBenchPacket.mPoolCardCount;
            aPlant->mRollSunSpent = aBenchPacket.mSunSpent;
            if (aPlant->mRollStarLevel == 2)
            {
                aPlant->mPlantHealth = aPlant->mPlantHealth * 150 / 100;
                aPlant->mPlantMaxHealth = aPlant->mPlantMaxHealth * 150 / 100;
            }
            else if (aPlant->mRollStarLevel >= 3)
            {
                aPlant->mPlantHealth *= 2;
                aPlant->mPlantMaxHealth *= 2;
            }
            // Ownership transfers to Plant. Removing the source packet must not return
            // its copies; they return only if the planted unit later dies.
            aBenchPacket.mPoolCardCount = 0;
            RemoveBenchSeedPacket(aBenchCursorIndex, false);
            if (mRollTutorialStep == ROLL_TUT_PLANT_SUNFLOWER && aPlantingSeedType == SeedType::SEED_SUNFLOWER)
                AdvanceRollTutorial(ROLL_TUT_PLANT_SUNFLOWER);
            else if (mRollTutorialStep == ROLL_TUT_PLANT_PEASHOOTER && aPlantingSeedType == SeedType::SEED_PEASHOOTER)
                AdvanceRollTutorial(ROLL_TUT_PLANT_PEASHOOTER);
            else if (mRollTutorialStep == ROLL_TUT_PLANT_MERGED && aPlantingSeedType == SeedType::SEED_SUNFLOWER && aPlant->mRollStarLevel == 2)
                AdvanceRollTutorial(ROLL_TUT_PLANT_MERGED);
        }
        else if (mCursorObject->mSeedBankIndex >= 0 && mCursorObject->mSeedBankIndex < mSeedBank->mNumPackets)
        {
            if (IsRollSeedBank())
                OnRollPurchase();
            mSeedBank->mSeedPackets[mCursorObject->mSeedBankIndex].WasPlanted();
        }
	}
	else
	{
		TOD_ASSERT();
	}
    if (mHologramEnabled)
    {
        if (mHologramPlanting)
        {
            // Consuming the copied plant leaves Hologram empty. It must not clone itself again.
            mHologramLastPlant = SeedType::SEED_NONE;
            mHologramCooldown = 6000; // 60 seconds at 100 updates per second
            mHologramPlanting = false;
        }
        else
        {
            // Only a newly planted, non-Hologram plant can refill the Hologram.
            mHologramLastPlant = aPlantingSeedType;
        }
    }
	
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_COLUMN)
	{
		for (int aRow = 0; aRow < MAX_GRID_SIZE_Y; aRow++)
		{
			if (aRow == aGridY || CanPlantAt(aGridX, aRow, aPlantingSeedType) != PlantingReason::PLANTING_OK)
				continue;

			if (aPlantingSeedType == SeedType::SEED_WALLNUT || aPlantingSeedType == SeedType::SEED_TALLNUT)
			{
				aNormalPlant = GetTopPlantAt(aGridX, aRow, PlantPriority::TOPPLANT_ONLY_NORMAL_POSITION);
				if (aNormalPlant && aNormalPlant->mSeedType == aPlantingSeedType)
				{
					aNormalPlant->Die();
				}
			}
			if (aPlantingSeedType == SeedType::SEED_PUMPKINSHELL)
			{
				aPumpkinPlant = GetTopPlantAt(aGridX, aRow, PlantPriority::TOPPLANT_ONLY_PUMPKIN);
				if (aPumpkinPlant && aPumpkinPlant->mSeedType == SeedType::SEED_PUMPKINSHELL)
				{
					aPumpkinPlant->Die();
				}
			}
			AddPlant(aGridX, aRow, mCursorObject->mType, mCursorObject->mImitaterType);
		}
	}

	if (mTutorialState == TutorialState::TUTORIAL_LEVEL_1_PLANT_PEASHOOTER)
	{
		SetTutorialState(mPlants.mSize >= 2 ? TutorialState::TUTORIAL_LEVEL_1_COMPLETED : TutorialState::TUTORIAL_LEVEL_1_REFRESH_PEASHOOTER);
	}
	else if (mTutorialState == TutorialState::TUTORIAL_LEVEL_2_PLANT_SUNFLOWER)
	{
		int aSunFlowersCount = CountSunFlowers();
		if (aPlantingSeedType == SeedType::SEED_SUNFLOWER && aSunFlowersCount == 2)
		{
			DisplayAdvice(_S("[ADVICE_MORE_SUNFLOWERS]"), MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL2, AdviceType::ADVICE_NONE);
			if (!mSeedBank->mSeedPackets[1].CanPickUp())
			{
				SetTutorialState(TutorialState::TUTORIAL_LEVEL_2_REFRESH_SUNFLOWER);
			}
			else
			{
				SetTutorialState(TutorialState::TUTORIAL_LEVEL_2_PICK_UP_SUNFLOWER);
			}
		}
		else if (aSunFlowersCount >= 3)
		{
			SetTutorialState(TutorialState::TUTORIAL_LEVEL_2_COMPLETED);
		}
		else if (!mSeedBank->mSeedPackets[1].CanPickUp())
		{
			SetTutorialState(TutorialState::TUTORIAL_LEVEL_2_REFRESH_SUNFLOWER);
		}
		else
		{
			SetTutorialState(TutorialState::TUTORIAL_LEVEL_2_PICK_UP_SUNFLOWER);
		}
	}
	else if (mTutorialState == TutorialState::TUTORIAL_MORESUN_PLANT_SUNFLOWER)
	{
		if (CountSunFlowers() >= 3)
		{
			SetTutorialState(TutorialState::TUTORIAL_MORESUN_COMPLETED);
			// Legacy 3-Sunflower reminder removed.
			mTutorialTimer = -1;
		}
		else if (!mSeedBank->mSeedPackets[1].CanPickUp())
		{
			SetTutorialState(TutorialState::TUTORIAL_MORESUN_REFRESH_SUNFLOWER);
		}
		else
		{
			SetTutorialState(TutorialState::TUTORIAL_MORESUN_PICK_UP_SUNFLOWER);
		}
	}

	if (mApp->IsWallnutBowlingLevel())
	{
		mApp->PlaySample(Sexy::SOUND_BOWLING);
	}

	ClearCursor();
}

Plant* Board::ToolHitTestHelper(HitResult* theHitResult)
{
	theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_PLANT;
	Plant* aPlant = (Plant*)theHitResult->mObject;
	return (aPlant->mSeedType != SeedType::SEED_GRAVEBUSTER || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN) ? aPlant : nullptr;
}

Plant* Board::ToolHitTest(int theX, int theY)
{
	HitResult aHitResult;
	MouseHitTest(theX, theY, &aHitResult);
	if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_PLANT)
	{
        // Grave Buster cannot normally be shoveled, but Garden Glove can
        // recover any planted packet, including one still eating a grave.
        if (IsRollSeedBank() && mGardenGloveEnabled &&
            mCursorObject->mCursorType == CursorType::CURSOR_TYPE_GLOVE)
            return (Plant*)aHitResult.mObject;
		return ToolHitTestHelper(&aHitResult);
	}
	return nullptr;
}

void Board::TutorialArrowShow(int theX, int theY)
{
	TutorialArrowRemove();
	TodParticleSystem* aParticle = mApp->AddTodParticle(theX, theY, MakeRenderOrder(RenderLayer::RENDER_LAYER_TOP, 0, 0), ParticleEffect::PARTICLE_SEED_PACKET_PICK);
	mTutorialParticleID = mApp->ParticleGetID(aParticle);
}

void Board::TutorialArrowRemove()
{
	mApp->RemoveParticle(mTutorialParticleID);
	mTutorialParticleID = ParticleSystemID::PARTICLESYSTEMID_NULL;
}

void Board::MouseDownWithTool(int x, int y, int theClickCount, CursorType theCursorType)
{
    if (IsRollSeedBank() &&
        ((theCursorType == CursorType::CURSOR_TYPE_SHOVEL && IsRollToolRecharging(GameObjectType::OBJECT_TYPE_SHOVEL)) ||
         (theCursorType == CursorType::CURSOR_TYPE_GLOVE && IsRollToolRecharging(GameObjectType::OBJECT_TYPE_GLOVE))))
    { ClearCursor(); mApp->PlaySample(Sexy::SOUND_BUZZER); return; }
	if (theClickCount < 0)
	{
		ClearCursor();
		mApp->PlayFoley(FoleyType::FOLEY_DROP);
		return;
	}

	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
	{
		mApp->mZenGarden->MouseDownWithTool(x, y, theCursorType);
		return;
	}
	if (mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM)
	{
		mChallenge->TreeOfWisdomTool(x, y);
		return;
	}

	Plant* aPlant = ToolHitTest(x, y);
	if (aPlant == nullptr)
	{
		mApp->PlayFoley(FoleyType::FOLEY_DROP);
	}
    else if (theCursorType == CursorType::CURSOR_TYPE_GLOVE && IsRollSeedBank() && mGardenGloveEnabled)
    {
        mGloveCooldownUntil = GetTickCount() + 30000U;
        // An owned plant transfers its exact pool reservation; a plant created
        // outside the Shop transfers zero reservations (never fabricate copies).
        const int cards = max(0, aPlant->mRollPoolCardCount);
        {
            const SeedType type = aPlant->mSeedType;
            const int stars = aPlant->mRollStarLevel;
            const int spent = aPlant->mRollSunSpent;
            aPlant->mRollPoolCardCount = 0; // transfer reservation; Die must not return it
            aPlant->mRollSunSpent = 0;
            const int x = (int)aPlant->mX, y = (int)aPlant->mY;
            aPlant->Die();
            if (IsBenchFull() || !AddSeedToBench(type, stars, cards, SeedType::SEED_NONE, x, y, spent))
            {
                Coin* packet = AddCoin(x, y, CoinType::COIN_USABLE_SEED_PACKET, CoinMotion::COIN_MOTION_COIN);
                packet->mUsableSeedType = type;
                packet->mUsableStarLevel = stars;
                packet->mUsablePoolCount = cards;
                packet->mUsableSunSpent = spent;
                packet->mPersistent = true;
            }
        }
    }
	else if (theCursorType == CursorType::CURSOR_TYPE_SHOVEL)
	{
        if (IsRollSeedBank()) mShovelCooldownUntil = GetTickCount() + 30000U;
		mApp->PlayFoley(FoleyType::FOLEY_USE_SHOVEL);
		mPlantsShoveled++;

        if (IsRollSeedBank() && HasAugment(RollAugment::AUGMENT_GENTLE_UPROOTING) &&
            aPlant->mPlantHealth >= aPlant->mPlantMaxHealth)
            DropRegularSun(aPlant->mRollSunSpent, (int)aPlant->mX, (int)aPlant->mY);

		aPlant->Die();

		if (aPlant->mSeedType == SeedType::SEED_CATTAIL && GetTopPlantAt(aPlant->mPlantCol, aPlant->mRow, PlantPriority::TOPPLANT_ONLY_PUMPKIN))
		{
			NewPlant(aPlant->mPlantCol, aPlant->mRow, SeedType::SEED_LILYPAD, SeedType::SEED_NONE);
		}
		if (mTutorialState == TutorialState::TUTORIAL_SHOVEL_DIG || mTutorialState == TutorialState::TUTORIAL_SHOVEL_KEEP_DIGGING)
		{
			SetTutorialState(CountPlantByType(SeedType::SEED_PEASHOOTER) == 0 ? TutorialState::TUTORIAL_SHOVEL_COMPLETED : TutorialState::TUTORIAL_SHOVEL_KEEP_DIGGING);
		}
	}

	ClearCursor();
}

Plant* Board::SpecialPlantHitTest(int x, int y)
{
	Plant* aPlant = nullptr;
	while (IteratePlants(aPlant))
	{
		if (aPlant->mSeedType == SeedType::SEED_PUMPKINSHELL)
		{
			float aMinDist = GetTopPlantAt(aPlant->mPlantCol, aPlant->mRow, PlantPriority::TOPPLANT_ONLY_NORMAL_POSITION) ? 25 : 0;
			float aDistance = Distance2D(x, y, aPlant->mX + 40, aPlant->mY + 40);
			if (aDistance >= aMinDist && aDistance <= 50 && y > aPlant->mY + 25)
			{
				return aPlant;
			}
		}
		else if (Plant::IsFlying(aPlant->mSeedType))
		{
			if (Distance2D(x, y, aPlant->mX + 40, aPlant->mY) < 15)
			{
				return aPlant;
			}
		}
	}
	return nullptr;
}

bool Board::MouseHitTestPlant(int x, int y, HitResult* theHitResult)
{
	if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_COBCANNON_TARGET || mCursorObject->mCursorType == CursorType::CURSOR_TYPE_HAMMER)
		return false;

	Plant* aPlant;
	aPlant = SpecialPlantHitTest(x, y);
	if (aPlant)
	{
		theHitResult->mObject = aPlant;
		theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_PLANT;
		return true;
	}

	int aGridX = PixelToGridX(x, y);
	int aGridY = PixelToGridY(x, y);
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
	{
		aPlant = GetTopPlantAt(aGridX, aGridY, PlantPriority::TOPPLANT_ZEN_TOOL_ORDER);
		if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_WATERING_CAN && (!aPlant || !mApp->mZenGarden->PlantCanBeWatered(aPlant)))
		{
			Plant* aTopPlant = GetTopPlantAt(PixelToGridX(x - 30, y - 20), PixelToGridY(x - 30, y - 20), PlantPriority::TOPPLANT_ZEN_TOOL_ORDER);
			if (aTopPlant && mApp->mZenGarden->PlantCanBeWatered(aTopPlant))
			{
				aPlant = aTopPlant;
			}
		}
	}
	else
	{
		aPlant = GetTopPlantAt(aGridX, aGridY, PlantPriority::TOPPLANT_DIGGING_ORDER);
		if (aPlant && (aPlant->mSeedType == SeedType::SEED_LILYPAD || aPlant->mSeedType == SeedType::SEED_FLOWERPOT))
		{
			if (GetTopPlantAt(aGridX, aGridY, PlantPriority::TOPPLANT_ONLY_PUMPKIN))
			{
				return false;
			}
		}
	}

	if (aPlant == nullptr)
	{
		return false;
	}
	if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_CHOCOLATE && !mApp->mZenGarden->PlantCanHaveChocolate(aPlant))
	{
		theHitResult->mObject = nullptr;
		theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_NONE;
		return false;
	}

	theHitResult->mObject = aPlant;
	theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_PLANT;
	return true;
}

bool Board::MouseHitTest(int x, int y, HitResult* theHitResult)
{
	if (mBoardFadeOutCounter >= 0 || IsScaryPotterDaveTalking())
	{
		theHitResult->mObject = nullptr;
		theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_NONE;
		return false;
	}

	if (mMenuButton->IsMouseOver() && CanInteractWithBoardButtons())
	{
		theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_MENU_BUTTON;
		return true;
	}
	if (mFastButton->IsMouseOver() && CanInteractWithBoardButtons())
	{
		theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_FASTMODE_BUTTON;
		return true;
	}
	else if (mStoreButton && mStoreButton->IsMouseOver() && CanInteractWithBoardButtons())
	{
		theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_STORE_BUTTON;
		return true;
	}

	Rect aShovelButtonRect = GetShovelButtonRect();
    if (mHologramEnabled && mApp->mGameScene == GameScenes::SCENE_PLAYING)
    {
        mHologramPacket.mX = BOARD_WIDTH - 260;
        mHologramPacket.mY = BOARD_HEIGHT - 135;
        mHologramPacket.mPacketType = mHologramLastPlant;
        mHologramPacket.mImitaterType = SeedType::SEED_NONE;
        if (mHologramPacket.MouseHitTest(x, y, theHitResult))
            return true;
    }

    if (IsRollSeedBank() && mApp->mGameScene == GameScenes::SCENE_PLAYING && mCursorObject->mCursorType == CursorType::CURSOR_TYPE_NORMAL)
    {
        for (int i = 0; i < mBenchCount; ++i)
        {
            if (mBenchPackets[i].MouseHitTest(x, y, theHitResult))
                return true;
        }
    }

	if (mSeedBank->MouseHitTest(x, y, theHitResult))
	{
		if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_NORMAL || 
			mCursorObject->mCursorType == CursorType::CURSOR_TYPE_COBCANNON_TARGET || 
			mCursorObject->mCursorType == CursorType::CURSOR_TYPE_HAMMER)
			return true;
	}
	if (IsRollSeedBank() && mGardenGloveEnabled && GetGloveButtonRect().Contains(x, y) && CanInteractWithBoardButtons())
    {
        theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_GLOVE;
        return true;
    }
    if (mShowShovel && aShovelButtonRect.Contains(x, y) && CanInteractWithBoardButtons())
	{
		theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_SHOVEL;
		return true;
	}

	if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_NORMAL || mCursorObject->mCursorType == CursorType::CURSOR_TYPE_HAMMER)
	{
		Coin* aCoin = nullptr;
		Coin* aTopCoin = nullptr;
		while (IterateCoins(aCoin))
		{
			HitResult aHitResultCoin;
			if (aCoin->MouseHitTest(x, y, &aHitResultCoin))
			{
				aCoin = (Coin*)aHitResultCoin.mObject;
				if (aTopCoin == nullptr || aCoin->mRenderOrder >= aTopCoin->mRenderOrder)
				{
					theHitResult->mObjectType = aHitResultCoin.mObjectType;
					theHitResult->mObject = aCoin;
					aTopCoin = aCoin;
				}
			}
		}
		if (aTopCoin)
		{
			return true;
		}
	}

	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
	{
		bool canClick = false;
		if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_CHOCOLATE && !mApp->mZenGarden->IsStinkyHighOnChocolate())
		{
			canClick = true;
		}
		else if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_NORMAL && mApp->mZenGarden->IsStinkySleeping())
		{
			canClick = true;
		}

		GridItem* aStinky = mApp->mZenGarden->GetStinky();
		if (canClick && aStinky)
		{
			Rect aStinkyRect(aStinky->mPosX - 6, aStinky->mPosY - 10, 84, 90);
			if (aStinkyRect.Contains(x, y))
			{
				theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_STINKY;
				return true;
			}
		}
	}
	if (mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM)
	{
		if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_TREE_FOOD && mChallenge->TreeOfWisdomHitTest(x, y, theHitResult))
		{
			return true;
		}
	}
	if ((mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN || mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM) && CanInteractWithBoardButtons())
	{
		for (int i = (int)GameObjectType::OBJECT_TYPE_WATERING_CAN; i <= (int)GameObjectType::OBJECT_TYPE_NEXT_GARDEN; i++)
		{
			GameObjectType aTool = (GameObjectType)i;
			if (CanUseGameObject(aTool) && (aTool != GameObjectType::OBJECT_TYPE_TREE_FOOD || mChallenge->TreeOfWisdomCanFeed()))
			{
				Rect aButtonRect = GetShovelButtonRect();
				if (aTool == GameObjectType::OBJECT_TYPE_NEXT_GARDEN)
				{
					aButtonRect.mX = 564 + BOARD_ADDITIONAL_WIDTH;
				}
				else
				{
					GetZenButtonRect(aTool, aButtonRect);
				}

				if (aButtonRect.Contains(x, y))
				{
					theHitResult->mObjectType = (GameObjectType)aTool;
					return true;
				}
			}
		}
	}

	if (MouseHitTestPlant(x, y, theHitResult))
		return true;

	if (mApp->IsScaryPotterLevel() && 
		mCursorObject->mCursorType == CursorType::CURSOR_TYPE_NORMAL &&
		mChallenge->mChallengeState != ChallengeState::STATECHALLENGE_SCARY_POTTER_MALLETING && 
		mApp->mGameScene == GameScenes::SCENE_PLAYING &&
		mApp->GetDialog(Dialogs::DIALOG_GAME_OVER) == nullptr && 
		mApp->GetDialog(Dialogs::DIALOG_CONTINUE) == nullptr)
	{
		GridItem* aScaryPot = GetGridItemAt(GridItemType::GRIDITEM_SCARY_POT, PixelToGridX(x, y), PixelToGridY(x, y));
		if (aScaryPot)
		{
			theHitResult->mObject = aScaryPot;
			theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_SCARY_POT;
			return true;
		}
	}

	if (mApp->IsSlotMachineLevel())
	{
		Rect aSlotMachineHandleRect = mChallenge->SlotMachineGetHandleRect();
		if (aSlotMachineHandleRect.Contains(x, y) && mChallenge->mChallengeState == ChallengeState::STATECHALLENGE_NORMAL && !HasLevelAwardDropped())
		{
			theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_SLOT_MACHINE_HANDLE;
			return true;
		}
	}

	theHitResult->mObject = nullptr;
	theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_NONE;
	return false;
}

bool Board::IsRollToolRecharging(GameObjectType theTool) const
{
    if (!const_cast<Board*>(this)->IsRollSeedBank()) return false;
    const unsigned int deadline = theTool == GameObjectType::OBJECT_TYPE_SHOVEL ? mShovelCooldownUntil :
        theTool == GameObjectType::OBJECT_TYPE_GLOVE ? mGloveCooldownUntil : 0;
    return deadline != 0 && (int)(deadline - GetTickCount()) > 0;
}

void Board::PickUpTool(GameObjectType theObjectType)
{
    if (IsRollToolRecharging(theObjectType))
    {
        mApp->PlaySample(Sexy::SOUND_BUZZER);
        return;
    }
	if (mPaused || (mApp->mGameScene != GameScenes::SCENE_PLAYING && !mCutScene->IsInShovelTutorial()))
		return;

	switch (theObjectType)
	{
	case GameObjectType::OBJECT_TYPE_SHOVEL:
		if (mTutorialState == TutorialState::TUTORIAL_SHOVEL_PICKUP)
		{
			SetTutorialState(TutorialState::TUTORIAL_SHOVEL_DIG);
		}
		mCursorObject->mCursorType = CursorType::CURSOR_TYPE_SHOVEL;
		mApp->PlayFoley(FoleyType::FOLEY_SHOVEL);
		break;

	case GameObjectType::OBJECT_TYPE_WATERING_CAN:
		if (mTutorialState == TutorialState::TUTORIAL_ZEN_GARDEN_PICKUP_WATER)
		{
			mTutorialState = TutorialState::TUTORIAL_ZEN_GARDEN_WATER_PLANT;
			DisplayAdvice(_S("[ADVICE_ZEN_GARDEN_WATER_PLANT]"), MessageStyle::MESSAGE_STYLE_ZEN_GARDEN_LONG, AdviceType::ADVICE_NONE);
			TutorialArrowRemove();
		}
		mCursorObject->mCursorType = CursorType::CURSOR_TYPE_WATERING_CAN;
		mApp->PlayFoley(FoleyType::FOLEY_DROP);
		break;

	case GameObjectType::OBJECT_TYPE_FERTILIZER:
		if (mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_FERTILIZER] > PURCHASE_COUNT_OFFSET)
		{
			mCursorObject->mCursorType = CursorType::CURSOR_TYPE_FERTILIZER;
		}
		else
		{
			mApp->PlaySample(Sexy::SOUND_BUZZER);
		}
		break;

	case GameObjectType::OBJECT_TYPE_BUG_SPRAY:
		if (mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_BUG_SPRAY] > PURCHASE_COUNT_OFFSET)
		{
			mCursorObject->mCursorType = CursorType::CURSOR_TYPE_BUG_SPRAY;
		}
		else
		{
			mApp->PlaySample(Sexy::SOUND_BUZZER);
		}
		break;

	case GameObjectType::OBJECT_TYPE_PHONOGRAPH:
		mCursorObject->mCursorType = CursorType::CURSOR_TYPE_PHONOGRAPH;
		mApp->PlayFoley(FoleyType::FOLEY_DROP);
		break;

	case GameObjectType::OBJECT_TYPE_CHOCOLATE:
		if (mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_CHOCOLATE] > PURCHASE_COUNT_OFFSET)
		{
			mCursorObject->mCursorType = CursorType::CURSOR_TYPE_CHOCOLATE;
		}
		else
		{
			mApp->PlaySample(Sexy::SOUND_BUZZER);
		}
		break;

	case GameObjectType::OBJECT_TYPE_GLOVE:
		mCursorObject->mCursorType = CursorType::CURSOR_TYPE_GLOVE;
		mApp->PlayFoley(FoleyType::FOLEY_DROP);
		break;

	case GameObjectType::OBJECT_TYPE_MONEY_SIGN:
		mCursorObject->mCursorType = CursorType::CURSOR_TYPE_MONEY_SIGN;
		mApp->PlayFoley(FoleyType::FOLEY_DROP);
		break;

	case GameObjectType::OBJECT_TYPE_WHEELBARROW:
		mCursorObject->mCursorType = CursorType::CURSOR_TYPE_WHEEELBARROW;
		mApp->PlayFoley(FoleyType::FOLEY_DROP);
		break;

	case GameObjectType::OBJECT_TYPE_TREE_FOOD:
		if (mChallenge->TreeOfWisdomCanFeed())
		{
			if (mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_TREE_FOOD] > PURCHASE_COUNT_OFFSET)
			{
				mCursorObject->mCursorType = CursorType::CURSOR_TYPE_TREE_FOOD;
			}
			else
			{
				mApp->PlaySample(Sexy::SOUND_BUZZER);
			}
		}
		break;

	default:
		TOD_ASSERT();
		break;
	}

	mCursorObject->mType = SeedType::SEED_NONE;
}

void Board::MouseDown(int x, int y, int theClickCount)
{
    if (mSeedDraftChoosing && theClickCount > 0 && !mPaused)
    {
        for (int i = 0; i < 3; ++i)
            if (mSeedDraftChoices[i] != SeedType::SEED_NONE &&
                Rect(BOARD_WIDTH / 2 - 155 + i * 105, BOARD_HEIGHT / 2 - 35,
                    SEED_PACKET_WIDTH + 4, SEED_PACKET_HEIGHT + 8).Contains(x, y))
            {
                ChooseSeedDraft(i);
                mIgnoreMouseUp = true;
                return;
            }
    }
    if (IsRollSeedBank() && !mAugmentChoosing && mActiveAugment != RollAugment::AUGMENT_NONE &&
        mApp->mGameScene == GameScenes::SCENE_PLAYING && theClickCount != 0)
    {
        int aMenuX = mMenuButton != nullptr ? mMenuButton->mX : BOARD_WIDTH - 160;
        if (theClickCount > 0 &&
            GetAugmentGalleryButtonRect(aMenuX, mMenuButton != nullptr ? mMenuButton->mY : -10).Contains(x, y))
        {
            // Bring the timeline up to date before reversing its direction.
            AdvanceAugmentGalleryAnimation(mAugmentGalleryOpen);
            if (!mAugmentGalleryOpen && gAugmentGalleryProgressMs == 0)
            {
                mAugmentGalleryPage = 0;
                PrepareGalleryAugmentPlants(mEnabledAugments);
            }
            mAugmentGalleryOpen = !mAugmentGalleryOpen;
            mIgnoreMouseUp = true;
            mApp->PlaySample(Sexy::SOUND_TAP);
            return;
        }
        if (mAugmentGalleryOpen || gAugmentGalleryProgressMs > 0)
        {
            // The gallery is view-only. Clicking outside or on the cards does
            // not close it and must never plant or buy anything underneath.
            if (mAugmentGalleryOpen && gAugmentGalleryProgressMs == AUGMENT_GALLERY_TOTAL_MS)
            {
                int aCount = 0;
                for (int i = 0; i < RollAugment::NUM_AUGMENTS; ++i)
                    if (HasAugment(i)) ++aCount;
                const int aPages = max(1, (aCount + 2) / 3);
                if (GetAugmentGalleryPreviousRect().Contains(x, y))
                {
                    mAugmentGalleryPage = max(0, mAugmentGalleryPage - 1);
                    gAugmentGalleryProgressMs = 0; // Reveal the new page from the right.
                }
                else if (GetAugmentGalleryNextRect().Contains(x, y))
                {
                    mAugmentGalleryPage = min(aPages - 1, mAugmentGalleryPage + 1);
                    gAugmentGalleryProgressMs = 0;
                }
            }
            mIgnoreMouseUp = true;
            mApp->PlaySample(Sexy::SOUND_TAP);
            return;
        }
    }

	// The Roll Shop tutorial uses the game's normal Advice line and Tutorial Arrow,
	// and gates all inputs to the one action required by the current step.
	if (IsRollTutorialActive())
	{
		mIgnoreMouseUp = false;
		if (mPaused || mApp->mGameScene != GameScenes::SCENE_PLAYING || mApp->GetDialogCount() > 0 || mAugmentChoosing)
			return;

		HitResult aTutorialHit;
		MouseHitTest(x, y, &aTutorialHit);
		bool aConsumed = false;
        switch (mRollTutorialStep)
        {
        case ROLL_TUT_BUY_SUNFLOWER:
        case ROLL_TUT_BUY_PEASHOOTER:
        case ROLL_TUT_BUY_MERGE_FIRST:
        case ROLL_TUT_BUY_MERGE_SECOND:
            if (theClickCount > 0 && aTutorialHit.mObjectType == GameObjectType::OBJECT_TYPE_SEEDPACKET)
            {
                SeedPacket* aPacket = (SeedPacket*)aTutorialHit.mObject;
                SeedType aRequired = mRollTutorialStep == ROLL_TUT_BUY_PEASHOOTER ? SeedType::SEED_PEASHOOTER : SeedType::SEED_SUNFLOWER;
                for (int i = 0; i < mSeedBank->mNumPackets; ++i)
                    if (aPacket == &mSeedBank->mSeedPackets[i] && aPacket->mPacketType == aRequired)
                    {
                        aPacket->MouseDown(x, y, theClickCount);
                        UpdateCursor();
                        aConsumed = true;
                        break;
                    }
            }
            break;
        case ROLL_TUT_PLANT_SUNFLOWER:
        case ROLL_TUT_PLANT_PEASHOOTER:
        case ROLL_TUT_PLANT_MERGED:
        {
            SeedType aRequired = mRollTutorialStep == ROLL_TUT_PLANT_PEASHOOTER ? SeedType::SEED_PEASHOOTER : SeedType::SEED_SUNFLOWER;
            if (theClickCount > 0 && mCursorObject->mCursorType == CursorType::CURSOR_TYPE_NORMAL &&
                aTutorialHit.mObjectType == GameObjectType::OBJECT_TYPE_SEEDPACKET)
            {
                SeedPacket* aPacket = (SeedPacket*)aTutorialHit.mObject;
                if (aPacket->mIsBenchPacket && aPacket->mPacketType == aRequired &&
                    (mRollTutorialStep != ROLL_TUT_PLANT_MERGED || aPacket->mStarLevel == 2))
                {
                    aPacket->MouseDown(x, y, theClickCount);
                    UpdateCursor();
                    if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_BANK)
                        SetRollTutorialStep(mRollTutorialStep);
                    aConsumed = true;
                }
            }
            else if (theClickCount > 0 && mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_BANK &&
                mCursorObject->mType == aRequired)
            {
                MouseDownWithPlant(x, y, theClickCount);
                UpdateCursor();
                aConsumed = true;
            }
            break;
        }
        case ROLL_TUT_REROLL:
        case ROLL_TUT_ROLL_LOCKED:
            if (theClickCount > 0 && GetRollButtonRect().Contains(x, y))
            {
                TryRollSeedBank();
                UpdateCursor();
                aConsumed = true;
            }
            break;
        case ROLL_TUT_LOCK:
            if (theClickCount < 0 && TryToggleSlotLock(x, y))
                aConsumed = true;
            break;
        case ROLL_TUT_WAIT_MERGE:
        case ROLL_TUT_FINISH:
            return;
        }
		if (!aConsumed)
			mApp->PlaySample(Sexy::SOUND_BUZZER);
		return;
	}

	Widget::MouseDown(x, y, theClickCount);
	mIgnoreMouseUp = !CanInteractWithBoardButtons();
	if (mAugmentChoosing)
	{
		mIgnoreMouseUp = true;
		AugmentMouseDown(x, y, theClickCount);
		return;
	}
    if (theClickCount > 0 && mHologramEnabled && mTimeStopCounter <= 0 &&
        !mPaused && mApp->GetDialogCount() == 0 && mApp->mGameScene == GameScenes::SCENE_PLAYING)
    {
        mHologramPacket.mX = BOARD_WIDTH - 260;
        mHologramPacket.mY = BOARD_HEIGHT - 135;
        mHologramPacket.mPacketType = mHologramLastPlant;
        mHologramPacket.mImitaterType = SeedType::SEED_NONE;
        HitResult aHologramHit;
        if (mHologramPacket.MouseHitTest(x, y, &aHologramHit))
        {
            if (mHologramCooldown > 0 || mCursorObject->mCursorType != CursorType::CURSOR_TYPE_NORMAL)
            {
                mApp->PlaySample(Sexy::SOUND_BUZZER);
                return;
            }
            mHologramPacket.mRefreshTime = 6000;
            mHologramPacket.mRefreshCounter = 6000 - mHologramCooldown;
            mHologramPacket.mActive = mHologramCooldown <= 0;
            if (!mHologramPacket.CanPickUp())
            {
                mApp->PlaySample(Sexy::SOUND_BUZZER);
                return;
            }
            int aCost = GetCurrentPlantCost(mHologramLastPlant, SeedType::SEED_NONE);
            if ((!mApp->mEasyPlantingCheat && !CanTakeSunMoney(aCost)) || !PlantingRequirementsMet(mHologramLastPlant))
            {
                mApp->PlaySample(Sexy::SOUND_BUZZER);
                return;
            }
            // Hologram is a native SeedPacket display, but its planting cursor owns no bank slot.
            mCursorObject->mCursorType = CursorType::CURSOR_TYPE_PLANT_FROM_BANK;
            mCursorObject->mType = mHologramLastPlant;
            mCursorObject->mImitaterType = SeedType::SEED_NONE;
            mCursorObject->mSeedBankIndex = -1;
            mHologramPlanting = true;
            UpdateCursor();
            return;
        }
    }
	if (mTimeStopCounter > 0)
		return;

	HitResult aHitResult;
	MouseHitTest(x, y, &aHitResult);
	if (mChallenge->MouseDown(x, y, theClickCount, &aHitResult))
		return;

	if (theClickCount > 0 && IsOverRollButton(x, y) && TryRollSeedBank())
	{
		UpdateCursor();
		return;
	}

	if (theClickCount < 0 && TryToggleSlotLock(x, y))
	{
		return;
	}


	if (mMenuButton->IsMouseOver() && CanInteractWithBoardButtons() && theClickCount > 0)
	{
		mApp->PlaySample(Sexy::SOUND_GRAVEBUTTON);
	}
	if (mFastButton->IsMouseOver() && CanInteractWithBoardButtons() && theClickCount > 0)
	{
		mApp->PlaySample(Sexy::SOUND_TAP);
	}
	else if (mStoreButton && mStoreButton->IsMouseOver() && CanInteractWithBoardButtons() && theClickCount > 0)
	{
		if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN || mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM)
		{
			mApp->PlaySample(Sexy::SOUND_TAP);
		}
		else if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND || mApp->mGameMode == GameMode::GAMEMODE_UPSELL)
		{
			mApp->PlaySample(Sexy::SOUND_GRAVEBUTTON);
		}
	}

	if (mApp->mGameScene == GameScenes::SCENE_LEVEL_INTRO && mApp->mSeedChooserScreen)
	{
		mApp->mSeedChooserScreen->CancelLawnView();
	}
	if (mApp->mGameScene == GameScenes::SCENE_ZOMBIES_WON)
	{
		mCutScene->ZombieWonClick();
		return;
	}
	if (mApp->mGameScene == GameScenes::SCENE_LEVEL_INTRO)
	{
		mCutScene->MouseDown(x, y);
	}
	
	if (mApp->mTodCheatKeys && !mApp->IsScaryPotterLevel() && mNextSurvivalStageCounter > 0)
	{
		mNextSurvivalStageCounter = 2;
		for (int i = 0; i < MAX_GRID_SIZE_Y; i++)
		{
			if (mIceTimer[i] > 2)
			{
				mIceTimer[i] = 2;
			}
		}
	}

	CursorType aCursor = mCursorObject->mCursorType;
	if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_NONE)
	{
		if (aCursor == CURSOR_TYPE_COBCANNON_TARGET)
		{
			MouseDownCobcannonFire(x, y, theClickCount);
			UpdateCursor();
			return;
		}
	}
	else if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_COIN && theClickCount >= 0)
	{
		Coin* aCoin = (Coin*)aHitResult.mObject;
		if (aCoin->mBoard)
		{
			aCoin->MouseDown(x, y, theClickCount);
		}
		UpdateCursor();
		return;
	}

	if (aCursor == CursorType::CURSOR_TYPE_SHOVEL ||
		aCursor == CursorType::CURSOR_TYPE_WATERING_CAN ||
		aCursor == CursorType::CURSOR_TYPE_FERTILIZER ||
		aCursor == CursorType::CURSOR_TYPE_BUG_SPRAY ||
		aCursor == CursorType::CURSOR_TYPE_PHONOGRAPH ||
		aCursor == CursorType::CURSOR_TYPE_CHOCOLATE ||
		aCursor == CursorType::CURSOR_TYPE_GLOVE ||
		aCursor == CursorType::CURSOR_TYPE_MONEY_SIGN ||
		aCursor == CursorType::CURSOR_TYPE_WHEEELBARROW ||
		aCursor == CursorType::CURSOR_TYPE_TREE_FOOD)
	{
		MouseDownWithTool(x, y, theClickCount, aCursor);
	}
	else if (IsPlantInCursor())
	{
		MouseDownWithPlant(x, y, theClickCount);
	}
	else if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_SEEDPACKET)
	{
		if (!mPaused)
		{
			((SeedPacket*)aHitResult.mObject)->MouseDown(x, y, theClickCount);
		}
	}
	else if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_NEXT_GARDEN)
	{
		if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
		{
			mApp->mZenGarden->GotoNextGarden();
		}
		else if (mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM)
		{
			mChallenge->TreeOfWisdomNextGarden();
		}
		mApp->PlaySample(Sexy::SOUND_TAP);
	}
	else if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_SHOVEL ||
		aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_WATERING_CAN ||
		aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_FERTILIZER ||
		aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_BUG_SPRAY ||
		aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_PHONOGRAPH ||
		aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_CHOCOLATE ||
		aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_GLOVE ||
		aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_MONEY_SIGN ||
		aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_WHEELBARROW ||
		aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_TREE_FOOD)
	{
		PickUpTool(aHitResult.mObjectType);
	}
	else if (aHitResult.mObjectType == GameObjectType::OBJECT_TYPE_PLANT)
	{
		((Plant*)aHitResult.mObject)->MouseDown(x, y, theClickCount);
	}

	UpdateCursor();
}

void Board::ClearCursor()
{
	if (mAdvice->mDuration > 0)
	{
		if (mHelpIndex == AdviceType::ADVICE_PLANT_GRAVEBUSTERS_ON_GRAVES ||
			mHelpIndex == AdviceType::ADVICE_PLANT_LILYPAD_ON_WATER ||
			mHelpIndex == AdviceType::ADVICE_PLANT_TANGLEKELP_ON_WATER ||
			mHelpIndex == AdviceType::ADVICE_PLANT_SEASHROOM_ON_WATER ||
			mHelpIndex == AdviceType::ADVICE_PLANT_POTATOE_MINE_ON_LILY ||
			mHelpIndex == AdviceType::ADVICE_PLANT_WRONG_ART_TYPE ||
			mHelpIndex == AdviceType::ADVICE_PLANT_NEED_POT ||
			mHelpIndex == AdviceType::ADVICE_PLANT_NOT_PASSED_LINE ||
			mHelpIndex == AdviceType::ADVICE_PLANT_ONLY_ON_REPEATERS ||
			mHelpIndex == AdviceType::ADVICE_PLANT_ONLY_ON_MELONPULT ||
			mHelpIndex == AdviceType::ADVICE_PLANT_ONLY_ON_SUNFLOWER ||
			mHelpIndex == AdviceType::ADVICE_PLANT_ONLY_ON_SPIKEWEED ||
			mHelpIndex == AdviceType::ADVICE_PLANT_ONLY_ON_KERNELPULT)
		{
			ClearAdvice(mHelpIndex);
		}
	}

	mCursorObject->mType = SeedType::SEED_NONE;
	mCursorObject->mCursorType = CursorType::CURSOR_TYPE_NORMAL;
	mCursorObject->mSeedBankIndex = -1;
	mCursorObject->mCoinID = CoinID::COINID_NULL;
	mCursorObject->mDuplicatorPlantID = PlantID::PLANTID_NULL;
	mCursorObject->mCobCannonPlantID = PlantID::PLANTID_NULL;
	mCursorObject->mGlovePlantID = PlantID::PLANTID_NULL;
	mApp->SetCursor(CURSOR_POINTER);
	mChallenge->ClearCursor();

	if (mTutorialState == TutorialState::TUTORIAL_LEVEL_1_PLANT_PEASHOOTER)
	{
		SetTutorialState(TutorialState::TUTORIAL_LEVEL_1_PICK_UP_PEASHOOTER);
	}
	else if (mTutorialState == TutorialState::TUTORIAL_LEVEL_2_PLANT_SUNFLOWER || mTutorialState == TutorialState::TUTORIAL_LEVEL_2_REFRESH_SUNFLOWER)
	{
		if (!mSeedBank->mSeedPackets[1].CanPickUp())
		{
			SetTutorialState(TutorialState::TUTORIAL_LEVEL_2_REFRESH_SUNFLOWER);
		}
		else
		{
			SetTutorialState(TutorialState::TUTORIAL_LEVEL_2_PICK_UP_SUNFLOWER);
		}
	}
	else if (mTutorialState == TutorialState::TUTORIAL_MORESUN_PLANT_SUNFLOWER || mTutorialState == TutorialState::TUTORIAL_MORESUN_REFRESH_SUNFLOWER)
	{
		if (!mSeedBank->mSeedPackets[1].CanPickUp())
		{
			SetTutorialState(TutorialState::TUTORIAL_MORESUN_REFRESH_SUNFLOWER);
		}
		else
		{
			SetTutorialState(TutorialState::TUTORIAL_MORESUN_PICK_UP_SUNFLOWER);
		}
	}
	else if (mTutorialState == TutorialState::TUTORIAL_SHOVEL_DIG)
	{
		SetTutorialState(TutorialState::TUTORIAL_SHOVEL_PICKUP);
	}
}

bool Board::CanInteractWithBoardButtons()
{
	if (mPaused || mApp->GetDialogCount() > 0)
		return false;

	if (mCursorObject->mCursorType != CursorType::CURSOR_TYPE_NORMAL && 
		mCursorObject->mCursorType != CursorType::CURSOR_TYPE_HAMMER &&
		mCursorObject->mCursorType != CursorType::CURSOR_TYPE_COBCANNON_TARGET)
		return false;

	if (mChallenge->mChallengeState == ChallengeState::STATECHALLENGE_ZEN_FADING)
		return false;

	return mApp->mGameMode == GameMode::GAMEMODE_UPSELL || mApp->mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_OFF;
}

void Board::MouseUp(int x, int y, int theClickCount)
{
	if (IsRollTutorialActive())
	{
		mIgnoreMouseUp = false;
		return;
	}
	Widget::MouseUp(x, y, theClickCount);
	if (mAugmentChoosing)
		return;
	if (mIgnoreMouseUp)
	{
		mIgnoreMouseUp = false;
		return;
	}

	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED && mChallenge->MouseUp(x, y) && theClickCount > 0)
		return;

	if (CanInteractWithBoardButtons() && theClickCount > 0)
	{
		if (mMenuButton->IsMouseOver() && !mApp->GetDialog(Dialogs::DIALOG_GAME_OVER) && !mApp->GetDialog(Dialogs::DIALOG_LEVEL_COMPLETE))
		{
			mMenuButton->mIsOver = false;
			mMenuButton->mIsDown = false;
			UpdateCursor();
			ClearCursor();
			if (mTutorialState == TutorialState::TUTORIAL_ZEN_GARDEN_COMPLETED)
			{
				mApp->FinishZenGardenToturial();
			}
			else if (mApp->mGameMode != GAMEMODE_CHALLENGE_ZEN_GARDEN && mApp->mGameMode != GAMEMODE_TREE_OF_WISDOM && mApp->mGameMode != GAMEMODE_UPSELL)
			{
				mApp->PlaySample(Sexy::SOUND_PAUSE);
				mApp->DoNewOptions(false);
			}
			else
			{
				mApp->mBoardResult = BoardResult::BOARDRESULT_QUIT;
				mApp->DoBackToMain();
			}
		}
		if (mFastButton->IsMouseOver() && !mApp->GetDialog(Dialogs::DIALOG_GAME_OVER) && !mApp->GetDialog(Dialogs::DIALOG_LEVEL_COMPLETE) && mBoardFadeOutCounter < 0)
		{
			mFastButton->mIsOver = false;
			mFastButton->mIsDown = false;
			UpdateCursor();
			mApp->mIsFastMode = !mApp->mIsFastMode;
		}
		else if(mStoreButton && mStoreButton->IsMouseOver())
		{
			if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
			{
				ClearAdviceImmediately();
				mApp->mZenGarden->OpenStore();
			}
			else if (mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM)
			{
				mChallenge->TreeOfWisdomOpenStore();
			}
			else if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND)
			{
				mChallenge->mChallengeState = ChallengeState::STATECHALLENGE_LAST_STAND_ONSLAUGHT;
				mStoreButton->mBtnNoDraw = true;
				mStoreButton->mDisabled = true;
				mZombieCountDown = 10;
				mZombieCountDownStart = 10;
			}
			else if (mApp->mGameMode == GameMode::GAMEMODE_UPSELL)
			{
				if (mApp->mDRM)
				{
					mApp->mDRM->BuyGame();
				}
				mApp->DoBackToMain();
			}
		}
	}
}

void Board::ShowCoinBank(int theDuration)
{
	mCoinBankFadeCount = theDuration;
}

void Board::Pause(bool thePause)
{
	if (mPaused == thePause)
		return;

	mPaused = thePause;
    // Do not count menu / paused wall-clock time toward the draft decision.
    mSeedDraftLastUpdateTick = GetTickCount();
	if (thePause && mApp->mPlayerInfo->GetMoneyDollars() > 0)
	{
		ShowCoinBank();
	}

	if (!thePause || mApp->mGameScene != GameScenes::SCENE_LEVEL_INTRO)
	{
		mApp->mSoundSystem->GamePause(thePause);
		mApp->mMusic->GameMusicPause(thePause);
	}
}

int Board::GetGraveStonesCount()
{
	int aCount = 0;

	GridItem* aGridItem = nullptr;
	while (IterateGridItems(aGridItem))
	{
		if (aGridItem->mGridItemType == GridItemType::GRIDITEM_GRAVESTONE)
		{
			aCount++;
		}
	}

	return aCount;
}

void Board::PickSpecialGraveStone()
{
	GridItem* aGridItem = nullptr;
	GridItem* aPicks[MAX_GRAVE_STONES];
	int aPickCount = 0;
	while (IterateGridItems(aGridItem))
	{
		if (aGridItem->mGridItemType == GridItemType::GRIDITEM_GRAVESTONE)
		{
			TOD_ASSERT(aPickCount < MAX_GRAVE_STONES);
			aPicks[aPickCount] = aGridItem;
			aPickCount++;
		}
	}

	if (aPickCount > 0)
	{
		((GridItem*)TodPickFromArray((int*)aPicks, aPickCount))->mGridItemState = GridItemState::GRIDITEM_STATE_GRAVESTONE_SPECIAL;
	}
}

void Board::SpawnZombiesFromPool()
{
	if (mIceTrapCounter > 0)
		return;
	
	int aCount, aZombiePoints;
	if (mLevel == 21 || mLevel == 22 || mLevel == 31 || mLevel == 32)
	{
		aCount = 2;
		aZombiePoints = 3;
	}
	else if (mLevel == 23 || mLevel == 24 || mLevel == 25 || mLevel == 33 || mLevel == 34 || mLevel == 35)
	{
		aCount = 3;
		aZombiePoints = 5;
	}
	else
	{
		aCount = 3;
		aZombiePoints = 7;
	}
	
	int aGridArrayCount = 0;
	TodWeightedGridArray aGridArray[MAX_POOL_GRID_SIZE];
	for (int aGridX = 5; aGridX < MAX_GRID_SIZE_X; aGridX++)
	{
		for (int aGridY = 2; aGridY <= 3; aGridY++)
		{
			aGridArray[aGridArrayCount].mX = aGridX;
			aGridArray[aGridArrayCount].mY = aGridY;
			aGridArray[aGridArrayCount].mWeight = 10000;
			aGridArrayCount++;
			TOD_ASSERT(aGridArrayCount <= MAX_POOL_GRID_SIZE);
		}
	}

	if (aGridArrayCount < 0)
	{
		aGridArrayCount = 0;
	}
	for (int i = 0; i < aCount; i++)
	{
		TodWeightedGridArray* aGrid = TodPickFromWeightedGridArray(aGridArray, aGridArrayCount);
		aGrid->mWeight = 0;

		ZombieType aZombieType = PickGraveRisingZombieType(aZombiePoints);
		Zombie* aZombie = AddZombieInRow(aZombieType, aGrid->mY, mCurrentWave, true);
		if (aZombie == nullptr)
		{
			return;
		}

		aZombie->RiseFromGrave(aGrid->mX, aGrid->mY);
		aZombiePoints -= GetZombieDefinition(aZombieType).mZombieValue;
		if (aZombiePoints < 1)
		{
			aZombiePoints = 1;
		}
	}
}

void Board::SetupBungeeDrop(BungeeDropGrid* theBungeeDropGrid)
{
	theBungeeDropGrid->mGridArrayCount = 0;
	for (int aGridX = 4; aGridX < MAX_GRID_SIZE_X; aGridX++)
	{
		for (int aGridY = 0; aGridY <= 4; aGridY++)
		{
			int aCount = theBungeeDropGrid->mGridArrayCount;
			theBungeeDropGrid->mGridArray[aCount].mX = aGridX;
			theBungeeDropGrid->mGridArray[aCount].mY = aGridY;
			theBungeeDropGrid->mGridArray[aCount].mWeight = 10000;
			theBungeeDropGrid->mGridArrayCount++;
			TOD_ASSERT(theBungeeDropGrid->mGridArrayCount <= LENGTH(theBungeeDropGrid->mGridArray));
		}
	}
}

void Board::BungeeDropZombie(BungeeDropGrid* theBungeeDropGrid, ZombieType theZombieType)
{
	TodWeightedGridArray* aGrid = TodPickFromWeightedGridArray(theBungeeDropGrid->mGridArray, theBungeeDropGrid->mGridArrayCount);
	aGrid->mWeight = 1;

	// Resolve both rows before creating either zombie so an unavailable row cannot
	// leave an unattached Bungee zombie behind. PickRowForNewZombie returns -1
	// when the requested zombie has no positive-weight spawn row.
	int aBungeeRow = PickRowForNewZombie(ZombieType::ZOMBIE_BUNGEE);
	int aZombieRow = PickRowForNewZombie(theZombieType);
	if (aBungeeRow < 0 || aZombieRow < 0)
	{
		return;
	}

	Zombie* aBungeeZombie = AddZombieInRow(ZombieType::ZOMBIE_BUNGEE, aBungeeRow, mCurrentWave, false);
	Zombie* aZombie = AddZombieInRow(theZombieType, aZombieRow, mCurrentWave, false);
	if (!aBungeeZombie || !aZombie)
	{
		return;
	}

	aBungeeZombie->BungeeDropZombie(aZombie, aGrid->mX, aGrid->mY);
}

void Board::SpawnZombiesFromSky()
{
	if (mIceTrapCounter > 0)
		return;

	int aCount, aZombiePoints;
	if (mLevel == 41 || mLevel == 42)
	{
		aCount = 2;
		aZombiePoints = 3;
	}
	else if (mLevel == 43 || mLevel == 44 || mLevel == 45)
	{
		aCount = 3;
		aZombiePoints = 5;
	}
	else
	{
		aCount = 3;
		aZombiePoints = 7;
	}
	
	BungeeDropGrid aBungeeDropGrid;
	SetupBungeeDrop(&aBungeeDropGrid);
	if (aCount > aBungeeDropGrid.mGridArrayCount)
	{
		aCount = aBungeeDropGrid.mGridArrayCount;
	}

	if (aBungeeDropGrid.mGridArrayCount == 0 || aCount <= 0)
		return;

	for (int i = 0; i < aCount; i++)
	{
		ZombieType aZombieType = PickGraveRisingZombieType(aZombiePoints);
		BungeeDropZombie(&aBungeeDropGrid, aZombieType);
		aZombiePoints -= GetZombieDefinition(aZombieType).mZombieValue;
		if (aZombiePoints < 1)
		{
			aZombiePoints = 1;
		}
	}
}

void Board::SpawnZombiesFromGraves()
{
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_WAR_AND_PEAS || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_WAR_AND_PEAS_2)
		return;

	if (StageHasRoof())
	{
		SpawnZombiesFromSky();
	}
	else if (StageHasPool())
	{
		SpawnZombiesFromPool();
		return;
	}
	
	int aZombiePoints = GetGraveStonesCount();
	GridItem* aGridItem = nullptr;
	while (IterateGridItems(aGridItem))
	{
		if (aGridItem->mGridItemType != GridItemType::GRIDITEM_GRAVESTONE || aGridItem->mGridItemCounter < 100)
		{
			continue;
		}
		if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_GRAVE_DANGER && Rand(mNumWaves) > mCurrentWave)
		{
			continue;
		}
		
		ZombieType aZombieType = PickGraveRisingZombieType(aZombiePoints);
		Zombie* aZombie = AddZombie(aZombieType, mCurrentWave, true);
		if (aZombie == nullptr)
		{
			return;
		}

		aZombie->RiseFromGrave(aGridItem->mGridX, aGridItem->mGridY);
		aZombiePoints -= GetZombieDefinition(aZombieType).mZombieValue;
		if (aZombieType < 1)
		{
			aZombiePoints = 1;
		}
	}
}

int Board::TotalZombiesHealthInWave(int theWaveIndex)
{
	int aTotalHealth = 0;
	Zombie* aZombie = nullptr;
	while (IterateZombies(aZombie))
	{
		if (aZombie->mFromWave == theWaveIndex && !aZombie->mMindControlled && !aZombie->IsDeadOrDying() &&
			aZombie->mZombieType != ZombieType::ZOMBIE_BUNGEE && aZombie->mRelatedZombieID == ZombieID::ZOMBIEID_NULL)
		{
			aTotalHealth += aZombie->mBodyHealth + aZombie->mHelmHealth + aZombie->mShieldHealth * 0.2f + aZombie->mFlyingHealth;
		}
	}
	return aTotalHealth;
}

void Board::SpawnZombieWave()
{
	if (IsRollSeedBank())
	{
		OnRollWaveStart();
	}

	mChallenge->SpawnZombieWave();
	if (mApp->IsBungeeBlitzLevel())
	{
		BungeeDropGrid aBungeeDropGrid;
		SetupBungeeDrop(&aBungeeDropGrid);
		for (int i = 0; i < MAX_ZOMBIES_IN_WAVE; i++)
		{
			ZombieType aZombieType = mZombiesInWave[mCurrentWave][i];
			if (aZombieType == ZombieType::ZOMBIE_INVALID)
				break;

			if (aZombieType == ZombieType::ZOMBIE_BUNGEE || aZombieType == ZombieType::ZOMBIE_ZAMBONI)
			{
				AddZombie(aZombieType, mCurrentWave);
			}
			else
			{
				BungeeDropZombie(&aBungeeDropGrid, aZombieType);
			}
		}
	}
	else
	{
		TOD_ASSERT(mCurrentWave >= 0 && mCurrentWave < MAX_ZOMBIE_WAVES && mCurrentWave < mNumWaves);
		for (int i = 0; i < MAX_ZOMBIES_IN_WAVE; i++)
		{
			ZombieType aZombieType = mZombiesInWave[mCurrentWave][i];
			if (aZombieType == ZombieType::ZOMBIE_INVALID)
				break;

			if (aZombieType == ZombieType::ZOMBIE_BOBSLED && !CanAddBobSled())
			{
				for (int i = 0; i < MAX_ZOMBIE_FOLLOWERS; i++)
				{
					AddZombie(ZombieType::ZOMBIE_NORMAL, mCurrentWave);  
				}
			}
			else
			{
				AddZombie(aZombieType, mCurrentWave);
			}
		}
	}

	if (mCurrentWave == mNumWaves - 1 && !mApp->IsContinuousChallenge())
	{
		mRiseFromGraveCounter = 210;
	}
	if (IsFlagWave(mCurrentWave))
	{
		mFlagRaiseCounter = FLAG_RAISE_TIME;
	}
	mCurrentWave++;
	mTotalSpawnedWaves++;
}

void Board::UpdateGameObjects()
{
    if (mHologramCooldown > 0)
        --mHologramCooldown;
    if (mHologramEnabled)
    {
        mHologramPacket.mPacketType = mHologramLastPlant;
        mHologramPacket.mImitaterType = SeedType::SEED_NONE;
        mHologramPacket.mRefreshTime = 6000;
        mHologramPacket.mRefreshCounter = 6000 - mHologramCooldown;
        mHologramPacket.mActive = mHologramLastPlant != SeedType::SEED_NONE && mHologramCooldown <= 0 && !mHologramPlanting;
        // While the copied plant is held by the cursor, use SeedPacket's native
        // dark overlay. A zero refresh counter means fully dark when mActive is false.
        // This is display state only; it must not start or advance the real cooldown.
        if (mHologramPlanting && mHologramCooldown <= 0)
            mHologramPacket.mRefreshCounter = 0;
        // Recharging is a property of the Hologram itself, not of the copied plant.
        // Keep the native SeedPacket cooldown overlay running even when its slot is empty.
        mHologramPacket.mRefreshing = mHologramCooldown > 0;
        if (mHologramLastPlant == SeedType::SEED_NONE && mHologramCooldown <= 0)
        {
            // No copied plant and no pending recharge: show an ordinary empty packet.
            mHologramPacket.mRefreshCounter = mHologramPacket.mRefreshTime;
            mHologramPacket.mActive = false;
            mHologramPacket.mRefreshing = false;
        }
    }
    if (IsRollSeedBank())
    {
        FlushAugmentSeedRewards();
        UpdateAugmentFire();
    }
    RefreshFlagAura();
	Plant* aPlant = nullptr;
	while (IteratePlants(aPlant))
	{
		aPlant->Update();
	}

	Zombie* aZombie = nullptr;
	while (IterateZombies(aZombie))
	{
		aZombie->Update();
	}

    UpdateNextWaveIncomeForecast();

	Projectile* aProjectile = nullptr;
	while (IterateProjectiles(aProjectile))
	{
		aProjectile->Update();
	}

	Coin* aCoin = nullptr;
	while (IterateCoins(aCoin))
	{
		aCoin->Update();
	}

	LawnMower* aLawnMower = nullptr;
	while (IterateLawnMowers(aLawnMower))
	{
		aLawnMower->Update();
	}

	Bush* aBush = nullptr;
	while (IterateBushes(aBush))
	{
		aBush->Update();
	}

	mCursorPreview->Update();
	mCursorObject->Update();

	for (int i = 0; i < mSeedBank->mNumPackets; i++)
	{
		mSeedBank->mSeedPackets[i].Update();
	}
    UpdateBenchPackets();
}

void Board::StopAllZombieSounds()
{
	Zombie* aZombie = nullptr;
	while (IterateZombies(aZombie))
	{
		aZombie->StopZombieSound();
	}
}

int Board::GetSurvivalFlagsCompleted()
{
	int aWavesPerFlag = GetNumWavesPerFlag();
	int aFlagsCompleted = mChallenge->mSurvivalStage * GetNumWavesPerSurvivalStage() / aWavesPerFlag;
	int aCurrentWave = mCurrentWave;
	if (IsFlagWave(aCurrentWave - 1) && mBoardFadeOutCounter < 0 && !mNextSurvivalStageCounter)
	{
		aCurrentWave -= 1;
	}
	return aCurrentWave / aWavesPerFlag + aFlagsCompleted;
}

void Board::SurvivalSaveScore()
{
	if (!mApp->IsSurvivalMode())
		return;

	int aFlagsCompleted = GetSurvivalFlagsCompleted();
	int& aFlagsRecord = mApp->mPlayerInfo->mChallengeRecords[mApp->GetCurrentChallengeIndex()];
	if (aFlagsCompleted > aFlagsRecord)
	{
		aFlagsRecord = aFlagsCompleted;
		mApp->WriteCurrentUserConfig();
	}
}

void Board::PuzzleSaveStreak()
{
	if (!mApp->IsEndlessIZombie(mApp->mGameMode) && !mApp->IsEndlessScaryPotter(mApp->mGameMode))
		return;

	int aStreak = mChallenge->mSurvivalStage + 1;
	int& aRecord = mApp->mPlayerInfo->mChallengeRecords[mApp->GetCurrentChallengeIndex()];
	if (aStreak > aRecord)
	{
		aRecord = aStreak;
		mApp->WriteCurrentUserConfig();
	}
}

void Board::ZombiesWon(Zombie* theZombie)
{
	mApp->mIsFastMode = false;
	if (mApp->mGameScene == GameScenes::SCENE_ZOMBIES_WON)
		return;

	ClearAdvice(AdviceType::ADVICE_NONE);
	mApp->mBoardResult = BoardResult::BOARDRESULT_LOST;

	Zombie* aZombie = nullptr;
	while (IterateZombies(aZombie))
	{
		if (aZombie == theZombie)
			continue;

		if (aZombie->GetZombieRect().mX < -50 || 
			aZombie->mZombiePhase == ZombiePhase::PHASE_RISING_FROM_GRAVE || 
			aZombie->mZombiePhase == ZombiePhase::PHASE_DANCER_RISING)
		{
			if ((aZombie->mZombieType == ZombieType::ZOMBIE_GARGANTUAR || aZombie->mZombieType == ZombieType::ZOMBIE_REDEYE_GARGANTUAR) && 
				aZombie->IsDeadOrDying() && aZombie->mPosX < 140 + BOARD_ADDITIONAL_WIDTH)
			{
				aZombie->DieNoLoot();
			}
		}
	}
	SurvivalSaveScore();

	SexyString aGameOverMsg;
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZOMBIQUARIUM)
	{
		aGameOverMsg = _S("[ZOMBIQUARIUM_DEATH_MESSAGE]");
	}
	else if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND)
	{
		SexyString aFlagStr = mApp->Pluralize(GetSurvivalFlagsCompleted(), _S("[ONE_FLAG]"), _S("[COUNT_FLAGS]"));
		aGameOverMsg = TodReplaceString(_S("[LAST_STAND_DEATH_MESSAGE]"), _S("{FLAGS}"), aFlagStr);
	}
	else if (mApp->IsEndlessIZombie(mApp->mGameMode) || mApp->IsEndlessScaryPotter(mApp->mGameMode))
	{
		aGameOverMsg = TodReplaceNumberString(_S("[ENDLESS_PUZZLE_DEATH_MESSAGE]"), _S("{STREAK}"), mChallenge->mSurvivalStage);
	}
	else if (mApp->IsIZombieLevel())
	{
		aGameOverMsg = _S("[I_ZOMBIE_DEATH_MESSAGE]");
	}
	else
	{
		mApp->mGameScene = GameScenes::SCENE_ZOMBIES_WON;
		if (theZombie)  
		{
			theZombie->WalkIntoHouse();
		}

		ClearAdvice(AdviceType::ADVICE_NONE);
		mCutScene->StartZombiesWon();
		FreezeEffectsForCutscene(true);
		TutorialArrowRemove();
		UpdateCursor();
		return;
	}

	GameOverDialog* aGameOverDialog = new GameOverDialog(aGameOverMsg, true);
	mApp->AddDialog(Dialogs::DIALOG_GAME_OVER, aGameOverDialog);

	mApp->mMusic->StopAllMusic();
	StopAllZombieSounds();
	mApp->PlaySample(Sexy::SOUND_LOSEMUSIC);

	ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_ZOMBIES_WON, true);
	Reanimation* aReanim = mApp->AddReanimation(-BOARD_OFFSET_X, 0, MakeRenderOrder(RenderLayer::RENDER_LAYER_SCREEN_FADE, 0, 0), ReanimationType::REANIM_ZOMBIES_WON);
	aReanim->mLoopType = ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD;
	aReanim->GetTrackInstanceByName("fullscreen")->mTrackColor = Color::Black;
	aReanim->SetFramesForLayer("anim_screen");
}

bool Board::IsFinalScaryPotterStage()
{
	if (!mApp->IsScaryPotterLevel())
		return false;

	if (mApp->IsAdventureMode())
	{
		return mChallenge->mSurvivalStage == 2;
	}
	
	return !mApp->IsEndlessScaryPotter(mApp->mGameMode);
}

bool Board::IsFinalSurvivalStage()
{
	if (!mApp->IsSurvivalMode())
		return false;

	int aFlags = GetNumWavesPerSurvivalStage() * (mChallenge->mSurvivalStage + 1) / GetNumWavesPerFlag();
	if (mApp->IsSurvivalNormal(mApp->mGameMode))
	{
		return aFlags >= 5;
	}
	if (mApp->IsSurvivalHard(mApp->mGameMode))
	{
		return aFlags >= 10;
	}

	return false;
}

bool Board::IsLastStandFinalStage()
{
	return mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND && mChallenge->mSurvivalStage == LAST_STAND_FLAGS - 1;
}

bool Board::IsSurvivalStageWithRepick()
{
	return mApp->IsSurvivalMode() && !IsFinalSurvivalStage();
}

bool Board::IsLastStandStageWithRepick()
{
	return mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND && !IsLastStandFinalStage();
}

bool Board::HasLevelAwardDropped()
{
	return mLevelAwardSpawned || mNextSurvivalStageCounter > 0 || mBoardFadeOutCounter >= 0;
}

void Board::UpdateSunSpawning()
{
	if (IsRollSeedBank() ||		// roll levels get a fixed amount of Sun every wave instead (DropWaveSun)
		StageIsNight() || 
		HasLevelAwardDropped() || 
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_RAINING_SEEDS || 
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ICE || 
		mApp->mGameMode == GameMode::GAMEMODE_UPSELL ||
		mApp->mGameMode == GameMode::GAMEMODE_INTRO || 
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZOMBIQUARIUM || 
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN ||
		mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM || 
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND || 
		mApp->IsIZombieLevel() ||
		mApp->IsScaryPotterLevel() || 
		mApp->IsSquirrelLevel() || 
		HasConveyorBeltSeedBank() || 
		mTutorialState == TutorialState::TUTORIAL_SLOT_MACHINE_PULL)
		return;

	if (mTutorialState == TutorialState::TUTORIAL_LEVEL_1_PICK_UP_PEASHOOTER || mTutorialState == TutorialState::TUTORIAL_LEVEL_1_PLANT_PEASHOOTER)
	{
		if (mPlants.mSize == 0)
		{
			return;
		}
	}

	mSunCountDown--;
	if (mSunCountDown != 0)
		return;

	mNumSunsFallen++;
	mSunCountDown = min(SUN_COUNTDOWN_MAX, SUN_COUNTDOWN + mNumSunsFallen * 10) + Rand(SUN_COUNTDOWN_RANGE);
	CoinType aSunType = mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_SUNNY_DAY ? CoinType::COIN_LARGESUN : CoinType::COIN_SUN;
	AddCoin(RandRangeInt(100 + BOARD_ADDITIONAL_WIDTH, 649 + BOARD_ADDITIONAL_WIDTH), 60, aSunType, CoinMotion::COIN_MOTION_FROM_SKY);
}

void Board::NextWaveComing()
{
	if (mCurrentWave + 1 == mNumWaves)
	{
		if (!IsSurvivalStageWithRepick() && mApp->mGameMode != GameMode::GAMEMODE_CHALLENGE_LAST_STAND && !mApp->IsContinuousChallenge())
		{
			mApp->AddReanimation(BOARD_ADDITIONAL_WIDTH, BOARD_OFFSET_Y, MakeRenderOrder(RenderLayer::RENDER_LAYER_ABOVE_UI, 0, 0), ReanimationType::REANIM_FINAL_WAVE);
			mFinalWaveSoundCounter = 60;
		}
	}

	if (mCurrentWave == 0)
	{
		mApp->PlaySample(Sexy::SOUND_AWOOGA);
	}
	else if ((mApp->IsWhackAZombieLevel() && mCurrentWave == mNumWaves - 1) || IsFlagWave(mCurrentWave))
	{
		mApp->PlaySample(Sexy::SOUND_SIREN);
	}
}

void Board::UpdateZombieSpawning()
{
	if (mApp->mGameMode == GameMode::GAMEMODE_UPSELL || mApp->mGameMode == GameMode::GAMEMODE_INTRO)
		return;

	if (IsRollTutorialActive())
		return;		// no zombies while the roll tutorial is explaining the shop

	if (mFinalWaveSoundCounter > 0)
	{
		mFinalWaveSoundCounter--;
		if (mFinalWaveSoundCounter == 0)
		{
			mApp->PlaySample(Sexy::SOUND_FINALWAVE);
		}
	}

	if (mTutorialState == TutorialState::TUTORIAL_LEVEL_1_PICK_UP_PEASHOOTER || 
		mTutorialState == TutorialState::TUTORIAL_LEVEL_1_PLANT_PEASHOOTER ||
		mTutorialState == TutorialState::TUTORIAL_LEVEL_1_REFRESH_PEASHOOTER || 
		mTutorialState == TutorialState::TUTORIAL_SLOT_MACHINE_PULL)
		return;

	if (HasLevelAwardDropped())
		return;

	if (mRiseFromGraveCounter > 0)
	{
		mRiseFromGraveCounter--;
		if (mRiseFromGraveCounter == 0)
		{
			SpawnZombiesFromGraves();
		}
	}

	if (mHugeWaveCountDown > 0)
	{
		mHugeWaveCountDown--;
		if (mHugeWaveCountDown == 0)
		{
			ClearAdvice(AdviceType::ADVICE_HUGE_WAVE);
			NextWaveComing();
			mZombieCountDown = 1;
		}
		else
		{
			if (mHugeWaveCountDown == 725)
			{
				mApp->PlaySample(Sexy::SOUND_HUGE_WAVE);
			}
			else
			{
				if (mApp->mMusic->mCurMusicTune == MusicTune::MUSIC_TUNE_DAY_GRASSWALK || 
					mApp->mMusic->mCurMusicTune == MusicTune::MUSIC_TUNE_POOL_WATERYGRAVES || 
					mApp->mMusic->mCurMusicTune == MusicTune::MUSIC_TUNE_FOG_RIGORMORMIST ||
					mApp->mMusic->mCurMusicTune == MusicTune::MUSIC_TUNE_ROOF_GRAZETHEROOF)
				{
					if (mHugeWaveCountDown == 400)
					{
						mApp->mMusic->StartBurst();
					}
				}
				else if (mApp->mMusic->mCurMusicTune == MusicTune::MUSIC_TUNE_NIGHT_MOONGRAINS)
				{
					if (mHugeWaveCountDown == 700)
					{
						mApp->mMusic->StartBurst();
					}
				}
				return;
			}
		}
	}

	if (mChallenge->UpdateZombieSpawning())
		return;

	if (mCurrentWave == mNumWaves)
	{
		if (IsFinalSurvivalStage())
		{
			return;
		}
		if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND)
		{
			return;
		}
		if (!mApp->IsSurvivalMode() && !mApp->IsContinuousChallenge())
		{
			return;
		}
	}

	mZombieCountDown--;
	if (mCurrentWave == mNumWaves && mApp->IsSurvivalMode())
	{
		if (mZombieCountDown == 0)
		{
			FadeOutLevel();
		}
		return;
	}

	if (mZombieCountDown > 200 && mZombieCountDownStart - mZombieCountDown > 400 && TotalZombiesHealthInWave(mCurrentWave - 1) <= mZombieHealthToNextWave)
	{
		mZombieCountDown = 200;
	}
	if (mZombieCountDown == 5)
	{
		if (IsFlagWave(mCurrentWave))
		{
			ClearAdviceImmediately();
			DisplayAdviceAgain(_S("[ADVICE_HUGE_WAVE]"), MessageStyle::MESSAGE_STYLE_HUGE_WAVE, AdviceType::ADVICE_HUGE_WAVE);
			mHugeWaveCountDown = 750;
			return;
		}
		NextWaveComing();
	}
	if (mZombieCountDown == 0)
	{
		if (ShouldOfferAugment())
		{
			// wave 3 is about to start: hold it back until the player has picked an augment
			StartAugmentSelection();
			mZombieCountDown = 1;
			return;
		}

		SpawnZombieWave();
		mZombieHealthWaveStart = TotalZombiesHealthInWave(mCurrentWave - 1);

		if (mCurrentWave == mNumWaves && mApp->IsSurvivalMode())
		{
			mZombieHealthToNextWave = 0;
			mZombieCountDown = ZOMBIE_COUNTDOWN_BEFORE_REPICK + 1;
		}
		else if (IsFlagWave(mCurrentWave) && (mApp->IsWallnutBowlingLevel() || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND))
		{
			mZombieHealthToNextWave = 0;
			mZombieCountDown = ZOMBIE_COUNTDOWN_BEFORE_FLAG;
		}
		else
		{
			mZombieHealthToNextWave = RandRangeFloat(0.5f, 0.65f) * mZombieHealthWaveStart;
			if (mApp->IsLittleTroubleLevel() || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_COLUMN || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND)
			{
				mZombieCountDown = 750;
			}
			else
			{
				mZombieCountDown = ZOMBIE_COUNTDOWN + Rand(ZOMBIE_COUNTDOWN_RANGE);
			}
		}
		mZombieCountDownStart = mZombieCountDown;
	}
}

void Board::UpdateIce()
{
	for (int aRow = 0; aRow < MAX_GRID_SIZE_Y; aRow++)
	{
		if (mIceTimer[aRow])
		{
			mIceTimer[aRow]--;
			TodParticleSystem* aParticleIce = mApp->ParticleTryToGet(mIceParticleID[aRow]);
			if (mIceTimer[aRow] == 0)
			{
				mIceMinX[aRow] = BOARD_ICE_START;
				if (aParticleIce)
				{
					aParticleIce->ParticleSystemDie();
				}
			}
			else
			{
				float aPosX = mIceMinX[aRow];
				float aPosY = GridToPixelY(8, aRow);
				if (aParticleIce)
				{
					aParticleIce->SystemMove(aPosX, aPosY);
				}
				else
				{
					int aRenderPosition = MakeRenderOrder(RenderLayer::RENDER_LAYER_GROUND, aRow, 3);
					aParticleIce = mApp->AddTodParticle(aPosX, aPosY, aRenderPosition, ParticleEffect::PARTICLE_ICE_SPARKLE);
					mIceParticleID[aRow] = mApp->ParticleGetID(aParticleIce);
				}
			}

			int anAlpha = ClampInt(mIceTimer[aRow] / 10, 0, 255);
			aParticleIce->OverrideColor(nullptr, Color(255, 255, 255, anAlpha));
		}
	}
}

void Board::UpdateProgressMeter()
{
	if (mApp->IsFinalBossLevel())
	{
		Zombie* aBoss = GetBossZombie();
		if (aBoss && !aBoss->IsDeadOrDying())
		{
			mProgressMeterWidth = 150 * (aBoss->mBodyMaxHealth - aBoss->mBodyHealth) / aBoss->mBodyMaxHealth;
		}
		else
		{
			mProgressMeterWidth = 150;
		}
	}
	else if (mCurrentWave != 0)
	{
		if (mFlagRaiseCounter > 0)
			mFlagRaiseCounter--;

		int aTotalWidth = 150;  
		int aNumWavesPerFlag = GetNumWavesPerFlag();  
		bool aHasFlags = ProgressMeterHasFlags();  
		if (aHasFlags)
		{
			aTotalWidth -= 12 * mNumWaves / aNumWavesPerFlag;  
		}

		int aWaveLength = aTotalWidth / (mNumWaves - 1);  
		int aCurrentWaveLength = (mCurrentWave - 1) * aTotalWidth / (mNumWaves - 1);  
		int aNextWaveLength = mCurrentWave * aTotalWidth / (mNumWaves - 1);  
		if (aHasFlags)
		{
			int anExtraLength = mCurrentWave / aNumWavesPerFlag * 12;  
			aCurrentWaveLength += anExtraLength;
			aNextWaveLength += anExtraLength;
		}

		float aFraction = (mZombieCountDownStart - mZombieCountDown) / (float)mZombieCountDownStart;
		if (mZombieHealthToNextWave != -1)
		{
			int aHealthCurrent = TotalZombiesHealthInWave(mCurrentWave - 1);
			int aDamageTarget = mZombieHealthWaveStart - mZombieHealthToNextWave;  
			if (aDamageTarget < 1)
			{
				aDamageTarget = 1;  
			}
			float aHealthFraction = (aDamageTarget - aHealthCurrent + mZombieHealthToNextWave) / (float)aDamageTarget;
			aFraction = max(aHealthFraction, aFraction);
		}

		int aLength = ClampInt(aCurrentWaveLength + FloatRoundToInt((aNextWaveLength - aCurrentWaveLength) * aFraction), 1, 150);
		int aDelta = aLength - mProgressMeterWidth;
		if ((aDelta > aWaveLength && (mMainCounter % 5 == 0)) || (aDelta > 0 && (mMainCounter % 20 == 0)))
		{
			mProgressMeterWidth++;
		}
	}
}

void Board::UpdateTutorial()
{
	// Replace only the default introductory tutorials on Adventure 1-1 and 1-2
	// when the Roll Shop is active. Other Adventure tutorials (notably the shovel
	// tutorial on 1-5) must continue using the stock tutorial state machine.
	if (mApp->IsAdventureMode() && !mApp->mPlayingQuickplay && IsRollSeedBank() && mLevel <= 2)
	{
		if (mTutorialState != TutorialState::TUTORIAL_OFF)
		{
			mTutorialState = TutorialState::TUTORIAL_OFF;
			mTutorialTimer = -1;
			TutorialArrowRemove();
		}
		return;
	}

    // Clear the obsolete three-Sunflower tutorial when loading an in-progress
    // level that already entered it; otherwise its blinking arrow can persist.
    if (mTutorialState == TutorialState::TUTORIAL_MORESUN_PICK_UP_SUNFLOWER ||
        mTutorialState == TutorialState::TUTORIAL_MORESUN_PLANT_SUNFLOWER ||
        mTutorialState == TutorialState::TUTORIAL_MORESUN_REFRESH_SUNFLOWER)
    {
        TutorialArrowRemove();
        ClearAdvice(AdviceType::ADVICE_PLANT_SUNFLOWER5);
        mTutorialState = TutorialState::TUTORIAL_MORESUN_COMPLETED;
        mTutorialTimer = -1;
    }

	if (mTutorialTimer > 0)
		mTutorialTimer--;

	if (mTutorialState == TutorialState::TUTORIAL_LEVEL_1_PICK_UP_PEASHOOTER && mTutorialTimer == 0)
	{
		DisplayAdvice(_S("[ADVICE_CLICK_PEASHOOTER]"), MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1_STAY, AdviceType::ADVICE_NONE);
		TutorialArrowShow(mSeedBank->mX + mSeedBank->mSeedPackets[0].mX, mSeedBank->mY + mSeedBank->mSeedPackets[0].mY);
		mTutorialTimer = -1;
	}
	else if (mTutorialState == TutorialState::TUTORIAL_LEVEL_2_PICK_UP_SUNFLOWER || 
		mTutorialState == TutorialState::TUTORIAL_LEVEL_2_PLANT_SUNFLOWER ||
		mTutorialState == TutorialState::TUTORIAL_LEVEL_2_REFRESH_SUNFLOWER)
	{
		if (mTutorialTimer == 0)
		{
			DisplayAdvice(_S("[ADVICE_PLANT_SUNFLOWER2]"), MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL2, AdviceType::ADVICE_NONE);
			mTutorialTimer = -1;
		}
		else if (mZombieCountDown == 750 && mCurrentWave == 0)
		{
			DisplayAdvice(_S("[ADVICE_PLANT_SUNFLOWER3]"), MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL2, AdviceType::ADVICE_NONE);
		}
	}
	else if (mTutorialState == TutorialState::TUTORIAL_MORESUN_PICK_UP_SUNFLOWER || 
		mTutorialState == TutorialState::TUTORIAL_MORESUN_PLANT_SUNFLOWER ||
		mTutorialState == TutorialState::TUTORIAL_MORESUN_REFRESH_SUNFLOWER)
	{
		if (mTutorialTimer == 0)
		{
			// Legacy 3-Sunflower reminder removed.
			mTutorialTimer = -1;
		}
	}

    // The legacy "plant at least three Sunflowers" tip is disabled. Roll Shop's
    // separate 2-star Sunflower bench-merge tutorial remains unchanged.
}

void Board::SetTutorialState(TutorialState theTutorialState)
{
	// Keep the custom Roll Shop tutorial isolated to the two introductory levels.
	// Later Adventure tutorials still need their normal state transitions.
	if (mApp->IsAdventureMode() && !mApp->mPlayingQuickplay && IsRollSeedBank() && mLevel <= 2)
	{
		mTutorialState = TutorialState::TUTORIAL_OFF;
		mTutorialTimer = -1;
		TutorialArrowRemove();
		return;
	}
	switch (theTutorialState)
	{
	case TutorialState::TUTORIAL_LEVEL_1_PICK_UP_PEASHOOTER:
		if (mPlants.mSize == 0)
		{
			float aPosX = mSeedBank->mX + mSeedBank->mSeedPackets[0].mX;
			float aPosY = mSeedBank->mY + mSeedBank->mSeedPackets[0].mY;
			TutorialArrowShow(aPosX, aPosY);
			DisplayAdvice(_S("[ADVICE_CLICK_SEED_PACKET]"), MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1_STAY, AdviceType::ADVICE_NONE);
		}
		else
		{
			DisplayAdvice(_S("[ADVICE_ENOUGH_SUN]"), MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1_STAY, AdviceType::ADVICE_NONE);
			mTutorialTimer = 400;
		}
		break;

	case TutorialState::TUTORIAL_LEVEL_1_PLANT_PEASHOOTER:
		mTutorialTimer = -1;
		TutorialArrowRemove();
		if (mPlants.mSize == 0)
		{
			DisplayAdvice(_S("[ADVICE_CLICK_ON_GRASS]"), MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1_STAY, AdviceType::ADVICE_NONE);
		}
		else
		{
			ClearAdvice(AdviceType::ADVICE_NONE);
		}
		break;

	case TutorialState::TUTORIAL_LEVEL_1_REFRESH_PEASHOOTER:
		DisplayAdvice(_S("[ADVICE_PLANTED_PEASHOOTER]"), MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1_STAY, AdviceType::ADVICE_NONE);
		mSunCountDown = 400;
		break;

	case TutorialState::TUTORIAL_LEVEL_1_COMPLETED:
		DisplayAdvice(_S("[ADVICE_ZOMBIE_ONSLAUGHT]"), MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1, AdviceType::ADVICE_NONE);
		mZombieCountDown = 99;
		mZombieCountDownStart = mZombieCountDown;
		break;

	case TutorialState::TUTORIAL_LEVEL_2_PICK_UP_SUNFLOWER:
	case TutorialState::TUTORIAL_MORESUN_PICK_UP_SUNFLOWER:
		TutorialArrowShow(mSeedBank->mX + mSeedBank->mSeedPackets[1].mX, mSeedBank->mY + mSeedBank->mSeedPackets[1].mY);
		break;

	case TutorialState::TUTORIAL_LEVEL_2_PLANT_SUNFLOWER:
	case TutorialState::TUTORIAL_LEVEL_2_REFRESH_SUNFLOWER:
	case TutorialState::TUTORIAL_MORESUN_PLANT_SUNFLOWER:
	case TutorialState::TUTORIAL_MORESUN_REFRESH_SUNFLOWER:
		TutorialArrowRemove();
		break;

	case TutorialState::TUTORIAL_LEVEL_2_COMPLETED:
		if (mCurrentWave == 0)
		{
			mZombieCountDown = 999;
			mZombieCountDownStart = mZombieCountDown;
		}
		break;

	case TutorialState::TUTORIAL_SLOT_MACHINE_PULL:
		DisplayAdvice(_S("[ADVICE_SLOT_MACHINE_PULL]"), MessageStyle::MESSAGE_STYLE_SLOT_MACHINE, AdviceType::ADVICE_SLOT_MACHINE_PULL);
		break;

	case TutorialState::TUTORIAL_SLOT_MACHINE_COMPLETED:
		ClearAdvice(AdviceType::ADVICE_SLOT_MACHINE_PULL);
		break;

	case TutorialState::TUTORIAL_SHOVEL_PICKUP:
	{
		DisplayAdvice(_S("[ADVICE_CLICK_SHOVEL]"), MessageStyle::MESSAGE_STYLE_HINT_STAY, AdviceType::ADVICE_NONE);
		Rect aShovelButtonRect = GetShovelButtonRect();
		int aPosX = aShovelButtonRect.mX + aShovelButtonRect.mWidth / 2 - 25;
		int aPosY = aShovelButtonRect.mY + aShovelButtonRect.mHeight - 65;
		TutorialArrowShow(aPosX, aPosY);
		break;
	}

	case TutorialState::TUTORIAL_SHOVEL_DIG:
		DisplayAdvice(_S("[ADVICE_CLICK_PLANT]"), MessageStyle::MESSAGE_STYLE_HINT_STAY, AdviceType::ADVICE_NONE);
		TutorialArrowRemove();
		break;

	case TutorialState::TUTORIAL_SHOVEL_KEEP_DIGGING:
		DisplayAdvice(_S("[ADVICE_KEEP_DIGGING]"), MessageStyle::MESSAGE_STYLE_HINT_STAY, AdviceType::ADVICE_NONE);
		break;

	case TutorialState::TUTORIAL_SHOVEL_COMPLETED:
		ClearAdvice(AdviceType::ADVICE_NONE);
		mCutScene->mCutsceneTime = 1500;
		mCutScene->mCrazyDaveDialogStart = 2410;
		break;
	}

	mTutorialState = theTutorialState;
}

void Board::UpdateGame()
{
	UpdateGameObjects();
	if (StageHasFog() && mFogBlownCountDown > 0)
	{
		float aMaxFogOffset = 1065.0f - LeftFogColumn() * 70.0f;
		if (mApp->mGameScene == GameScenes::SCENE_LEVEL_INTRO)
		{
			mFogOffset = TodAnimateCurveFloat(200, 0, mFogBlownCountDown, aMaxFogOffset, 0, TodCurves::CURVE_EASE_OUT);
		}
		else if (mFogBlownCountDown < 2000)
		{
			mFogOffset = TodAnimateCurveFloat(2000, 0, mFogBlownCountDown, aMaxFogOffset, 0, TodCurves::CURVE_EASE_OUT);
		}
		else if (mFogOffset < aMaxFogOffset)
		{
			mFogOffset = TodAnimateCurveFloat(-5, aMaxFogOffset, mFogOffset * 1.1f, 0, aMaxFogOffset, TodCurves::CURVE_LINEAR);
		}
	}

	if (mApp->mGameScene != GameScenes::SCENE_PLAYING && !mCutScene->ShouldRunUpsellBoard())
		return;

	mMainCounter++;
    FlushPendingAugmentSunDrops();
	UpdateSunSpawning();
	UpdateZombieSpawning();
	UpdateIce();
	if (mIceTrapCounter > 0)
	{
		mIceTrapCounter--;
		if (mIceTrapCounter == 0)
		{
			TodParticleSystem* aPoolSparklyParticle = mApp->ParticleTryToGet(mPoolSparklyParticleID);
			if (aPoolSparklyParticle)
			{
				aPoolSparklyParticle->mDontUpdate = false;
			}
		}
	}

	if (mFogBlownCountDown > 0)
	{
		mFogBlownCountDown--;
	}

	if (mMainCounter == 1 && mApp->IsFirstTimeAdventureMode() && !(mApp->IsAdventureMode() && !mApp->mPlayingQuickplay && IsRollSeedBank() && mLevel <= 2))
	{
		if (mLevel == 1)
		{
			SetTutorialState(TutorialState::TUTORIAL_LEVEL_1_PICK_UP_PEASHOOTER);
		}
		else if (mLevel == 2)
		{
			SetTutorialState(TutorialState::TUTORIAL_LEVEL_2_PICK_UP_SUNFLOWER);
			DisplayAdvice(_S("[ADVICE_PLANT_SUNFLOWER1]"), MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL2, AdviceType::ADVICE_NONE);
			mTutorialTimer = 500;
		}
	}

	UpdateProgressMeter();
}

void Board::Update()
{
    if (mBenchFullNoticeTicks > 0 && !mPaused) --mBenchFullNoticeTicks;
	TodHesitationBracket aHesitation("Board::Update");

	Widget::Update();
	MarkDirty();

    UpdateSeedDraft(); // Non-modal: continue zombie, plant, projectile and UI updates.
	if (mAugmentChoosing)
	{
		UpdateAugmentSelection();	// the whole game is frozen while an augment is being chosen
		return;
	}

	// Keep the already-created selected-card preview animated after the chooser closes.
	// This updates an existing UI-only Plant; it never creates or initializes a Plant here.
	UpdateActiveAugmentTooltipPlant();
    AdvanceAugmentGalleryAnimation(mAugmentGalleryOpen);
    if (mAugmentGalleryOpen || gAugmentGalleryProgressMs > 0)
        UpdateGalleryAugmentPlants();

	if (mPaused && mApp->mIsFastMode)
		mApp->mIsFastMode = false;

	if (mFastButton != nullptr && !mFastButton->mBtnNoDraw)
	{
		mFastButton->mButtonImage = !mApp->mIsFastMode ? IMAGE_FASTBUTTON : IMAGE_FASTBUTTON_HIGHLIGHT;
		mFastButton->mOverImage = !mApp->mIsFastMode ? IMAGE_FASTBUTTON : IMAGE_FASTBUTTON_HIGHLIGHT;
		mFastButton->mDownImage = !mApp->mIsFastMode ? IMAGE_FASTBUTTON_HIGHLIGHT : IMAGE_FASTBUTTON;
	}

	SexyString aDetails;
	if (!mApp->IsAdventureMode())
		aDetails = mApp->GetCurrentChallengeDef().mChallengeName;
	else
		aDetails = TodReplaceString(mApp->mPlayingQuickplay ? _S("[DISCORD_BOARD_QUICK_PLAY]") : _S("[DISCORD_BOARD_ADVENTURE]"), _S("{LEVEL}"), mApp->GetStageString(mLevel).erase(0, 1));
	mApp->mDetails = aDetails;

	SexyString aState;
	if (mApp->GetDialog(Dialogs::DIALOG_CONTINUE))
		aState = _S("[DISCORD_BOARD_CONTINUE_DIALOG]");
	else if (mApp->GetDialog(Dialogs::DIALOG_PAUSED))
		aState = _S("[DISCORD_BOARD_PAUSED_DIALOG]");
	else if (mApp->mGameScene == GameScenes::SCENE_ZOMBIES_WON)
		aState = _S("[DISCORD_BOARD_GAME_OVER]");
	else if (mCutScene->mSeedChoosing)
		aState = _S("[DISCORD_BOARD_CHOOSING_PLANTS]");
	else if (mApp->mGameScene != GameScenes::SCENE_PLAYING && mCutScene->mCutsceneTime > 0)
		aState = _S("[DISCORD_BOARD_CUTSCENE]");
	else if (mBoardFadeOutCounter >= 0)
		aState = _S("[DISCORD_BOARD_FINISHING]");
	else if (mLevelAwardSpawned)
		aState = _S("[DISCORD_BOARD_FINISHED]");
	else
		aState = _S("[DISCORD_BOARD_PLAYING]");
	mApp->UpdateDiscordState(aState);

	if (mSunMoney >= 320 && !mApp->mPlayingQuickplay)
		mApp->GetAchievement(ACHIEVEMENT_SUNNY_DAYS);

	mCutScene->Update();
    // SaveGame and older builds can restore a mid-intro negative SeedBank Y.
    // Once gameplay is live, recover its position without touching Shop offers,
    // locks, copy counts, reroll results, or the Tutorial state machine.
    if (IsRollSeedBank() && mSeedBank != nullptr &&
        mApp->mGameScene == GameScenes::SCENE_PLAYING &&
        (mSeedBank->mY < 0 || !mSeedBank->mVisible))
    {
        mSeedBank->Move(SEED_BANK_OFFSET_X_END, 0);
        mSeedBank->mVisible = true;
        mSeedBank->mCutSceneDarken = 128;
    }
	UpdateMousePosition();
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
	{
		mApp->mZenGarden->ZenGardenUpdate();
	}
	if (IsScaryPotterDaveTalking())
	{
		mApp->UpdateCrazyDave();
	}

	if (!mRollTutorialChecked && mApp->mGameScene == GameScenes::SCENE_PLAYING && !mPaused && mApp->GetDialogCount() == 0 &&
		mApp->mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_OFF)
	{
		mRollTutorialChecked = true;

		// Adventure 1-1 through 1-7 do not pass through SeedChooserScreen::CloseSeedChooser().
		// Reconcile the Roll Shop on the first live frame as well as during InitLevel: at level
		// startup the final Adventure/Roll mode can be resolved after the original seed-bank
		// capacity and packet positions were assigned. Without this pass, the Reroll button can
		// overlap packet 1 and the shop can retain the classic 1/2-plant startup list.
		if (mApp->IsAdventureMode() && !mApp->mPlayingQuickplay && mLevel < ROLL_REMOVE_SEED_START_LEVEL && IsRollSeedBank())
		{
			bool aNeedsPoolRefresh = mNumChosenPlants < ROLL_STARTER_PLANTS;
			for (int aStarterIndex = 0; aStarterIndex < ROLL_STARTER_PLANTS && !aNeedsPoolRefresh; ++aStarterIndex)
			{
				SeedType aStarterType = (SeedType)((int)SeedType::SEED_PEASHOOTER + aStarterIndex);
				bool aFoundStarter = false;
				for (int aPoolIndex = 0; aPoolIndex < mNumChosenPlants; ++aPoolIndex)
				{
					if (mChosenPlants[aPoolIndex].mSeedType == aStarterType)
					{
						aFoundStarter = true;
						break;
					}
				}
				if (!aFoundStarter)
					aNeedsPoolRefresh = true;
			}

			if (aNeedsPoolRefresh)
			{
				mNumChosenPlants = 0;
				for (SeedType aSeedType = SeedType::SEED_PEASHOOTER; aSeedType < NUM_SEEDS_IN_CHOOSER && mNumChosenPlants < NUM_SEEDS_IN_CHOOSER; aSeedType = (SeedType)(aSeedType + 1))
				{
					bool aIsStarterPlant = (int)aSeedType < (int)SeedType::SEED_PEASHOOTER + ROLL_STARTER_PLANTS;
					if ((aIsStarterPlant || mApp->SeedTypeAvailable(aSeedType)) &&
						aSeedType != SeedType::SEED_IMITATER && !Plant::IsUpgrade(aSeedType))
					{
						ChosenPlant& aChosenPlant = mChosenPlants[mNumChosenPlants++];
						aChosenPlant.mSeedType = aSeedType;
						aChosenPlant.mImitaterType = SeedType::SEED_NONE;
					}
				}
			}

			int aOldSlotCount = mSeedBank->mNumPackets;
			mSeedBank->UpdateWidth(); // sets five packet slots and offsets them after the Reroll button
			bool aNeedsShopRoll = aNeedsPoolRefresh || aOldSlotCount != ROLL_MAX_SLOTS;
			for (int i = 0; i < mSeedBank->mNumPackets && !aNeedsShopRoll; ++i)
			{
				if (mSeedBank->mSeedPackets[i].mPacketType == SeedType::SEED_NONE)
					aNeedsShopRoll = true;
			}
			if (aNeedsShopRoll && mNumChosenPlants > 0)
				mSeedBank->RollAllSlots(false, true);
		}

		if (mApp->IsAdventureMode() && !mApp->mPlayingQuickplay && mLevel == 1 && IsRollSeedBank() && mApp->mPlayerInfo != nullptr && mApp->mPlayerInfo->mPurchases[ROLL_TUTORIAL_SEEN_SLOT] < ROLL_TUTORIAL_VERSION)
		{
			StartRollTutorial();
		}
	}
	if (!mPaused)
	{
		UpdateRollTutorial();
	}

	if (mPaused)
	{
		mChallenge->Update();
		mCursorPreview->mVisible = false;
		mCursorObject->mVisible = false;
		return;
	}

	bool aDisabled = !CanInteractWithBoardButtons() || mIgnoreMouseUp;
	if (!mMenuButton->mBtnNoDraw)
	{
		mMenuButton->mDisabled = aDisabled;
	}
	mMenuButton->Update();
	if (!mFastButton->mBtnNoDraw)
	{
		mFastButton->mDisabled = aDisabled;
	}
	if (HAS_FAST_FORWARD_BUTTON)
		mFastButton->Update();
	if (mStoreButton)
	{
		mStoreButton->mDisabled = aDisabled;
		mStoreButton->Update();
	}

	mApp->mEffectSystem->Update();
	mAdvice->Update();
	UpdateTutorial();

	if (mCobCannonCursorDelayCounter > 0)
	{
		mCobCannonCursorDelayCounter--;
	}
	if (mOutOfMoneyCounter > 0)
	{
		mOutOfMoneyCounter--;
	}
	if (mShakeCounter > 0)
	{
		mShakeCounter--;
		if (mShakeCounter == 0)
		{
			mX = 0;
			mY = 0;
		}
		else
		{
			if (!Rand(3))
			{
				mShakeAmountX = -mShakeAmountX;
			}
			mX = TodAnimateCurve(12, 0, mShakeCounter, 0, mShakeAmountX, TodCurves::CURVE_BOUNCE);
			mY = TodAnimateCurve(12, 0, mShakeCounter, 0, mShakeAmountY, TodCurves::CURVE_BOUNCE);
		}
	}
	if (mCoinBankFadeCount > 0 && mApp->GetDialog(Dialogs::DIALOG_PURCHASE_PACKET_SLOT) == nullptr)
	{
		mCoinBankFadeCount--;
	}
	UpdateLayers();

	if (mTimeStopCounter > 0)
		return;

	mEffectCounter++;
	if (StageHasPool() && !mIceTrapCounter && mApp->mGameScene != GameScenes::SCENE_ZOMBIES_WON && !mCutScene->IsSurvivalRepick())
	{
		mApp->mPoolEffect->PoolEffectUpdate();
	}
	if (mBackground == BackgroundType::BACKGROUND_3_POOL && mPoolSparklyParticleID == ParticleSystemID::PARTICLESYSTEMID_NULL && mDrawCount > 0)
	{
		int aRenderPosition = MakeRenderOrder(RenderLayer::RENDER_LAYER_GROUND, 2, 0);
		TodParticleSystem* aPoolParticle = mApp->AddTodParticle(450 + BOARD_ADDITIONAL_WIDTH, 295 + BOARD_OFFSET_Y, aRenderPosition, ParticleEffect::PARTICLE_POOL_SPARKLY);
		mPoolSparklyParticleID = mApp->ParticleGetID(aPoolParticle);
	}

	UpdateGridItems();
	UpdateFwoosh();
	UpdateGame();
	UpdateFog();
	mChallenge->Update();
	UpdateLevelEndSequence();
	mPrevMouseX = mApp->mWidgetManager->mLastMouseX;
	mPrevMouseY = mApp->mWidgetManager->mLastMouseY;
}

void Board::UpdateLayers()
{
	if (mWidgetManager)
	{
		mWidgetManager->MarkAllDirty();

		for (DialogList::iterator anIter = mApp->mDialogList.begin(); anIter != mApp->mDialogList.end(); ++anIter)
		{
			Dialog* aDialog = *anIter;
			mWidgetManager->BringToFront(aDialog);
			aDialog->MarkDirty();
		}
	}
}

bool Board::RowCanHaveZombies(int theRow)
{
	if (theRow < 0 || theRow >= MAX_GRID_SIZE_Y)
		return false;

	return (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_RESODDED && theRow <= 4) || mPlantRow[theRow] != PlantRowType::PLANTROW_DIRT;
}

int Board::GetIceZPos(int theRow)
{
	return MakeRenderOrder(RenderLayer::RENDER_LAYER_GROUND, theRow, 2);
}

void Board::DrawIce(Graphics* g, int theGridY)
{
	int aPosY = GridToPixelY(8, theGridY) + 20;
	int aHeight = Sexy::IMAGE_ICE->GetHeight();
	int aWidth = Sexy::IMAGE_ICE->GetWidth();
	int anAlpha = ClampInt(255 * mIceTimer[theGridY] / 10, 0, 255);
	if (anAlpha < 255)
	{
		g->SetColorizeImages(true);
		g->SetColor(Color(255, 255, 255, anAlpha));
	}

	int aBeginningX = mIceMinX[theGridY] + 13, aDeltaX;
	for (int aPosX = aBeginningX; aPosX < BOARD_ICE_START; aPosX += aDeltaX)
	{
		if (aPosX == aBeginningX)
		{
			aDeltaX = (BOARD_ICE_START - aBeginningX) % aWidth;
			if (!aDeltaX) aDeltaX = aWidth;
		}
		else aDeltaX = aWidth;
		Rect aRepeatSrcRect(aWidth - aDeltaX, 0, aDeltaX, aHeight);
		Rect aRepeatDstRect(aPosX, aPosY, aDeltaX, aHeight);
		g->DrawImage(Sexy::IMAGE_ICE, aRepeatDstRect, aRepeatSrcRect);
	}
	g->DrawImage(Sexy::IMAGE_ICE_CAP, mIceMinX[theGridY], aPosY);
	g->SetColorizeImages(false);
}

void Board::DrawBackdrop(Graphics* g)
{
	Image* aBgImage = nullptr;
	switch (mBackground)
	{
	case BackgroundType::BACKGROUND_1_DAY:				aBgImage = Sexy::IMAGE_BACKGROUND1;						break;
	case BackgroundType::BACKGROUND_2_NIGHT:			aBgImage = Sexy::IMAGE_BACKGROUND2;						break;
	case BackgroundType::BACKGROUND_3_POOL:				aBgImage = Sexy::IMAGE_BACKGROUND3;						break;
	case BackgroundType::BACKGROUND_4_FOG:				aBgImage = Sexy::IMAGE_BACKGROUND4;						break;
	case BackgroundType::BACKGROUND_5_ROOF:				aBgImage = Sexy::IMAGE_BACKGROUND5;						break;
	case BackgroundType::BACKGROUND_6_BOSS:				aBgImage = Sexy::IMAGE_BACKGROUND6BOSS;					break;
	case BackgroundType::BACKGROUND_MUSHROOM_GARDEN:	aBgImage = Sexy::IMAGE_BACKGROUND_MUSHROOMGARDEN;		break;
	case BackgroundType::BACKGROUND_GREENHOUSE:			aBgImage = Sexy::IMAGE_BACKGROUND_GREENHOUSE;			break;
	case BackgroundType::BACKGROUND_ZOMBIQUARIUM:		aBgImage = Sexy::IMAGE_AQUARIUM1;						break;
	case BackgroundType::BACKGROUND_TREEOFWISDOM:		aBgImage = nullptr;										break;
	default:											TOD_ASSERT();											break;
	}

	if (mLevel == 1 && mApp->IsFirstTimeAdventureMode())
	{
		g->DrawImage(Sexy::IMAGE_BACKGROUND1UNSODDED, -BOARD_OFFSET_X, 0);
		int aWidth = TodAnimateCurve(0, 1000, mSodPosition, 0, Sexy::IMAGE_SOD1ROW->GetWidth(), TodCurves::CURVE_LINEAR);
		Rect aSrcRect(0, 0, aWidth, Sexy::IMAGE_SOD1ROW->GetHeight());
		g->DrawImage(Sexy::IMAGE_SOD1ROW, 239 - BOARD_OFFSET_X + BOARD_ADDITIONAL_WIDTH, 265 + BOARD_OFFSET_Y, aSrcRect);
	}
	else if (((mLevel == 2 || mLevel == 3) && mApp->IsFirstTimeAdventureMode()) || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_RESODDED)
	{
		g->DrawImage(Sexy::IMAGE_BACKGROUND1UNSODDED, -BOARD_OFFSET_X, 0);
		g->DrawImage(Sexy::IMAGE_SOD1ROW, 239 - BOARD_OFFSET_X + BOARD_ADDITIONAL_WIDTH, 265 + BOARD_OFFSET_Y);
		int aWidth = TodAnimateCurve(0, 1000, mSodPosition, 0, Sexy::IMAGE_SOD3ROW->GetWidth(), TodCurves::CURVE_LINEAR);
		Rect aSrcRect(0, 0, aWidth, Sexy::IMAGE_SOD3ROW->GetHeight());
		g->DrawImage(Sexy::IMAGE_SOD3ROW, 235 - BOARD_OFFSET_X + BOARD_ADDITIONAL_WIDTH, 149 + BOARD_OFFSET_Y, aSrcRect);
	}
	else if (mLevel == 4 && mApp->IsFirstTimeAdventureMode())
	{
		g->DrawImage(Sexy::IMAGE_BACKGROUND1UNSODDED, -BOARD_OFFSET_X, 0);
		g->DrawImage(Sexy::IMAGE_SOD3ROW, 235 - BOARD_OFFSET_X  + BOARD_ADDITIONAL_WIDTH, 149 + BOARD_OFFSET_Y);
		int aWidth = TodAnimateCurve(0, 1000, mSodPosition, 0, 773, TodCurves::CURVE_LINEAR);
		Rect aSrcRect(232, 0, aWidth + BOARD_ADDITIONAL_WIDTH, Sexy::IMAGE_BACKGROUND1->GetHeight());
		g->DrawImage(Sexy::IMAGE_BACKGROUND1, 232 - BOARD_OFFSET_X, 0, aSrcRect);
	}
	else if (aBgImage)
	{
		if (aBgImage == Sexy::IMAGE_BACKGROUND_MUSHROOMGARDEN || aBgImage == Sexy::IMAGE_BACKGROUND_GREENHOUSE || aBgImage == Sexy::IMAGE_AQUARIUM1)
		{
			g->DrawImage(aBgImage, 0, 0);
		}
		else
		{
			g->DrawImage(aBgImage, -BOARD_OFFSET_X, 0);
		}
	}

	if (mApp->mGameScene == GameScenes::SCENE_ZOMBIES_WON)
	{
		DrawHouseDoorBottom(g);
	}
	if (StageHasPool())
	{
		g->mTransX += BOARD_ADDITIONAL_WIDTH;
		g->mTransY += BOARD_OFFSET_Y;
		mApp->mPoolEffect->PoolEffectDraw(g, StageIsNight());
		g->mTransX -= BOARD_ADDITIONAL_WIDTH;
		g->mTransY -= BOARD_OFFSET_Y;
	}
	if (mTutorialState == TutorialState::TUTORIAL_LEVEL_1_PLANT_PEASHOOTER)
	{
		Graphics aClipG(*g);
		aClipG.SetColorizeImages(true);
		aClipG.SetColor(GetFlashingColor(mMainCounter, 75));
		aClipG.DrawImage(Sexy::IMAGE_SOD1ROW, 239 - BOARD_OFFSET_X + BOARD_ADDITIONAL_WIDTH, 265 + BOARD_OFFSET_Y);
		aClipG.SetColorizeImages(false);
	}
	mChallenge->DrawBackdrop(g);
	if (mApp->mGameScene == GameScenes::SCENE_LEVEL_INTRO && StageHasGraveStones())
	{
		g->DrawImage(Sexy::IMAGE_NIGHT_GRAVE_GRAPHIC, 1092 + BOARD_ADDITIONAL_WIDTH, 30 + BOARD_OFFSET_Y);
	}
}

bool RenderItemSortFunc(const RenderItem& theItem1, const RenderItem& theItem2)
{
	if (theItem1.mZPos == theItem2.mZPos)
	{
		return theItem1.mGameObject < theItem2.mGameObject;
	}

	return theItem1.mZPos < theItem2.mZPos;
}

void Board::AddBossRenderItem(RenderItem* theRenderList, int& theCurRenderItem, Zombie* theBossZombie)
{
	TOD_ASSERT(theCurRenderItem < MAX_RENDER_ITEMS);
	int aBackLegRow = 1;
	int aFrontLegRow = 3;
	int aBackArmRow = 4;
	if (theBossZombie->IsDeadOrDying())
	{
		aBackArmRow = 1;
	}
	else if (theBossZombie->mZombiePhase == ZombiePhase::PHASE_BOSS_STOMPING)
	{
		Reanimation* aBossReanim = mApp->ReanimationTryToGet(theBossZombie->mBodyReanimID);
		if (aBossReanim->mAnimTime > 0.25f && aBossReanim->mAnimTime < 0.75f)
		{
			if (theBossZombie->mTargetRow == 1)
			{
				aBackLegRow = 2;
			}
			else if (theBossZombie->mTargetRow == 3)
			{
				aFrontLegRow = 4;
			}
		}
	}

	RenderItem* aItem = &theRenderList[theCurRenderItem];
	aItem->mRenderObjectType = RenderObjectType::RENDER_ITEM_BOSS_PART;
	aItem->mZPos = MakeRenderOrder(RenderLayer::RENDER_LAYER_BOSS, aBackLegRow, 2);
	aItem->mBossPart = BossPart::BOSS_PART_BACK_LEG;
	theCurRenderItem++;
	aItem = &theRenderList[theCurRenderItem];
	aItem->mRenderObjectType = RenderObjectType::RENDER_ITEM_BOSS_PART;
	aItem->mZPos = MakeRenderOrder(RenderLayer::RENDER_LAYER_BOSS, aFrontLegRow, 2);
	aItem->mBossPart = BossPart::BOSS_PART_FRONT_LEG;
	theCurRenderItem++;
	aItem = &theRenderList[theCurRenderItem];
	aItem->mRenderObjectType = RenderObjectType::RENDER_ITEM_BOSS_PART;
	aItem->mZPos = MakeRenderOrder(RenderLayer::RENDER_LAYER_BOSS, 4, 2);
	aItem->mBossPart = BossPart::BOSS_PART_MAIN;
	theCurRenderItem++;
	aItem = &theRenderList[theCurRenderItem];
	aItem->mRenderObjectType = RenderObjectType::RENDER_ITEM_BOSS_PART;
	aItem->mZPos = MakeRenderOrder(RenderLayer::RENDER_LAYER_BOSS, aBackArmRow, 3);
	aItem->mBossPart = BossPart::BOSS_PART_BACK_ARM;
	theCurRenderItem++;

	Reanimation* aBallReanim = mApp->ReanimationTryToGet(theBossZombie->mBossFireBallReanimID);
	if (aBallReanim)
	{
		RenderItem* aItem = &theRenderList[theCurRenderItem];
		aItem->mRenderObjectType = RenderObjectType::RENDER_ITEM_BOSS_PART;
		aItem->mZPos = aBallReanim->mRenderOrder;
		aItem->mBossPart = BossPart::BOSS_PART_FIREBALL;
		theCurRenderItem++;
	}
}

static inline void AddGameObjectRenderItem(RenderItem* theRenderList, int& theCurRenderItem, RenderObjectType theRenderObjectType, GameObject* theGameObject)
{
	TOD_ASSERT(theCurRenderItem < MAX_RENDER_ITEMS);
	RenderItem& aRenderItem = theRenderList[theCurRenderItem];
	aRenderItem.mRenderObjectType = theRenderObjectType;
	aRenderItem.mZPos = theGameObject->mRenderOrder;
	aRenderItem.mGameObject = theGameObject;
	theCurRenderItem++;
}

static inline void AddGameObjectRenderItemCursorPreview(RenderItem* theRenderList, int& theCurRenderItem, RenderObjectType theRenderObjectType, GameObject* theGameObject)
{
	TOD_ASSERT(theCurRenderItem < MAX_RENDER_ITEMS);
	RenderItem& aRenderItem = theRenderList[theCurRenderItem];
	aRenderItem.mRenderObjectType = theRenderObjectType;
	aRenderItem.mZPos = theGameObject->mRenderOrder;
	aRenderItem.mGameObject = theGameObject;
	aRenderItem.mCursorPreview = (CursorPreview*)theGameObject;

	theCurRenderItem++;
}

static inline void AddGameObjectRenderItemPlant(RenderItem* theRenderList, int& theCurRenderItem, RenderObjectType theRenderObjectType, GameObject* theGameObject)
{
	TOD_ASSERT(theCurRenderItem < MAX_RENDER_ITEMS);
	RenderItem& aRenderItem = theRenderList[theCurRenderItem];
	aRenderItem.mRenderObjectType = theRenderObjectType;
	aRenderItem.mZPos = theGameObject->mRenderOrder;
	aRenderItem.mGameObject = theGameObject;
	aRenderItem.mPlant = (Plant*)theGameObject;

	theCurRenderItem++;
}

static inline void AddGameObjectRenderItemZombie(RenderItem* theRenderList, int& theCurRenderItem, RenderObjectType theRenderObjectType, GameObject* theGameObject)
{
	TOD_ASSERT(theCurRenderItem < MAX_RENDER_ITEMS);
	RenderItem& aRenderItem = theRenderList[theCurRenderItem];
	aRenderItem.mRenderObjectType = theRenderObjectType;
	aRenderItem.mZPos = theGameObject->mRenderOrder;
	aRenderItem.mGameObject = theGameObject;
	aRenderItem.mZombie = (Zombie*)theGameObject;
	theCurRenderItem++;
}

static inline void AddGameObjectRenderItemProjectile(RenderItem* theRenderList, int& theCurRenderItem, RenderObjectType theRenderObjectType, GameObject* theGameObject)
{
	TOD_ASSERT(theCurRenderItem < MAX_RENDER_ITEMS);
	RenderItem& aRenderItem = theRenderList[theCurRenderItem];
	aRenderItem.mRenderObjectType = theRenderObjectType;
	aRenderItem.mZPos = theGameObject->mRenderOrder;
	aRenderItem.mGameObject = theGameObject;
	aRenderItem.mProjectile = (Projectile*)theGameObject;
	theCurRenderItem++;
}

static inline void AddGameObjectRenderItemCoin(RenderItem* theRenderList, int& theCurRenderItem, RenderObjectType theRenderObjectType, GameObject* theGameObject)
{
	TOD_ASSERT(theCurRenderItem < MAX_RENDER_ITEMS);
	RenderItem& aRenderItem = theRenderList[theCurRenderItem];
	aRenderItem.mRenderObjectType = theRenderObjectType;
	aRenderItem.mZPos = theGameObject->mRenderOrder;
	aRenderItem.mGameObject = theGameObject;
	aRenderItem.mCoin = (Coin*)theGameObject;
	theCurRenderItem++;
}

static inline void AddGameObjectRenderItemBush(RenderItem* theRenderList, int& theCurRenderItem, RenderObjectType theRenderObjectType, GameObject* theGameObject)
{
	TOD_ASSERT(theCurRenderItem < MAX_RENDER_ITEMS);
	RenderItem& aRenderItem = theRenderList[theCurRenderItem];
	aRenderItem.mRenderObjectType = theRenderObjectType;
	aRenderItem.mZPos = theGameObject->mRenderOrder;
	aRenderItem.mGameObject = theGameObject;
	aRenderItem.mCoin = (Coin*)theGameObject;
	theCurRenderItem++;
}

static inline void AddUIRenderItem(RenderItem* theRenderList, int& theCurRenderItem, RenderObjectType theRenderObjectType, int thePosZ)
{
	TOD_ASSERT(theCurRenderItem < MAX_RENDER_ITEMS);
	RenderItem& aRenderItem = theRenderList[theCurRenderItem];
	aRenderItem.mRenderObjectType = theRenderObjectType;
	aRenderItem.mZPos = thePosZ;
	aRenderItem.mGameObject = nullptr;
	theCurRenderItem++;
}

void Board::DrawGameObjects(Graphics* g)
{
	TodHesitationTrace("creating render list");

    // Heap-backed: a 32K render list would overflow the default Windows thread stack.
    std::vector<RenderItem> renderItems(MAX_RENDER_ITEMS);
    RenderItem* aRenderList = &renderItems[0];
	int aRenderItemCount = 0;

	{
		Plant* aPlant = nullptr;
		while (IteratePlants(aPlant))
		{
			if (aPlant->mOnBungeeState == PlantOnBungeeState::NOT_ON_BUNGEE)
			{
				AddGameObjectRenderItemPlant(aRenderList, aRenderItemCount, RenderObjectType::RENDER_ITEM_PLANT, aPlant);

				if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN && aPlant->mPottedPlantIndex != -1)
				{
					RenderItem& aRenderItem = aRenderList[aRenderItemCount];
					aRenderItem.mRenderObjectType = RenderObjectType::RENDER_ITEM_PLANT_OVERLAY;
					aRenderItem.mZPos = MakeRenderOrder(RenderLayer::RENDER_LAYER_PARTICLE, 0, mY);
					aRenderItem.mPlant = aPlant;
					aRenderItemCount++;
				}

				if ((aPlant->mSeedType == SeedType::SEED_MAGNETSHROOM || aPlant->mSeedType == SeedType::SEED_GOLD_MAGNET) && aPlant->DrawMagnetItemsOnTop())
				{
					RenderItem& aRenderItem = aRenderList[aRenderItemCount];
					aRenderItem.mRenderObjectType = RenderObjectType::RENDER_ITEM_PLANT_MAGNET_ITEMS;
					aRenderItem.mZPos = MakeRenderOrder(RenderLayer::RENDER_LAYER_TOP, 0, -1);
					aRenderItem.mPlant = aPlant;
					aRenderItemCount++;
				}

				bool aPlantCheck = (aPlant->mSeedType == SeedType::SEED_INSTANT_COFFEE || aPlant->mImitaterType == SeedType::SEED_INSTANT_COFFEE) || (aPlant->mSeedType == SeedType::SEED_FLOWERPOT || aPlant->mImitaterType == SeedType::SEED_FLOWERPOT) || (aPlant->mSeedType == SeedType::SEED_LILYPAD || aPlant->mImitaterType == SeedType::SEED_LILYPAD);
				if (mApp->mPlantHealthbars && mApp->mGameMode != GAMEMODE_CHALLENGE_ZEN_GARDEN && !aPlantCheck)
				{
					RenderItem& aRenderItem = aRenderList[aRenderItemCount];
					aRenderItem.mRenderObjectType = RenderObjectType::RENDER_ITEM_HEALTHBAR_PLANT;
					aRenderItem.mZPos = aPlant->mRenderOrder + 1;
					aRenderItem.mPlant = aPlant;
					aRenderItemCount++;
				}
			}
		}
	}
	{
		Coin* aCoin = nullptr;
		while (IterateCoins(aCoin))
		{
			AddGameObjectRenderItemCoin(aRenderList, aRenderItemCount, RenderObjectType::RENDER_ITEM_COIN, aCoin);
		}
		Bush* aBush = nullptr;
		while (IterateBushes(aBush))
		{
			AddGameObjectRenderItemBush(aRenderList, aRenderItemCount, RenderObjectType::RENDER_ITEM_BUSH, aBush);
		}
	}
	{
		Zombie* aZombie = nullptr;
		while (IterateZombies(aZombie))
		{
			if (aZombie->mZombieType == ZombieType::ZOMBIE_BOSS)
			{
				AddBossRenderItem(aRenderList, aRenderItemCount, aZombie);
			}
			else
			{
				AddGameObjectRenderItemZombie(aRenderList, aRenderItemCount, RenderObjectType::RENDER_ITEM_ZOMBIE, aZombie);

				if (aZombie->HasShadow())
				{
					RenderItem& aRenderItem = aRenderList[aRenderItemCount];
					aRenderItem.mRenderObjectType = RenderObjectType::RENDER_ITEM_ZOMBIE_SHADOW;
					aRenderItem.mZPos = MakeRenderOrder(RenderLayer::RENDER_LAYER_GROUND, aZombie->mRow, 3);
					aRenderItem.mZombie = aZombie;
					aRenderItemCount++;
				}

				if (aZombie->mZombieType == ZombieType::ZOMBIE_BUNGEE)
				{
					RenderItem& aRenderItem = aRenderList[aRenderItemCount];
					aRenderItem.mRenderObjectType = RenderObjectType::RENDER_ITEM_ZOMBIE_BUNGEE_TARGET;
					aRenderItem.mZPos = MakeRenderOrder(RenderLayer::RENDER_LAYER_PROJECTILE, aZombie->mRow, 1);
					aRenderItem.mZombie = aZombie;
					aRenderItemCount++;
				}

				if (mApp->mZombieHealthbars && mApp->mGameMode != GameMode::GAMEMODE_CHALLENGE_INVISIGHOUL)
				{
					RenderItem& aRenderItem = aRenderList[aRenderItemCount];
					aRenderItem.mRenderObjectType = RenderObjectType::RENDER_ITEM_HEALTHBAR_ZOMBIE;
					aRenderItem.mZPos = aZombie->mRenderOrder + 1;
					aRenderItem.mZombie = aZombie;
					aRenderItemCount++;
				}
			}
		}
	}
	{
		Projectile* aProjectile = nullptr;
		while (IterateProjectiles(aProjectile))
		{
			AddGameObjectRenderItemProjectile(aRenderList, aRenderItemCount, RenderObjectType::RENDER_ITEM_PROJECTILE, aProjectile);

			RenderItem& aRenderItem = aRenderList[aRenderItemCount];
			aRenderItem.mRenderObjectType = RenderObjectType::RENDER_ITEM_PROJECTILE_SHADOW;
			aRenderItem.mZPos = MakeRenderOrder(RenderLayer::RENDER_LAYER_GROUND, aProjectile->mRow, 3);
			aRenderItem.mProjectile = aProjectile;
			aRenderItemCount++;
		}
	}
	{
		LawnMower* aLawnMower = nullptr;
		while (IterateLawnMowers(aLawnMower))
		{
			RenderItem& aRenderItem = aRenderList[aRenderItemCount];
			aRenderItem.mRenderObjectType = RenderObjectType::RENDER_ITEM_MOWER;
			aRenderItem.mZPos = aLawnMower->mRenderOrder;
			aRenderItem.mMower = aLawnMower;
			aRenderItemCount++;
		}
	}
	{
		TodParticleSystem* aParticle = nullptr;
		while (IterateParticles(aParticle))
		{
			if (!aParticle->mIsAttachment)
			{
				RenderItem& aRenderItem = aRenderList[aRenderItemCount];
				aRenderItem.mRenderObjectType = RenderObjectType::RENDER_ITEM_PARTICLE;
				aRenderItem.mZPos = aParticle->mRenderOrder;
				aRenderItem.mParticleSytem = aParticle;
				aRenderItemCount++;
			}
		}
	}
	{
		Reanimation* aReanimation = nullptr;
		while (IterateReanimations(aReanimation))
		{
			if (!aReanimation->mIsAttachment)
			{
				RenderItem& aRenderItem = aRenderList[aRenderItemCount];
				aRenderItem.mRenderObjectType = RenderObjectType::RENDER_ITEM_REANIMATION;
				aRenderItem.mZPos = aReanimation->mRenderOrder;
				aRenderItem.mReanimation = aReanimation;
				aRenderItemCount++;
			}
		}
	}
	{
		GridItem* aGridItem = nullptr;
		while (IterateGridItems(aGridItem))
		{
			RenderItem& aRenderItem = aRenderList[aRenderItemCount];
			aRenderItem.mRenderObjectType = RenderObjectType::RENDER_ITEM_GRID_ITEM;
            // Also fix buckets restored from an older saved game whose render
            // order was serialized in the plant layer.
            aRenderItem.mZPos = aGridItem->mGridItemType == GridItemType::GRIDITEM_DROPPED_BUCKET ?
                MakeRenderOrder(RenderLayer::RENDER_LAYER_ZOMBIE, aGridItem->mGridY, -1) :
                aGridItem->mRenderOrder;
			aRenderItem.mGridItem = aGridItem;
			aRenderItemCount++;

			if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN && aGridItem->mGridItemType == GridItemType::GRIDITEM_STINKY)
			{
				RenderItem& aRenderItem = aRenderList[aRenderItemCount];
				aRenderItem.mRenderObjectType = RenderObjectType::RENDER_ITEM_GRID_ITEM_OVERLAY;
				aRenderItem.mZPos = MakeRenderOrder(RenderLayer::RENDER_LAYER_PARTICLE, 0, aGridItem->mPosY - 30.0f);
				aRenderItem.mGridItem = aGridItem;
				aRenderItemCount++;
			}
		}
	}
	for (int i = 0; i < MAX_GRID_SIZE_Y; i++)
	{
		if (mIceTimer[i])
		{
			RenderItem& aRenderItem = aRenderList[aRenderItemCount];
			aRenderItem.mRenderObjectType = RenderObjectType::RENDER_ITEM_ICE;
			aRenderItem.mBoardGridY = i;
			aRenderItem.mZPos = GetIceZPos(i);
			aRenderItemCount++;
		}
	}
	{
		int aZPos;
		if (mTimeStopCounter > 0 || (mApp->mGameScene == GameScenes::SCENE_LEVEL_INTRO && mCutScene->IsPanningLeft() && mTutorialState == TutorialState::TUTORIAL_OFF))
		{
			aZPos = MakeRenderOrder(RenderLayer::RENDER_LAYER_ABOVE_UI, 0, 0);
		}
		else if (mApp->mGameScene == GameScenes::SCENE_PLAYING || mApp->mGameScene == GameScenes::SCENE_ZOMBIES_WON || mCutScene->IsAfterSeedChooser() || mCutScene->IsInShovelTutorial() || mHelpIndex == AdviceType::ADVICE_CLICK_TO_CONTINUE)
		{
			aZPos = MakeRenderOrder(RenderLayer::RENDER_LAYER_UI_BOTTOM, 0, 1);
		}
		else
		{
			aZPos = MakeRenderOrder(RenderLayer::RENDER_LAYER_ABOVE_UI, 0, 0);
		}

		AddUIRenderItem(aRenderList, aRenderItemCount, RenderObjectType::RENDER_ITEM_BACKDROP, MakeRenderOrder(RenderLayer::RENDER_LAYER_UI_BOTTOM, 0, 0));
		AddUIRenderItem(aRenderList, aRenderItemCount, RenderObjectType::RENDER_ITEM_BOTTOM_UI, aZPos);
		AddUIRenderItem(aRenderList, aRenderItemCount, RenderObjectType::RENDER_ITEM_COVER, MakeRenderOrder(RenderLayer::RENDER_LAYER_TOP, 0, 0));
		AddUIRenderItem(aRenderList, aRenderItemCount, RenderObjectType::RENDER_ITEM_COIN_BANK, MakeRenderOrder(RenderLayer::RENDER_LAYER_COIN_BANK, 0, 0));
		AddUIRenderItem(aRenderList, aRenderItemCount, RenderObjectType::RENDER_ITEM_TOP_UI, MakeRenderOrder(RenderLayer::RENDER_LAYER_UI_TOP, 0, 0));
		AddUIRenderItem(aRenderList, aRenderItemCount, RenderObjectType::RENDER_ITEM_SCREEN_FADE, MakeRenderOrder(RenderLayer::RENDER_LAYER_SCREEN_FADE, 0, 0));
	}
	if (mApp->mGameScene == GameScenes::SCENE_ZOMBIES_WON)
	{
		int aZPos;
		if (StageHasRoof())
		{
			aZPos = MakeRenderOrder(RenderLayer::RENDER_LAYER_GRAVE_STONE, 0, 4);
		}
		else
		{
			aZPos = MakeRenderOrder(RenderLayer::RENDER_LAYER_GRAVE_STONE, 3, 2);
		}
		AddUIRenderItem(aRenderList, aRenderItemCount, RenderObjectType::RENDER_ITEM_DOOR_MASK, aZPos);
	}
	if (StageHasFog())
	{
		AddUIRenderItem(aRenderList, aRenderItemCount, RenderObjectType::RENDER_ITEM_FOG, MakeRenderOrder(RenderLayer::RENDER_LAYER_FOG, 0, 0));
	}
	if (mApp->IsStormyNightLevel() || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_RAINING_SEEDS)
	{
		AddUIRenderItem(aRenderList, aRenderItemCount, RenderObjectType::RENDER_ITEM_STORM, MakeRenderOrder(RenderLayer::RENDER_LAYER_FOG, 0, 3));
	}
	AddGameObjectRenderItemCursorPreview(aRenderList, aRenderItemCount, RenderObjectType::RENDER_ITEM_CURSOR_PREVIEW, mCursorPreview);
	/*
	ControllerBoard* aGamepadPlayer = nullptr;
	while (IterateControllerBoards(aGamepadPlayer))
	{
		AddGameObjectRenderItemCursorPreview(aRenderList, aRenderItemCount, RenderObjectType::RENDER_ITEM_CURSOR_PREVIEW, aGamepadPlayer->mCursorPreview);
	}
	*/

	TodHesitationTrace("start sort");
	std::sort(aRenderList, aRenderList + aRenderItemCount, RenderItemSortFunc);

	TodHesitationTrace("end sort, start draw");
	for (int i = 0; i < aRenderItemCount; i++)
	{
		RenderItem& aRenderItem = aRenderList[i];
		switch (aRenderItem.mRenderObjectType)
		{
		case RenderObjectType::RENDER_ITEM_PLANT:
		{
			Plant* aPlant = aRenderItem.mPlant;
			if (aPlant->BeginDraw(g))
			{
				aPlant->Draw(g);
				aPlant->EndDraw(g);
			}
			break;
		}

		case RenderObjectType::RENDER_ITEM_PLANT_OVERLAY:
		{
			Plant* aPlant = aRenderItem.mPlant;
			if (aPlant->BeginDraw(g))
			{
				mApp->mZenGarden->DrawPlantOverlay(g, aPlant);
				aPlant->EndDraw(g);
			}
			break;
		}

		case RenderObjectType::RENDER_ITEM_PLANT_MAGNET_ITEMS:
		{
			Plant* aPlant = aRenderItem.mPlant;
			if (aPlant->BeginDraw(g))
			{
				aPlant->DrawMagnetItems(g);
				aPlant->EndDraw(g);
			}
			break;
		}

		case RenderObjectType::RENDER_ITEM_MOWER:
		{
			LawnMower* aLawnMower = aRenderItem.mMower;
			aLawnMower->Draw(g);
			break;
		}

		case RenderObjectType::RENDER_ITEM_ZOMBIE:
		{
			Zombie* aZombie = aRenderItem.mZombie;
			if (aZombie->BeginDraw(g))
			{
				aZombie->Draw(g);
				aZombie->EndDraw(g);
			}
			break;
		}

		case RenderObjectType::RENDER_ITEM_ZOMBIE_SHADOW:
		{
			Zombie* aZombie = aRenderItem.mZombie;
			if (aZombie->BeginDraw(g))
			{
				aZombie->DrawShadow(g);
				aZombie->EndDraw(g);
			}
			break;
		}

		case RenderObjectType::RENDER_ITEM_ZOMBIE_BUNGEE_TARGET:
		{
			Zombie* aZombie = aRenderItem.mZombie;
			aZombie->DrawBungeeTarget(g);
			break;
		}

		case RenderObjectType::RENDER_ITEM_BOSS_PART:
		{
			Zombie* aBossZombie = GetBossZombie();
			if (aBossZombie && aBossZombie->BeginDraw(g))
			{
				aBossZombie->DrawBossPart(g, aRenderItem.mBossPart);
				aBossZombie->EndDraw(g);
			}
			break;
		}

		case RenderObjectType::RENDER_ITEM_COIN:
		{
			Coin* aCoin = aRenderItem.mCoin;
			if (aCoin->BeginDraw(g))
			{
				aCoin->Draw(g);
				aCoin->EndDraw(g);
			}
			break;
		}

		case RenderObjectType::RENDER_ITEM_PROJECTILE:
		{
			Projectile* aProjectile = aRenderItem.mProjectile;
			if (aProjectile->BeginDraw(g))
			{
				aProjectile->Draw(g);
				aProjectile->EndDraw(g);
			}
			break;
		}

		case RenderObjectType::RENDER_ITEM_PROJECTILE_SHADOW:
		{
			Projectile* aProjectile = aRenderItem.mProjectile;
			if (aProjectile->BeginDraw(g))
			{
				aProjectile->DrawShadow(g);
				aProjectile->EndDraw(g);
			}
			break;
		}

		case RenderObjectType::RENDER_ITEM_CURSOR_PREVIEW:
		{
			CursorPreview* aCursorPreview = aRenderItem.mCursorPreview;
			if (aCursorPreview->BeginDraw(g))
			{
				aCursorPreview->Draw(g);
				aCursorPreview->EndDraw(g);
			}
			break;
		}
		
		case RenderObjectType::RENDER_ITEM_GRID_ITEM:
		{
			GridItem* aGridItem = aRenderItem.mGridItem;
			aGridItem->DrawGridItem(g);
			break;
		}

		case RenderObjectType::RENDER_ITEM_GRID_ITEM_OVERLAY:
		{
			GridItem* aGridItem = aRenderItem.mGridItem;
			aGridItem->DrawGridItemOverlay(g);
			break;
		}

		case RenderObjectType::RENDER_ITEM_ICE:
			DrawIce(g, aRenderItem.mBoardGridY);
			break;

		case RenderObjectType::RENDER_ITEM_PARTICLE:
		{
			TodParticleSystem* aParticle = aRenderItem.mParticleSytem;
			aParticle->Draw(g);
			break;
		}

		case RenderObjectType::RENDER_ITEM_REANIMATION:
		{
			Reanimation* aReanimation = aRenderItem.mReanimation;
			aReanimation->Draw(g);
			break;
		}

		case RenderObjectType::RENDER_ITEM_COIN_BANK:
			DrawUICoinBank(g);
			break;

		case RenderObjectType::RENDER_ITEM_BACKDROP:
			DrawBackdrop(g);
			break;

		case RenderObjectType::RENDER_ITEM_DOOR_MASK:
			DrawHouseDoorTop(g);
			break;

		case RenderObjectType::RENDER_ITEM_BOTTOM_UI:
			DrawUIBottom(g);
			break;
		
		case RenderObjectType::RENDER_ITEM_TOP_UI:
			DrawUITop(g);
			break;
			
		case RenderObjectType::RENDER_ITEM_BUSH:
		{
			Bush* aBush = aRenderItem.mBush;
			if (aBush->BeginDraw(g))
			{
				aBush->Draw(g);
				aBush->EndDraw(g);
			}
			break;
		}

		case RenderObjectType::RENDER_ITEM_COVER:
			DrawCover(g);
			break;

		case RenderObjectType::RENDER_ITEM_FOG:
			DrawFog(g);
			break;

		case RenderObjectType::RENDER_ITEM_STORM:
			mChallenge->DrawWeather(g);
			break;
		
		case RenderObjectType::RENDER_ITEM_SCREEN_FADE:
			DrawFadeOut(g);
			break;

		case RenderObjectType::RENDER_ITEM_HEALTHBAR_ZOMBIE:
		{
			Zombie* aZombie = aRenderItem.mZombie;
			Rect rect = aZombie->GetZombieRect();
			int barWidth = 55;
			int barHeight = 10;
			int barOffsetY = 0;
			int baseBarOffsetY = 3;
			int textOffsetY = 3;
			int baseTextOffsetY = 14;
			int textOutlineOffset = 1;
			Color maxColor = Color(255, 0, 0);
			Color textColor = Color::White;
			bool drawBarOutline = true;
			if (aZombie->mBodyHealth > 0)
			{
				barOffsetY += baseBarOffsetY;
				DrawHealthbar(g, rect, maxColor, aZombie->mBodyMaxHealth, Color(255, 255, 0), aZombie->mBodyHealth, barWidth, barHeight, 0, barOffsetY, textColor, FONT_BRIANNETOD12, textOffsetY, Color::Black, textOutlineOffset, drawBarOutline);
			}
			if (aZombie->mHelmHealth > 0)
			{
				barOffsetY += baseBarOffsetY + barHeight + textOffsetY + baseTextOffsetY;
				DrawHealthbar(g, rect, maxColor, aZombie->mHelmMaxHealth, Color(0, 0, 255), aZombie->mHelmHealth, barWidth, barHeight, 0, barOffsetY, textColor, FONT_BRIANNETOD12, textOffsetY, Color::Black, textOutlineOffset, drawBarOutline);
			}
			if (aZombie->mShieldHealth > 0)
			{
				barOffsetY += baseBarOffsetY + barHeight + textOffsetY + baseTextOffsetY;
				DrawHealthbar(g, rect, maxColor, aZombie->mShieldMaxHealth, Color(0, 255, 255), aZombie->mShieldHealth, barWidth, barHeight, 0, barOffsetY, textColor, FONT_BRIANNETOD12, textOffsetY, Color::Black, textOutlineOffset, drawBarOutline);
			}
			break;
		}
		case RenderObjectType::RENDER_ITEM_HEALTHBAR_PLANT:
		{
			Plant* aPlant = aRenderItem.mPlant;
			Rect rect = aPlant->GetPlantRect();
			int barWidth = 55;
			int barHeight = 10;
			int barOffsetY = 0;
			int baseBarOffsetY = 3;
			int textOffsetY = 3;
			int baseTextOffsetY = 14;
			int textOutlineOffset = 1;
			bool isPumpkin = aPlant->mSeedType == SeedType::SEED_PUMPKINSHELL || aPlant->mImitaterType == SeedType::SEED_PUMPKINSHELL;
			Color baseColor;
			if (Plant::IsUpgrade(aPlant->mSeedType))
				baseColor = Color(170, 122, 210);
			else if (isPumpkin)
				baseColor = Color(255, 188, 32);
			else
				baseColor = Color(0, 255, 0);
			Color maxColor = Color(255, 0, 0);
			if (aPlant->mSeedType == SeedType::SEED_IMITATER || aPlant->mImitaterType != SEED_NONE)
			{
				int imitaterColor = 125;
				for (int i = 0; i <= 2; i++)
				{
					switch (i)
					{
					case 0:
						baseColor.mRed = min(255, baseColor.mRed + imitaterColor);
						maxColor.mRed = min(255, maxColor.mRed + imitaterColor);
						break;
					case 1:
						baseColor.mGreen = min(255, baseColor.mGreen + imitaterColor);
						maxColor.mGreen = min(255, maxColor.mGreen + imitaterColor);
						break;
					case 2:
						baseColor.mBlue = min(255, baseColor.mBlue + imitaterColor);
						maxColor.mBlue = min(255, maxColor.mBlue + imitaterColor);
						break;
					}
				}
			}
			Color textColor = Color::White;
			bool drawBarOutline = true;
			if (aPlant->mPlantHealth > 0)
			{
				barOffsetY += baseBarOffsetY;
				if (isPumpkin)
					barOffsetY += barHeight + textOffsetY + baseTextOffsetY;
				DrawHealthbar(g, rect, maxColor, aPlant->mPlantMaxHealth, baseColor, aPlant->mPlantHealth, barWidth, barHeight, (aPlant->mSeedType != SeedType::SEED_IMITATER && isPumpkin) || aPlant->mSeedType == SeedType::SEED_TALLNUT ? 10 : 0, barOffsetY, textColor, FONT_BRIANNETOD12, textOffsetY, Color::Black, textOutlineOffset, drawBarOutline);
			}
			break;
		}

		default:
			TOD_ASSERT();
			break;
		}
	}

	TodHesitationTrace("end draw");
}

bool Board::HasProgressMeter()
{
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED || 
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST || 
		mApp->IsFinalBossLevel() || 
		mApp->IsSlotMachineLevel() || 
		mApp->IsSquirrelLevel() || 
		mApp->IsIZombieLevel())
		return true;

	if (mProgressMeterWidth == 0)
		return false;

	if (mApp->IsContinuousChallenge() || 
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN || 
		mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM || 
		mApp->IsScaryPotterLevel())
		return false;

	return true;
}

bool Board::ProgressMeterHasFlags()
{
	if (mApp->IsFirstTimeAdventureMode() && mLevel == 1)
		return false;

	if (mApp->IsWhackAZombieLevel() ||
		mApp->IsFinalBossLevel() ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST ||
		mApp->IsSlotMachineLevel() ||
		mApp->IsSquirrelLevel() ||
		mApp->IsIZombieLevel())
		return false;

	return true;
}

void Board::DrawProgressMeter(Graphics* g)
{
	if (!HasProgressMeter())
		return;

	int aImagePosX = BOARD_WIDTH - 200;
	int aImagePosY = BOARD_HEIGHT - 25;
	g->DrawImageCel(Sexy::IMAGE_FLAGMETER, aImagePosX, aImagePosY, 0);
	int aCelWidth = Sexy::IMAGE_FLAGMETER->GetCelWidth();
	int aCelHeight = Sexy::IMAGE_FLAGMETER->GetCelHeight();
	int aClipWidth = TodAnimateCurve(0, PROGRESS_METER_COUNTER, mProgressMeterWidth, 0, 143, TodCurves::CURVE_LINEAR);
	Rect aSrcRect(aCelWidth - aClipWidth - 7, aCelHeight, aClipWidth, aCelHeight);
	Rect aDstRect(aCelWidth - aClipWidth + aImagePosX - 7, aImagePosY, aClipWidth, aCelHeight);
	g->DrawImage(Sexy::IMAGE_FLAGMETER, aDstRect, aSrcRect);
	
	int aPosX = aCelWidth / 2 + aImagePosX;
	Color aColor(224, 187, 98);
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST)
	{
		SexyString aMatchStr = StrFormat(_S("%d / %d %s"), mChallenge->mChallengeScore, 75, TodStringTranslate(_S("[MATCHES]")).c_str());
		TodDrawString(g, aMatchStr, aPosX, aImagePosY + 14, Sexy::FONT_DWARVENTODCRAFT12, aColor, DrawStringJustification::DS_ALIGN_CENTER);
	}
	else if (mApp->IsSquirrelLevel())
	{
		SexyString aMatchStr = StrFormat(_S("%d / %d %s"), mChallenge->mChallengeScore, 7, TodStringTranslate(_S("[SQUIRRELS]")).c_str());
		TodDrawString(g, aMatchStr, aPosX, aImagePosY + 14, Sexy::FONT_DWARVENTODCRAFT12, aColor, DrawStringJustification::DS_ALIGN_CENTER);
	}
	else if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_SLOT_MACHINE)
	{
		int aSunMoney = ClampInt(mSunMoney, 0, SLOT_MACHINE_WINNING_SCORE);
		SexyString aMatchStr = StrFormat(_S("%d / %d %s"), aSunMoney, SLOT_MACHINE_WINNING_SCORE, TodStringTranslate(_S("[SUN]")).c_str());
		TodDrawString(g, aMatchStr, aPosX, aImagePosY + 14, Sexy::FONT_DWARVENTODCRAFT12, aColor, DrawStringJustification::DS_ALIGN_CENTER);
	}
	else if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZOMBIQUARIUM)
	{
		int aSunMoney = ClampInt(mSunMoney, 0, ZOMBIQUARIUM_WINNING_SCORE);
		SexyString aMatchStr = StrFormat(_S("%d / %d %s"), aSunMoney, ZOMBIQUARIUM_WINNING_SCORE, TodStringTranslate(_S("[SUN]")).c_str());
		TodDrawString(g, aMatchStr, aPosX, aImagePosY + 14, Sexy::FONT_DWARVENTODCRAFT12, aColor, DrawStringJustification::DS_ALIGN_CENTER);
	}
	else if (mApp->IsIZombieLevel())
	{
		SexyString aMatchStr = StrFormat(_S("%d / %d %s"), mChallenge->mChallengeScore, 5, TodStringTranslate(_S("[BRAINS]")).c_str());
		TodDrawString(g, aMatchStr, aPosX, aImagePosY + 14, Sexy::FONT_DWARVENTODCRAFT12, aColor, DrawStringJustification::DS_ALIGN_CENTER);
	}
	else if (ProgressMeterHasFlags())
	{
		int aNumWavesPerFlag = GetNumWavesPerFlag();
		int aNumFlagWaves = mNumWaves / aNumWavesPerFlag;
		int aFlagsPosEnd = aImagePosX + aCelWidth - 10;
		for (int aFlagWave = 1; aFlagWave <= aNumFlagWaves; aFlagWave++)
		{
			int aHeight = 0;
			int aTotalWavesAtFlag = aFlagWave * aNumWavesPerFlag;
			if (aTotalWavesAtFlag < mCurrentWave)
			{
				aHeight = 14;
			}
			else if (aTotalWavesAtFlag == mCurrentWave)
			{
				aHeight = TodAnimateCurve(100, 0, mFlagRaiseCounter, 0, 14, TodCurves::CURVE_LINEAR);
			}
			int aPosX = TodAnimateCurve(0, mNumWaves, aTotalWavesAtFlag, aFlagsPosEnd, aImagePosX + 6, TodCurves::CURVE_LINEAR);
			g->DrawImageCel(Sexy::IMAGE_FLAGMETERPARTS, aPosX, aImagePosY - 4, 1, 0);
			g->DrawImageCel(Sexy::IMAGE_FLAGMETERPARTS, aPosX, aImagePosY - aHeight - 3, 2, 0);
		}
	}

	g->DrawImage(Sexy::IMAGE_FLAGMETERLEVELPROGRESS, aImagePosX + 38, aImagePosY + 14);
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED || 
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZOMBIQUARIUM || 
		mApp->IsSquirrelLevel() || 
		mApp->IsSlotMachineLevel() ||
		mApp->IsIZombieLevel() || 
		mApp->IsFinalBossLevel())
		return;
	int aHeadProgress = TodAnimateCurve(0, 150, mProgressMeterWidth, 0, 135, CURVE_LINEAR);
	g->DrawImageCel(Sexy::IMAGE_FLAGMETERPARTS, aCelWidth - aHeadProgress + aImagePosX - 20, aImagePosY - 3, 0, 0);
}

void Board::DrawHouseDoorBottom(Graphics* g)
{
	switch (mBackground)
	{
	case BackgroundType::BACKGROUND_1_DAY:		g->DrawImage(Sexy::IMAGE_BACKGROUND1_GAMEOVER_INTERIOR_OVERLAY, -126 + BOARD_ADDITIONAL_WIDTH, 225 + BOARD_OFFSET_Y);		break;
	case BackgroundType::BACKGROUND_2_NIGHT:	g->DrawImage(Sexy::IMAGE_BACKGROUND2_GAMEOVER_INTERIOR_OVERLAY, -125 + BOARD_ADDITIONAL_WIDTH, 196 + BOARD_OFFSET_Y);		break;
	case BackgroundType::BACKGROUND_3_POOL:		g->DrawImage(Sexy::IMAGE_BACKGROUND3_GAMEOVER_INTERIOR_OVERLAY, -171 + BOARD_ADDITIONAL_WIDTH, 241 + BOARD_OFFSET_Y);		break;
	case BackgroundType::BACKGROUND_4_FOG:		g->DrawImage(Sexy::IMAGE_BACKGROUND4_GAMEOVER_INTERIOR_OVERLAY, -172 + BOARD_ADDITIONAL_WIDTH, 246 + BOARD_OFFSET_Y);		break;
	default:																																								break;
	}
}

void Board::DrawHouseDoorTop(Graphics* g)
{
	switch (mBackground)
	{
	case BackgroundType::BACKGROUND_1_DAY:		g->DrawImage(Sexy::IMAGE_BACKGROUND1_GAMEOVER_MASK, -130 + BOARD_ADDITIONAL_WIDTH, 202 + BOARD_OFFSET_Y);		break;
	case BackgroundType::BACKGROUND_2_NIGHT:	g->DrawImage(Sexy::IMAGE_BACKGROUND2_GAMEOVER_MASK, -128 + BOARD_ADDITIONAL_WIDTH, 207 + BOARD_OFFSET_Y);		break;
	case BackgroundType::BACKGROUND_3_POOL:		g->DrawImage(Sexy::IMAGE_BACKGROUND3_GAMEOVER_MASK, -172 + BOARD_ADDITIONAL_WIDTH, 234 + BOARD_OFFSET_Y);		break;
	case BackgroundType::BACKGROUND_4_FOG:		g->DrawImage(Sexy::IMAGE_BACKGROUND4_GAMEOVER_MASK, -173 + BOARD_ADDITIONAL_WIDTH, 133 + BOARD_OFFSET_Y);		break;
	case BackgroundType::BACKGROUND_5_ROOF:		g->DrawImage(Sexy::IMAGE_BACKGROUND5_GAMEOVER_MASK, -220 + BOARD_ADDITIONAL_WIDTH, 81 + BOARD_OFFSET_Y);		break;
	case BackgroundType::BACKGROUND_6_BOSS:		g->DrawImage(Sexy::IMAGE_BACKGROUND6_GAMEOVER_MASK, -220 + BOARD_ADDITIONAL_WIDTH, 81 + BOARD_OFFSET_Y);		break;
	default:																																					break;
	}
}

void Board::DrawLevel(Graphics* g)
{
	SexyString aLevelStr;
	if (mApp->IsAdventureMode())
	{
		aLevelStr = TodReplaceString(mApp->mPlayingQuickplay ? _S("[QUICK_PLAY_LEVEL]") : _S("[LEVEL]"), _S("{LEVEL}"), mApp->GetStageString(mLevel).erase(0, 1));
	}
	else
	{
		aLevelStr = mApp->GetCurrentChallengeDef().mChallengeName;
		if (mApp->IsSurvivalMode() || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND)
		{
			int aFlags = GetSurvivalFlagsCompleted();
			if (aFlags > 0)
			{
				SexyString aFlagStr = mApp->Pluralize(aFlags, _S("[ONE_FLAG]"), _S("[COUNT_FLAGS]"));
				SexyString aCompletedStr = TodReplaceString(_S("[FLAGS_COMPLETED]"), _S("{FLAGS}"), aFlagStr);
				aLevelStr = StrFormat(_S("%s - %s"), TodStringTranslate(aLevelStr).c_str(), aCompletedStr.c_str());
			}
		}
		else if (mApp->IsEndlessIZombie(mApp->mGameMode) || mApp->IsEndlessScaryPotter(mApp->mGameMode))
		{
			int aStreak = mChallenge->mSurvivalStage;
			if (mNextSurvivalStageCounter > 0)
			{
				aStreak++;
			}
			if (aStreak > 0)
			{
				SexyString aStreakStr = TodReplaceNumberString(_S("[ENDLESS_STREAK]"), _S("{STREAK}"), aStreak);
				aLevelStr = StrFormat(_S("%s - %s"), TodStringTranslate(aLevelStr).c_str(), aStreakStr.c_str());
			}
		}
	}
	

	int aPosX = BOARD_WIDTH - 20;
	int aPosY = BOARD_HEIGHT - 5;
	if (HasProgressMeter())
	{
		aPosX = BOARD_WIDTH - 207;
	}
	if (mChallenge->mChallengeState == ChallengeState::STATECHALLENGE_ZEN_FADING)
	{
		aPosY += TodAnimateCurve(50, 0, mChallenge->mChallengeStateCounter, 0, 50, TodCurves::CURVE_EASE_IN_OUT);
	}
	TodDrawString(g, aLevelStr, aPosX, aPosY, Sexy::FONT_HOUSEOFTERROR16, Color(224, 187, 98), DrawStringJustification::DS_ALIGN_RIGHT);
}

void Board::DrawZenWheelBarrowButton(Graphics* g, int theOffsetY)
{
	Rect aButtonRect = GetShovelButtonRect();
	GetZenButtonRect(GameObjectType::OBJECT_TYPE_WHEELBARROW, aButtonRect);
	PottedPlant* aPlant = mApp->mZenGarden->GetPottedPlantInWheelbarrow();
	if (aPlant && mCursorObject->mCursorType != CursorType::CURSOR_TYPE_PLANT_FROM_WHEEL_BARROW)
	{
		if (mChallenge->mChallengeState == ChallengeState::STATECHALLENGE_ZEN_FADING)
		{
			g->DrawImage(Sexy::IMAGE_ZEN_WHEELBARROW, aButtonRect.mX - 7, aButtonRect.mY + theOffsetY - 3);
		}
		else
		{
			g->DrawImage(Sexy::IMAGE_ZEN_WHEELBARROW, aButtonRect.mX - 7, aButtonRect.mY + theOffsetY + 4);
		}

		if (aPlant->mPlantAge == PottedPlantAge::PLANTAGE_SMALL)
		{
			mApp->mZenGarden->DrawPottedPlant(g, aButtonRect.mX + 23, aButtonRect.mY + theOffsetY - 8, aPlant, 0.6f, true);
		}
		else if (aPlant->mPlantAge == PottedPlantAge::PLANTAGE_MEDIUM)
		{
			mApp->mZenGarden->DrawPottedPlant(g, aButtonRect.mX + 28, aButtonRect.mY + theOffsetY + 2, aPlant, 0.5f, true);
		}
		else
		{
			mApp->mZenGarden->DrawPottedPlant(g, aButtonRect.mX + 34, aButtonRect.mY + theOffsetY + 12, aPlant, 0.4f, true);
		}
	}
	else
	{
		g->DrawImage(Sexy::IMAGE_ZEN_WHEELBARROW, aButtonRect.mX - 7, aButtonRect.mY + theOffsetY - 3);
	}
}

void Board::DrawZenButtons(Graphics* g)
{
	int aOffsetY = 0;
	if (mChallenge->mChallengeState == ChallengeState::STATECHALLENGE_ZEN_FADING)
	{
		aOffsetY = TodAnimateCurve(50, 0, mChallenge->mChallengeStateCounter, 0, -72, TodCurves::CURVE_EASE_IN_OUT);
	}

	for (GameObjectType aTool = GameObjectType::OBJECT_TYPE_WATERING_CAN; aTool <= GameObjectType::OBJECT_TYPE_NEXT_GARDEN; aTool = (GameObjectType)(aTool + 1))
	{
		if (!CanUseGameObject(aTool))
			continue;

		Rect aButtonRect = GetShovelButtonRect();
		if (aTool == GameObjectType::OBJECT_TYPE_NEXT_GARDEN)
		{
			aButtonRect.mX = 564 + BOARD_ADDITIONAL_WIDTH;
			if (!mMenuButton->mBtnNoDraw)
			{
				g->DrawImage(Sexy::IMAGE_ZEN_NEXTGARDEN, aButtonRect.mX + 2, aButtonRect.mY + aOffsetY);
			}
		}
		else
		{
			GetZenButtonRect(aTool, aButtonRect);
			g->DrawImage(Sexy::IMAGE_SHOVELBANK, aButtonRect.mX, aButtonRect.mY + aOffsetY);
			if ((int)mCursorObject->mCursorType == (int)CursorType::CURSOR_TYPE_WATERING_CAN + (int)aTool - 6)
			{
				continue;  
			}

			if (aTool == GameObjectType::OBJECT_TYPE_WATERING_CAN)
			{
				if (mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_GOLD_WATERINGCAN])
				{
					g->DrawImage(Sexy::IMAGE_WATERINGCANGOLD, aButtonRect.mX - 2, aButtonRect.mY + aOffsetY - 6);
				}
				else
				{
					g->DrawImage(Sexy::IMAGE_WATERINGCAN, aButtonRect.mX - 2, aButtonRect.mY + aOffsetY - 6);
				}
			}
			else if (aTool == GameObjectType::OBJECT_TYPE_FERTILIZER)
			{
				int aCharges = mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_FERTILIZER] - PURCHASE_COUNT_OFFSET;
				if (aCharges == 0)
				{
					g->SetColorizeImages(true);
					g->SetColor(Color(96, 96, 96));
				}
				else if (mTutorialState == TutorialState::TUTORIAL_ZEN_GARDEN_FERTILIZE_PLANTS)
				{
					g->SetColorizeImages(true);
					g->SetColor(GetFlashingColor(mMainCounter, 75));
				}
				g->DrawImage(Sexy::IMAGE_FERTILIZER, aButtonRect.mX - 6, aButtonRect.mY + aOffsetY - 7);
				g->SetColorizeImages(false);

				SexyString aChargeString = StrFormat(_S("x%d"), aCharges);
				TodDrawString(g, aChargeString, aButtonRect.mX + 64, aButtonRect.mY + aOffsetY + 65, Sexy::FONT_HOUSEOFTERROR16, Color::White, DS_ALIGN_RIGHT);
			}
			else if (aTool == GameObjectType::OBJECT_TYPE_BUG_SPRAY)
			{
				int aCharges = mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_BUG_SPRAY] - PURCHASE_COUNT_OFFSET;
				if (aCharges == 0)
				{
					g->SetColorizeImages(true);
					g->SetColor(Color(128, 128, 128));
				}
				g->DrawImage(Sexy::IMAGE_BUG_SPRAY, aButtonRect.mX, aButtonRect.mY + aOffsetY - 1);
				g->SetColorizeImages(false);

				SexyString aChargeString = StrFormat(_S("x%d"), aCharges);
				TodDrawString(g, aChargeString, aButtonRect.mX + 64, aButtonRect.mY + aOffsetY + 65, Sexy::FONT_HOUSEOFTERROR16, Color::White, DS_ALIGN_RIGHT);
			}
			else if (aTool == GameObjectType::OBJECT_TYPE_PHONOGRAPH)
			{
				g->DrawImage(Sexy::IMAGE_PHONOGRAPH, aButtonRect.mX + 2, aButtonRect.mY + aOffsetY + 2);
			}
			else if (aTool == GameObjectType::OBJECT_TYPE_CHOCOLATE)
			{
				int aCharges = mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_CHOCOLATE] - PURCHASE_COUNT_OFFSET;
				if (aCharges == 0)
				{
					g->SetColorizeImages(true);
					g->SetColor(Color(128, 128, 128));
				}
				g->DrawImage(Sexy::IMAGE_CHOCOLATE, aButtonRect.mX + 6, aButtonRect.mY + aOffsetY + 4);
				g->SetColorizeImages(false);

				SexyString aChargeString = StrFormat(_S("x%d"), aCharges);
				TodDrawString(g, aChargeString, aButtonRect.mX + 64, aButtonRect.mY + aOffsetY + 65, Sexy::FONT_HOUSEOFTERROR16, Color::White, DS_ALIGN_RIGHT);
			}
			else if (aTool == GameObjectType::OBJECT_TYPE_GLOVE)
			{
				if (mCursorObject->mCursorType != CursorType::CURSOR_TYPE_PLANT_FROM_GLOVE && 
					mCursorObject->mCursorType != CursorType::CURSOR_TYPE_PLANT_FROM_WHEEL_BARROW)
				{
					g->DrawImage(Sexy::IMAGE_ZEN_GARDENGLOVE, aButtonRect.mX - 6, aButtonRect.mY + aOffsetY - 4);
				}
			}
			else if (aTool == GameObjectType::OBJECT_TYPE_MONEY_SIGN)
			{
				g->DrawImage(Sexy::IMAGE_ZEN_MONEYSIGN, aButtonRect.mX - 5, aButtonRect.mY + aOffsetY - 4);
			}
			else if (aTool == GameObjectType::OBJECT_TYPE_WHEELBARROW)
			{
				DrawZenWheelBarrowButton(g, aOffsetY);
			}
			else if (aTool == GameObjectType::OBJECT_TYPE_TREE_FOOD)
			{
				int aCharges = mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_TREE_FOOD] - PURCHASE_COUNT_OFFSET;
				if (aCharges <= 0)
				{
					g->SetColorizeImages(true);
					g->SetColor(Color(128, 128, 128));
					aCharges = 0;
				}
				if (!mChallenge->TreeOfWisdomCanFeed())
				{
					g->SetColorizeImages(true);
					g->SetColor(Color(128, 128, 128));
				}
				g->DrawImage(Sexy::IMAGE_TREEFOOD, aButtonRect.mX - 6, aButtonRect.mY + aOffsetY - 7);
				g->SetColorizeImages(false);

				SexyString aChargeString = StrFormat(_S("x%d"), aCharges);
				TodDrawString(g, aChargeString, aButtonRect.mX + 64, aButtonRect.mY + aOffsetY + 65, Sexy::FONT_HOUSEOFTERROR16, Color::White, DS_ALIGN_RIGHT);
			}
		}
	}
}

void Board::DrawShovel(Graphics* g)
{
	Rect aShovelRect = GetShovelButtonRect();
    if (mShowShovel)
        g->DrawImage(Sexy::IMAGE_SHOVELBANK, aShovelRect.mX, aShovelRect.mY);
    if (IsRollSeedBank() && mGardenGloveEnabled)
    {
        const Rect aGloveRect = GetGloveButtonRect();
        g->DrawImage(Sexy::IMAGE_SHOVELBANK, aGloveRect.mX, aGloveRect.mY);
        if (mCursorObject->mCursorType != CursorType::CURSOR_TYPE_GLOVE)
            g->DrawImage(Sexy::IMAGE_ZEN_GARDENGLOVE, aGloveRect.mX - 4, aGloveRect.mY - 4);
    }

    if (mShowShovel && mCursorObject->mCursorType != CursorType::CURSOR_TYPE_SHOVEL)
	{
		if (mChallenge->mChallengeState == (ChallengeState)15)
		{
			//scrapped idea prob 
			g->SetColorizeImages(true);
			g->SetColor(GetFlashingColor(mMainCounter, 75));
		}
		g->DrawImage(Sexy::IMAGE_SHOVEL, aShovelRect.mX - 7, aShovelRect.mY - 3);
		g->SetColorizeImages(false);
	}
    // Both tools have independent real-time cooldowns. The numbers are drawn
    // using existing game fonts; there are no additional textures or sprites.
    if (IsRollSeedBank())
    {
        if (mShowShovel && IsRollToolRecharging(GameObjectType::OBJECT_TYPE_SHOVEL))
        {
            const int remain = max(1, (int)((mShovelCooldownUntil - GetTickCount() + 999U) / 1000U));
            TodDrawString(g, StrFormat(_S("%ds"), remain), aShovelRect.mX + aShovelRect.mWidth / 2,
                aShovelRect.mY + aShovelRect.mHeight - 9, Sexy::FONT_HOUSEOFTERROR16,
                Color(245, 65, 55), DS_ALIGN_CENTER);
        }
        if (mGardenGloveEnabled && IsRollToolRecharging(GameObjectType::OBJECT_TYPE_GLOVE))
        {
            const Rect glove = GetGloveButtonRect();
            const int remain = max(1, (int)((mGloveCooldownUntil - GetTickCount() + 999U) / 1000U));
            TodDrawString(g, StrFormat(_S("%ds"), remain), glove.mX + glove.mWidth / 2,
                glove.mY + glove.mHeight - 9, Sexy::FONT_HOUSEOFTERROR16,
                Color(245, 65, 55), DS_ALIGN_CENTER);
        }
    }


}

void Board::DrawDebugText(Graphics* g)
{
	SexyString aText;

	switch (mDebugTextMode)
	{
	case DebugTextMode::DEBUG_TEXT_NONE:
		break;

	case DebugTextMode::DEBUG_TEXT_ZOMBIE_SPAWN:
	{
		int aTime = mZombieCountDownStart - mZombieCountDown;
		float aCountDownFraction = (float)aTime / (float)mZombieCountDownStart;

		aText += StrFormat(_S("ZOMBIE SPAWNING DEBUG\n"));
		aText += StrFormat(_S("CurrentWave: %d of %d\n"), mCurrentWave, mNumWaves);
		aText += StrFormat(_S("TimeSinseLastSpawn: %d %s\n"), aTime, aTime > 400 ? _S("") : _S("(too soon)"));
		aText += StrFormat(_S("ZombieCountDown: %d/%d (%.0f%%)\n"), mZombieCountDown, mZombieCountDownStart, aCountDownFraction);

		if (mZombieHealthToNextWave != -1)
		{
			int aTotalHealth = TotalZombiesHealthInWave(mCurrentWave - 1);
			int aHealthRange = max(mZombieHealthWaveStart - mZombieHealthToNextWave, 1);
			float aHealthFraction = (float)(mZombieHealthToNextWave - aTotalHealth + aHealthRange) / (float)aHealthRange;
			aText += StrFormat(_S("ZombieHealth: CurZombieHealth %d trigger %d (%.0f%%)\n"), aTotalHealth, mZombieHealthToNextWave, aHealthFraction * 100);
		}
		else
		{
			aText += StrFormat(_S("ZombieHealth: before first wave\n"));
		}

		if (mHugeWaveCountDown > 0)
		{
			aText += StrFormat(_S("HugeWaveCountDown: %d\n"), mHugeWaveCountDown);
		}

		Zombie* aBossZombie = GetBossZombie();
		if (aBossZombie)
		{
			aText += StrFormat(_S("\nSpawn: %d\n"), aBossZombie->mSummonCounter);
			aText += StrFormat(_S("Stomp: %d\n"), aBossZombie->mBossStompCounter);
			aText += StrFormat(_S("Bungee: %d\n"), aBossZombie->mBossBungeeCounter);
			aText += StrFormat(_S("Head: %d\n"), aBossZombie->mBossHeadCounter);
			aText += StrFormat(_S("Health: %d of %d\n"), aBossZombie->mBodyHealth, aBossZombie->mBodyMaxHealth);
		}

		break;
	}

	case DebugTextMode::DEBUG_TEXT_MUSIC:
	{
		aText += StrFormat(_S("MUSIC DEBUG\n"));
		aText += StrFormat(_S("CurrentWave: %d of %d\n"), mCurrentWave, mNumWaves);

		if (mApp->mMusic->mCurMusicFileMain == MusicFile::MUSIC_FILE_NONE)
		{
			aText += StrFormat(_S("No music"));
		}
		else
		{
			aText += StrFormat(_S("Music Burst: "));

			if (mApp->mMusic->mMusicBurstState == MusicBurstState::MUSIC_BURST_OFF)
			{
				aText += StrFormat(_S("Off"));
			}
			else if (mApp->mMusic->mMusicBurstState == MusicBurstState::MUSIC_BURST_STARTING)
			{
				aText += StrFormat(_S("Starting %d/%d"), mApp->mMusic->mBurstStateCounter, 400);
			}
			else if (mApp->mMusic->mMusicBurstState == MusicBurstState::MUSIC_BURST_ON)
			{
				aText += StrFormat(_S("On at least until %d/%d"), mApp->mMusic->mBurstStateCounter, 800);
			}
			else if (mApp->mMusic->mMusicBurstState == MusicBurstState::MUSIC_BURST_FINISHING)
			{
				aText += StrFormat(_S("Finishing %d/%d"), mApp->mMusic->mBurstStateCounter, 400);
			}

			if (mApp->mMusic->mMusicDrumsState == MusicDrumsState::MUSIC_DRUMS_OFF)
			{
				aText += StrFormat(_S(", Drums off"));
			}
			else if (mApp->mMusic->mMusicDrumsState == MusicDrumsState::MUSIC_DRUMS_ON_QUEUED)
			{
				aText += StrFormat(_S(", Drums queued on"));
			}
			else if (mApp->mMusic->mMusicDrumsState == MusicDrumsState::MUSIC_DRUMS_ON)
			{
				aText += StrFormat(_S(", Drums on"));
			}
			else if (mApp->mMusic->mMusicDrumsState == MusicDrumsState::MUSIC_DRUMS_OFF_QUEUED)
			{
				aText += StrFormat(_S(", Drums queued off"));
			}
			else if (mApp->mMusic->mMusicDrumsState == MusicDrumsState::MUSIC_DRUMS_FADING)
			{
				aText += StrFormat(_S(", Drums fading off %d/%d"), mApp->mMusic->mDrumsStateCounter, 50);
			}
			aText += StrFormat(_S("\n"));

			int aPackedOrderMain = mApp->mMusic->GetMusicOrder(mApp->mMusic->mCurMusicFileMain);
			int aCurrentOrder = LOWORD(aPackedOrderMain);
			aText += StrFormat(_S("Music order %02d row %02d\n"), LOWORD(aPackedOrderMain), HIWORD(aPackedOrderMain) / 4);
			if (mApp->mMusic->mCurMusicTune == MusicTune::MUSIC_TUNE_DAY_GRASSWALK ||
				mApp->mMusic->mCurMusicTune == MusicTune::MUSIC_TUNE_POOL_WATERYGRAVES ||
				mApp->mMusic->mCurMusicTune == MusicTune::MUSIC_TUNE_FOG_RIGORMORMIST ||
				mApp->mMusic->mCurMusicTune == MusicTune::MUSIC_TUNE_ROOF_GRAZETHEROOF)
			{
				int aPackedOrderHihats = mApp->mMusic->GetMusicOrder(mApp->mMusic->mCurMusicFileHihats);
				int aPackedOrderDrums = mApp->mMusic->GetMusicOrder(mApp->mMusic->mCurMusicFileDrums);
				if (aCurrentOrder == LOWORD(aPackedOrderHihats) && aCurrentOrder == LOWORD(aPackedOrderDrums))
				{
					int aDiffHihats = HIWORD(aPackedOrderHihats) - HIWORD(aPackedOrderMain);
					int aDiffDrums = HIWORD(aPackedOrderDrums) - HIWORD(aPackedOrderMain);
					if (abs(aDiffHihats) > 1 || abs(aDiffDrums) > 1)
					{
						aText += StrFormat(_S("Music unsynced hihats %d drums %d\n"), aDiffHihats, aDiffDrums);
					}
				}

				HMUSIC aMusicHandle1 = mApp->mMusic->GetBassMusicHandle(mApp->mMusic->mCurMusicFileMain);
				HMUSIC aMusicHandle2 = mApp->mMusic->GetBassMusicHandle(mApp->mMusic->mCurMusicFileHihats);
				HMUSIC aMusicHandle3 = mApp->mMusic->GetBassMusicHandle(mApp->mMusic->mCurMusicFileDrums);
				//int bpm1 = gBass->BASS_MusicGetAttribute(aMusicHandle1, BASS_MUSIC_ATTRIB_BPM);
				//int bpm2 = gBass->BASS_MusicGetAttribute(aMusicHandle2, BASS_MUSIC_ATTRIB_BPM);
				//int bpm3 = gBass->BASS_MusicGetAttribute(aMusicHandle3, BASS_MUSIC_ATTRIB_BPM);
				//aText += StrFormat(_S("bpm1 %d bmp2 %d bpm3 %d\n"), bpm1, bpm2, bpm3);
			}
			else if (mApp->mMusic->mCurMusicTune == MusicTune::MUSIC_TUNE_NIGHT_MOONGRAINS)
			{
				int aPackedOrderDrums = mApp->mMusic->GetMusicOrder(mApp->mMusic->mCurMusicFileDrums);
				aText += StrFormat(_S("Drum order %02d row %02d\n"), LOWORD(aPackedOrderDrums), HIWORD(aPackedOrderDrums) / 4);
				int aDiffDrums = HIWORD(aPackedOrderDrums) - HIWORD(aPackedOrderMain);
				if (abs(aDiffDrums) > 0 && abs(aDiffDrums) <= 128)
				{
					aText += StrFormat(_S("Drums unsynced %d"), aDiffDrums);
				}
			}
		}

		break;
	}

	case DebugTextMode::DEBUG_TEXT_MEMORY:
		aText += StrFormat(_S("MEMORY DEBUG\n"));
		aText += StrFormat(_S("attachments %d\n"), mApp->mEffectSystem->mAttachmentHolder->mAttachments.mSize);
		aText += StrFormat(_S("emitters %d\n"), mApp->mEffectSystem->mParticleHolder->mEmitters.mSize);
		aText += StrFormat(_S("particles %d\n"), mApp->mEffectSystem->mParticleHolder->mParticles.mSize);
		aText += StrFormat(_S("particle systems %d\n"), mApp->mEffectSystem->mParticleHolder->mParticleSystems.mSize);
		aText += StrFormat(_S("trails %d\n"), mApp->mEffectSystem->mTrailHolder->mTrails.mSize);
		aText += StrFormat(_S("reanimation %d\n"), mApp->mEffectSystem->mReanimationHolder->mReanimations.mSize);
		aText += StrFormat(_S("zombies %d\n"), mZombies.mSize);
		aText += StrFormat(_S("plants %d\n"), mPlants.mSize);
		aText += StrFormat(_S("projectiles %d\n"), mProjectiles.mSize);
		aText += StrFormat(_S("coins %d\n"), mCoins.mSize);
		aText += StrFormat(_S("lawn mowers %d\n"), mLawnMowers.mSize);
		aText += StrFormat(_S("grid items %d\n"), mGridItems.mSize);
		aText += StrFormat(_S("bushes %d\n"), mBushes.mSize);
		aText += StrFormat(_S("gamepad players %d\n"), mGamepadPlayers.mSize);
		break;

	case DebugTextMode::DEBUG_TEXT_COLLISION:
		aText += StrFormat(_S("COLLISION DEBUG\n"));
		break;

	default:
		TOD_ASSERT();
		break;
	}

	g->SetFont(mDebugFont);
	g->SetColor(Color::Black);
	g->DrawStringWordWrapped(aText, 10, 89);
	g->DrawStringWordWrapped(aText, 11, 91);
	g->DrawStringWordWrapped(aText, 9, 90);
	g->DrawStringWordWrapped(aText, 11, 90);
	g->SetColor(Color(255, 255, 255));
	g->DrawStringWordWrapped(aText, 10, 90);
}

void Board::DrawDebugObjectRects(Graphics* g)
{
	if (mDebugTextMode != DebugTextMode::DEBUG_TEXT_COLLISION)
		return;

	{
		Plant* aPlant = nullptr;
		while (IteratePlants(aPlant))
		{
			Rect aRect = aPlant->GetPlantRect();
			g->SetColor(Color(0, 255, 0));
			g->DrawRect(aRect);

			Rect aAttackRect = aPlant->GetPlantAttackRect(PlantWeapon::WEAPON_PRIMARY);
			if (aAttackRect.mWidth < BOARD_WIDTH)
			{
				g->SetColor(Color(255, 0, 0));
				g->DrawRect(aAttackRect);
			}

			Rect aSecondaryRect = aPlant->GetPlantAttackRect(PlantWeapon::WEAPON_SECONDARY);
			if (aSecondaryRect.mWidth < BOARD_WIDTH)
			{
				g->SetColor(Color(255, 0, 128));
				g->DrawRect(aSecondaryRect);
			}
		}
	}
	{
		Zombie* aZombie = nullptr;
		while (IterateZombies(aZombie))
		{
			if (!aZombie->IsDeadOrDying())
			{
				Rect aRect = aZombie->GetZombieRect();
				g->SetColor(Color(0, 255, 0));
				g->DrawRect(aRect);

				Rect aAttackRect = aZombie->GetZombieAttackRect();
				g->SetColor(Color(255, 0, 0));
				g->DrawRect(aAttackRect);
			}
		}
	}
	{
		LawnMower* aLawnMower = nullptr;
		while (IterateLawnMowers(aLawnMower))
		{
			Rect aAttackRect = aLawnMower->GetLawnMowerAttackRect();
			g->SetColor(Color(255, 0, 0));
			g->DrawRect(aAttackRect);
		}
	}
	{
		Projectile* aProjectile = nullptr;
		while (IterateProjectiles(aProjectile))
		{
			g->SetColor(Color(255, 0, 0));
			Rect aDamageRect = aProjectile->GetProjectileRect();
			g->DrawRect(aDamageRect);
		}
	}
}

void Board::DrawFadeOut(Graphics* g)
{
	if (mBoardFadeOutCounter < 0 || IsSurvivalStageWithRepick())
		return;

	int anAlpha = TodAnimateCurve(200, 0, mBoardFadeOutCounter, 0, 255, TodCurves::CURVE_LINEAR);
	if (mLevel == 9 || mLevel == 19 || mLevel == 29 || mLevel == 39 || mLevel == 49)
	{
		g->SetColor(Color(0, 0, 0, anAlpha));
	}
	else
	{
		g->SetColor(Color(255, 255, 255, anAlpha));
	}
	g->FillRect(0, 0, mWidth, mHeight);
}

void Board::DrawCover(Graphics* g)
{
	switch (mBackground)
	{
	case BACKGROUND_1_DAY:
		g->DrawImage(Sexy::IMAGE_BACKGROUND1_COVER, 685 + BOARD_ADDITIONAL_WIDTH, 557 + BOARD_OFFSET_Y);
		break;
	case BACKGROUND_2_NIGHT:
		g->DrawImage(Sexy::IMAGE_BACKGROUND2_COVER, 685 + BOARD_ADDITIONAL_WIDTH, 557 + BOARD_OFFSET_Y);
		break;
	case BACKGROUND_3_POOL:
		g->DrawImage(Sexy::IMAGE_BACKGROUND3_COVER, 671 + BOARD_ADDITIONAL_WIDTH, 613 + BOARD_OFFSET_Y);
		break;
	case BACKGROUND_4_FOG:
		g->DrawImage(Sexy::IMAGE_BACKGROUND4_COVER, 671 + BOARD_ADDITIONAL_WIDTH, 613 + BOARD_OFFSET_Y);
		break;
	case BACKGROUND_5_ROOF:
		g->DrawImage(Sexy::IMAGE_BACKGROUND5_TREES, mRoofTreeOffset, 0);
		g->DrawImage(Sexy::IMAGE_BACKGROUND5_POLE, mRoofPoleOffset, 0);
		break;
	case BACKGROUND_6_BOSS:
		g->DrawImage(Sexy::IMAGE_BACKGROUND6_TREES, mRoofTreeOffset, 0);
		g->DrawImage(Sexy::IMAGE_BACKGROUND6_POLE, mRoofPoleOffset, 0);
		break;
	}
}
void Board::DrawTopRightUI(Graphics* g)
{
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
	{
		int aButtonOffsetX = BOARD_ADDITIONAL_WIDTH * 2;
		if (mChallenge->mChallengeState == STATECHALLENGE_ZEN_FADING)
		{
			mMenuButton->mY = TodAnimateCurve(50, 0, mChallenge->mChallengeStateCounter, -10, -50, TodCurves::CURVE_EASE_IN_OUT);
			mStoreButton->mX = TodAnimateCurve(50, 0, mChallenge->mChallengeStateCounter, 678 + aButtonOffsetX, BOARD_WIDTH, TodCurves::CURVE_EASE_IN_OUT);
		}
		else
		{
			mMenuButton->mY = -10;
			mStoreButton->mX = 678 + aButtonOffsetX;
		}
	}

	if (mTutorialState == TutorialState::TUTORIAL_ZEN_GARDEN_COMPLETED)
	{
		g->SetColorizeImages(true);
		g->SetColor(GetFlashingColor(mMainCounter, 75));
	}

	g->SetColorizeImages(false);

	if (mStoreButton && mApp->mGameMode != GameMode::GAMEMODE_CHALLENGE_LAST_STAND)
	{
		if (mTutorialState == TutorialState::TUTORIAL_ZEN_GARDEN_VISIT_STORE)
		{
			g->SetColorizeImages(true);
			g->SetColor(GetFlashingColor(mMainCounter, 75));
		}
		mStoreButton->Draw(g);
		g->SetColorizeImages(false);
	}
}

void Board::DrawUIBottom(Graphics* g)
{
	if (mBackground == BackgroundType::BACKGROUND_ZOMBIQUARIUM)
	{
		int aWaveTime = abs(mMainCounter / 8 % 22 - 11);
		g->SetDrawMode(Graphics::DRAWMODE_ADDITIVE);
		int aOffset = -80;
		int aWidth = 160;
		int aWaves = BOARD_WIDTH / aWidth;
		for (int i = 0; i <= aWaves; i++)
			TodDrawImageCelScaled(g, i == 0 || i == aWaves ? Sexy::IMAGE_WAVESIDE : Sexy::IMAGE_WAVECENTER, i * aWidth + aOffset + (i == aWaves ? aWidth : 0), 40, 0, aWaveTime, i == aWaves ? -1.0f : 1.0f, 1.0f);
		g->SetDrawMode(Graphics::DRAWMODE_NORMAL);
	}

	if (mBackground == BackgroundType::BACKGROUND_GREENHOUSE || mBackground == BackgroundType::BACKGROUND_ZOMBIQUARIUM)
	{
		g->SetDrawMode(Graphics::DRAWMODE_ADDITIVE);
		g->DrawImage(
			IMAGE_BACKGROUND_GREENHOUSE_OVERLAY, 
			Rect(0, 0, BOARD_WIDTH, BOARD_HEIGHT), 
			Rect(0, 0, IMAGE_BACKGROUND_GREENHOUSE_OVERLAY->mWidth, IMAGE_BACKGROUND_GREENHOUSE_OVERLAY->mHeight)
		);
		g->SetDrawMode(Graphics::DRAWMODE_NORMAL);
	}

	if (mApp->mGameScene != GameScenes::SCENE_ZOMBIES_WON)
	{
		if (mSeedBank->BeginDraw(g))
		{
			mSeedBank->Draw(g);
			mSeedBank->EndDraw(g);
		}

        if (IsRollSeedBank() && mApp->mGameScene == GameScenes::SCENE_PLAYING)
        {
            const int aMouseX = mApp->mWidgetManager->mLastMouseX - mX;
            const int aMouseY = mApp->mWidgetManager->mLastMouseY - mY;
            int aHoveredIndex = -1;
            for (int i = 0; i < mBenchCount; ++i)
            {
                SeedPacket& aPacket = mBenchPackets[i];
                if (!mBenchMergeActive && !aPacket.mBenchMoving &&
                    aMouseX >= aPacket.mX + aPacket.mOffsetX && aMouseX < aPacket.mX + aPacket.mOffsetX + aPacket.mWidth &&
                    aMouseY >= aPacket.mY && aMouseY < aPacket.mY + aPacket.mHeight)
                    aHoveredIndex = i;
            }
            // Draw non-hovered cards first, then the raised hovered card on top.
            // Both are drawn at native scale; no zoom is applied.
            for (int pass = 0; pass < 2; ++pass)
            {
                for (int i = 0; i < mBenchCount; ++i)
                {
                    if ((pass == 0 && i == aHoveredIndex) || (pass == 1 && i != aHoveredIndex))
                        continue;
                    SeedPacket& aPacket = mBenchPackets[i];
                    if (aPacket.mPacketType == SeedType::SEED_NONE || !aPacket.BeginDraw(g))
                        continue;
                    aPacket.Draw(g); // Hover movement is animated in UpdateBenchPackets, never scaled.
                    aPacket.EndDraw(g);
                }
            }

            // The existing downward-facing arrow asset belongs ABOVE a Bench
            // packet. The old particle below the Bench was clipped by the screen.
            if (IMAGE_BOARD_ARROW != nullptr &&
                (mRollTutorialStep == ROLL_TUT_PLANT_SUNFLOWER ||
                 mRollTutorialStep == ROLL_TUT_PLANT_PEASHOOTER ||
                 mRollTutorialStep == ROLL_TUT_PLANT_MERGED) &&
                mCursorObject->mCursorType == CursorType::CURSOR_TYPE_NORMAL)
            {
                SeedType aRequired = mRollTutorialStep == ROLL_TUT_PLANT_PEASHOOTER ?
                    SeedType::SEED_PEASHOOTER : SeedType::SEED_SUNFLOWER;
                for (int i = 0; i < mBenchCount; ++i)
                {
                    const SeedPacket& aPacket = mBenchPackets[i];
                    if (aPacket.mPacketType != aRequired ||
                        (mRollTutorialStep == ROLL_TUT_PLANT_MERGED && aPacket.mStarLevel != 2))
                        continue;
                    const int aBob = (mMainCounter / 10) % 3;
                    g->DrawImage(IMAGE_BOARD_ARROW,
                        aPacket.mX + (aPacket.mWidth - IMAGE_BOARD_ARROW->mWidth) / 2,
                        aPacket.mY - IMAGE_BOARD_ARROW->mHeight - 5 - aBob);
                    break;
                }
            }
        }

        // The five-second countdown remains in game logic; only its text was removed.

		if (mAdvice->mMessageStyle == MessageStyle::MESSAGE_STYLE_SLOT_MACHINE)
		{
			mAdvice->Draw(g);
		}
	}

	if (mShowShovel || (IsRollSeedBank() && mGardenGloveEnabled))
	{
		DrawShovel(g);
	}
	if (!StageHasFog())
	{
		DrawTopRightUI(g);
	}
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN || mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM)
	{
		DrawZenButtons(g);
	}
}

void Board::DrawUICoinBank(Graphics* g)
{
	mCoinBankX = 57;
	mCoinBankY = BOARD_HEIGHT - Sexy::IMAGE_COINBANK->GetHeight() - 1;
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN || mApp->mCrazyDaveState != CrazyDaveState::CRAZY_DAVE_OFF)
	{
		mCoinBankX = BOARD_WIDTH - 350 - mX;
	}

	if (mApp->mGameScene != GameScenes::SCENE_PLAYING && mApp->mCrazyDaveState == CrazyDaveState::CRAZY_DAVE_OFF)
		return;

	if (mCoinBankFadeCount <= 0)
		return;

	g->SetColorizeImages(true);
	int anAlpha = ClampInt(255 * mCoinBankFadeCount / 15, 0, 255);
	g->SetColor(Color(255, 255, 255, anAlpha));
	g->DrawImage(Sexy::IMAGE_COINBANK, mCoinBankX, mCoinBankY);

	g->SetColor(Color(180, 255, 90, anAlpha));
	g->SetFont(Sexy::FONT_CONTINUUMBOLD14);
	SexyString aCoinLabel = LawnApp::GetMoneyStringDollars(mApp->mPlayerInfo->GetMoneyDollars());
	g->DrawString(aCoinLabel, mCoinBankX + 116 - Sexy::FONT_CONTINUUMBOLD14->StringWidth(aCoinLabel), mCoinBankY + 24);
	g->SetColorizeImages(false);
}

void Board::ClearFogAroundPlant(Plant* thePlant, int theSize)
{
	//int aFogFadeOutSpeed = mFogBlownCountDown >= 2000 ? 40 : mFogBlownCountDown > 0 ? 2 : 6;
	int aFogFadeOutSpeed = 6;
	if (mFogBlownCountDown > 0 && mFogBlownCountDown < 2000)
	{
		aFogFadeOutSpeed = 2;
	}
	else if (mFogBlownCountDown > 0)
	{
		aFogFadeOutSpeed = 40;
	}

	int aLeft = LeftFogColumn();
	int aFogOffsetX = (mFogOffset + 50) / 100;
	int aStartX = thePlant->mPlantCol - theSize - aFogOffsetX;
	int aEndX = thePlant->mPlantCol + theSize - aFogOffsetX;
	aStartX = max(aStartX, aLeft);
	aEndX = min(aEndX, MAX_GRID_SIZE_X - 1);

	int aStartY = thePlant->mRow - theSize;
	int aEndY = thePlant->mRow + theSize;
	aStartY = max(aStartY, 0);
	aEndY = min(aEndY, MAX_GRID_SIZE_Y);

	for (int x = aStartX; x <= aEndX; x++)
	{
		for (int y = aStartY; y <= aEndY; y++)
		{
			int aDistX = abs(x + aFogOffsetX - thePlant->mPlantCol);
			int aDistY = abs(y - thePlant->mRow);
			if (theSize == 4)
			{
				if (aDistX > 3 || aDistY > 2)
				{
					continue;
				}
				if (aDistX + aDistY == 5)
				{
					continue;
				}
			}
			else if (aDistX + aDistY > theSize)
			{
				continue;
			}

			mGridCelFog[x][y] = max(mGridCelFog[x][y] - aFogFadeOutSpeed, 0);
		}
	}
}

void Board::UpdateFog()
{
	if (!StageHasFog())
		return;

	//int aFogFadeInSpeed = mFogBlownCountDown >= 2000 ? 20 : mFogBlownCountDown > 0 ? 1 : 3;
	int aFogFadeInSpeed = 3;
	if (mFogBlownCountDown > 0 && mFogBlownCountDown < 2000)
	{
		aFogFadeInSpeed = 1;
	}
	else if (mFogBlownCountDown > 0)
	{
		aFogFadeInSpeed = 20;
	}

	int aLeft = LeftFogColumn();
	for (int x = aLeft; x < MAX_GRID_SIZE_X; x++)
	{
		for (int y = 0; y < MAX_GRID_SIZE_Y + 1; y++)
		{
			int aFogMax = x == aLeft ? 200 : 255;
			mGridCelFog[x][y] = min(mGridCelFog[x][y] + aFogFadeInSpeed, aFogMax);
		}
	}

	Plant* aPlant = nullptr;
	while (IteratePlants(aPlant))
	{
		if (aPlant->NotOnGround())
			continue;

		if (aPlant->mSeedType == SeedType::SEED_PLANTERN)
		{
			ClearFogAroundPlant(aPlant, 4);
		}
		else if (aPlant->mSeedType == SeedType::SEED_TORCHWOOD)
		{
			ClearFogAroundPlant(aPlant, 1);
		}
	}
}

void Board::DrawFog(Graphics* g)
{
	Image* aImageFog = mApp->Is3dAccel() ? Sexy::IMAGE_FOG : Sexy::IMAGE_FOG_SOFTWARE;
	for (int x = 0; x < MAX_GRID_SIZE_X; x++)
	{
		for (int y = 0; y < MAX_GRID_SIZE_Y + 1; y++)
		{
			int aFadeAmount = mGridCelFog[x][y];
			if (aFadeAmount == 0)
				continue;

			int aCelLook = mGridCelLook[x][y % MAX_GRID_SIZE_Y];
			int aCelCol = aCelLook % 8;
			int aPosXOffset = 80;
			float aPosX = x * aPosXOffset + mFogOffset - 15 + BOARD_ADDITIONAL_WIDTH;
			int aPosYOffset = 85;
			float aPosY = y * aPosYOffset + 20 + BOARD_OFFSET_Y;
			float aTime = mMainCounter * PI * 2;
			float aPhaseX = 6 * PI * x / MAX_GRID_SIZE_X;
			float aPhaseY = 6 * PI * y / (MAX_GRID_SIZE_Y + 1);
			float aMotion = 13 + 4 * sin(aTime / 900 + aPhaseY) + 8 * sin(aTime / 500 + aPhaseX);

			int aColorVariant = 255 - aCelLook * 1.5 - aMotion * 1.5;
			int aLightnessVariant = 255 - aCelLook - aMotion;
			if (!mApp->Is3dAccel())
			{
				aPosX += 10;
				aPosY += 3;
				aCelCol = aCelLook % Sexy::IMAGE_FOG_SOFTWARE->mNumCols;
				aColorVariant = 255;
				aLightnessVariant = 255;
			}

			g->SetColorizeImages(true);
			g->SetColor(Color(aColorVariant, aColorVariant, aLightnessVariant, aFadeAmount));
			g->DrawImageCel(aImageFog, aPosX, aPosY, aCelCol, 0);

			if (x == MAX_GRID_SIZE_X - 1)
			{
				for (int i = 1; i <= MAX_GRID_SIZE_X - LeftFogColumn() + 1; i++)
				{
					if (mApp->Is3dAccel())
					{
						aCelLook += i;
						aCelCol = aCelLook % 8;
						aPhaseX = 6 * PI * (x + i) / MAX_GRID_SIZE_X;
						aPhaseY = 6 * PI * (y + i) / (MAX_GRID_SIZE_Y + 1);
						aMotion = 13 + 4 * sin(aTime / 900 + aPhaseY) + 8 * sin(aTime / 500 + aPhaseX);
						aColorVariant = 255 - aCelLook * 1.5 - aMotion * 1.5;
						aLightnessVariant = 255 - aCelLook - aMotion;
						g->SetColor(Color(aColorVariant, aColorVariant, aLightnessVariant, aFadeAmount));
					}
					g->DrawImageCel(aImageFog, aPosX + aPosXOffset * i, aPosY, aCelCol, 0);
					if (y == 0 && SHOULD_FOG_COVER_FULLY)
					{
						for (int j = 1; j <= 2; j++)
							g->DrawImageCel(aImageFog, aPosX + aPosXOffset * i, aPosY + aPosYOffset * -j, aCelCol, 0);
					}
				}
			}
			if (y == 0 && SHOULD_FOG_COVER_FULLY)
			{
				for (int i = 1; i <= 2; i++)
					g->DrawImageCel(aImageFog, aPosX, aPosY + aPosYOffset * -i, aCelCol, 0);
			}
			g->SetColorizeImages(false);
		}
	}
}

bool Board::IsScaryPotterDaveTalking()
{
	return mApp->IsScaryPotterLevel() && mNextSurvivalStageCounter > 0 && mApp->mCrazyDaveState != CrazyDaveState::CRAZY_DAVE_OFF;
}

void Board::DrawUITop(Graphics* g)
{
	if (StageHasFog())
	{
		DrawTopRightUI(g);
	}

	mMenuButton->Draw(g);


	if (HAS_FAST_FORWARD_BUTTON)
		mFastButton->Draw(g);

	if (mTimeStopCounter > 0)
	{
		g->SetColor(Color(200, 200, 200, 210));
		g->FillRect(0, 0, BOARD_WIDTH, BOARD_HEIGHT);
	}

	if (mApp->mGameScene == GameScenes::SCENE_PLAYING || mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM)
	{
		DrawProgressMeter(g);
		DrawLevel(g);
	}

    // Draw Hologram through the game's native SeedPacket renderer. Its own
    // packet state supplies the normal icon, cost font and recharge darkening.
    if (mHologramEnabled && mApp->mGameScene != GameScenes::SCENE_ZOMBIES_WON)
    {
        mHologramPacket.mX = BOARD_WIDTH - 260;
        mHologramPacket.mY = BOARD_HEIGHT - 135;
        mHologramPacket.mPacketType = mHologramLastPlant;
        mHologramPacket.mImitaterType = SeedType::SEED_NONE;
        if (mHologramPacket.BeginDraw(g))
        {
            mHologramPacket.Draw(g);
            mHologramPacket.EndDraw(g);
        }
    }

	DrawAugmentSelection(g);
	if (mStoreButton && mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND)
	{
		mStoreButton->Draw(g);
	}

	if ((mApp->mGameMode == GameMode::GAMEMODE_UPSELL || mApp->mGameMode == GameMode::GAMEMODE_INTRO) && mCutScene->mUpsellHideBoard)
	{
		g->SetColor(Color(0, 0, 0));
		g->FillRect(0, 0, BOARD_WIDTH, BOARD_HEIGHT);
	}

	if (StageHasFog())
	{
		DrawTopRightUI(g);
	}

	mMenuButton->Draw(g);
	if (HAS_FAST_FORWARD_BUTTON)
		mFastButton->Draw(g);

	if (mTimeStopCounter > 0)
	{
		g->SetColor(Color(200, 200, 200, 210));
		g->FillRect(0, 0, BOARD_WIDTH, BOARD_HEIGHT);
	}

	if (mApp->mGameScene == GameScenes::SCENE_PLAYING || mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM)
	{
		DrawProgressMeter(g);
		DrawLevel(g);
	}
	if (mStoreButton && mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND)
	{
		mStoreButton->Draw(g);
	}

	if (mApp->mGameMode == GameMode::GAMEMODE_UPSELL)
	{
		mCutScene->DrawUpsell(g);
	}
	if (mApp->mGameMode == GameMode::GAMEMODE_INTRO)
	{
		mCutScene->DrawIntro(g);
	}

	if (mApp->mGameMode != GameMode::GAMEMODE_UPSELL && (mApp->mGameScene == GameScenes::SCENE_LEVEL_INTRO ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN ||
		mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM ||
		IsScaryPotterDaveTalking()))
	{
		Graphics aScreenSpace(*g);
		aScreenSpace.mTransX -= mX;
		aScreenSpace.mTransY -= mY;
		mApp->DrawCrazyDave(&aScreenSpace);
	}

	if (mAdvice->mMessageStyle != MessageStyle::MESSAGE_STYLE_SLOT_MACHINE)
	{
		mAdvice->Draw(g);
	}

	if (mTimeStopCounter == 0 && mCursorObject->BeginDraw(g))
	{
		mCursorObject->Draw(g);
		mCursorObject->EndDraw(g);
	}

	mToolTip->Draw(g);
	DrawDebugText(g);
	DrawDebugObjectRects(g);
    DrawActiveAugment(g); // Augment gallery overlays all other gameplay UI.
    DrawSeedDraft(g); // This modal must appear above the gallery, toolbar and cursor.

}

void Board::Draw(Graphics* g)
{
	if (mApp->GetDialog(Dialogs::DIALOG_STORE) || mApp->GetDialog(Dialogs::DIALOG_ALMANAC))
		return;

	g->SetLinearBlend(true);

	if (mDrawCount && mCutScene->mPreloaded)
	{
		int aTickCount = GetTickCount();
		int aIntervalDraws = mDrawCount - mIntervalDrawCountStart;
		int aInterval = aTickCount - mIntervalDrawTime;
		if (aInterval > 10000)
		{
			float aIntervalFPS = (aIntervalDraws * 1000 + 500) / aInterval;
			if (mMinFPS > aIntervalFPS)
			{
				mMinFPS = aIntervalFPS;
			}
			mIntervalDrawCountStart = mDrawCount;
			mIntervalDrawTime = aTickCount;
		}
	}
	else
	{
		ResetFPSStats();
	}

	mDrawCount++;
	DrawGameObjects(g);
}

void Board::SetMustacheMode(bool theEnableMustache)
{
	mApp->PlayFoley(FoleyType::FOLEY_POLEVAULT);
	mMustacheMode = theEnableMustache;
	mApp->mMustacheMode = theEnableMustache;

	if (mMustacheMode)
	{
		mApp->GetAchievement(ACHIEVEMENT_MUSTACHE_MODE);
	}

	Zombie* aZombie = nullptr;
	while (IterateZombies(aZombie))
	{
		aZombie->EnableMustache(theEnableMustache);
	}
}

void Board::SetFutureMode(bool theEnableFuture)
{
	mApp->PlaySample(Sexy::SOUND_BOING);
	mFutureMode = theEnableFuture;
	mApp->mFutureMode = theEnableFuture;

	Zombie* aZombie = nullptr;
	while (IterateZombies(aZombie))
	{
		aZombie->EnableFuture(theEnableFuture);
	}
}

void Board::SetPinataMode(bool theEnablePinata)
{
	mApp->PlayFoley(FoleyType::FOLEY_JUICY);
	mPinataMode = theEnablePinata;
	mApp->mPinataMode = theEnablePinata;
}

void Board::SetDanceMode(bool theEnableDance)
{
	mApp->PlayFoley(FoleyType::FOLEY_DANCER);
	mDanceMode = theEnableDance;
	mApp->mDanceMode = theEnableDance;

	Zombie* aZombie = nullptr;
	while (IterateZombies(aZombie))
	{
		if (!aZombie->mDead)
		{
			aZombie->EnableDance(theEnableDance);
		}
	}
}

void Board::SetSuperMowerMode(bool theEnableSuperMower)
{
	mApp->PlayFoley(FoleyType::FOLEY_ZAMBONI);
	mSuperMowerMode = theEnableSuperMower;
	mApp->mSuperMowerMode = theEnableSuperMower;

	LawnMower* aLawnMower = nullptr;
	while (IterateLawnMowers(aLawnMower))
	{
		aLawnMower->EnableSuperMower(theEnableSuperMower);
	}
}

void Board::SetDaisyMode(bool theEnableDaisy)
{
	mApp->PlaySample(SOUND_LOADINGBAR_FLOWER);
	mDaisyMode = theEnableDaisy;
	mApp->mDaisyMode = theEnableDaisy;
}

void Board::SetSukhbirMode(bool theEnableSukhbir)
{
	mApp->PlaySample(Sexy::SOUND_SUKHBIR);
	mSukhbirMode = theEnableSukhbir;
	mApp->mSukhbirMode = theEnableSukhbir;
}

void Board::DoTypingCheck(KeyCode theKey)
{
	if (mApp->mKonamiCheck->Check(theKey))
	{
		mApp->PlayFoley(FoleyType::FOLEY_DROP);
		return;
	}
	if (mApp->mMustacheCheck->Check(theKey) || mApp->mMoustacheCheck->Check(theKey))
	{
		SetMustacheMode(!mMustacheMode);
		return;
	}
	if (mApp->mSuperMowerCheck->Check(theKey) || mApp->mSuperMowerCheck2->Check(theKey))
	{
		SetSuperMowerMode(!mSuperMowerMode);
		return;
	}
	if (mApp->mFutureCheck->Check(theKey))
	{
		SetFutureMode(!mFutureMode);
		return;
	}
	if (mApp->mPinataCheck->Check(theKey))
	{
		if (mApp->CanDoPinataMode())
		{
			SetPinataMode(!mPinataMode);
			return;
		}
		else
		{
			if (mApp->mGameScene == GameScenes::SCENE_PLAYING)
			{
				DisplayAdvice(_S("[CANT_USE_CODE]"), MessageStyle::MESSAGE_STYLE_BIG_MIDDLE_FAST, AdviceType::ADVICE_NONE);
			}
			mApp->PlaySample(Sexy::SOUND_BUZZER);
			return;
		}
	}
	if (mApp->mDanceCheck->Check(theKey))
	{
		if (mApp->CanDoDanceMode())
		{
			SetDanceMode(!mDanceMode);
			return;
		}
		else
		{
			if (mApp->mGameScene == GameScenes::SCENE_PLAYING)
			{
				DisplayAdvice(_S("[CANT_USE_CODE]"), MessageStyle::MESSAGE_STYLE_BIG_MIDDLE_FAST, AdviceType::ADVICE_NONE);
			}
			mApp->PlaySample(Sexy::SOUND_BUZZER);
			return;
		}
	}
	if (mApp->mDaisyCheck->Check(theKey))
	{
		if (mApp->CanDoDaisyMode())
		{
			SetDaisyMode(!mDaisyMode);
			return;
		}
		else
		{
			if (mApp->mGameScene == GameScenes::SCENE_PLAYING)
			{
				DisplayAdvice(_S("[CANT_USE_CODE]"), MessageStyle::MESSAGE_STYLE_BIG_MIDDLE_FAST, AdviceType::ADVICE_NONE);
			}
			mApp->PlaySample(Sexy::SOUND_BUZZER);
			return;
		}
	}
	if (mApp->mSukhbirCheck->Check(theKey))
	{
		SetSukhbirMode(!mSukhbirMode);
		return;
	}
}

void Board::KeyDown(KeyCode theKey)
{
	// During the Roll Shop walkthrough only the explicitly permitted mouse action for the
	// current step is accepted. Block keyboard shortcuts and cheats as well as mouse input.
	if (IsRollTutorialActive())
	{
		return;
	}

	// Do not feed keys to the Monika typing buffer outside an active debug session.
	// Still allow developer testing while the Augment chooser blocks other input.
	if (mApp->mDeveloperDebugSession && mApp->mTodCheatKeys &&
        mApp->mDebugKeysEnabled && mApp->mMonikaCheck->Check(theKey))
	{
		if (mApp->mGameScene == GameScenes::SCENE_PLAYING && !mLevelComplete && mBoardFadeOutCounter < 0)
		{
			if (mAugmentChoosing)
			{
				mAugmentChoosing = false;
				mAugmentHidden = false;
				TutorialArrowRemove();
				ClearAugmentPlants();
				mApp->mIsFastMode = mAugmentRestoreFastMode;
			}
			mApp->mBoardResult = BoardResult::BOARDRESULT_WON;
			mLevelAwardSpawned = true;
			FadeOutLevel();
			mBoardFadeOutCounter = 1;
			mScoreNextMowerCounter = 0;
		}
		return;
	}

	if (mAugmentChoosing)
		return;

    if (mAugmentGalleryOpen || gAugmentGalleryProgressMs > 0)
        return; // Only the Augment button toggles the gallery; block game hotkeys while visible.

	DoTypingCheck(theKey);

	if (mApp->mGameScene == GameScenes::SCENE_LEVEL_INTRO && 
		mApp->mGameMode != GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN && 
		mApp->mGameMode != GameMode::GAMEMODE_TREE_OF_WISDOM)
	{
		mCutScene->KeyDown(theKey);
	}
	else if (theKey == KeyCode::KEYCODE_RETURN || theKey == KeyCode::KEYCODE_SPACE)
	{
		if (IsScaryPotterDaveTalking() && mApp->mCrazyDaveMessageIndex != -1)
		{
			mChallenge->AdvanceCrazyDaveDialog();
		}
		else if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN || mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM)
		{
			mApp->mZenGarden->AdvanceCrazyDaveDialog();
		}
		else if (theKey == KeyCode::KEYCODE_SPACE && mApp->CanPauseNow())
		{
			mApp->PlaySample(Sexy::SOUND_PAUSE);
			mApp->DoPauseDialog();
		}
	}
	else if (theKey == KeyCode::KEYCODE_ESCAPE)
	{
		if (mCursorObject->mCursorType != CursorType::CURSOR_TYPE_NORMAL && mCursorObject->mCursorType != CursorType::CURSOR_TYPE_HAMMER)
		{
			RefreshSeedPacketFromCursor();
		}
		else if (CanInteractWithBoardButtons() && mApp->mGameScene != GameScenes::SCENE_ZOMBIES_WON)
		{
			mApp->SetCursor(CURSOR_POINTER);
			mApp->DoNewOptions(false);
		}
	}
}

static void TodCrash()
{
	TOD_ASSERT(false, "Crash%s", "!!!!");
}

void Board::KeyChar(SexyChar theChar)
{
	if (IsRollTutorialActive())
	{
		mApp->PlaySample(Sexy::SOUND_BUZZER);
		return;
	}
	if (mAugmentChoosing)
		return;

	bool canUseKeybinds = mApp->mBankKeybinds && (!mPaused || mApp->mGameScene == GameScenes::SCENE_PLAYING || mApp->mCrazyDaveReanimID != ReanimationID::REANIMATIONID_NULL);
	if (isdigit(theChar) && canUseKeybinds && mSeedBank->mY >= 0)
	{
		for (int i = 0; i < mSeedBank->mNumPackets; i++)
		{
			int aSeedIndex = i;
			if (theChar == '0' + aSeedIndex && mSeedBank->mNumPackets > aSeedIndex)
			{
				if (mApp->mZeroNineBankFormat)
				{
					if (aSeedIndex == 0)
						aSeedIndex = 9;
					else
						aSeedIndex--;
				}
				SeedPacket* aPacket = &mSeedBank->mSeedPackets[aSeedIndex];
				if (aPacket->mPacketType == SeedType::SEED_NONE)	
					break;

				if (mCursorObject->mSeedBankIndex == aSeedIndex)
				{
					RefreshSeedPacketFromCursor();
					mApp->PlayFoley(FoleyType::FOLEY_DROP);
				}
				else
				{
					if (mCursorObject->mCursorType != CursorType::CURSOR_TYPE_PLANT_FROM_BANK || mCursorObject->mSeedBankIndex != aSeedIndex)
					{
						if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_BANK)
							RefreshSeedPacketFromCursor();
						else
							ClearCursor();
					}
					aPacket->MouseDown(0, 0, 0);
				}
				break;
			}
		}
	}
	else if (tolower(theChar) == _S('s') && canUseKeybinds && mShowShovel)
	{
		if (mCursorObject->mCursorType != CursorType::CURSOR_TYPE_SHOVEL)
		{
			if (mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_BANK)
				RefreshSeedPacketFromCursor();
			PickUpTool(GameObjectType::OBJECT_TYPE_SHOVEL);
		}
		else
		{
			ClearCursor();
			mApp->PlayFoley(FoleyType::FOLEY_DROP);
		}
	}
	else if (tolower(theChar) == _S('d') && IsRollSeedBank())
	{
		if (TryRollSeedBank())
			return;
	}

	if (!mApp->mDebugKeysEnabled)
		return;

	TodTraceAndLog("Board cheat key '%c'", theChar);

	if (theChar == _S('e'))
	{
		for (int i = 0; i < NUM_ACHIEVEMENTS; i++)
			mApp->GetAchievement((AchievementType)i);
	}

	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
	{
		if (theChar == _S('m'))
		{
			if (!mApp->mZenGarden->IsZenGardenFull(true))
			{
				PottedPlant aPottedPlant;
				aPottedPlant.InitializePottedPlant(SeedType::SEED_MARIGOLD);
				aPottedPlant.mDrawVariation = (DrawVariation)RandRangeInt((int)DrawVariation::VARIATION_MARIGOLD_WHITE, (int)DrawVariation::VARIATION_MARIGOLD_LIGHT_GREEN);
				mApp->mZenGarden->AddPottedPlant(&aPottedPlant);
			}
			return;
		}
		
		if (theChar == _S('+'))
		{
			if (!mApp->mZenGarden->IsZenGardenFull(true))
			{
				PottedPlant aPottedPlant;
				aPottedPlant.InitializePottedPlant(mApp->mZenGarden->PickRandomSeedType());
				mApp->mZenGarden->AddPottedPlant(&aPottedPlant);
			}
			return;
		}
		
		if (theChar == _S('a'))
		{
			if (!mApp->mZenGarden->IsZenGardenFull(true))
			{
				PottedPlant aPottedPlant;
				aPottedPlant.InitializePottedPlant(mApp->mZenGarden->PickRandomSeedType());
				aPottedPlant.mPlantAge = PottedPlantAge::PLANTAGE_FULL;
				mApp->mZenGarden->AddPottedPlant(&aPottedPlant);
			}
			return;
		}
		
		if (theChar == _S('f'))
		{
			Plant* aPlant = nullptr;
			while (IteratePlants(aPlant))
			{
				if (GetZenToolAt(aPlant->mPlantCol, aPlant->mRow) == nullptr && aPlant->mPottedPlantIndex >= 0)
				{
					PottedPlant* aPottedPlant = mApp->mZenGarden->PottedPlantFromIndex(aPlant->mPottedPlantIndex);
					PottedPlantNeed aNeed = mApp->mZenGarden->GetPlantsNeed(aPottedPlant);
					if (aNeed == PottedPlantNeed::PLANTNEED_WATER)
					{
						aPlant->mHighlighted = true;
						mApp->mZenGarden->MouseDownWithFeedingTool(aPlant->mX, aPlant->mY, CursorType::CURSOR_TYPE_WATERING_CAN);
						return;
					}
					else if (aNeed == PottedPlantNeed::PLANTNEED_FERTILIZER)
					{
						aPlant->mHighlighted = true;
						if (mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_FERTILIZER] <= PURCHASE_COUNT_OFFSET)
						{
							mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_FERTILIZER] = PURCHASE_COUNT_OFFSET + 1;
						}
						mApp->mZenGarden->MouseDownWithFeedingTool(aPlant->mX, aPlant->mY, CursorType::CURSOR_TYPE_FERTILIZER);
						return;
					}
					else if (aNeed == PottedPlantNeed::PLANTNEED_BUGSPRAY)
					{
						aPlant->mHighlighted = true;
						if (mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_BUG_SPRAY] <= PURCHASE_COUNT_OFFSET)
						{
							mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_BUG_SPRAY] = PURCHASE_COUNT_OFFSET + 1;
						}
						mApp->mZenGarden->MouseDownWithFeedingTool(aPlant->mX, aPlant->mY, CursorType::CURSOR_TYPE_BUG_SPRAY);
						return;
					}
					else if (aNeed == PottedPlantNeed::PLANTNEED_PHONOGRAPH)
					{
						aPlant->mHighlighted = true;
						mApp->mZenGarden->MouseDownWithFeedingTool(aPlant->mX, aPlant->mY, CursorType::CURSOR_TYPE_PHONOGRAPH);
						return;
					}
				}
			}
			return;
		}

		if (theChar == _S('r'))
		{
			Plant* aPlant = nullptr;
			while (IteratePlants(aPlant))
			{
				if (aPlant->mPottedPlantIndex >= 0)
				{
					TOD_ASSERT(aPlant->mPottedPlantIndex < mApp->mPlayerInfo->mNumPottedPlants);
					PottedPlant* aPottedPlant = &mApp->mPlayerInfo->mPottedPlant[aPlant->mPottedPlantIndex];
					mApp->mZenGarden->ResetPlantTimers(aPottedPlant);
				}
			}
			return;
		}

		if (theChar == _S('s'))
		{
			if (mApp->mZenGarden->IsStinkySleeping())
			{
				mApp->mZenGarden->WakeStinky();
			}
			else
			{
				mApp->mZenGarden->ResetStinkyTimers();
			}
			return;
		}

		if (theChar == _S('c'))
		{
			if (mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_CHOCOLATE] < PURCHASE_COUNT_OFFSET)
			{
				mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_CHOCOLATE] = PURCHASE_COUNT_OFFSET + 1;
			}
			else
			{
				mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_CHOCOLATE]++;
			}
			return;
		}

		if (theChar == _S(']'))
		{
			PottedPlant* aPottedPlant = mApp->mZenGarden->GetPottedPlantInWheelbarrow();
			if (aPottedPlant)
			{
				aPottedPlant->mSeedType = (SeedType)((int)aPottedPlant->mSeedType + 1);
				if (aPottedPlant->mSeedType == SeedType::SEED_GATLINGPEA)
				{
					aPottedPlant->mSeedType = SeedType::SEED_PEASHOOTER;
				}
				if (aPottedPlant->mSeedType == SeedType::SEED_FLOWERPOT)
				{
					aPottedPlant->mSeedType = SeedType::SEED_KERNELPULT;
				}
			}
			return;
		}
	}

	if (mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM)
	{
		if (theChar == _S('f'))
		{
			if (mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_TREE_FOOD] <= PURCHASE_COUNT_OFFSET)
			{
				mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_TREE_FOOD] = PURCHASE_COUNT_OFFSET + 1;
			}
			mChallenge->TreeOfWisdomFertilize();
		}
		else if (theChar == _S('g'))
		{
			mChallenge->TreeOfWisdomGrow();
		}
		else if (theChar == _S('b'))
		{
			mChallenge->mChallengeStateCounter = 1;
		}
		else if (theChar == _S('0'))
		{
			mApp->mPlayerInfo->mChallengeRecords[mApp->GetCurrentChallengeIndex()] = 0;
			mChallenge->TreeOfWisdomGrow();
		}
		else if (theChar == _S('1'))
		{
			mApp->mPlayerInfo->mChallengeRecords[mApp->GetCurrentChallengeIndex()] = 9;
			mChallenge->TreeOfWisdomGrow();
		}
		else if (theChar == _S('2'))
		{
			mApp->mPlayerInfo->mChallengeRecords[mApp->GetCurrentChallengeIndex()] = 19;
			mChallenge->TreeOfWisdomGrow();
		}
		else if (theChar == _S('3'))
		{
			mApp->mPlayerInfo->mChallengeRecords[mApp->GetCurrentChallengeIndex()] = 29;
			mChallenge->TreeOfWisdomGrow();
		}
		else if (theChar == _S('4'))
		{
			mApp->mPlayerInfo->mChallengeRecords[mApp->GetCurrentChallengeIndex()] = 39;
			mChallenge->TreeOfWisdomGrow();
		}
		else if (theChar == _S('5'))
		{
			mApp->mPlayerInfo->mChallengeRecords[mApp->GetCurrentChallengeIndex()] = 49;
			mChallenge->TreeOfWisdomGrow();
		}
		else if (theChar == _S('6'))
		{
			mApp->mPlayerInfo->mChallengeRecords[mApp->GetCurrentChallengeIndex()] = 98;
			mChallenge->TreeOfWisdomGrow();
		}
		else if (theChar == _S('7'))
		{
			mApp->mPlayerInfo->mChallengeRecords[mApp->GetCurrentChallengeIndex()] = 498;
			mChallenge->TreeOfWisdomGrow();
		}
		else if (theChar == _S('8'))
		{
			mApp->mPlayerInfo->mChallengeRecords[mApp->GetCurrentChallengeIndex()] = 998;
			mChallenge->TreeOfWisdomGrow();
		}

		return;
	}

	if (theChar == _S('<'))
	{
		mApp->DoNewOptions(false);
	}
	else if (theChar == _S('l'))
	{
		mApp->DoCheatDialog();
	}
	else if (theChar == _S('#'))
	{
		if (mApp->IsSurvivalMode())
		{
			if (mApp->mGameScene == GameScenes::SCENE_LEVEL_INTRO)
			{
				return;
			}

			mCurrentWave = mNumWaves;
			mChallenge->mSurvivalStage += 5;
			RemoveAllZombies();
			FadeOutLevel();
		}
	}
	else if (theChar == _S('!'))
	{
		mApp->mBoardResult = BoardResult::BOARDRESULT_CHEAT;
		if (IsLastStandStageWithRepick())
		{
			if (mNextSurvivalStageCounter == 0)
			{
				mCurrentWave = mNumWaves;
				RemoveAllZombies();
				FadeOutLevel();
			}
		}
		else if ((mApp->IsScaryPotterLevel() && !IsFinalScaryPotterStage()) || mApp->IsEndlessIZombie(mApp->mGameMode))
		{
			if (mNextSurvivalStageCounter == 0)
			{
				RemoveAllZombies();
				FadeOutLevel();
			}
		}
		else if (mApp->IsSurvivalMode())
		{
			if (mApp->mGameScene == GameScenes::SCENE_LEVEL_INTRO)
			{
				return;
			}

			mCurrentWave = mNumWaves;
			//if (!IsSurvivalStageWithRepick())
			//{
				RemoveAllZombies();
			//}
			FadeOutLevel();
		}
		else
		{
			RemoveAllZombies();
			FadeOutLevel();
			mBoardFadeOutCounter = 200;
		}
	}
	else if (theChar == _S('+'))
	{
		mApp->mBoardResult = BoardResult::BOARDRESULT_CHEAT;
		if (IsLastStandStageWithRepick())
		{
			if (mNextSurvivalStageCounter == 0)
			{
				mCurrentWave = mNumWaves;
				RemoveAllZombies();
				FadeOutLevel();
			}
		}
		else if ((mApp->IsScaryPotterLevel() && !IsFinalScaryPotterStage()) || mApp->IsEndlessIZombie(mApp->mGameMode))
		{
			if (mNextSurvivalStageCounter == 0)
			{
				RemoveAllZombies();
				FadeOutLevel();
			}
		}
		else if (mApp->IsSurvivalEndless(mApp->mGameMode))
		{
			if (mApp->mGameScene == GameScenes::SCENE_LEVEL_INTRO)
			{
				return;
			}

			mCurrentWave = mNumWaves;
			RemoveAllZombies();
			FadeOutLevel();
		}
		else if (mApp->IsSurvivalMode())
		{
			mChallenge->mSurvivalStage = 5;
			RemoveAllZombies();
			FadeOutLevel();
			mBoardFadeOutCounter = 200;
		}
		else
		{
			RemoveAllZombies();
			FadeOutLevel();
			mBoardFadeOutCounter = 200;
		}
	}
	else if (theChar == _S('8'))
	{
		mApp->mEasyPlantingCheat = !mApp->mEasyPlantingCheat;
	}
	else if (theChar == _S('7'))
	{
		mApp->ToggleSlowMo();
	}
	else if (theChar == _S('6'))
	{
		mApp->ToggleFastMo();
	}
	else if (theChar == _S('z'))
	{
		mDebugTextMode = (DebugTextMode)((int)mDebugTextMode + 1);
		if (mDebugTextMode > DebugTextMode::DEBUG_TEXT_COLLISION)
		{
			mDebugTextMode = DebugTextMode::DEBUG_TEXT_NONE;
		}
	}

	if (mApp->mGameScene != GameScenes::SCENE_PLAYING)
	{
		return;
	}

	Zombie* aBossZombie = GetBossZombie();
	if (aBossZombie && !aBossZombie->IsDeadOrDying())
	{
		if (theChar == _S('b'))
		{
			aBossZombie->mBossBungeeCounter = 0;
			return;
		}
		if (theChar == _S('u'))
		{
			aBossZombie->mSummonCounter = 0;
			return;
		}
		if (theChar == _S('s'))
		{
			aBossZombie->mBossStompCounter = 0;
			return;
		}
		if (theChar == _S('r'))
		{
			aBossZombie->BossRVAttack();
			return;
		}
		if (theChar == _S('h'))
		{
			aBossZombie->mBossHeadCounter = 0;
			return;
		}
		if (theChar == _S('d'))
		{
			aBossZombie->TakeDamage(10000, 0U);
			return;
		}
	}

	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_WAR_AND_PEAS || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_WAR_AND_PEAS_2)
	{
		if (theChar == _S('w'))
		{
			AddZombie(ZombieType::ZOMBIE_WALLNUT_HEAD, Zombie::ZOMBIE_WAVE_DEBUG);
			return;
		}
		if (theChar == _S('t'))
		{
			AddZombie(ZombieType::ZOMBIE_TALLNUT_HEAD, Zombie::ZOMBIE_WAVE_DEBUG);
			return;
		}
		if (theChar == _S('j'))
		{
			AddZombie(ZombieType::ZOMBIE_JALAPENO_HEAD, Zombie::ZOMBIE_WAVE_DEBUG);
			return;
		}
		if (theChar == _S('g'))
		{
			AddZombie(ZombieType::ZOMBIE_GATLING_HEAD, Zombie::ZOMBIE_WAVE_DEBUG);
			return;
		}
		if (theChar == _S('s'))
		{
			AddZombie(ZombieType::ZOMBIE_SQUASH_HEAD, Zombie::ZOMBIE_WAVE_DEBUG);
			return;
		}
	}

	if (theChar == _S('q'))
	{
		if (mApp->IsSurvivalEndless(mApp->mGameMode))
		{
			mApp->mEasyPlantingCheat = true;
			for (int y = 0; y < MAX_GRID_SIZE_X; y++)
			{
				for (int x = 0; x < MAX_GRID_SIZE_Y; x++)
				{
					if (CanPlantAt(x, y, SeedType::SEED_LILYPAD) == PlantingReason::PLANTING_OK)
					{
						AddPlant(x, y, SeedType::SEED_LILYPAD, SeedType::SEED_NONE);
					}
					if (CanPlantAt(x, y, SeedType::SEED_PUMPKINSHELL) == PlantingReason::PLANTING_OK)
					{
						if (x <= 6 || IsPoolSquare(x, y))
						{
							AddPlant(x, y, SeedType::SEED_PUMPKINSHELL, SeedType::SEED_NONE);
						}
					}
					if (CanPlantAt(x, y, SeedType::SEED_GATLINGPEA) == PlantingReason::PLANTING_OK)
					{
						if (x < 5)
						{
							AddPlant(x, y, SeedType::SEED_GATLINGPEA, SeedType::SEED_NONE);
						}
						else if (x == 5)
						{
							AddPlant(x, y, SeedType::SEED_TORCHWOOD, SeedType::SEED_NONE);
						}
						else if (x == 6)
						{
							AddPlant(x, y, SeedType::SEED_SPLITPEA, SeedType::SEED_NONE);
						}
						else if (y == 2 || y == 3)
						{
							AddPlant(x, y, SeedType::SEED_GLOOMSHROOM, SeedType::SEED_NONE);
							if (CanPlantAt(x, y, SeedType::SEED_INSTANT_COFFEE) == PlantingReason::PLANTING_OK)
							{
								AddPlant(x, y, SeedType::SEED_INSTANT_COFFEE, SeedType::SEED_NONE);
							}
						}
					}
				}
			}
		}
		else if (mApp->IsIZombieLevel())
		{
			mApp->mEasyPlantingCheat = true;
			for (int i = 0; i < 5; i++)
			{
				mChallenge->IZombiePlaceZombie(ZombieType::ZOMBIE_FOOTBALL, 6, i);
			}
		}
		else
		{
			mApp->mEasyPlantingCheat = true;
			for (int y = 0; y < MAX_GRID_SIZE_Y; ++y)
			{
				for (int x = 0; x < MAX_GRID_SIZE_X; ++x)
				{
					if (StageHasRoof() && CanPlantAt(x, y, SeedType::SEED_FLOWERPOT) == PlantingReason::PLANTING_OK)
					{
						AddPlant(x, y, SeedType::SEED_FLOWERPOT, SeedType::SEED_NONE);
					}
					if (CanPlantAt(x, y, SeedType::SEED_LILYPAD) == PlantingReason::PLANTING_OK)
					{
						AddPlant(x, y, SeedType::SEED_LILYPAD, SeedType::SEED_NONE);
					}
					if (CanPlantAt(x, y, SeedType::SEED_THREEPEATER) == PlantingReason::PLANTING_OK)
					{
						AddPlant(x, y, SeedType::SEED_THREEPEATER, SeedType::SEED_NONE);
					}
				}
			}

			if (!mChallenge->UpdateZombieSpawning())
			{
				int aWavesRemaining = min(mNumWaves - mCurrentWave, 20);
				while (aWavesRemaining)
				{
					SpawnZombieWave();
					aWavesRemaining--;
				}
			}

			if (mApp->IsScaryPotterLevel())
			{
				GridItem* aGridItem = nullptr;
				while (IterateGridItems(aGridItem))
				{
					if (aGridItem->mGridItemType == GridItemType::GRIDITEM_SCARY_POT)
					{
						mChallenge->ScaryPotterOpenPot(aGridItem);
					}
				}
			}
		}

		return;
	}

	if (theChar == _S('O'))
	{
		mApp->mEasyPlantingCheat = true;
		for (int y = 0; y < MAX_GRID_SIZE_Y; y++)
		{
			for (int x = 0; x < 3; x++)
			{
				if (CanPlantAt(x, y, SeedType::SEED_FLOWERPOT) == PlantingReason::PLANTING_OK)
				{
					AddPlant(x, y, SeedType::SEED_FLOWERPOT, SeedType::SEED_NONE);
				}
			}
		}
		return;
	}

	if (theChar == _S('?') || theChar == _S('/'))
	{
		if (mHugeWaveCountDown > 0)
		{
			mHugeWaveCountDown = 1;
		}
		else
		{
			mZombieCountDown = 6;
		}
		return;
	}

	if (theChar == _S('b'))
	{
		AddZombie(ZombieType::ZOMBIE_BUNGEE, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('o'))
	{
		AddZombie(ZombieType::ZOMBIE_FOOTBALL, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('s'))
	{
		AddZombie(ZombieType::ZOMBIE_DOOR, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('L'))
	{
		AddZombie(ZombieType::ZOMBIE_LADDER, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('y'))
	{
		AddZombie(ZombieType::ZOMBIE_YETI, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('a'))
	{
		AddZombie(ZombieType::ZOMBIE_FLAG, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('w'))
	{
		AddZombie(ZombieType::ZOMBIE_NEWSPAPER, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('F'))
	{
		AddZombie(ZombieType::ZOMBIE_BALLOON, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('n'))
	{
		if (StageHasPool())
		{
			AddZombie(ZombieType::ZOMBIE_SNORKEL, Zombie::ZOMBIE_WAVE_DEBUG);
		}
	}
	if (theChar == _S('c'))
	{
		AddZombie(ZombieType::ZOMBIE_TRAFFIC_CONE, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('m'))
	{
		AddZombie(ZombieType::ZOMBIE_DANCER, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('h'))
	{
		AddZombie(ZombieType::ZOMBIE_PAIL, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	//if (theChar == _S('H')
	//{
	//	AddZombie(ZombieType::ZOMBIE_PAIL, Zombie::ZOMBIE_WAVE_DEBUG);
	//	AddZombie(ZombieType::ZOMBIE_PAIL, Zombie::ZOMBIE_WAVE_DEBUG);
	//	AddZombie(ZombieType::ZOMBIE_PAIL, Zombie::ZOMBIE_WAVE_DEBUG);
	//	AddZombie(ZombieType::ZOMBIE_PAIL, Zombie::ZOMBIE_WAVE_DEBUG);
	//	AddZombie(ZombieType::ZOMBIE_PAIL, Zombie::ZOMBIE_WAVE_DEBUG);
	//	return;
	//}
	if (theChar == _S('D'))
	{
		AddZombie(ZombieType::ZOMBIE_DIGGER, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('p'))
	{
		AddZombie(ZombieType::ZOMBIE_POLEVAULTER, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('P'))
	{
		AddZombie(ZombieType::ZOMBIE_POGO, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('R'))
	{
		if (StageHasPool())
		{
			AddZombie(ZombieType::ZOMBIE_DOLPHIN_RIDER, Zombie::ZOMBIE_WAVE_DEBUG);
		}
		return;
	}
	else if(theChar == _S('j'))
	{
		AddZombie(ZombieType::ZOMBIE_JACK_IN_THE_BOX, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('g'))
	{
		AddZombie(ZombieType::ZOMBIE_GARGANTUAR, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('G'))
	{
		AddZombie(ZombieType::ZOMBIE_REDEYE_GARGANTUAR, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('i'))
	{
		AddZombie(ZombieType::ZOMBIE_ZAMBONI, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('C'))
	{
		AddZombie(ZombieType::ZOMBIE_CATAPULT, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('1'))
	{
		Plant* aPlant = GetTopPlantAt(0, 0, PlantPriority::TOPPLANT_ANY);
		if (aPlant)
		{
			aPlant->Die();
			mChallenge->ZombieAtePlant(nullptr, aPlant);
			return;
		}
	}
	if (theChar == _S('B'))
	{
		mFogBlownCountDown = 2200;
		return;
	}
	if (theChar == _S('t'))
	{
		if (!CanAddBobSled())
		{
			int aRow = Rand(5);
			int aPos = 400 + BOARD_ADDITIONAL_WIDTH;
			if (StageHasPool())
			{
				aRow = Rand(2);
			}
			else if (StageHasRoof())
			{
				aPos = 500 + BOARD_ADDITIONAL_WIDTH;
			}
			mIceTimer[aRow] = 3000;
			mIceMinX[aRow] = aPos;
		}

		AddZombie(ZombieType::ZOMBIE_BOBSLED, Zombie::ZOMBIE_WAVE_DEBUG);
		return;
	}
	if (theChar == _S('r'))
	{
		SpawnZombiesFromGraves();
		return;
	}
	if (theChar == _S('0'))
	{
		AddSunMoney(4);
		mApp->PlaySample(SOUND_BUTTONCLICK);
		return;
	}
	if (theChar == _S('9'))
	{
		AddSunMoney(400);
		mApp->PlaySample(SOUND_BUTTONCLICK);
		return;
	}
	if (theChar == _S('$'))
	{
		mApp->mPlayerInfo->AddCoins(100);
		mApp->PlaySample(SOUND_BUTTONCLICK);
		ShowCoinBank();
		return;
	}
	if (theChar == _S('-'))
	{
		mSunMoney -= 4;
		if (mSunMoney < 0)
		{
			mSunMoney = 0;
		}
		return;
	}
	if (theChar == _S('%'))
	{
		mApp->SwitchScreenMode(mApp->mIsWindowed, !mApp->Is3DAccelerated(), false);
	}
	if (theChar == _S('M'))
	{
		mApp->mMusic->mBurstOverride = 2 - (mApp->mMusic->mBurstOverride != 1);
		return;
	}

	if (theChar == _S('\3') && mApp->mCtrlDown && mApp->mTodCheatKeys)
	{
		TodCrash();

		if (mHugeWaveCountDown > 0)
		{
			mHugeWaveCountDown = 1;
		}
		else
		{
			mZombieCountDown = 6;
		}
	}
}

void Board::AddSunMoney(int theAmount)
{
	mSunMoney += theAmount;
	if (mSunMoney > 999)
	{
		mSunMoney = 999;
	}
}

int Board::CountSunBeingCollected()
{
	int aCount = 0;
	Coin* aCoin = nullptr;
	while (IterateCoins(aCoin))
	{
		if (aCoin->mIsBeingCollected && aCoin->IsSun())
		{
			aCount += aCoin->GetSunValue();
		}
	}
	return aCount;
}

int Board::CountCoinsBeingCollected()
{
	int aCount = 0;
	Coin* aCoin = nullptr;
	while (IterateCoins(aCoin))
	{
		if (aCoin->mIsBeingCollected && aCoin->IsMoney())
		{
			aCount += aCoin->GetCoinValue(aCoin->mType);
		}
	}
	return aCount;
}

bool Board::TakeSunMoney(int theAmount)
{
	if (CanTakeSunMoney(theAmount))
	{
		mSunMoney -= theAmount;
		return true;
	}

	mApp->PlaySample(Sexy::SOUND_BUZZER);
	mOutOfMoneyCounter = 70;
	return false;
}

bool Board::CanTakeSunMoney(int theAmount)
{
	return theAmount <= mSunMoney + CountSunBeingCollected();
}

void Board::ProcessDeleteQueue()
{
	{
		Plant* aPlant = nullptr;
		while (mPlants.IterateNext(aPlant))
		{
			if (aPlant->mDead)
			{
				mPlants.DataArrayFree(aPlant);
			}
		}
	}
	{
		Zombie* aZombie = nullptr;
		while (mZombies.IterateNext(aZombie))
		{
			if (aZombie->mDead)
			{
				mZombies.DataArrayFree(aZombie);
			}
		}
	}
	{
		Projectile* aProjectile = nullptr;
		while (mProjectiles.IterateNext(aProjectile))
		{
			if (aProjectile->mDead)
			{
				mProjectiles.DataArrayFree(aProjectile);
			}
		}
	}
	{
		Coin* aCoin = nullptr;
		while (mCoins.IterateNext(aCoin))
		{
			if (aCoin->mDead)
			{
				mCoins.DataArrayFree(aCoin);
			}
		}
	}
	{
		LawnMower* aLawnMower = nullptr;
		while (mLawnMowers.IterateNext(aLawnMower))
		{
			if (aLawnMower->mDead)
			{
				mLawnMowers.DataArrayFree(aLawnMower);
			}
		}
	}
	{
		GridItem* aGridItem = nullptr;
		while (mGridItems.IterateNext(aGridItem))
		{
			if (aGridItem->mDead)
			{
				mGridItems.DataArrayFree(aGridItem);
			}
		}
	}
}

bool Board::HasConveyorBeltSeedBank()
{
	return
		mApp->IsFinalBossLevel() || 
		mApp->IsMiniBossLevel() || 
		mApp->IsShovelLevel() || 
		mApp->IsWallnutBowlingLevel() ||
		mApp->IsLittleTroubleLevel() || 
		mApp->IsStormyNightLevel() || 
		mApp->IsBungeeBlitzLevel() || 
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_PORTAL_COMBAT || 
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_COLUMN || 
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_INVISIGHOUL;
}

int Board::GetNumSeedsInBank()
{
	if (mApp->IsScaryPotterLevel())
	{
		return 1;
	}
	if (mApp->IsWhackAZombieLevel())
	{
		return 3;
	}
	if (mApp->IsChallengeWithoutSeedBank())
	{
		return 0;
	}
	if (HasConveyorBeltSeedBank())
	{
		return 10;
	}
	if (mApp->IsSlotMachineLevel())
	{
		return 3;
	}
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ICE)
	{
		return 6;
	}
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST)
	{
		return 0;
	}
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZOMBIQUARIUM)
	{
		return 2;
	}
	if (mApp->mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_1 || mApp->mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_2 ||
		mApp->mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_3 || mApp->mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_4)
	{
		return 3;
	}
	if (mApp->mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_5 || mApp->mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_6 ||
		mApp->mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_7)
	{
		return 4;
	}
	if (mApp->mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_8)
	{
		return 6;
	}
	if (mApp->mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_9)
	{
		return 8;
	}
	if (mApp->mGameMode == GameMode::GAMEMODE_PUZZLE_I_ZOMBIE_ENDLESS)
	{
		return 9;
	}

	// Legacy packet-upgrade purchases remain in old saves for ID compatibility, but no longer
	// grant extra Seed Bank slots. Adventure uses the standard six-slot bank.
	int aSeedsAvailable = mApp->GetSeedsAvailable();
	return min(6, aSeedsAvailable);
}

bool Board::StageIsNight()
{
	return 
		mBackground == BackgroundType::BACKGROUND_2_NIGHT || 
		mBackground == BackgroundType::BACKGROUND_4_FOG || 
		mBackground == BackgroundType::BACKGROUND_6_BOSS ||
		mBackground == BackgroundType::BACKGROUND_MUSHROOM_GARDEN || 
		mBackground == BackgroundType::BACKGROUND_ZOMBIQUARIUM;
}

bool Board::StageHasGraveStones()
{
	if (mApp->IsWallnutBowlingLevel() ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_POGO_PARTY ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_PORTAL_COMBAT ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND ||
		mApp->IsIZombieLevel() ||
		mApp->IsScaryPotterLevel())
		return false;

	return mBackground == BackgroundType::BACKGROUND_2_NIGHT;
}

bool Board::StageHasRoof()
{
	return (mBackground == BackgroundType::BACKGROUND_5_ROOF || mBackground == BackgroundType::BACKGROUND_6_BOSS);
}

bool Board::StageHasPool()
{
	return (mBackground == BackgroundType::BACKGROUND_3_POOL || mBackground == BackgroundType::BACKGROUND_4_FOG);
}

bool Board::StageHas6Rows()
{
	return (mBackground == BackgroundType::BACKGROUND_3_POOL || mBackground == BackgroundType::BACKGROUND_4_FOG);
}

bool Board::StageHasBushes() 
{
	return (mBackground == BackgroundType::BACKGROUND_1_DAY || mBackground == BackgroundType::BACKGROUND_2_NIGHT 
		|| mBackground == BackgroundType::BACKGROUND_3_POOL || mBackground == BackgroundType::BACKGROUND_4_FOG);
}

bool Board::StageHasZombieWalkInFromRight()
{
	if (mApp->IsWhackAZombieLevel() ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ICE ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN ||
		mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZOMBIQUARIUM ||
		mApp->IsFinalBossLevel() ||
		mApp->IsIZombieLevel() ||
		mApp->IsSquirrelLevel() ||
		mApp->IsScaryPotterLevel())
		return false;

	return true;
}

bool Board::StageHasFog()
{
	return !mApp->IsStormyNightLevel() && mApp->mGameMode != GameMode::GAMEMODE_CHALLENGE_INVISIGHOUL && mBackground == BackgroundType::BACKGROUND_4_FOG;
}

int Board::LeftFogColumn()
{
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_AIR_RAID)		return 6;
	if (!mApp->IsAdventureMode())										return 5;
	if (mLevel == 31)													return 6;
	if (mLevel >= 32 && mLevel <= 36)									return 5;
	if (mLevel >= 37 && mLevel <= 40)									return 4;
	TOD_ASSERT();
}

int Board::GetSeedPacketPositionX(int theIndex)
{
	if (mApp->IsSlotMachineLevel())			return theIndex * 59 + 247;
	if (HasConveyorBeltSeedBank())			return theIndex * 50 + 91;
	
	if (mSeedBank->mNumPackets <= 7)		return theIndex * 59 + 85;
	else if (mSeedBank->mNumPackets == 8)	return theIndex * 54 + 81;
	else if (mSeedBank->mNumPackets == 9)	return theIndex * 52 + 80;
	else									return theIndex * 51 + 79;
}

int Board::GetSeedBankExtraWidth()
{
	int aNumPackets = mSeedBank->mNumPackets;
	return aNumPackets <= 6 ? 0 : aNumPackets == 7 ? 60 : aNumPackets == 8 ? 76 : aNumPackets == 9 ? 112 : 153;
}

// ---------------------------------------------------------------------------------------------
// Roll Seed Bank (TFT-shop style)
//   Chosen Plants    = Board::mChosenPlants / mNumChosenPlants, the pool picked in the seed chooser.
//   Chosen Seed Bank = Board::mSeedBank, the shop slots (at most ROLL_MAX_SLOTS); each slot is rolled from the
//                      pool and points back at its pool entry through SeedPacket::mChosenPlantIndex.
//   The Reroll button is an extra slot in front of the shop slots and is not counted in the slot limit.
// ---------------------------------------------------------------------------------------------

// The roll shop is used on every ordinary Adventure level, including 1-1.
// The chooser itself is reserved for the Remove Your Seed ban screen from 1-8 onward.
bool Board::IsRollSeedBank()
{
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND ||
		mApp->IsChallengeWithoutSeedBank() || HasConveyorBeltSeedBank() ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ICE ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZOMBIQUARIUM ||
		mApp->IsIZombieLevel() || mApp->IsSlotMachineLevel())
		return false;
	return mApp->IsAdventureMode() || ChooseSeedsOnCurrentLevel();
}

// Tier registry for the shared Roll Shop card pool. Keep this mapping centralized so
// shop odds, packet colors, and per-level copy caps cannot drift apart.
int Board::GetPlantTier(SeedType theSeedType)
{
    switch (theSeedType)
    {
    case SeedType::SEED_PEASHOOTER:
    case SeedType::SEED_SUNFLOWER:
    case SeedType::SEED_POTATOMINE:
    case SeedType::SEED_CHOMPER:
    case SeedType::SEED_SUNSHROOM:
    case SeedType::SEED_SCAREDYSHROOM:
    case SeedType::SEED_TANGLEKELP:
    case SeedType::SEED_TORCHWOOD:
    case SeedType::SEED_SEASHROOM:
    case SeedType::SEED_PLANTERN:
    case SeedType::SEED_PUFFSHROOM:
    case SeedType::SEED_CACTUS:
    case SeedType::SEED_SPLITPEA:
    case SeedType::SEED_MAGNETSHROOM:
    case SeedType::SEED_UMBRELLA:
        return 1;

    case SeedType::SEED_REPEATER:
    case SeedType::SEED_WALLNUT:
    case SeedType::SEED_GRAVEBUSTER:
    case SeedType::SEED_HYPNOSHROOM:
    case SeedType::SEED_LILYPAD:
    case SeedType::SEED_SQUASH:
    case SeedType::SEED_PUMPKINSHELL:
    case SeedType::SEED_CABBAGEPULT:
    case SeedType::SEED_FLOWERPOT:
    case SeedType::SEED_INSTANT_COFFEE:
    case SeedType::SEED_GARLIC:
        return 2;

    case SeedType::SEED_CHERRYBOMB:
    case SeedType::SEED_SNOWPEA:
    case SeedType::SEED_FUMESHROOM:
    case SeedType::SEED_THREEPEATER:
    case SeedType::SEED_JALAPENO:
    case SeedType::SEED_TALLNUT:
    case SeedType::SEED_KERNELPULT:
    case SeedType::SEED_SPIKEWEED:
        return 3;

    case SeedType::SEED_ICESHROOM:
    case SeedType::SEED_BLOVER:
    case SeedType::SEED_STARFRUIT:
        return 4;

    case SeedType::SEED_DOOMSHROOM:
    case SeedType::SEED_MARIGOLD:
    case SeedType::SEED_MELONPULT:
        return 5;
    default:
        return 0;
    }
}

int Board::GetPlantTierPoolSize(SeedType theSeedType)
{
    switch (GetPlantTier(theSeedType))
    {
    case 1: return 30;
    case 2: return 25;
    case 3: return 18;
    case 4: return 10;
    case 5: return 9;
    default: return 0;
    }
}

bool Board::HasAugment(int theAugment) const
{
    return theAugment >= 0 && theAugment < RollAugment::NUM_AUGMENTS &&
        mEnabledAugments[theAugment];
}

void Board::DropRegularSun(int theCount, int theX, int theY)
{
    // All Augment Sun is a collectible Coin object; never credit mSunMoney here.
    // Queue the remainder if other Coins/packets have filled the shared array.
    if (theCount <= 0) return;
    for (int i = 0; i < theCount; ++i)
    {
        if (mCoins.mSize >= mCoins.mMaxSize)
        {
            mPendingAugmentSunDrops += theCount - i;
            return;
        }
        const int x = theX < 0 ? RandRangeInt(130, 730) : theX + RandRangeInt(-24, 24);
        const int y = theY < 0 ? RandRangeInt(130, 410) : theY + RandRangeInt(-20, 20);
        if (AddCoin(x, y, CoinType::COIN_SUN, CoinMotion::COIN_MOTION_COIN) == nullptr)
        {
            mPendingAugmentSunDrops += theCount - i;
            return;
        }
    }
}

void Board::FlushPendingAugmentSunDrops()
{
    // SaveGame serializes the pending count; postpone until the player can collect.
    while (mPendingAugmentSunDrops > 0 && mCoins.mSize < mCoins.mMaxSize)
    {
        const int x = RandRangeInt(130, 730);
        const int y = RandRangeInt(130, 410);
        if (AddCoin(x, y, CoinType::COIN_SUN, CoinMotion::COIN_MOTION_COIN) == nullptr)
            return;
        --mPendingAugmentSunDrops;
    }
}

void Board::DestroyGravestone(GridItem* theGrave, bool theFromDoom)
{
    if (!theGrave || theGrave->mDead || theGrave->mGridItemType != GridItemType::GRIDITEM_GRAVESTONE)
        return;
    const int x = theGrave->mGridX, y = theGrave->mGridY;
    theGrave->GridItemDie();
    if (theFromDoom)
        AddACrater(x, y)->mGridItemCounter = 18000;
}

void Board::SpawnGraveHarvest()
{
    if (!(HasAugment(RollAugment::AUGMENT_GRAVE_HARVEST) || HasAugment(RollAugment::AUGMENT_GRAVE_HARVEST_II))) return;
    int xs[MAX_GRID_SIZE_X * MAX_GRID_SIZE_Y];
    int ys[MAX_GRID_SIZE_X * MAX_GRID_SIZE_Y];
    int count = 0;
    for (int y = 0; y < (StageHas6Rows() ? 6 : 5); ++y)
        for (int x = 0; x < 9 && x < MAX_GRID_SIZE_X; ++x)
            {
                bool blocked = false;
                GridItem* item = nullptr;
                while (IterateGridItems(item))
                    if (!item->mDead && item->mGridX == x && item->mGridY == y &&
                        (item->mGridItemType == GridItemType::GRIDITEM_GRAVESTONE ||
                         item->mGridItemType == GridItemType::GRIDITEM_CRATER ||
                         item->mGridItemType == GridItemType::GRIDITEM_SCARY_POT ||
                         item->mGridItemType == GridItemType::GRIDITEM_DROPPED_BUCKET ||
                         item->mGridItemType == GridItemType::GRIDITEM_PORTAL_CIRCLE ||
                         item->mGridItemType == GridItemType::GRIDITEM_PORTAL_SQUARE))
                        blocked = true;
                if (!blocked) { xs[count] = x; ys[count++] = y; }
            }
    if (!count || mGridItems.mSize >= mGridItems.mMaxSize) return;
    const int chosen = Rand(count);
    const int x = xs[chosen], y = ys[chosen];
    // Allocate first: never sacrifice a plant if the grid-item array is full.
    GridItem* grave = AddAGraveStone(x, y);
    if (!grave) return;
    grave->mGridItemCounter = 1; // Immediately visible, then grows to 100.
    Plant* p = nullptr;
    while (IteratePlants(p))
        if (!p->mDead && p->mPlantCol == x && p->mRow == y)
            p->Die(); // all layers, including support and Pumpkin
}

bool Board::IsBenchFull()
{
    return mBenchCount >= ROLL_BENCH_SLOTS;
}

// Center the OCCUPIED cards as one compact group. Slot index still determines
// adjacency and merge order; changing the count only changes their visual X.
int Board::GetBenchSlotX(int theSlotIndex)
{
    const int aCount = max(1, mBenchCount);
    const int aStride = SEED_PACKET_WIDTH + ROLL_BENCH_GAP;
    const int aGroupWidth = aCount * SEED_PACKET_WIDTH + (aCount - 1) * ROLL_BENCH_GAP;
    return (BOARD_WIDTH - aGroupWidth) / 2 + theSlotIndex * aStride;
}

int Board::GetBenchRestY()
{
    // One quarter of the packet remains below the bottom edge until hovered.
    return BOARD_HEIGHT - (SEED_PACKET_HEIGHT * 3) / 4;
}

void Board::LayoutBenchPackets()
{
    const unsigned int aNow = GetTickCount();
    for (int i = 0; i < mBenchCount; ++i)
    {
        SeedPacket& aPacket = mBenchPackets[i];
        const int aTargetX = GetBenchSlotX(i);
        const int aTargetY = GetBenchRestY();
        if (aPacket.mPacketType == SeedType::SEED_NONE ||
            (aPacket.mBenchMoving && mBenchMergeActive))
            continue;
        if (!aPacket.mBenchMoving && aPacket.mX == aTargetX &&
            aPacket.mY >= aTargetY - (SEED_PACKET_HEIGHT - (SEED_PACKET_HEIGHT * 3) / 4) && aPacket.mY <= aTargetY)
            continue; // ignore the hover lift, which is independent of Bench layout
        aPacket.mBenchMoveFromX = aPacket.mX;
        aPacket.mBenchMoveFromY = aPacket.mY;
        aPacket.mBenchTargetX = aTargetX;
        aPacket.mBenchTargetY = aTargetY;
        aPacket.mBenchMoveStartTick = aNow;
        aPacket.mBenchMoveDurationMs = ROLL_BENCH_ANIMATION_MS;
        aPacket.mBenchMoving = aPacket.mX != aTargetX || aPacket.mY != aTargetY;
    }
}

bool Board::ReservePlantPoolCard(SeedType theSeedType)
{
    if (!IsRollSeedBank() || theSeedType < SeedType::SEED_PEASHOOTER || theSeedType >= NUM_SEEDS_IN_CHOOSER)
        return false;
    int aIndex = (int)theSeedType;
    if (mSeedPoolMaximum[aIndex] <= 0 || mSeedPoolRemaining[aIndex] <= 0)
        return false;
    --mSeedPoolRemaining[aIndex];
    return true;
}

void Board::ReturnPlantPoolCard(SeedType theSeedType, int theCount)
{
    if (!IsRollSeedBank() || theCount <= 0 || theSeedType < SeedType::SEED_PEASHOOTER || theSeedType >= NUM_SEEDS_IN_CHOOSER)
        return;
    int aIndex = (int)theSeedType;
    if (mSeedPoolMaximum[aIndex] <= 0)
        return;
    mSeedPoolRemaining[aIndex] = min(mSeedPoolMaximum[aIndex], mSeedPoolRemaining[aIndex] + theCount);
}

bool Board::IsBenchCursorIndex(int theSeedBankIndex, int& theBenchIndex)
{
    theBenchIndex = -1;
    if (theSeedBankIndex > -2)
        return false;
    int aIndex = -theSeedBankIndex - 2;
    if (aIndex < 0 || aIndex >= mBenchCount || mBenchPackets[aIndex].mPacketType == SeedType::SEED_NONE)
        return false;
    theBenchIndex = aIndex;
    return true;
}

bool Board::AddSeedToBench(SeedType theSeedType, int theStarLevel, int thePoolCardCount, SeedType theImitaterType, int theSourceX, int theSourceY, int theSunSpent)
{
    if (!IsRollSeedBank() || theSeedType == SeedType::SEED_NONE || IsBenchFull())
        return false;

    int aPoolCardCount = thePoolCardCount;
    // -1 means a new free/reward packet should claim one unreserved copy if available;
    // a nonnegative count is an ownership transfer from a Shop packet or another packet.
    if (aPoolCardCount < 0)
    {
        // Free/reward packets still draw from the shared pool. If no copy remains,
        // leave the reward in the world instead of creating an untracked duplicate.
        if (!ReservePlantPoolCard(theSeedType))
            return false;
        aPoolCardCount = 1;
    }

    SeedPacket& aPacket = mBenchPackets[mBenchCount];
    aPacket.mPoolCardCount = 0;
    aPacket.EmptySlot();
    aPacket.mIsBenchPacket = true;
    aPacket.mIndex = mBenchCount;
    aPacket.mPacketType = theSeedType;
    aPacket.mImitaterType = theImitaterType;
    aPacket.mStarLevel = max(1, min(3, theStarLevel));
    aPacket.mPoolCardCount = aPoolCardCount;
    aPacket.mSunSpent = max(0, theSunSpent);
    aPacket.mActive = true;
    aPacket.mOffsetX = 0;
    // Enter from the exact Shop packet or world reward position. Existing Bench
    // packets begin shifting at the same time, to the new CENTERED layout.
    // The fallback is only used by callers without a visible source packet.
    aPacket.mX = theSourceX >= 0 ? theSourceX : (BOARD_WIDTH - SEED_PACKET_WIDTH) / 2;
    aPacket.mY = theSourceY >= 0 ? theSourceY : GetBenchRestY();
    aPacket.mBenchMoving = false;
    aPacket.mBenchMoveDurationMs = ROLL_BENCH_ANIMATION_MS;
    aPacket.mRollAnim = 0;
    aPacket.mLocked = false;
    ++mBenchCount;
    LayoutBenchPackets();

    // Only natural index order is considered; there is no manual rearrange.
    MergeBenchSeedsInOrder();
    return true;
}

bool Board::BuyRollShopSeedPacket(SeedPacket* thePacket)
{
    if (!IsRollSeedBank() || thePacket == nullptr || thePacket->mIsBenchPacket ||
        thePacket->mPacketType == SeedType::SEED_NONE || thePacket->mRollAnim > 0 ||
        mPaused || mAugmentChoosing || mShopWasClosed || mApp->mGameScene != GameScenes::SCENE_PLAYING ||
        mApp->GetDialogCount() > 0 || mBoardFadeOutCounter >= 0 || mTimeStopCounter > 0)
        return false;
    if (IsBenchFull())
    {
        mApp->PlaySample(Sexy::SOUND_BUZZER);
        if (mApp->GetDialog(Dialogs::DIALOG_MESSAGE) == nullptr)
            mApp->DoDialog(Dialogs::DIALOG_MESSAGE, true, _S("Your Bench Is Full"),
                _S("Double-right-click a Bench slot to sell its plant. No refund will be given."),
                _S("[DIALOG_BUTTON_OK]"), Dialog::BUTTONS_FOOTER);
        return true;
    }
    if (!thePacket->mActive)
    {
        mApp->PlaySample(Sexy::SOUND_BUZZER);
        return true;
    }

    SeedType aSeedType = thePacket->mPacketType;
    SeedType aImitaterType = thePacket->mImitaterType;
    int aStarLevel = max(1, thePacket->mStarLevel);
    int aPoolCardCount = thePacket->mPoolCardCount;
    bool aFallbackPoolReservation = false;
    // Shop offers normally own a reservation already. If an unexpected empty token
    // appears, repair it before charging Sun so a failed purchase cannot consume money.
    if (aPoolCardCount <= 0)
    {
        if (!ReservePlantPoolCard(aSeedType))
        {
            mApp->PlaySample(Sexy::SOUND_BUZZER);
            return true;
        }
        aPoolCardCount = 1;
        aFallbackPoolReservation = true;
    }

    int aCost = GetCurrentPlantCost(aSeedType, aImitaterType);
    const bool aPayCoins = HasAugment(RollAugment::AUGMENT_LUCKY_ROLLS) && !mApp->mEasyPlantingCheat;
    const int aCoinCost = aCost * 10;
    if (!mApp->mEasyPlantingCheat &&
        (aPayCoins ? (mApp->mPlayerInfo == nullptr || mApp->mPlayerInfo->mCoins < aCoinCost)
                   : !CanTakeSunMoney(aCost)))
    {
        if (aFallbackPoolReservation) ReturnPlantPoolCard(aSeedType, aPoolCardCount);
        mApp->PlaySample(Sexy::SOUND_BUZZER);
        mOutOfMoneyCounter = 70;
        return true;
    }
    if (aPayCoins)
    {
        mApp->mPlayerInfo->AddCoins(-aCoinCost);
        mApp->mPlayerInfo->SaveDetails();
    }
    else if (!mApp->mEasyPlantingCheat && aCost > 0 && !TakeSunMoney(aCost))
    {
        if (aFallbackPoolReservation) ReturnPlantPoolCard(aSeedType, aPoolCardCount);
        return true;
    }

    // Capture the on-screen location BEFORE EmptySlot clears the shop offer.
    // Shop seed packets are positioned relative to the SeedBank widget.
    const int aSourceX = mSeedBank->mX + thePacket->mX + thePacket->mOffsetX;
    const int aSourceY = mSeedBank->mY + thePacket->mY;
    // Transfer, don't release, the reserved card from Shop to Bench.
    thePacket->mPoolCardCount = 0;
    thePacket->EmptySlot();
    if (!AddSeedToBench(aSeedType, aStarLevel, aPoolCardCount, aImitaterType, aSourceX, aSourceY, mApp->mEasyPlantingCheat ? 0 : aCost))
    {
        // Defensive rollback; with the synchronous full check above this should not occur.
        if (aPayCoins) { mApp->mPlayerInfo->AddCoins(aCoinCost); mApp->mPlayerInfo->SaveDetails(); }
        else if (!mApp->mEasyPlantingCheat && aCost > 0) AddSunMoney(aCost);
        ReturnPlantPoolCard(aSeedType, aPoolCardCount);
        mApp->PlaySample(Sexy::SOUND_BUZZER);
        return true;
    }

    if (mRollTutorialStep == ROLL_TUT_BUY_SUNFLOWER && aSeedType == SeedType::SEED_SUNFLOWER)
        AdvanceRollTutorial(ROLL_TUT_BUY_SUNFLOWER);
    else if (mRollTutorialStep == ROLL_TUT_BUY_PEASHOOTER && aSeedType == SeedType::SEED_PEASHOOTER)
        AdvanceRollTutorial(ROLL_TUT_BUY_PEASHOOTER);
    else if (mRollTutorialStep == ROLL_TUT_BUY_MERGE_FIRST && aSeedType == SeedType::SEED_SUNFLOWER)
        AdvanceRollTutorial(ROLL_TUT_BUY_MERGE_FIRST);
    else if (mRollTutorialStep == ROLL_TUT_BUY_MERGE_SECOND && aSeedType == SeedType::SEED_SUNFLOWER)
        AdvanceRollTutorial(ROLL_TUT_BUY_MERGE_SECOND);
    OnRollPurchase();
    mApp->PlaySample(SOUND_SEEDLIFT);
    CheckLuckyShopAchievement();
    return true;
}

void Board::RemoveBenchSeedPacket(int theIndex, bool theReturnToPool)
{
    if (theIndex < 0 || theIndex >= mBenchCount)
        return;

    SeedPacket& aRemoved = mBenchPackets[theIndex];
    if (theReturnToPool && aRemoved.mPoolCardCount > 0)
        ReturnPlantPoolCard(aRemoved.mPacketType, aRemoved.mPoolCardCount);
    aRemoved.mPoolCardCount = 0;
    aRemoved.EmptySlot();

    int aNewCount = mBenchCount - 1;
    const unsigned int aSharedMoveStartTick = GetTickCount();
    // Snapshot/shift packet payloads while preserving each packet object's Board/App identity.
    // All packets share one start tick so the 500ms movement is synchronized.
    for (int i = theIndex; i < aNewCount; ++i)
    {
        SeedPacket& aDest = mBenchPackets[i];
        SeedPacket& aSrc = mBenchPackets[i + 1];
        SeedType aType = aSrc.mPacketType;
        SeedType aImitater = aSrc.mImitaterType;
        int aStars = aSrc.mStarLevel;
        int aCards = aSrc.mPoolCardCount;
        int aPaidSun = aSrc.mSunSpent;
        bool aActive = aSrc.mActive;
        int aFromX = aSrc.mX;
        int aFromY = aSrc.mY;

        aDest.mPoolCardCount = 0;
        aDest.EmptySlot();
        aDest.mIsBenchPacket = true;
        aDest.mIndex = i;
        aDest.mPacketType = aType;
        aDest.mImitaterType = aImitater;
        aDest.mStarLevel = aStars;
        aDest.mPoolCardCount = aCards;
        aDest.mSunSpent = aPaidSun;
        aDest.mActive = aActive;
        aDest.mOffsetX = 0;
        aDest.mX = aFromX;
        aDest.mY = aFromY;
        aDest.mBenchMoveFromX = aFromX;
        aDest.mBenchMoveFromY = aFromY;
        aDest.mBenchTargetX = GetBenchSlotX(i);
        aDest.mBenchTargetY = GetBenchRestY();
        aDest.mBenchMoveStartTick = aSharedMoveStartTick;
        aDest.mBenchMoveDurationMs = ROLL_BENCH_ANIMATION_MS;
        aDest.mBenchMoving = aDest.mX != aDest.mBenchTargetX || aDest.mY != aDest.mBenchTargetY;
        aDest.mRollAnim = 0;
        aDest.mLocked = false;

        aSrc.mPoolCardCount = 0;
        aSrc.EmptySlot();
        aSrc.mIsBenchPacket = true;
        aSrc.mIndex = i + 1;
    }
    mBenchCount = aNewCount;
    // Recalculate all target positions for the new occupied count. Keep the same
    // source positions so removal remains a synchronized slide rather than a jump.
    LayoutBenchPackets();
    SeedPacket& aLast = mBenchPackets[mBenchCount];
    aLast.mPoolCardCount = 0;
    aLast.EmptySlot();
    aLast.mIsBenchPacket = true;
    aLast.mIndex = mBenchCount;
    aLast.mX = GetBenchSlotX(mBenchCount);
    aLast.mY = GetBenchRestY();
    aLast.mBenchMoving = false;
    aLast.mBenchMoveDurationMs = ROLL_BENCH_ANIMATION_MS;
}

void Board::MergeBenchSeedsInOrder()
{
    if (!IsRollSeedBank() || mBenchMergeActive || mBenchCount < 2)
        return;

    // Never start a merge while another packet is still sliding into its logical
    // slot. Once the row settles, UpdateBenchPackets calls this again automatically.
    for (int i = 0; i < mBenchCount; ++i)
        if (mBenchPackets[i].mBenchMoving)
            return;

    for (int i = 0; i + 1 < mBenchCount; ++i)
    {
        SeedPacket& aLeft = mBenchPackets[i];
        SeedPacket& aRight = mBenchPackets[i + 1];
        if (aLeft.mPacketType == SeedType::SEED_NONE || aRight.mPacketType == SeedType::SEED_NONE ||
            !aLeft.mActive || !aRight.mActive || aLeft.mPacketType != aRight.mPacketType ||
            aLeft.mImitaterType != aRight.mImitaterType || aLeft.mStarLevel != aRight.mStarLevel || aLeft.mStarLevel >= 3)
            continue;

        mBenchMergeActive = true;
        mBenchMergeLeftIndex = i;
        mBenchMergeStartTick = GetTickCount();
        const int aCenterX = (aLeft.mX + aRight.mX) / 2;
        const int aCenterY = (aLeft.mY + aRight.mY) / 2;
        SeedPacket* aPair[2] = { &aLeft, &aRight };
        for (int j = 0; j < 2; ++j)
        {
            SeedPacket& aPacket = *aPair[j];
            aPacket.mBenchMoveFromX = aPacket.mX;
            aPacket.mBenchMoveFromY = aPacket.mY;
            aPacket.mBenchTargetX = aCenterX;
            aPacket.mBenchTargetY = aCenterY;
            aPacket.mBenchMoveStartTick = mBenchMergeStartTick;
            aPacket.mBenchMoveDurationMs = ROLL_BENCH_MERGE_MS;
            aPacket.mBenchMoving = true;
        }
        return; // one pair at a time; chain reactions start after the row settles
    }
}

void Board::CompleteBenchMerge()
{
    if (!mBenchMergeActive)
        return;
    const int aLeftIndex = mBenchMergeLeftIndex;
    if (aLeftIndex < 0 || aLeftIndex + 1 >= mBenchCount)
    {
        mBenchMergeActive = false;
        mBenchMergeLeftIndex = -1;
        return;
    }

    SeedPacket& aLeft = mBenchPackets[aLeftIndex];
    SeedPacket& aRight = mBenchPackets[aLeftIndex + 1];
    if (aLeft.mPacketType == SeedType::SEED_NONE || aLeft.mPacketType != aRight.mPacketType ||
        aLeft.mImitaterType != aRight.mImitaterType || aLeft.mStarLevel != aRight.mStarLevel || aLeft.mStarLevel >= 3)
    {
        mBenchMergeActive = false;
        mBenchMergeLeftIndex = -1;
        return;
    }

    // The two cards have visibly converged. Only now combine their stars and pool
    // ownership; the combined packet represents the exact same number of copies.
    aLeft.mStarLevel++;
    if (mShiftingBenchPendingBonusMerges > 0)
    { ++mFreeRolls; --mShiftingBenchPendingBonusMerges; }
    aLeft.mPoolCardCount += aRight.mPoolCardCount;
    aLeft.mSunSpent += aRight.mSunSpent;
    aRight.mPoolCardCount = 0;
    // Retain the visual merge position and slide to the new evenly spaced
    // slot during RemoveBenchSeedPacket(), rather than teleporting sideways.
    aLeft.mY = GetBenchRestY();
    aLeft.mBenchMoving = false;
    aLeft.mBenchMoveDurationMs = ROLL_BENCH_ANIMATION_MS;
    mBenchMergeActive = false;
    mBenchMergeLeftIndex = -1;
    RemoveBenchSeedPacket(aLeftIndex + 1, false);
    // Removal starts a synchronized 500ms slide. The automatic update loop will
    // inspect the row again when every packet has reached its destination.
}

void Board::UpdateBenchPackets()
{
    if (!IsRollSeedBank())
        return;
    for (int i = 0; i < mBenchCount; ++i)
        mBenchPackets[i].Update();

    if (mBenchMergeActive)
    {
        if (GetTickCount() - mBenchMergeStartTick >= ROLL_BENCH_MERGE_MS)
            CompleteBenchMerge();
        return;
    }

    bool aAnyMoving = false;
    for (int i = 0; i < mBenchCount; ++i)
        aAnyMoving = aAnyMoving || mBenchPackets[i].mBenchMoving;
    if (!aAnyMoving && mShiftingBenchTransformPending)
        TransformShiftingBench();
    if (!aAnyMoving)
        MergeBenchSeedsInOrder();
    if (mBenchMergeActive || aAnyMoving)
        return;

    // Slide the hovered packet up to reveal its entire original artwork. This
    // changes only its visual/hit-test Y; stars, ownership and ordering are intact.
    const int aMouseX = mApp->mWidgetManager->mLastMouseX - mX;
    const int aMouseY = mApp->mWidgetManager->mLastMouseY - mY;
    int aHovered = -1;
    for (int i = 0; i < mBenchCount; ++i)
    {
        SeedPacket& aPacket = mBenchPackets[i];
        if (aMouseX >= aPacket.mX + aPacket.mOffsetX &&
            aMouseX < aPacket.mX + aPacket.mOffsetX + aPacket.mWidth &&
            aMouseY >= aPacket.mY && aMouseY < aPacket.mY + aPacket.mHeight)
            aHovered = i;
    }
    for (int i = 0; i < mBenchCount; ++i)
    {
        SeedPacket& aPacket = mBenchPackets[i];
        const int aTargetY = GetBenchRestY() - (i == aHovered ? SEED_PACKET_HEIGHT - (SEED_PACKET_HEIGHT * 3) / 4 : 0);
        if (aPacket.mY < aTargetY)
            aPacket.mY = min(aTargetY, aPacket.mY + 4);
        else if (aPacket.mY > aTargetY)
            aPacket.mY = max(aTargetY, aPacket.mY - 4);
    }
}

bool Board::IsRemoveYourSeedMode()
{
	return IsRollSeedBank() && mApp->IsAdventureMode() && mLevel >= ROLL_REMOVE_SEED_START_LEVEL &&
		!mApp->mPlayingQuickplay && !mCutScene->IsSurvivalRepick();
}

// How many plants the player has to pick in the chooser to fill the Chosen Plants pool.
int Board::GetNumChosenPlantsToPick()
{
	if (IsRemoveYourSeedMode()) return GetNumBanSlots();
	if (IsRollSeedBank())
	{
		// The number of plants the level lets you take (GetNumSeedsInBank) decides the pool size,
		// not the number of shop slots (which is capped at ROLL_MAX_SLOTS).
		int aNumAllowed = GetNumSeedsInBank();
		return aNumAllowed >= ROLL_POOL_MIN_BANK_SLOTS ? ROLL_POOL_SIZE_WIDE_BANK : aNumAllowed;
	}

	return mSeedBank->mNumPackets > 0 ? mSeedBank->mNumPackets : GetNumSeedsInBank();
}

int Board::GetNumBanSlots()
{
	int aSlots = 2;
	for (StoreItem aItem = StoreItem::STORE_ITEM_BAN_SLOT_3; aItem <= StoreItem::STORE_ITEM_BAN_SLOT_8; aItem = (StoreItem)(aItem + 1))
	{
		if (mApp->mPlayerInfo->mPurchases[(int)aItem] > 0) aSlots++;
	}
	return min(ROLL_MAX_BAN_SLOTS, aSlots);
}

// How many plant slots the seed bank has. The roll seed bank (shop) never shows more than ROLL_MAX_SLOTS.
int Board::GetNumSeedBankSlots()
{
    // Remove Your Seed uses the chooser's bank as a temporary list of plants to ban.
    // This is intentionally separate from the live Roll Shop capacity.
    if (IsRemoveYourSeedMode())
        return GetNumBanSlots();

    // The Roll Shop always displays its full five plant slots, even on Adventure 1-1
    // through 1-7 where the ordinary seed chooser capacity is smaller. The reroll
    // button occupies a separate position before these five slots and is not counted.
    if (IsRollSeedBank())
        return ROLL_MAX_SLOTS;

    return GetNumSeedsInBank();
}

void Board::RefreshRemoveYourSeedBanCapacity()
{
	// Ban slots expand the temporary chooser list only. Never resize the live Roll Shop.
	if (mSeedBank == nullptr || !IsRemoveYourSeedMode() || mApp->mGameScene != GameScenes::SCENE_LEVEL_INTRO)
		return;

	int aOldCount = mSeedBank->mNumPackets;
	mSeedBank->UpdateWidth();
	for (int i = aOldCount; i < mSeedBank->mNumPackets; ++i)
	{
		SeedPacket& aPacket = mSeedBank->mSeedPackets[i];
		aPacket.EmptySlot();
		aPacket.mIndex = i;
		aPacket.mX = GetSeedBankSlotX(i);
		aPacket.mY = 8;
	}
}

// X position of shop slot theSlotIndex. On roll banks the first position belongs to the Reroll slot.
int Board::GetSeedBankSlotX(int theSlotIndex)
{
	return GetSeedPacketPositionX(IsRollSeedBank() ? theSlotIndex + 1 : theSlotIndex);
}

// The shop rerolls by itself at the start of every wave (free). Locked slots are kept; a plant held on the cursor goes back
// and its slot is rolled like the others (done inside RollAllSlots).
void Board::AutoRollSeedBank()
{
	if (!IsRollSeedBank() || mNumChosenPlants <= 0 || mShopWasClosed)
	{
		return;
	}

	mSeedBank->RollAllSlots(false, false);
	mApp->PlaySample(Sexy::SOUND_SLOT_MACHINE);
}

// The Reroll slot, in board coordinates.
Rect Board::GetRollButtonRect()
{
	Rect aRect = mSeedBank->GetRollButtonRect();
	aRect.mX += mSeedBank->mX;
	aRect.mY += mSeedBank->mY;
	return aRect;
}

bool Board::IsOverRollButton(int theX, int theY)
{
	return IsRollSeedBank() && GetRollButtonRect().Contains(theX, theY);
}

bool Board::CanUseRollButton()
{
	if (IsRollTutorialActive() && mRollTutorialStep != ROLL_TUT_REROLL && mRollTutorialStep != ROLL_TUT_ROLL_LOCKED) return false;
	if (!IsRollSeedBank() || mPaused || mAugmentChoosing || mShopWasClosed || mApp->mGameScene != GameScenes::SCENE_PLAYING || mApp->GetDialogCount() > 0 ||
		mBoardFadeOutCounter >= 0 || mTimeStopCounter > 0 || mSeedBank->mY < 0 || mApp->mCrazyDaveState != CrazyDaveState::CRAZY_DAVE_OFF)
	{
		return false;
	}

	return mCursorObject->mCursorType == CursorType::CURSOR_TYPE_NORMAL || mCursorObject->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_BANK;
}

// Reroll price: always ROLL_SEED_BANK_COST, or free while a free reroll is stored.
int Board::GetRollCost()
{
	if (mApp->mEasyPlantingCheat || mFreeRolls > 0)
	{
		return 0;
	}
	return ROLL_SEED_BANK_COST;
}

void Board::GrantFreeRolls(int theCount)
{
	if (!IsRollSeedBank())
	{
		return;
	}

	if (theCount > 0)
        mFreeRolls = theCount > 1000000000 - mFreeRolls ? 1000000000 : mFreeRolls + theCount;
    // Rerolls accumulate without a gameplay cap. Prevent signed overflow only.
    if (mFreeRolls < 0) mFreeRolls = 0;
}

// Pays the reroll price (or spends a free reroll) and rerolls every unlocked slot of the Chosen Seed Bank.
// Returns true when the input was consumed (even if the player could not afford it).
bool Board::TryRollSeedBank()
{
	if (!CanUseRollButton())
	{
		return false;
	}

    // Validate the locked tutorial step before any Sun, free reroll or achievement counters change.
    if (mRollTutorialStep == ROLL_TUT_ROLL_LOCKED && mSeedBank->CountLockedSlots() == 0)
    {
        DisplayAdvice(_S("Lock a Shop packet before rerolling."),
            MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_NONE);
        return true;
    }
    int aRollCost = GetRollCost();
    if (aRollCost > 0 && !TakeSunMoney(aRollCost))
	{
		return true;	// TakeSunMoney already played the buzzer and flashed the sun counter
	}

	if (aRollCost > 0)
		mPaidRollCount++;

	if (!mApp->mEasyPlantingCheat && mFreeRolls > 0)
	{
		mFreeRolls--;
	}

	// This function is only called by explicit player input (mouse or the D shortcut),
	// so automatic wave rolls and Augment-triggered rolls do not count for High Roller.
	mManualRollCount++;
	if (mManualRollCount >= 66)
		mApp->GetAchievement(AchievementType::ACHIEVEMENT_HIGH_ROLLER);

	// RollAllSlots puts a plant held on the cursor back and rerolls its slot with all the others
    mSeedBank->RollAllSlots(false, false);
    mApp->PlaySample(Sexy::SOUND_SLOT_MACHINE);
    if (mRollTutorialStep == ROLL_TUT_ROLL_LOCKED)
        AdvanceRollTutorial(ROLL_TUT_ROLL_LOCKED);
    else
        AdvanceRollTutorial(ROLL_TUT_REROLL);
    return true;
}

// Right-click on a slot locks it (a reroll keeps locked slots) or unlocks it.
bool Board::TryToggleSlotLock(int theX, int theY)
{
	if (IsRollTutorialActive() && mRollTutorialStep != ROLL_TUT_LOCK) return false;
	if (!IsRollSeedBank() || mPaused || mAugmentChoosing || mShopWasClosed || mApp->mGameScene != GameScenes::SCENE_PLAYING || mCursorObject->mCursorType != CursorType::CURSOR_TYPE_NORMAL)
	{
		return false;
	}

	HitResult aHitResult;
	if (!mSeedBank->MouseHitTest(theX, theY, &aHitResult) || aHitResult.mObjectType != GameObjectType::OBJECT_TYPE_SEEDPACKET)
	{
		return false;
	}

	SeedPacket* aSeedPacket = (SeedPacket*)aHitResult.mObject;
	if (aSeedPacket->mLocked)
	{
		aSeedPacket->mLocked = false;
	}
	else if (mSeedBank->CountLockedSlots() >=
		(mApp->mPlayerInfo && mApp->mPlayerInfo->mPurchases[STORE_ITEM_LOCK_UPGRADE] > 0 ? 2 : ROLL_MAX_LOCKS))
	{
		mApp->PlaySample(Sexy::SOUND_BUZZER);
		return true;
	}
	else
	{
		aSeedPacket->mLocked = true;
		AdvanceRollTutorial(ROLL_TUT_LOCK);
	}

	mApp->PlaySample(Sexy::SOUND_TAP);
	return true;
}

// Called when a plant bought from the roll seed bank is planted.
void Board::OnRollPurchase()
{
    // Buying now creates a Bench packet; the tutorial next asks the player to select
    // that packet and plant it, rather than expecting the Shop click to hold the cursor.
    // Purchases advance only when the expected SeedType is actually bought.
}

// ---------------------------------------------------------------------------------------------
// Roll Shop Income: fixed sky drops plus up to five ACTIVE income plants
// (one per 10 held Sun). Twins produce two Sun each; small Sun-shrooms
// independently provide a free reroll until their growth wave.
// ---------------------------------------------------------------------------------------------

// Called at the start of every wave of a roll level (from SpawnZombieWave).
void Board::OnRollWaveStart()
{
	int aWaveNumber = mCurrentWave + 1;
    // Snapshot before Balanced Budget and other start-of-wave rewards.
    const int aIncomeUnit = mSunMoney / ROLL_INCOME_SUN_STEP;

	// "Shop Holiday": count the closed waves down, then reopen the shop
	bool aShopJustReopened = false;
	if (mShopWasClosed)
	{
		if (mShopClosedWaves > 0)
		{
			mShopClosedWaves--;
		}
		else
		{
			OpenShop();
			aShopJustReopened = true;
		}
	}

	// The first packet is granted when the augment is chosen before wave 3, so the recurring
	// packets start at wave 5 (after two full waves) and repeat every two waves after that.
	if (HasAugment(RollAugment::AUGMENT_BALANCED_BUDGET) && mBalancedBudgetWavesRemaining > 0 && aWaveNumber > ROLL_AUGMENT_WAVE_INDEX)
    {
        DropRegularSun(2);
        --mBalancedBudgetWavesRemaining;
    }
    if (HasAugment(RollAugment::AUGMENT_PEASHOOTER) && IsFlagWave(mCurrentWave))
        GiveUsableSeeds(5, SeedType::SEED_PEASHOOTER, true);

    const int aFirstAugmentWave = ROLL_AUGMENT_WAVE_INDEX + 1;
	if (HasAugment(RollAugment::AUGMENT_PUFF_PATROL) && aWaveNumber > aFirstAugmentWave &&
		(aWaveNumber - aFirstAugmentWave) % 2 == 0)
	{
		GiveUsableSeeds(1, SeedType::SEED_PUFFSHROOM);
	}

    // Each 10 held Sun unlocks ONE working producer, not one Sun per producer.
    // A Twin Sunflower occupies one active slot but emits TWO regular Sun.
    // There are never more than five active slots; the 5/3 sky drops are separate.
    const int aActiveSlots = min(ROLL_MAX_SUN_PLANTS, aIncomeUnit);
    Plant* aProducers[64];
    int aNumProducers = 0;
    Plant* aPlant = nullptr;
    while (IteratePlants(aPlant))
    {
        if (aPlant->mDead || !aPlant->MakesSun()) continue;
        if (aPlant->mSeedType == SeedType::SEED_SUNSHROOM &&
            aPlant->mWaveCounter < ROLL_SUNSHROOM_GROW_WAVES)
        {
            aPlant->ProduceWaveSun(0); // Small: Little Sun; wave 2: grow with no reward.
            continue;
        }
        const bool aGrown = aPlant->mSeedType != SeedType::SEED_SUNSHROOM ||
            aPlant->mState == PlantState::STATE_SUNSHROOM_BIG;
        if (aGrown && !aPlant->mIsAsleep && aNumProducers < 64)
            aProducers[aNumProducers++] = aPlant;
        else
            aPlant->ProduceWaveSun(0); // Reset glow; growing/asleep plants earn nothing.
    }
    if (aNumProducers > 0)
    {
        // Fair rotation among eligible plants, regardless of plant type.
        const int aOffset = (aWaveNumber - 1) % aNumProducers;
        const int aWorking = min(aActiveSlots, aNumProducers);
        int aIncome[64] = { 0 };
        for (int n = 0; n < aWorking; ++n)
        {
            const int k = (n + aOffset) % aNumProducers;
            aIncome[k] = aProducers[k]->mSeedType == SeedType::SEED_TWINSUNFLOWER ? 2 : 1;
        }
        for (int i = 0; i < aNumProducers; ++i)
            aProducers[i]->ProduceWaveSun(aIncome[i]);
    }
    DropWaveSun(aWaveNumber);
    SpawnGraveHarvest();
    if (HasAugment(RollAugment::AUGMENT_SHIFTING_BENCH))
    {
        mShiftingBenchTransformPending = true;
        TransformShiftingBench(); // defer if an earlier merge animation is in progress
    }

	if (!aShopJustReopened)
	{
		AutoRollSeedBank();		// every wave, flag or not, the shop rolls by itself
	}
}

// Re-run this cheap forecast whenever the economy or planted producers change.
// It uses the same ordering and round-robin offset as OnRollWaveStart().
void Board::RefreshFlagAura()
{
    int active = 0;
    Zombie* zombie = nullptr;
    while (IterateZombies(zombie))
        if (zombie->mZombieType == ZombieType::ZOMBIE_FLAG &&
            !zombie->mMindControlled && zombie->IsOnBoard() && !zombie->IsDeadOrDying() &&
            zombie->mFromWave != Zombie::ZOMBIE_WAVE_CUTSCENE)
            ++active;
    const bool entering = (mActiveFlagCount == 0 && active > 0);
    const bool leaving = (mActiveFlagCount > 0 && active == 0);
    mActiveFlagCount = active;
    if (leaving)
    {
        Zombie* restored = nullptr;
        while (IterateZombies(restored)) restored->UpdateAnimSpeed();
    }
    if (entering)
    {
        // Newly protected zombies lose adverse conditions already applied.
        Zombie* protectedZombie = nullptr;
        while (IterateZombies(protectedZombie))
        {
            if (!protectedZombie->IsFlagProtected()) continue;
            if (protectedZombie->mIceTrapCounter > 0) protectedZombie->RemoveIceTrap();
            protectedZombie->mChilledCounter = 0;
            protectedZombie->mButteredCounter = 0;
            protectedZombie->mBrittleFrozen = false;
            if (protectedZombie->mZombieHeight == ZombieHeight::HEIGHT_DRAGGED_UNDER)
                protectedZombie->mZombieHeight = ZombieHeight::HEIGHT_ZOMBIE_NORMAL;
            protectedZombie->mAugmentKelpOwner = -1;
            protectedZombie->UpdateAnimSpeed();
        }
    }
}

void Board::AddDroppedBucket(float theX, float theY, int theRow)
{
    if (theRow < 0 || theRow >= MAX_GRID_SIZE_Y || mGridItems.mSize >= mGridItems.mMaxSize)
        return;
    const int col = PixelToGridXKeepOnBoard((int)theX, (int)theY);
    if (IsPoolSquare(col, theRow)) return; // buckets falling in water are lost
    GridItem* item = mGridItems.DataArrayAlloc();
    if (!item) return;
    item->mGridItemType = GridItemType::GRIDITEM_DROPPED_BUCKET;
    item->mGridX = col;
    item->mGridY = theRow;
    item->mPosX = theX;
    item->mPosY = theY;
    item->mGridItemCounter = 1;
    // Just below the zombie layer, but in front of every plant in this lane.
    item->mRenderOrder = MakeRenderOrder(RenderLayer::RENDER_LAYER_ZOMBIE, theRow, -1);
}

void Board::UpdateNextWaveIncomeForecast()
{
    if (!IsRollSeedBank()) return;
    Plant* producers[64];
    int count = 0;
    Plant* plant = nullptr;
    while (IteratePlants(plant))
    {
        if (plant->mDead || !plant->MakesSun()) continue;
        plant->mRollIncomeActive = false;
        if (plant->mSeedType == SeedType::SEED_SUNSHROOM &&
            (plant->mWaveCounter + 1 <= ROLL_SUNSHROOM_GROW_WAVES ||
             plant->mState != PlantState::STATE_SUNSHROOM_BIG)) continue;
        if (!plant->mIsAsleep && count < 64) producers[count++] = plant;
    }
    if (!count) return;
    const int nextWaveNumber = mCurrentWave + 1;
    const int slots = min(min(ROLL_MAX_SUN_PLANTS, mSunMoney / ROLL_INCOME_SUN_STEP), count);
    const int offset = (nextWaveNumber - 1) % count;
    for (int i = 0; i < slots; ++i)
        producers[(offset + i) % count]->mRollIncomeActive = true;
}

// Exactly three sky Suns by day and one at night; they must be collected.
void Board::DropWaveSun(int theWaveNumber)
{
    (void)theWaveNumber;
    const int aDrops = StageIsNight() ? 1 : 3;
    for (int i = 0; i < aDrops; ++i)
        AddCoin(RandRangeInt(100, 649), 60, CoinType::COIN_SUN, CoinMotion::COIN_MOTION_FROM_SKY);
}

// Exact 46/34/14/5/1 weights when all tiers exist. Missing lower-tier weight
// stays within available tiers 1-3. Missing high-tier weight flows downward.
// Only when ALL low tiers are depleted do surviving high tiers share 100%.
// Sampling the pool after EACH reserved card ensures no phantom stock is drawn.
int Board::RollAvailableTier()
{
    const int rates[5] = { 46, 34, 14, 5, 1 };
    bool available[5] = { false, false, false, false, false };
    for (int i = 0; i < mNumChosenPlants; ++i)
    {
        const ChosenPlant& cp = mChosenPlants[i];
        if (IsSeedBannedFromShop(cp.mSeedType, cp.mImitaterType)) continue;
        int tier = GetPlantTier(cp.mSeedType);
        if (tier >= 1 && tier <= 5 && mSeedPoolRemaining[(int)cp.mSeedType] > 0)
            available[tier - 1] = true;
    }
    int lowWeight = 0;
    for (int i = 0; i < 3; ++i) if (available[i]) lowWeight += rates[i];
    const int highWeight = (available[3] ? rates[3] : 0) + (available[4] ? rates[4] : 0);
    if (lowWeight == 0 && highWeight == 0) return 0;
    int weights[5] = { 0, 0, 0, 0, 0 };
    if (lowWeight == 0)
    {
        weights[3] = available[3] ? rates[3] : 0;
        weights[4] = available[4] ? rates[4] : 0;
    }
    else
    {
        // Scale by lowWeight to preserve the exact relative proportions of
        // available low tiers without rounding off the 5%/1% high-tier odds.
        for (int i = 0; i < 3; ++i)
            weights[i] = available[i] ? rates[i] * (100 - highWeight) : 0;
        weights[3] = available[3] ? rates[3] * lowWeight : 0;
        weights[4] = available[4] ? rates[4] * lowWeight : 0;
    }
    int total = 0;
    for (int i = 0; i < 5; ++i) total += weights[i];
    int ticket = Rand(total);
    for (int i = 0; i < 5; ++i)
    {
        ticket -= weights[i];
        if (ticket < 0) return i + 1;
    }
    return 0;
}

bool Board::CanAffordRollPurchase(int theSunCost) const
{
    if (HasAugment(RollAugment::AUGMENT_LUCKY_ROLLS))
        return mApp->mPlayerInfo != nullptr && mApp->mPlayerInfo->mCoins >= theSunCost * 10;
    return theSunCost <= mSunMoney + const_cast<Board*>(this)->CountSunBeingCollected();
}

// Sun producers on the lawn (Sunflower, Sun-shroom, Twin Sunflower).
int Board::CountSunPlants()
{
	int aCount = 0;
	Plant* aPlant = nullptr;
	while (IteratePlants(aPlant))
	{
		if (!aPlant->mDead && (aPlant->mSeedType == SeedType::SEED_SUNFLOWER || aPlant->mSeedType == SeedType::SEED_SUNSHROOM || aPlant->mSeedType == SeedType::SEED_TWINSUNFLOWER))
		{
			aCount++;
		}
	}
	return aCount;
}

// How many more Sunflowers / Sun-shrooms the shop may offer right now.
int Board::GetSunPlantRoom()
{
	int aRoom = ROLL_MAX_SUN_PLANTS - CountSunPlants() + ROLL_SUN_SHOP_EXTRA;
	return aRoom < 0 ? 0 : aRoom;
}

// Plants that must not show up in the shop (the "Mine Stash" augment removes Potato Mine).
bool Board::IsSeedBannedFromShop(SeedType theSeedType, SeedType theImitaterType)
{
    SeedType aRealSeedType = theSeedType == SeedType::SEED_IMITATER ? theImitaterType : theSeedType;
    if (aRealSeedType >= SeedType::SEED_PEASHOOTER && aRealSeedType < NUM_SEEDS_IN_CHOOSER && mSeedDraftExcluded[(int)aRealSeedType]) return true;
	if (aRealSeedType >= SeedType::SEED_PEASHOOTER && aRealSeedType < NUM_SEEDS_IN_CHOOSER && mRollBannedSeeds[(int)aRealSeedType])
		return true;
	return (HasAugment(RollAugment::AUGMENT_POTATOMINE) && aRealSeedType == SeedType::SEED_POTATOMINE) ||
        (HasAugment(RollAugment::AUGMENT_PUFF_PATROL) && aRealSeedType == SeedType::SEED_PUFFSHROOM);
}

bool Board::IsShopClosed()
{
	return mShopWasClosed;
}

// A plant picked up from the shop goes back to its slot.
void Board::CancelHeldBankPlant()
{
	if (mCursorObject->mCursorType != CursorType::CURSOR_TYPE_PLANT_FROM_BANK)
	{
		return;
	}

	int aIndex = mCursorObject->mSeedBankIndex;
	if (aIndex >= 0 && aIndex < mSeedBank->mNumPackets && mSeedBank->mSeedPackets[aIndex].mPacketType != SeedType::SEED_NONE)
	{
		RefreshSeedPacketFromCursor();
	}
}

// "Shop Holiday" is over: drop 22 regular Sun coins and reopen the shop.
void Board::OpenShop()
{
	mShopWasClosed = false;
	mShopClosedWaves = 0;
	DropRegularSun(22);
	mSeedBank->RollAllSlots(false, false);
	mApp->PlaySample(Sexy::SOUND_SLOT_MACHINE);
	DisplayAdvice(_S("The shop is open again! 22 Sun dropped"), MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_NONE);
}

// Lucky Shop: award when all five visible shop slots show the same exact seed packet.
void Board::CheckLuckyShopAchievement()
{
	if (!IsRollSeedBank() || mSeedBank == nullptr || mSeedBank->mNumPackets < ROLL_MAX_SLOTS)
		return;
	const SeedPacket& aFirst = mSeedBank->mSeedPackets[0];
	if (aFirst.mPacketType == SeedType::SEED_NONE)
		return;
	for (int i = 1; i < ROLL_MAX_SLOTS; ++i)
	{
		const SeedPacket& aPacket = mSeedBank->mSeedPackets[i];
		if (aPacket.mPacketType != aFirst.mPacketType || aPacket.mImitaterType != aFirst.mImitaterType)
			return;
	}
	mApp->GetAchievement(AchievementType::ACHIEVEMENT_LUCKY_SHOP);
}

// "Sunny Neighbors": every plant around a producing Sunflower (3x3) has a 25% chance to make Sun too.
void Board::SunflowerNeighborSun(Plant* theSunflower)
{
    if (theSunflower == nullptr || Rand(100) >= 25) return;
    Plant* candidates[32];
    int count = 0;
    Plant* p = nullptr;
    while (IteratePlants(p))
        if (p != theSunflower && !p->mDead &&
            abs(p->mPlantCol - theSunflower->mPlantCol) <= 1 &&
            abs(p->mRow - theSunflower->mRow) <= 1 && count < 32)
            candidates[count++] = p;
    if (count == 0) return;
    Plant* chosen = candidates[Rand(count)];
    if (mCoins.mSize < mCoins.mMaxSize)
        AddCoin((int)chosen->mX, (int)chosen->mY, CoinType::COIN_SUN, CoinMotion::COIN_MOTION_FROM_PLANT);
    else
        DropRegularSun(1, (int)chosen->mX, (int)chosen->mY);
}

// The choice timer uses wall-clock milliseconds so game speed modifiers cannot change its duration.
static const unsigned int ROLL_AUGMENT_CHOICE_TIME_MS = 30000;
static const unsigned int ROLL_AUGMENT_SELECTION_ANIMATION_MS = 500;
static const unsigned int ROLL_AUGMENT_ANIMATION_TICK_MS = 10;

// ---------------------------------------------------------------------------------------------
// Augments: right before wave 3 the game freezes and the player picks 1 of 3 augments.
// Plant augments are only offered for plants in the Chosen Plants pool and are shown on the almanac plant card
// (with the plant animation); general augments are shown as a gift box.
// ---------------------------------------------------------------------------------------------

// same order as the RollAugment enum
const AugmentDef gAugmentDefs[RollAugment::NUM_AUGMENTS] =
{
	{ SeedType::SEED_PEASHOOTER,	_S("Budget Peas"),		_S("Gain 5 Peashooters now and at the start of every flag wave.") ,	_S("budget_peas") },
	{ SeedType::SEED_SUNFLOWER,		_S("Sunny Neighbors"),	_S("When a Sunflower produces income, a nearby plant has a 25% chance to produce 1 Sun. Gain a Sunflower.") ,	_S("sunny_neighbors") },
	{ SeedType::SEED_CHERRYBOMB,	_S("Cherry Jackpot"),	_S("Whenever a Cherry Bomb explosion kills at least 1 Zombie, drop 1 Mini Cherry Bomb. Gain a Cherry Bomb. ") ,	_S("cherry_jackpot") },
	{ SeedType::SEED_WALLNUT,		_S("Spiked Shell"),		_S("Wall-nuts have 50% less HP, but reflect 25% of the damage zombies deal to them. Gain a Wall-nut.") ,	_S("spiked_shell") },
	{ SeedType::SEED_POTATOMINE,	_S("Mine Stash"),		_S("Gain 3 Potato Mine Seed Packets. Potato Mine leaves the Shop for the rest of this level.") ,	_S("mine_stash") },
	{ SeedType::SEED_SNOWPEA,		_S("Deep Freeze"),		_S("Snow Pea shots have a 5% chance to freeze a zombie for 3 seconds. Gain a Snow Pea.") ,	_S("deep_freeze") },
	{ SeedType::SEED_CHOMPER,		_S("Big Appetite"),		_S("Chompers have triple the range and chew 50% faster. Gain a Chomper.") ,	_S("big_appetite") },
	{ SeedType::SEED_REPEATER,		_S("Armor Piercer"),	_S("Every 30 seconds, a Repeater shot strips one piece of zombie armor. Gain a Repeater.") ,	_S("armor_piercer") },
	{ SeedType::SEED_NONE,			_S("Seed Surprise 1"),	_S("Gain 3 random Plant Seed Packets from Plant Pool.") ,	_S("seed_surprise"), _S("seed_surprise"), 1, 1, false },
	{ SeedType::SEED_NONE,			_S("Sun Boon 1"),			_S("Drop 10 Sun to collect.") ,	_S("sun_boon"), _S("sun_boon"), 1, 1, false },
	{ SeedType::SEED_NONE,			_S("Cash Crops"),			_S("Shop plants cost Coins ($) instead of Sun at 10x their normal Sun price.") ,	_S("lucky_rolls"), nullptr, 1, 1, false },
	{ SeedType::SEED_NONE,			_S("Shop Holiday"),		_S("Disable your shop for the next 3 waves. Then 22 Sun drop.") ,	_S("shop_holiday"), nullptr, 1, 1, false },
	{ SeedType::SEED_NONE,			_S("Tough Plants"),		_S("Your plants gain 20% of max HP.") ,	_S("tough_plants") },
	{ SeedType::SEED_PUFFSHROOM,	_S("Puff Patrol"),		_S("Gain a Puff-shroom now and at the start of every 2 waves. Puff-shroom leaves the Shop for the rest of this level.") ,	_S("puff_patrol") },
	{ SeedType::SEED_NONE,			_S("Gentle Uprooting"),	_S("Shoveling a full-health plant returns its paid Sun as drops.") ,	_S("gentle_uprooting") },
	{ SeedType::SEED_NONE,			_S("Garden Pack"),		_S("Immediately plants up to 8 random plants from Plant Pool on empty lawn tiles.") ,	_S("garden_pack") },
	{ SeedType::SEED_NONE,           _S("Spoils of War"),       _S("Defeated zombies have a 25% chance to drop a loot."), _S("spoils_of_war") },
    { SeedType::SEED_NONE,            _S("Balanced Budget"), _S("Drop 2 Sun now and at the start of each of the next 3 waves."), _S("balanced_budget") },
    { SeedType::SEED_NONE,            _S("Final Reserves"), _S("When all lawn mowers in the current level have been triggered, gain 32 Suns and gain 12 free rerolls."), _S("final_reserves") },
    { SeedType::SEED_NONE,            _S("Hologram"), _S("Gain a Hologram Packet that copies the last plant you planted."), _S("hologram") },
    { SeedType::SEED_SUNSHROOM,       _S("Shadow Shroom"), _S("Zombies ignore your Sun-shrooms. Gain a Sun-shroom."), _S("shadow_shroom") },
    { SeedType::SEED_FUMESHROOM,      _S("Infinite Fumes"), _S("Fume-shrooms have infinite range but attack 25% slower. Gain a Fume-shroom."), _S("infinite_fumes") },
    { SeedType::SEED_GRAVEBUSTER, _S("Grave Dividend"), _S("Grave Buster grants 1 free reroll after destroying a gravestone. Gain a Grave Buster."), _S("grave_dividend") },
    { SeedType::SEED_HYPNOSHROOM, _S("Lingering Hypnosis"), _S("Hypno-shroom survives its first hypnosis. Gain a Hypno-shroom."), _S("lingering_hypnosis") },
    { SeedType::SEED_SCAREDYSHROOM, _S("Panic Spores"), _S("While scared, Scaredy-shroom pulses every 1.5 seconds. Gain a Scaredy-shroom."), _S("panic_spores") },
    { SeedType::SEED_ICESHROOM, _S("Brittle Ice"), _S("Ice-shroom freezes zombies until hit. Breaking the freeze deals 180 armor-piercing damage. Gain an Ice-shroom."), _S("brittle_ice") },
    { SeedType::SEED_DOOMSHROOM, _S("Grave Collapse"), _S("Doom-shroom destroys nearby gravestones, leaving craters. Gain a Doom-shroom."), _S("grave_collapse") },
    { SeedType::SEED_NONE, _S("Grave Harvest 1"), _S("A gravestone appears each wave. Destroying any gravestone drops 2 Sun."), _S("grave_harvest"), _S("grave_harvest"), 1, 1, false },
    { SeedType::SEED_NONE, _S("Garden Glove"), _S("Gain a reusable Glove. Pick up a plant to recover its Seed Packet."), _S("garden_glove") },
    { SeedType::SEED_NONE, _S("Fortune's Favor 1"), _S("Gain a random additional Augment and drop 3 Sun."), _S("fortunes_favor"), _S("fortunes_favor"), 1, 1, true },
    { SeedType::SEED_LILYPAD, _S("Waterbed Renewal"), _S("Plants on Lily Pads recover 10 HP every 5 seconds, up to their maximum HP. Gain a Lily Pad."), _S("waterbed_renewal") },
    { SeedType::SEED_SQUASH, _S("Second Serving"), _S("Squashes survive their first slam on land, then slam once more. Gain a Squash."), _S("second_serving") },
    { SeedType::SEED_THREEPEATER, _S("Split Peas"), _S("Threepeater peas have a 15% chance on hit to split into peas in the adjacent rows. Gain a Threepeater."), _S("split_peas") },
    { SeedType::SEED_TANGLEKELP, _S("Kelp Collective"), _S("Tangle Kelp drags down all zombies of the same type currently swimming in the pool. Gain a Tangle Kelp."), _S("kelp_collective") },
    { SeedType::SEED_JALAPENO, _S("Crossfire"), _S("Jalapenos ignite their row and column, leaving fire for 5 seconds that deals 40 damage per second. Gain a Jalapeno."), _S("crossfire") },
    { SeedType::SEED_TORCHWOOD, _S("Blazing Canopy"), _S("Every 2 seconds, Torchwoods deal 40 damage in a 3x3 area, with a 1% chance to turn zombies to ash. Gain a Torchwood."), _S("blazing_canopy") },
    { SeedType::SEED_SPIKEWEED, _S("Thorn Recoil"), _S("Spikeweed hits have a 10% chance to knock zombies back half a tile. Gain a Spikeweed."), _S("thorn_recoil") },
    { SeedType::SEED_TALLNUT, _S("Enduring Bastion"), _S("After 5 seconds without damage, Tall-nuts heal 50 HP, or gain 10 maximum HP when fully healed. Gain a Tall-nut."), _S("enduring_bastion") },
    { SeedType::SEED_SEASHROOM, _S("Spore Harvest"), _S("Sea-shroom kills have a 20% chance to drop a Puff-shroom or Sea-shroom Seed Packet. Gain a Sea-shroom."), _S("spore_harvest") },
    { SeedType::SEED_PLANTERN, _S("Golden Glow"), _S("Zombies defeated in Plantern light drop one extra Coin in addition to normal loot. Gain a Plantern."), _S("golden_glow") },
    { SeedType::SEED_NONE, _S("Seed Surprise 2"), _S("Gain 5 random Plant Seed Packets from this level's Plant Pool."), _S("seed_surprise_ii"), _S("seed_surprise"), 2, 2, false },
    { SeedType::SEED_NONE, _S("Sun Boon 2"), _S("Drop 22 Sun to collect."), _S("sun_boon_ii"), _S("sun_boon"), 2, 2, false },
    { SeedType::SEED_NONE, _S("Grave Harvest 2"), _S("A gravestone appears each wave. Destroying any gravestone drops 2 Sun. Spawn 5 gravestones now."), _S("grave_harvest_ii"), _S("grave_harvest"), 2, 2, false },
    { SeedType::SEED_NONE, _S("Fortune's Favor 2"), _S("Gain a random additional Augment and drop 6 Sun."), _S("fortunes_favor_ii"), _S("fortunes_favor"), 2, 2, true },
    { SeedType::SEED_NONE, _S("Seed Draft"), _S("Choose 1 of 3 different plants from this level's Plant Pool. Gain it and remove the other two from the Shop for the rest of this level."), _S("seed_draft") },
    { SeedType::SEED_NONE, _S("Shifting Bench"), _S("At the start of each wave, Bench packets transform into random different plants of the same Tier, keeping their stars and using available Pool copies, then gain 1 free reroll for each resulting star upgrade."), _S("shifting_bench") },
};

static SexyString GetActiveAugmentLabel(int theAugment)
{
	return StrFormat(_S("Augment: %s"), gAugmentDefs[theAugment].mName);
}

static Rect GetActiveAugmentTextRect(int theAugment, int theMenuButtonX)
{
    SexyString aLabel = GetActiveAugmentLabel(theAugment);
    int aWidth = FONT_HOUSEOFTERROR16->StringWidth(aLabel);
    const int aRightEdge = theMenuButtonX - 12;
    return Rect(aRightEdge - aWidth, 8, aWidth, 22);
}

static Rect GetActiveAugmentTooltipCardRect(int theAugment, int theMenuButtonX)
{
    (void)theAugment;
    const int aWidth = 246;
    const int aHeight = IMAGE_ALMANAC_PLANTCARD != nullptr
        ? (int)((float)IMAGE_ALMANAC_PLANTCARD->GetHeight() * (float)aWidth / (float)IMAGE_ALMANAC_PLANTCARD->GetWidth())
        : 360;
    // Align the full card with the label's right edge; keep the Menu button unobstructed.
    return Rect(theMenuButtonX - 12 - aWidth, 38, aWidth, aHeight);
}

// The plants shown on the three chooser cards and the selected-Augment tooltip are previews only.
static Plant* gAugmentPlants[3] = { nullptr, nullptr, nullptr };
static Plant* gGalleryAugmentPlants[RollAugment::NUM_AUGMENTS] = { nullptr };
static unsigned int gGalleryAugmentUpdateTick = 0;

static void ClearGalleryAugmentPlants()
{
    for (int i = 0; i < RollAugment::NUM_AUGMENTS; ++i)
        if (gGalleryAugmentPlants[i] != nullptr)
        {
            gGalleryAugmentPlants[i]->Die();
            delete gGalleryAugmentPlants[i];
            gGalleryAugmentPlants[i] = nullptr;
        }
    gGalleryAugmentUpdateTick = 0;
}

static void PrepareGalleryAugmentPlants(const bool* theOwnedAugments)
{
    ClearGalleryAugmentPlants();
    if (theOwnedAugments == nullptr) return;
    // Reuse precisely the chooser's board-less preview construction; this is
    // done on click (not in Draw), and never consumes Plant Pool copies.
    for (int i = 0; i < RollAugment::NUM_AUGMENTS; ++i)
    {
        if (!theOwnedAugments[i] || gAugmentDefs[i].mSeedType == SeedType::SEED_NONE)
            continue;
        Plant* p = new Plant();
        p->mBoard = nullptr;
        p->mIsOnBoard = false;
        p->PlantInitialize(0, 0, gAugmentDefs[i].mSeedType, SeedType::SEED_NONE);
        gGalleryAugmentPlants[i] = p;
    }
    gGalleryAugmentUpdateTick = GetTickCount();
}

static void UpdateGalleryAugmentPlants()
{
    const unsigned int now = GetTickCount();
    if (gGalleryAugmentUpdateTick && now - gGalleryAugmentUpdateTick < ROLL_AUGMENT_ANIMATION_TICK_MS)
        return;
    for (int i = 0; i < RollAugment::NUM_AUGMENTS; ++i)
        if (gGalleryAugmentPlants[i] != nullptr)
            gGalleryAugmentPlants[i]->Update();
    gGalleryAugmentUpdateTick = now;
}

static Plant* gActiveAugmentTooltipPlant = nullptr;
static SeedType gActiveAugmentTooltipPlantType = SeedType::SEED_NONE;
static unsigned int gActiveAugmentTooltipPlantLastUpdateTick = 0;

static void UpdateActiveAugmentTooltipPlant()
{
    if (gActiveAugmentTooltipPlant == nullptr)
        return;

    const unsigned int aNowTick = GetTickCount();
    if (gActiveAugmentTooltipPlantLastUpdateTick == 0 ||
        aNowTick - gActiveAugmentTooltipPlantLastUpdateTick >= ROLL_AUGMENT_ANIMATION_TICK_MS)
    {
        // The Plant was created as a UI preview during card setup. Plant::Update is
        // already used by the chooser for the same kind of board-less preview.
        gActiveAugmentTooltipPlant->Update();
        gActiveAugmentTooltipPlantLastUpdateTick = aNowTick;
    }
}

static void ClearActiveAugmentTooltipPlant()
{
    if (gActiveAugmentTooltipPlant != nullptr)
    {
        gActiveAugmentTooltipPlant->Die();
        delete gActiveAugmentTooltipPlant;
        gActiveAugmentTooltipPlant = nullptr;
    }
    gActiveAugmentTooltipPlantType = SeedType::SEED_NONE;
    gActiveAugmentTooltipPlantLastUpdateTick = 0;
}

static void EnsureActiveAugmentTooltipPlant(int theAugment)
{
    SeedType aSeedType = (theAugment >= 0 && theAugment < RollAugment::NUM_AUGMENTS)
        ? gAugmentDefs[theAugment].mSeedType : SeedType::SEED_NONE;
    if (aSeedType == SeedType::SEED_NONE)
    {
        ClearActiveAugmentTooltipPlant();
        return;
    }

    // Tooltip drawing must never initialize a gameplay Plant. PlantInitialize can
    // allocate reanimations and run plant-specific initialization/RNG code; doing
    // that from DrawUIP caused an assertion/crash while hovering the active label.
    // ChooseAugment transfers the already-initialized preview for the chosen card
    // into gActiveAugmentTooltipPlant. If no preview exists (e.g. a legacy save),
    // leave it null and let the card use its non-plant fallback art.
    if (gActiveAugmentTooltipPlantType == aSeedType)
        return;

    ClearActiveAugmentTooltipPlant();
    gActiveAugmentTooltipPlantType = aSeedType;
    for (int i = 0; i < 3; ++i)
    {
        if (gAugmentPlants[i] != nullptr && gAugmentPlants[i]->mSeedType == aSeedType)
        {
            gActiveAugmentTooltipPlant = gAugmentPlants[i];
            gAugmentPlants[i] = nullptr;
            break;
        }
    }
}

static void DrawActiveAugmentTooltipCard(Graphics* g, int theAugment, int theMenuButtonX)
{
    if (theAugment < 0 || theAugment >= RollAugment::NUM_AUGMENTS || IMAGE_ALMANAC_PLANTCARD == nullptr || IMAGE_ALMANAC_GROUNDDAY == nullptr)
        return;

    EnsureActiveAugmentTooltipPlant(theAugment);
    const AugmentDef& aDef = gAugmentDefs[theAugment];
    Rect aRect = GetActiveAugmentTooltipCardRect(theAugment, theMenuButtonX);
    float aScale = (float)aRect.mWidth / (float)IMAGE_ALMANAC_PLANTCARD->GetWidth();
    Graphics aCardGraphics(*g);
    aCardGraphics.SetColorizeImages(true);
    aCardGraphics.SetColor(Color(255, 255, 255, 250));

    Image* aGround = IMAGE_ALMANAC_GROUNDDAY;
    int aGroundX = aRect.mX + (int)(62 * aScale);
    int aGroundY = aRect.mY + (int)(21 * aScale);
    int aGroundW = (int)(aGround->GetWidth() * aScale);
    int aGroundH = (int)(aGround->GetHeight() * aScale);
    aCardGraphics.DrawImage(aGround, aGroundX, aGroundY, aGroundW, aGroundH);

    if (gActiveAugmentTooltipPlant != nullptr)
    {
        gActiveAugmentTooltipPlant->mX = aRect.mX + (int)(119 * aScale);
        gActiveAugmentTooltipPlant->mY = aRect.mY + (int)(54 * aScale);
        if (gActiveAugmentTooltipPlant->BeginDraw(&aCardGraphics))
        {
            gActiveAugmentTooltipPlant->Draw(&aCardGraphics);
            gActiveAugmentTooltipPlant->EndDraw(&aCardGraphics);
        }
    }
    else if (IMAGE_PRESENT != nullptr)
    {
        float aGiftX = aGroundX + (aGroundW - IMAGE_PRESENT->GetWidth()) / 2.0f;
        // Match the chooser card's gentle vertical bob (chooser timer advances every 10 ms).
        float aBob = sinf((float)GetTickCount() * 0.008f) * 4.0f;
        float aGiftY = aGroundY + (aGroundH - IMAGE_PRESENT->GetHeight()) / 2.0f + aBob;
        aCardGraphics.DrawImage(IMAGE_PRESENT, (int)aGiftX, (int)aGiftY);
    }

    aCardGraphics.SetColorizeImages(true);
    aCardGraphics.SetColor(Color(255, 255, 255, 250));
    aCardGraphics.DrawImage(IMAGE_ALMANAC_PLANTCARD, aRect.mX, aRect.mY, aRect.mWidth, aRect.mHeight);
    aCardGraphics.SetColorizeImages(false);

    float aTextScale = aScale;
    TodDrawStringWrapped(&aCardGraphics, aDef.mName,
        Rect(aRect.mX + (int)(18 * aTextScale), aRect.mY + (int)(184 * aTextScale), (int)(278 * aTextScale), (int)(27 * aTextScale)),
        FONT_DWARVENTODCRAFT18YELLOW, Color(255, 255, 255, 250), DrawStringJustification::DS_ALIGN_CENTER);
    Rect aEffectRect(aRect.mX + (int)(26 * aTextScale), aRect.mY + (int)(230 * aTextScale), (int)(258 * aTextScale), (int)(224 * aTextScale));
    TodDrawStringWrapped(&aCardGraphics, aDef.mEffect, aEffectRect, FONT_BRIANNETOD12,
        Color(40, 50, 90, 250), DrawStringJustification::DS_ALIGN_CENTER_VERTICAL_MIDDLE);
}

void Board::ClearAugmentPlants()
{
	for (int i = 0; i < 3; i++)
	{
		if (gAugmentPlants[i] != nullptr)
		{
			gAugmentPlants[i]->Die();
			delete gAugmentPlants[i];
			gAugmentPlants[i] = nullptr;
		}
	}
}

// Preserve the preview that was already initialized for the chosen card. Reusing
// it avoids running PlantInitialize again during gameplay/UI rendering.
static void PreserveChosenAugmentPreviewPlant(int theChoiceIndex, int theAugment)
{
    ClearActiveAugmentTooltipPlant();

    SeedType aSeedType = (theAugment >= 0 && theAugment < RollAugment::NUM_AUGMENTS)
        ? gAugmentDefs[theAugment].mSeedType : SeedType::SEED_NONE;
    Plant* aPreservedPlant = nullptr;
    if (aSeedType != SeedType::SEED_NONE)
    {
        int aPreserveIndex = -1;
        if (theChoiceIndex >= 0 && theChoiceIndex < 3 && gAugmentPlants[theChoiceIndex] != nullptr &&
            gAugmentPlants[theChoiceIndex]->mSeedType == aSeedType)
        {
            aPreserveIndex = theChoiceIndex;
        }
        else
        {
            for (int i = 0; i < 3; ++i)
            {
                if (gAugmentPlants[i] != nullptr && gAugmentPlants[i]->mSeedType == aSeedType)
                {
                    aPreserveIndex = i;
                    break;
                }
            }
        }

        if (aPreserveIndex >= 0)
        {
            aPreservedPlant = gAugmentPlants[aPreserveIndex];
            gAugmentPlants[aPreserveIndex] = nullptr;
        }
    }

    // Clear the other two choice previews only after detaching the chosen one.
    for (int i = 0; i < 3; ++i)
    {
        if (gAugmentPlants[i] != nullptr)
        {
            gAugmentPlants[i]->Die();
            delete gAugmentPlants[i];
            gAugmentPlants[i] = nullptr;
        }
    }

    gActiveAugmentTooltipPlant = aPreservedPlant;
    gActiveAugmentTooltipPlantType = aPreservedPlant != nullptr ? aSeedType : SeedType::SEED_NONE;
    gActiveAugmentTooltipPlantLastUpdateTick = GetTickCount();
}

static void CreateAugmentPlants(const int* theChoices)
{
	for (int i = 0; i < 3; i++)
	{
		if (gAugmentPlants[i] != nullptr || theChoices[i] < 0 || gAugmentDefs[theChoices[i]].mSeedType == SeedType::SEED_NONE)
			continue;

		Plant* aPlant = new Plant();
		aPlant->mBoard = nullptr;
		aPlant->mIsOnBoard = false;
		aPlant->PlantInitialize(0, 0, gAugmentDefs[theChoices[i]].mSeedType, SeedType::SEED_NONE);
		gAugmentPlants[i] = aPlant;
	}
}

bool Board::ShouldOfferAugment()
{
	return IsRollSeedBank() && !mAugmentChoosing && !mSeedDraftChoosing && !IsRollTutorialActive() &&
        ((!mAugmentOffered && mCurrentWave == ROLL_AUGMENT_WAVE_INDEX) ||
         (!mAugmentStage2Offered && mAugmentOffered && mCurrentWave >= 11 && mNumWaves > 11));
}

// Single source of truth for the stage restrictions on independent variant IDs.
// A Family controls eligibility, NEVER identity or effect dispatch. Solo IDs may
// be offered in either round unless their explicit stage mask restricts them.
static bool IsAugmentAllowedAtStage(int theId, int theStage)
{
    if (theId < 0 || theId >= RollAugment::NUM_AUGMENTS || theStage < 1 || theStage > 31)
        return false;
    const AugmentDef& rule = gAugmentDefs[theId];
    const unsigned int mask = rule.mStageMask ? rule.mStageMask : 3U;
    if ((mask & (1U << (theStage - 1))) == 0) return false;
    if (rule.mFamily != nullptr && rule.mVariantLevel != theStage)
        return false; // Cannot leak a level-1 family member into stage 2.
    return true;
}

bool Board::IsAugmentEligible(int theAugment)
{
	if (theAugment < 0 || theAugment >= RollAugment::NUM_AUGMENTS)
		return false;

    const AugmentDef& rule = gAugmentDefs[theAugment];
    if (!IsAugmentAllowedAtStage(theAugment, mAugmentStage)) return false;
    if (HasAugment(theAugment)) return false;
    // Stage 1 history excludes an exact ID, not unrelated or higher-level variants.
    if (mAugmentStage > 1 && mStage1Shown[theAugment]) return false;
    if (rule.mFamily != nullptr && rule.mVariantLevel > 1 && !rule.mAllowCoexist)
    {
        for (int i = 0; i < RollAugment::NUM_AUGMENTS; ++i)
            if (gAugmentDefs[i].mFamily != nullptr && gAugmentDefs[i].mVariantLevel < rule.mVariantLevel &&
                SexyString(gAugmentDefs[i].mFamily) == rule.mFamily && HasAugment(i))
                return false;
    }
    if (theAugment == RollAugment::AUGMENT_SEED_DRAFT)
    {
        bool found[NUM_SEEDS_IN_CHOOSER] = { false };
        int available = 0;
        for (int i = 0; i < mNumChosenPlants; ++i)
        {
            const SeedType seed = mChosenPlants[i].mSeedType;
            if (seed >= SeedType::SEED_PEASHOOTER && seed < NUM_SEEDS_IN_CHOOSER &&
                !found[(int)seed] && mSeedPoolRemaining[(int)seed] > 0 &&
                !IsSeedBannedFromShop(seed, mChosenPlants[i].mImitaterType))
            { found[(int)seed] = true; ++available; }
        }
        if (available < 3) return false;
    }
	if (theAugment == RollAugment::AUGMENT_GENTLE_UPROOTING && !mShowShovel)
		return false;

	const AugmentDef& aDef = gAugmentDefs[theAugment];
	if (aDef.mSeedType == SeedType::SEED_NONE)
        return true;
    if (HasAugment(RollAugment::AUGMENT_PUFF_PATROL) && aDef.mSeedType == SeedType::SEED_PUFFSHROOM)
        return false;
	if (IsSeedBannedFromShop(aDef.mSeedType, SeedType::SEED_NONE))
		return false;

	for (int i = 0; i < mNumChosenPlants; i++)
	{
		if (mChosenPlants[i].mSeedType == aDef.mSeedType)
			return true;
	}
	return false;
}

// Drafts are stored per mode+level, rather than as a single last-level value.
// IDs (not array indices) encode the offered/rolled-away set so appended Augments
// remain compatible with existing saves and more than 32 Augments are supported.
static int FindAugmentIndexById(const SexyString& theId)
{
	for (int i = 0; i < RollAugment::NUM_AUGMENTS; ++i)
	{
		if (theId == gAugmentDefs[i].mId)
			return i;
	}
	return -1;
}

static bool DecodeAugmentChoiceIds(const SexyString& thePart, int* theChoices)
{
	bool aUsed[RollAugment::NUM_AUGMENTS] = { false };
	size_t aStart = 0;
	for (int i = 0; i < 3; ++i)
	{
		size_t aEnd = thePart.find(static_cast<SexyChar>('|'), aStart);
		if ((i < 2 && aEnd == SexyString::npos) || (i == 2 && aEnd != SexyString::npos))
			return false;
		SexyString aId = thePart.substr(aStart, aEnd == SexyString::npos ? SexyString::npos : aEnd - aStart);
		int aIndex = FindAugmentIndexById(aId);
		if (aIndex < 0 || aUsed[aIndex])
			return false;
		theChoices[i] = aIndex;
		aUsed[aIndex] = true;
		aStart = aEnd == SexyString::npos ? thePart.size() : aEnd + 1;
	}
	return true;
}

static void DecodeAugmentSeenIds(const SexyString& thePart, bool* theSeen)
{
	size_t aStart = 0;
	while (aStart < thePart.size())
	{
		size_t aEnd = thePart.find(static_cast<SexyChar>('|'), aStart);
		SexyString aId = thePart.substr(aStart, aEnd == SexyString::npos ? SexyString::npos : aEnd - aStart);
		int aIndex = FindAugmentIndexById(aId);
		if (aIndex >= 0)
			theSeen[aIndex] = true;
		if (aEnd == SexyString::npos)
			break;
		aStart = aEnd + 1;
	}
}

static bool TryRecoverLegacyInitialChoices(const int* theChoices, const bool* theSeen,
	const bool* theRerolled, int* theInitialChoices)
{
	bool aCurrent[RollAugment::NUM_AUGMENTS] = { false };
	int aNumRerolled = 0;
	for (int i = 0; i < 3; ++i)
	{
		if (theChoices[i] < 0 || theChoices[i] >= RollAugment::NUM_AUGMENTS)
			return false;
		aCurrent[theChoices[i]] = true;
		if (theRerolled[i])
		{
			theInitialChoices[i] = -1;
			++aNumRerolled;
		}
		else
			theInitialChoices[i] = theChoices[i];
	}

	// In legacy drafts, Seen contains every card ever offered. Since each slot can
	// be rerolled only once and a rolled-away card is never offered again, the
	// original cards missing from the current offer can be recovered as a set.
	// Older saves did not retain which rolled slot each missing card belonged to,
	// so assign those recovered cards in stable Augment order.
	int aRecovered[RollAugment::NUM_AUGMENTS];
	int aNumRecovered = 0;
	for (int i = 0; i < RollAugment::NUM_AUGMENTS; ++i)
		if (theSeen[i] && !aCurrent[i])
			aRecovered[aNumRecovered++] = i;
	if (aNumRecovered != aNumRerolled)
		return false;

	int aNext = 0;
	for (int i = 0; i < 3; ++i)
		if (theRerolled[i])
			theInitialChoices[i] = aRecovered[aNext++];
	return true;
}

static bool DecodeAugmentRerollResults(const SexyString& thePart, int* theResults)
{
    size_t aStart = 0;
    for (int i = 0; i < 3; ++i)
    {
        size_t aEnd = thePart.find(static_cast<SexyChar>('|'), aStart);
        if ((i < 2 && aEnd == SexyString::npos) || (i == 2 && aEnd != SexyString::npos))
            return false;
        SexyString aId = thePart.substr(aStart, aEnd == SexyString::npos ? SexyString::npos : aEnd - aStart);
        if (aId == _S("-"))
            theResults[i] = -1;
        else
        {
            int aIndex = FindAugmentIndexById(aId);
            if (aIndex < 0)
                return false;
            theResults[i] = aIndex;
        }
        aStart = aEnd == SexyString::npos ? thePart.size() : aEnd + 1;
    }
    return true;
}

static bool DecodeAugmentDraft(const SexyString& theData, int* theChoices, int* theInitialChoices,
    bool* theSeen, bool* theRerolled, int* theRerollResults, bool& theHasInitialChoices)
{
    memset(theSeen, 0, sizeof(bool) * RollAugment::NUM_AUGMENTS);
    theRerolled[0] = theRerolled[1] = theRerolled[2] = false;
    theRerollResults[0] = theRerollResults[1] = theRerollResults[2] = -1;
    theHasInitialChoices = false;

    // Version 3 persists each slot's first reroll result separately from the
    // currently displayed choices. A retry can restore the initial cards and
    // still replay exactly the same reroll results.
    if (theData.compare(0, 3, _S("v3;")) == 0)
    {
        size_t aInitialEnd = theData.find(static_cast<SexyChar>(';'), 3);
        size_t aChoicesEnd = aInitialEnd == SexyString::npos ? SexyString::npos : theData.find(static_cast<SexyChar>(';'), aInitialEnd + 1);
        size_t aFlagsEnd = aChoicesEnd == SexyString::npos ? SexyString::npos : theData.find(static_cast<SexyChar>(';'), aChoicesEnd + 1);
        size_t aSeenEnd = aFlagsEnd == SexyString::npos ? SexyString::npos : theData.find(static_cast<SexyChar>(';'), aFlagsEnd + 1);
        if (aInitialEnd == SexyString::npos || aChoicesEnd == SexyString::npos || aFlagsEnd == SexyString::npos || aSeenEnd == SexyString::npos)
            return false;

        SexyString aInitialPart = theData.substr(3, aInitialEnd - 3);
        SexyString aChoicePart = theData.substr(aInitialEnd + 1, aChoicesEnd - aInitialEnd - 1);
        SexyString aFlagsPart = theData.substr(aChoicesEnd + 1, aFlagsEnd - aChoicesEnd - 1);
        SexyString aSeenPart = theData.substr(aFlagsEnd + 1, aSeenEnd - aFlagsEnd - 1);
        SexyString aResultsPart = theData.substr(aSeenEnd + 1);
        int aFlag0 = 0, aFlag1 = 0, aFlag2 = 0;
        if (!DecodeAugmentChoiceIds(aInitialPart, theInitialChoices) ||
            !DecodeAugmentChoiceIds(aChoicePart, theChoices) ||
            !DecodeAugmentRerollResults(aResultsPart, theRerollResults) ||
            sexysscanf(aFlagsPart.c_str(), _S("%d,%d,%d"), &aFlag0, &aFlag1, &aFlag2) != 3)
            return false;
        DecodeAugmentSeenIds(aSeenPart, theSeen);
        theRerolled[0] = aFlag0 != 0;
        theRerolled[1] = aFlag1 != 0;
        theRerolled[2] = aFlag2 != 0;
        for (int i = 0; i < 3; ++i)
        {
            theSeen[theInitialChoices[i]] = true;
            theSeen[theChoices[i]] = true;
            if (theRerollResults[i] >= 0)
                theSeen[theRerollResults[i]] = true;
        }
        theHasInitialChoices = true;
        return true;
    }

    // Version 2 already stored initial and live choices, but not a separate
    // reroll history. If a slot had been rerolled, its live choice is the best
    // available migration source for that slot's fixed result.
    if (theData.compare(0, 3, _S("v2;")) == 0)
    {
        const size_t aInitialEnd = theData.find(static_cast<SexyChar>(';'), 3);
        const size_t aChoicesEnd = aInitialEnd == SexyString::npos ? SexyString::npos : theData.find(static_cast<SexyChar>(';'), aInitialEnd + 1);
        const size_t aFlagsEnd = aChoicesEnd == SexyString::npos ? SexyString::npos : theData.find(static_cast<SexyChar>(';'), aChoicesEnd + 1);
        if (aInitialEnd == SexyString::npos || aChoicesEnd == SexyString::npos || aFlagsEnd == SexyString::npos)
            return false;

        SexyString aInitialPart = theData.substr(3, aInitialEnd - 3);
        SexyString aChoicePart = theData.substr(aInitialEnd + 1, aChoicesEnd - aInitialEnd - 1);
        SexyString aFlagsPart = theData.substr(aChoicesEnd + 1, aFlagsEnd - aChoicesEnd - 1);
        SexyString aSeenPart = theData.substr(aFlagsEnd + 1);
        int aFlag0 = 0, aFlag1 = 0, aFlag2 = 0;
        if (!DecodeAugmentChoiceIds(aInitialPart, theInitialChoices) ||
            !DecodeAugmentChoiceIds(aChoicePart, theChoices) ||
            sexysscanf(aFlagsPart.c_str(), _S("%d,%d,%d"), &aFlag0, &aFlag1, &aFlag2) != 3)
            return false;
        DecodeAugmentSeenIds(aSeenPart, theSeen);
        theRerolled[0] = aFlag0 != 0; theRerolled[1] = aFlag1 != 0; theRerolled[2] = aFlag2 != 0;
        for (int i = 0; i < 3; ++i)
        {
            theRerollResults[i] = theRerolled[i] ? theChoices[i] : -1;
            theSeen[theInitialChoices[i]] = true;
            theSeen[theChoices[i]] = true;
        }
        theHasInitialChoices = true;
        return true;
    }

    if (theData.find(static_cast<SexyChar>(';')) == SexyString::npos)
    {
        // Backward compatibility with older version-14 draft payloads, which stored
        // array indices and a 32-bit seen mask. Recover what can be recovered.
        int c0 = -1, c1 = -1, c2 = -1, r0 = 0, r1 = 0, r2 = 0;
        unsigned int aSeenMask = 0;
        if (sexysscanf(theData.c_str(), _S("%d,%d,%d,%u,%d,%d,%d"), &c0, &c1, &c2, &aSeenMask, &r0, &r1, &r2) != 7)
            return false;
        if (c0 < 0 || c0 >= RollAugment::NUM_AUGMENTS || c1 < 0 || c1 >= RollAugment::NUM_AUGMENTS || c2 < 0 || c2 >= RollAugment::NUM_AUGMENTS || c0 == c1 || c0 == c2 || c1 == c2)
            return false;
        theChoices[0] = c0; theChoices[1] = c1; theChoices[2] = c2;
        for (int i = 0; i < 3; ++i)
            theInitialChoices[i] = theChoices[i];
        for (int i = 0; i < RollAugment::NUM_AUGMENTS && i < 32; ++i)
            theSeen[i] = (aSeenMask & (1u << i)) != 0;
        for (int i = 0; i < 3; ++i)
            theSeen[theChoices[i]] = true;
        theRerolled[0] = r0 != 0; theRerolled[1] = r1 != 0; theRerolled[2] = r2 != 0;
        theHasInitialChoices = TryRecoverLegacyInitialChoices(theChoices, theSeen, theRerolled, theInitialChoices);
        for (int i = 0; i < 3; ++i)
            theRerollResults[i] = theRerolled[i] ? theChoices[i] : -1;
        return true;
    }

    // Legacy ID-based drafts preserve the current offer and Seen set, but not
    // always the original offer. Recover the original where its history allows it.
    const size_t aFirstSep = theData.find(static_cast<SexyChar>(';'));
    const size_t aSecondSep = theData.find(static_cast<SexyChar>(';'), aFirstSep + 1);
    if (aSecondSep == SexyString::npos)
        return false;
    SexyString aChoicePart = theData.substr(0, aFirstSep);
    SexyString aFlagsPart = theData.substr(aFirstSep + 1, aSecondSep - aFirstSep - 1);
    SexyString aSeenPart = theData.substr(aSecondSep + 1);
    int aFlag0 = 0, aFlag1 = 0, aFlag2 = 0;
    if (!DecodeAugmentChoiceIds(aChoicePart, theChoices) ||
        sexysscanf(aFlagsPart.c_str(), _S("%d,%d,%d"), &aFlag0, &aFlag1, &aFlag2) != 3)
        return false;
    for (int i = 0; i < 3; ++i)
        theInitialChoices[i] = theChoices[i];
    DecodeAugmentSeenIds(aSeenPart, theSeen);
    for (int i = 0; i < 3; ++i)
        theSeen[theChoices[i]] = true;
    theRerolled[0] = aFlag0 != 0; theRerolled[1] = aFlag1 != 0; theRerolled[2] = aFlag2 != 0;
    theHasInitialChoices = TryRecoverLegacyInitialChoices(theChoices, theSeen, theRerolled, theInitialChoices);
    for (int i = 0; i < 3; ++i)
        theRerollResults[i] = theRerolled[i] ? theChoices[i] : -1;
    return true;
}

static SexyString EncodeAugmentDraft(const int* theInitialChoices, const int* theChoices, const bool* theSeen,
    const bool* theRerolled, const int* theRerollResults)
{
    SexyString aData = _S("v3;");
    for (int i = 0; i < 3; ++i)
    {
        if (i > 0) aData += _S("|");
        aData += gAugmentDefs[theInitialChoices[i]].mId;
    }
    aData += _S(";");
    for (int i = 0; i < 3; ++i)
    {
        if (i > 0) aData += _S("|");
        aData += gAugmentDefs[theChoices[i]].mId;
    }
    aData += _S(";");
    aData += theRerolled[0] ? _S("1,") : _S("0,");
    aData += theRerolled[1] ? _S("1,") : _S("0,");
    aData += theRerolled[2] ? _S("1;") : _S("0;");
    bool aFirstSeen = true;
    for (int i = 0; i < RollAugment::NUM_AUGMENTS; ++i)
    {
        if (theSeen[i])
        {
            if (!aFirstSeen)
                aData += _S("|");
            aData += gAugmentDefs[i].mId;
            aFirstSeen = false;
        }
    }
    aData += _S(";");
    for (int i = 0; i < 3; ++i)
    {
        if (i > 0) aData += _S("|");
        if (theRerollResults[i] >= 0 && theRerollResults[i] < RollAugment::NUM_AUGMENTS)
            aData += gAugmentDefs[theRerollResults[i]].mId;
        else
            aData += _S("-");
    }
    return aData;
}

static bool ReadAugmentDraft(PlayerInfo* thePlayerInfo, const SexyString& theKey, SexyString& theData)
{
	if (!thePlayerInfo || thePlayerInfo->mAugmentDraftData.empty())
		return false;
	SexyString aHistory = thePlayerInfo->mAugmentDraftData;
	if (aHistory.find(static_cast<SexyChar>('#')) == SexyString::npos)
	{
		// Migrate the legacy single-entry representation when its key matches.
		if (thePlayerInfo->mAugmentDraftKey == theKey)
		{
			theData = aHistory;
			return true;
		}
		return false;
	}
	SexyString aKey = theKey;
	size_t aStart = 0;
	while (aStart < aHistory.size())
	{
		size_t aEnd = aHistory.find(static_cast<SexyChar>('\n'), aStart);
		SexyString aLine = aHistory.substr(aStart, aEnd == SexyString::npos ? SexyString::npos : aEnd - aStart);
		size_t aSep = aLine.find(static_cast<SexyChar>('#'));
		if (aSep != SexyString::npos && aLine.substr(0, aSep) == aKey)
		{
			theData = aLine.substr(aSep + 1);
			return !theData.empty();
		}
		if (aEnd == SexyString::npos)
			break;
		aStart = aEnd + 1;
	}
	return false;
}

static void WriteAugmentDraft(PlayerInfo* thePlayerInfo, const SexyString& theKey, const SexyString& thePayload)
{
	if (!thePlayerInfo)
		return;
	std::map<SexyString, SexyString> aEntries;
	SexyString aHistory = thePlayerInfo->mAugmentDraftData;
	if (aHistory.find(static_cast<SexyChar>('#')) != SexyString::npos)
	{
		size_t aStart = 0;
		while (aStart < aHistory.size())
		{
			size_t aEnd = aHistory.find(static_cast<SexyChar>('\n'), aStart);
			SexyString aLine = aHistory.substr(aStart, aEnd == SexyString::npos ? SexyString::npos : aEnd - aStart);
			size_t aSep = aLine.find(static_cast<SexyChar>('#'));
			if (aSep != SexyString::npos)
				aEntries[aLine.substr(0, aSep)] = aLine.substr(aSep + 1);
			if (aEnd == SexyString::npos)
				break;
			aStart = aEnd + 1;
		}
	}
	else if (!aHistory.empty() && !thePlayerInfo->mAugmentDraftKey.empty())
	{
		// Retain the one legacy draft while moving to the multi-screen history format.
		aEntries[thePlayerInfo->mAugmentDraftKey] = aHistory;
	}
	aEntries[theKey] = thePayload;
	aHistory.clear();
	for (std::map<SexyString, SexyString>::const_iterator it = aEntries.begin(); it != aEntries.end(); ++it)
		aHistory += it->first + _S("#") + it->second + _S("\n");
	thePlayerInfo->mAugmentDraftKey = theKey;
	thePlayerInfo->mAugmentDraftData = aHistory;
	thePlayerInfo->SaveDetails();
}

bool Board::SavedAugmentsBlockedByBans(const bool* theBans)
{
    if (mApp->mDeveloperDebugSession) return false; // debug uses fresh drafts
    if (mApp->mPlayerInfo == nullptr || theBans == nullptr)
        return false;
    SexyString aDraft;
    const SexyString aKey = StrFormat(_S("%d:%d"), (int)mApp->mGameMode, mLevel);
    if (!ReadAugmentDraft(mApp->mPlayerInfo, aKey, aDraft))
        return false;
    int aCurrent[3], aInitial[3], aResults[3];
    bool aSeen[RollAugment::NUM_AUGMENTS], aRerolled[3], aHasInitial = false;
    if (!DecodeAugmentDraft(aDraft, aCurrent, aInitial, aSeen, aRerolled, aResults, aHasInitial))
        return false;
    // Check original choices, because Restart restores them, not the last rerolled UI.
    for (int i = 0; i < 3; ++i)
    {
        const int aIndex = aHasInitial ? aInitial[i] : aCurrent[i];
        if (aIndex < 0 || aIndex >= RollAugment::NUM_AUGMENTS)
            return false;
        const SeedType aPlant = gAugmentDefs[aIndex].mSeedType;
        if (aPlant == SeedType::SEED_NONE || !theBans[(int)aPlant])
            return false;
    }
    return true;
}

void Board::ClearWonAugmentDraft()
{
    if (mApp->mDeveloperDebugSession) return; // never modify a normal profile during testing
    if (mApp->mPlayerInfo == nullptr || !IsRollSeedBank())
        return;
    PlayerInfo* aPlayer = mApp->mPlayerInfo;
    const SexyString aKey = StrFormat(_S("%d:%d"), (int)mApp->mGameMode, mLevel);
    std::map<SexyString, SexyString> aEntries;
    SexyString aHistory = aPlayer->mAugmentDraftData;
    if (aHistory.find(static_cast<SexyChar>('#')) != SexyString::npos)
    {
        size_t aStart = 0;
        while (aStart < aHistory.size())
        {
            size_t aEnd = aHistory.find(static_cast<SexyChar>('\n'), aStart);
            SexyString aLine = aHistory.substr(aStart, aEnd == SexyString::npos ? SexyString::npos : aEnd - aStart);
            size_t aSep = aLine.find(static_cast<SexyChar>('#'));
            if (aSep != SexyString::npos)
            {
                const SexyString aStoredKey = aLine.substr(0, aSep);
                const SexyString aFortunePrefix = aKey + _S(":fortune:");
                if (aStoredKey != aKey && aStoredKey != aKey + _S(":stage2") && aStoredKey != aKey + _S(":seed_draft") && aStoredKey.compare(0, aFortunePrefix.size(), aFortunePrefix) != 0)
                    aEntries[aStoredKey] = aLine.substr(aSep + 1);
            }
            if (aEnd == SexyString::npos) break;
            aStart = aEnd + 1;
        }
    }
    else if (!aHistory.empty() && aPlayer->mAugmentDraftKey != aKey)
        aEntries[aPlayer->mAugmentDraftKey] = aHistory;
    aPlayer->mAugmentDraftData.clear();
    for (std::map<SexyString, SexyString>::const_iterator it = aEntries.begin(); it != aEntries.end(); ++it)
        aPlayer->mAugmentDraftData += it->first + _S("#") + it->second + _S("\n");
    aPlayer->mAugmentDraftKey.clear();
    aPlayer->SaveDetails();
}

void Board::StartAugmentSelection()
{
    if (mCurrentWave >= 11 && mAugmentOffered)
    {
        // Only cards actually displayed in Stage 1 count as seen. Preset rerolls
        // that the player never pressed are deliberately excluded.
        for (int i = 0; i < 3; ++i)
        {
            if (mAugmentInitialChoices[i] >= 0 && mAugmentInitialChoices[i] < RollAugment::NUM_AUGMENTS)
                mStage1Shown[mAugmentInitialChoices[i]] = true;
            if (mAugmentRerolled[i] && mAugmentChoices[i] >= 0 && mAugmentChoices[i] < RollAugment::NUM_AUGMENTS)
                mStage1Shown[mAugmentChoices[i]] = true;
        }
    }
    mAugmentStage = (mCurrentWave >= 11 && mAugmentOffered) ? 2 : 1;
    if (mAugmentStage == 2)
        mAugmentStage2Offered = true;
    else
        mAugmentOffered = true;
    mAugmentChoosing = true;
    mAugmentSelectionPending = false;
    mAugmentSelectedIndex = -1;
    mAugmentSelectionStartTick = 0;
    mAugmentHidden = false;
    mAugmentTimer = 0;
    mAugmentStartTick = GetTickCount();
    mAugmentLastPlantUpdateTick = mAugmentStartTick;
    mAugmentHover = -1;
    mAugmentRerolled[0] = mAugmentRerolled[1] = mAugmentRerolled[2] = false;
    mAugmentRerollResults[0] = mAugmentRerollResults[1] = mAugmentRerollResults[2] = -1;
    memset(mAugmentSeen, 0, sizeof(mAugmentSeen));
    mAugmentRestoreFastMode = mApp->mIsFastMode;
    // Stop the game's multi-update speed-up while the selection UI is open.
    mApp->mIsFastMode = false;
    if (mFastButton != nullptr && !mFastButton->mBtnNoDraw)
    {
        mFastButton->mButtonImage = IMAGE_FASTBUTTON;
        mFastButton->mOverImage = IMAGE_FASTBUTTON;
        mFastButton->mDownImage = IMAGE_FASTBUTTON_HIGHLIGHT;
    }
    CancelHeldBankPlant();
    TutorialArrowRemove();

    const bool aResetPending = mAugmentResetPending;
    SexyString aDraftKey = StrFormat(mAugmentStage == 2 ? _S("%d:%d:stage2") : _S("%d:%d"), (int)mApp->mGameMode, mLevel);
    PlayerInfo* aPlayerInfo = mApp->mPlayerInfo;
    SexyString aStoredDraft;
    int aStoredChoices[3] = { -1, -1, -1 };
    int aStoredInitialChoices[3] = { -1, -1, -1 };
    int aStoredRerollResults[3] = { -1, -1, -1 };
    bool aStoredSeen[RollAugment::NUM_AUGMENTS] = { false };
    bool aStoredRerolled[3] = { false, false, false };
    bool aHasInitialChoices = false;
    bool aHasDraft = !mApp->mDeveloperDebugSession && ReadAugmentDraft(aPlayerInfo, aDraftKey, aStoredDraft) &&
        DecodeAugmentDraft(aStoredDraft, aStoredChoices, aStoredInitialChoices, aStoredSeen,
            aStoredRerolled, aStoredRerollResults, aHasInitialChoices);
    // Stage 1 choices can differ between replays of an unfinished level. An
    // older Stage 2 draft must never reoffer an ID actually shown in Stage 1,
    // nor a II variant whose I version was chosen on this attempt. Preserve
    // a compatible draft; regenerate only when the new history conflicts.
    if (mAugmentStage == 2 && aHasDraft && aHasInitialChoices)
    {
        for (int i = 0; i < 3 && aHasDraft; ++i)
        {
            const int ids[3] = { aStoredInitialChoices[i], aStoredChoices[i], aStoredRerollResults[i] };
            for (int j = 0; j < 3; ++j)
            {
                const int id = ids[j];
                if (id < 0 || id >= RollAugment::NUM_AUGMENTS) continue;
                const AugmentDef& rule = gAugmentDefs[id];
                if (mStage1Shown[id] || !IsAugmentAllowedAtStage(id, 2))
                { aHasDraft = false; break; }
                if (rule.mFamily != nullptr && rule.mVariantLevel > 1 && !rule.mAllowCoexist)
                    for (int k = 0; k < RollAugment::NUM_AUGMENTS; ++k)
                        if (gAugmentDefs[k].mFamily != nullptr &&
                            gAugmentDefs[k].mVariantLevel < rule.mVariantLevel &&
                            SexyString(gAugmentDefs[k].mFamily) == rule.mFamily && HasAugment(k))
                        { aHasDraft = false; break; }
                if (!aHasDraft) break;
            }
        }
    }
    // Legacy Lucky Shop drafts have an obsolete string ID and therefore fail
    // DecodeAugmentDraft naturally; Spoils of War retains enum slot 16 safely.
    bool aRestoredDraft = false;

    int aEligible[RollAugment::NUM_AUGMENTS];
    int aNumEligible = 0;
    for (int aAugment = 0; aAugment < RollAugment::NUM_AUGMENTS; ++aAugment)
        if (IsAugmentEligible(aAugment))
            aEligible[aNumEligible++] = aAugment;

    // History is immutable for an unfinished run. Banning a Plant changes whether its
    // card can be chosen, never the identity or position of the stored card.
    if (aHasDraft && aHasInitialChoices)
    {
        memcpy(mAugmentInitialChoices, aStoredInitialChoices, sizeof(mAugmentInitialChoices));
        memcpy(mAugmentChoices, aResetPending ? aStoredInitialChoices : aStoredChoices, sizeof(mAugmentChoices));
        if (aResetPending)
            memset(mAugmentRerolled, 0, sizeof(mAugmentRerolled));
        else
            memcpy(mAugmentRerolled, aStoredRerolled, sizeof(mAugmentRerolled));
        memcpy(mAugmentRerollResults, aStoredRerollResults, sizeof(mAugmentRerollResults));
        aRestoredDraft = true;
    }

    if (!aRestoredDraft)
    {
        // Create the initial three cards exactly once. Later restarts do not regenerate them.
        for (int i = 0; i < 3; ++i)
        {
            if (aNumEligible <= 0)
            {
                mAugmentChoices[i] = -1;
                continue;
            }
            int aPick = Rand(aNumEligible);
            mAugmentChoices[i] = aEligible[aPick];
            aEligible[aPick] = aEligible[--aNumEligible];
        }
        memcpy(mAugmentInitialChoices, mAugmentChoices, sizeof(mAugmentInitialChoices));
        memset(mAugmentRerolled, 0, sizeof(mAugmentRerolled));
        mAugmentRerollResults[0] = mAugmentRerollResults[1] = mAugmentRerollResults[2] = -1;
    }

    // Precompute and persist EACH reroll's result before a player can press ROLL.
    // A ban introduced on restart cannot replace an existing fixed result.
    memset(mAugmentSeen, 0, sizeof(mAugmentSeen));
    for (int i = 0; i < 3; ++i)
    {
        if (mAugmentInitialChoices[i] >= 0 && mAugmentInitialChoices[i] < RollAugment::NUM_AUGMENTS)
            mAugmentSeen[mAugmentInitialChoices[i]] = true;
        if (mAugmentRerollResults[i] >= 0 && mAugmentRerollResults[i] < RollAugment::NUM_AUGMENTS)
            mAugmentSeen[mAugmentRerollResults[i]] = true;
    }
    for (int i = 0; i < 3; ++i)
    {
        if (mAugmentRerollResults[i] >= 0)
            continue;
        int aReplacementPool[RollAugment::NUM_AUGMENTS];
        int aCount = 0;
        for (int j = 0; j < RollAugment::NUM_AUGMENTS; ++j)
            if (IsAugmentEligible(j) && !mAugmentSeen[j])
                aReplacementPool[aCount++] = j;
        if (aCount > 0)
        {
            const int aResult = aReplacementPool[Rand(aCount)];
            mAugmentRerollResults[i] = aResult;
            mAugmentSeen[aResult] = true;
        }
    }

    mAugmentResetPending = false;
    if (mRollTutorialStep == ROLL_TUT_WAIT_AUGMENT)
        SetRollTutorialStep(ROLL_TUT_AUGMENT_REROLL);
    ClearAugmentPlants();
    CreateAugmentPlants(mAugmentChoices);
    if (!mApp->mDeveloperDebugSession && aPlayerInfo != nullptr && mAugmentChoices[0] >= 0 && mAugmentChoices[1] >= 0 && mAugmentChoices[2] >= 0)
        WriteAugmentDraft(aPlayerInfo, aDraftKey, EncodeAugmentDraft(mAugmentInitialChoices, mAugmentChoices,
            mAugmentSeen, mAugmentRerolled, mAugmentRerollResults));
}

void Board::UpdateAugmentSelection()
{
	unsigned int aNowTick = GetTickCount();
    // mAugmentStartTick is the ORIGINAL entrance/preview animation clock.
    // Never rewrite it during the reroll lesson: that had frozen gift bobbing
    // and bypassed the normal fade/slide entrance animation.
    const unsigned int aVisualElapsedMs = aNowTick - mAugmentStartTick;
    mAugmentTimer = (int)(aVisualElapsedMs / ROLL_AUGMENT_ANIMATION_TICK_MS);
    // After the tutorial reroll, start an independent 30-second choice window.
    // The selection-start tick is free until a card has been clicked.
    const unsigned int aChoiceElapsedMs = mRollTutorialStep == ROLL_TUT_AUGMENT_CHOOSE ?
        aNowTick - mAugmentSelectionStartTick : aVisualElapsedMs;

    if (mAugmentSelectionPending)
    {
        if (aNowTick - mAugmentSelectionStartTick >= ROLL_AUGMENT_SELECTION_ANIMATION_MS)
        {
            int aChosenIndex = mAugmentSelectedIndex;
            mAugmentSelectionPending = false;
            mAugmentSelectedIndex = -1;
            ChooseAugment(aChosenIndex);
        }
        return;
    }

	// The Augment chooser already highlights its cards; do not spawn the pulsing
	// tutorial arrow here, as it appears as an unexplained flicker over early rounds.

	CreateAugmentPlants(mAugmentChoices); // recreate preview plants if needed after loading
	if (aNowTick - mAugmentLastPlantUpdateTick >= ROLL_AUGMENT_ANIMATION_TICK_MS)
	{
        for (int i = 0; i < 3; ++i)
        {
            Plant* aPreview = gAugmentPlants[i];
            if (aPreview == nullptr)
                continue;
            aPreview->Update();
            // Board::Update returns early during Augment selection, so the
            // normal EffectSystem::Update never advances Reanimations. Advance
            // ONLY this preview plant's body (and its attached effects), not
            // the live lawn's animations that should remain paused.
            Reanimation* aPreviewReanim = mApp->ReanimationTryToGet(aPreview->mBodyReanimID);
            if (aPreviewReanim != nullptr && !aPreviewReanim->mDead)
                aPreviewReanim->Update();
        }
        mAugmentLastPlantUpdateTick = aNowTick;
	}

	// Thirty seconds of real time, unaffected by Speed Up. The choice is made from the
	// currently visible cards, including any replacements.
    // The introductory lesson has no timeout until its mandatory reroll is done.
	if (mRollTutorialStep != ROLL_TUT_AUGMENT_REROLL && aChoiceElapsedMs >= ROLL_AUGMENT_CHOICE_TIME_MS)
	{
        int aAvailable[3];
        int aCount = 0;
        for (int i = 0; i < 3; ++i)
            if (IsAugmentEligible(mAugmentChoices[i]))
                aAvailable[aCount++] = i;
        if (aCount > 0)
            ChooseAugment(aAvailable[Rand(aCount)]);
        return;
	}

	int aMouseX = mApp->mWidgetManager->mLastMouseX - mX;
	int aMouseY = mApp->mWidgetManager->mLastMouseY - mY;
	mAugmentHover = -1;
	bool aOverControl = false;
    if (mAugmentSelectionPending)
    {
        mApp->SetCursor(CURSOR_POINTER);
        return;
    }
	if (mAugmentHidden)
	{
		aOverControl = GetAugmentHideButtonRect().Contains(aMouseX, aMouseY);
	}
	else
	{
		for (int i = 0; i < 3; i++)
		{
			Rect aCard = GetAugmentCardRect(i);
			if (mAugmentTimer < 18)
				aCard.mY += 18 - mAugmentTimer;
			if (aCard.Contains(aMouseX, aMouseY))
				mAugmentHover = i;
			if (CanRerollAugment(i) && GetAugmentRerollButtonRect(i).Contains(aMouseX, aMouseY))
				aOverControl = true;
		}
		aOverControl = aOverControl || GetAugmentHideButtonRect().Contains(aMouseX, aMouseY);
	}
	mApp->SetCursor((mAugmentHover >= 0 || aOverControl) ? CURSOR_HAND : CURSOR_POINTER);
}

Rect Board::GetAugmentCardRect(int theIndex)
{
	const int aCardWidth = 246;
	const int aGap = 14;
	int aTotalWidth = 3 * aCardWidth + 2 * aGap;
	float aScale = (float)aCardWidth / (float)IMAGE_ALMANAC_PLANTCARD->GetWidth();
	return Rect((BOARD_WIDTH - aTotalWidth) / 2 + theIndex * (aCardWidth + aGap), 78, aCardWidth, (int)(IMAGE_ALMANAC_PLANTCARD->GetHeight() * aScale));
}

Rect Board::GetAugmentRerollButtonRect(int theIndex)
{
	Rect aCard = GetAugmentCardRect(theIndex);
	const int aWidth = 92;
	const int aHeight = 25;
	int aSlide = mAugmentTimer < 18 ? (18 - mAugmentTimer) : 0;
	return Rect(aCard.mX + (aCard.mWidth - aWidth) / 2, aCard.mY + aCard.mHeight + 5 + aSlide, aWidth, aHeight);
}

Rect Board::GetAugmentTimerRect()
{
	Rect aCard = GetAugmentCardRect(0);
	int aY = aCard.mY + aCard.mHeight + 5 + 25 + 23;
	return Rect((BOARD_WIDTH - 400) / 2, aY, 400, 12);
}

Rect Board::GetAugmentHideButtonRect()
{
	Rect aTimer = GetAugmentTimerRect();
	const int aWidth = 154;
	const int aHeight = 46;
	return Rect((BOARD_WIDTH - aWidth) / 2, aTimer.mY + aTimer.mHeight + 13, aWidth, aHeight);
}

bool Board::CanRerollAugment(int theIndex)
{
    if (theIndex < 0 || theIndex >= 3 || mAugmentRerolled[theIndex])
        return false;
    const int aResult = mAugmentRerollResults[theIndex];
    if (aResult < 0 || aResult >= RollAugment::NUM_AUGMENTS)
        return false;
    if (IsAugmentEligible(aResult))
        return true;
    // Preserve at least one selectable card. Other invalid cards may still reroll.
    if (!IsAugmentEligible(mAugmentChoices[theIndex]))
        return true;
    for (int i = 0; i < 3; ++i)
        if (i != theIndex && IsAugmentEligible(mAugmentChoices[i]))
            return true;
    return false;
}

void Board::RerollAugment(int theIndex)
{
    if (!CanRerollAugment(theIndex))
    {
        mApp->PlaySample(Sexy::SOUND_BUZZER);
        DisplayAdvice(_S("Cannot reroll the last available Augment into a banned one."),
            MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_NONE);
        return;
    }
    const int aReplacement = mAugmentRerollResults[theIndex];
    if (gAugmentPlants[theIndex] != nullptr)
    {
        gAugmentPlants[theIndex]->Die();
        delete gAugmentPlants[theIndex];
        gAugmentPlants[theIndex] = nullptr;
    }
    mAugmentChoices[theIndex] = aReplacement;
    mAugmentRerolled[theIndex] = true;
    CreateAugmentPlants(mAugmentChoices);
    if (!mApp->mDeveloperDebugSession && mApp->mPlayerInfo != nullptr)
    {
        SexyString aKey = StrFormat(mAugmentStage == 2 ? _S("%d:%d:stage2") : _S("%d:%d"), (int)mApp->mGameMode, mLevel);
        WriteAugmentDraft(mApp->mPlayerInfo, aKey, EncodeAugmentDraft(mAugmentInitialChoices, mAugmentChoices,
            mAugmentSeen, mAugmentRerolled, mAugmentRerollResults));
    }
    mApp->PlaySample(Sexy::SOUND_SLOT_MACHINE); // existing reroll sound asset
    if (mRollTutorialStep == ROLL_TUT_AUGMENT_REROLL)
    {
        // New 30-second selection clock; the original entrance/gift/plant
        // animation clock is NOT modified. Other two cards keep animating.
        mAugmentSelectionStartTick = GetTickCount();
        AdvanceRollTutorial(ROLL_TUT_AUGMENT_REROLL);
    }
}

// The same commit path handles player picks, Fortune's Favor and future draft rounds.
// Keep UI/draft lifecycle in ChooseAugment: bonus picks must not close or rewrite the draft.
bool Board::CommitAugmentSelection(int theAugment)
{
    if (theAugment < 0 || theAugment >= RollAugment::NUM_AUGMENTS || HasAugment(theAugment))
        return false;
    const int aChosen = theAugment;
	// Persist discovery by stable ID, not by display name or array position. The
	// completion check is dynamic, so newly registered Augments extend Tactician.
	if (!mApp->mDeveloperDebugSession && mApp->mPlayerInfo && aChosen >= 0 && aChosen < RollAugment::NUM_AUGMENTS)
	{
		if (mApp->mPlayerInfo->MarkAugmentDiscovered(gAugmentDefs[aChosen].mId))
			mApp->mPlayerInfo->SaveDetails();
		bool aAllDiscovered = true;
		for (int i = 0; i < RollAugment::NUM_AUGMENTS; ++i)
		{
			if (!mApp->mPlayerInfo->HasDiscoveredAugment(gAugmentDefs[i].mId))
			{
				aAllDiscovered = false;
				break;
			}
		}
		if (aAllDiscovered)
		{
			mApp->GetAchievement(AchievementType::ACHIEVEMENT_TACTICIAN);
			mApp->mPlayerInfo->SaveDetails();
		}
	}
    ApplyAugment(theAugment);
    return HasAugment(theAugment);
}

void Board::ChooseAugment(int theIndex)
{
	if (theIndex < 0 || theIndex >= 3 || !IsAugmentEligible(mAugmentChoices[theIndex]))
    {
        mApp->PlaySample(Sexy::SOUND_BUZZER);
        return;
    }

	int aChosen = mAugmentChoices[theIndex];
	mAugmentChoosing = false;
    mAugmentSelectionPending = false;
    mAugmentSelectedIndex = -1;
    mAugmentSelectionStartTick = 0;
	mAugmentHidden = false;
	TutorialArrowRemove();
    PreserveChosenAugmentPreviewPlant(theIndex, aChosen);
    // Confirm a picked card with the game's existing reward chime, not a generic tap.
    mApp->PlaySample(Sexy::SOUND_CHIME);
    CommitAugmentSelection(aChosen);
    if (mRollTutorialStep == ROLL_TUT_AUGMENT_CHOOSE)
        SetRollTutorialStep(ROLL_TUT_FINISH);
	mApp->mIsFastMode = mAugmentRestoreFastMode;
}

bool Board::AugmentMouseDown(int theX, int theY, int theClickCount)
{
	if (theClickCount <= 0)
		return true;
    if (mAugmentSelectionPending)
        return true;
    if (mRollTutorialStep == ROLL_TUT_AUGMENT_REROLL)
    {
        for (int i = 0; i < 3; ++i)
            if (GetAugmentRerollButtonRect(i).Contains(theX, theY))
            {
                RerollAugment(i);
                return true;
            }
        mApp->PlaySample(Sexy::SOUND_BUZZER);
        return true;
    }
    // Keep the Augment UI visible until the first-run player has chosen a card.
    if (mRollTutorialStep == ROLL_TUT_AUGMENT_CHOOSE && GetAugmentHideButtonRect().Contains(theX, theY))
        return true;

	if (mAugmentHidden)
	{
		if (GetAugmentHideButtonRect().Contains(theX, theY))
		{
			mAugmentHidden = false;
			mApp->PlaySample(Sexy::SOUND_TAP);
		}
		return true;
	}

	if (GetAugmentHideButtonRect().Contains(theX, theY))
	{
		mAugmentHidden = true;
		TutorialArrowRemove();
		mAugmentHover = -1;
		mApp->PlaySample(Sexy::SOUND_TAP);
		return true;
	}

	for (int i = 0; i < 3; i++)
	{
		if (GetAugmentRerollButtonRect(i).Contains(theX, theY))
		{
			RerollAugment(i);
			return true;
		}
	}

	for (int i = 0; i < 3; i++)
	{
		Rect aCard = GetAugmentCardRect(i);
		if (mAugmentTimer < 18)
			aCard.mY += 18 - mAugmentTimer;
        if (mAugmentChoices[i] >= 0 && aCard.Contains(theX, theY))
        {
            if (!IsAugmentEligible(mAugmentChoices[i]))
            {
                const SeedType aBlockedPlant = gAugmentDefs[mAugmentChoices[i]].mSeedType;
                if (aBlockedPlant != SeedType::SEED_NONE && IsSeedBannedFromShop(aBlockedPlant, SeedType::SEED_NONE))
                    DisplayAdvice(StrFormat(_S("%s is banned. This Augment cannot be selected."),
                        Plant::GetNameString(aBlockedPlant).c_str()),
                        MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_NONE);
                else
                    DisplayAdvice(_S("This Augment is currently unavailable."),
                        MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_NONE);
                mApp->PlaySample(Sexy::SOUND_BUZZER);
                return true;
            }
            if (!mAugmentSelectionPending)
            {
                mAugmentSelectionPending = true;
                mAugmentSelectedIndex = i;
                mAugmentSelectionStartTick = GetTickCount();
                mApp->PlaySample(Sexy::SOUND_TAP2); // existing card-selection click
            }
            return true;
        }
	}
	return true;
}

// A zombie kill can award one existing Regular Sun or a reserved Pool packet.
// All odds are conditional on an actual kill, never on a Zombie leaving the board.
void Board::TryDropZombieSpoils(int theX, int theY)
{
    if (!IsRollSeedBank() || !HasAugment(RollAugment::AUGMENT_SPOILS_OF_WAR) || Rand(100) >= 25)
        return;

    SeedType rewardSeed = SeedType::SEED_NONE;
    if (Rand(100) >= 85)
    {
        const int tier = RollAvailableTier(); // uses adaptive finite-Pool odds
        if (tier > 0)
        {
            int total = 0;
            for (int i = 0; i < mNumChosenPlants; ++i)
            {
                const ChosenPlant& choice = mChosenPlants[i];
                if (GetPlantTier(choice.mSeedType) == tier &&
                    !IsSeedBannedFromShop(choice.mSeedType, choice.mImitaterType))
                    total += max(0, mSeedPoolRemaining[(int)choice.mSeedType]);
            }
            if (total > 0)
            {
                int pick = Rand(total);
                for (int i = 0; i < mNumChosenPlants; ++i)
                {
                    const ChosenPlant& choice = mChosenPlants[i];
                    if (GetPlantTier(choice.mSeedType) != tier ||
                        IsSeedBannedFromShop(choice.mSeedType, choice.mImitaterType)) continue;
                    pick -= max(0, mSeedPoolRemaining[(int)choice.mSeedType]);
                    if (pick < 0) { rewardSeed = choice.mSeedType; break; }
                }
            }
        }
    }

    if (rewardSeed != SeedType::SEED_NONE && ReservePlantPoolCard(rewardSeed))
    {
        Coin* packet = AddCoin(theX, theY, CoinType::COIN_USABLE_SEED_PACKET, CoinMotion::COIN_MOTION_COIN);
        if (packet != nullptr)
        {
            packet->mUsableSeedType = rewardSeed;
            packet->mUsableStarLevel = 1;
            packet->mUsablePoolCount = 1; // ownership transfers to Bench on collection
            packet->mPersistent = true;
            return;
        }
        ReturnPlantPoolCard(rewardSeed);
    }
    // An empty Pool (or exhausted Coin capacity) must never create a Plant.
    DropRegularSun(1, theX, theY);
}

// Puts usable seed packets on the lawn. theFixedSeed = SEED_NONE picks random Chosen Plants.
void Board::QueueAugmentSeedReward(SeedType theSeed, int theX, int theY)
{
    if (!IsRollSeedBank() || theSeed < SeedType::SEED_PEASHOOTER || theSeed >= NUM_SEEDS_IN_CHOOSER)
        return;
    // Granted copies do NOT consume Shop Pool stock, and are not returned to it later.
    Coin* aPacket = nullptr;
    if (mCoins.mSize < mCoins.mMaxSize)
        aPacket = AddCoin(theX < 0 ? RandRangeInt(100, 649) : theX,
            theY < 0 ? RandRangeInt(150, 460) : theY,
            CoinType::COIN_USABLE_SEED_PACKET, CoinMotion::COIN_MOTION_COIN);
    if (aPacket)
    {
        aPacket->mUsableSeedType = theSeed;
        aPacket->mUsableStarLevel = 1;
        aPacket->mUsablePoolCount = 0;
        aPacket->mPersistent = true;
    }
    else ++mAugmentPendingPackets[(int)theSeed]; // Never discard when Coin capacity is full.
}

void Board::FlushAugmentSeedRewards()
{
    while (mPendingMiniCherryBombs > 0 && mCoins.mSize < mCoins.mMaxSize)
    {
        Coin* coin = AddCoin(RandRangeInt(100, 649), RandRangeInt(150, 460),
            CoinType::COIN_USABLE_SEED_PACKET, CoinMotion::COIN_MOTION_COIN);
        if (coin == nullptr) return;
        coin->mUsableSeedType = SeedType::SEED_CHERRYBOMB;
        coin->mUsableStarLevel = 0; // A Mini Cherry, not a normal Bench packet.
        coin->mUsablePoolCount = 0;
        coin->mPersistent = true;
        --mPendingMiniCherryBombs;
    }
    if (mSeedDraftPendingReward != SeedType::SEED_NONE)
    {
        if (mCoins.mSize >= mCoins.mMaxSize) return;
        Coin* coin = AddCoin(BOARD_WIDTH / 2, BOARD_HEIGHT / 2,
            CoinType::COIN_USABLE_SEED_PACKET, CoinMotion::COIN_MOTION_COIN);
        if (coin == nullptr) return;
        coin->mUsableSeedType = mSeedDraftPendingReward;
        coin->mUsableStarLevel = 1;
        coin->mUsablePoolCount = 1; // Transfer of the already-reserved copy.
        coin->mPersistent = true;
        mSeedDraftPendingReward = SeedType::SEED_NONE;
    }
    // Coins and Seed Packets share one bounded Coin array. Deferred rewards remain saved.
    while (mAugmentPendingGoldCoins > 0)
    {
        if (mCoins.mSize >= mCoins.mMaxSize) return;
        AddCoin(RandRangeInt(100, 649), RandRangeInt(150, 460),
            CoinType::COIN_GOLD, CoinMotion::COIN_MOTION_COIN);
        --mAugmentPendingGoldCoins;
    }
    while (mAugmentPendingSilverCoins > 0)
    {
        if (mCoins.mSize >= mCoins.mMaxSize) return;
        AddCoin(RandRangeInt(100, 649), RandRangeInt(150, 460),
            CoinType::COIN_SILVER, CoinMotion::COIN_MOTION_COIN);
        --mAugmentPendingSilverCoins;
    }
    for (int seed = 0; seed < NUM_SEEDS_IN_CHOOSER; ++seed)
        while (mAugmentPendingPackets[seed] > 0)
        {
            if (mCoins.mSize >= mCoins.mMaxSize) return;
            Coin* aPacket = AddCoin(RandRangeInt(100, 649), RandRangeInt(150, 460),
                CoinType::COIN_USABLE_SEED_PACKET, CoinMotion::COIN_MOTION_COIN);
            aPacket->mUsableSeedType = (SeedType)seed;
            aPacket->mUsableStarLevel = 1;
            aPacket->mUsablePoolCount = 0;
            aPacket->mPersistent = true;
            --mAugmentPendingPackets[seed];
        }
}

void Board::IgniteAugmentCross(int theCol, int theRow)
{
    if (!IsRollSeedBank() || !HasAugment(RollAugment::AUGMENT_JALAPENO_CROSSFIRE)) return;
    for (int y = 0; y < (StageHas6Rows() ? 6 : 5); ++y)
    {
        if (y == theRow) continue;
        Zombie* z = nullptr;
        while (IterateZombies(z))
        {
            if (z->mRow == y && !z->IsDeadOrDying() && !z->mMindControlled &&
                PixelToGridXKeepOnBoard((int)z->mPosX + 40, z->mY) == theCol && z->EffectedByDamage(18U))
            {
                z->RemoveColdEffects();
                z->ApplyBurn();
            }
        }
    }
    for (int x = 0; x < MAX_GRID_SIZE_X; ++x)
        for (int y = 0; y < (StageHas6Rows() ? 6 : 5); ++y)
        {
            if (x != theCol && y != theRow) continue;
            mAugmentFireTicks[x][y] = 500;
            if (mApp->ReanimationTryToGet(mAugmentFireReanim[x][y])) continue;
            Reanimation* fire = mApp->AddReanimation(GridToPixelX(x, y) + 15,
                GridToPixelY(x, y) + 5, MakeRenderOrder(RenderLayer::RENDER_LAYER_PARTICLE, y, 1),
                ReanimationType::REANIM_JALAPENO_FIRE);
            if (fire)
            {
                fire->SetFramesForLayer("anim_flame");
                fire->mLoopType = ReanimLoopType::REANIM_LOOP;
                mAugmentFireReanim[x][y] = mApp->ReanimationGetID(fire);
            }
        }
}

void Board::UpdateAugmentFire()
{
    for (int x = 0; x < MAX_GRID_SIZE_X; ++x)
        for (int y = 0; y < MAX_GRID_SIZE_Y; ++y)
        {
            if (mAugmentFireTicks[x][y] <= 0) continue;
            if (--mAugmentFireTicks[x][y] % 100 == 0)
            {
                Zombie* z = nullptr;
                while (IterateZombies(z))
                    if (!z->IsDeadOrDying() && !z->mMindControlled && z->mRow == y &&
                        PixelToGridXKeepOnBoard((int)z->mPosX + 40, z->mY) == x && z->EffectedByDamage(2U))
                        z->TakeDamage(40, 2U);
            }
            if (mAugmentFireTicks[x][y] == 0)
            {
                Reanimation* fire = mApp->ReanimationTryToGet(mAugmentFireReanim[x][y]);
                if (fire) fire->ReanimationDie();
                mAugmentFireReanim[x][y] = ReanimationID::REANIMATIONID_NULL;
            }
        }
}

void Board::TryDropPlanternBonus(Zombie* theZombie)
{
    if (!IsRollSeedBank() || !HasAugment(RollAugment::AUGMENT_PLANTERN_GOLD) ||
        !theZombie || theZombie->mMindControlled) return;
    const int x = PixelToGridXKeepOnBoard((int)theZombie->mPosX + 40, theZombie->mY);
    const int y = theZombie->mRow;
    Plant* p = nullptr;
    while (IteratePlants(p))
    {
        if (p->mDead || p->mSquished || p->mSeedType != SeedType::SEED_PLANTERN) continue;
        // Same diamond-cut 7x5 shape used by ClearFogAroundPlant(size = 4).
        const int dx = abs(x - p->mPlantCol), dy = abs(y - p->mRow);
        if (dx > 3 || dy > 2 || dx + dy == 5) continue;
        Rect r = theZombie->GetZombieRect();
        const CoinType bonusType = Rand(10) == 0 ? CoinType::COIN_GOLD : CoinType::COIN_SILVER;
        if (mCoins.mSize >= mCoins.mMaxSize)
        {
            if (bonusType == CoinType::COIN_GOLD) ++mAugmentPendingGoldCoins;
            else ++mAugmentPendingSilverCoins;
        }
        else AddCoin(r.mX + r.mWidth / 2, r.mY + r.mHeight / 4, bonusType,
            CoinMotion::COIN_MOTION_COIN);
        return; // One extra Coin regardless of overlapping Planterns.
    }
}

void Board::GiveUsableSeeds(int theCount, SeedType theFixedSeed, bool thePersistent)
{
    for (int i = 0; i < theCount; i++)
    {
        // Fixed-plant Augment rewards are minted independently of the finite Shop Pool.
        if (IsRollSeedBank() && theFixedSeed != SeedType::SEED_NONE)
        {
            if (theFixedSeed >= SeedType::SEED_PEASHOOTER && theFixedSeed < NUM_SEEDS_IN_CHOOSER &&
                mSeedDraftExcluded[(int)theFixedSeed]) continue;
            QueueAugmentSeedReward(theFixedSeed);
            continue;
        }
		SeedType aSeedType = theFixedSeed;
		if (aSeedType == SeedType::SEED_NONE)
		{
			            int tier = RollAvailableTier();
            if (tier == 0) break;
            int total = 0;
            for (int k = 0; k < mNumChosenPlants; ++k)
            {
                const ChosenPlant& cp = mChosenPlants[k];
                if (GetPlantTier(cp.mSeedType) == tier && !IsSeedBannedFromShop(cp.mSeedType, cp.mImitaterType))
                    total += max(0, mSeedPoolRemaining[(int)cp.mSeedType]);
            }
            if (total == 0) break;
            int picked = Rand(total);
            for (int k = 0; k < mNumChosenPlants; ++k)
            {
                const ChosenPlant& cp = mChosenPlants[k];
                if (GetPlantTier(cp.mSeedType) != tier || IsSeedBannedFromShop(cp.mSeedType, cp.mImitaterType)) continue;
                picked -= max(0, mSeedPoolRemaining[(int)cp.mSeedType]);
                if (picked < 0) { aSeedType = cp.mSeedType; break; }
            }
		}

        if (IsRollSeedBank() && (aSeedType < SeedType::SEED_PEASHOOTER || aSeedType >= NUM_SEEDS_IN_CHOOSER ||
            mSeedPoolMaximum[(int)aSeedType] <= 0))
            continue;
        if (IsRollSeedBank() && !ReservePlantPoolCard(aSeedType))
            continue;
        Coin* aCoin = AddCoin(RandRangeInt(100, 649), RandRangeInt(150, 460), CoinType::COIN_USABLE_SEED_PACKET, CoinMotion::COIN_MOTION_COIN);
        if (aCoin == nullptr)
        {
            if (IsRollSeedBank()) ReturnPlantPoolCard(aSeedType);
            break;
        }
        aCoin->mUsableSeedType = aSeedType;
        aCoin->mUsablePoolCount = IsRollSeedBank() ? 1 : 0;
		aCoin->mPersistent = thePersistent || IsRollSeedBank();
	}
}

void Board::CheckFinalReserves()
{
    if (!HasAugment(RollAugment::AUGMENT_FINAL_RESERVES) || mFinalReservesClaimed)
        return;
    bool aFoundMower = false;
    LawnMower* aMower = nullptr;
    while (IterateLawnMowers(aMower))
    {
        if (aMower->mMowerState == LawnMowerState::MOWER_SQUISHED)
            continue;
        aFoundMower = true;
        if (aMower->mMowerState != LawnMowerState::MOWER_TRIGGERED)
            return;
    }
    if (!aFoundMower)
        return;
    mFinalReservesClaimed = true;
    DropRegularSun(32);
    GrantFreeRolls(12);
}

void Board::UpdateInfiniteFumesLaunchRates()
{
    const int aBaseRate = GetPlantDefinition(SeedType::SEED_FUMESHROOM).mLaunchRate;
    const bool aInfiniteFumesActive = IsRollSeedBank() && HasAugment(RollAugment::AUGMENT_FUMESHROOM_INFINITE);
    const int aTargetRate = aInfiniteFumesActive ? (aBaseRate * 125) / 100 : aBaseRate;

    Plant* aPlant = nullptr;
    while (IteratePlants(aPlant))
    {
        if (aPlant->mDead || aPlant->mSeedType != SeedType::SEED_FUMESHROOM || aPlant->mLaunchRate == aTargetRate)
            continue;

        const int aOldRate = aPlant->mLaunchRate;
        if (aOldRate > 0 && aPlant->mLaunchCounter > 0)
        {
            // Preserve the fraction of the current firing cycle when the interval changes.
            int aScaledCounter = (aPlant->mLaunchCounter * aTargetRate + aOldRate / 2) / aOldRate;
            if (aScaledCounter > aTargetRate)
                aScaledCounter = aTargetRate;
            aPlant->mLaunchCounter = aScaledCounter;
        }
        aPlant->mLaunchRate = aTargetRate;
    }
}

void Board::ApplyAugment(int theAugment)
{
    if (theAugment < 0 || theAugment >= RollAugment::NUM_AUGMENTS || HasAugment(theAugment))
        return;
    if (mActiveAugment == RollAugment::AUGMENT_NONE)
        mActiveAugment = theAugment;
    mEnabledAugments[theAugment] = true;
    mActiveAugmentHoverStartTick = 0;
    mActiveAugmentTooltipVisible = false;
    if (theAugment == RollAugment::AUGMENT_TALLNUT_ENDURING)
    {
        Plant* p = nullptr;
        while (IteratePlants(p))
            if (!p->mDead && p->mSeedType == SeedType::SEED_TALLNUT)
            {
                p->mAugmentTallObservedHP = p->mPlantHealth;
                p->mAugmentTallSafeTicks = 0;
            }
    }
    UpdateInfiniteFumesLaunchRates();

	// Plant Augments, including Cherry Jackpot, grant one packet unless they have a custom multi-packet reward.
	// Preserve Mine Stash, Puff Patrol, and Budget Peas' existing multi-packet rewards.
	const AugmentDef& aAugmentDef = gAugmentDefs[theAugment];
	if (aAugmentDef.mSeedType != SeedType::SEED_NONE && theAugment != RollAugment::AUGMENT_POTATOMINE && theAugment != RollAugment::AUGMENT_PUFF_PATROL && theAugment != RollAugment::AUGMENT_PEASHOOTER)
	{
		GiveUsableSeeds(1, aAugmentDef.mSeedType);
	}

	switch (theAugment)
	{
	case RollAugment::AUGMENT_WALLNUT:
	{
		Plant* aPlant = nullptr;
		while (IteratePlants(aPlant))
		{
			if (!aPlant->mDead && aPlant->mSeedType == SeedType::SEED_WALLNUT)
			{
				aPlant->mPlantHealth /= 2;
				aPlant->mPlantMaxHealth /= 2;
			}
		}
		break;
	}
	case RollAugment::AUGMENT_POTATOMINE:
	{
		GiveUsableSeeds(3, SeedType::SEED_POTATOMINE, true);
		bool aShopHasPotato = false;
		for (int i = 0; i < mSeedBank->mNumPackets; i++)
		{
			aShopHasPotato |= IsSeedBannedFromShop(mSeedBank->mSeedPackets[i].mPacketType, mSeedBank->mSeedPackets[i].mImitaterType);
		}
		if (aShopHasPotato && !mShopWasClosed)
		{
			mSeedBank->RollAllSlots(false, false);
			mApp->PlaySample(Sexy::SOUND_SLOT_MACHINE);
		}
		break;
	}
    case RollAugment::AUGMENT_SEED_SURPRISE_II:
        GiveUsableSeeds(5, SeedType::SEED_NONE, true);
        break;
	case RollAugment::AUGMENT_FIVE_SEEDS:
		GiveUsableSeeds(3, SeedType::SEED_NONE, true);
		break;
	case RollAugment::AUGMENT_GARDEN_PACK:
	{
		int aPool[NUM_SEEDS_IN_CHOOSER];
		int aPoolCount = 0;
		for (int i = 0; i < mNumChosenPlants; ++i)
		{
			const ChosenPlant& aPlant = mChosenPlants[i];
			SeedType aValidationType = aPlant.mSeedType == SeedType::SEED_IMITATER ? aPlant.mImitaterType : aPlant.mSeedType;
			if (aValidationType != SeedType::SEED_NONE && aPlant.mSeedType != SeedType::SEED_NONE &&
				!IsSeedBannedFromShop(aPlant.mSeedType, aPlant.mImitaterType) && PlantingRequirementsMet(aValidationType))
				aPool[aPoolCount++] = i;
		}

		// Remember every selected tile separately. Some Instant Use plants disappear
		// immediately after AddPlant, so checking only the current lawn can otherwise
		// accidentally select the same tile more than once in this Garden Pack.
		int aUsedX[8];
		int aUsedY[8];
		int aUsedCount = 0;
		for (int n = 0; n < 8 && aPoolCount > 0; ++n)
		{
			// Shuffle candidate order so a plant that cannot fit here does not waste
			// one of the eight placements when a different plant can still be planted.
			int aOrder[NUM_SEEDS_IN_CHOOSER];
			for (int i = 0; i < aPoolCount; ++i) aOrder[i] = i;
			for (int i = aPoolCount - 1; i > 0; --i)
			{
				int j = Rand(i + 1);
				int aTemp = aOrder[i]; aOrder[i] = aOrder[j]; aOrder[j] = aTemp;
			}

			bool aPlanted = false;
			for (int aTry = 0; aTry < aPoolCount && !aPlanted; ++aTry)
			{
				const ChosenPlant& aChosen = mChosenPlants[aPool[aOrder[aTry]]];
				SeedType aValidationType = aChosen.mSeedType == SeedType::SEED_IMITATER ? aChosen.mImitaterType : aChosen.mSeedType;
				if (aValidationType == SeedType::SEED_NONE || !PlantingRequirementsMet(aValidationType))
					continue;
				int aCellsX[MAX_GRID_SIZE_X * MAX_GRID_SIZE_Y];
				int aCellsY[MAX_GRID_SIZE_X * MAX_GRID_SIZE_Y];
				int aCellCount = 0;
				for (int x = 0; x < MAX_GRID_SIZE_X; ++x)
				{
					for (int y = 0; y < MAX_GRID_SIZE_Y; ++y)
					{
						bool aAlreadyUsed = false;
						for (int u = 0; u < aUsedCount; ++u)
							if (aUsedX[u] == x && aUsedY[u] == y) { aAlreadyUsed = true; break; }
						if (!aAlreadyUsed && CanPlantAt(x, y, aValidationType) == PlantingReason::PLANTING_OK)
						{
							aCellsX[aCellCount] = x;
							aCellsY[aCellCount++] = y;
						}
					}
				}
				if (aCellCount <= 0)
					continue;
				int aCell = Rand(aCellCount);
				int x = aCellsX[aCell], y = aCellsY[aCell];
                // An Augment-created plant is NOT an unlimited free copy.
                if (!ReservePlantPoolCard(aChosen.mSeedType))
                    continue;
                Plant* aCreated = AddPlant(x, y, aChosen.mSeedType, aChosen.mImitaterType);
                if (aCreated == nullptr)
                {
                    ReturnPlantPoolCard(aChosen.mSeedType);
                    continue;
                }
                aCreated->mRollPoolCardCount = 1;
                aUsedX[aUsedCount] = x; aUsedY[aUsedCount++] = y;
                aPlanted = true;
			}
			if (!aPlanted)
				break; // no remaining plant can be safely placed on any unused cell
		}
		break;
	}

    case RollAugment::AUGMENT_SUN_BOON_II:
        DropRegularSun(22);
        break;
	case RollAugment::AUGMENT_SUN_BOON:
		DropRegularSun(10);
		break;
	case RollAugment::AUGMENT_LUCKY_ROLLS:
        // Cash Crops is enforced by the Shop purchase path.
        break;
    case RollAugment::AUGMENT_PEASHOOTER:
        GiveUsableSeeds(5, SeedType::SEED_PEASHOOTER, true);
		break;
	case RollAugment::AUGMENT_SHOP_HOLIDAY:
		CancelHeldBankPlant();
		mShopWasClosed = true;
		mShopClosedWaves = 3;
		for (int i = 0; i < mSeedBank->mNumPackets; i++)
		{
			mSeedBank->mSeedPackets[i].EmptySlot();
		}
		break;
	case RollAugment::AUGMENT_PUFF_PATROL:
        GiveUsableSeeds(1, SeedType::SEED_PUFFSHROOM);
        break;
    case RollAugment::AUGMENT_BALANCED_BUDGET:
        DropRegularSun(2);
        mBalancedBudgetWavesRemaining = 3;
        break;
    case RollAugment::AUGMENT_FINAL_RESERVES:
        CheckFinalReserves();
        break;
    case RollAugment::AUGMENT_HOLOGRAM:
        mHologramEnabled = true;
        mHologramLastPlant = SeedType::SEED_NONE;
        mHologramCooldown = 0;
        break;
	case RollAugment::AUGMENT_TOUGH_PLANTS:
	{
		Plant* aPlant = nullptr;
		while (IteratePlants(aPlant))
		{
			if (!aPlant->mDead)
			{
				aPlant->mPlantHealth = aPlant->mPlantHealth * 120 / 100;
				aPlant->mPlantMaxHealth = aPlant->mPlantMaxHealth * 120 / 100;
			}
		}
		break;
	}
    case RollAugment::AUGMENT_GARDEN_GLOVE:
        mGardenGloveEnabled = true;
        break;
    case RollAugment::AUGMENT_GRAVE_HARVEST_II:
        for (int g = 0; g < 4; ++g) SpawnGraveHarvest();
        break;
    case RollAugment::AUGMENT_SEED_DRAFT:
        StartSeedDraft();
        break;
    case RollAugment::AUGMENT_FORTUNES_FAVOR_II:
    case RollAugment::AUGMENT_FORTUNES_FAVOR:
    {
        DropRegularSun(theAugment == RollAugment::AUGMENT_FORTUNES_FAVOR_II ? 6 : 3);
        int choices[RollAugment::NUM_AUGMENTS];
        int count = 0;
        for (int i = 0; i < RollAugment::NUM_AUGMENTS; ++i)
            if (i != RollAugment::AUGMENT_FORTUNES_FAVOR && i != RollAugment::AUGMENT_FORTUNES_FAVOR_II && !HasAugment(i) && IsAugmentEligible(i))
                choices[count++] = i;
        if (count > 0)
        {
            // A companion Draft entry stores the *bonus* result by stable ID.
            // Retry/replay an unfinished level reuses it. Clearing a won draft
            // also clears this companion key. Debug never reads or writes it.
            const SexyString aKey = StrFormat(_S("%d:%d:fortune:%d"), (int)mApp->mGameMode, mLevel, theAugment == RollAugment::AUGMENT_FORTUNES_FAVOR_II ? 2 : 0);
            int aResult = -1;
            SexyString aStoredId;
            if (!mApp->mDeveloperDebugSession && mApp->mPlayerInfo != nullptr &&
                ReadAugmentDraft(mApp->mPlayerInfo, aKey, aStoredId))
            {
                const int aSaved = FindAugmentIndexById(aStoredId);
                for (int i = 0; i < count; ++i)
                {
                    if (choices[i] == aSaved)
                    {
                        aResult = aSaved;
                        break;
                    }
                }
                // Do not reroll a saved but now ineligible result: that would
                // permit manipulating Chosen Plants to fish for a new reward.
                if (aResult == -1) break;
            }
            else
            {
                aResult = choices[Rand(count)];
                if (!mApp->mDeveloperDebugSession && mApp->mPlayerInfo != nullptr)
                    WriteAugmentDraft(mApp->mPlayerInfo, aKey, gAugmentDefs[aResult].mId);
            }
            CommitAugmentSelection(aResult);
        }
        break;
    }

	default:
		break;
	}

	// The Augments button now shows both primary and bonus augments; no popup text.
}

// These draft picks are independent of the Augment card draft. Persist the
// actual three SeedType IDs so restarting an unfinished level cannot reroll them.
void Board::QueueMiniCherryBomb(int theX, int theY)
{
    Coin* coin = mCoins.mSize < mCoins.mMaxSize ?
        AddCoin(theX, theY, CoinType::COIN_USABLE_SEED_PACKET, CoinMotion::COIN_MOTION_COIN) : nullptr;
    if (coin == nullptr)
    {
        ++mPendingMiniCherryBombs;
        return;
    }
    coin->mUsableSeedType = SeedType::SEED_CHERRYBOMB;
    coin->mUsableStarLevel = 0;
    coin->mUsablePoolCount = 0;
    coin->mPersistent = true;
}

static const unsigned int SEED_DRAFT_TIME_MS = 15000U;

void Board::StartSeedDraft()
{
    if (!IsRollSeedBank() || mSeedDraftChoosing) return;
    for (int i = 0; i < 3; ++i) mSeedDraftChoices[i] = SeedType::SEED_NONE;
    mSeedDraftCopiesReserved = false;
    const SexyString key = StrFormat(_S("%d:%d:seed_draft"), (int)mApp->mGameMode, mLevel);
    SexyString saved;
    int ids[3] = { -1, -1, -1 };
    int fixedAutoChoice = -1;
    const bool haveSaved = !mApp->mDeveloperDebugSession &&
        ReadAugmentDraft(mApp->mPlayerInfo, key, saved);
    const int parsed = haveSaved ? sexysscanf(saved.c_str(), _S("%d,%d,%d,%d"),
        &ids[0], &ids[1], &ids[2], &fixedAutoChoice) : 0;
    bool restored = parsed >= 3;
    if (restored)
    {
        // Saved choices can become impossible after different Shop purchases on replay.
        // Preserve a valid immutable trio; otherwise choose from available stock.
        for (int i = 0; i < 3; ++i)
        {
            if (ids[i] < (int)SeedType::SEED_PEASHOOTER || ids[i] >= NUM_SEEDS_IN_CHOOSER ||
                mSeedPoolRemaining[ids[i]] <= 0 || IsSeedBannedFromShop((SeedType)ids[i], SeedType::SEED_NONE))
                restored = false;
            for (int j = 0; j < i; ++j)
                if (ids[i] == ids[j]) restored = false;
        }
    }
    if (restored)
        for (int i = 0; i < 3; ++i) mSeedDraftChoices[i] = (SeedType)ids[i];
    else
    {
        SeedType pool[NUM_SEEDS_IN_CHOOSER];
        int count = 0;
        for (int i = 0; i < mNumChosenPlants; ++i)
        {
            const SeedType plant = mChosenPlants[i].mSeedType;
            if (plant < SeedType::SEED_PEASHOOTER || plant >= NUM_SEEDS_IN_CHOOSER ||
                mSeedPoolRemaining[(int)plant] <= 0 || IsSeedBannedFromShop(plant, mChosenPlants[i].mImitaterType))
                continue;
            bool repeated = false;
            for (int p = 0; p < count; ++p) if (pool[p] == plant) repeated = true;
            if (!repeated) pool[count++] = plant;
        }
        if (count < 3) return; // Do not present an incomplete draft or consume stock.
        for (int i = 0; i < 3; ++i)
        {
            const int pick = Rand(count);
            mSeedDraftChoices[i] = pool[pick];
            pool[pick] = pool[--count];
        }
    }
    // Choose once for automatic timeout, independently of gameplay speed.
    // Persist this hidden choice with the three visible options so restart/load
    // cannot be used to reroll an expired Seed Draft.
    mSeedDraftAutoChoiceIndex = restored && fixedAutoChoice >= 0 && fixedAutoChoice < 3 ?
        fixedAutoChoice : Rand(3);
    if ((!restored || parsed != 4 || fixedAutoChoice < 0 || fixedAutoChoice > 2) &&
        !mApp->mDeveloperDebugSession && mApp->mPlayerInfo != nullptr)
        WriteAugmentDraft(mApp->mPlayerInfo, key, StrFormat(_S("%d,%d,%d,%d"),
            (int)mSeedDraftChoices[0], (int)mSeedDraftChoices[1],
            (int)mSeedDraftChoices[2], mSeedDraftAutoChoiceIndex));
    // Actual Pool reservations, one per displayed option; the Shop cannot sell
    // or consume these reserved copies during the 15-second decision window.
    int reserved = 0;
    for (; reserved < 3 && ReservePlantPoolCard(mSeedDraftChoices[reserved]); ++reserved) {}
    if (reserved != 3)
    {
        for (int i = 0; i < reserved; ++i) ReturnPlantPoolCard(mSeedDraftChoices[i]);
        return;
    }
    mSeedDraftCopiesReserved = true;
    mSeedDraftChoosing = true;
    mSeedDraftRemainingMs = SEED_DRAFT_TIME_MS;
    mSeedDraftLastUpdateTick = GetTickCount();
}

void Board::UpdateSeedDraft()
{
    if (!mSeedDraftChoosing) return;
    const unsigned int now = GetTickCount();
    const unsigned int elapsed = now - mSeedDraftLastUpdateTick;
    mSeedDraftLastUpdateTick = now;
    if (mPaused || mAugmentChoosing || mApp->GetDialogCount() > 0 ||
        mApp->mGameScene != GameScenes::SCENE_PLAYING)
        return;
    if (elapsed >= mSeedDraftRemainingMs)
    {
        mSeedDraftRemainingMs = 0;
        ChooseSeedDraft(mSeedDraftAutoChoiceIndex);
    }
    else mSeedDraftRemainingMs -= elapsed;
}

void Board::ChooseSeedDraft(int theIndex)
{
    if (!mSeedDraftChoosing || !mSeedDraftCopiesReserved || theIndex < 0 || theIndex >= 3 ||
        mSeedDraftChoices[theIndex] == SeedType::SEED_NONE) return;
    const SeedType chosen = mSeedDraftChoices[theIndex];
    mSeedDraftChoosing = false;
    mSeedDraftCopiesReserved = false;
    mSeedDraftRemainingMs = 0;
    for (int i = 0; i < 3; ++i)
    {
        const SeedType plant = mSeedDraftChoices[i];
        if (i != theIndex && plant >= SeedType::SEED_PEASHOOTER && plant < NUM_SEEDS_IN_CHOOSER)
        {
            ReturnPlantPoolCard(plant); // Return unused reserved copy before excluding type.
            mSeedDraftExcluded[(int)plant] = true;
        }
    }
    // The chosen copy was already removed from the Pool at draft creation.
    if (!AddSeedToBench(chosen, 1, 1, SeedType::SEED_NONE,
        BOARD_WIDTH / 2, BOARD_HEIGHT / 2, 0))
    {
        // Bench full: release the same reserved copy as an on-lawn collectible.
        // If the Coin array is full, defer it without returning stock or minting.
        Coin* packet = mCoins.mSize < mCoins.mMaxSize ?
            AddCoin(BOARD_WIDTH / 2, BOARD_HEIGHT / 2,
                CoinType::COIN_USABLE_SEED_PACKET, CoinMotion::COIN_MOTION_COIN) : nullptr;
        if (packet)
        {
            packet->mUsableSeedType = chosen;
            packet->mUsableStarLevel = 1;
            packet->mUsablePoolCount = 1;
            packet->mPersistent = true;
        }
        else mSeedDraftPendingReward = chosen;
    }
    mApp->PlaySample(Sexy::SOUND_CHIME);
}

void Board::DrawSeedDraft(Graphics* g)
{
    if (!mSeedDraftChoosing) return;
    // Non-modal Seed Bank: no dark full-screen rectangle or input capture.
    const int x0 = BOARD_WIDTH / 2 - 155;
    const int y0 = BOARD_HEIGHT / 2 - 35;
    g->SetColor(Color(20, 32, 17, 210));
    g->FillRect(x0 - 16, y0 - 38, 3 * 105 + 20, SEED_PACKET_HEIGHT + 105);
    TodDrawString(g, _S("SEED DRAFT - CHOOSE ONE"), BOARD_WIDTH / 2,
        y0 - 14, FONT_DWARVENTODCRAFT18YELLOW, Color::White, DS_ALIGN_CENTER);
    for (int i = 0; i < 3; ++i)
    {
        if (mSeedDraftChoices[i] == SeedType::SEED_NONE) continue;
        const int x = x0 + i * 105;
        DrawSeedPacket(g, (float)x, (float)y0, mSeedDraftChoices[i], SeedType::SEED_NONE,
            0, 255, false, false, GetPlantTier(mSeedDraftChoices[i]));
        TodDrawStringWrapped(g, Plant::GetNameString(mSeedDraftChoices[i]),
            Rect(x - 19, y0 + SEED_PACKET_HEIGHT + 2, 88, 28), FONT_BRIANNETOD12,
            Color::White, DS_ALIGN_CENTER_VERTICAL_MIDDLE);
    }
    // Exactly below the temporary bank, render a real-time remaining-time bar.
    const int barX = x0 - 8;
    const int barY = y0 + SEED_PACKET_HEIGHT + 34;
    const int barW = 3 * 105 + 4;
    g->SetColor(Color(40, 40, 40, 220));
    g->FillRect(barX, barY, barW, 10);
    g->SetColor(Color(230, 192, 65));
    g->FillRect(barX + 1, barY + 1, max(0, (int)((barW - 2) * mSeedDraftRemainingMs / SEED_DRAFT_TIME_MS)), 8);
    TodDrawString(g, StrFormat(_S("%ds"), (int)((mSeedDraftRemainingMs + 999U) / 1000U)),
        BOARD_WIDTH / 2, barY + 27, FONT_BRIANNETOD12, Color::White, DS_ALIGN_CENTER);
}

void Board::TransformShiftingBench()
{
    if (!IsRollSeedBank() || mBenchMergeActive) return;
    // Defer the wave-triggered transformation until existing packet movement
    // and star-combination animations have finished. Never skip that wave.
    for (int i = 0; i < mBenchCount; ++i)
        if (mBenchPackets[i].mBenchMoving) return;
    mShiftingBenchTransformPending = false;
    if (mBenchCount <= 0) return;
    mShiftingBenchPendingBonusMerges = 0;
    // Compute pending pre-transformation merges so ordinary pre-existing pairs
    // are not rewarded by this Augment.
    SeedType baselineType[ROLL_BENCH_SLOTS], baselineImit[ROLL_BENCH_SLOTS];
    int baselineStar[ROLL_BENCH_SLOTS], baselineSize = 0, baselineMerges = 0;
    for (int i = 0; i < mBenchCount; ++i)
    {
        baselineType[baselineSize] = mBenchPackets[i].mPacketType;
        baselineImit[baselineSize] = mBenchPackets[i].mImitaterType;
        baselineStar[baselineSize] = mBenchPackets[i].mStarLevel;
        ++baselineSize;
        while (baselineSize >= 2 && baselineType[baselineSize-1] != SeedType::SEED_NONE &&
            baselineType[baselineSize-1] == baselineType[baselineSize-2] &&
            baselineImit[baselineSize-1] == baselineImit[baselineSize-2] &&
            baselineStar[baselineSize-1] == baselineStar[baselineSize-2] && baselineStar[baselineSize-1] < 3)
        { --baselineSize; ++baselineStar[baselineSize-1]; ++baselineMerges; }
    }
    // Every changed packet transfers its *entire* copy reservation. A 2-star
    // packet cannot become a different type unless that many copies are free.
    for (int i = 0; i < mBenchCount; ++i)
    {
        SeedPacket& packet = mBenchPackets[i];
        const SeedType oldType = packet.mPacketType;
        const int count = packet.mPoolCardCount;
        // A free Augment packet can have zero reserved Pool copies. Its replacement
        // must claim enough actual copies for its star level (1/2/4), not spawn
        // an unlimited untracked copy. Already-reserved packets keep their count.
        const int needed = count > 0 ? count : (1 << (max(1, min(3, packet.mStarLevel)) - 1));
        if (oldType < SeedType::SEED_PEASHOOTER || oldType >= NUM_SEEDS_IN_CHOOSER ||
            GetPlantTier(oldType) <= 0) continue;
        SeedType candidates[NUM_SEEDS_IN_CHOOSER];
        int num = 0;
        for (int n = 0; n < mNumChosenPlants; ++n)
        {
            const SeedType type = mChosenPlants[n].mSeedType;
            if (type < SeedType::SEED_PEASHOOTER || type >= NUM_SEEDS_IN_CHOOSER || type == oldType ||
                GetPlantTier(type) != GetPlantTier(oldType) ||
                mSeedPoolRemaining[(int)type] < needed ||
                IsSeedBannedFromShop(type, mChosenPlants[n].mImitaterType)) continue;
            bool duplicate = false;
            for (int c = 0; c < num; ++c) if (candidates[c] == type) duplicate = true;
            if (!duplicate) candidates[num++] = type;
        }
        if (!num) continue;
        const SeedType replacement = candidates[Rand(num)];
        // Pool reservation exchange is atomic with respect to the single game tick.
        ReturnPlantPoolCard(oldType, count);
        int reserved = 0;
        for (; reserved < needed && ReservePlantPoolCard(replacement); ++reserved) {}
        if (reserved != needed)
        {
            ReturnPlantPoolCard(replacement, reserved);
            for (int c = 0; c < count; ++c) ReservePlantPoolCard(oldType);
            continue;
        }
        packet.mPacketType = replacement;
        packet.mImitaterType = SeedType::SEED_NONE;
        packet.mPoolCardCount = needed; // exact replacement copy reservation
        // mStarLevel, mSunSpent and placement are preserved.
    }
    // Simulate exactly the adjacent-in-order merge rule before animation starts.
    // This counts *each* chain upgrade, not merely once per wave.
    SeedType stackType[ROLL_BENCH_SLOTS];
    int stackStar[ROLL_BENCH_SLOTS];
    SeedType stackImit[ROLL_BENCH_SLOTS];
    int size = 0, bonuses = 0;
    for (int i = 0; i < mBenchCount; ++i)
    {
        stackType[size] = mBenchPackets[i].mPacketType;
        stackStar[size] = mBenchPackets[i].mStarLevel;
        stackImit[size] = mBenchPackets[i].mImitaterType;
        ++size;
        while (size >= 2 && stackType[size - 1] != SeedType::SEED_NONE &&
            stackType[size - 1] == stackType[size - 2] && stackImit[size - 1] == stackImit[size - 2] &&
            stackStar[size - 1] == stackStar[size - 2] && stackStar[size - 1] < 3)
        {
            --size;
            ++stackStar[size - 1];
            ++bonuses;
        }
    }
    mShiftingBenchPendingBonusMerges = max(0, bonuses - baselineMerges);
    MergeBenchSeedsInOrder(); // Preserve the existing moving/merging animations.
}

// The selected Augment label and its hover hit box share one geometry calculation.
// The gallery uses only existing game art: stone buttons and the exact
// Almanac card/ground/plant/gift renderer used by the Augment chooser.
void Board::DrawActiveAugment(Graphics* g)
{
    if (!IsRollSeedBank() || mActiveAugment == RollAugment::AUGMENT_NONE)
        return;

    const int menuX = mMenuButton != nullptr ? mMenuButton->mX : BOARD_WIDTH - 160;
    const int menuY = mMenuButton != nullptr ? mMenuButton->mY : -10;
    const Rect button = GetAugmentGalleryButtonRect(menuX, menuY);
    const int mouseX = mApp->mWidgetManager->mLastMouseX - mX;
    const int mouseY = mApp->mWidgetManager->mLastMouseY - mY;

    // Same existing stone-button assets and exact dimensions/height as Menu.
    // Render the lower-case label separately with the game's smaller native
    // font; changing DrawStoneButton would change Menu and all other buttons.
    const bool hovered = button.Contains(mouseX, mouseY);
    DrawStoneButton(g, button.mX, button.mY, button.mWidth, button.mHeight,
        false, hovered, _S(""));
    if (FONT_DWARVENTODCRAFT15 != nullptr)
    {
        const int ascent = FONT_DWARVENTODCRAFT15->GetAscent();
        const int baseline = button.mY +
            (button.mHeight - ascent / 6 - 1 + ascent) / 2 - 4;
        TodDrawString(g, _S("augment"), button.mX + button.mWidth / 2,
            baseline, FONT_DWARVENTODCRAFT15,
            hovered ? Color(155, 235, 110) : Color(91, 175, 69), DS_ALIGN_CENTER);
    }

    // Keep rendering during Closing, including the original animated plant previews.
    // They are freed only after the last card has fully retracted.
    AdvanceAugmentGalleryAnimation(mAugmentGalleryOpen);
    if ((!mAugmentGalleryOpen && gAugmentGalleryProgressMs == 0) ||
        IMAGE_ALMANAC_PLANTCARD == nullptr || IMAGE_ALMANAC_GROUNDDAY == nullptr)
        return;

    int ids[RollAugment::NUM_AUGMENTS];
    int count = 0;
    for (int i = 0; i < RollAugment::NUM_AUGMENTS; ++i)
        if (HasAugment(i)) ids[count++] = i;
    const int pages = max(1, (count + 2) / 3);
    const int page = min(max(mAugmentGalleryPage, 0), pages - 1);
    const int timelineMs = gAugmentGalleryProgressMs;

    // Rightmost card is nearest the Augment button. Additional cards enter
    // to its left, one after another. Use the ORIGINAL card/ground/plant/gift
    // textures; only translate/fade those existing images during the entrance.
    for (int slot = 0; slot < 3 && page * 3 + slot < count; ++slot)
    {
        const int delayMs = slot * AUGMENT_GALLERY_STAGGER_MS;
        if (timelineMs <= delayMs)
            continue;
        const int localMs = timelineMs - delayMs;
        const float t = localMs >= AUGMENT_GALLERY_SLIDE_MS ? 1.0f :
            (float)localMs / (float)AUGMENT_GALLERY_SLIDE_MS;
        const float eased = 1.0f - (1.0f - t) * (1.0f - t);
        const int alpha = (int)(255.0f * t);

        const int id = ids[page * 3 + slot];
        const AugmentDef& def = gAugmentDefs[id];
        Rect rect = GetAugmentCardRect(slot);
        const int gap = 14;
        rect.mX = button.mX + button.mWidth - rect.mWidth -
            slot * (rect.mWidth + gap);
        rect.mY = button.mY + button.mHeight + 42;
        rect.mX += (int)((1.0f - eased) * 40.0f);

        const float scale = (float)rect.mWidth / (float)IMAGE_ALMANAC_PLANTCARD->GetWidth();
        Graphics cg(*g);
        cg.SetColorizeImages(true);
        cg.SetColor(Color(255, 255, 255, alpha));
        const int groundX = rect.mX + (int)(62 * scale);
        const int groundY = rect.mY + (int)(21 * scale);
        const int groundW = (int)(IMAGE_ALMANAC_GROUNDDAY->GetWidth() * scale);
        const int groundH = (int)(IMAGE_ALMANAC_GROUNDDAY->GetHeight() * scale);
        cg.DrawImage(IMAGE_ALMANAC_GROUNDDAY, groundX, groundY, groundW, groundH);

        Plant* preview = gGalleryAugmentPlants[id];
        if (def.mSeedType != SeedType::SEED_NONE && preview != nullptr)
        {
            preview->mX = rect.mX + (int)(119 * scale);
            preview->mY = rect.mY + (int)(54 * scale);
            if (preview->BeginDraw(&cg))
            {
                preview->Draw(&cg);
                preview->EndDraw(&cg);
            }
        }
        else if (IMAGE_PRESENT != nullptr)
        {
            const int presentX = groundX + (groundW - IMAGE_PRESENT->GetWidth()) / 2;
            const int presentY = groundY + (groundH - IMAGE_PRESENT->GetHeight()) / 2 +
                (int)(sinf((float)GetTickCount() * 0.008f + slot) * 4.0f);
            cg.DrawImage(IMAGE_PRESENT, presentX, presentY);
        }

        cg.DrawImage(IMAGE_ALMANAC_PLANTCARD, rect.mX, rect.mY, rect.mWidth, rect.mHeight);
        cg.SetColorizeImages(false);
        TodDrawStringWrapped(&cg, def.mName,
            Rect(rect.mX + (int)(18 * scale), rect.mY + (int)(184 * scale),
                 (int)(278 * scale), (int)(27 * scale)),
            FONT_DWARVENTODCRAFT18YELLOW, Color(255, 255, 255, alpha), DS_ALIGN_CENTER);
        TodDrawStringWrapped(&cg, def.mEffect,
            Rect(rect.mX + (int)(26 * scale), rect.mY + (int)(230 * scale),
                 (int)(258 * scale), (int)(224 * scale)),
            FONT_BRIANNETOD12, Color(40, 50, 90, alpha), DS_ALIGN_CENTER_VERTICAL_MIDDLE);
    }

    // Only the existing 'augment' button closes the gallery.
    if (mAugmentGalleryOpen && gAugmentGalleryProgressMs == AUGMENT_GALLERY_TOTAL_MS && pages > 1)
    {
        const Rect prev = GetAugmentGalleryPreviousRect();
        const Rect next = GetAugmentGalleryNextRect();
        DrawStoneButton(g, prev.mX, prev.mY, prev.mWidth, prev.mHeight,
            false, prev.Contains(mouseX, mouseY), _S("PREV"));
        DrawStoneButton(g, next.mX, next.mY, next.mWidth, next.mHeight,
            false, next.Contains(mouseX, mouseY), _S("NEXT"));
        TodDrawString(g, StrFormat(_S("%d / %d"), page + 1, pages), BOARD_WIDTH / 2,
            BOARD_HEIGHT - 28, FONT_BRIANNETOD12, Color(255, 230, 160), DS_ALIGN_CENTER);
    }
}

static void DrawAugmentTimerBar(Graphics* g, const Rect& theRect, int theRemainingMs)
{
	const int aDuration = 30000;
	int aFillWidth = theRemainingMs * theRect.mWidth / aDuration;
	if (aFillWidth < 0) aFillWidth = 0;
	if (aFillWidth > theRect.mWidth) aFillWidth = theRect.mWidth;
	g->SetColor(Color(37, 24, 17, 240));
	g->FillRect(theRect);
	if (aFillWidth > 0)
	{
		g->SetColor(Color(230, 170, 53, 255));
		g->FillRect(theRect.mX, theRect.mY, aFillWidth, theRect.mHeight);
	}
	g->SetColor(Color(245, 216, 152, 255));
	g->DrawRect(theRect);
}

void Board::DrawAugmentSelection(Graphics* g)
{
	if (!mAugmentChoosing)
		return;

    // Use a separate clock for the tutorial's post-reroll choice window.
    // The actual card entrance and animated gifts always use mAugmentTimer.
    unsigned int aElapsedMs = mRollTutorialStep == ROLL_TUT_AUGMENT_REROLL ? 0U :
        (GetTickCount() - (mRollTutorialStep == ROLL_TUT_AUGMENT_CHOOSE ?
            mAugmentSelectionStartTick : mAugmentStartTick));
    if (aElapsedMs > ROLL_AUGMENT_CHOICE_TIME_MS)
        aElapsedMs = ROLL_AUGMENT_CHOICE_TIME_MS;
    int aRemainingMs = (int)(ROLL_AUGMENT_CHOICE_TIME_MS - aElapsedMs);
    unsigned int aSelectionElapsedMs = mAugmentSelectionPending ? GetTickCount() - mAugmentSelectionStartTick : 0;
    if (aSelectionElapsedMs > ROLL_AUGMENT_SELECTION_ANIMATION_MS)
        aSelectionElapsedMs = ROLL_AUGMENT_SELECTION_ANIMATION_MS;

	// When temporarily hidden, reveal the lawn without resuming simulation.
	if (mAugmentHidden)
	{
		DrawAugmentTimerBar(g, GetAugmentTimerRect(), aRemainingMs);
		Rect aShowRect = GetAugmentHideButtonRect();
		int aMouseX = mApp->mWidgetManager->mLastMouseX - mX;
		int aMouseY = mApp->mWidgetManager->mLastMouseY - mY;
		DrawStoneButton(g, aShowRect.mX, aShowRect.mY, aShowRect.mWidth, aShowRect.mHeight, false, aShowRect.Contains(aMouseX, aMouseY), TodStringTranslate(_S("SHOW")));
		return;
	}

	g->SetColor(Color(0, 0, 0, 190));
	g->FillRect(0, 0, BOARD_WIDTH, BOARD_HEIGHT);
	TodDrawStringWrapped(g, _S("CHOOSE AN AUGMENT"), Rect(0, 24, BOARD_WIDTH, 30), FONT_HOUSEOFTERROR20, Color(213, 159, 43), DrawStringJustification::DS_ALIGN_CENTER);
    if (mRollTutorialStep == ROLL_TUT_AUGMENT_REROLL || mRollTutorialStep == ROLL_TUT_AUGMENT_CHOOSE)
        TodDrawStringWrapped(g, mRollTutorialStep == ROLL_TUT_AUGMENT_REROLL ?
            _S("TUTORIAL: Click ROLL under one card to see another Augment.") :
            _S("TUTORIAL: Click an Augment card to choose its upgrade."),
            Rect(30, 55, BOARD_WIDTH - 60, 22), FONT_BRIANNETOD12,
            Color(255, 235, 165), DrawStringJustification::DS_ALIGN_CENTER);

	int aFadeAlpha = mAugmentTimer >= 18 ? 255 : (mAugmentTimer * 255 / 18);
	if (aFadeAlpha < 0) aFadeAlpha = 0;
	for (int i = 0; i < 3; i++)
	{
		if (mAugmentChoices[i] < 0)
			continue;
        if (mAugmentSelectionPending && i != mAugmentSelectedIndex && aSelectionElapsedMs >= ROLL_AUGMENT_SELECTION_ANIMATION_MS)
            continue;

		const AugmentDef& aDef = gAugmentDefs[mAugmentChoices[i]];
		Rect aLayoutRect = GetAugmentCardRect(i);
		// Entrance: fade in and slide upward during the first 180 ms of real time.
		int aSlide = mAugmentTimer < 18 ? (18 - mAugmentTimer) : 0;
        int aCardAlpha = aFadeAlpha; // banned cards retain opacity; they are darkened instead
        if (mAugmentSelectionPending && i != mAugmentSelectedIndex)
        {
            float aProgress = (float)aSelectionElapsedMs / (float)ROLL_AUGMENT_SELECTION_ANIMATION_MS;
            if (aProgress > 1.0f) aProgress = 1.0f;
            aLayoutRect.mY += (int)(aProgress * 110.0f);
            aCardAlpha = (int)(255.0f * (1.0f - aProgress));
        }
		aLayoutRect.mY += aSlide;
		Rect aRect = aLayoutRect;
		if (i == mAugmentHover)
		{
			const int aExtraW = 10;
			const int aExtraH = 10;
			aRect.mX -= aExtraW / 2;
			aRect.mY -= aExtraH / 2;
			aRect.mWidth += aExtraW;
			aRect.mHeight += aExtraH;
		}
		float aScale = (float)aRect.mWidth / (float)IMAGE_ALMANAC_PLANTCARD->GetWidth();
		Graphics aCardGraphics = Graphics(*g);
		aCardGraphics.SetColorizeImages(true);
		aCardGraphics.SetColor(Color(255, 255, 255, aCardAlpha));

		Image* aGround = IMAGE_ALMANAC_GROUNDDAY;
		int aGroundX = aRect.mX + (int)(62 * aScale);
		int aGroundY = aRect.mY + (int)(21 * aScale);
		int aGroundW = (int)(aGround->GetWidth() * aScale);
		int aGroundH = (int)(aGround->GetHeight() * aScale);
		aCardGraphics.DrawImage(aGround, aGroundX, aGroundY, aGroundW, aGroundH);

		if (aDef.mSeedType != SeedType::SEED_NONE)
		{
			if (gAugmentPlants[i] != nullptr)
			{
				gAugmentPlants[i]->mX = aRect.mX + (int)(119 * aScale);
				gAugmentPlants[i]->mY = aRect.mY + (int)(54 * aScale);
				// BeginDraw translates the Graphics object by the plant's position.
				// Always balance it with EndDraw so the card frame, name and description
				// are drawn at their own card coordinates rather than shifted off-screen.
				if (gAugmentPlants[i]->BeginDraw(&aCardGraphics))
				{
					gAugmentPlants[i]->Draw(&aCardGraphics);
					gAugmentPlants[i]->EndDraw(&aCardGraphics);
				}
			}
		}
		else
		{
			float aBob = sinf(mAugmentTimer * 0.08f + i) * 4.0f;
			float aGiftX = aGroundX + (aGroundW - IMAGE_PRESENT->GetWidth()) / 2.0f;
			float aGiftY = aGroundY + (aGroundH - IMAGE_PRESENT->GetHeight()) / 2.0f + aBob;
			aCardGraphics.DrawImage(IMAGE_PRESENT, (int)aGiftX, (int)aGiftY);
		}

		aCardGraphics.SetColorizeImages(true);
		aCardGraphics.SetColor(Color(255, 255, 255, aCardAlpha));
		aCardGraphics.DrawImage(IMAGE_ALMANAC_PLANTCARD, aRect.mX, aRect.mY, aRect.mWidth, aRect.mHeight);
		aCardGraphics.SetColorizeImages(false);

		float aTextScale = (float)aLayoutRect.mWidth / (float)IMAGE_ALMANAC_PLANTCARD->GetWidth();
		Color aNameColor(255, 255, 255, aCardAlpha);
		TodDrawStringWrapped(&aCardGraphics, aDef.mName,
			Rect(aLayoutRect.mX + (int)(18 * aTextScale), aLayoutRect.mY + (int)(184 * aTextScale), (int)(278 * aTextScale), (int)(27 * aTextScale)),
			FONT_DWARVENTODCRAFT18YELLOW, aNameColor, DrawStringJustification::DS_ALIGN_CENTER);


        // Darken (do not fade out) disabled Plant Augments. Reuse the native
        // card artwork and draw a translucent BLACK overlay within card bounds.
        // The unavailable label remains bright and legible above the overlay.
        if (!IsAugmentEligible(mAugmentChoices[i]))
        {
            Graphics aShadeGraphics = Graphics(*g);
            aShadeGraphics.SetColorizeImages(false);
            aShadeGraphics.SetColor(Color(0, 0, 0, (aFadeAlpha * 120) / 255));
            aShadeGraphics.FillRect(aRect.mX, aRect.mY, aRect.mWidth, aRect.mHeight);
        }

		// Text keeps a fixed layout rectangle while the card artwork grows on hover, preventing reflow.
		Color aEffectColor(40, 50, 90, aCardAlpha);
		Rect aTextRect(aLayoutRect.mX + (int)(26 * aTextScale), aLayoutRect.mY + (int)(230 * aTextScale), (int)(258 * aTextScale), (int)(224 * aTextScale));
		TodDrawStringWrapped(&aCardGraphics, aDef.mEffect, aTextRect, FONT_BRIANNETOD12, aEffectColor, DrawStringJustification::DS_ALIGN_CENTER_VERTICAL_MIDDLE);

        if (!IsAugmentEligible(mAugmentChoices[i]))
            TodDrawStringWrapped(&aCardGraphics,
                (aDef.mSeedType != SeedType::SEED_NONE && IsSeedBannedFromShop(aDef.mSeedType, SeedType::SEED_NONE)) ?
                _S("BANNED - CANNOT SELECT") : _S("UNAVAILABLE"),
                Rect(aRect.mX + 8, aRect.mY + aRect.mHeight - 37, aRect.mWidth - 16, 28),
                FONT_BRIANNETOD12, Color(190, 48, 37, aCardAlpha), DrawStringJustification::DS_ALIGN_CENTER);

        // The chosen card brightens over the same 500 ms used to dismiss the other two.
        // Use an additive overlay instead of introducing a new texture or effect system.
        if (mAugmentSelectionPending && i == mAugmentSelectedIndex && aSelectionElapsedMs > 0)
        {
            const int aGlowAlpha = (int)(65.0f * ((float)aSelectionElapsedMs / (float)ROLL_AUGMENT_SELECTION_ANIMATION_MS));
            Graphics aGlowGraphics = Graphics(*g);
            aGlowGraphics.SetDrawMode(Graphics::DRAWMODE_ADDITIVE);
            aGlowGraphics.SetColorizeImages(true);
            aGlowGraphics.SetColor(Color(255, 238, 175, aGlowAlpha));
            // Re-draw only the card asset. Its own alpha mask defines the glow shape;
            // a FillRect here would create a rectangular highlight outside the card.
            aGlowGraphics.DrawImage(IMAGE_ALMANAC_PLANTCARD, aRect.mX, aRect.mY, aRect.mWidth, aRect.mHeight);
            aGlowGraphics.SetColorizeImages(false);
            aGlowGraphics.SetDrawMode(Graphics::DRAWMODE_NORMAL);
        }
	}

    if (mAugmentSelectionPending)
        return;

	// One free reroll per card. A used button remains visible but becomes disabled.
	int aMouseX = mApp->mWidgetManager->mLastMouseX - mX;
	int aMouseY = mApp->mWidgetManager->mLastMouseY - mY;
	for (int i = 0; i < 3; i++)
	{
		Rect aButton = GetAugmentRerollButtonRect(i);
		bool aCanReroll = CanRerollAugment(i);
        bool aButtonHovered = aButton.Contains(aMouseX, aMouseY) && aCanReroll;
		Image* aButtonImage = IMAGE_SEEDCHOOSER_BUTTON2;
		g->SetColorizeImages(true);
		g->SetColor(!aCanReroll ? Color(145, 145, 145, aFadeAlpha) : Color(255, 255, 255, aFadeAlpha));
		g->DrawImage(aButtonImage, aButton.mX, aButton.mY, aButton.mWidth, aButton.mHeight);
		if (aButtonHovered)
		{
			g->SetDrawMode(Graphics::DRAWMODE_ADDITIVE);
			g->SetColor(Color(255, 255, 255, aFadeAlpha));
			g->DrawImage(IMAGE_SEEDCHOOSER_BUTTON2_GLOW, aButton.mX, aButton.mY, aButton.mWidth, aButton.mHeight);
			g->SetDrawMode(Graphics::DRAWMODE_NORMAL);
		}
		g->SetColorizeImages(false);
		TodDrawStringWrapped(g, mAugmentRerolled[i] ? _S("USED") : (!aCanReroll ? _S("LOCKED") : _S("ROLL")), aButton, FONT_BRIANNETOD12,
			!aCanReroll ? Color(115, 115, 115, aFadeAlpha) : Color(42, 42, 90, aFadeAlpha), DrawStringJustification::DS_ALIGN_CENTER_VERTICAL_MIDDLE);
	}

	DrawAugmentTimerBar(g, GetAugmentTimerRect(), aRemainingMs);
	Rect aHideRect = GetAugmentHideButtonRect();
	bool aHideHovered = aHideRect.Contains(aMouseX, aMouseY);
	DrawStoneButton(g, aHideRect.mX, aHideRect.mY, aHideRect.mWidth, aHideRect.mHeight, false, aHideHovered, TodStringTranslate(_S("HIDE")));
}

// ---------------------------------------------------------------------------------------------
// Roll tutorial: a guided, step-by-step walkthrough. Each step waits for the player to do the action
// it explains (pick up, plant, reroll, lock). Zombie waves are held back while it runs.
// ---------------------------------------------------------------------------------------------

bool Board::IsRollTutorialActive()
{
	return mRollTutorialStep != ROLL_TUT_NONE &&
        mRollTutorialStep != ROLL_TUT_WAIT_AUGMENT &&
        mRollTutorialStep != ROLL_TUT_AUGMENT_REROLL &&
        mRollTutorialStep != ROLL_TUT_AUGMENT_CHOOSE;
}

void Board::StartRollTutorial()
{
    // Tutorial operates on the actual Shop/Bench/Lawn, without temporary fake packets.
    // Three Sunflowers + one Peashooter cost 250 Sun at the start of 1-1.
    // Leave a small reserve so the first purchase lesson cannot dead-end.
    if (mSunMoney < ROLL_TUTORIAL_SUN)
        mSunMoney = ROLL_TUTORIAL_SUN;
    SetRollTutorialStep(ROLL_TUT_BUY_SUNFLOWER);
}

void Board::SetRollTutorialStep(int theStep)
{
    mRollTutorialStep = theStep;
    mRollTutorialTimer = 0;
    TutorialArrowRemove();
    if (theStep != ROLL_TUT_BUY_SUNFLOWER)
        mApp->PlaySample(Sexy::SOUND_TAP);

    switch (theStep)
    {
    case ROLL_TUT_BUY_SUNFLOWER:
    case ROLL_TUT_BUY_PEASHOOTER:
    case ROLL_TUT_BUY_MERGE_FIRST:
    case ROLL_TUT_BUY_MERGE_SECOND:
    {
        SeedType aRequired = theStep == ROLL_TUT_BUY_PEASHOOTER ? SeedType::SEED_PEASHOOTER : SeedType::SEED_SUNFLOWER;
        SexyString aHelp = theStep == ROLL_TUT_BUY_SUNFLOWER ?
            _S("Buy a Sunflower from the Shop. The packet goes into your Bench.") :
            theStep == ROLL_TUT_BUY_PEASHOOTER ?
            _S("Buy a Peashooter. You will not pay Sun again when planting it.") :
            theStep == ROLL_TUT_BUY_MERGE_FIRST ?
            _S("Star upgrades: buy another Sunflower. Leave it on the Bench.") :
            _S("Buy the third Sunflower. Two neighboring 1-star packets combine into a 2-star packet.");
        DisplayAdvice(aHelp,
            MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1_STAY, AdviceType::ADVICE_NONE);
        for (int i = 0; i < mSeedBank->mNumPackets; ++i)
            if (mSeedBank->mSeedPackets[i].mPacketType == aRequired)
            {
                TutorialArrowShow(mSeedBank->mX + mSeedBank->mSeedPackets[i].mX,
                    mSeedBank->mY + mSeedBank->mSeedPackets[i].mY);
                break;
            }
        break;
    }
    case ROLL_TUT_PLANT_SUNFLOWER:
    case ROLL_TUT_PLANT_PEASHOOTER:
    case ROLL_TUT_PLANT_MERGED:
    {
        SeedType aRequired = theStep == ROLL_TUT_PLANT_PEASHOOTER ? SeedType::SEED_PEASHOOTER : SeedType::SEED_SUNFLOWER;
        DisplayAdvice(theStep == ROLL_TUT_PLANT_SUNFLOWER ?
            _S("Select the Sunflower from Bench, then plant it to produce Sun.") :
            theStep == ROLL_TUT_PLANT_PEASHOOTER ?
            _S("Select the Peashooter from Bench, then plant it to defend your lawn.") :
            _S("Your 2-star Sunflower has 50% more HP. Plant it now! Two adjacent 2-star packets make a 3-star plant (+100% HP)."),
            MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1_STAY, AdviceType::ADVICE_NONE);
        if (mCursorObject->mCursorType != CursorType::CURSOR_TYPE_PLANT_FROM_BANK)
        {
            for (int i = 0; i < mBenchCount; ++i)
                if (mBenchPackets[i].mPacketType == aRequired &&
                    (theStep != ROLL_TUT_PLANT_MERGED || mBenchPackets[i].mStarLevel == 2))
                {
                    // A downward-pointing IMAGE_BOARD_ARROW is drawn immediately
                    // above this moving Bench packet in Board::DrawUI().
                    return;
                }
        }
        int aX = -1, aY = -1, aBest = 0x7fffffff;
        for (int y = 0; y < MAX_GRID_SIZE_Y; ++y)
            for (int x = 0; x < MAX_GRID_SIZE_X; ++x)
                if (CanPlantAt(x, y, aRequired) == PlantingReason::PLANTING_OK)
                {
                    int aDistance = abs(x - MAX_GRID_SIZE_X / 2) + abs(y - MAX_GRID_SIZE_Y / 2);
                    if (aDistance < aBest) { aX = x; aY = y; aBest = aDistance; }
                }
        if (aX >= 0)
            TutorialArrowShow(GridToPixelX(aX, aY) + 20, GridToPixelY(aX, aY) + 20);
        break;
    }
    case ROLL_TUT_WAIT_MERGE:
        DisplayAdvice(_S("Watch your two adjacent Sunflowers merge into one 2-star Bench packet. Stars improve maximum HP."),
            MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1_STAY, AdviceType::ADVICE_NONE);
        break;
    case ROLL_TUT_REROLL:
    case ROLL_TUT_ROLL_LOCKED:
        if (mFreeRolls < 1) mFreeRolls = 1;
        DisplayAdvice(theStep == ROLL_TUT_REROLL ?
            _S("Click Reroll to refresh the Shop. This tutorial reroll is free.") :
            _S("Reroll again. Your locked plant stays while all unlocked slots change."),
            MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1_STAY, AdviceType::ADVICE_NONE);
        TutorialArrowShow(mSeedBank->mX + mSeedBank->GetRollButtonRect().mX,
            mSeedBank->mY + mSeedBank->GetRollButtonRect().mY);
        break;
    case ROLL_TUT_LOCK:
        DisplayAdvice(_S("Right-click a Shop packet to lock it for the next reroll."),
            MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1_STAY, AdviceType::ADVICE_NONE);
        for (int i = 0; i < mSeedBank->mNumPackets; ++i)
            if (mSeedBank->mSeedPackets[i].mPacketType != SeedType::SEED_NONE)
            {
                TutorialArrowShow(mSeedBank->mX + mSeedBank->mSeedPackets[i].mX,
                    mSeedBank->mY + mSeedBank->mSeedPackets[i].mY);
                break;
            }
        break;
    case ROLL_TUT_WAIT_AUGMENT:
        DisplayAdvice(_S("Great! Shop lessons complete. Defend your lawn. Before Wave 3 you will learn Augments, which grant a special upgrade."),
            MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1, AdviceType::ADVICE_NONE);
        break;
    case ROLL_TUT_AUGMENT_REROLL:
        DisplayAdvice(_S("Augment lesson: three upgrades are offered. Click ROLL below any card once to see an alternative."),
            MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1_STAY, AdviceType::ADVICE_NONE);
        break;
    case ROLL_TUT_AUGMENT_CHOOSE:
        DisplayAdvice(_S("Now select one Augment card. Its effect lasts for this level; banned Plant Augments cannot be selected."),
            MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1_STAY, AdviceType::ADVICE_NONE);
        break;
    case ROLL_TUT_FINISH:
        DisplayAdvice(_S("Tutorial complete! Use the Shop, star upgrades and Augments to defend your lawn."),
            MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1, AdviceType::ADVICE_NONE);
        break;
    default:
        ClearAdvice(AdviceType::ADVICE_NONE);
        break;
    }
}

void Board::AdvanceRollTutorial(int theFromStep)
{
    if (mRollTutorialStep != theFromStep)
        return;
    // Preserve legacy numeric step IDs stored by older SaveGame versions.
    int aNextStep = theFromStep + 1;
    if (theFromStep == ROLL_TUT_PLANT_PEASHOOTER) aNextStep = ROLL_TUT_BUY_MERGE_FIRST;
    else if (theFromStep == ROLL_TUT_BUY_MERGE_FIRST) aNextStep = ROLL_TUT_BUY_MERGE_SECOND;
    else if (theFromStep == ROLL_TUT_BUY_MERGE_SECOND) aNextStep = ROLL_TUT_WAIT_MERGE;
    else if (theFromStep == ROLL_TUT_WAIT_MERGE) aNextStep = ROLL_TUT_PLANT_MERGED;
    else if (theFromStep == ROLL_TUT_PLANT_MERGED) aNextStep = ROLL_TUT_REROLL;
    else if (theFromStep == ROLL_TUT_ROLL_LOCKED)
    {
        aNextStep = ROLL_TUT_WAIT_AUGMENT;
        // The shop lesson ends here. Give the player a visible five-second
        // countdown before the FIRST zombie wave (not the original long delay).
        // Use the existing zombie wave countdown so pause/save/load and the
        // normal SpawnZombieWave path continue to work unchanged.
        if (mCurrentWave == 0)
        {
            mApp->mIsFastMode = false;
            mZombieCountDown = ROLL_TUTORIAL_FIRST_WAVE_TICKS;
            mZombieCountDownStart = mZombieCountDown;
        }
    }
    else if (theFromStep == ROLL_TUT_AUGMENT_REROLL) aNextStep = ROLL_TUT_AUGMENT_CHOOSE;
    SetRollTutorialStep(aNextStep);
}

void Board::EndRollTutorial()
{
    mRollTutorialStep = ROLL_TUT_NONE;
    TutorialArrowRemove();
    ClearAdvice(AdviceType::ADVICE_NONE);
    if (mApp->mPlayerInfo != nullptr && mApp->mPlayerInfo->mPurchases[ROLL_TUTORIAL_SEEN_SLOT] < ROLL_TUTORIAL_VERSION)
    {
        mApp->mPlayerInfo->mPurchases[ROLL_TUTORIAL_SEEN_SLOT] = ROLL_TUTORIAL_VERSION;
        mApp->WriteCurrentUserConfig();
    }
}

void Board::UpdateRollTutorial()
{
    if (mRollTutorialStep == ROLL_TUT_NONE)
        return;
    // The five-second first-wave countdown must not be accelerated by the
    // optional fast-forward button. Restore normal player control at Wave 1.
    if (mRollTutorialStep == ROLL_TUT_WAIT_AUGMENT && mCurrentWave == 0 &&
        mZombieCountDownStart == ROLL_TUTORIAL_FIRST_WAVE_TICKS)
        mApp->mIsFastMode = false;
    ++mRollTutorialTimer;
    if (mRollTutorialStep == ROLL_TUT_WAIT_MERGE && !mBenchMergeActive)
    {
        for (int i = 0; i < mBenchCount; ++i)
            if (mBenchPackets[i].mPacketType == SeedType::SEED_SUNFLOWER && mBenchPackets[i].mStarLevel == 2)
            {
                AdvanceRollTutorial(ROLL_TUT_WAIT_MERGE);
                break;
            }
    }
    if (mRollTutorialStep == ROLL_TUT_FINISH)
        EndRollTutorial();
}

void Board::OffsetYForPlanting(int& theY, SeedType theSeedType)
{
	if (Plant::IsFlying(theSeedType) || theSeedType == SeedType::SEED_GRAVEBUSTER)
	{
		theY += 15;
	}
	if (theSeedType == SeedType::SEED_SPIKEWEED || theSeedType == SeedType::SEED_SPIKEROCK)
	{
		theY -= 15;
	}
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN && mBackground == BackgroundType::BACKGROUND_GREENHOUSE)
	{
		theY -= 25;
	}
}

int Board::PlantingPixelToGridX(int theX, int theY, SeedType theSeedType)
{
	OffsetYForPlanting(theY, theSeedType);
	return PixelToGridX(theX, theY);
}

int Board::PlantingPixelToGridY(int theX, int theY, SeedType theSeedType)
{
	OffsetYForPlanting(theY, theSeedType);

	int aGridY = PixelToGridY(theX, theY);
	if (theSeedType == SeedType::SEED_INSTANT_COFFEE)
	{
		int aGridX = PixelToGridX(theX, theY);
		
		Plant* aPlant = GetTopPlantAt(aGridX, aGridY, PlantPriority::TOPPLANT_ONLY_NORMAL_POSITION);
		if (aPlant && aPlant->mIsAsleep)
		{
			return aGridY;
		}
		
		int aGridYDown = PixelToGridY(theX, theY + 30);
		if (aGridYDown != aGridY)
		{
			Plant* aPlantDown = GetTopPlantAt(aGridX, aGridYDown, PlantPriority::TOPPLANT_ONLY_NORMAL_POSITION);
			if (aPlantDown && aPlantDown->mIsAsleep)
			{
				return aGridYDown;
			}
		}

		int aGridYUp = PixelToGridY(theX, theY - 50);
		if (aGridYUp != aGridY)
		{
			Plant* aPlantUp = GetTopPlantAt(aGridX, aGridYUp, PlantPriority::TOPPLANT_ONLY_NORMAL_POSITION);
			if (aPlantUp && aPlantUp->mIsAsleep)
			{
				return aGridYUp;
			}
		}
	}
	return aGridY;
}

int Board::PixelToGridX(int theX, int theY)
{
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
	{
		if (mBackground == BackgroundType::BACKGROUND_GREENHOUSE || 
			mBackground == BackgroundType::BACKGROUND_MUSHROOM_GARDEN || 
			mBackground == BackgroundType::BACKGROUND_ZOMBIQUARIUM)
		{
			return mApp->mZenGarden->PixelToGridX(theX, theY);
		}
	}

	if (theX < LAWN_XMIN + BOARD_ADDITIONAL_WIDTH || theX > BOARD_WIDTH - BOARD_ADDITIONAL_WIDTH - LAWN_XMIN)
		return -1;

	return ClampInt((theX - LAWN_XMIN - BOARD_ADDITIONAL_WIDTH) / 80, 0, MAX_GRID_SIZE_X - 1);
}

int Board::PixelToGridXKeepOnBoard(int theX, int theY)
{
	int aGridX = PixelToGridX(theX, theY);
	return max(aGridX, 0);
}

int Board::PixelToGridY(int theX, int theY)
{
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
	{
		if (mBackground == BackgroundType::BACKGROUND_GREENHOUSE ||
			mBackground == BackgroundType::BACKGROUND_MUSHROOM_GARDEN || 
			mBackground == BackgroundType::BACKGROUND_ZOMBIQUARIUM)
		{
			return mApp->mZenGarden->PixelToGridY(theX, theY);
		}
	}

	int aGridX = PixelToGridX(theX, theY);
	if (aGridX == -1 || theY < LAWN_YMIN + BOARD_OFFSET_Y || theY > BOARD_HEIGHT - LAWN_YMIN)
		return -1;

	if (StageHasRoof())
	{
		if (aGridX < 5)
		{
			theY -= (4 - aGridX) * 20;
		}
		return ClampInt((theY - LAWN_YMIN - BOARD_OFFSET_Y) / 85, 0, MAX_GRID_SIZE_Y - 2);
	}
	else if (StageHasPool())
	{
		return ClampInt((theY - LAWN_YMIN - BOARD_OFFSET_Y) / 85, 0, MAX_GRID_SIZE_Y - 1);
	}
	else
	{
		return ClampInt((theY - LAWN_YMIN - BOARD_OFFSET_Y) / 100, 0, MAX_GRID_SIZE_Y - 2);
	}
}

int Board::PixelToGridYKeepOnBoard(int theX, int theY)
{
	int aGridY = PixelToGridY(max(theX, 80), theY);
	return max(aGridY, 0);
}

int Board::GridToPixelX(int theGridX, int theGridY)
{
	TOD_ASSERT(theGridX >= 0 && theGridX < MAX_GRID_SIZE_X);
	TOD_ASSERT(theGridY >= 0 && theGridY < MAX_GRID_SIZE_Y);
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
	{
		if (mBackground == BackgroundType::BACKGROUND_GREENHOUSE ||
			mBackground == BackgroundType::BACKGROUND_MUSHROOM_GARDEN || 
			mBackground == BackgroundType::BACKGROUND_ZOMBIQUARIUM)
		{
			return mApp->mZenGarden->GridToPixelX(theGridX, theGridY);
		}
	}

	return theGridX * 80 + LAWN_XMIN + BOARD_ADDITIONAL_WIDTH;
}

float Board::GetPosYBasedOnRow(float thePosX, int theRow)
{
	if (StageHasRoof())
	{
		float aSlopeOffset = 0.0f;
		if (thePosX < 440.0f + BOARD_ADDITIONAL_WIDTH)
		{
			aSlopeOffset = (440.0f - thePosX) * 0.25f + BOARD_OFFSET_Y;
		}

		return GridToPixelY(8, theRow) + aSlopeOffset;
	}
	
	return GridToPixelY(0, theRow);
}

int Board::GridToPixelY(int theGridX, int theGridY)
{
	TOD_ASSERT(theGridX >= 0 && theGridX < MAX_GRID_SIZE_X);
	TOD_ASSERT(theGridY >= 0 && theGridY < MAX_GRID_SIZE_Y);
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
	{
		if (mBackground == BackgroundType::BACKGROUND_GREENHOUSE ||
			mBackground == BackgroundType::BACKGROUND_MUSHROOM_GARDEN ||
			mBackground == BackgroundType::BACKGROUND_ZOMBIQUARIUM)
		{
			return mApp->mZenGarden->GridToPixelY(theGridX, theGridY);
		}
	}

	int aY;
	if (StageHasRoof())
	{
		int aSlopeOffset;
		if (theGridX < 5)
		{
			aSlopeOffset = (5 - theGridX) * 20;
		}
		else
		{
			aSlopeOffset = 0;
		}
		aY = theGridY * 85 + aSlopeOffset + LAWN_YMIN - 10;
	}
	else if (StageHasPool())
	{
		aY = theGridY * 85 + LAWN_YMIN;
	}
	else
	{
		aY = theGridY * 100 + LAWN_YMIN;
	}

	if (theGridX != -1 && mGridSquareType[theGridX][theGridY] == GridSquareType::GRIDSQUARE_HIGH_GROUND)
	{
		aY -= HIGH_GROUND_HEIGHT;
	}

	return aY + BOARD_OFFSET_Y;
}

ZombieID Board::ZombieGetID(Zombie* theZombie)
{
	return (ZombieID)mZombies.DataArrayGetID(theZombie);
}

Zombie* Board::ZombieGet(ZombieID theZombieID)
{
	return mZombies.DataArrayGet((unsigned int)theZombieID);
}

Zombie* Board::ZombieTryToGet(ZombieID theZombieID)
{
	return mZombies.DataArrayTryToGet((unsigned int)theZombieID);
}

int GetRectOverlap(const Rect& rect1, const Rect& rect2)
{
	int xmax, rmin, rmax;

	if (rect1.mX < rect2.mX)
	{
		rmin = rect1.mX + rect1.mWidth;
		rmax = rect2.mX + rect2.mWidth;
		xmax = rect2.mX;
	}
	else
	{
		rmin = rect2.mX + rect2.mWidth;
		rmax = rect1.mX + rect1.mWidth;
		xmax = rect1.mX;
	}

	if (rmin > xmax && rmin > rmax)
	{
		rmin = rmax;
	}

	return rmin - xmax;
}

bool GetCircleRectOverlap(int theCircleX, int theCircleY, int theRadius, const Rect& theRect)
{
	int dx = 0;  
	int dy = 0;  
	bool xOut = false;  
	bool yOut = false;  

	if (theCircleX < theRect.mX)
	{
		xOut = true;
		dx = theRect.mX - theCircleX;
	}
	else if (theCircleX > theRect.mX + theRect.mWidth)
	{
		xOut = true;
		dx = theCircleX - theRect.mX - theRect.mWidth;
	}
	if (theCircleY < theRect.mY)
	{
		yOut = true;
		dy = theRect.mY - theCircleY;
	}
	else if (theCircleY > theRect.mY + theRect.mHeight)
	{
		yOut = true;
		dy = theCircleY - theRect.mY - theRect.mHeight;
	}

	if (!xOut && !yOut)  
	{
		return true;
	}
	else if (xOut && yOut)
	{
		return dx * dx + dy * dy <= theRadius * theRadius;
	}
	else if (xOut)
	{
		return dx <= theRadius;
	}
	else
	{
		return dy <= theRadius;
	}
}

bool Board::IterateZombies(Zombie*& theZombie)
{
	while (mZombies.IterateNext(theZombie))
	{
		if (!theZombie->mDead)
		{
			return true;
		}
	}

	theZombie = (Zombie*)-1;
	return false;
}

bool Board::IteratePlants(Plant*& thePlant)
{
	while (mPlants.IterateNext(thePlant))
	{
		if (!thePlant->mDead)
		{
			return true;
		}
	}

	thePlant = (Plant*)-1;
	return false;
}

bool Board::IterateProjectiles(Projectile*& theProjectile)
{
	while (mProjectiles.IterateNext(theProjectile))
	{
		if (!theProjectile->mDead)
		{
			return true;
		}
	}

	theProjectile = (Projectile*)-1;
	return false;
}

bool Board::IterateCoins(Coin*& theCoin) 
{
	while (mCoins.IterateNext(theCoin))
	{
		if (!theCoin->mDead)
		{
			return true;
		}
	}

	theCoin = (Coin*)-1;
	return false;
}

bool Board::IterateLawnMowers(LawnMower*& theLawnMower)
{
	while (mLawnMowers.IterateNext(theLawnMower))
	{
		if (!theLawnMower->mDead)
		{
			return true;
		}
	}

	theLawnMower = (LawnMower*)-1;
	return false;
}

bool Board::IterateGridItems(GridItem*& theGridItem)
{
	while (mGridItems.IterateNext(theGridItem))
	{
		if (!theGridItem->mDead)
		{
			return true;
		}
	}

	theGridItem = (GridItem*)-1;
	return false;
}

bool Board::IterateParticles(TodParticleSystem*& theParticle)
{
	while (mApp->mEffectSystem->mParticleHolder->mParticleSystems.IterateNext(theParticle))
	{
		if (!theParticle->mDead)
		{
			return true;
		}
	}

	theParticle = (TodParticleSystem*)-1;
	return false;
}

bool Board::IterateReanimations(Reanimation*& theReanimation)
{
	while (mApp->mEffectSystem->mReanimationHolder->mReanimations.IterateNext(theReanimation))
	{
		if (!theReanimation->mDead)
		{
			return true;
		}
	}

	theReanimation = (Reanimation*)-1;
	return false;
}

bool Board::IterateBushes(Bush*& theBush)
{
	while (mBushes.IterateNext(theBush))
	{
		if (theBush != nullptr)
		{
			return true;
		}
	}

	theBush = (Bush*)-1;
	return false;
}

bool Board::IterateGamepadPlayers(GamepadPlayer*& theGamepadPlayer)
{
	while (mGamepadPlayers.IterateNext(theGamepadPlayer))
	{
		if (theGamepadPlayer != nullptr)
		{
			return true;
		}
	}

	theGamepadPlayer = (GamepadPlayer*)-1;
	return false;
}

bool Board::IsControllerSeedBankSelected(int theIndex)
{
	GamepadPlayer* aGamepadPlayer = nullptr;
	while (IterateGamepadPlayers(aGamepadPlayer))
	{
		if (aGamepadPlayer->mSeedBankIndex == theIndex)
			return true;
	}
	return false;
}

bool Board::IsControllerChooserSelected(SeedType theSeedType)
{
	GamepadPlayer* aGamepadPlayer = nullptr;
	while (IterateGamepadPlayers(aGamepadPlayer))
	{
		if (aGamepadPlayer->mSeedChooserSeed == theSeedType)
			return true;
	}
	return false;
}

void Board::KillAllPlantsInRadius(int theX, int theY, int theRadius)
{
	Plant* aPlant = nullptr;
	while (IteratePlants(aPlant))
	{
		if (GetCircleRectOverlap(theX, theY, theRadius, aPlant->GetPlantRect()))
		{
			mPlantsEaten++;
			aPlant->Die();
		}
	}
}

unsigned int Board::SeedNotRecommendedForLevel(SeedType theSeedType)
{
	unsigned int aNotRec = 0;
	if (Plant::IsNocturnal(theSeedType) && !StageIsNight())
	{
		SetBit(aNotRec, NotRecommend::NOT_RECOMMENDED_NOCTURNAL, true);
	}
	if (theSeedType == SeedType::SEED_INSTANT_COFFEE && StageIsNight())
	{
		SetBit(aNotRec, NotRecommend::NOT_RECOMMENDED_AT_NIGHT, true);
	}
	if (theSeedType == SeedType::SEED_GRAVEBUSTER && !StageHasGraveStones())
	{
		SetBit(aNotRec, NotRecommend::NOT_RECOMMENDED_NEEDS_GRAVES, true);
	}
	if (theSeedType == SeedType::SEED_PLANTERN && !StageHasFog())
	{
		SetBit(aNotRec, NotRecommend::NOT_RECOMMENDED_NEEDS_FOG, true);
	}
	if (theSeedType == SeedType::SEED_FLOWERPOT && !StageHasRoof())
	{
		SetBit(aNotRec, NotRecommend::NOT_RECOMMENDED_NEEDS_ROOF, true);
	}
	if (StageHasRoof() && (theSeedType == SeedType::SEED_SPIKEWEED || theSeedType == SeedType::SEED_SPIKEROCK))
	{
		SetBit(aNotRec, NotRecommend::NOT_RECOMMENDED_ON_ROOF, true);
	}
	if (!StageHasPool() && Plant::IsAquatic(theSeedType))
	{
		SetBit(aNotRec, NotRecommend::NOT_RECOMMENDED_NEEDS_POOL, true);
	}
	return aNotRec;
}

int Board::CountCoinByType(CoinType theCoinType)
{
	int aCount = 0;

	Coin* aCoin = nullptr;
	while (IterateCoins(aCoin))
	{
		if (aCoin->mType == theCoinType)
		{
			aCount++;
		}
	}

	return aCount;
}

int Board::GetGraveStoneCount()
{
	int aCount = 0;

	GridItem* aGridItem = nullptr;
	while (IterateGridItems(aGridItem))
	{
		if (aGridItem->mGridItemType == GridItemType::GRIDITEM_GRAVESTONE)
		{
			aCount++;
		}
	}

	return aCount;
}

void Board::DropLootPiece(int thePosX, int thePosY, int theDropFactor)
{
	if (mApp->IsFirstTimeAdventureMode())
	{
		if (mLevel == 22 && mCurrentWave > 5 && !mApp->mPlayerInfo->mHasUnlockedMinigames && CountCoinByType(CoinType::COIN_PRESENT_MINIGAMES) == 0)
		{
			mApp->PlayFoley(FoleyType::FOLEY_ART_CHALLENGE);
			AddCoin(thePosX, thePosY, CoinType::COIN_PRESENT_MINIGAMES, CoinMotion::COIN_MOTION_COIN);
			return;
		}
		if (mLevel == 36 && mCurrentWave > 5 && !mApp->mPlayerInfo->mHasUnlockedPuzzleMode && CountCoinByType(CoinType::COIN_PRESENT_PUZZLE_MODE) == 0)
		{
			mApp->PlayFoley(FoleyType::FOLEY_ART_CHALLENGE);
			AddCoin(thePosX, thePosY, CoinType::COIN_PRESENT_PUZZLE_MODE, CoinMotion::COIN_MOTION_COIN);
			return;
		}
	}

	int aDropHit = Rand(30000);
	if (mApp->IsFirstTimeAdventureMode() && mLevel == 11 && !mDroppedFirstCoin && mCurrentWave > 5)
	{
		aDropHit = 1000;
	}
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_COLUMN)
	{
		aDropHit *= 5;
	}

	if (mApp->IsWhackAZombieLevel())
	{
		int aSunChanceMin = 2500;
		int aSunChanceMax = mSunMoney > 20 ? 2800 : mSunMoney > 14 ? 3100 : mSunMoney > 8 ? 3700 : 5000;
		if (aDropHit >= aSunChanceMin * theDropFactor && aDropHit <= aSunChanceMax * theDropFactor)
		{
			mApp->PlayFoley(FoleyType::FOLEY_SPAWN_SUN);
			AddCoin(thePosX - 20, thePosY, CoinType::COIN_SUN, CoinMotion::COIN_MOTION_COIN);
			AddCoin(thePosX - 40, thePosY, CoinType::COIN_SUN, CoinMotion::COIN_MOTION_COIN);
			AddCoin(thePosX - 60, thePosY, CoinType::COIN_SUN, CoinMotion::COIN_MOTION_COIN);
			return;
		}
	}

	if (mApp->mPlayingQuickplay)
		return;

	if (mTotalSpawnedWaves > 70)
		return;

	int aPottedPlantChance;
	if (!mApp->mZenGarden->CanDropPottedPlantLoot())
	{
		aPottedPlantChance = 0;
	}
	else if (mApp->IsAdventureMode() && !mApp->IsFirstTimeAdventureMode())
	{
		aPottedPlantChance = 24;
	}
	else
	{
		aPottedPlantChance = mApp->IsSurvivalEndless(mApp->mGameMode) ? 3 : 12;
	}

	int aChocolateChance = aPottedPlantChance;
	if (mApp->mZenGarden->CanDropChocolate())
	{
		if (mApp->IsAdventureMode() && !mApp->IsFirstTimeAdventureMode())
		{
			aChocolateChance = aPottedPlantChance + 72;
		}
		else
		{
			aChocolateChance = aPottedPlantChance + (mApp->IsSurvivalEndless(mApp->mGameMode) ? 9 : 36);
		}
	}

	int aDiamondChance = aChocolateChance + 14;
	int aGoldChance = aChocolateChance + 250;
	int aSilverChance = aChocolateChance + 2500;

	CoinType aCoinType;
	if (aDropHit < aPottedPlantChance * theDropFactor)
	{
		aCoinType = CoinType::COIN_PRESENT_PLANT;
	}
	else if (aDropHit < aChocolateChance * theDropFactor)
	{
		aCoinType = CoinType::COIN_CHOCOLATE;
	}
	else if (aDropHit < aDiamondChance * theDropFactor)
	{
		aCoinType = CoinType::COIN_GOLD;
	}
	else if (aDropHit < aGoldChance * theDropFactor)
	{
		aCoinType = CoinType::COIN_GOLD;
	}
	else if (aDropHit < aSilverChance * theDropFactor)
	{
		aCoinType = CoinType::COIN_SILVER;
	}
	else return;

	if (mApp->IsWallnutBowlingLevel() && Coin::IsMoney(aCoinType))
		return;

	mApp->PlayFoley(FoleyType::FOLEY_SPAWN_SUN);
	AddCoin(thePosX - 40, thePosY, aCoinType, CoinMotion::COIN_MOTION_COIN);
	mDroppedFirstCoin = true;
}

bool Board::CanDropLoot()
{
	return !mCutScene->ShouldRunUpsellBoard() && (!mApp->IsFirstTimeAdventureMode() || mLevel >= 11);
}

bool Board::BungeeIsTargetingCell(int theGridX, int theGridY)
{
	Zombie* aZombie = nullptr;
	while (IterateZombies(aZombie))
	{
		if (!aZombie->IsDeadOrDying() && aZombie->mZombieType == ZombieType::ZOMBIE_BUNGEE && aZombie->mRow == theGridY && aZombie->mTargetCol == theGridX)
		{
			return true;
		}
	}
	return false;
}

Zombie* Board::GetBossZombie()
{
	Zombie* aZombie = nullptr;
	while (IterateZombies(aZombie))
	{
		if (aZombie->mZombieType == ZombieType::ZOMBIE_BOSS)
		{
			return aZombie;
		}
	}
	return nullptr;
}

Plant* Board::FindUmbrellaPlant(int theGridX, int theGridY)
{
	Plant* aPlant = nullptr;
	while (IteratePlants(aPlant))
	{
		if (aPlant->mSeedType == SeedType::SEED_UMBRELLA && !aPlant->NotOnGround() && GridInRange(theGridX, theGridY, aPlant->mPlantCol, aPlant->mRow, 1, 1))
		{
			return aPlant;
		}
	}
	return nullptr;
}

void Board::DoFwoosh(int theRow)
{
	int aRenderOrder = MakeRenderOrder(RenderLayer::RENDER_LAYER_PARTICLE, theRow, 1);
	for (int i = 0; i < 12; i++)
	{
		Reanimation* aOriReanim = mApp->ReanimationTryToGet(mFwooshID[theRow][i]);
		if (aOriReanim)
		{
			aOriReanim->ReanimationDie();
		}

		float aPosX = 750.0f * i / 11.0f + 10.0f + BOARD_ADDITIONAL_WIDTH;
		float aPosY = GetPosYBasedOnRow(aPosX + 10.0f, theRow) - 10.0f;
		Reanimation* aFwoosh = mApp->AddReanimation(aPosX, aPosY, aRenderOrder, ReanimationType::REANIM_JALAPENO_FIRE);
		aFwoosh->SetFramesForLayer("anim_flame");
		aFwoosh->mLoopType = ReanimLoopType::REANIM_LOOP_FULL_LAST_FRAME;
		aFwoosh->mAnimRate *= RandRangeFloat(0.7f, 1.3f);

		float aScale = RandRangeFloat(0.9f, 1.1f);
		float aFlip = Rand(2) ? 1.0f : -1.0f;
		aFwoosh->OverrideScale(aScale * aFlip, 1);

		mFwooshID[theRow][i] = mApp->ReanimationGetID(aFwoosh);
	}
	mFwooshCountDown = 100;
}

void Board::UpdateFwoosh()
{
	if (mFwooshCountDown == 0)
		return;

	int aFwooshRemaining = TodAnimateCurve(50, 0, --mFwooshCountDown, 12, 0, TodCurves::CURVE_LINEAR);
	for (int aRow = 0; aRow < MAX_GRID_SIZE_Y; aRow++)
	{
		for (int i = 0; i < 12 - aFwooshRemaining; i++)
		{
			Reanimation* aFwoosh = mApp->ReanimationTryToGet(mFwooshID[aRow][i]);
			if (aFwoosh)
			{
				aFwoosh->SetFramesForLayer("anim_done");
				aFwoosh->mAnimRate = 15;
				aFwoosh->mLoopType = ReanimLoopType::REANIM_PLAY_ONCE_FULL_LAST_FRAME;
			}
			mFwooshID[aRow][i] = ReanimationID::REANIMATIONID_NULL;
		}
	}
}

void Board::UpdateGridItems()
{
	GridItem* aGridItem = nullptr;
	while (IterateGridItems(aGridItem))
	{
		if ((mEnableGraveStones || (IsRollSeedBank() && (HasAugment(RollAugment::AUGMENT_GRAVE_HARVEST) || HasAugment(RollAugment::AUGMENT_GRAVE_HARVEST_II)))) &&
            aGridItem->mGridItemType == GridItemType::GRIDITEM_GRAVESTONE && aGridItem->mGridItemCounter < 100)
		{
			aGridItem->mGridItemCounter++;
		}

		if (aGridItem->mGridItemType == GridItemType::GRIDITEM_CRATER && mApp->mGameScene == GameScenes::SCENE_PLAYING)
		{
			if (aGridItem->mGridItemCounter > 0)
			{
				aGridItem->mGridItemCounter--;
			}
			if (aGridItem->mGridItemCounter == 0)
			{
				aGridItem->GridItemDie();
			}
		}
		aGridItem->Update();
	}
}

bool Board::PlantingRequirementsMet(SeedType theSeedType)
{
	switch (theSeedType)
	{
	case SeedType::SEED_GATLINGPEA:			return CountPlantByType(SeedType::SEED_REPEATER);
	case SeedType::SEED_TWINSUNFLOWER:		return CountPlantByType(SeedType::SEED_SUNFLOWER);
	case SeedType::SEED_GLOOMSHROOM:		return CountPlantByType(SeedType::SEED_FUMESHROOM);
	case SeedType::SEED_CATTAIL:			return CountEmptyPotsOrLilies(SeedType::SEED_LILYPAD);
	case SeedType::SEED_WINTERMELON:		return CountPlantByType(SeedType::SEED_MELONPULT);
	case SeedType::SEED_GOLD_MAGNET:		return CountPlantByType(SeedType::SEED_MAGNETSHROOM);
	case SeedType::SEED_SPIKEROCK:			return CountPlantByType(SeedType::SEED_SPIKEWEED);
	case SeedType::SEED_COBCANNON:			return HasValidCobCannonSpot();
	default:								return true;
	}
}

int Board::KillAllZombiesInRadius(int theRow, int theX, int theY, int theRadius, int theRowRange, bool theBurn, int theDamageRangeFlags, bool theOnlyGridCell)
{
    int aNewDeaths = 0;
    const int aTileLeft = theOnlyGridCell ? GridToPixelX(PixelToGridXKeepOnBoard(theX, theY), theRow) : 0;
    const int aTileRight = aTileLeft + 80;
	Zombie* aZombie = nullptr;
	while (IterateZombies(aZombie))
	{
		if (aZombie->EffectedByDamage(theDamageRangeFlags))
		{
			Rect aZombieRect = aZombie->GetZombieRect();
			int aRowDist = aZombie->mRow - theRow;
			if (aZombie->mZombieType == ZombieType::ZOMBIE_BOSS)
			{
				aRowDist = 0;
			}

            const int aZombieCenterX = aZombieRect.mX + aZombieRect.mWidth / 2;
            if (aRowDist <= theRowRange && aRowDist >= -theRowRange &&
                (theOnlyGridCell ? (aZombie->mRow == theRow && aZombieCenterX >= aTileLeft && aZombieCenterX < aTileRight)
                                 : GetCircleRectOverlap(theX, theY, theRadius, aZombieRect)))
            {
                const bool wasAlive = !aZombie->IsDeadOrDying() && aZombie->mBodyHealth > 0;
                if (theBurn)
                    aZombie->ApplyBurn();
                else
                    aZombie->TakeDamage(1800, 18U);
                if (wasAlive && (aZombie->IsDeadOrDying() || aZombie->mBodyHealth <= 0))
                    ++aNewDeaths;
            }
		}
	}

	// This helper is used by plant abilities that kill or burn zombies instantly
	// (for example Potato Mine, Cherry Bomb, Doom-shroom and Cob Cannon). Grave
	// stones are immune to these effects; ordinary projectile splash damage is
	// handled separately by Projectile::DoSplashDamage.

	int aGridX = PixelToGridXKeepOnBoard(theX, theY);
	int aGridY = PixelToGridYKeepOnBoard(theX, theY);
	GridItem* aGridItem = nullptr;
	while (IterateGridItems(aGridItem))
	{
        if (aGridItem->mGridItemType == GridItemType::GRIDITEM_LADDER)
        {
            if (theOnlyGridCell ? (aGridItem->mGridX == aGridX && aGridItem->mGridY == aGridY) :
                GridInRange(aGridItem->mGridX, aGridItem->mGridY, aGridX, aGridY, theRowRange, theRowRange))
                aGridItem->GridItemDie();
        }
    }
    return aNewDeaths;
}

int Board::GetAllZombiesInRadius(int theRow, int theX, int theY, int theRadius, int theRowRange, int theDamageRangeFlags)
{
	Zombie* aZombie = nullptr;
	int TotalZombies = 0;
	while (IterateZombies(aZombie))
	{
		if (aZombie->EffectedByDamage(theDamageRangeFlags))
		{
			Rect aZombieRect = aZombie->GetZombieRect();
			int aRowDist = aZombie->mRow - theRow;
			if (aZombie->mZombieType == ZombieType::ZOMBIE_BOSS)
			{
				aRowDist = 0;
			}

			if (aRowDist <= theRowRange && aRowDist >= -theRowRange && GetCircleRectOverlap(theX, theY, theRadius, aZombieRect))
			{
				TotalZombies++;
			}
		}
	}

	return TotalZombies;
}

int Board::GetNumWavesPerSurvivalStage()
{
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND || mApp->IsSurvivalNormal(mApp->mGameMode))
	{
		return 10;
	}
	else if (mApp->IsSurvivalHard(mApp->mGameMode) || mApp->IsSurvivalEndless(mApp->mGameMode))
	{
		return 20;
	}

	TOD_ASSERT();
}

void Board::RemoveParticleByType(ParticleEffect theEffectType)
{
	TodParticleSystem* aParticle = nullptr;
	while (IterateParticles(aParticle))
	{
		if (aParticle->mEffectType == theEffectType)
		{
			aParticle->ParticleSystemDie();
		}
	}
}

bool Board::PlantUsesAcceleratedPricing(SeedType theSeedType)
{
	// UI-only seed sentinels (such as the Roll/Shuffle button) are not plant types.
	if ((int)theSeedType < 0 || (int)theSeedType >= (int)SeedType::NUM_SEED_TYPES)
		return false;
	return Plant::IsUpgrade(theSeedType) && mApp->IsSurvivalEndless(mApp->mGameMode);
}

int Board::GetCurrentPlantCost(SeedType theSeedType, SeedType theImitaterType)
{
	int aCost = Plant::GetCost(theSeedType, theImitaterType);
    if (IsRollSeedBank())
    {
        SeedType aType = theSeedType == SeedType::SEED_IMITATER ? theImitaterType : theSeedType;
        int aTier = GetPlantTier(aType);
        if (aTier > 0) aCost = aTier;
    }
	if (PlantUsesAcceleratedPricing(theSeedType))
	{
		aCost += CountPlantByType(theSeedType) * 2;
	}

	return aCost;
}

bool Board::CanUseGameObject(GameObjectType theGameObject)
{
	if (mApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM)
	{
		return theGameObject == GameObjectType::OBJECT_TYPE_TREE_FOOD || theGameObject == GameObjectType::OBJECT_TYPE_NEXT_GARDEN;
	}
	if (mApp->mGameMode != GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
	{
		return false;
	}

	if (theGameObject == GameObjectType::OBJECT_TYPE_WATERING_CAN)
	{
		return true;
	}
	if (theGameObject == GameObjectType::OBJECT_TYPE_NEXT_GARDEN)
	{
		return 
			mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_MUSHROOM_GARDEN] || 
			mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_AQUARIUM_GARDEN] ||
			mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_TREE_OF_WISDOM];
	}
	if (theGameObject == GameObjectType::OBJECT_TYPE_FERTILIZER)
	{
		return mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_FERTILIZER] > 0;
	}
	if (theGameObject == GameObjectType::OBJECT_TYPE_BUG_SPRAY)
	{
		return mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_BUG_SPRAY] > 0;
	}
	if (theGameObject == GameObjectType::OBJECT_TYPE_PHONOGRAPH)
	{
		return  mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_PHONOGRAPH] > 0;
	}
	if (theGameObject == GameObjectType::OBJECT_TYPE_CHOCOLATE)
	{
		return mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_CHOCOLATE] > 0;
	}
	if (theGameObject == GameObjectType::OBJECT_TYPE_WHEELBARROW)
	{
		return mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_WHEEL_BARROW] > 0;
	}
	if (theGameObject == GameObjectType::OBJECT_TYPE_GLOVE)
	{
		return mApp->mPlayerInfo->mPurchases[(int)StoreItem::STORE_ITEM_GARDENING_GLOVE] > 0;
	}
	if (theGameObject == GameObjectType::OBJECT_TYPE_MONEY_SIGN)
	{
		return mApp->HasFinishedAdventure();
	}
	if (theGameObject == GameObjectType::OBJECT_TYPE_TREE_FOOD)
	{
		return false;
	}
	
	TOD_ASSERT();
}

void Board::ShakeBoard(int theShakeAmountX, int theShakeAmountY)
{
	mShakeCounter = 12;
	mShakeAmountX = theShakeAmountX;
	mShakeAmountY = theShakeAmountY;
}

LawnMower* Board::FindLawnMowerInRow(int theRow)
{
	LawnMower* aLawnMower = nullptr;
	while (IterateLawnMowers(aLawnMower))
	{
		if (aLawnMower->mRow == theRow)
		{
			return aLawnMower;
		}
	}
	return nullptr;
}

Zombie* Board::GetWinningZombie()
{
	Zombie* aZombie = nullptr;
	while (IterateZombies(aZombie))
	{
		if (aZombie->mFromWave == Zombie::ZOMBIE_WAVE_WINNER)
		{
			return aZombie;
		}
	}
	return nullptr;
}

int Board::CountZombieByType(ZombieType theZombieType)
{
	int aCount = 0;

	Zombie* aZombie = nullptr;
	while (IterateZombies(aZombie))
	{
		if (aZombie->mZombieType == theZombieType)
		{
			aCount++;
		}
	}

	return aCount;
}

int Board::NumberZombiesInWave(int theWaveIndex)
{
	TOD_ASSERT(theWaveIndex >= 0 && theWaveIndex < MAX_ZOMBIE_WAVES && theWaveIndex < mNumWaves);

	for (int i = 0; i < MAX_ZOMBIES_IN_WAVE; i++)
	{
		if (mZombiesInWave[theWaveIndex][i] == ZombieType::ZOMBIE_INVALID)
		{
			return i;
		}
	}

	TOD_ASSERT();
	return 0;
}

bool Board::IsZombieTypeSpawnedOnly(ZombieType theZombieType)
{
	return (theZombieType == ZombieType::ZOMBIE_BACKUP_DANCER || theZombieType == ZombieType::ZOMBIE_BOBSLED || theZombieType == ZombieType::ZOMBIE_IMP);
}

void Board::DrawHealthbar(Graphics* g, Rect theRect, Color theMaxColor, int theMaxHealth, Color theBaseColor, int theBaseHealth, int theBarWidth, int theBarHeight, int theBarOffsetX, int theBarOffsetY, Color theTextColor, Font* theTextFont, int theOffsetY, Color theTextOutlineColor, int theTextOutlineOffset, bool theHasBarOutline)
{
	int aBarX = theRect.mX + (theRect.mWidth - theBarWidth) / 2 - theBarOffsetX;
	int aBarY = theRect.mY - theBarHeight - theBarOffsetY;
	int aBasePercentage = theBaseHealth * 100 / theMaxHealth;
	int aBaseBarWidth = theBarWidth * aBasePercentage / 100;
	SexyString text = StrFormat(_S("%d / %d"), theBaseHealth, theMaxHealth);
	TodDrawString(g, text, aBarX + (theBarWidth / 2) + theTextOutlineOffset, aBarY - theOffsetY + theTextOutlineOffset, theTextFont, theTextOutlineColor, DS_ALIGN_CENTER);
	TodDrawString(g, text, aBarX + (theBarWidth / 2), aBarY - theOffsetY, theTextFont, theTextColor, DS_ALIGN_CENTER);
	Color lastColor = g->mColor;
	g->SetColor(theMaxColor);
	g->FillRect(Rect(aBarX + aBaseBarWidth, aBarY, theBarWidth - aBaseBarWidth, theBarHeight));
	g->SetColor(theBaseColor);
	g->FillRect(Rect(aBarX, aBarY, aBaseBarWidth, theBarHeight));
	if (theHasBarOutline)
	{
		g->SetColor(Color::Black);
		g->DrawRect(Rect(aBarX - 1, aBarY - 1, theBarWidth + 1, theBarHeight + 1));
	}
	g->SetColor(lastColor);
}