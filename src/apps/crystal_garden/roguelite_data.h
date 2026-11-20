/**
 * roguelite_data.h — 64キャラクター / 36チャーム / 6ボス アーキタイプ定義 (Flash常駐)
 */
#pragma once
#include <stdint.h>

namespace game {

constexpr int TALENT_N = 64;
constexpr int CHARM_N  = 36;
constexpr int BOSS_N   = 6;
constexpr int PARTY_MAX = 3;

enum Rarity : uint8_t {
    RAR_N = 0,
    RAR_R = 1,
    RAR_SR = 2,
    RAR_SSR = 3,
};

enum CombatStyle : uint8_t {
    STYLE_COMBO_BLADE  = 0, /* 3段近接コンボ + 打ち上げ追撃 */
    STYLE_FLURRY_DART  = 1, /* 高速連撃ダッシュ + 高会心 */
    STYLE_MAGIC_BOLT   = 2, /* 追尾魔導弾 + バックステップ */
    STYLE_SPREAD_VOLLEY= 3, /* 3方向扇状弾 + 対空牽制 */
    STYLE_SNIPER_LANCE = 4, /* 超長射程貫通槍撃 */
    STYLE_HEAVY_SLAM   = 5, /* 重叩きつけAoE + 被ダメ軽減 */
    STYLE_PALADIN_WARD = 6, /* 前衛打撃 + 3段毎小回復＆状態短縮 */
    STYLE_ALCH_MEND    = 7, /* 攻撃魔弾 + 最弱味方回復/浄化 */
};

enum TraitId : uint8_t {
    TRAIT_NONE      = 0,
    TRAIT_BERSERK   = 1, /* HP50%未満で攻撃+35% */
    TRAIT_VAMP      = 2, /* 命中時HP回復 +2.5 */
    TRAIT_CRIT      = 3, /* 会心率 +15% */
    TRAIT_WARDEN    = 4, /* 味方全体の床/毒ダメ -35% */
    TRAIT_HASTE     = 5, /* 移動/攻撃回転 +18% */
    TRAIT_EXEC      = 6, /* HP40%未満/ボスへ+35%ダメ */
    TRAIT_THORNS    = 7, /* 被弾時に反撃棘ダメ */
    TRAIT_SCAVENGER = 8, /* 結晶ドロップ +20% */
    TRAIT_FROST     = 9, /* 攻撃時スロウ付与 */
    TRAIT_RAGE      = 10,/* MP蓄積 +30% */
    TRAIT_BARRIER   = 11,/* Wave開始時18%シールド */
};

enum UltKind : uint8_t {
    ULT_CRYSTAL_RAIN = 0,
    ULT_AQUA_STORM   = 1,
    ULT_GRAND_SLAM   = 2,
    ULT_SANCTUARY    = 3, /* 全体回復32% + 全浄化 + 45f無敵 */
    ULT_STAR_BARRAGE = 4, /* 10発追尾星弾連射 */
    ULT_OVERDRIVE    = 5, /* 広範囲斬撃 + 全体MP+35 & ヘイスト */
};

enum CharmEff : uint8_t {
    CE_ATK_PCT = 0,     /* 全味方攻撃 +% */
    CE_HP_PCT,          /* 全味方最大HP +% */
    CE_CRIT_PCT,        /* 会心率 +% */
    CE_SPD_PCT,         /* 行動速度 +% */
    CE_VAMP_X10,        /* 攻撃回復 +val/10 */
    CE_REGEN_X10,       /* 秒間自動回復 +val/10 */
    CE_CHIP_RES_PCT,    /* 毒/火傷/床ダメ軽減 +% */
    CE_FLAT_ARMOR,      /* 被ダメ固定軽減 +val */
    CE_DROP_PCT,        /* 結晶ドロップ +% */
    CE_BOX_DISC_PCT,    /* ルートボックス割引 +% */
    CE_RAGE_PCT,        /* MP増加 +% */
    CE_ULT_PCT,         /* 必殺技威力 +% */
    CE_WAVE_HEAL_PCT,   /* Waveクリア回復 +% */
    CE_BOSS_SLAY_PCT,   /* ボス特効 +% */
    CE_EXEC_PCT,        /* 低HP敵特効 +% */
    CE_THORNS,          /* 反撃固定ダメージ +val */
    CE_SHIELD_PCT,      /* Wave開始シールド +%HP */
    CE_START_MP,        /* Wave開始時初期MP +val */
    CE_GLASS_ATK,       /* 攻撃+val% かつ HP-16% */
    CE_BERSERK_PCT,     /* 低HP時追加攻撃 +% */
    CE_CHAIN_SPARK,     /* 攻撃時連鎖スパーク確率 +% */
    CE_FROST_SLOW,      /* 攻撃時スロウ減速確率 +% */
    CE_TWIN_BOLT,       /* 遠距離追撃弾確率 +% */
    CE_KILL_HEAL_PCT,   /* 撃破時全体回復 +val% */
    CE_CLEANSE_ULT,     /* 必殺技発動で状態異常回復 */
    CE_SCOUT_LUCK,      /* 高レア出現率UP +val */
    CE_BOSS_GUARD_PCT,  /* ボスPhaseダメ軽減 +% */
    CE_PHOENIX_ASH,     /* 1ラン1回全滅回避 (HP50%蘇生) */
    CE_BLOOD_GRAIL,     /* 攻撃+val% かつ VAMP +2.5 */
    CE_STAR_CORE,       /* 必殺技連撃強化 + MP蓄積+20% */
    CE_CROWN_ALL,       /* 全能力 +val% + 開始シールド+10% */
};

struct TalentDef {
    const char* name;      /* <= 9 chars */
    const char* role_tag;  /* <= 7 chars */
    const char* ult_name;  /* <= 17 chars */
    uint8_t rarity;
    uint8_t unlock_tier;   /* 0=初期解放, 1..8=達成解放 */
    uint8_t base_atlas;    /* 0=LUMI, 1=MIRA, 2=GORO */
    uint8_t use_orig_pal;  /* 1=元配色そのまま */
    uint16_t pal_pri;      /* 服/体 メイン色 (RGB565) */
    uint16_t pal_sec;      /* 髪/飾/武器 アクセント色 (RGB565) */
    uint8_t style;
    uint8_t trait;
    uint8_t ult;
    float hp;
    float dmg;
    uint16_t spd_x100;
    uint8_t reach;
    uint8_t windup;
    uint8_t strike;
    uint8_t recover;
};

struct CharmDef {
    const char* name;      /* <= 13 chars */
    const char* desc;      /* <= 18 chars */
    uint8_t rarity;
    uint8_t unlock_tier;
    uint8_t max_lv;
    uint8_t eff;
    int16_t val;
};

struct BossDef {
    const char* name1;
    const char* name2;
    const char* p50_name;
    const char* p30_name;
    const char* p10_name;
    uint8_t chassis;       /* 0=E_BOSS(標準魔王) 1=E_GOLEM(重装甲巨神) */
    uint8_t use_orig_pal;
    uint16_t pal_pri;
    uint16_t pal_sec;
    uint16_t hp_base;
    float dmg_mult;
    uint8_t p50_kind;      /* 各しきい値固有アクション種別 */
    uint8_t p30_kind;
    uint8_t p10_kind;
};

static constexpr uint16_t C565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* 役割別の描画向き・ステータス表示名 */
static inline const char* rarity_label(uint8_t r)
{
    switch (r) {
    case RAR_N:   return "COMMON [N]";
    case RAR_R:   return "RARE [R]";
    case RAR_SR:  return "EPIC [SR]";
    case RAR_SSR: return "LEGEND [SSR]";
    default:      return "[N]";
    }
}

static inline const char* rarity_tag(uint8_t r)
{
    switch (r) {
    case RAR_N:   return "[N]";
    case RAR_R:   return "[R]";
    case RAR_SR:  return "[SR]";
    case RAR_SSR: return "[SSR]";
    default:      return "[N]";
    }
}

/* ================= 64 プレイアブルキャラクター (N:26 / R:20 / SR:12 / SSR:6) ================= */
static const TalentDef TALENTS[TALENT_N] = {
    /* --- [0..25] COMMON (N) 26人 --- */
    /* 00 初期主人公 (弱いスタート) */
    {"LUMI",     "NOVICE",  "CRYSTAL SHOWER",  RAR_N,   0, 0, 1, C565(90,210,245),  C565(255,215,90),  STYLE_COMBO_BLADE,  TRAIT_NONE,      ULT_CRYSTAL_RAIN,  92,  6.5f, 255, 42, 5, 6, 7},
    {"PIP",      "SCOUT",   "ACORN FLURRY",    RAR_N,   0, 0, 0, C565(120,210,95),  C565(245,190,80),  STYLE_FLURRY_DART,  TRAIT_HASTE,     ULT_OVERDRIVE,     94,  7.6f, 285, 40, 4, 5, 5},
    {"RENO",     "SQUIRE",  "IRON CLEAVE",     RAR_N,   0, 2, 0, C565(165,175,195), C565(215,90,70),   STYLE_HEAVY_SLAM,   TRAIT_THORNS,    ULT_GRAND_SLAM,   138,  9.2f, 195, 44, 9, 6, 9},
    {"TINA",     "HERBAL",  "DEWDROP MEND",    RAR_N,   0, 1, 0, C565(95,215,155),  C565(245,235,130), STYLE_ALCH_MEND,    TRAIT_WARDEN,    ULT_SANCTUARY,     88,  6.8f, 225,175, 7, 5, 7},
    {"KAI",      "ARCHER",  "WIND VOLLEY",     RAR_N,   0, 1, 0, C565(80,175,115),  C565(225,140,70),  STYLE_SPREAD_VOLLEY,TRAIT_CRIT,      ULT_STAR_BARRAGE,  86,  7.4f, 235,180, 6, 5, 7},
    {"BERN",     "GUARD",   "SHIELD BASH",     RAR_N,   0, 2, 0, C565(100,145,215), C565(230,220,165), STYLE_PALADIN_WARD, TRAIT_BARRIER,   ULT_SANCTUARY,    145,  8.4f, 190, 44, 8, 6, 8},
    {"NORA",     "DANCER",  "PETAL SLASH",     RAR_N,   0, 0, 0, C565(245,120,175), C565(255,235,140), STYLE_COMBO_BLADE,  TRAIT_HASTE,     ULT_CRYSTAL_RAIN,  96,  8.2f, 280, 44, 4, 5, 6},
    {"MILO",     "APPRENT", "SPARK SHOT",      RAR_N,   0, 1, 0, C565(235,155,75),  C565(120,210,255), STYLE_MAGIC_BOLT,   TRAIT_RAGE,      ULT_AQUA_STORM,    88,  7.8f, 225,178, 7, 5, 7},
    {"HUGO",     "MINER",   "PICKAXE CRUSH",   RAR_N,   0, 2, 0, C565(180,130,85),  C565(105,220,245), STYLE_HEAVY_SLAM,   TRAIT_SCAVENGER, ULT_GRAND_SLAM,   140,  9.5f, 192, 46, 9, 6, 9},
    {"SORA",     "PIKER",   "LONG THRUST",     RAR_N,   0, 1, 0, C565(90,195,245),  C565(240,245,255), STYLE_SNIPER_LANCE, TRAIT_EXEC,      ULT_STAR_BARRAGE,  92,  8.5f, 220,200, 8, 5, 8},
    {"COCO",     "ALCH",    "TONIC FLASK",     RAR_N,   0, 1, 0, C565(215,115,225), C565(115,240,180), STYLE_ALCH_MEND,    TRAIT_VAMP,      ULT_SANCTUARY,     90,  7.0f, 225,172, 7, 5, 7},
    {"DAN",      "BRAWLER", "KNUCKLE DUST",    RAR_N,   0, 0, 0, C565(225,95,80),   C565(245,210,120), STYLE_COMBO_BLADE,  TRAIT_BERSERK,   ULT_OVERDRIVE,    108,  8.6f, 260, 42, 4, 6, 6},
    {"FAY",      "SLINGER", "PEBBLE SPRAY",    RAR_N,   0, 1, 0, C565(230,190,95),  C565(150,220,120), STYLE_SPREAD_VOLLEY,TRAIT_SCAVENGER, ULT_STAR_BARRAGE,  86,  7.5f, 240,175, 6, 5, 7},
    {"LEO",      "TEMPLAR", "LIGHT WARD",      RAR_N,   0, 2, 0, C565(225,205,130), C565(110,185,245), STYLE_PALADIN_WARD, TRAIT_WARDEN,    ULT_SANCTUARY,    142,  8.6f, 198, 44, 8, 6, 8},
    {"ZAC",      "THIEF",   "SHADOW STAB",     RAR_N,   1, 0, 0, C565(95,95,125),   C565(215,75,115),  STYLE_FLURRY_DART,  TRAIT_CRIT,      ULT_OVERDRIVE,     94,  8.8f, 290, 42, 4, 5, 5},
    {"EMA",      "CLERIC",  "HOPE HYMN",       RAR_N,   1, 1, 0, C565(240,235,210), C565(245,175,85),  STYLE_ALCH_MEND,    TRAIT_BARRIER,   ULT_SANCTUARY,     92,  6.9f, 225,178, 7, 5, 7},
    {"TORI",     "FROSTIE", "CHILL NEEDLE",    RAR_N,   1, 1, 0, C565(115,215,255), C565(225,245,255), STYLE_MAGIC_BOLT,   TRAIT_FROST,     ULT_AQUA_STORM,    88,  7.9f, 225,180, 7, 5, 7},
    {"GUS",      "MASON",   "STONE WALL",      RAR_N,   2, 2, 0, C565(145,155,140), C565(230,165,85),  STYLE_HEAVY_SLAM,   TRAIT_THORNS,    ULT_GRAND_SLAM,   152,  9.0f, 188, 46, 9, 6, 9},
    {"YUI",      "FENCER",  "LEAF BLADE",      RAR_N,   2, 0, 0, C565(85,205,150),  C565(245,145,165), STYLE_COMBO_BLADE,  TRAIT_HASTE,     ULT_CRYSTAL_RAIN,  98,  8.5f, 280, 44, 4, 6, 6},
    {"BART",     "HUNTER",  "SNARE ARROW",     RAR_N,   2, 1, 0, C565(145,115,75),  C565(120,230,130), STYLE_SNIPER_LANCE, TRAIT_CRIT,      ULT_STAR_BARRAGE,  90,  8.7f, 225,205, 8, 5, 8},
    {"RINA",     "SAGE",    "RUNE FLASH",      RAR_N,   3, 1, 0, C565(175,110,235), C565(110,240,230), STYLE_MAGIC_BOLT,   TRAIT_RAGE,      ULT_AQUA_STORM,    90,  8.1f, 230,180, 7, 5, 7},
    {"KEN",      "MONK",    "PALM STRIKE",     RAR_N,   3, 0, 0, C565(240,145,65),  C565(245,240,210), STYLE_FLURRY_DART,  TRAIT_VAMP,      ULT_OVERDRIVE,    106,  8.7f, 275, 42, 4, 5, 6},
    {"MOE",      "PAGE",    "COURAGE HIT",     RAR_N,   4, 2, 0, C565(195,95,120),  C565(240,215,110), STYLE_PALADIN_WARD, TRAIT_BARRIER,   ULT_SANCTUARY,    144,  8.5f, 200, 44, 8, 6, 8},
    {"VICK",     "ROGUE",   "COIN DAGGER",     RAR_N,   4, 0, 0, C565(85,165,175),  C565(250,215,75),  STYLE_FLURRY_DART,  TRAIT_SCAVENGER, ULT_CRYSTAL_RAIN,  95,  8.6f, 285, 42, 4, 5, 5},
    {"PATTY",    "BAKER",   "ROLLING PIN",     RAR_N,   5, 2, 0, C565(245,205,155), C565(235,105,125), STYLE_HEAVY_SLAM,   TRAIT_WARDEN,    ULT_GRAND_SLAM,   148,  8.8f, 198, 44, 8, 6, 8},
    {"HAL",      "WATCH",   "BEACON SHOT",     RAR_N,   5, 1, 0, C565(115,125,215), C565(250,220,95),  STYLE_SPREAD_VOLLEY,TRAIT_RAGE,      ULT_STAR_BARRAGE,  92,  8.0f, 232,182, 6, 5, 7},

    /* --- [26..45] RARE (R) 20人 --- */
    /* 26 MIRA 本人 */
    {"MIRA",     "WITCH",   "AQUA STORM",      RAR_R,   0, 1, 1, C565(155,105,245), C565(90,230,245),  STYLE_MAGIC_BOLT,   TRAIT_RAGE,      ULT_AQUA_STORM,   108, 10.6f, 235,190, 6, 5, 6},
    {"ELENA",    "VALKYR",  "RADIANT SLASH",   RAR_R,   0, 0, 0, C565(245,220,120), C565(110,215,255), STYLE_COMBO_BLADE,  TRAIT_CRIT,      ULT_CRYSTAL_RAIN, 122, 11.4f, 285, 46, 4, 5, 6},
    {"DANTE",    "ASSASSN", "CRIMSON EDGE",    RAR_R,   0, 0, 0, C565(195,55,75),   C565(245,215,130), STYLE_FLURRY_DART,  TRAIT_EXEC,      ULT_OVERDRIVE,    112, 12.2f, 305, 44, 3, 5, 5},
    {"CEDRIC",   "KNIGHT",  "AEGIS BREAKER",   RAR_R,   0, 2, 0, C565(95,180,235),  C565(245,235,175), STYLE_PALADIN_WARD, TRAIT_BARRIER,   ULT_SANCTUARY,    185, 11.0f, 205, 48, 8, 6, 8},
    {"SYLVIE",   "RANGER",  "THORN VOLLEY",    RAR_R,   0, 1, 0, C565(90,205,115),  C565(245,165,75),  STYLE_SPREAD_VOLLEY,TRAIT_FROST,     ULT_STAR_BARRAGE, 108, 10.4f, 248,192, 6, 5, 6},
    {"IGNIS",    "PYRO",    "FLAME LANCE",     RAR_R,   0, 1, 0, C565(245,100,55),  C565(255,225,85),  STYLE_SNIPER_LANCE, TRAIT_BERSERK,   ULT_AQUA_STORM,   106, 12.0f, 230,215, 7, 5, 7},
    {"IRIS",     "PRIEST",  "VERDANT BLESS",   RAR_R,   1, 1, 0, C565(235,240,250), C565(110,240,185), STYLE_ALCH_MEND,    TRAIT_WARDEN,    ULT_SANCTUARY,    116,  9.4f, 232,185, 6, 5, 6},
    {"MAGNUS",   "CRUSHER", "TITAN EARTH",     RAR_R,   1, 2, 0, C565(205,120,65),  C565(245,205,85),  STYLE_HEAVY_SLAM,   TRAIT_THORNS,    ULT_GRAND_SLAM,   188, 12.6f, 198, 50, 9, 6, 8},
    {"LYRA",     "BARD",    "SONIC CHIME",     RAR_R,   2, 1, 0, C565(240,115,195), C565(120,235,250), STYLE_SPREAD_VOLLEY,TRAIT_HASTE,     ULT_OVERDRIVE,    110, 10.2f, 255,188, 6, 5, 6},
    {"VEX",      "REAPER",  "SOUL SCYTHE",     RAR_R,   2, 0, 0, C565(130,85,185),  C565(105,245,165), STYLE_COMBO_BLADE,  TRAIT_VAMP,      ULT_OVERDRIVE,    118, 11.8f, 290, 48, 4, 5, 6},
    {"FREYA",    "FROST",   "GLACIER SPEAR",   RAR_R,   3, 1, 0, C565(105,220,255), C565(215,165,255), STYLE_SNIPER_LANCE, TRAIT_FROST,     ULT_STAR_BARRAGE, 112, 11.8f, 238,218, 7, 5, 7},
    {"ORION",    "LANCER",  "COMET PIERCE",    RAR_R,   3, 0, 0, C565(80,145,240),  C565(250,210,95),  STYLE_COMBO_BLADE,  TRAIT_EXEC,      ULT_CRYSTAL_RAIN, 124, 11.6f, 288, 48, 4, 5, 6},
    {"CHLOE",    "ALCH",    "ELIXIR BLOOM",    RAR_R,   4, 1, 0, C565(245,175,90),  C565(130,245,140), STYLE_ALCH_MEND,    TRAIT_SCAVENGER, ULT_SANCTUARY,    114,  9.6f, 235,185, 6, 5, 6},
    {"BRAM",     "SENTINL", "BASTION ROAR",    RAR_R,   4, 2, 0, C565(135,165,180), C565(90,235,215),  STYLE_PALADIN_WARD, TRAIT_THORNS,    ULT_GRAND_SLAM,   196, 11.2f, 202, 48, 8, 6, 8},
    {"NIX",      "NINJA",   "KUNAI TEMPEST",   RAR_R,   5, 0, 0, C565(75,115,165),  C565(245,95,95),   STYLE_FLURRY_DART,  TRAIT_CRIT,      ULT_STAR_BARRAGE, 110, 12.1f, 310, 44, 3, 5, 5},
    {"VERA",     "MYSTIC",  "STARLIGHT ORB",   RAR_R,   5, 1, 0, C565(195,120,245), C565(255,220,120), STYLE_MAGIC_BOLT,   TRAIT_RAGE,      ULT_AQUA_STORM,   112, 11.2f, 238,195, 6, 5, 6},
    {"REX",      "BEAST",   "SAVAGE CRUSH",    RAR_R,   6, 2, 0, C565(215,95,75),   C565(245,205,135), STYLE_HEAVY_SLAM,   TRAIT_BERSERK,   ULT_GRAND_SLAM,   184, 13.0f, 210, 48, 8, 6, 8},
    {"ARIA",     "WIND",    "TEMPEST BOW",     RAR_R,   6, 1, 0, C565(125,240,185), C565(245,245,225), STYLE_SPREAD_VOLLEY,TRAIT_HASTE,     ULT_STAR_BARRAGE, 110, 10.8f, 255,195, 5, 5, 6},
    {"JUDE",     "EXORCST", "PURGE SEAL",      RAR_R,   7, 1, 0, C565(225,205,245), C565(250,185,75),  STYLE_SNIPER_LANCE, TRAIT_WARDEN,    ULT_SANCTUARY,    116, 11.5f, 235,212, 7, 5, 7},
    {"TARA",     "GLADIAT", "ARENA FURY",      RAR_R,   7, 0, 0, C565(235,140,80),  C565(250,240,160), STYLE_COMBO_BLADE,  TRAIT_BERSERK,   ULT_OVERDRIVE,    128, 12.0f, 290, 46, 4, 5, 5},

    /* --- [46..57] EPIC (SR) 12人 --- */
    /* 46 GORO 本人 */
    {"GORO",     "GUARDN",  "GRAND SLAM",      RAR_SR,  0, 2, 1, C565(150,175,205), C565(245,195,95),  STYLE_HEAVY_SLAM,   TRAIT_THORNS,    ULT_GRAND_SLAM,   245, 16.5f, 215, 52, 8, 6, 8},
    {"KAGURA",   "BLADEDR", "SAKURA STORM",    RAR_SR,  0, 0, 0, C565(245,125,175), C565(255,240,245), STYLE_COMBO_BLADE,  TRAIT_CRIT,      ULT_OVERDRIVE,    146, 15.8f, 315, 48, 3, 5, 5},
    {"ALISTAR",  "TEMPLAR", "HOLY AEGIS",      RAR_SR,  0, 2, 0, C565(245,225,130), C565(115,215,255), STYLE_PALADIN_WARD, TRAIT_BARRIER,   ULT_SANCTUARY,    238, 14.5f, 218, 50, 7, 6, 7},
    {"SELENE",   "LUNA",    "MOONLIT SALVO",   RAR_SR,  0, 1, 0, C565(125,145,245), C565(245,245,180), STYLE_SPREAD_VOLLEY,TRAIT_RAGE,      ULT_STAR_BARRAGE, 134, 14.2f, 258,202, 5, 5, 6},
    {"VULCAN",   "FORGE",   "MAGMA QUAKE",     RAR_SR,  2, 2, 0, C565(245,95,55),   C565(255,225,95),  STYLE_HEAVY_SLAM,   TRAIT_BERSERK,   ULT_GRAND_SLAM,   240, 17.2f, 212, 52, 8, 6, 7},
    {"ZEPHYR",   "GALE",    "CYCLONE THRUST",  RAR_SR,  3, 0, 0, C565(95,235,195),  C565(245,245,140), STYLE_FLURRY_DART,  TRAIT_HASTE,     ULT_CRYSTAL_RAIN, 142, 15.6f, 325, 46, 3, 5, 4},
    {"MORGAN",   "WARLOCK", "ABYSS SIPHON",    RAR_SR,  4, 1, 0, C565(165,80,225),  C565(245,115,155), STYLE_MAGIC_BOLT,   TRAIT_VAMP,      ULT_AQUA_STORM,   136, 15.4f, 245,205, 6, 5, 6},
    {"CLARICE",  "SERAPH",  "ANGELIC MEND",    RAR_SR,  4, 1, 0, C565(250,245,235), C565(130,225,255), STYLE_ALCH_MEND,    TRAIT_WARDEN,    ULT_SANCTUARY,    140, 13.0f, 248,198, 6, 5, 6},
    {"FENRIR",   "WOLF",    "FROST FANG",      RAR_SR,  5, 0, 0, C565(120,195,245), C565(240,245,255), STYLE_COMBO_BLADE,  TRAIT_FROST,     ULT_OVERDRIVE,    150, 16.0f, 310, 48, 3, 5, 5},
    {"ARTEMIS",  "SNIPER",  "SOLAR PIERCE",    RAR_SR,  6, 1, 0, C565(95,205,135),  C565(255,215,95),  STYLE_SNIPER_LANCE, TRAIT_EXEC,      ULT_STAR_BARRAGE, 132, 16.8f, 248,235, 6, 5, 6},
    {"BALTHAZ",  "DRAGOON", "SKY BREAKER",     RAR_SR,  7, 2, 0, C565(125,110,225), C565(250,180,85),  STYLE_PALADIN_WARD, TRAIT_SCAVENGER, ULT_GRAND_SLAM,   232, 15.6f, 222, 50, 7, 6, 7},
    {"NOEL",     "ARCANST", "PRISM CASCADE",   RAR_SR,  7, 1, 0, C565(105,235,245), C565(245,140,225), STYLE_SPREAD_VOLLEY,TRAIT_RAGE,      ULT_CRYSTAL_RAIN, 136, 14.8f, 255,205, 5, 5, 6},

    /* --- [58..63] LEGEND (SSR) 6人 --- */
    {"SERAPHIA", "DAWN",    "HEAVEN SANCTUM",  RAR_SSR, 2, 1, 0, C565(255,235,145), C565(135,240,255), STYLE_ALCH_MEND,    TRAIT_WARDEN,    ULT_SANCTUARY,    172, 17.5f, 265,210, 5, 5, 5},
    {"VALKYRIA", "APEX",    "VALHALLA EDGE",   RAR_SSR, 3, 0, 0, C565(250,210,95),  C565(245,105,145), STYLE_COMBO_BLADE,  TRAIT_EXEC,      ULT_OVERDRIVE,    180, 20.5f, 330, 52, 3, 5, 4},
    {"CHRONOS",  "ETERNAL", "STAR JUDGMENT",   RAR_SSR, 4, 1, 0, C565(135,180,255), C565(255,230,130), STYLE_SNIPER_LANCE, TRAIT_RAGE,      ULT_STAR_BARRAGE, 164, 21.5f, 265,245, 5, 5, 5},
    {"SOLARIS",  "TITAN",   "SOLSTICE CRUSH",  RAR_SSR, 6, 2, 0, C565(255,155,70),  C565(255,245,175), STYLE_HEAVY_SLAM,   TRAIT_BARRIER,   ULT_GRAND_SLAM,   295, 21.0f, 228, 56, 7, 6, 7},
    {"CELESTIA", "PRISM",   "ETERNAL GARDEN",  RAR_SSR, 7, 0, 0, C565(115,245,255), C565(255,165,235), STYLE_FLURRY_DART,  TRAIT_VAMP,      ULT_CRYSTAL_RAIN, 176, 20.2f, 335, 50, 3, 5, 4},
    {"ASTRAEA",  "CROWN",   "COSMIC FINALE",   RAR_SSR, 8, 1, 0, C565(245,120,250), C565(255,235,130), STYLE_SPREAD_VOLLEY,TRAIT_CRIT,      ULT_AQUA_STORM,   168, 19.8f, 275,215, 5, 5, 5},
};

/* ================= 36 パッシブチャーム (N:12 / R:12 / SR:8 / SSR:4) ================= */
static const CharmDef CHARMS[CHARM_N] = {
    /* --- COMMON (N) 12 --- */
    {"IRON FANG",     "PARTY ATK +16%",     RAR_N,   0, 5, CE_ATK_PCT,       16},
    {"VITAL LEAF",    "MAX HP +18%",        RAR_N,   0, 5, CE_HP_PCT,        18},
    {"KEEN LENS",     "CRIT RATE +10%",     RAR_N,   0, 4, CE_CRIT_PCT,      10},
    {"SWIFT BOOTS",   "MOVE+ATK SPD +12%",  RAR_N,   0, 4, CE_SPD_PCT,       12},
    {"VAMP TOOTH",    "HIT HEAL +1.2",      RAR_N,   0, 4, CE_VAMP_X10,      12},
    {"ANTIDOTE VIAL", "POISON/HAZARD -25%", RAR_N,   0, 3, CE_CHIP_RES_PCT,  25},
    {"SHELL CHARM",   "CHIP GUARD +2",      RAR_N,   0, 4, CE_FLAT_ARMOR,     2},
    {"LUCKY CLOVER",  "CRYSTAL DROP +20%",  RAR_N,   0, 4, CE_DROP_PCT,      20},
    {"RAGE HEART",    "MP GAIN +22%",       RAR_N,   0, 4, CE_RAGE_PCT,      22},
    {"CAMP TEA",      "WAVE HEAL +8%",      RAR_N,   0, 4, CE_WAVE_HEAL_PCT,  8},
    {"THORN BRAMBLE", "REFLECT DMG +6",     RAR_N,   1, 3, CE_THORNS,         6},
    {"MAGNET STAR",   "START MP +22",       RAR_N,   2, 3, CE_START_MP,      22},

    /* --- RARE (R) 12 --- */
    {"BRUTAL EDGE",   "PARTY ATK +24%",     RAR_R,   0, 4, CE_ATK_PCT,       24},
    {"MOSS HEART",    "REGEN +1.8/S",       RAR_R,   0, 4, CE_REGEN_X10,     18},
    {"GIANT BANE",    "VS BOSS DMG +25%",   RAR_R,   0, 3, CE_BOSS_SLAY_PCT, 25},
    {"PURE RIBBON",   "ULT CLEANSE+HEAL",   RAR_R,   0, 2, CE_CLEANSE_ULT,    1},
    {"GUARD CLOAK",   "HAZARD RES +32%",    RAR_R,   0, 3, CE_CHIP_RES_PCT,  32},
    {"MERCHANT DICE", "BOX COST -14%",      RAR_R,   0, 3, CE_BOX_DISC_PCT,  14},
    {"FROST GEM",     "HIT SLOW CHILL",     RAR_R,   1, 3, CE_FROST_SLOW,    35},
    {"BARRIER PEARL", "WAVE SHIELD +15%",   RAR_R,   2, 3, CE_SHIELD_PCT,    15},
    {"ARCANE CROWN",  "ULT POWER +30%",     RAR_R,   2, 3, CE_ULT_PCT,       30},
    {"EXEC SIGIL",    "LOW HP ENEMY +28%",  RAR_R,   3, 3, CE_EXEC_PCT,      28},
    {"SOUL HARVEST",  "KILL PARTY HEAL 3%", RAR_R,   4, 3, CE_KILL_HEAL_PCT,  3},
    {"SCOUT BADGE",   "HIGH RARITY LUCK",   RAR_R,   5, 3, CE_SCOUT_LUCK,    18},

    /* --- EPIC (SR) 8 --- */
    {"GLASS PRISM",   "ATK+42% HP-16%",     RAR_SR,  0, 2, CE_GLASS_ATK,     42},
    {"CHAIN SPARK",   "CHAIN LIGHTNING",    RAR_SR,  0, 3, CE_CHAIN_SPARK,   34},
    {"BERSERK TORC",  "LOW HP ATK +45%",    RAR_SR,  1, 2, CE_BERSERK_PCT,   45},
    {"TWIN ECHO",     "EXTRA BOLT CHANCE",  RAR_SR,  3, 3, CE_TWIN_BOLT,     40},
    {"CHRONO GEAR",   "BOSS PHASE DMG -25%",RAR_SR,  4, 2, CE_BOSS_GUARD_PCT,25},
    {"CRIMSON FANG",  "HIT HEAL +2.8",      RAR_SR,  5, 2, CE_VAMP_X10,      28},
    {"AEGIS CORE",    "ARMOR+4 + SHIELD",   RAR_SR,  6, 2, CE_FLAT_ARMOR,     4},
    {"SOLAR HERB",    "WAVE HEAL +16%",     RAR_SR,  6, 2, CE_WAVE_HEAL_PCT, 16},

    /* --- LEGEND (SSR) 4 --- */
    {"PHOENIX ASH",   "1X WIPE REVIVE 50%", RAR_SSR, 0, 1, CE_PHOENIX_ASH,    1},
    {"BLOOD GRAIL",   "ATK+36% + VAMP+2.5", RAR_SSR, 2, 2, CE_BLOOD_GRAIL,   36},
    {"STARLIGHT COR", "DOUBLE ULT + MP+20%",RAR_SSR, 5, 2, CE_STAR_CORE,      1},
    {"CROWN OF LUMI", "ALL+22% + SHIELD",   RAR_SSR, 7, 2, CE_CROWN_ALL,     22},
};

/* ================= 6 ボスバリエーション (Wave 5,10..50 / 50%・30%・10% 固有アクション) ================= */
static const BossDef BOSSES[BOSS_N] = {
    /* 0: Wave 5, 30 */
    {"DREAD THORN", "TITAN",     "THORN GARDEN",  "BRIAR SWARM",   "OVERGROWTH",
     0, 1, C565(175,45,95),   C565(140,225,85),  440, 1.00f, 0, 1, 2},
    /* 1: Wave 10, 35 */
    {"VENOM QUEEN", "MIASMA",    "ACID FLOOD",    "BROOD CALL",    "MELTDOWN MIST",
     0, 0, C565(95,185,75),   C565(225,95,215),  520, 1.06f, 3, 1, 3},
    /* 2: Wave 15, 40 (巨神ゴーレム骨格 + 溶岩黒曜カラー) */
    {"IRONCLAD",    "COLOSSUS",  "MAGMA MORTAR",  "AEGIS QUAKES",  "CATACLYSM",
     1, 0, C565(75,70,95),    C565(250,145,55),  620, 1.10f, 4, 5, 4},
    /* 3: Wave 20, 45 */
    {"NOCTURNE",    "ARCHFIEND", "SHADOW VOLLEY", "BLOOD ECLIPSE", "ABYSS TEMPEST",
     0, 0, C565(65,55,145),   C565(245,65,105),  560, 1.14f, 6, 7, 6},
    /* 4: Wave 25 */
    {"FROSTBANNER", "SOVEREIGN", "PERMAFROST",    "GLACIAL GUARD", "ABSOLUTE ZERO",
     1, 0, C565(70,155,230),  C565(225,245,255), 600, 1.18f, 8, 5, 8},
    /* 5: Wave 50 Final Boss */
    {"ECLIPSE GOD", "CALAMITY",  "VOID RIFT",     "ECLIPSE NOVA",  "LAST JUDGMENT",
     0, 0, C565(225,45,85),   C565(255,215,95),  760, 1.28f, 9, 10, 11},
};

static inline uint8_t boss_id_for_wave(uint32_t w)
{
    if (w >= 50) return 5;
    if (w < 5) return 0;
    return (uint8_t)(((w / 5) - 1) % 5);
}

/* Wave・挑戦回数に応じた解禁ティア (0..8) */
static inline uint8_t unlock_tier_for_progress(uint32_t best_wave, uint32_t runs, bool cleared)
{
    if (cleared) return 8;
    uint8_t t = 0;
    if (best_wave >= 4 || runs >= 2)  t = 1;
    if (best_wave >= 8 || runs >= 4)  t = 2;
    if (best_wave >= 12 || runs >= 6) t = 3;
    if (best_wave >= 16 || runs >= 8) t = 4;
    if (best_wave >= 22 || runs >= 11) t = 5;
    if (best_wave >= 28 || runs >= 15) t = 6;
    if (best_wave >= 35 || runs >= 20) t = 7;
    if (best_wave >= 42 || runs >= 28) t = 8;
    return t;
}

} // namespace game
