#include "GameCommon.h"

#define SCREEN_W 1920
#define SCREEN_H 1080

static std::string calcRank(int score){
    if(score >= 990000) return "SSS";
    else if(score >= 950000) return "SS";
    else if(score >= 900000) return "S";
    else if(score >= 880000) return "AAA";
    else if(score >= 850000) return "AA";
    else if(score >= 800000) return "A";
    else if(score >= 700000) return "B";
    else if(score >= 600000) return "C";
    return "F"; 
}

static SDL_Color rankColor(const std::string& rank){
    if(rank == "SSS") return {255, 215, 0, 255};
    if(rank == "SS") return {255, 255, 100, 255};
    if(rank == "S") return {0, 255, 255, 255};
    if(rank == "AAA" || rank == "AA" || rank == "A") return {0, 255, 120, 255};
    if(rank == "B") return {255, 165, 0, 255};
    if(rank == "C") return {200, 200, 200, 255};
    return {211, 211, 211, 200};
}

GameScene resultScene([[__attribute_maybe_unused__]]SDL_Window* window, SDL_Renderer* renderer, const ResultData& result, SDL_Texture* targetTex){
    TTF_Font* fontLarge = TTF_OpenFont("fonts/prac.ttf", 100);
    TTF_Font* fontMed = TTF_OpenFont("fonts/prac.ttf", 50);
    TTF_Font* fontSmall = TTF_OpenFont("fonts/prac.ttf", 36);

    if(!fontLarge || !fontMed || !fontSmall){
        std::cout << "[ERROR] ResultSceneフォント読込失敗\n";
        return GameScene::Select;
    }

    std::string rank = calcRank(result.score);
    SDL_Color rColor = rankColor(rank);

    auto renderLine = [&](TTF_Font* font, const std::string& text, SDL_Color color, int x, int y, bool centerX){
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
        if(!surf) return;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_Rect r = { centerX ? x - surf->w / 2 : x, y, surf->w, surf->h };
        SDL_RenderCopy(renderer, tex, NULL, &r);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    };

    auto drawAll = [&](){
        SDL_SetRenderDrawColor(renderer, 10, 10, 20, 255);
        SDL_RenderClear(renderer);

        renderLine(fontLarge, "RESULT", {255, 255, 255, 255}, SCREEN_W / 2, 60, true);
        renderLine(fontLarge, rank, rColor, SCREEN_W / 2, 200, true);

        char scoreBuf[16];
        snprintf(scoreBuf, sizeof(scoreBuf), "%07d", result.score);
        renderLine(fontMed, std::string("SCORE: ") + scoreBuf, {255, 255, 255, 255}, SCREEN_W / 2, 470, true);

        std::string status;
        if(result.isAP) status = "ALL PERFECT";
        else if(result.isFC) status = "FULL COMBO";

        if(!status.empty()){
            renderLine(fontMed, status, {255, 215, 0, 255}, SCREEN_W / 2, 540, true);
        }

        int baseY = 650;
        int gap = 55;
        int labelX = SCREEN_W / 2 - 200;
        renderLine(fontSmall, "PREFECT : " + std::to_string(result.perfectCount), {255, 215, 0, 255}, labelX, baseY, false);
        renderLine(fontSmall, "GOOD : " + std::to_string(result.goodCount), {0, 255, 255, 255}, labelX, baseY + gap, false);
        renderLine(fontSmall, "BAD : " + std::to_string(result.badCount), {180, 50, 50, 255}, labelX, baseY + gap * 2, false);
        renderLine(fontSmall, "MISS : " + std::to_string(result.missCount), {100, 100, 100, 255}, labelX, baseY + gap * 3, false);

        renderLine(fontSmall, "Press ENTER to continue", {150, 150, 150, 255}, SCREEN_W / 2, SCREEN_H  - 100, true);
    };

    if(targetTex != nullptr){
        SDL_SetRenderTarget(renderer, targetTex);
        drawAll();
        SDL_SetRenderTarget(renderer, NULL);

        TTF_CloseFont(fontLarge);
        TTF_CloseFont(fontMed);
        TTF_CloseFont(fontSmall);
        return GameScene::Result;
    }

    bool running = true;
    SDL_Event e;
    GameScene nextScene = GameScene::Select;

    while(running){
        while(SDL_PollEvent(&e) != 0){
            if(e.type == SDL_QUIT){
                running = false;
                nextScene = GameScene::Shutdown;
            }
            if(e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_SPACE)){
                running = false;
                nextScene = GameScene::Select;
            }
        }

        drawAll();
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    TTF_CloseFont(fontLarge);
    TTF_CloseFont(fontMed);
    TTF_CloseFont(fontSmall);

    return nextScene;
}