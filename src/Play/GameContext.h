#pragma once

#include "GameCommon.h"
#include "Play/type.h"
#include "Play/scoreTracker.h"
#include "Play/DigitRenderer.h"
#include "Play/Timingdisplay.h"
#include <climits>   // INT32_MIN（NO_PRESS_TIME）

struct SpectrumVisualizer;

// プレイ中の Cube の数と、左上の表示に使うもの
struct CubeHud{
    long long base   = 0;        // プレイを始める前に持っていた Cube
    int       earned = 0;        // このプレイで集めた Cube（立方体がテレポートし終わるたびに +1）
    double    popMs  = -1e9;     // 最後に増えた時刻（表示の時刻）。数字を少し大きくして戻す演出に使う
    SDL_Texture* label = nullptr;   // 「CUBE」の文字
    int labelW = 0, labelH = 0;
};
class LaneTilt;
class PlayBackground;
class ScrollMap;   // Play/BSpeedEvent.h
struct FalseNote;  // Play/FalseNotes.h

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

    // レーンの左右の傾き（押しているロングの左右の割合で決まる。PlayScene.cpp）
    //   laneLayerTex にレーン・ノーツ・エフェクトを描いてから、レーンの中心線を軸に laneRollDeg 度だけ傾けて貼る（renderGame.h の drawRolledLayer）
    //   laneLayerTex が nullptr か、角度がほぼ 0 のときは今まで通り（回さずにそのまま描く）
    double laneRollDeg = 0.0;                      // + = 右側が奥へ下がる、- = 左側が奥へ下がる
    //   laneLayerTex は、画面より上下に LANE_ROLL_OVERDRAW_PX ずつ大きく作る（高さ = SCREEN_H + 2 × これ）。
    //   傾けると奥の側が縮んで、画面の上下の端にレーンの無い所が見えてしまうので、その分を余分に描いておく
    SDL_Texture* laneLayerTex = nullptr;

    // ゲーム内の通貨「Cube」（Play/CubeWallet.h）。nullptr なら数えない・表示しない（ノーツの詳細設定のプレビューなど）
    CubeHud* cubeHud = nullptr;

    // 押している状態・離した瞬間（Play/PreciseInput.h のスレッドが見たもの）。preciseLanes が true のレーンで使う
    //   ロングのホールドを、押した瞬間と同じ所（別スレッド）で見た状態で判定するため（nullptr なら SDL のキーの状態だけ）
    const bool*    preciseHeld  = nullptr;   // レーンごと：今押しているか
    const int32_t* releaseTimes = nullptr;   // レーンごと：このフレームまでに離した瞬間の「曲の時刻」。NO_PRESS_TIME = 離していない

    bool isAutoplay = false;                 // オートプレイ中か（ノーツを取っても Cube を出さない）
    const ScrollMap* scrollMap = nullptr;   // B_SpeedEvent のスクロール位置（Play/BSpeedEvent.h）。nullptr = 使わない（S(t) = t）
    const std::vector<FalseNote>* falseNotes = nullptr;   // false ノーツ（見た目だけ。Play/FalseNotes.h）。nullptr = なし

    // 始まりの演出（PlayScene.cpp が時間で決める。1 = 終わっていて、いつも通り）
    double laneFrameIntro = 1.0;             // レーンの外枠の線が、判定ラインから上下・左右へ伸びていく進み具合（0〜1）
    double laneIntro      = 1.0;             // レーン（背景・区切り線・判定枠・ノーツ）がグリッチで出てくる進み具合（0〜1）
    bool   laneOutro      = false;           // 曲の終わりの演出中（laneIntro を 1 → 0 にしてレーンをグリッチで消している）

    // 最後に「叩いた瞬間で測った」判定（noteJudge.h の recordTiming が写す）。nullptr なら使わない
    //   ドラッグ・ロングの PERFECT など、測らない判定が出ても ±ms の表示をこの値のまま残すため
    JudgeEffect* lastTimedJudge = nullptr;
};

// レーンの左右の傾きで、上下に余分に描く量(px)
inline constexpr int LANE_ROLL_OVERDRAW_PX = 50;

// pressTimes の「押されていない」の印
inline constexpr int32_t NO_PRESS_TIME = INT32_MIN;