#pragma once

#include "GameCommon.h"

// ==========================================
// 数字(0〜9)とラベル(" COMBO")を最初に1回だけテクスチャ化しておき、
// スコアやコンボは「数字テクスチャを並べて貼る」だけで描画する。
// → プレイ中に TTF_Render / CreateTexture / DestroyTexture を一切呼ばない。
// 文字は白で作っておき、色は SDL_SetTextureColorMod で付ける。
// ==========================================
struct DigitFont{
    SDL_Texture* digit[10] = {};
    int digitW[10] = {};
    int digitH = 0;

    SDL_Texture* label = nullptr;
    int labelW = 0;
    int labelH = 0;

    bool init(SDL_Renderer* renderer, TTF_Font* font, const char* labelText){
        const SDL_Color white = {255, 255, 255, 255};

        for(int i = 0; i < 10; i++){
            char s[2] = {static_cast<char>('0' + i), '\0'};
            SDL_Surface* surf = TTF_RenderUTF8_Blended(font, s, white);
            if(!surf) return false;

            digit[i] = SDL_CreateTextureFromSurface(renderer, surf);
            digitW[i] = surf->w;
            digitH = surf->h;
            SDL_FreeSurface(surf);
            if(!digit[i]) return false;
        }

        SDL_Surface* ls = TTF_RenderUTF8_Blended(font, labelText, white);
        if(!ls) return false;
        label = SDL_CreateTextureFromSurface(renderer, ls);
        labelW = ls->w;
        labelH = ls->h;
        SDL_FreeSurface(ls);

        return label != nullptr;
    }

    void destroy(){
        for(int i = 0; i < 10; i++){
            if(digit[i]){
                SDL_DestroyTexture(digit[i]);
                digit[i] = nullptr;
            }
        }
        if(label){
            SDL_DestroyTexture(label);
            label = nullptr;
        }
    }

    // num の数字列（＋必要ならラベル）を、(centerX, centerY) を中心に scale 倍で描く
    void drawCentered(SDL_Renderer* renderer, const char* num, bool withLabel,
                      int centerX, int centerY, double scale, SDL_Color color){
        // ① 全体の幅を測る
        int totalW = 0;
        for(const char* p = num; *p != '\0'; ++p){
            if(*p >= '0' && *p <= '9') totalW += digitW[*p - '0'];
        }
        if(withLabel && label) totalW += labelW;

        double x = centerX - (totalW * scale) / 2.0;

        // ② 数字を左から順に貼る
        int h = static_cast<int>(digitH * scale + 0.5);
        int y = centerY - h / 2;
        for(const char* p = num; *p != '\0'; ++p){
            if(*p < '0' || *p > '9') continue;
            int d = *p - '0';

            SDL_SetTextureColorMod(digit[d], color.r, color.g, color.b);
            SDL_Rect dst = {
                static_cast<int>(x),
                y,
                static_cast<int>(digitW[d] * scale + 0.5),
                h
            };
            SDL_RenderCopy(renderer, digit[d], NULL, &dst);
            x += digitW[d] * scale;
        }

        // ③ ラベル（" COMBO"）
        if(withLabel && label){
            int lh = static_cast<int>(labelH * scale + 0.5);
            SDL_SetTextureColorMod(label, color.r, color.g, color.b);
            SDL_Rect dst = {
                static_cast<int>(x),
                centerY - lh / 2,
                static_cast<int>(labelW * scale + 0.5),
                lh
            };
            SDL_RenderCopy(renderer, label, NULL, &dst);
        }
    }
};