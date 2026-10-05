//アニメーションやグラフィックス関係のヘッダファイル
#pragma once

#include <SDL2/SDL.h>
#include <cmath>

// ==========================================
// 焼き込みテクスチャのキャッシュ
// （最初に必要になったときに1度だけ作り、ゲーム終了時に releaseGraphicsCache() で破棄）
// ==========================================
struct GraphicsCache{
    SDL_Texture* gradient = nullptr;  // 背景グラデーション
    SDL_Texture* beam = nullptr;      // キービーム
};
inline GraphicsCache g_gfxCache;

// main.cpp の SDL_DestroyRenderer より前に呼ぶ
inline void releaseGraphicsCache(){
    if(g_gfxCache.gradient){ SDL_DestroyTexture(g_gfxCache.gradient); g_gfxCache.gradient = nullptr; }
    if(g_gfxCache.beam){ SDL_DestroyTexture(g_gfxCache.beam); g_gfxCache.beam = nullptr; }
}

// 横 screenW/4 列 × 縦1px：元コードの4px幅の帯1本 = 1ピクセル
inline SDL_Texture* buildGradientTexture(SDL_Renderer* renderer, int screenW){
    const int cols = screenW / 4;
    const int half = screenW / 2;
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, cols, 1, 32, SDL_PIXELFORMAT_RGBA32);
    if(!s) return nullptr;

    Uint32* px = static_cast<Uint32*>(s->pixels);
    for(int c = 0; c < cols; c++){
        int x = c * 4;
        double ratio = (x < half)
            ? static_cast<double>(x) / half
            : static_cast<double>(screenW - x) / (screenW - half);
        Uint8 r = static_cast<Uint8>(10 * ratio);
        Uint8 g = static_cast<Uint8>(20 * ratio);
        Uint8 b = static_cast<Uint8>(90 * ratio); // 青は最大値で焼き、ColorModで揺らす
        px[c] = SDL_MapRGBA(s->format, r, g, b, 255);
    }

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, s);
    SDL_FreeSurface(s);
    if(tex){
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest); // 元と同じ4px刻みの帯
    }
    return tex;
}

inline void GradientBackground(SDL_Renderer* renderer, int screenW, int screenH, uint32_t musicTime){
    if(!g_gfxCache.gradient){
        g_gfxCache.gradient = buildGradientTexture(renderer, screenW);
    }

    double wave = (std::sin(musicTime * 0.001) + 1.0) / 2.0;
    int baseBlue = static_cast<int>(60 + wave * 30);

    // ① ベースの灰色を1回で塗る
    SDL_SetRenderDrawColor(renderer, 55, 55, 55, 255);
    SDL_Rect full = {0, 0, screenW, screenH};
    SDL_RenderFillRect(renderer, &full);

    // ② グラデーションを加算合成で1回貼る（480回 → 2回）
    if(g_gfxCache.gradient){
        SDL_SetTextureColorMod(g_gfxCache.gradient, 255, 255, static_cast<Uint8>(baseBlue * 255 / 90));
        SDL_RenderCopy(renderer, g_gfxCache.gradient, NULL, &full);
    }
}

// キービーム用：横1px×縦40px、下ほど不透明な白
inline SDL_Texture* getBeamTexture(SDL_Renderer* renderer){
    if(g_gfxCache.beam) return g_gfxCache.beam;

    const int strips = 40;
    const int baseAlpha = 150;
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, 1, strips, 32, SDL_PIXELFORMAT_RGBA32);
    if(!s) return nullptr;

    Uint32* px = static_cast<Uint32*>(s->pixels);
    for(int row = 0; row < strips; row++){
        int stripIndexFromBottom = strips - 1 - row;
        double verticalFade = 1.0 - static_cast<double>(stripIndexFromBottom) / strips;
        Uint8 a = static_cast<Uint8>(baseAlpha * verticalFade);
        px[row * (s->pitch / 4)] = SDL_MapRGBA(s->format, 255, 255, 255, a);
    }

    g_gfxCache.beam = SDL_CreateTextureFromSurface(renderer, s);
    SDL_FreeSurface(s);
    if(g_gfxCache.beam){
        SDL_SetTextureBlendMode(g_gfxCache.beam, SDL_BLENDMODE_BLEND);
    }
    return g_gfxCache.beam;
}

// ==========================================
// イージング関数（既存）
// ==========================================
inline double easeOutCubic(double t){
    return 1.0 - std::pow(1.0 - t, 3.0);
}

inline double easeInCubic(double t){
    return t * t * t;
}