#pragma once

#include "GameCommon.h"
#include "Play/type.h"
#include "Play/scoreTracker.h"

struct GameContext{
    SDL_Renderer* renderer;
    TTF_Font* font;

    ScoreTracker& scoreTracker;
    int32_t musicTime;
    bool& isAP;
    bool& isFC;
    
    SDL_Texture*& scoreTexture;
    SDL_Texture*& comboTexture;
    SDL_Rect& scoreRect;
    SDL_Rect& comboRect;
    double& visualScore;
    int& lastDisplayScoreValue;
    int& lastCombo;
    ScorePopEffect& scorePop;
    ComboPopEffect& comboPop;

    const bool* currentPressed;
    bool* prevPressed;
    JudgeEffect* laneJudge;
    std::vector<Note>& notes;
    std::vector<Effect>& effects;
    
    Mix_Chunk* tap_sound;

    int& comboCount;

    int& startX;
    int& endX;

    int& judgeY;

    int& laneWidth;

    bool *laneActive;
};