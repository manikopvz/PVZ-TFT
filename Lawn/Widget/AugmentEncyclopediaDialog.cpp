#include "AugmentEncyclopediaDialog.h"

#include "GameButton.h"
#include "../Board.h"
#include "../Plant.h"
#include "../SeedPacket.h"
#include "../System/PlayerInfo.h"
#include "../../LawnApp.h"
#include "../../Resources.h"
#include "../../Sexy.TodLib/TodCommon.h"
#include "../../Sexy.TodLib/TodStringFile.h"
#include "../../SexyAppFramework/Font.h"
#include "../../SexyAppFramework/Graphics.h"
#include "../../SexyAppFramework/Slider.h"
#include "../../SexyAppFramework/WidgetManager.h"

namespace
{
    const int AUGMENT_CATALOG_ICON_COLUMNS = 8;
    const int AUGMENT_CATALOG_ICON_X_STEP = SEED_PACKET_WIDTH + 2;
    const int AUGMENT_CATALOG_ICON_Y_STEP = SEED_PACKET_HEIGHT + 8;
    const int AUGMENT_CATALOG_LIST_X = BOARD_ADDITIONAL_WIDTH;
    const int AUGMENT_CATALOG_LIST_Y = SEED_PACKET_HEIGHT + 8 + 14 + BOARD_OFFSET_Y;
    const int AUGMENT_CATALOG_LIST_WIDTH = (SEED_PACKET_WIDTH + 2) / 2 +
        (AUGMENT_CATALOG_ICON_COLUMNS - 1) * AUGMENT_CATALOG_ICON_X_STEP + SEED_PACKET_WIDTH;
    const int AUGMENT_CATALOG_LIST_HEIGHT = 460;
    // Match the original Almanac footer: category buttons left, Close button right.
    const Rect AUGMENT_CATALOG_PLANTS_TAB(32 + BOARD_ADDITIONAL_WIDTH, 567 + BOARD_OFFSET_Y, 164, 26);
    const Rect AUGMENT_CATALOG_GIFTS_TAB(206 + BOARD_ADDITIONAL_WIDTH, 567 + BOARD_OFFSET_Y, 164, 26);
    const int AUGMENT_CATALOG_CARD_X = 459 + BOARD_ADDITIONAL_WIDTH;
    const int AUGMENT_CATALOG_CARD_Y = 86 + BOARD_OFFSET_Y;
    const int AUGMENT_CATALOG_GROUND_X = 521 + BOARD_ADDITIONAL_WIDTH;
    const int AUGMENT_CATALOG_GROUND_Y = 107 + BOARD_OFFSET_Y;
    const int AUGMENT_CATALOG_PREVIEW_X = (int)(578.0f + BOARD_ADDITIONAL_WIDTH);
    const int AUGMENT_CATALOG_PREVIEW_Y = (int)(140.0f + BOARD_OFFSET_Y);

    bool IsAugmentInTab(int theAugmentIndex, int theTab)
    {
        if (theAugmentIndex < 0 || theAugmentIndex >= RollAugment::NUM_AUGMENTS)
            return false;
        bool aPlantAugment = gAugmentDefs[theAugmentIndex].mSeedType != SeedType::SEED_NONE;
        return theTab == 0 ? aPlantAugment : !aPlantAugment;
    }

    int GetVisibleAugmentCount(int theTab)
    {
        int aCount = 0;
        for (int i = 0; i < RollAugment::NUM_AUGMENTS; ++i)
            if (IsAugmentInTab(i, theTab))
                ++aCount;
        return aCount;
    }

    int GetVisibleAugmentIndex(int theTab, int theVisibleIndex)
    {
        int aVisibleIndex = 0;
        for (int i = 0; i < RollAugment::NUM_AUGMENTS; ++i)
        {
            if (!IsAugmentInTab(i, theTab))
                continue;
            if (aVisibleIndex == theVisibleIndex)
                return i;
            ++aVisibleIndex;
        }
        return -1;
    }

    int GetFirstAugmentForTab(int theTab)
    {
        return GetVisibleAugmentIndex(theTab, 0);
    }

    void GetAugmentIconPosition(int theVisibleIndex, int theScrollOffset, int& x, int& y)
    {
        const int aColumn = theVisibleIndex % AUGMENT_CATALOG_ICON_COLUMNS;
        const int aRow = theVisibleIndex / AUGMENT_CATALOG_ICON_COLUMNS;
        // These offsets mirror AlmanacDialog::GetSeedPosition for a real SeedPacket.
        x = BOARD_ADDITIONAL_WIDTH + aColumn * AUGMENT_CATALOG_ICON_X_STEP + AUGMENT_CATALOG_ICON_X_STEP / 2;
        y = aRow * AUGMENT_CATALOG_ICON_Y_STEP + AUGMENT_CATALOG_ICON_Y_STEP + 14 - theScrollOffset + BOARD_OFFSET_Y;
    }

    int GetAugmentMaxScrollOffset(int theTab, int theListHeight)
    {
        const int aCount = GetVisibleAugmentCount(theTab);
        const int aRows = (aCount + AUGMENT_CATALOG_ICON_COLUMNS - 1) / AUGMENT_CATALOG_ICON_COLUMNS;
        if (aRows <= 0)
            return 0;
        const int aContentHeight = (aRows - 1) * AUGMENT_CATALOG_ICON_Y_STEP + SEED_PACKET_HEIGHT;
        return max(0, aContentHeight - theListHeight);
    }
}

AugmentEncyclopediaDialog::AugmentEncyclopediaDialog(LawnApp* theApp)
    : LawnDialog(theApp, Dialogs::DIALOG_AUGMENT_ENCYCLOPEDIA, true, _S(""), _S(""), _S(""), BUTTONS_NONE)
{
    mDrawStandardBack = false;
    Resize(0, 0, BOARD_WIDTH, BOARD_HEIGHT);
    mListRect = Rect(AUGMENT_CATALOG_LIST_X, AUGMENT_CATALOG_LIST_Y, AUGMENT_CATALOG_LIST_WIDTH, AUGMENT_CATALOG_LIST_HEIGHT);
    mScrollOffset = 0;
    mSelectedTab = 0;
    mMaxScrollOffset = GetAugmentMaxScrollOffset(mSelectedTab, mListRect.mHeight);
    mSelectedAugment = GetFirstAugmentForTab(mSelectedTab);
    mHoverAugment = -1;
    mLastMouseX = mLastMouseY = 0;
    mPreviewPlant = nullptr;
    mPlantsTabButton = nullptr;
    mGiftsTabButton = nullptr;
    mAugmentSlider = nullptr;

    TodLoadResources("DelayLoad_Almanac");
    mCloseButton = new GameButton(0);
    mCloseButton->mLabel = _S("[CLOSE_BUTTON]");
    mCloseButton->mButtonImage = Sexy::IMAGE_ALMANAC_CLOSEBUTTON;
    mCloseButton->mOverImage = Sexy::IMAGE_ALMANAC_CLOSEBUTTONHIGHLIGHT;
    mCloseButton->mDownImage = nullptr;
    mCloseButton->SetFont(Sexy::FONT_BRIANNETOD12);
    Color aButtonColor(42, 42, 90);
    mCloseButton->mColors[GameButton::COLOR_LABEL] = aButtonColor;
    mCloseButton->mColors[GameButton::COLOR_LABEL_HILITE] = aButtonColor;
    mCloseButton->Resize(676 + BOARD_ADDITIONAL_WIDTH, 567 + BOARD_OFFSET_Y, 89, 26);
    mCloseButton->mParentWidget = this;
    mCloseButton->mTextOffsetX = -8;
    mCloseButton->mTextOffsetY = 1;

    // Reuse the game's native Almanac index-button assets for both category tabs.
    mPlantsTabButton = new GameButton(1);
    mPlantsTabButton->mLabel = _S("Augment Plants");
    mPlantsTabButton->mButtonImage = Sexy::IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT;
    mPlantsTabButton->mOverImage = Sexy::IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT;
    mPlantsTabButton->mDownImage = nullptr;
    mPlantsTabButton->SetFont(Sexy::FONT_BRIANNETOD12);
    mPlantsTabButton->mColors[GameButton::COLOR_LABEL] = aButtonColor;
    mPlantsTabButton->mColors[GameButton::COLOR_LABEL_HILITE] = aButtonColor;
    mPlantsTabButton->mLabelJustify = GameButton::BUTTON_LABEL_CENTER;
    mPlantsTabButton->Resize(AUGMENT_CATALOG_PLANTS_TAB.mX, AUGMENT_CATALOG_PLANTS_TAB.mY,
        AUGMENT_CATALOG_PLANTS_TAB.mWidth, AUGMENT_CATALOG_PLANTS_TAB.mHeight);
    mPlantsTabButton->mParentWidget = this;

    mGiftsTabButton = new GameButton(2);
    mGiftsTabButton->mLabel = _S("Augment Gift");
    mGiftsTabButton->mButtonImage = Sexy::IMAGE_ALMANAC_INDEXBUTTON;
    mGiftsTabButton->mOverImage = Sexy::IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT;
    mGiftsTabButton->mDownImage = nullptr;
    mGiftsTabButton->SetFont(Sexy::FONT_BRIANNETOD12);
    mGiftsTabButton->mColors[GameButton::COLOR_LABEL] = aButtonColor;
    mGiftsTabButton->mColors[GameButton::COLOR_LABEL_HILITE] = aButtonColor;
    mGiftsTabButton->mLabelJustify = GameButton::BUTTON_LABEL_CENTER;
    mGiftsTabButton->Resize(AUGMENT_CATALOG_GIFTS_TAB.mX, AUGMENT_CATALOG_GIFTS_TAB.mY,
        AUGMENT_CATALOG_GIFTS_TAB.mWidth, AUGMENT_CATALOG_GIFTS_TAB.mHeight);
    mGiftsTabButton->mParentWidget = this;

    // Use the same native Almanac slider assets and geometry for long icon grids.
    mAugmentSlider = new Sexy::Slider(Sexy::IMAGE_OPTIONS_SLIDERSLOT_PLANT,
        Sexy::IMAGE_OPTIONS_SLIDERKNOB_PLANT, 0, this);
    mAugmentSlider->mHorizontal = false;
    mAugmentSlider->Resize(10 + BOARD_ADDITIONAL_WIDTH, mListRect.mY, 20, mListRect.mHeight);
    mAugmentSlider->mThumbOffsetX = -5;
    mAugmentSlider->mVisible = mMaxScrollOffset > 0;
    mAugmentSlider->SetValue(0.0);

    SetupSelectedAugment();
}

AugmentEncyclopediaDialog::~AugmentEncyclopediaDialog()
{
    ClearPreviewPlant();
    delete mCloseButton;
    delete mPlantsTabButton;
    delete mGiftsTabButton;
    delete mAugmentSlider;
}

void AugmentEncyclopediaDialog::ClearPreviewPlant()
{
    if (mPreviewPlant != nullptr)
    {
        mPreviewPlant->Die();
        delete mPreviewPlant;
        mPreviewPlant = nullptr;
    }
}

void AugmentEncyclopediaDialog::SetupSelectedAugment()
{
    ClearPreviewPlant();
    if (mSelectedAugment < 0 || mSelectedAugment >= RollAugment::NUM_AUGMENTS)
        return;

    SeedType aSeedType = gAugmentDefs[mSelectedAugment].mSeedType;
    if (aSeedType == SeedType::SEED_NONE)
        return;

    mPreviewPlant = new Plant();
    mPreviewPlant->mBoard = nullptr;
    mPreviewPlant->mIsOnBoard = false;
    mPreviewPlant->PlantInitialize(0, 0, aSeedType, SeedType::SEED_NONE);
    mPreviewPlant->mX = (float)AUGMENT_CATALOG_PREVIEW_X;
    mPreviewPlant->mY = (float)AUGMENT_CATALOG_PREVIEW_Y;
}

int AugmentEncyclopediaDialog::AugmentHitTest(int x, int y) const
{
    if (!mListRect.Contains(x, y))
        return -1;

    const int aVisibleCount = GetVisibleAugmentCount(mSelectedTab);
    for (int aVisibleIndex = 0; aVisibleIndex < aVisibleCount; ++aVisibleIndex)
    {
        int aIconX, aIconY;
        GetAugmentIconPosition(aVisibleIndex, mScrollOffset, aIconX, aIconY);
        if (Rect(aIconX, aIconY, SEED_PACKET_WIDTH, SEED_PACKET_HEIGHT).Contains(x, y))
            return GetVisibleAugmentIndex(mSelectedTab, aVisibleIndex);
    }
    return -1;
}

void AugmentEncyclopediaDialog::AddedToManager(WidgetManager* theWidgetManager)
{
    LawnDialog::AddedToManager(theWidgetManager);
    // As in AlmanacDialog, the slider is a child widget so native mouse
    // capture/drag behavior is handled by WidgetManager.
    AddWidget(mAugmentSlider);
}

void AugmentEncyclopediaDialog::RemovedFromManager(WidgetManager* theWidgetManager)
{
    LawnDialog::RemovedFromManager(theWidgetManager);
    RemoveWidget(mAugmentSlider);
    ClearPreviewPlant();
}

void AugmentEncyclopediaDialog::Update()
{
    LawnDialog::Update();
    mLastMouseX = mApp->mWidgetManager->mLastMouseX;
    mLastMouseY = mApp->mWidgetManager->mLastMouseY;
    if (mCloseButton)
        mCloseButton->Update();
    if (mPlantsTabButton)
        mPlantsTabButton->Update();
    if (mGiftsTabButton)
        mGiftsTabButton->Update();
    if (mAugmentSlider)
    {
        mAugmentSlider->mVisible = mMaxScrollOffset > 0;
        mAugmentSlider->Update();
        mAugmentSlider->SetValue(mMaxScrollOffset > 0 ? (double)mScrollOffset / mMaxScrollOffset : 0.0);
    }
    if (mPreviewPlant)
        mPreviewPlant->Update();

    mHoverAugment = AugmentHitTest(mLastMouseX, mLastMouseY);
    if (!(mAugmentSlider && (mAugmentSlider->mIsOver || mAugmentSlider->mDragging)) &&
        (mHoverAugment >= 0 || AUGMENT_CATALOG_PLANTS_TAB.Contains(mLastMouseX, mLastMouseY) ||
        AUGMENT_CATALOG_GIFTS_TAB.Contains(mLastMouseX, mLastMouseY) ||
        (mPlantsTabButton && mPlantsTabButton->IsMouseOver()) ||
        (mGiftsTabButton && mGiftsTabButton->IsMouseOver()) ||
        (mCloseButton && mCloseButton->IsMouseOver())))
        mApp->SetCursor(CURSOR_HAND);
    else
        mApp->SetCursor(CURSOR_POINTER);
    MarkDirty();
}

void AugmentEncyclopediaDialog::Draw(Graphics* g)
{
    g->SetColorizeImages(false);
    g->SetDrawMode(Graphics::DRAWMODE_NORMAL);
    g->SetColor(Color::White);
    // Match the Plant and Zombie Almanac backdrop and header alignment.
    g->DrawImage(Sexy::IMAGE_ALMANAC_PLANTBACK, 0, 0);
    TodDrawString(g, _S("AUGMENT ALMANAC"), BOARD_WIDTH / 2, 48 + BOARD_OFFSET_Y,
        Sexy::FONT_HOUSEOFTERROR20, Color(213, 159, 43), DS_ALIGN_CENTER);

    // Draw the left pane as the original Almanac's eight-column SeedPacket icon grid.
    // Names and effects stay in the right-hand plant-card panel, not in self-drawn rows.
    const int aVisibleCount = GetVisibleAugmentCount(mSelectedTab);
    g->SetClipRect(mListRect);
    for (int aVisibleIndex = 0; aVisibleIndex < aVisibleCount; ++aVisibleIndex)
    {
        const int i = GetVisibleAugmentIndex(mSelectedTab, aVisibleIndex);
        if (i < 0)
            continue;

        int aIconX, aIconY;
        GetAugmentIconPosition(aVisibleIndex, mScrollOffset, aIconX, aIconY);
        if (aIconY + SEED_PACKET_HEIGHT < mListRect.mY || aIconY > mListRect.mY + mListRect.mHeight)
            continue;

        const AugmentDef& aDef = gAugmentDefs[i];
        if (aDef.mSeedType != SeedType::SEED_NONE)
        {
            // Same size, frame, cost treatment and drawing path as AlmanacDialog::DrawPlants.
            DrawSeedPacket(g, (float)aIconX, (float)aIconY, aDef.mSeedType,
                SeedType::SEED_NONE, 0.0f, 255, true, false);
        }
        else
        {
            // Gift Augments occupy a native empty SeedPacket slot with the existing present art.
            DrawSeedPacket(g, (float)aIconX, (float)aIconY, SeedType::SEED_NONE,
                SeedType::SEED_NONE, 0.0f, 255, false, false);
            g->DrawImage(Sexy::IMAGE_PRESENT, aIconX + 10, aIconY + 17, 30, 30);
        }

        if (i == mHoverAugment)
            g->DrawImage(Sexy::IMAGE_SEEDPACKETFLASH, aIconX, aIconY);
    }
    g->ClearClipRect();

    if (mSelectedAugment >= 0 && mSelectedAugment < RollAugment::NUM_AUGMENTS)
    {
        const AugmentDef& aDef = gAugmentDefs[mSelectedAugment];
        const bool aPlantAugment = aDef.mSeedType != SeedType::SEED_NONE;
        const bool aDiscovered = mApp->mPlayerInfo && mApp->mPlayerInfo->HasDiscoveredAugment(aDef.mId);

        Sexy::Image* aGround = aPlantAugment && (Plant::IsNocturnal(aDef.mSeedType) || aDef.mSeedType == SeedType::SEED_GRAVEBUSTER || aDef.mSeedType == SeedType::SEED_PLANTERN)
            ? Sexy::IMAGE_ALMANAC_GROUNDNIGHT : Sexy::IMAGE_ALMANAC_GROUNDDAY;
        if (aPlantAugment && aDef.mSeedType == SeedType::SEED_FLOWERPOT)
            aGround = Sexy::IMAGE_ALMANAC_GROUNDROOF;
        g->DrawImage(aGround, AUGMENT_CATALOG_GROUND_X, AUGMENT_CATALOG_GROUND_Y);

        g->SetColorizeImages(false);
        g->SetDrawMode(Graphics::DRAWMODE_NORMAL);
        g->SetColor(Color::White);
        if (aPlantAugment && mPreviewPlant)
        {
            mPreviewPlant->mX = (float)AUGMENT_CATALOG_PREVIEW_X;
            mPreviewPlant->mY = (float)AUGMENT_CATALOG_PREVIEW_Y;
            Graphics aPlantGraphics = Graphics(*g);
            if (mPreviewPlant->BeginDraw(&aPlantGraphics))
            {
                mPreviewPlant->Draw(&aPlantGraphics);
                mPreviewPlant->EndDraw(&aPlantGraphics);
            }
        }
        else
        {
            int aGiftWidth = Sexy::IMAGE_PRESENT->GetWidth();
            int aGiftHeight = Sexy::IMAGE_PRESENT->GetHeight();
            int aGiftX = AUGMENT_CATALOG_GROUND_X + (aGround->GetWidth() - aGiftWidth) / 2;
            int aGiftY = AUGMENT_CATALOG_GROUND_Y + (aGround->GetHeight() - aGiftHeight) / 2;
            g->DrawImage(Sexy::IMAGE_PRESENT, aGiftX, aGiftY);
        }

        g->DrawImage(Sexy::IMAGE_ALMANAC_PLANTCARD, AUGMENT_CATALOG_CARD_X, AUGMENT_CATALOG_CARD_Y);
        TodDrawString(g, aDef.mName, 617 + BOARD_ADDITIONAL_WIDTH, 288 + BOARD_OFFSET_Y,
            Sexy::FONT_DWARVENTODCRAFT18YELLOW, Color::White, DS_ALIGN_CENTER);
        Rect aDescriptionRect(485 + BOARD_ADDITIONAL_WIDTH, 309 + BOARD_OFFSET_Y, 258, 210);
        SexyString aDescription = aDiscovered ? aDef.mEffect : _S("coming soon...");
        TodDrawStringWrapped(g, aDescription, aDescriptionRect, Sexy::FONT_BRIANNETOD12, Color(40, 50, 90), DS_ALIGN_LEFT);
    }

    if (mPlantsTabButton)
    {
        // Keep the selected family visibly active while the other tab retains native hover art.
        mPlantsTabButton->mButtonImage = mSelectedTab == 0
            ? Sexy::IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT : Sexy::IMAGE_ALMANAC_INDEXBUTTON;
        mPlantsTabButton->mOverImage = Sexy::IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT;
        mPlantsTabButton->Draw(g);
    }
    if (mGiftsTabButton)
    {
        mGiftsTabButton->mButtonImage = mSelectedTab == 1
            ? Sexy::IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT : Sexy::IMAGE_ALMANAC_INDEXBUTTON;
        mGiftsTabButton->mOverImage = Sexy::IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT;
        mGiftsTabButton->Draw(g);
    }
    if (mCloseButton)
        mCloseButton->Draw(g);
}

void AugmentEncyclopediaDialog::MouseDown(int x, int y, int theClickCount)
{
    if (theClickCount <= 0)
        return;
    if (AUGMENT_CATALOG_PLANTS_TAB.Contains(x, y) || AUGMENT_CATALOG_GIFTS_TAB.Contains(x, y))
    {
        int aNewTab = AUGMENT_CATALOG_PLANTS_TAB.Contains(x, y) ? 0 : 1;
        if (aNewTab != mSelectedTab)
        {
            mSelectedTab = aNewTab;
            if (mPlantsTabButton)
                mPlantsTabButton->mButtonImage = mSelectedTab == 0
                    ? Sexy::IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT : Sexy::IMAGE_ALMANAC_INDEXBUTTON;
            if (mGiftsTabButton)
                mGiftsTabButton->mButtonImage = mSelectedTab == 1
                    ? Sexy::IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT : Sexy::IMAGE_ALMANAC_INDEXBUTTON;
            mSelectedAugment = GetFirstAugmentForTab(mSelectedTab);
            mScrollOffset = 0;
            mMaxScrollOffset = GetAugmentMaxScrollOffset(mSelectedTab, mListRect.mHeight);
            SetupSelectedAugment();
            mApp->PlaySample(Sexy::SOUND_TAP);
            MarkDirty();
        }
        return;
    }
    int aAugment = AugmentHitTest(x, y);
    if (aAugment >= 0 && aAugment != mSelectedAugment)
    {
        mSelectedAugment = aAugment;
        SetupSelectedAugment();
        mApp->PlaySample(Sexy::SOUND_TAP);
        MarkDirty();
    }
    if (mCloseButton && mCloseButton->IsMouseOver())
        mApp->PlaySample(Sexy::SOUND_TAP);
}

void AugmentEncyclopediaDialog::MouseUp(int x, int y, int theClickCount)
{
    if (mCloseButton && mCloseButton->IsMouseOver())
        mApp->KillDialog(Dialogs::DIALOG_AUGMENT_ENCYCLOPEDIA);
}

void AugmentEncyclopediaDialog::SliderVal(int theId, double theVal)
{
    if (theId != 0 || mMaxScrollOffset <= 0)
        return;
    mScrollOffset = ClampInt((int)(theVal * mMaxScrollOffset), 0, mMaxScrollOffset);
    MarkDirty();
}

void AugmentEncyclopediaDialog::MouseWheel(int theDelta)
{
    if (!mListRect.Contains(mApp->mWidgetManager->mLastMouseX, mApp->mWidgetManager->mLastMouseY))
        return;
    mScrollOffset -= theDelta * 32;
    mScrollOffset = ClampInt(mScrollOffset, 0, mMaxScrollOffset);
    MarkDirty();
}

void AugmentEncyclopediaDialog::KeyDown(KeyCode theKey)
{
    if (theKey == KeyCode::KEYCODE_ESCAPE)
        mApp->KillDialog(Dialogs::DIALOG_AUGMENT_ENCYCLOPEDIA);
    else if (theKey == KeyCode::KEYCODE_UP)
    {
        mScrollOffset = max(0, mScrollOffset - AUGMENT_CATALOG_ICON_Y_STEP);
        MarkDirty();
    }
    else if (theKey == KeyCode::KEYCODE_DOWN)
    {
        mScrollOffset = min(mMaxScrollOffset, mScrollOffset + AUGMENT_CATALOG_ICON_Y_STEP);
        MarkDirty();
    }
}
