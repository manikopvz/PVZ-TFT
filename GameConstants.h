#pragma once

#include "ConstEnums.h"

constexpr const double PI = 3.141592653589793;

const int			BOARD_WIDTH = 1280; // PC : 800 - Console : 1280
const int			BOARD_HEIGHT = 720; // PC : 600 - Console : 720
const int			BOARD_OFFSET_X = 220; // PC : 220
const int			BOARD_OFFSET_Y = 60; // Console : 60
const int			BOARD_ADDITIONAL_WIDTH = 240; // Console : 240
const int			WIDE_BOARD_WIDTH = 800 + BOARD_ADDITIONAL_WIDTH; // PC : 800 - Console : 800
const int			BOARD_EDGE = -100 + BOARD_ADDITIONAL_WIDTH;
const int			BOARD_IMAGE_WIDTH_OFFSET = 1180 + BOARD_ADDITIONAL_WIDTH;
const int           BOARD_ICE_START = 800 + BOARD_ADDITIONAL_WIDTH;
const int           LAWN_XMIN = 40;
const int           LAWN_YMIN = 80;
const int           HIGH_GROUND_HEIGHT = 30;
const int			STREET_ZOMBIE_START_X = 1020; // PC: 830 - Console : 1000
const int			STREET_ZOMBIE_ROOF_START_X = 900;
const int			STREET_ZOMBIE_START_Y = 70;
const int			STREET_ZOMBIE_GRID_SIZE_X = 30; // PC : 56 - Console : 30
const int			STREET_ZOMBIE_GRID_SIZE_Y = 90;
const int			STREET_ZOMBIE_ROOF_OFFSET = 15; // PC : 30 - Console : 15
const int			ROOF_POLE_START = WIDE_BOARD_WIDTH + 70 - BOARD_ADDITIONAL_WIDTH;
const int			ROOF_POLE_END = -BOARD_WIDTH;
const int			ROOF_TREE_START = WIDE_BOARD_WIDTH + 130 - BOARD_ADDITIONAL_WIDTH;
const int			ROOF_TREE_END = -670;

const int           SEEDBANK_MAX = 10;

// ---- Roll Seed Bank (TFT-shop style) ----
// "Chosen Plants"     = the pool of plants the player picks in the seed chooser.
// "Chosen Seed Bank"  = the shop slots, randomly rolled from that pool. The Reroll button is an extra slot in front of them.
const int           ROLL_SEED_BANK_COST = 2;           // Sun cost of a reroll (fixed)
const int           ROLL_MAX_SLOTS = 5;                 // the shop shows at most this many plant slots (the Reroll slot is not counted)
const int           ROLL_TUTORIAL_VERSION = 3;          // new Bench merge and Wave 3 Augment lessons
const int           ROLL_BENCH_SLOTS = 6;               // purchased and rewarded seed packets wait here before planting
const int           ROLL_BENCH_GAP = 8;                 // visual gap between Bench packets
const int           ROLL_BENCH_ANIMATION_MS = 150;      // incoming card and existing Bench packets move together for 0.15 seconds
const int           ROLL_BENCH_MERGE_MS = 260;          // packets converge before their star level is combined
const int           ROLL_POOL_MIN_BANK_SLOTS = 7;       // if the level lets you carry >= this many plants, the pool has the fixed size below
const int           ROLL_POOL_SIZE_WIDE_BANK = 5;       // Chosen Plants pool size in that case
const int           ROLL_FREE_MAX = 3;                  // max stored free rerolls (one is granted per flag wave)
const int           ROLL_START_SUN = 5;               // starting Sun of roll levels that would normally start with 50
// ---- Roll Seed Bank: slots ----
const int           ROLL_MAX_LOCKS = 1;                 // slots that can be locked (right-click) so a reroll keeps them
const int           ROLL_MAX_SAME_PER_ROLL = 5;         // max copies of one plant in a single roll
const int           ROLL_MIN_SUN_PLANTS = 2;            // the first shop of a level has at least this many Sunflowers / Sun-shrooms (if the pool has one)
const int           ROLL_ANIM_FRAMES = 24;              // slot flip animation length (frames)
const int           ROLL_ANIM_STAGGER = 4;              // extra frames per slot index, so slots settle left to right
const int           ROLL_TEMPORARY_SEED_PACKET_LIFETIME = 1000; // 10 seconds at the game's 100 updates per second
// ---- Roll Seed Bank: Sun economy ----
const int           ROLL_MAX_SUN_PLANTS = 5;            // maximum live sun production plants in Roll Shop
const int           ROLL_SUN_SHOP_EXTRA = 0;            // extra sun-plant slots the shop may show beyond the free room (0 = never more than the room)
const int           ROLL_INCOME_SUN_STEP = 10;          // one income bracket per 10 held Sun
// Maximum active income producers equals ROLL_MAX_SUN_PLANTS; twins yield two Sun each.
const int           ROLL_SUNSHROOM_GROW_WAVES = 2;      // growth wave provides no resource
// ---- Roll Seed Bank: augments ----
const int           ROLL_AUGMENT_WAVE_INDEX = 2;        // the augment is offered right before wave (index + 1), i.e. wave 3
// ---- Roll Seed Bank: tutorial / Remove Your Seed ----
const int           ROLL_TUTORIAL_SEEN_SLOT = 79;       // PlayerInfo::mPurchases[] slot used as the "tutorial seen" flag (unused by the store)
const int           ROLL_TUTORIAL_SUN = 25;            // the tutorial makes sure you have at least this much Sun
const int           ROLL_TUTORIAL_FIRST_WAVE_TICKS = 500; // five seconds at the normal 100-updates/second rate
const int           ROLL_STARTER_PLANTS = 8;            // the first eight plants are always unlocked and in the pool
const int           ROLL_MAX_BAN_SLOTS = 8;             // maximum number of plants that can be banned from one level
const int           ROLL_REMOVE_SEED_START_LEVEL = 8;  // Remove Your Seed starts at Adventure 1-8
enum RollTutorialStep
{
    ROLL_TUT_NONE = -1,
    ROLL_TUT_BUY_SUNFLOWER = 0,
    ROLL_TUT_PLANT_SUNFLOWER,
    ROLL_TUT_BUY_PEASHOOTER,
    ROLL_TUT_PLANT_PEASHOOTER,
    ROLL_TUT_REROLL,
    ROLL_TUT_LOCK,
    ROLL_TUT_ROLL_LOCKED,
    ROLL_TUT_FINISH, // Legacy saved-state ID; reserved for compatibility.
    ROLL_TUT_BUY_MERGE_FIRST,
    ROLL_TUT_BUY_MERGE_SECOND,
    ROLL_TUT_WAIT_MERGE,
    ROLL_TUT_PLANT_MERGED,
    ROLL_TUT_WAIT_AUGMENT,
    ROLL_TUT_AUGMENT_REROLL,
    ROLL_TUT_AUGMENT_CHOOSE,
    ROLL_TUT_NUM_STEPS
};
enum RollAugment
{
    AUGMENT_NONE = -1,
    AUGMENT_PEASHOOTER = 0,     // plant augments (need the plant in the Chosen Plants pool)
    AUGMENT_SUNFLOWER,
    AUGMENT_CHERRYBOMB,
    AUGMENT_WALLNUT,
    AUGMENT_POTATOMINE,
    AUGMENT_SNOWPEA,
    AUGMENT_CHOMPER,
    AUGMENT_REPEATER,
    AUGMENT_FIVE_SEEDS,         // general augments (shown as a gift box)
    AUGMENT_SUN_BOON,
    AUGMENT_LUCKY_ROLLS,
    AUGMENT_SHOP_HOLIDAY,
    AUGMENT_TOUGH_PLANTS,
    AUGMENT_PUFF_PATROL,        // grants Puff-shroom packets immediately and every two waves
    AUGMENT_GENTLE_UPROOTING,   // full-health plants can be reclaimed with the shovel
    AUGMENT_GARDEN_PACK,        // plant eight random plants from this level's pool
    AUGMENT_SPOILS_OF_WAR,     // defeated zombies can drop sun or a pool-backed packet
    AUGMENT_BALANCED_BUDGET,
    AUGMENT_FINAL_RESERVES,
    AUGMENT_HOLOGRAM,
    AUGMENT_SUNSHROOM_SHADOW,
    AUGMENT_FUMESHROOM_INFINITE,
    AUGMENT_GRAVE_DIVIDEND,
    AUGMENT_LINGERING_HYPNOSIS,
    AUGMENT_PANIC_SPORES,
    AUGMENT_BRITTLE_ICE,
    AUGMENT_GRAVE_COLLAPSE,
    AUGMENT_GRAVE_HARVEST,
    AUGMENT_GARDEN_GLOVE,
    AUGMENT_FORTUNES_FAVOR,
    AUGMENT_LILYPAD_RENEWAL,
    AUGMENT_SQUASH_SECOND,
    AUGMENT_THREEPEATER_SPLIT,
    AUGMENT_TANGLEKELP_COLLECTIVE,
    AUGMENT_JALAPENO_CROSSFIRE,
    AUGMENT_TORCHWOOD_HEAT,
    AUGMENT_SPIKEWEED_RECOIL,
    AUGMENT_TALLNUT_ENDURING,
    AUGMENT_SEASHROOM_HARVEST,
    AUGMENT_PLANTERN_GOLD,
    AUGMENT_SEED_SURPRISE_II,
    AUGMENT_SUN_BOON_II,
    AUGMENT_GRAVE_HARVEST_II,
    AUGMENT_FORTUNES_FAVOR_II,
    AUGMENT_SEED_DRAFT,
    AUGMENT_SHIFTING_BENCH,
    NUM_AUGMENTS,
    FIRST_GENERAL_AUGMENT = AUGMENT_FIVE_SEEDS
};

const int           SEED_BANK_OFFSET_X = 0;
const int           SEED_BANK_OFFSET_X_END = 10;
const int			SEED_CHOOSER_EXTRA_HEIGHT = 120;
const int           SEED_CHOOSER_OFFSET_Y = 720; // PC : 516 - Console : 720
const int           SEED_PACKET_WIDTH = 50;
const int           SEED_PACKET_HEIGHT = 70;
const int			IMITATER_POS_X = 459;
const int			IMITATER_POS_Y = 503;
const int           IMITATER_DIALOG_WIDTH = 500;
const int           IMITATER_DIALOG_HEIGHT = 600;

const int			ADVENTURE_AREAS = 5;
const int			LEVELS_PER_AREA = 10;
const int           NUM_LEVELS = ADVENTURE_AREAS * LEVELS_PER_AREA;
const int			FINAL_LEVEL = NUM_LEVELS;
const int           FLAG_RAISE_TIME = 100;
const int           LAST_STAND_FLAGS = 5;
const int           ZOMBIE_COUNTDOWN_FIRST_WAVE = 900;
const int           ZOMBIE_COUNTDOWN = 2500;
const int           ZOMBIE_COUNTDOWN_RANGE = 600;
const int           ZOMBIE_COUNTDOWN_BEFORE_FLAG = 4500;
const int           ZOMBIE_COUNTDOWN_BEFORE_REPICK = 5499;
const int           ZOMBIE_COUNTDOWN_MIN = 400;
const int			ZOMBIE_CLIPRECT_WIDTH = BOARD_WIDTH - 45;
const int           FOG_BLOW_RETURN_TIME = 2000;
const int           SUN_COUNTDOWN = 425;
const int           SUN_COUNTDOWN_RANGE = 275;
const int           SUN_COUNTDOWN_MAX = 950;
const int           SURVIVAL_NORMAL_FLAGS = 5;
const int           SURVIVAL_HARD_FLAGS = 10;

const int           STORESCREEN_ITEMOFFSET_1_X = 422 + BOARD_ADDITIONAL_WIDTH;
const int           STORESCREEN_ITEMOFFSET_1_Y = 206 + BOARD_OFFSET_Y;
const int           STORESCREEN_ITEMOFFSET_2_X = 372 + BOARD_ADDITIONAL_WIDTH;
const int           STORESCREEN_ITEMOFFSET_2_Y = 325 + BOARD_OFFSET_Y;
const int           STORESCREEN_ITEMSIZE = 74;
const int           STORESCREEN_COINBANK_X = 650 + BOARD_ADDITIONAL_WIDTH * 2;
const int           STORESCREEN_COINBANK_Y = 559 + BOARD_OFFSET_Y * 2;
const int           STORESCREEN_PAGESTRING_X = 470 + BOARD_ADDITIONAL_WIDTH;
const int           STORESCREEN_PAGESTRING_Y = 500 + BOARD_OFFSET_Y * 2;

////////////////////////////////////////////////////////////
///					   QOTL VARIABLES		   			 ///
////////////////////////////////////////////////////////////

const int			ADVANCEDOPTIONS_MAX_PAGES = 4;
const int			ADVANCEDOPTIONS_PAGE_Y = 355;
const int			ADVANCEDOPTIONS_SPEED_X = 284;
const int			ADVANCEDOPTIONS_SPEED_Y = 128;

const int			SPEED_MODIFIER_MIN = 2;
const int			SPEED_MODIFIER_MAX = 9;

const bool			HAS_QUICKPLAY = true;
const bool			HAS_ACHIEVEMENTS = true;
const bool			HAS_PAGE_SELECTOR = true;
const bool			HAS_FAST_FORWARD_BUTTON = true;

////////////////////////////////////////////////////////////
///			    	 ENHANCED VARIABLES					 ///
////////////////////////////////////////////////////////////

const bool			SHOULD_FOG_COVER_FULLY = false; // Console : false
const bool			USE_OLD_STYLE_SEEDPACKET = false; // PC : true - Console : false
const bool			USE_OLD_STYLE_TOOLTIP = false; // PC : true - Console : false
const bool			USE_ZOMBIE_SEED_VARIANT = true;
const bool			USE_CONSOLE_SEED_VARIANTS = true; // PC: false - Console : true
const int			MAX_GAMEPADS = 2;