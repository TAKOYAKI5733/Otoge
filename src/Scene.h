#ifndef SCENE_H
#define SCENE_H

#include "GameCommon.h"

enum class GameScene{
    Title,
    Select,
    Play,
    Result,
    Shutdown,
    Setting,
    Load,
    ChartCreate
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
};

struct PlayerSettings{
    double offsetMs = 0.0;
    int bgmVolume = 70;
    int seVolume = 100;
};

void loadPlayerSettings(PlayerSettings& settings);
void savePlayerSettings(const PlayerSettings& settings);

GameScene playGame(SDL_Window* window, SDL_Renderer* renderer, const std::string& selectedScorePath, int selectedDifficulty, ResultData& outResult, PlayerSettings& playerSettings, SDL_Texture* targetTex = nullptr);
GameScene selectSongScene(SDL_Window* window, SDL_Renderer* renderer, std::string& selectedScorePath, int& selectedDifficulty, PlayerSettings& playerSettings,SDL_Texture* targetTex = nullptr);
GameScene loadScene(SDL_Window* window, SDL_Renderer* renderer, std::string& selectedScorePath, SDL_Texture* targetTex = nullptr);
GameScene chartCreateScene(SDL_Window* window, SDL_Renderer* renderer, std::string& scorePath, SDL_Texture* targetTex = nullptr);
GameScene resultScene(SDL_Window* window, SDL_Renderer* renderer, const ResultData& result, SDL_Texture* targettex = nullptr);
GameScene settingScene(SDL_Window* window, SDL_Renderer* renderer, PlayerSettings& playerSettings, SDL_Texture* targetTex = nullptr);

#endif