//アニメーションやグラフィックス関係のヘッダファイル
#pragma once

#include <SDL2/SDL.h>
#include <cmath>

inline void GradientBackground(SDL_Renderer* renderer, int screenW, int screenH, uint32_t musicTime){
    double wave = (std::sin(musicTime * 0.001) + 1.0) / 2.0;
    int baseBlue = static_cast<int>(60 + wave * 30);

    for(int x = 0; x < (screenW / 2); x+= 4){
        double ratio = ((screenW / 2) > 0) ? static_cast<double>(x) / (screenW / 2) : 0.0;
        int currentR = static_cast<int>(10 * ratio);
        int currentG = static_cast<int>(20 * ratio);
        int currentB = static_cast<int>(baseBlue * ratio);

        SDL_SetRenderDrawColor(renderer, 55 + currentR, 55 + currentG, 55 + currentB, 255);
        SDL_Rect rect = {x, 0, 4, screenH};
        SDL_RenderFillRect(renderer, &rect);
    }

    for(int x = (screenW / 2); x < screenW; x += 4){
        double ratio = (screenW > (screenW / 2)) ? static_cast<double>(screenW - x) / (screenW - (screenW / 2)) : 0.0;
        int currentR = static_cast<int>(10 * ratio);
        int currentG = static_cast<int>(20 * ratio);
        int currentB = static_cast<int>(baseBlue * ratio);

        SDL_SetRenderDrawColor(renderer, 55 + currentR, 55 + currentG, 55 + currentB, 255);
        SDL_Rect rect = {x, 0, 4, screenH};
        SDL_RenderFillRect(renderer, &rect);
    }
}

inline double easeOutCubic(double t){
    return 1.0 - std::pow(1.0 - t, 3.0);
}

inline double easeInCubic(double t){
    return t * t * t;
}