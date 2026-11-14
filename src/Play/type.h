//構造体定義

#pragma once

#include "GameCommon.h"
#include <climits>   // INT32_MIN

enum class NoteType{
    Normal,
    Normal_c,
    Lane,
    Long,
    Long_c,
    Drag
};

struct Effect{
    int lane;
    int widthLanes = 1;
    int32_t spawnTime;
    uint32_t duration = 300; //300[ms]
    int judgeType = 0;
};

struct Note{
    int lane;
    int widthLanes = 1;
    int32_t targetTime;
    bool isHit = false;
    int32_t hitTime = 0;          // 判定が出た瞬間の時刻（判定の時計）。表示の時刻がここを過ぎたら「叩いた見た目」にする
    NoteType type = NoteType::Normal;
    int32_t durationMs = 0;
    int32_t lastTickTime = 0;
    bool isHolding = false;

    bool isFailed = false;
    int32_t failCutTime = 0;
    bool releaseGrace = false;

    // ロングを離しても、すぐには途切れない（noteJudge.h）
    //   離してから releaseToleranceMs（10ms 固定。loadScore.h の LONG_RELEASE_TOLERANCE_MS）以内に押し直せば、続けてホールドできる
    int32_t releaseToleranceMs = 0;
    int32_t releasedAtMs = INT32_MIN;   // 離した時刻（INT32_MIN = 今は押している）
    bool endHeld = false;
    bool dragArmed = false;             // ドラッグ：判定の幅（±20ms）の中で押していたか（判定ラインに来たら、離していても取れる）               // ロングを押したまま終点まで行けたか（つながったロングの始点を、押しっぱなしで取れるようにする）

    bool movesLane = false;
    bool isCyan = false;

    bool hasCustomSpeed = false;
    double customSpeed = 1.0;

    NoteType headType = NoteType::Normal;
    bool isDragLong() const { return type == NoteType::Long && headType == NoteType::Drag; }

    // ロングの中継点（コンボだけが増える時刻）。譜面を読んだときに決めて、プレイ中は変えない
    std::vector<int32_t> tickTimes;
    size_t nextTick = 0;   // 次に判定する中継点の番号
};

struct JudgeEffect{
    SDL_Texture* texture;
    SDL_Rect rect;
    int32_t spawnTime;
    uint32_t duration = 200;

    bool hasTiming = false;   //タイミングのある判定か（MISS などは false）
    int32_t diffMs = 0;       //叩いた時刻 − 正しい時刻
    int judgeType = 0;        //1 = PERFECT、2 = GOOD、3 = BAD
};

struct SpeedEvent{
    int32_t triggerTime;
    int32_t duration = 500;
    double targetspeed;
    double startSpeed = 1.0;
    int easing;
    bool isTriggered = false;
};

struct ScorePopEffect{
    double scale = 1.0;
    int32_t startTime = 0;
    uint32_t duration = 150;    //150[ms]
};

struct ComboPopEffect{
    double scale = 1.0;
    int32_t startTime = 0;
    uint32_t duration = 120;
};

struct Tex{
    SDL_Texture* perfect;
    SDL_Texture* good;
    SDL_Texture* bad;
    SDL_Texture* miss;
    SDL_Texture* waku_init;
};

struct Sq{
    SDL_Rect perfect;
    SDL_Rect good;
    SDL_Rect bad;
    SDL_Rect miss;
    SDL_Rect waku_init;
};

struct KeyBeam{
    int lane;
    int32_t spawnTime;
};

// ノーツを取ったとき・ロングの中継点で飛び出す、半透明の立方体 1 つぶん
//   上へ飛び出し → 重力で落ち → 判定ボックスの下の端（床）で小さく跳ね返り → 止まって消える
//   位置は毎フレーム「出てからの時間」から計算する（renderGame.h の cubeStateAt）ので、ここには最初の値だけ持つ
//   落ちている途中（teleportMs）で止まり、テレポートの演出（CUBE_TELEPORT_MS）で消える。消え終わった瞬間に Cube が 1 増える
struct TickParticle{
    float x, y;                 // 出た位置（判定ラインの上、画面の座標）
    float vx, vy;               // 最初の速さ(px/ms)。vy はマイナス = 上へ
    float floorDy;              // 床（跳ね返る所）が、出た位置からどれだけ下か(px)
    float size;                 // 立方体の 1 辺(px)
    float rotX, rotY, rotZ;     // 最初の向き(ラジアン)
    float spinX, spinY, spinZ;  // 回る速さ(ラジアン/ms)
    int   lane;                 // 色を決めるレーン（そのレーンの色の立方体になる）
    double spawnTime;           // 出た時刻（表示の時刻）
    float alpha;                // 濃さ（0〜1、粒ごとに少し変える）
    float teleportMs;           // 出てから何 ms で止まってテレポートを始めるか（落ちている途中）
    bool  collected = false;    // テレポートし終わって、Cube を数えたか
};

// 立方体の設定（renderGame.h と noteJudge.h の両方で使うので、両方より先に読まれる type.h に置く）
inline constexpr int    TICK_PARTICLE_COUNT   = 1;       // ロングの中継点 1 回で出る数
inline constexpr int    NOTE_CUBE_MIN         = 1;       // ノーツ 1 個を取ったときに出る数（MIN〜MAX のどれかをランダムに）
inline constexpr int    NOTE_CUBE_MAX         = 2;
inline constexpr double CUBE_TELEPORT_MS      = 260.0;   // テレポートの演出の長さ(ms)
inline constexpr float  CUBE_TELEPORT_FROM    = 0.30f;   // 一番高い所から床までの間の、どこで止まるか（この割合〜TO の間でランダム）
inline constexpr float  CUBE_TELEPORT_TO      = 0.75f;
inline constexpr double TICK_PARTICLE_LIFE_MS = 800.0;   // 片付けるまでの時間(ms)。テレポートが終わるまでより長くする
inline constexpr double CUBE_GRAVITY          = 0.0042;  // 重力(px/ms²)。大きいほど速く落ちる
inline constexpr double CUBE_BOUNCE           = 0.36;    // 跳ね返り（床に当たった速さの何倍で跳ね返るか）
inline constexpr double CUBE_FRICTION         = 0.55;    // 床に当たるたびに、横の速さ・回る速さを何倍にするか
inline constexpr int    CUBE_MAX_BOUNCES      = 3;       // 跳ね返る回数の上限（それ以降は床に止まる）