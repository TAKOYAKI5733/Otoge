#pragma once

#include "GameCommon.h"
#include "Play/GameContext.h"
#include "Play/type.h"

inline void draw_waku_init(SDL_Renderer* renderer, Tex& tex, Sq& sq, bool laneActive[]){
    int laneWidth = SCREEN_W / 16;
    int startX = SCREEN_W / 2 - (laneWidth * 3);
 
    for(int i = 0; i < 6; i++){
        SDL_Rect rect = {startX + (laneWidth * i), 0, laneWidth, SCREEN_H};

        if(i % 2 == 0) SDL_SetRenderDrawColor(renderer, 0, 0, 5, 255);
        else SDL_SetRenderDrawColor(renderer, 25, 25, 35, 255);

        if(!laneActive[i]) continue;

        SDL_RenderFillRect(renderer, &rect);
    }

    for(int i = 0; i <= 6; i++){

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        int laneX = startX + (laneWidth * i);
        SDL_RenderDrawLine(renderer, laneX, 0, laneX, SCREEN_W);
    }

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderCopy(renderer, tex.waku_init, NULL, &sq.waku_init);
    SDL_RenderDrawLine(renderer, 0, SCREEN_H * (3.0 / 4.0), SCREEN_W, SCREEN_H * (3.0 / 4.0));

    return;
}