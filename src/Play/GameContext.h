#pragma once

#include "GameCommon.h"
#include "Play/type.h"
#include "Play/scoreTracker.h"
#include "Play/DigitRenderer.h"
#include "Play/Timingdisplay.h"

struct SpectrumVisualizer;
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

    bool* currentPressed;
    bool* prevPressed;
    JudgeEffect* laneJudge;
    std::vector<Note>& notes;
    std::vector<Effect>& effects;
    std::vector<KeyBeam>& keyBeams;
    
    Mix_Chunk* tap_sound;
    Mix_Chunk* tap_sound_c;

    int& comboCount;

    int& startX;
    int& endX;

    int& judgeY;

    int& laneWidth;

    bool *laneActive;

    SDL_Texture* noteTextureNormal;
    SDL_Texture* noteTextureDrag;
    SDL_Texture* noteTextureLane;

    // ===== ここから追加メンバ（初期化子の順番もこの順にすること） =====
    double musicTimeExact = 0.0;     // 描画専用の高精度時刻
    DigitFont* digitFont = nullptr;  // スコア・コンボ用の数字テクスチャ
    double scoreScale = 1.0;         // updateAndRenderScoreTexture で計算 → renderGame で使う
    double comboScale = 1.0;

    TutorialOverlay* tutorial = nullptr;

    SpectrumVisualizer* visualizer = nullptr;
    double visualOffsetMs = 0.0;

    TimingDisplay* timing = nullptr;   // FAST/LATE・±ms の表示
    
    std::vector<TimingSample>* timingLog = nullptr;
};