#pragma once

#include "GameCommon.h"
#include "Play/type.h"
#include "Play/scoreTracker.h"
#include "Play/DigitRenderer.h"
#include "Play/Timingdisplay.h"
#include <climits>   // INT32_MIN（NO_PRESS_TIME）

struct SpectrumVisualizer;
class LaneTilt;
class PlayBackground;
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
    std::vector<TickParticle>* tickParticles = nullptr;   // ロングの中継点のエフェクト
    SDL_Texture* comboLabel = nullptr;                    // コンボ数の上の「COMBO」
    int comboLabelW = 0;
    int comboLabelH = 0;

    LaneTilt* laneTilt = nullptr;    // レーンを奥に倒して描く道具（Play/LaneTilt.h）。nullptr なら傾けない
    double laneTiltDeg = 0.0;        // 倒す角度（度）。0 なら傾けない
    double laneCoverRatio = 0.0;     // レーンカバー：レーンの奥を壁で隠す割合（0〜0.8）。0 なら隠さない
    double keyBeamRatio = 0.62;      // キービームの長さ：見えているレーンの何割まで届くか（0〜1）。0 なら出さない

    // 押した瞬間の正確な時刻（Play/PreciseInput.h）。nullptr なら今まで通り（フレームの時刻で判定）
    const int32_t* pressTimes = nullptr;   // レーンごと：このフレームまでに押された瞬間の「曲の時刻」。NO_PRESS_TIME = 押されていない
    const bool* preciseLanes = nullptr;    // レーンごと：true なら pressTimes で判定する（フレームごとの「押した瞬間」は使わない）

    const PlayBackground* background = nullptr;   // 背景（Play/PlayBackground.h）。nullptr なら今まで通りのグラデーション
};

// pressTimes の「押されていない」の印
inline constexpr int32_t NO_PRESS_TIME = INT32_MIN;