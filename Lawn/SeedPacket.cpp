#include "Board.h"
#include "Cutscene.h"
#include "Challenge.h"
#include "SeedPacket.h"
#include "../LawnApp.h"
#include "CursorObject.h"
#include "../Resources.h"
#include "MessageWidget.h"
#include "../SexyAppFramework/Font.h"
#include "../Sexy.TodLib/FilterEffect.h"
#include "../SexyAppFramework/SexyMatrix.h"

static int gSeedPacketCostOverride = -1;

SeedPacket::SeedPacket()
{
	mSlotMachiningPosition = 0.0f;
	mPacketType = SeedType::SEED_NONE;
	mImitaterType = SeedType::SEED_NONE;
	mIndex = -1;
	mSlotMachiningNextSeed = SeedType::SEED_NONE;
	mWidth = 50;
	mHeight = 70;
	mRefreshCounter = 0;
	mRefreshTime = 0;
	mRefreshing = false;
	mActive = true;
	mOffsetX = 0;
	mSlotMachineCountDown = 0;
	mSlotMachiningNextSeed = SeedType::SEED_NONE;
	mTimesUsed = 0;
	mChosenPlantIndex = -1;
	mLocked = false;
	mRollAnim = 0;
    mIsBenchPacket = false;
    mStarLevel = 1;
    mPoolCardCount = 0;
    mSunSpent = 0;
    mBenchMoving = false;
    mBenchMoveStartTick = 0;
    mBenchMoveFromX = mBenchMoveFromY = 0;
    mBenchTargetX = mBenchTargetY = 0;
    mBenchMoveDurationMs = ROLL_BENCH_ANIMATION_MS;
}

void SeedPacket::PickNextSlotMachineSeed()
{
	int aPeasCount = mBoard->CountPlantByType(SeedType::SEED_PEASHOOTER);

	SeedType SLOT_SEED_TYPES[] = {
		SeedType::SEED_SUNFLOWER,
		SeedType::SEED_PEASHOOTER,
		SeedType::SEED_SNOWPEA,
		SeedType::SEED_WALLNUT,
		SeedType::SEED_SLOT_MACHINE_SUN,
		SeedType::SEED_SLOT_MACHINE_DIAMOND
	};

	int aSeedsCount = 0;
	TodWeightedArray aSeedWeightArray[(int)SeedType::NUM_SEED_TYPES];
	for (int i = 0; i < LENGTH(SLOT_SEED_TYPES); i++)
	{
		SeedType aSeedType = SLOT_SEED_TYPES[i];

		int aWeight = 100;
		if (aSeedType == SeedType::SEED_PEASHOOTER)
		{
			aWeight = TodAnimateCurve(0, 5, aPeasCount, 200, 100, TodCurves::CURVE_LINEAR);
		}
		else if (aSeedType == SeedType::SEED_SLOT_MACHINE_DIAMOND)
		{
			aWeight = 30;
		}

		if (mIndex == 2 && aSeedType != SeedType::SEED_SLOT_MACHINE_DIAMOND)
		{
			if (aSeedType == mBoard->mSeedBank->mSeedPackets[0].mSlotMachiningNextSeed || aSeedType == mBoard->mSeedBank->mSeedPackets[1].mSlotMachiningNextSeed)
			{
				aWeight += aWeight / 2;
			}
		}

		aSeedWeightArray[aSeedsCount].mItem = (int)aSeedType;
		aSeedWeightArray[aSeedsCount].mWeight = aWeight;
		aSeedsCount++;
	}

	mSlotMachiningNextSeed = (SeedType)TodPickFromWeightedArray(aSeedWeightArray, aSeedsCount);
}

void SeedPacket::SlotMachineStart()
{
	mSlotMachineCountDown = 300;
	mSlotMachiningPosition = 0.0f;
	PickNextSlotMachineSeed();
}

void SeedPacket::FlashIfReady()
{
	if (!CanPickUp() || mApp->mEasyPlantingCheat)
		return;

	if (!mBoard->HasConveyorBeltSeedBank())
	{
		int aRenderPosition = Board::MakeRenderOrder(RenderLayer::RENDER_LAYER_UI_BOTTOM, 0, 2);
        int aParticleX = mIsBenchPacket ? mX : mX + mBoard->mSeedBank->mX;
        int aParticleY = mIsBenchPacket ? mY : mY + mBoard->mSeedBank->mY;
		mApp->AddTodParticle(aParticleX, aParticleY, aRenderPosition, ParticleEffect::PARTICLE_SEED_PACKET_FLASH);
	}
	
	if (mBoard->mTutorialState == TutorialState::TUTORIAL_LEVEL_1_REFRESH_PEASHOOTER)
	{
		mBoard->SetTutorialState(TutorialState::TUTORIAL_LEVEL_1_PICK_UP_PEASHOOTER);
	}
	else if (mBoard->mTutorialState == TutorialState::TUTORIAL_LEVEL_2_REFRESH_SUNFLOWER && mPacketType == SeedType::SEED_SUNFLOWER)
	{
		mBoard->SetTutorialState(TutorialState::TUTORIAL_LEVEL_2_PICK_UP_SUNFLOWER);
	}
	else if (mBoard->mTutorialState == TutorialState::TUTORIAL_MORESUN_REFRESH_SUNFLOWER && mPacketType == SeedType::SEED_SUNFLOWER)
	{
		mBoard->SetTutorialState(TutorialState::TUTORIAL_MORESUN_PICK_UP_SUNFLOWER);
	}
}

void SeedPacket::Activate()
{
	TOD_ASSERT(mPacketType != SeedType::SEED_NONE);
	mActive = true;
}

void SeedPacket::Deactivate()
{
	mActive = false;
	mRefreshCounter = 0;
	mRefreshTime = 0;
	mRefreshing = false;
}

void SeedPacket::SetActivate(bool theActive)
{
	if (theActive)
	{
		Activate();
	}
	else
	{
		Deactivate();
	}
}

void SeedPacket::Update()
{
    if (mApp->mGameScene != GameScenes::SCENE_PLAYING || mPacketType == SeedType::SEED_NONE)
    {
        return;
    }

    if (mIsBenchPacket && mBenchMoving)
    {
        const unsigned int aElapsed = GetTickCount() - mBenchMoveStartTick;
        const int aDuration = max(1, mBenchMoveDurationMs);
        const float aProgress = min(1.0f, (float)aElapsed / (float)aDuration);
        mX = FloatRoundToInt(mBenchMoveFromX + (mBenchTargetX - mBenchMoveFromX) * aProgress);
        mY = FloatRoundToInt(mBenchMoveFromY + (mBenchTargetY - mBenchMoveFromY) * aProgress);
        if (aProgress >= 1.0f)
        {
            mX = mBenchTargetX;
            mY = mBenchTargetY;
            mBenchMoving = false;
        }
    }

	if (mRollAnim > 0)
	{
		mRollAnim--;
	}

	if (mBoard->mMainCounter == 0)
	{
		FlashIfReady();
	}

	if (!mActive && mRefreshing)
	{
		mRefreshCounter++;
		if (mRefreshCounter > mRefreshTime)
		{
			mRefreshCounter = 0;
			mRefreshing = false;
			Activate();
			FlashIfReady();
		}
	}

	if (mSlotMachineCountDown > 0)
	{
		mSlotMachineCountDown--;
		float aFlipsPerSecont = TodAnimateCurveFloat(SLOT_MACHINE_TIME, 0, mSlotMachineCountDown, 6.0f, 2.0f, TodCurves::CURVE_LINEAR);
		mSlotMachiningPosition += aFlipsPerSecont * 0.01f;

		if (mSlotMachiningPosition >= 1.0f)
		{
			mPacketType = mSlotMachiningNextSeed;
			if (mSlotMachineCountDown == 0)
			{
				Activate();
				mSlotMachiningPosition = 0.0f;
			}
			else
			{
				mSlotMachiningPosition -= 1.0f;
				PickNextSlotMachineSeed();
			}
		}
		else if (mSlotMachineCountDown == 0)
		{
			mSlotMachineCountDown = 1;
		}
	}
}

void SeedPacketDrawSeed(Graphics* g, float x, float y, SeedType theSeedType, SeedType theImitaterType, float theOffsetX, float theOffsetY, float theScale)
{
	Image* aImage = IMAGE_PACKET_PLANTS;
	SeedType aSeedType = theSeedType;
	if (theSeedType == SeedType::SEED_IMITATER && theImitaterType != SeedType::SEED_NONE)
	{
		aSeedType = theImitaterType;
		FilterEffect aFilterEffect = FilterEffect::FILTER_EFFECT_WASHED_OUT;
		if (theImitaterType == SeedType::SEED_HYPNOSHROOM || theImitaterType == SeedType::SEED_SQUASH || theImitaterType == SeedType::SEED_POTATOMINE ||
			theImitaterType == SeedType::SEED_GARLIC || theImitaterType == SeedType::SEED_LILYPAD)
			aFilterEffect = FilterEffect::FILTER_EFFECT_LESS_WASHED_OUT;
		aImage = FilterEffectGetImage(aImage, aFilterEffect);
	}

	if (aSeedType == SeedType::SEED_POTATOMINE && g->mScaleX <= 1.0f)
	{
		TodDrawImageCelScaledF(g, aImage, x, y, 0, 0, g->mScaleX, g->mScaleY);
	}
	else if (aSeedType == SeedType::SEED_CHOMPER && g->mScaleX <= 1.0f)
	{
		TodDrawImageCelScaledF(g, aImage, x, y, 1, 0, g->mScaleX, g->mScaleY);
	}
	else if (aSeedType == SeedType::SEED_HYPNOSHROOM && g->mScaleX <= 1.0f)
	{
		TodDrawImageCelScaledF(g, aImage, x, y, 2, 0, g->mScaleX, g->mScaleY);
	}
	else if (aSeedType == SeedType::SEED_TALLNUT && g->mScaleX <= 1.0f)
	{
		TodDrawImageCelScaledF(g, aImage, x, y, 3, 0, g->mScaleX, g->mScaleY);
	}
	else if (aSeedType == SeedType::SEED_BLOVER && g->mScaleX <= 1.0f)
	{
		TodDrawImageCelScaledF(g, aImage, x, y, 4, 0, g->mScaleX, g->mScaleY);
	}
	else if (aSeedType == SeedType::SEED_PUMPKINSHELL && g->mScaleX <= 1.0f)
	{
		TodDrawImageCelScaledF(g, aImage, x, y, 5, 0, g->mScaleX, g->mScaleY);
	}
	else if (aSeedType == SeedType::SEED_TWINSUNFLOWER && g->mScaleX <= 1.0f)
	{
		TodDrawImageCelScaledF(g, aImage, x, y, 6, 0, g->mScaleX, g->mScaleY);
	}
	else if (aSeedType == SeedType::SEED_COBCANNON && g->mScaleX <= 1.0f)
	{
		TodDrawImageCelScaledF(g, aImage, x, y, 7, 0, g->mScaleX, g->mScaleY);
	}
	else if (aSeedType == SeedType::SEED_CABBAGEPULT && g->mScaleX <= 1.0f)
	{
		TodDrawImageCelScaledF(g, aImage, x, y, 8, 0, g->mScaleX, g->mScaleY);
	}
	else if (aSeedType == SeedType::SEED_KERNELPULT && g->mScaleX <= 1.0f)
	{
		TodDrawImageCelScaledF(g, aImage, x, y, 9, 0, g->mScaleX, g->mScaleY);
	}
	else if (aSeedType == SeedType::SEED_MELONPULT && g->mScaleX <= 1.0f)
	{
		TodDrawImageCelScaledF(g, aImage, x, y, 10, 0, g->mScaleX, g->mScaleY);
	}
	else if (aSeedType == SeedType::SEED_WINTERMELON && g->mScaleX <= 1.0f)
	{
		TodDrawImageCelScaledF(g, aImage, x, y, 11, 0, g->mScaleX, g->mScaleY);
	}
	else if (aSeedType == SeedType::SEED_SPIKEROCK && g->mScaleX <= 1.0f)
	{
		TodDrawImageCelScaledF(g, aImage, x, y, 12, 0, g->mScaleX, g->mScaleY);
	}
	else
	{
		Graphics aSeedG(*g);
		aSeedG.mScaleX = theScale * g->mScaleX;
		aSeedG.mScaleY = theScale * g->mScaleY;
		Plant::DrawSeedType(&aSeedG, theSeedType, theImitaterType, DrawVariation::VARIATION_NORMAL, x + theOffsetX, y + theOffsetY);
	}
}

void DrawSeedPacket(Graphics* g, float x, float y, SeedType theSeedType, SeedType theImitaterType, float thePercentDark, int theGrayness, bool theDrawCost, bool theUseCurrentCost, int theRollTier)
{
	SeedType aSeedType = theSeedType;
	if (aSeedType == SeedType::SEED_IMITATER && theImitaterType != SeedType::SEED_NONE)
	{
		aSeedType = theImitaterType;
	}

	if (theGrayness != 255)
	{
		g->SetColor(Color(theGrayness, theGrayness, theGrayness));
		g->SetColorizeImages(true);
	}
	else if (thePercentDark > 0)
	{
		g->SetColor(Color(128, 128, 128, 255));
		g->SetColorizeImages(true);
	}

	int aPacketBackground;
	if (theSeedType == SeedType::SEED_IMITATER)
		aPacketBackground = 0;
	// UI-only sentinel seed types must be handled before Plant::IsUpgrade().
	// IsUpgrade() may query plant definitions, but these sentinels are not plants.
	else if (theSeedType == SeedType::SEED_BEGHOULED_BUTTON_CRATER)
		aPacketBackground = 3;
	else if (theSeedType == SeedType::SEED_BEGHOULED_BUTTON_SHUFFLE)
		aPacketBackground = 4;
	else if (theSeedType == SeedType::SEED_SLOT_MACHINE_SUN)
		aPacketBackground = 5;
	else if (theSeedType == SeedType::SEED_SLOT_MACHINE_DIAMOND)
		aPacketBackground = 6;
	else if (theSeedType == SeedType::SEED_ZOMBIQUARIUM_SNORKLE)
		aPacketBackground = 7;
	else if (theSeedType == SeedType::SEED_ZOMBIQUARIUM_TROPHY)
		aPacketBackground = 8;
    else if (theSeedType == SeedType::SEED_NONE)
        aPacketBackground = 2;
	else if (Plant::IsUpgrade(aSeedType))
		aPacketBackground = 1;
	else if ((theSeedType >= SeedType::SEED_ZOMBIE_NORMAL && theSeedType < SeedType::NUM_ZOMBIE_SEEDS) && USE_ZOMBIE_SEED_VARIANT)
		aPacketBackground = 9;
	else
		aPacketBackground = 2;

    // Keep Tier color on the packet background only; the plant artwork itself remains untinted.
    // Tier is a property of the packet's BACKGROUND, not of its interactive state.
    // Apply recharge/unaffordable/cutscene darkness *to* the tier RGB rather than
    // reverting to the stock brown background while the packet is disabled.
    const bool aTierTint = theRollTier >= 2 && theRollTier <= 5;
    Color aOriginalColor = g->mColor;
    bool aOriginalColorize = g->mColorizeImages;
    if (aTierTint)
    {
        Color aTierColor = theRollTier == 2 ? Color(80, 205, 100) :
            theRollTier == 3 ? Color(70, 155, 240) :
            theRollTier == 4 ? Color(180, 100, 225) : Color(255, 175, 55);
        float aShade = max(0.0f, min(1.0f, theGrayness / 255.0f));
        if (thePercentDark > 0.0f) aShade *= 0.5f;
        g->SetColor(Color((int)(aTierColor.mRed * aShade),
            (int)(aTierColor.mGreen * aShade), (int)(aTierColor.mBlue * aShade)));
        g->SetColorizeImages(true);
    }
    if (g->mScaleX > 1)
    {
        TodDrawImageCelScaledF(g, Sexy::IMAGE_SEEDPACKET_LARGER, x, y, 0, 0, g->mScaleX * 0.5f, g->mScaleY * 0.5f);
    }
    else
    {
        TodDrawImageCelScaledF(g, USE_CONSOLE_SEED_VARIANTS ? Sexy::IMAGE_CONSOLE_SEEDS : Sexy::IMAGE_SEEDS, x, y, aPacketBackground, 0, g->mScaleX, g->mScaleY);
    }
    if (aTierTint)
    {
        g->SetColor(aOriginalColor);
        g->SetColorizeImages(aOriginalColorize);
    }

	float aScale = 0.5f;
	bool aDrawSeedInMiddle = true;
	float aOffsetX = 5.0f;
	float aOffsetY = 8.0f;
    // A SeedPacket with SEED_NONE is used for the empty Hologram slot before
    // the player has planted anything. Draw the native packet frame only.
    if (theSeedType == SeedType::SEED_NONE)
        aDrawSeedInMiddle = false;
	switch (aSeedType)
	{
	case SeedType::SEED_TALLNUT:
		aScale = 0.3f;
		aOffsetX = 12.0f;
		aOffsetY = 22.0f;
		break;

	case SeedType::SEED_INSTANT_COFFEE:
		aScale = 0.55f;
		aOffsetX = 0;
		aOffsetY = 9.0f;
		break;

	case SeedType::SEED_COBCANNON:
		aScale = 0.26f;
		aOffsetX = 6.0f;
		aOffsetY = 22.0f;
		break;

	case SeedType::SEED_CACTUS:
		aOffsetX = 9.0f;
		aOffsetY = 13.0f;
		break;

	case SeedType::SEED_POTATOMINE:
		aScale = 0.4f;
		aOffsetX = 8.0f;
		aOffsetY = 12.0f;
		break;

	case SeedType::SEED_MAGNETSHROOM:
		aOffsetY = 12.0f;
		break;

	case SeedType::SEED_FUMESHROOM:
	case SeedType::SEED_PUMPKINSHELL:
	case SeedType::SEED_CHOMPER:
	case SeedType::SEED_DOOMSHROOM:
	case SeedType::SEED_SQUASH:
	case SeedType::SEED_HYPNOSHROOM:
	case SeedType::SEED_SPIKEWEED:
	case SeedType::SEED_SPIKEROCK:
	case SeedType::SEED_PLANTERN:
	case SeedType::SEED_TORCHWOOD:
	case SeedType::SEED_TANGLEKELP:
		aScale = 0.4f;
		aOffsetX = 8.0f;
		aOffsetY = 12.0f;
		break;

	case SeedType::SEED_TWINSUNFLOWER:
	case SeedType::SEED_GLOOMSHROOM:
		aScale = 0.45f;
		aOffsetX = 7.0f;
		aOffsetY = 14.0f;
		break;

	case SeedType::SEED_CATTAIL:
		aScale = 0.45f;
		aOffsetX = 8.0f;
		aOffsetY = 13.0f;
		break;

	case SeedType::SEED_UMBRELLA:
		aScale = 0.5f;
		aOffsetX = 5.0f;
		aOffsetY = 10.0f;
		break;

	case SeedType::SEED_KERNELPULT:
		aScale = 0.4f;
		aOffsetX = 13.0f;
		aOffsetY = 14.0f;
		break;

	case SeedType::SEED_CABBAGEPULT:
		aScale = 0.4f;
		aOffsetX = 15.0f;
		aOffsetY = 14.0f;
		break;

	case SeedType::SEED_MELONPULT:
	case SeedType::SEED_WINTERMELON:
		aScale = 0.35f;
		aOffsetX = 18.0f;
		aOffsetY = 19.0f;
		break;

	case SeedType::SEED_GRAVEBUSTER:
		aScale = 0.4f;
		aOffsetX = 10.0f;
		aOffsetY = 15.0f;
		break;

	case SeedType::SEED_SPLITPEA:
		aScale = 0.45f;
		aOffsetX = 12.0f;
		aOffsetY = 12.0f;
		break;

	case SeedType::SEED_BLOVER:
		aScale = 0.4f;
		aOffsetX = 8.0f;
		aOffsetY = 17.0f;
		break;

	case SeedType::SEED_STARFRUIT:
		aScale = 0.5f;
		aOffsetX = 6.0f;
		aOffsetY = 8.0f;
		break;

	case SeedType::SEED_THREEPEATER:
		aScale = 0.5f;
		aOffsetX = 5.0f;
		aOffsetY = 10.0f;
		break;

	case SeedType::SEED_GATLINGPEA:
		aScale = 0.5f;
		aOffsetX = 2.0f;
		aOffsetY = 8.0f;
		break;

	case SeedType::SEED_ZOMBIE_NORMAL:
	case SeedType::SEED_ZOMBIE_TRAFFIC_CONE:
	case SeedType::SEED_ZOMBIE_PAIL:
	case SeedType::SEED_ZOMBIE_DANCER:
		aScale = 0.35f;
		aOffsetX = -3.0f;
		aOffsetY = -7.0f;
		break;

	case SeedType::SEED_ZOMBIE_POLEVAULTER:
		aScale = 0.35f;
		aOffsetX = -8.0f;
		aOffsetY = -12.0f;
		break;

	case SeedType::SEED_ZOMBIE_LADDER:
	case SeedType::SEED_ZOMBIE_DIGGER:
	case SeedType::SEED_ZOMBIE_SCREEN_DOOR:
	case SeedType::SEED_ZOMBIE_POGO:
		aScale = 0.35f;
		aOffsetX = -3.0f;
		aOffsetY = -10.0f;
		break;

	case SeedType::SEED_ZOMBIE_BUNGEE:
		aScale = 0.3f;
		aOffsetX = 1.0f;
		aOffsetY = -1.0f;
		break;

	case SeedType::SEED_ZOMBIE_FOOTBALL:
		aScale = 0.33f;
		aOffsetX = -7.0f;
		aOffsetY = -9.0f;
		break;

	case SeedType::SEED_ZOMBIE_BALLOON:
		aScale = 0.35f;
		aOffsetX = -3.0f;
		aOffsetY = -5.0f;
		break;

	case SeedType::SEED_ZOMBIE_IMP:
		aScale = 0.4f;
		aOffsetX = -12.0f;
		aOffsetY = -17.0f;
		break;

	case SeedType::SEED_ZOMBONI:
		aScale = 0.23f;
		aOffsetX = 12.0f;
		aOffsetY = 3.0f;
		break;

	case SeedType::SEED_ZOMBIE_GARGANTUAR:
		aScale = 0.23f;
		aOffsetX = 4.0f;
		aOffsetY = 3.0f;
		break;

	case SeedType::SEED_BEGHOULED_BUTTON_SHUFFLE:
	case SeedType::SEED_BEGHOULED_BUTTON_CRATER:
	case SeedType::SEED_SLOT_MACHINE_SUN:
	case SeedType::SEED_SLOT_MACHINE_DIAMOND:
	case SeedType::SEED_ZOMBIQUARIUM_SNORKLE:
	case SeedType::SEED_ZOMBIQUARIUM_TROPHY:
		aDrawSeedInMiddle = false;
		break;
	}
	if (((LawnApp*)gSexyAppBase)->mGameMode == GameMode::GAMEMODE_CHALLENGE_BIG_TIME)
	{
		if (aSeedType == SeedType::SEED_WALLNUT || aSeedType == SeedType::SEED_SUNFLOWER || aSeedType == SeedType::SEED_MARIGOLD)
		{
			aOffsetX = 16.0f;
			aOffsetY = 34.0f;
		}
	}
	if (aSeedType == SeedType::SEED_GIANT_WALLNUT)
	{
		aScale *= 0.75f;
		aOffsetX = 52.0f;
		aOffsetY = 58.0f;
	}
	aOffsetX = g->mScaleX * aOffsetX;
	aOffsetY = g->mScaleY * (aOffsetY + 1.0f);
	if (aDrawSeedInMiddle)
	{
		SeedPacketDrawSeed(g, x, y, theSeedType, theImitaterType, aOffsetX, aOffsetY, aScale);
	}

	if (thePercentDark > 0.0f)
	{
		int aDarknessHeight = FloatRoundToInt(68.0f * thePercentDark) + 2;
		Graphics aPlantG(*g);
		aPlantG.SetColor(Color(64, 64, 64, 255));
		aPlantG.SetColorizeImages(true);
		aPlantG.ClipRect(x, y, SEED_PACKET_WIDTH, aDarknessHeight);
        if (aTierTint)
        {
            const Color aTier = theRollTier == 2 ? Color(80, 205, 100) :
                theRollTier == 3 ? Color(70, 155, 240) :
                theRollTier == 4 ? Color(180, 100, 225) : Color(255, 175, 55);
            // Darkened overlay uses the SAME Tier RGB as the base packet.
            aPlantG.SetColor(Color(aTier.mRed * 64 / 255, aTier.mGreen * 64 / 255,
                aTier.mBlue * 64 / 255));
        }
        TodDrawImageCelScaledF(&aPlantG, Sexy::IMAGE_SEEDS, x, y, aPacketBackground, 0, aPlantG.mScaleX, aPlantG.mScaleY);
		if (aDrawSeedInMiddle)
		{
			SeedPacketDrawSeed(&aPlantG, x, y, theSeedType, theImitaterType, aOffsetX, aOffsetY, aScale);
		}
	}

	if (theDrawCost)
	{
		SexyString aCostStr;
		// The Roll Shop button reuses the Beghouled shuffle seed type, which is a UI-only
		// sentinel and is outside NUM_SEED_TYPES. Never pass it to Plant::GetCost or
		// other plant-definition lookups (GetPlantDefinition asserts during the first draw).
		if (theSeedType == SeedType::SEED_BEGHOULED_BUTTON_SHUFFLE)
		{
			// Always avoid plant-cost and plant-definition lookups for this UI-only seed.
			aCostStr = StrFormat(_S("%d"), gSeedPacketCostOverride >= 0 ? gSeedPacketCostOverride : 0);
		}
		else if (gSeedPacketCostOverride >= 0 && gLawnApp->mBoard && gLawnApp->mBoard->IsRollSeedBank())
		{
			// Roll Shop passes the resolved live cost here. This covers Augment overrides
			// such as Budget Peas (Peashooter = 75) as well as accelerated pricing.
			aCostStr = StrFormat(_S("%d"), gSeedPacketCostOverride);
		}
		else if (gLawnApp->mBoard && gLawnApp->mBoard->PlantUsesAcceleratedPricing(aSeedType))
		{
			if (theUseCurrentCost)
			{
				aCostStr = StrFormat(_S("%d"), gLawnApp->mBoard->GetCurrentPlantCost(theSeedType, theImitaterType));
			}
			else
			{
				aCostStr = StrFormat(_S("%d+"), Plant::GetCost(theSeedType, theImitaterType));
			}
		}
		else
		{
			aCostStr = StrFormat(_S("%d"), Plant::GetCost(theSeedType, theImitaterType));
		}

		Font* aTextFont = USE_OLD_STYLE_SEEDPACKET ? Sexy::FONT_PICO129 : Sexy::FONT_BRIANNETOD12;
		int aTextOffsetX = 32 - aTextFont->StringWidth(aCostStr);
		int aTextOffsetY = aTextFont->GetAscent() + (USE_OLD_STYLE_SEEDPACKET ? 54 : 52) - (USE_CONSOLE_SEED_VARIANTS ? 2 : 0);
		if (g->mScaleX == 1.0f && g->mScaleY == 1.0f)
		{
			TodDrawString(g, aCostStr, x + aTextOffsetX, y + aTextOffsetY, aTextFont, Color::Black, DS_ALIGN_LEFT);
		}
		else
		{
			SexyMatrix3 aMatrix;
			TodScaleTransformMatrix(aMatrix, aTextOffsetX * g->mScaleX + x, aTextOffsetY * g->mScaleY + y, g->mScaleX, g->mScaleY);
			if (g->mScaleX > 1.8f)
			{
				g->SetLinearBlend(false);
			}
			TodDrawStringMatrix(g, aTextFont, aMatrix, aCostStr, Color::Black);
			g->SetLinearBlend(true);
		}
	}

	g->SetColorizeImages(false);
}

void SeedPacket::Draw(Graphics* g)
{
	// roll seed bank flip animation: cycle through the Chosen Plants while the slot "rolls"
	SeedType aRealType = mPacketType;
	SeedType aRealImitater = mImitaterType;
	bool aRolling = mRollAnim > 0 && mPacketType != SeedType::SEED_NONE && mBoard->mNumChosenPlants > 0;
	if (aRolling)
	{
		const ChosenPlant& aFlipPlant = mBoard->mChosenPlants[(mBoard->mMainCounter / 3 + mIndex * 2) % mBoard->mNumChosenPlants];
		mPacketType = aFlipPlant.mSeedType;
		mImitaterType = aFlipPlant.mImitaterType;
	}

	float aPercentDark = 0.0f;
	if (!mActive)
	{
		if (mRefreshTime == 0)
		{
			aPercentDark = 1.0f;
		}
		else
		{
			aPercentDark = (float)(mRefreshTime - mRefreshCounter) / (float)mRefreshTime;
		}
	}

	if (mSlotMachineCountDown > 0)
	{
		int aOffsetY = FloatRoundToInt(-mHeight * mSlotMachiningPosition);

		Graphics aClipG(*g);
		aClipG.ClipRect(0, 0, mWidth, mHeight);

		DrawSeedPacket(&aClipG, 0.0f, aOffsetY, mPacketType, SeedType::SEED_NONE, 0.0f, 128, false, false);
		DrawSeedPacket(&aClipG, 0.0f, mHeight + aOffsetY, mSlotMachiningNextSeed, SeedType::SEED_NONE, 0.0f, 128, false, false);
	}
	else
	{
		SeedType aUseSeedType = mPacketType;
		if (mPacketType == SeedType::SEED_IMITATER && mImitaterType != SeedType::SEED_NONE)
		{
			aUseSeedType = mImitaterType;
		}

		bool aDrawCost = mPacketType != SeedType::SEED_NONE && !mIsBenchPacket;
		if (mBoard->HasConveyorBeltSeedBank() || mApp->IsSlotMachineLevel())
		{
			aDrawCost = false;
		}
		// Hologram uses an empty SEED_NONE packet until the first plant is placed.
		// Avoid querying plant definitions for that UI-only empty state.
		int aCost = mPacketType != SeedType::SEED_NONE
			? mBoard->GetCurrentPlantCost(mPacketType, mImitaterType)
			: 0;

		int aGrayness = 255;
		if ((mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED && !mActive) ||
			(mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST && !mActive))
		{
			aGrayness = 64;
		}
		else if (mApp->mGameScene != GameScenes::SCENE_PLAYING)
		{
			aGrayness = mBoard->mSeedBank->mCutSceneDarken;
			aPercentDark = 0.0f;
		}
		else if (mBoard->mTutorialState == TutorialState::TUTORIAL_LEVEL_1_PICK_UP_PEASHOOTER && mBoard->mTutorialTimer == -1 && mPacketType == SeedType::SEED_PEASHOOTER)
		{
			aGrayness = GetFlashingColor(mBoard->mMainCounter, 75).mRed;
		}
		else if (mBoard->mTutorialState == TutorialState::TUTORIAL_LEVEL_2_PICK_UP_SUNFLOWER && mPacketType == SeedType::SEED_SUNFLOWER)
		{
			aGrayness = GetFlashingColor(mBoard->mMainCounter, 75).mRed;
		}
		else if (mBoard->mTutorialState == TutorialState::TUTORIAL_MORESUN_PICK_UP_SUNFLOWER && mPacketType == SeedType::SEED_SUNFLOWER)
		{
			aGrayness = GetFlashingColor(mBoard->mMainCounter, 75).mRed;
		}
		else if (mBoard->mTutorialState == TutorialState::TUTORIAL_WHACK_A_ZOMBIE_PICK_SEED)
		{
			aGrayness = GetFlashingColor(mBoard->mMainCounter, 75).mRed;
		}
		else if (mApp->mEasyPlantingCheat)
		{
			aPercentDark = 0.0f;
		}
		else if (!mIsBenchPacket && ((!(mBoard->IsRollSeedBank() && this != &mBoard->mHologramPacket ?
                  mBoard->CanAffordRollPurchase(aCost) : mBoard->CanTakeSunMoney(aCost)) && aDrawCost) || aPercentDark > 1.0f || (!mBoard->IsRollSeedBank() && !mBoard->PlantingRequirementsMet(aUseSeedType))))
		{
			aGrayness = 128;
		}

        if (mBoard->IsRollSeedBank() && !mIsBenchPacket && mBoard->IsShopClosed())
            aGrayness = 90; // native packet grayscale, no additional overlay

		// roll seed bank: print the real cost (augments can change it, e.g. cheaper Peashooters)
		bool aRollBank = mBoard->IsRollSeedBank();
		if (aRollBank && aDrawCost && !aRolling)
		{
			gSeedPacketCostOverride = aCost * (this != &mBoard->mHologramPacket && mBoard->HasAugment(RollAugment::AUGMENT_LUCKY_ROLLS) ? 10 : 1);
		}
        int aRollTier = mBoard->IsRollSeedBank() ? mBoard->GetPlantTier(aUseSeedType) : 0;
        DrawSeedPacket(g, mOffsetX, 0.0f, mPacketType, mImitaterType, aPercentDark, aGrayness, aDrawCost, true, aRollTier);
        gSeedPacketCostOverride = -1;
        if (aRollBank && aDrawCost && !aRolling && this != &mBoard->mHologramPacket && mBoard->HasAugment(RollAugment::AUGMENT_LUCKY_ROLLS))
            TodDrawString(g, _S("$"), mOffsetX + 4, mHeight - 5, FONT_BRIANNETOD12,
                Color(245, 225, 128), DrawStringJustification::DS_ALIGN_LEFT);

        // Stars are displayed only on owned Bench packets; Shop offers are not yet owned.
        if (mIsBenchPacket && mApp->mGameScene == GameScenes::SCENE_PLAYING)
        {
            Color aOldColor = g->mColor;
            g->SetColor(Color(20, 20, 20, 220));
            g->FillRect(mOffsetX + mWidth - 22, 2, 20, 14);
            g->SetColor(Color(255, 230, 130));
            TodDrawString(g, StrFormat(_S("%d*"), mStarLevel), mOffsetX + mWidth - 3, 13, FONT_BRIANNETOD12, Color(255, 230, 130), DS_ALIGN_RIGHT);
            g->SetColor(aOldColor);
        }

		// A locked slot keeps its normal artwork and shows a small padlock in its top-right corner
		if (mLocked && !aRolling && aRollBank && mApp->mGameScene == GameScenes::SCENE_PLAYING)
		{
			// Defensive guard: IMAGE_LOCK is a delayed resource and must never
			// be dereferenced if its resource group failed to load.
			if (IMAGE_LOCK != nullptr)
			{
				const float aLockScale = 0.32f;
				float aLockX = mOffsetX + mWidth - IMAGE_LOCK->GetWidth() * aLockScale - 2.0f;
				TodDrawImageScaledF(g, IMAGE_LOCK, aLockX, 2.0f, aLockScale, aLockScale);
			}
		}
	}

	mPacketType = aRealType;
	mImitaterType = aRealImitater;
}

bool SeedPacket::CanPickUp()
{
    if (mBoard->mPaused || mApp->mGameScene != GameScenes::SCENE_PLAYING || mPacketType == SeedType::SEED_NONE || mRollAnim > 0)
        return false;
    if (mBoard->IsRollTutorialActive())
    {
        int aStep = mBoard->mRollTutorialStep;
        if (mIsBenchPacket)
        {
            if (aStep != ROLL_TUT_PLANT_SUNFLOWER && aStep != ROLL_TUT_PLANT_PEASHOOTER &&
                aStep != ROLL_TUT_PLANT_MERGED)
                return false;
            if (mPacketType != (aStep == ROLL_TUT_PLANT_PEASHOOTER ? SeedType::SEED_PEASHOOTER : SeedType::SEED_SUNFLOWER) ||
                (aStep == ROLL_TUT_PLANT_MERGED && mStarLevel != 2))
                return false;
        }
        else if (aStep != ROLL_TUT_BUY_SUNFLOWER && aStep != ROLL_TUT_BUY_PEASHOOTER &&
                 aStep != ROLL_TUT_BUY_MERGE_FIRST && aStep != ROLL_TUT_BUY_MERGE_SECOND)
            return false;
        else if (mPacketType != (aStep == ROLL_TUT_BUY_PEASHOOTER ? SeedType::SEED_PEASHOOTER : SeedType::SEED_SUNFLOWER))
            return false;
    }
    // A full Bench prevents buying/selecting another Shop or reward packet, but it must
    // never prevent selecting an existing Bench packet to plant it.
    // Hologram plants directly onto the lawn and does not consume a Bench slot.
    if (!mIsBenchPacket && this != &mBoard->mHologramPacket &&
        mBoard->IsRollSeedBank() && mBoard->IsBenchFull())
        return false;

	SeedType aUseSeedType = mPacketType;
	if (mPacketType == SeedType::SEED_IMITATER && mImitaterType != SeedType::SEED_NONE)
	{
		aUseSeedType = mImitaterType;
	}

	if (mApp->IsSlotMachineLevel())
	{
		return false;
	}

    if (!mApp->mEasyPlantingCheat)
    {
        if (!mActive)
            return false;

        int aCost = mBoard->GetCurrentPlantCost(mPacketType, mImitaterType);
        if (!mIsBenchPacket && !(mBoard->IsRollSeedBank() && this != &mBoard->mHologramPacket ?
            mBoard->CanAffordRollPurchase(aCost) : mBoard->CanTakeSunMoney(aCost)) && !mBoard->HasConveyorBeltSeedBank())
            return false;

        if (!mIsBenchPacket && !mBoard->IsRollSeedBank() && !mBoard->PlantingRequirementsMet(aUseSeedType))
            return false;
	}

	return true;
}

void SeedPacket::MouseDown(int x, int y, int theClickCount)
{
    // Bench packets are already purchased. Left-click selects them for planting;
    // a double right-click discards them without refunding Sun.
    if (mIsBenchPacket)
    {
        if (mBoard->mPaused || mApp->mGameScene != GameScenes::SCENE_PLAYING || mPacketType == SeedType::SEED_NONE || mRollAnim > 0 || mBenchMoving || mBoard->mBenchMergeActive)
            return;
        if (mBoard->IsRollTutorialActive() && mBoard->mRollTutorialStep != ROLL_TUT_PLANT_SUNFLOWER &&
            mBoard->mRollTutorialStep != ROLL_TUT_PLANT_PEASHOOTER &&
            mBoard->mRollTutorialStep != ROLL_TUT_PLANT_MERGED)
        {
            mApp->PlaySample(Sexy::SOUND_BUZZER);
            return;
        }
        if (theClickCount <= -2)
        {
            if (!mBoard->IsRollTutorialActive())
            {
                mBoard->RemoveBenchSeedPacket(mIndex, true);
                mApp->PlaySample(Sexy::SOUND_TAP);
            }
            return;
        }
        if (theClickCount < 0)
            return;
        if (!CanPickUp())
        {
            mApp->PlaySample(Sexy::SOUND_BUZZER);
            return;
        }
        mBoard->mCursorObject->mType = mPacketType;
        mBoard->mCursorObject->mImitaterType = mImitaterType;
        mBoard->mCursorObject->mCursorType = CursorType::CURSOR_TYPE_PLANT_FROM_BANK;
        mBoard->mCursorObject->mSeedBankIndex = -(mIndex + 2); // -1 remains reserved for Hologram
        Deactivate();
        mApp->PlaySample(SOUND_SEEDLIFT);
        return;
    }

    // In Roll Shop levels, clicking a Shop slot performs a purchase immediately;
    // the player receives a Bench packet instead of a plant cursor.
    if (mBoard->IsRollSeedBank())
    {
        if (theClickCount >= 0)
            mBoard->BuyRollShopSeedPacket(this);
        return;
    }
    if (mBoard->IsRollTutorialActive() &&
        ((mBoard->mRollTutorialStep == ROLL_TUT_BUY_SUNFLOWER && mPacketType != SeedType::SEED_SUNFLOWER) ||
         (mBoard->mRollTutorialStep == ROLL_TUT_BUY_PEASHOOTER && mPacketType != SeedType::SEED_PEASHOOTER)))
	{
		mApp->PlaySample(Sexy::SOUND_BUZZER);
		return;
	}
	if (mBoard->IsRollTutorialActive() && mBoard->mRollTutorialStep != ROLL_TUT_BUY_SUNFLOWER &&
        mBoard->mRollTutorialStep != ROLL_TUT_BUY_PEASHOOTER)
	{
		mApp->PlaySample(Sexy::SOUND_BUZZER);
		return;
	}
	if (mBoard->mPaused || mApp->mGameScene != GameScenes::SCENE_PLAYING || mPacketType == SeedType::SEED_NONE || mRollAnim > 0)
	{
		return;
	}

	if (mApp->IsSlotMachineLevel())
	{
		if (!mBoard->mAdvice->IsBeingDisplayed())
		{
			mBoard->DisplayAdvice(_S("[ADVICE_SLOT_MACHINE_PULL]"), MessageStyle::MESSAGE_STYLE_HINT_TALL_FAST, AdviceType::ADVICE_NONE);
		}
		mBoard->mChallenge->mSlotMachineRollCount = min(mBoard->mChallenge->mSlotMachineRollCount, 2);
		return;
	}

	SeedType aUseSeedType = mPacketType;
	if (mPacketType == SeedType::SEED_IMITATER && mImitaterType != SeedType::SEED_NONE)
	{
		aUseSeedType = mImitaterType;
	}

	if (!mApp->mEasyPlantingCheat)
	{
		if (!mActive)
		{
			mApp->PlaySample(SOUND_BUZZER);
			if (mApp->IsFirstTimeAdventureMode() && mBoard->mLevel == 1 && !mBoard->IsRollSeedBank() && mBoard->mHelpDisplayed[(int)AdviceType::ADVICE_CLICK_ON_SUN])
			{
				mBoard->DisplayAdvice(_S("[ADVICE_SEED_REFRESH]"), MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1, AdviceType::ADVICE_SEED_REFRESH);
			}
			return;
		}

		int aCost = mBoard->GetCurrentPlantCost(mPacketType, mImitaterType);
		if (!(mBoard->IsRollSeedBank() && this != &mBoard->mHologramPacket ?
            mBoard->CanAffordRollPurchase(aCost) : mBoard->CanTakeSunMoney(aCost)) && !mBoard->HasConveyorBeltSeedBank())
		{
			mApp->PlaySample(SOUND_BUZZER);
			mBoard->mOutOfMoneyCounter = 70;
			if (mApp->IsFirstTimeAdventureMode() && mBoard->mLevel == 1 && !mBoard->IsRollSeedBank() && mBoard->mHelpDisplayed[(int)AdviceType::ADVICE_CLICK_ON_SUN])
			{
				mBoard->DisplayAdvice(_S("[ADVICE_CANT_AFFORD_PLANT]"), MessageStyle::MESSAGE_STYLE_TUTORIAL_LEVEL1, AdviceType::ADVICE_CANT_AFFORD_PLANT);
			}
			return;
		}

		if (!mBoard->PlantingRequirementsMet(aUseSeedType))
		{
			mApp->PlaySample(SOUND_BUZZER);
			if (aUseSeedType == SeedType::SEED_GATLINGPEA)
			{
				mBoard->DisplayAdvice(_S("[ADVICE_PLANT_NEEDS_REPEATER]"), MessageStyle::MESSAGE_STYLE_HINT_LONG, AdviceType::ADVICE_PLANT_NEEDS_REPEATER);
			}
			else if (aUseSeedType == SeedType::SEED_WINTERMELON)
			{
				mBoard->DisplayAdvice(_S("[ADVICE_PLANT_NEEDS_MELONPULT]"), MessageStyle::MESSAGE_STYLE_HINT_LONG, AdviceType::ADVICE_PLANT_NEEDS_MELONPULT);
			}
			else if (aUseSeedType == SeedType::SEED_TWINSUNFLOWER)
			{
				mBoard->DisplayAdvice(_S("[ADVICE_PLANT_NEEDS_SUNFLOWER]"), MessageStyle::MESSAGE_STYLE_HINT_LONG, AdviceType::ADVICE_PLANT_NEEDS_SUNFLOWER);
			}
			else if (aUseSeedType == SeedType::SEED_SPIKEROCK)
			{
				mBoard->DisplayAdvice(_S("[ADVICE_PLANT_NEEDS_SPIKEWEED]"), MessageStyle::MESSAGE_STYLE_HINT_LONG, AdviceType::ADVICE_PLANT_NEEDS_SPIKEWEED);
			}
			else if (aUseSeedType == SeedType::SEED_COBCANNON)
			{
				mBoard->DisplayAdvice(_S("[ADVICE_PLANT_NEEDS_KERNELPULT]"), MessageStyle::MESSAGE_STYLE_HINT_LONG, AdviceType::ADVICE_PLANT_NEEDS_KERNELPULT);
			}
			else if (aUseSeedType == SeedType::SEED_GOLD_MAGNET)
			{
				mBoard->DisplayAdvice(_S("[ADVICE_PLANT_NEEDS_MAGNETSHROOM]"), MessageStyle::MESSAGE_STYLE_HINT_LONG, AdviceType::ADVICE_PLANT_NEEDS_MAGNETSHROOM);
			}
			else if (aUseSeedType == SeedType::SEED_GLOOMSHROOM)
			{
				mBoard->DisplayAdvice(_S("[ADVICE_PLANT_NEEDS_FUMESHROOM]"), MessageStyle::MESSAGE_STYLE_HINT_LONG, AdviceType::ADVICE_PLANT_NEEDS_FUMESHROOM);
			}
			else if (aUseSeedType == SeedType::SEED_CATTAIL)
			{
				mBoard->DisplayAdvice(_S("[ADVICE_PLANT_NEEDS_LILYPAD]"), MessageStyle::MESSAGE_STYLE_HINT_LONG, AdviceType::ADVICE_PLANT_NEEDS_LILYPAD);
			}
			else
			{
				TOD_ASSERT();
			}

			return;
		}
	}

	mBoard->ClearAdvice(AdviceType::ADVICE_CANT_AFFORD_PLANT);
	mBoard->ClearAdvice(AdviceType::ADVICE_PLANT_NEEDS_REPEATER);
	mBoard->ClearAdvice(AdviceType::ADVICE_PLANT_NEEDS_MELONPULT);
	mBoard->ClearAdvice(AdviceType::ADVICE_PLANT_NEEDS_SUNFLOWER);
	mBoard->ClearAdvice(AdviceType::ADVICE_PLANT_NEEDS_KERNELPULT);
	mBoard->ClearAdvice(AdviceType::ADVICE_PLANT_NEEDS_SPIKEWEED);
	mBoard->ClearAdvice(AdviceType::ADVICE_PLANT_NEEDS_MAGNETSHROOM);
	mBoard->ClearAdvice(AdviceType::ADVICE_PLANT_NEEDS_FUMESHROOM);
	mBoard->ClearAdvice(AdviceType::ADVICE_PLANT_NEEDS_LILYPAD);

	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST)
	{
		mBoard->mChallenge->BeghouledPacketClicked(this);
	}
	else if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZOMBIQUARIUM)
	{
		mBoard->mChallenge->ZombiquariumPacketClicked(this);
	}
	else
	{
		mBoard->mCursorObject->mType = mPacketType;
		mBoard->mCursorObject->mImitaterType = mImitaterType;
		mBoard->mCursorObject->mCursorType = CursorType::CURSOR_TYPE_PLANT_FROM_BANK;
		mBoard->mCursorObject->mSeedBankIndex = mIndex;
		mApp->PlaySample(SOUND_SEEDLIFT);

		if (mBoard->mTutorialState == TutorialState::TUTORIAL_LEVEL_1_PICK_UP_PEASHOOTER)
		{
			mBoard->SetTutorialState(TutorialState::TUTORIAL_LEVEL_1_PLANT_PEASHOOTER);
		}
		else if (mBoard->mTutorialState == TutorialState::TUTORIAL_LEVEL_2_PICK_UP_SUNFLOWER)
		{
			if (mPacketType == SeedType::SEED_SUNFLOWER)
			{
				mBoard->SetTutorialState(TutorialState::TUTORIAL_LEVEL_2_PLANT_SUNFLOWER);
			}
			else
			{
				mBoard->SetTutorialState(TutorialState::TUTORIAL_LEVEL_2_REFRESH_SUNFLOWER);
			}
		}
		else if (mBoard->mTutorialState == TutorialState::TUTORIAL_MORESUN_PICK_UP_SUNFLOWER)
		{
			if (mPacketType == SeedType::SEED_SUNFLOWER)
			{
				mBoard->SetTutorialState(TutorialState::TUTORIAL_MORESUN_PLANT_SUNFLOWER);
			}
			else
			{
				mBoard->SetTutorialState(TutorialState::TUTORIAL_MORESUN_REFRESH_SUNFLOWER);
			}
		}
		else if (mBoard->mTutorialState == TutorialState::TUTORIAL_WHACK_A_ZOMBIE_PICK_SEED || mBoard->mTutorialState == TutorialState::TUTORIAL_WHACK_A_ZOMBIE_BEFORE_PICK_SEED)
		{
			mBoard->SetTutorialState(TutorialState::TUTORIAL_WHACK_A_ZOMBIE_COMPLETED);
		}

		Deactivate();
	}
}

void SeedPacket::WasPlanted()
{
	TOD_ASSERT(mPacketType != SeedType::SEED_NONE);

	if (mBoard->IsRollSeedBank())
	{
		// Purchased: the slot of the Chosen Seed Bank empties (the plant stays in the Chosen Plants pool).
		// The shop stays empty until its normal next-wave auto-roll.
		mTimesUsed++;
		EmptySlot();
		// The shop stays empty until its normal next-wave auto-roll; buying the last slot does not roll it.
		return;
	}

	if (mBoard->HasConveyorBeltSeedBank())
	{
		mBoard->mSeedBank->RemoveSeed(mIndex);
	}
	else if (mApp->IsSlotMachineLevel())
	{
		Deactivate();
	}
	else if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND && mBoard->mChallenge->mChallengeState != ChallengeState::STATECHALLENGE_LAST_STAND_ONSLAUGHT)
	{
		mTimesUsed++;
		mActive = true;
		FlashIfReady();
	}
	else
	{
		mTimesUsed++;
		mRefreshing = true;
		mRefreshTime = Plant::GetRefreshTime(mPacketType, mImitaterType);
	}
}

bool SeedPacket::MouseHitTest(int theX, int theY, HitResult* theHitResult)
{
	if (mSlotMachineCountDown > 0 || mPacketType == SeedType::SEED_NONE || (mIsBenchPacket && (mBenchMoving || mBoard->mBenchMergeActive)))
	{
		theHitResult->mObject = nullptr;
		theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_NONE;
		return false;
	}

	if (theX >= mX + mOffsetX && theX < mX + mOffsetX + mWidth && theY >= mY && theY < mY + mHeight)
	{
		theHitResult->mObject = this;
		theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_SEEDPACKET;
		return true;
	}

	theHitResult->mObject = nullptr;
	theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_NONE;
	return false;
}

SeedBank::SeedBank()
{
	mWidth = IMAGE_SEEDBANK->GetWidth();
	mHeight = IMAGE_SEEDBANK->GetHeight();

	mNumPackets = 0;
	mConveyorBeltCounter = 0;
	mCutSceneDarken = 255;
}

void SeedBank::Draw(Graphics* g)
{
	if (mBoard->mCutScene && mBoard->mCutScene->IsBeforePreloading())
	{
		return;
	}

	if (mApp->mGameScene != GameScenes::SCENE_PLAYING)
	{
		g->mTransX -= mBoard->mX;
		g->mTransY -= mBoard->mY;
	}

	if (mApp->IsSlotMachineLevel())
	{
		g->DrawImage(IMAGE_SUNBANK, 0, 0);
	}
	else if (mBoard->HasConveyorBeltSeedBank())
	{
		g->DrawImage(IMAGE_CONVEYORBELT_BACKDROP, 83, 0);
		g->DrawImageCel(IMAGE_CONVEYORBELT, 90, 63, mConveyorBeltCounter / 4 % 6);
		g->SetClipRect(90, 0, 501, BOARD_HEIGHT);
	}
	else
	{
		int aExtraWidth = mBoard->GetSeedBankExtraWidth();
		Rect theSrcRect(IMAGE_SEEDBANK->mWidth - aExtraWidth - 12, 0, aExtraWidth + 12, IMAGE_SEEDBANK->mHeight);
		g->DrawImage(IMAGE_SEEDBANK, 0, 0);
		g->DrawImage(IMAGE_SEEDBANK, IMAGE_SEEDBANK->mWidth - 12, 0, theSrcRect);
	}
	
	for (int i = 0; i < mNumPackets; i++)
	{
		SeedPacket* aSeedPacket = &mSeedPackets[i];
		if (aSeedPacket->mPacketType != SeedType::SEED_NONE && !mBoard->IsControllerSeedBankSelected(i) && aSeedPacket->BeginDraw(g))
		{
			aSeedPacket->Draw(g);
			aSeedPacket->EndDraw(g);
		}
	}
	if (!mApp->IsChallengeWithoutSeedBank() && mBoard->mSeedBank->mY == 0) //return false somehow when cutscene is still active while seedchooser animation
	{
		for (int i = 0; i < MAX_GAMEPADS; i++)
		{
			GamepadPlayer* aGamepadPlayer = mBoard->mGamepadPlayerList[i];
			int aIndex = aGamepadPlayer->mSeedBankIndex;
			if (aIndex <= -1 || mBoard->mCutScene->mSeedChoosing)
				continue;
			SeedPacket* aSeedPacket = &mSeedPackets[aIndex];
			float aScale = 1.05;
			int aSeedSelectorWidth = IMAGE_SEED_SELECTOR->mWidth * aScale;
			int aSeedSelectorHeight = IMAGE_SEED_SELECTOR->mHeight * aScale;
			int aOffset = 7;
			int aPosX = aSeedPacket->mX - aOffset;
			int aPosY = aSeedPacket->mY - aOffset;
			Color aOldColor = g->mColor;
			g->SetColorizeImages(true);
			g->SetColor(aGamepadPlayer->mColor);
			TodDrawImageScaledF(g, IMAGE_SEED_SELECTOR, aPosX, aPosY, aScale, aScale);
			g->SetColor(aOldColor);
			g->SetColorizeImages(false);
		}
	}
	for (int i = 0; i < mNumPackets; i++)
	{
		SeedPacket* aSeedPacket = &mSeedPackets[i];
		if (aSeedPacket->mPacketType != SeedType::SEED_NONE && mBoard->IsControllerSeedBankSelected(i) && aSeedPacket->BeginDraw(g))
		{
			aSeedPacket->Draw(g);
			aSeedPacket->EndDraw(g);
		}
	}

	g->ClearClipRect();
	if (mApp->IsSlotMachineLevel() && mY > -IMAGE_SEEDBANK->GetHeight())
	{
		g->DrawImage(IMAGE_SLOTMACHINE_OVERLAY, 189, -2);
	}

	if (!mBoard->HasConveyorBeltSeedBank())
	{
		SexyString aMoneyLabel = StrFormat(_S("%d"), max(mBoard->mSunMoney, 0));
		Color aMoneyColor(0, 0, 0);
		if (mBoard->mOutOfMoneyCounter > 0 && mBoard->mOutOfMoneyCounter % 20 < 10)
		{
			aMoneyColor = Color(255, 0, 0);
		}

		TodDrawString(g, aMoneyLabel, 34, 78, FONT_CONTINUUMBOLD14, aMoneyColor, DrawStringJustification::DS_ALIGN_CENTER);
	}

	if (mBoard->IsRollSeedBank() && mApp->mGameScene == GameScenes::SCENE_PLAYING)
	{
		DrawRollButton(g);
	}

	if (mApp->mGameScene != GameScenes::SCENE_PLAYING)
	{
		g->mTransX += mBoard->mX;
		g->mTransY += mBoard->mY;
	}
}

bool SeedBank::MouseHitTest(int x, int y, HitResult* theHitResult)
{
	if (x - mX <= mWidth - 5 && mNumPackets > 0)
	{
		for (SeedPacket& aSeedPacket : mSeedPackets)
		{
			if (aSeedPacket.MouseHitTest(x - mX, y - mY, theHitResult))
			{
				return true;
			}
		}
	}

	theHitResult->mObject = nullptr;
	theHitResult->mObjectType = GameObjectType::OBJECT_TYPE_NONE;
	return false;
}

bool SeedBank::ContainsPoint(int theX, int theY)
{
	return theX >= mX && theX < mX + mWidth && theY >= mY && theY < mY + mHeight;
}

void SeedBank::AddSeed(SeedType theSeedType, bool thePlaceOnLeft)
{
	TOD_ASSERT(mBoard->HasConveyorBeltSeedBank());
	TOD_ASSERT(theSeedType != SeedType::SEED_NONE);

	int aNumSeeds = GetNumSeedsOnConveyorBelt();
	if (aNumSeeds == mNumPackets)
	{
		return;
	}

	SeedPacket* aSeedPacket = &mSeedPackets[aNumSeeds];
	aSeedPacket->mPacketType = theSeedType;
	aSeedPacket->mRefreshCounter = 0;
	aSeedPacket->mRefreshTime = 0;
	aSeedPacket->mRefreshing = false;
	aSeedPacket->mActive = true;
	aSeedPacket->mOffsetX = 515 - (SEED_PACKET_WIDTH + 1) * aNumSeeds;
	if (thePlaceOnLeft)
	{
		aSeedPacket->mOffsetX = 0;
	}
	if (aNumSeeds > 0)
	{
		SeedPacket* aPrevPacket = &mSeedPackets[aNumSeeds - 1];
		if (aSeedPacket->mOffsetX < aPrevPacket->mOffsetX)
		{
			aSeedPacket->mOffsetX = aPrevPacket->mOffsetX + 40;
		}
	}
}

void SeedBank::RemoveSeed(int theIndex)
{
	TOD_ASSERT(mBoard->HasConveyorBeltSeedBank());
	TOD_ASSERT(theIndex >= 0 && theIndex < GetNumSeedsOnConveyorBelt());

	for (int i = theIndex; i < mNumPackets; i++)
	{
		SeedPacket* aSeedPacket = &mSeedPackets[i];
		if (aSeedPacket->mPacketType == SeedType::SEED_NONE)
		{
			break;
		}

		if (i == mNumPackets - 1)
		{
			aSeedPacket->mPacketType = SeedType::SEED_NONE;
			aSeedPacket->mOffsetX = 0;
		}
		else
		{
			SeedPacket* aNextPacket = &mSeedPackets[i + 1];
			aSeedPacket->mPacketType = aNextPacket->mPacketType;
			aSeedPacket->mOffsetX = aNextPacket->mOffsetX + SEED_PACKET_WIDTH + 1;
		}

		aSeedPacket->mRefreshCounter = 0;
		aSeedPacket->mRefreshTime = 0;
		aSeedPacket->mRefreshing = false;
		aSeedPacket->mActive = true;
	}
}

int SeedBank::GetNumSeedsOnConveyorBelt()
{
	for (int i = 0; i < mNumPackets; i++)
	{
		if (mSeedPackets[i].mPacketType == SeedType::SEED_NONE)
		{
			return i;
		}
	}
	return mNumPackets;
}

int SeedBank::CountOfTypeOnConveyorBelt(SeedType theSeedType)
{
	int aCount = 0;
	for (int i = 0; i < mNumPackets; i++)
	{
		if (mSeedPackets[i].mPacketType == theSeedType)
		{
			aCount++;
		}
	}
	return aCount;
}

void SeedPacket::SetPacketType(SeedType theSeedType, SeedType theImitaterType)
{
	mPacketType = theSeedType;
	mImitaterType = theImitaterType;
	mRefreshCounter = 0;
	mRefreshTime = 0;
	mRefreshing = false;
	mActive = true;

	if (mBoard->IsRollSeedBank())
	{
		return;		// roll seed banks have no recharge countdowns
	}

	SeedType aUseSeedType = theSeedType;
	if (theSeedType == SeedType::SEED_IMITATER && theImitaterType != SeedType::SEED_NONE)
	{
		aUseSeedType = theImitaterType;
	}

	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_BEGHOULED_TWIST ||
		mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZOMBIQUARIUM || mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_LAST_STAND ||
		mApp->IsIZombieLevel() || mApp->IsScaryPotterLevel() || mApp->IsWhackAZombieLevel() || (mApp->IsSurvivalMode() && mBoard->mChallenge->mSurvivalStage > 0))
		return;

	if ((Plant::IsUpgrade(aUseSeedType) && !gLawnApp->IsSurvivalMode()) || Plant::GetRefreshTime(mPacketType, mImitaterType) == 5000)
	{
		mRefreshTime = 3500;
		mRefreshing = true;
		mActive = false;
	}
	else if (Plant::IsUpgrade(aUseSeedType) && gLawnApp->IsSurvivalMode())
	{
		mRefreshTime = 8000;
		mRefreshing = true;
		mActive = false;
	}
	else if (Plant::GetRefreshTime(mPacketType, mImitaterType) == 3000)
	{
		mRefreshTime = 2000;
		mRefreshing = true;
		mActive = false;
	}
}

void SeedBank::UpdateConveyorBelt()
{
	mConveyorBeltCounter++;
	if (mConveyorBeltCounter % CONVEYOR_SPEED == 0)
	{
		for (int i = 0; i < mNumPackets; i++)
		{
			SeedPacket* aSeedPacket = &mSeedPackets[i];
			if (aSeedPacket->mOffsetX > 0)
			{
				aSeedPacket->mOffsetX = max(aSeedPacket->mOffsetX - 1, 0);
			}
		}

		mBoard->UpdateToolTip();
	}
}

void SeedBank::UpdateWidth()
{
    // GetNumSeedsInBank() is the normal seed chooser capacity, not the Roll Shop display count.
    // Use the Board's mode-aware count so the shop remains capped at five plant slots.
    mNumPackets = mBoard->GetNumSeedBankSlots();
    mWidth = IMAGE_SEEDBANK->GetWidth() + mBoard->GetSeedBankExtraWidth();
    for (int i = 0; i < mNumPackets; i++)
    {
        mSeedPackets[i].mX = mBoard->GetSeedBankSlotX(i);
    }
}

void SeedBank::RefreshAllPackets()
{
	for (int i = 0; i < mNumPackets; i++)
	{
		SeedPacket* aSeedPacket = &mSeedPackets[i];
		if (aSeedPacket->mPacketType == SeedType::SEED_NONE)
		{
			break;
		}

		if (aSeedPacket->mRefreshing)
		{
			aSeedPacket->mRefreshCounter = 0;
			aSeedPacket->mRefreshing = false;
			aSeedPacket->mActive = true;
			aSeedPacket->FlashIfReady();
		}
	}
}

void SeedPacket::SetFromChosenPlant(int theChosenPlantIndex)
{
    TOD_ASSERT(theChosenPlantIndex >= 0 && theChosenPlantIndex < mBoard->mNumChosenPlants);

    // Replacing a visible offer releases its reservation first (e.g. Lucky Roll).
    if (mPacketType != SeedType::SEED_NONE || mPoolCardCount > 0)
        EmptySlot();
    const ChosenPlant& aChosenPlant = mBoard->mChosenPlants[theChosenPlantIndex];
    SetPacketType(aChosenPlant.mSeedType, aChosenPlant.mImitaterType);
    if (mBoard->IsRollSeedBank())
    {
        if (!mBoard->ReservePlantPoolCard(aChosenPlant.mSeedType))
        {
            EmptySlot();
            return;
        }
        mPoolCardCount = 1;
        mStarLevel = 1;
    }
    mChosenPlantIndex = theChosenPlantIndex;
}

void SeedPacket::EmptySlot()
{
    if (mPoolCardCount > 0 && mPacketType != SeedType::SEED_NONE && mBoard != nullptr && mBoard->IsRollSeedBank())
        mBoard->ReturnPlantPoolCard(mPacketType, mPoolCardCount);
    mPoolCardCount = 0;
    mSunSpent = 0;
    mStarLevel = 1;
    mBenchMoving = false;
    mBenchMoveDurationMs = ROLL_BENCH_ANIMATION_MS;
	mPacketType = SeedType::SEED_NONE;
	mImitaterType = SeedType::SEED_NONE;
	mChosenPlantIndex = -1;
	mRefreshCounter = 0;
	mRefreshTime = 0;
	mRefreshing = false;
	mActive = false;
	mLocked = false;
	mRollAnim = 0;
}

void SeedBank::RollAllSlots(bool theAnimate, bool theGuaranteeSun, bool theForcePeashooterFirstSlot)
{
    const int aNumChosenPlants = mBoard->mNumChosenPlants;
    const bool aFirstAdventureLevel = mBoard->mApp->IsAdventureMode() && !mBoard->mApp->mPlayingQuickplay && mBoard->mLevel == 1;
    const bool aForcePeashooter = aFirstAdventureLevel && theGuaranteeSun;
    // The first-run lesson needs one Sunflower to plant and TWO more to merge in Bench.
    // These are normal Shop reservations taken from the finite shared Plant Pool.
    const bool aTeachBenchMerge = aForcePeashooter && mBoard->mApp->mPlayerInfo != nullptr &&
        mBoard->mApp->mPlayerInfo->mPurchases[ROLL_TUTORIAL_SEEN_SLOT] < ROLL_TUTORIAL_VERSION;
    const int aTutorialSunTarget = aTeachBenchMerge ? 3 : ROLL_MIN_SUN_PLANTS;
    const int aPeashooterSlot = aTeachBenchMerge ? 3 : 2;
    int aPeashooterPoolIndex = -1;
    for (int i = 0; i < aNumChosenPlants; ++i)
        if (mBoard->mChosenPlants[i].mSeedType == SeedType::SEED_PEASHOOTER)
            aPeashooterPoolIndex = i;

    // A selected Bench packet is not a Shop reservation. CancelHeldBankPlant handles
    // only actual nonnegative Shop indexes and safely ignores encoded Bench indexes.
    mBoard->CancelHeldBankPlant();

    // Locked offers keep their reserved card. Every other visible offer is released
    // before rolling, so the shared pool never creates more copies than its cap.
    for (int i = 0; i < mNumPackets; ++i)
    {
        SeedPacket& aPacket = mSeedPackets[i];
        if (aPacket.mLocked && aPacket.mPacketType != SeedType::SEED_NONE)
        {
            continue;
        }
        aPacket.EmptySlot();
    }

    int aGuaranteedSunCount = 0;
    for (int i = 0; i < mNumPackets; ++i)
        if (mSeedPackets[i].mLocked &&
            (mSeedPackets[i].mPacketType == SeedType::SEED_SUNFLOWER ||
             mSeedPackets[i].mPacketType == SeedType::SEED_SUNSHROOM ||
             mSeedPackets[i].mPacketType == SeedType::SEED_TWINSUNFLOWER))
            ++aGuaranteedSunCount;
    for (int i = 0; i < mNumPackets; ++i)
    {
        SeedPacket& aPacket = mSeedPackets[i];
        if (aPacket.mLocked && aPacket.mPacketType != SeedType::SEED_NONE)
            continue;

        if (aNumChosenPlants <= 0)
            continue;

        if (theGuaranteeSun && aGuaranteedSunCount < aTutorialSunTarget)
        {
            int aCandidates[NUM_SEEDS_IN_CHOOSER];
            int aCandidateWeights[NUM_SEEDS_IN_CHOOSER];
            int aCandidateCount = 0;
            int aCandidateWeight = 0;
            for (int j = 0; j < aNumChosenPlants; ++j)
            {
                const SeedType aPlant = mBoard->mChosenPlants[j].mSeedType;
                if (aPlant != SeedType::SEED_SUNFLOWER && aPlant != SeedType::SEED_SUNSHROOM &&
                    aPlant != SeedType::SEED_TWINSUNFLOWER)
                    continue;
                if (mBoard->IsSeedBannedFromShop(aPlant, mBoard->mChosenPlants[j].mImitaterType))
                    continue;
                const int aCopies = mBoard->mSeedPoolRemaining[(int)aPlant];
                if (aCopies > 0)
                {
                    aCandidates[aCandidateCount] = j;
                    aCandidateWeights[aCandidateCount++] = aCopies;
                    aCandidateWeight += aCopies;
                }
            }
            if (aCandidateWeight > 0)
            {
                int aTicket = Rand(aCandidateWeight);
                int aChosenCandidate = 0;
                for (; aChosenCandidate + 1 < aCandidateCount; ++aChosenCandidate)
                {
                    if (aTicket < aCandidateWeights[aChosenCandidate]) break;
                    aTicket -= aCandidateWeights[aChosenCandidate];
                }
                aPacket.SetFromChosenPlant(aCandidates[aChosenCandidate]);
                if (aPacket.mPacketType != SeedType::SEED_NONE)
                {
                    ++aGuaranteedSunCount;
                    if (theAnimate) aPacket.mRollAnim = ROLL_ANIM_FRAMES + i * ROLL_ANIM_STAGGER;
                    continue;
                }
            }
        }

        if (i == aPeashooterSlot && aForcePeashooter && aPeashooterPoolIndex >= 0 &&
            mBoard->mSeedPoolRemaining[(int)SeedType::SEED_PEASHOOTER] > 0 &&
            !mBoard->IsSeedBannedFromShop(SeedType::SEED_PEASHOOTER, SeedType::SEED_NONE))
        {
            aPacket.SetFromChosenPlant(aPeashooterPoolIndex);
            if (aPacket.mPacketType != SeedType::SEED_NONE)
            {
                    if (theAnimate)
                    aPacket.mRollAnim = ROLL_ANIM_FRAMES + i * ROLL_ANIM_STAGGER;
            }
            continue;
        }

        // Finite-pool adaptive tier draw, shared by Shop and Seed Surprise.
        const int aChosenTier = mBoard->RollAvailableTier();
        if (aChosenTier == 0) continue; // No eligible copies remain in any tier.

        // Draw from the finite tier pool without replacement. Remaining card copies
        // determine card weights, but must never affect the tier distribution.
        int aCardWeightTotal = 0;
        for (int aPool = 0; aPool < aNumChosenPlants; ++aPool)
        {
            const ChosenPlant& aPlant = mBoard->mChosenPlants[aPool];
            if (mBoard->GetPlantTier(aPlant.mSeedType) != aChosenTier ||
                mBoard->IsSeedBannedFromShop(aPlant.mSeedType, aPlant.mImitaterType))
                continue;
            int aRemaining = mBoard->mSeedPoolRemaining[(int)aPlant.mSeedType];
            if (aRemaining <= 0)
                continue;
            int aMultiplier = 100;
            aCardWeightTotal += aRemaining * aMultiplier;
        }
        if (aCardWeightTotal <= 0)
            continue;

        int aCardRoll = Rand(aCardWeightTotal);
        int aChosenPlantIndex = -1;
        for (int aPool = 0; aPool < aNumChosenPlants; ++aPool)
        {
            const ChosenPlant& aPlant = mBoard->mChosenPlants[aPool];
            if (mBoard->GetPlantTier(aPlant.mSeedType) != aChosenTier ||
                mBoard->IsSeedBannedFromShop(aPlant.mSeedType, aPlant.mImitaterType))
                continue;
            int aRemaining = mBoard->mSeedPoolRemaining[(int)aPlant.mSeedType];
            if (aRemaining <= 0)
                continue;
            int aMultiplier = 100;
            int aWeight = aRemaining * aMultiplier;
            if (aCardRoll < aWeight)
            {
                aChosenPlantIndex = aPool;
                break;
            }
            aCardRoll -= aWeight;
        }
        if (aChosenPlantIndex < 0)
            continue;

        aPacket.SetFromChosenPlant(aChosenPlantIndex);
        if (aPacket.mPacketType != SeedType::SEED_NONE)
        {
            if (theAnimate)
                aPacket.mRollAnim = ROLL_ANIM_FRAMES + i * ROLL_ANIM_STAGGER;
        }
    }

    mBoard->CheckLuckyShopAchievement();

    // Lock is a one-reroll hold: preserve locked contents during this roll, then clear every lock.
    for (int i = 0; i < mNumPackets; ++i)
        mSeedPackets[i].mLocked = false;

}

int SeedBank::CountLockedSlots()
{
	int aCount = 0;
	for (int i = 0; i < mNumPackets; i++)
	{
		if (mSeedPackets[i].mLocked && mSeedPackets[i].mPacketType != SeedType::SEED_NONE)
		{
			aCount++;
		}
	}
	return aCount;
}

bool SeedBank::AreAllSlotsEmpty()
{
	for (int i = 0; i < mNumPackets; i++)
	{
		if (mSeedPackets[i].mPacketType != SeedType::SEED_NONE)
		{
			return false;
		}
	}
	return true;
}

// The Reroll button is a slot of its own in front of the shop slots (it isn't one of the mNumPackets plant slots).
Rect SeedBank::GetRollButtonRect()
{
	return Rect(mBoard->GetSeedPacketPositionX(0), mSeedPackets[0].mY, SEED_PACKET_WIDTH, SEED_PACKET_HEIGHT);
}

// Draws the Reroll slot with the rotating-arrow thumbnail of the Beghouled shuffle button.
void SeedBank::DrawRollButton(Graphics* g)
{
	Rect aRect = GetRollButtonRect();

    // During Shop Holiday show the native Reroll packet disabled, not a
    // custom black overlay. Purchase/Reroll guards remain authoritative.

	int aRollCost = mBoard->GetRollCost();
	bool aIsFree = aRollCost == 0;
	bool aCanAfford = !mBoard->IsShopClosed() && (aIsFree || mBoard->CanTakeSunMoney(aRollCost));
	bool aHover = !mBoard->IsShopClosed() && mBoard->CanUseRollButton() &&
		mBoard->IsOverRollButton(mApp->mWidgetManager->mLastMouseX - mBoard->mX, mApp->mWidgetManager->mLastMouseY - mBoard->mY);

	gSeedPacketCostOverride = aRollCost;
	DrawSeedPacket(g, (float)aRect.mX, (float)aRect.mY, SeedType::SEED_BEGHOULED_BUTTON_SHUFFLE, SeedType::SEED_NONE, 0.0f, aCanAfford ? 255 : 115, true, false);
	gSeedPacketCostOverride = -1;

	if (aHover && aCanAfford)
	{
		g->SetColor(Color(255, 240, 150));
		g->DrawRect(aRect.mX, aRect.mY, aRect.mWidth - 1, aRect.mHeight - 1);
		g->DrawRect(aRect.mX + 1, aRect.mY + 1, aRect.mWidth - 3, aRect.mHeight - 3);
	}

	// stored free rerolls
	if (mBoard->mFreeRolls > 0 && !mApp->mEasyPlantingCheat)
	{
		TodDrawString(g, StrFormat(_S("x%d"), mBoard->mFreeRolls), aRect.mX + aRect.mWidth - 12, aRect.mY + 13, FONT_BRIANNETOD12, Color(120, 255, 120), DrawStringJustification::DS_ALIGN_CENTER);
	}
}
