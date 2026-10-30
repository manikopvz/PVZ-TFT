#ifndef __AUGMENTENCYCLOPEDIADIALOG_H__
#define __AUGMENTENCYCLOPEDIADIALOG_H__

#include "LawnDialog.h"
#include "../../SexyAppFramework/SliderListener.h"
#include "../../SexyAppFramework/Slider.h"
#include "../AugmentData.h"

class GameButton;
class LawnApp;
class Plant;

class AugmentEncyclopediaDialog : public LawnDialog, public Sexy::SliderListener
{
public:
    GameButton*     mCloseButton;
    GameButton*     mPlantsTabButton;
    GameButton*     mGiftsTabButton;
    Sexy::Slider*   mAugmentSlider;
    int             mScrollOffset;
    Rect            mListRect;
    int             mMaxScrollOffset;
    int             mSelectedAugment;
    int             mSelectedTab;
    int             mHoverAugment;
    int             mLastMouseX;
    int             mLastMouseY;
    Plant*          mPreviewPlant;

public:
    explicit AugmentEncyclopediaDialog(LawnApp* theApp);
    virtual ~AugmentEncyclopediaDialog();

    virtual void AddedToManager(WidgetManager* theWidgetManager);
    virtual void RemovedFromManager(WidgetManager* theWidgetManager);
    virtual void Update();
    virtual void Draw(Graphics* g);
    virtual void MouseDown(int x, int y, int theClickCount);
    virtual void MouseUp(int x, int y, int theClickCount);
    virtual void MouseWheel(int theDelta);
    virtual void KeyDown(KeyCode theKey);
    virtual void SliderVal(int theId, double theVal);

    void SetupSelectedAugment();
    void ClearPreviewPlant();
    int AugmentHitTest(int x, int y) const;
};

#endif
