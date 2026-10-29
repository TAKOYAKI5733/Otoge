//構造体定義

#pragma once

#include "GameCommon.h"

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

struct PathKeyframe{
    int32_t time;
    double y = 0.0;
    int easing = 1;
};
struct Note{
    int lane;
    int widthLanes = 1;
    int32_t targetTime;
    bool isHit = false;
    int32_t hitTime = 0;   // 判定が出た時刻（曲の時刻）。ロングは終点まで押し切ったら終点の判定の時刻に書き換える
                           //   描画では「表示の時刻」がここを過ぎたら、叩いた見た目（消える・破裂）にする
    NoteType type = NoteType::Normal;
    int32_t durationMs = 0;
    int32_t lastTickTime = 0;
    bool isHolding = false;

    bool isFailed = false;
    int32_t failCutTime = 0;
    bool releaseGrace = false;

    bool movesLane = false;
    bool isCyan = false;

    bool hasCustomSpeed = false;
    double customSpeed = 1.0;

    std::vector<PathKeyframe> path;
    bool hasCustomPath() const { return !path.empty(); }

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

// ロングの中継点で飛び散る、正方形の枠1つぶん
struct TickParticle{
    float x, y;          // 出た位置（判定ボックスの中心）
    float vx, vy;        // 飛ぶ向きと速さ(px/ms)
    float size;          // 正方形の1辺(px)
    float angle;         // 最初の傾き(ラジアン)
    float spin;          // 回る速さ(ラジアン/ms)
    double spawnTime;    // 出た時刻
    SDL_Color color;
};

// 中継点のエフェクトの設定（renderGame.h と noteJudge.h の両方で使うので、両方より先に読まれる type.h に置く）
inline constexpr int    TICK_PARTICLE_COUNT   = 1;       // 1回に飛ぶ枠の数
inline constexpr double TICK_PARTICLE_LIFE_MS = 420.0;   // 消えるまでの時間