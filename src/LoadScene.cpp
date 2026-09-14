#include <iostream>
#include <vector>
#include <string>

#define _USE_MATH_DEFINES

#include <cmath>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <fstream>
#include <random>
#include "Scene.h"

#define SCREEN_W 1920
#define SCREEN_H 1080

double easeOutBack(double t);
void renderTransition(SDL_Renderer* renderer, SDL_Texture* prev, SDL_Texture* nex, double progress);

GameScene loadScene([[__attribute_maybe_unused__]]SDL_Window* window, SDL_Renderer* renderer, std::string& selectedScorePath, SDL_Texture* targetTex){
    
    TTF_Font* font = TTF_OpenFont("fonts/prac.ttf", 60);
    TTF_Font* subFont = TTF_OpenFont("fonts/prac.ttf", 40);
    if(!font || !subFont){
        std::cout << "[ERROR] LoadSceneフォント読込失敗\n";
        return GameScene::Select;
    }

    std::vector<std::string> notes;
    std::ifstream file("fonts/notes.txt");
    if(file.is_open()){
        std::string line;
        while(std::getline(file, line)){
            if(!line.empty()){
                notes.push_back(line);
            }
        }
        file.close();
    }
    else{
        notes.push_back("これは普通にエラー表示なんだよね...");
    }

    std::string selectedNote = "これは普通にエラー表示なんだよね...";
    if(!notes.empty()){
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, notes.size() - 1);
        selectedNote = notes[dis(gen)];
    }

    SDL_Color textColor = {255, 255, 255, 255};
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, "NOW LOADING...", textColor);
    SDL_Surface* notes_surf = TTF_RenderUTF8_Blended(subFont, selectedNote.c_str(), textColor);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Texture* notes_tex = SDL_CreateTextureFromSurface(renderer, notes_surf);
    SDL_Rect textRect = {SCREEN_W / 2 - surf->w / 2, SCREEN_H / 2 + 150, surf->w, surf->h};
    SDL_Rect notesRect = {100, SCREEN_H - 120, notes_surf->w, notes_surf->h};
    SDL_FreeSurface(surf);
    SDL_FreeSurface(notes_surf);

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(notes_tex, SDL_BLENDMODE_BLEND);

    uint32_t startTime = SDL_GetTicks();
    uint32_t loadDurationMs = 5000;     //5000[ms]

    bool running = true;
    SDL_Event e;
    GameScene nextScene = GameScene::Play;

    if(targetTex != nullptr){
        SDL_SetRenderTarget(renderer, targetTex);

        SDL_SetRenderDrawColor(renderer, 15, 15, 25, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, tex, NULL, &textRect);

        SDL_SetRenderTarget(renderer, NULL);

        SDL_DestroyTexture(tex);
        TTF_CloseFont(font);
        TTF_CloseFont(subFont);

        return GameScene::Play;
    }

    SDL_SetRenderTarget(renderer, NULL);

    bool isTransitionFinished = false;
    uint32_t transitionFinishedTime = 0;
    float commentAlpha = 0.0f;

    while(running){
        uint32_t currentTime = SDL_GetTicks();
        uint32_t elapsedTime = currentTime - startTime;

        double progress = static_cast<double>(elapsedTime) / loadDurationMs;
        if(progress > 1.0) progress = 1.0;

        while(SDL_PollEvent(&e) != 0){
            if(e.type == SDL_QUIT){
                running = false;
                nextScene = GameScene::Shutdown;
            }
        }

        if(progress >= 1.0){
            running = false;
        }

        SDL_SetRenderDrawColor(renderer, 15, 15, 25, 255);
        SDL_RenderClear(renderer);

        uint32_t slideTransitionDuration = 500;

        if(elapsedTime >= slideTransitionDuration){
            if(!isTransitionFinished){
                isTransitionFinished = true;
                transitionFinishedTime = elapsedTime;
            }

            uint32_t timeSinceFinished = elapsedTime - transitionFinishedTime;

            commentAlpha = static_cast<float>(timeSinceFinished) / 1000.0f;
            if(commentAlpha > 1.0f) commentAlpha = 1.0f;
        }

        double pulse = (std::sin(elapsedTime * 0.005) + 1.0) / 2.0;
        int textAlpha = static_cast<int>(100 +  pulse * 155);
        SDL_SetTextureAlphaMod(tex, textAlpha);
        SDL_RenderCopy(renderer, tex, NULL, &textRect);

        if(isTransitionFinished){
            int finalNotesAlpha = 0;
            if(commentAlpha < 1.0f){
                finalNotesAlpha = static_cast<int>(commentAlpha * 250.0f);
            }
            else{
                finalNotesAlpha = textAlpha;
            }

            SDL_SetTextureAlphaMod(notes_tex, finalNotesAlpha);
            SDL_RenderCopy(renderer, notes_tex, NULL, &notesRect);
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        int centerX = SCREEN_W / 2;
        int centerY = SCREEN_H / 2 - 50;
        int numSquares = 4;
        int baseSize = 40;

        for(int i = 0; i < numSquares; i++){
            double offset = i * (M_PI / 2.0);
            double timeAngle = (elapsedTime * 0.003) + offset;

            int posX = centerX + static_cast<int>(std::cos(timeAngle) * 80.0);
            int posY = centerY + static_cast<int>(std::sin(timeAngle) * 80.0);

            double scaleFactor = (std::sin(timeAngle * 2.0) + 1.0) / 2.0;
            double easedScale = easeOutBack(scaleFactor);
            
            int currentSize = static_cast<int>(baseSize * (0.5 * easedScale * 0.8));

            SDL_Rect sqRect = {
                posX - currentSize / 2,
                posY - currentSize / 2,
                currentSize,
                currentSize
            };

            int r = static_cast<int>(100 + (i * 40));
            int g = static_cast<int>(200 - (i * 30));
            int b = 255;
            int alpha = static_cast<int>(150 + (scaleFactor * 105));

            SDL_SetRenderDrawColor(renderer, r, g, b, alpha);
            SDL_RenderFillRect(renderer, &sqRect);

            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 100);
            SDL_RenderDrawRect(renderer, &sqRect);
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyTexture(notes_tex);
    TTF_CloseFont(font);
    TTF_CloseFont(subFont);

    return nextScene;
}

double easeOutBack(double t){
    const double c1 = 1.70158;
    const double c3 = c1 + 1.0;
    return 1.0 + c3 * std::pow(t - 1.0, 3.0) + c1 * std::pow(t - 1.0, 2.0);
}