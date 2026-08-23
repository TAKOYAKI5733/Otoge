//構造体定義

#pragma once

#include "GameCommon.h"

enum class NoteType{
    Normal,
    Lane,
    Long,
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
    NoteType type = NoteType::Normal;
    int32_t durationMs = 0;
    int32_t lastTickTime = 0;
    bool isHolding = false;

    bool movesLane = false;
};

struct JudgeEffect{
    SDL_Texture* texture;
    SDL_Rect rect;
    int32_t spawnTime;
    uint32_t duration = 200;
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