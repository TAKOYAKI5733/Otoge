#ifndef SCENE_H
#define SCENE_H

#include "GameCommon.h"
#include "InputBinding.h"

class TutorialOverlay;

enum class GameScene{
    Title,
    Select,
    Play,
    Result,
    Shutdown,
    Setting,
    Load,
    ChartCreate,
    Tutorial,
    Calibration,
    KeyConfig,
    NoteSetting   // ノーツの詳細設定（NoteSettingScene.cpp）
};

struct TimingSample{
    int32_t diffMs = 0;     // 叩いた時刻 − 正しい時刻（- = 早い、+ = 遅い）
    int judgeType = 0;      // 1 = PERFECT、2 = GOOD、3 = BAD
};

struct ResultData{
    int score = 0;
    int maxCombo = 0;
    bool isAP = false;
    bool isFC = false;
    int perfectCount = 0;
    int goodCount = 0;
    int badCount = 0;
    int missCount = 0;
    std::vector<TimingSample> timings;
    bool isAutoplay = false;
};

// ノーツ速度（倍率）の範囲と、左右キー1回で変わる量
//   Setting.cpp（読み込み時の範囲チェック）・Settingspanel.h（変更）・PlayScene.cpp（反映）で共通に使う
inline constexpr double NOTE_SPEED_RATE_MIN  = 0.5;
inline constexpr double NOTE_SPEED_RATE_MAX  = 20.0;
inline constexpr double NOTE_SPEED_RATE_STEP = 0.1;   // Shift を押しながらだと 0.5 ずつ

// レーンの傾き（度）の範囲と、左右キー1回で変わる量
//   0 = 傾けない（今まで通り）。大きいほど奥に倒れて、上の方が細く・低くなる
inline constexpr double LANE_TILT_DEG_MIN  = 0.0;
inline constexpr double LANE_TILT_DEG_MAX  = 60.0;
inline constexpr double LANE_TILT_DEG_STEP = 1.0;     // Shift を押しながらだと 5 度ずつ

// レーンカバー：レーンの奥を壁で隠す割合（%）
//   判定ラインから、画面で見えているレーンの奥の端までのうち、奥の何 % を壁で隠すか。0 = 隠さない
//   傾けていても傾けていなくても使える
inline constexpr int LANE_COVER_PERCENT_MIN = 0;
inline constexpr int LANE_COVER_PERCENT_MAX = 80;

// キービーム（キーを押したときにレーンに出る光）の長さ（%）
//   判定ラインから、画面で見えているレーンの奥の端までのうち、何 % の所まで届くか。0 = 出さない
//   62 % が今までの長さ（傾けていないとき 約 500px）
inline constexpr int KEY_BEAM_PERCENT_MIN     = 0;
inline constexpr int KEY_BEAM_PERCENT_MAX     = 100;
inline constexpr int KEY_BEAM_PERCENT_DEFAULT = 62;

struct PlayerSettings{
    double offsetMs = 0.0;
    int bgmVolume = 70;
    int seVolume = 100;
    bool vsync = true;
    int fpsCap = 0;
    int windowMode = 0;
    std::string tapSound = "";
    double visualOffsetMs = 0.0;
    bool showFastLate = true;    // FAST/LATE 表示
    bool showTimingMs = false;   // ±ms 表示
    LaneBindings laneBindings = defaultLaneBindings();
    bool fpsAuto = false;
    double noteSpeedRate = 1.0;  // ノーツ速度の倍率（1.0 = 譜面どおり、2.0 = 2倍の速さで流れる）
    double laneTiltDeg = 0.0;    // レーンを奥に倒す角度（度）。0 = 傾けない
    int laneCoverPercent = 0;    // レーンの奥を壁で隠す割合（%）。0 = 隠さない
    bool streamMode = false;     // 配信モード（OBS などで録画・配信しやすくする。Displaymode.h）
    int keyBeamPercent = KEY_BEAM_PERCENT_DEFAULT;   // キービームの長さ（%）。0 = 出さない
};

struct PlayTestOptions{
    int32_t startMs = 0;   // 譜面のこの時刻から始める（0 = 最初から）
};

void loadPlayerSettings(PlayerSettings& settings);
void savePlayerSettings(const PlayerSettings& settings);

GameScene playGame(SDL_Window* window, SDL_Renderer* renderer, const std::string& selectedScorePath, int selectedDifficulty, ResultData& outResult, PlayerSettings& playerSettings, bool isAutoplay, SDL_Texture* targetTex = nullptr, TutorialOverlay* tutorial = nullptr, const PlayTestOptions* test = nullptr);
GameScene selectSongScene(SDL_Window* window, SDL_Renderer* renderer, std::string& selectedScorePath, int& selectedDifficulty, PlayerSettings& playerSettings, bool& outAutoplay, SDL_Texture* targetTex = nullptr);
GameScene loadScene(SDL_Window* window, SDL_Renderer* renderer, std::string& selectedScorePath, SDL_Texture* targetTex = nullptr);
GameScene chartCreateScene(SDL_Window* window, SDL_Renderer* renderer, std::string& scorePath, SDL_Texture* targetTex = nullptr);
GameScene resultScene(SDL_Window* window, SDL_Renderer* renderer, const ResultData& result, SDL_Texture* targettex = nullptr);
GameScene settingScene(SDL_Window* window, SDL_Renderer* renderer, PlayerSettings& playerSettings, SDL_Texture* targetTex = nullptr);
GameScene calibrationScene(SDL_Window* window, SDL_Renderer* renderer, PlayerSettings& settings);
GameScene titleScene(SDL_Window* window, SDL_Renderer* renderer);
GameScene keyConfigScene(SDL_Window* window, SDL_Renderer* renderer, PlayerSettings& settings);
GameScene noteSettingScene(SDL_Window* window, SDL_Renderer* renderer, PlayerSettings& playerSettings);

#endif