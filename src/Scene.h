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
    Load
};

GameScene playGame(SDL_Window* window, SDL_Renderer* renderer, const std::string& selectedScorePath, SDL_Texture* targetTex = nullptr);
GameScene selectSongScene(SDL_Window* window, SDL_Renderer* renderer, std::string& selectedScorePath, SDL_Texture* targetTex = nullptr);
GameScene loadScene(SDL_Window* window, SDL_Renderer* renderer, std::string& selectedScorePath, SDL_Texture* targetTex = nullptr);

#endif