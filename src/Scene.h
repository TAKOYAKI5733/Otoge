#ifndef SCENE_H
#define SCENE_H

#include "GameCommon.h"

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
    Calibration
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

#endif