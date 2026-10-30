#include "DataSync.h"
#include "PlayerInfo.h"
#include "../LawnCommon.h"
#include "../Widget/ChallengeScreen.h"
#include "../../Sexy.TodLib/TodDebug.h"
#include "../../Sexy.TodLib/TodCommon.h"
#include "../../SexyAppFramework/Buffer.h"
#include "../../SexyAppFramework/SexyAppBase.h"

static int gUserVersion = 16;

PlayerInfo::PlayerInfo()
{
	Reset();
}

void PlayerInfo::SyncSummary(DataSync& theSync)
{
	theSync.SyncString(mName);
	theSync.SyncLong(mUseSeq);
	theSync.SyncLong(mId);
}

void PlayerInfo::SyncDetails(DataSync& theSync)
{
	if (theSync.GetReader())
	{
		Reset();
	}

	int aVersion = gUserVersion;
	theSync.SyncLong(aVersion);
	theSync.SetVersion(aVersion);
	// Version 12 is the existing released format. Keep reading each historical
	// layout exactly, then migrate the old 10-dollar currency units to direct dollars.
	if (aVersion != 12 && aVersion != 13 && aVersion != 14 && aVersion != 15 && aVersion != gUserVersion)
	{
		return;
	}

	theSync.SyncLong(mLevel);
	theSync.SyncLong(mCoins);
	theSync.SyncLong(mFinishedAdventure);
	for (int i = 0; i < 100; i++)
	{
		theSync.SyncLong(mChallengeRecords[i]);
	}
	for (int i = 0; i < 80; i++)
	{
		theSync.SyncLong(mPurchases[i]);
	}
	theSync.SyncLong(mPlayTimeActivePlayer);
	theSync.SyncLong(mPlayTimeInactivePlayer);
	theSync.SyncLong(mHasUsedCheatKeys);
	theSync.SyncLong(mHasWokenStinky);
	theSync.SyncLong(mDidntPurchasePacketUpgrade);
	theSync.SyncLong(mLastStinkyChocolateTime);
	theSync.SyncLong(mStinkyPosX);
	theSync.SyncLong(mStinkyPosY);
	theSync.SyncLong(mHasUnlockedMinigames);
	theSync.SyncLong(mHasUnlockedPuzzleMode);
	theSync.SyncLong(mHasNewMiniGame);
	theSync.SyncLong(mHasNewScaryPotter);
	theSync.SyncLong(mHasNewIZombie);
	theSync.SyncLong(mHasNewSurvival);
	theSync.SyncLong(mHasUnlockedSurvivalMode);
	theSync.SyncLong(mNeedsMessageOnGameSelector);
	theSync.SyncLong(mNeedsMagicTacoReward);
	theSync.SyncLong(mHasSeenStinky);
	theSync.SyncLong(mHasSeenUpsell);
	theSync.SyncLong(mPlaceHolderPlayerStats);
	theSync.SyncLong(mNumPottedPlants);
	
	TOD_ASSERT(mNumPottedPlants <= MAX_POTTED_PLANTS);
	for (int i = 0; i < mNumPottedPlants; i++)
	{
		theSync.SyncBytes(&mPottedPlant[i], sizeof(PottedPlant));
	}
	const int aAchievementCount = aVersion >= 13 ? NUM_ACHIEVEMENTS : 20;
	for (int i = 0; i < aAchievementCount; i++)
	{
		theSync.SyncBool(mEarnedAchievements[i]);
	}
	for (int i = 0; i < aAchievementCount; i++)
	{
		theSync.SyncBool(mShownedAchievements[i]);
	}
	if (aVersion >= 13)
	{
		theSync.SyncString(mDiscoveredAugments);
	}
	if (aVersion >= 14)
	{
		theSync.SyncString(mAugmentDraftKey);
		theSync.SyncString(mAugmentDraftData);
	}
	// Save version 15 appended the old 0..9 dollar remainder to mCoins * 10.
	// Version 16 stores the whole dollar balance directly in mCoins and no longer
	// serializes that legacy remainder.
	if (aVersion == 15)
	{
		theSync.SyncLong(mCoinRemainder);
		mCoinRemainder = max(0, min(9, mCoinRemainder));
	}
	if (aVersion >= 16)
	{
		theSync.SyncLong(mRollSlotRoundsRemaining);
		mRollSlotRoundsRemaining = max(0, min(3, mRollSlotRoundsRemaining));
	}
	else if (theSync.GetReader())
	{
		mRollSlotRoundsRemaining = 0;
	}

	if (theSync.GetReader() && aVersion <= 15)
	{
		// Preserve the balance the player saw before this update, converting it once.
		long long aDollars = (long long)mCoins * 10LL + (long long)mCoinRemainder;
		mCoins = (int)max(0LL, min(999999LL, aDollars));
		mCoinRemainder = 0;
		mCurrencyMigrationPending = true;
	}
}

void PlayerInfo::LoadDetails()
{
	try
	{
		Buffer aBuffer;
		std::string aFileName = GetAppDataFolder() + StrFormat("userdata/user%d.dat", mId);
		if (!gSexyAppBase->ReadBufferFromFile(aFileName, &aBuffer, false))
		{
			return;
		}

		DataReader aReader;
		aReader.OpenMemory(aBuffer.GetDataPtr(), aBuffer.GetDataLen(), false);
		DataSync aSync(aReader);
		SyncDetails(aSync);
		if (mCurrencyMigrationPending)
		{
			mCurrencyMigrationPending = false;
			SaveDetails(); // Rewrite the old save once using the new dollar unit.
		}
	}
	catch (DataReaderException&)
	{
		TodTrace("Failed to get player data, resetting it\n");
		TodErrorMessageBox("The save data is corrupted. \nResetting", _S("Error"));
		Reset();
	}
}

void PlayerInfo::SaveDetails()
{
	DataWriter aWriter;
	aWriter.OpenMemory();
	DataSync aSync(aWriter);
	SyncDetails(aSync);

	MkDir(GetAppDataFolder() + "userdata");
	std::string aFileName = GetAppDataFolder() + StrFormat("userdata/user%d.dat", mId);
	gSexyAppBase->WriteBytesToFile(aFileName, aWriter.GetDataPtr(), aWriter.GetDataLen());
}

void PlayerInfo::DeleteUserFiles()
{
	std::string aFilename = GetAppDataFolder() + StrFormat("userdata/user%d.dat", mId);
	gSexyAppBase->EraseFile(aFilename);

	for (int i = 0; i < (int)GameMode::NUM_GAME_MODES; i++)
	{
		std::string aFileName = GetSavedGameName((GameMode)i, mId);
		gSexyAppBase->EraseFile(aFileName);
	}
}

void PlayerInfo::Reset()
{
	mLevel = 1;
	mCoins = 0;
	mCoinRemainder = 0;
	mRollSlotRoundsRemaining = 0;
	mCurrencyMigrationPending = false;
	mFinishedAdventure = 0;
	memset(mChallengeRecords, 0, sizeof(mChallengeRecords));
	memset(mPurchases, 0, sizeof(mPurchases));
	mPlayTimeActivePlayer = 0;
	mPlayTimeInactivePlayer = 0;
	mHasUsedCheatKeys = 0;
	mHasWokenStinky = 0;
	mDidntPurchasePacketUpgrade = 0;
	mLastStinkyChocolateTime = 0;
	mStinkyPosX = 0;
	mStinkyPosY = 0;
	mHasUnlockedMinigames = 0;
	mHasUnlockedPuzzleMode = 0;
	mHasNewMiniGame = 0;
	mHasNewScaryPotter = 0;
	mHasNewIZombie = 0;
	mHasNewSurvival = 0;
	mHasUnlockedSurvivalMode = 0;
	mNeedsMessageOnGameSelector = 0;
	mNeedsMagicTacoReward = 0;
	mHasSeenStinky = 0;
	mHasSeenUpsell = 0;
	mPlaceHolderPlayerStats = 0;
	memset(mPottedPlant, 0, sizeof(mPottedPlant));
	mNumPottedPlants = 0;
	memset(mEarnedAchievements, 0, sizeof(mEarnedAchievements));
	memset(mShownedAchievements, 0, sizeof(mShownedAchievements));
	mDiscoveredAugments.clear();
	mAugmentDraftKey.clear();
	mAugmentDraftData.clear();
}

bool PlayerInfo::HasDiscoveredAugment(const SexyString& theAugmentId) const
{
	if (theAugmentId.empty() || mDiscoveredAugments.empty())
		return false;
	SexyString aToken = _S("|") + theAugmentId + _S("|");
	return mDiscoveredAugments.find(aToken) != SexyString::npos;
}

bool PlayerInfo::MarkAugmentDiscovered(const SexyString& theAugmentId)
{
	if (theAugmentId.empty() || HasDiscoveredAugment(theAugmentId))
		return false;
	if (mDiscoveredAugments.empty())
		mDiscoveredAugments = _S("|");
	mDiscoveredAugments += theAugmentId;
	mDiscoveredAugments += _S("|");
	return true;
}

void PlayerInfo::AddCoins(int theAmount)
{
	long long aNewTotal = (long long)mCoins + theAmount;
	mCoins = (int)max(0LL, min(999999LL, aNewTotal));
	mCoinRemainder = 0;
}

int PlayerInfo::GetMoneyDollars() const
{
	return max(0, min(999999, mCoins));
}

void PlayerInfo::AddMoneyDollars(int theAmount)
{
	// Kept for call-site clarity; this is now the same direct-dollar unit as AddCoins.
	AddCoins(theAmount);
}

void PlayerInfo::ResetChallengeRecord(GameMode theGameMode)
{
	int aGameMode = (int)theGameMode - (int)GameMode::GAMEMODE_SURVIVAL_NORMAL_STAGE_1;
	TOD_ASSERT(aGameMode >= 0 && aGameMode <= NUM_CHALLENGE_MODES);
	mChallengeRecords[aGameMode] = 0;
}

void PottedPlant::InitializePottedPlant(SeedType theSeedType)
{
	memset(this, 0, sizeof(PottedPlant));
	mSeedType = theSeedType;
	mDrawVariation = DrawVariation::VARIATION_NORMAL;
	mLastWateredTime = 0i64;
	mFacing = (FacingDirection)RandRangeInt((int)FacingDirection::FACING_RIGHT, (int)FacingDirection::FACING_LEFT);
	mPlantAge = PottedPlantAge::PLANTAGE_SPROUT;
	mTimesFed = 0;
	mWhichZenGarden = GardenType::GARDEN_MAIN;
	mFeedingsPerGrow = RandRangeInt(3, 5);
	mPlantNeed = PottedPlantNeed::PLANTNEED_NONE;
	mLastNeedFulfilledTime = 0i64;
	mLastFertilizedTime = 0i64;
	mLastChocolateTime = 0i64;
}
