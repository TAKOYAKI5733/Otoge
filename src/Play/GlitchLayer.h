#pragma once

// =====================================================================
//  GlitchLayer.h — 透明な絵に描いた物を、グリッチさせながら画面に出す
//
//  使い方：
//    1. 画面と同じ大きさの透明な絵（SDL_TEXTUREACCESS_TARGET）に、出したい物を描く
//    2. drawGlitchTexture(renderer, 絵, 範囲, 進み具合 p) で画面に貼る（p：0 → 1 で出てくる）
//
//  しくみ：範囲を横の帯（bands 本）に切って貼る
//    ・見えている帯の割合 = p（ばらばらの帯から現れて、最後に全部そろう）
//    ・帯を左右にずらす量・赤と青の色ずれ = (1 − p)²（はじめ強く、すぐ落ち着く）
//    ・帯の形は 30ms ごとに変わる（同じ数から同じ答えが出る乱数なので、変なちらつき方はしない）
//    ・透明な絵に半透明の物を描くと、色が「透明度を掛けた値」で入る（premultiplied）ので、貼るときもその前提の混ぜ方にする
//  選曲画面の曲の一覧のウィンドウと同じやり方
// =====================================================================

#include "GameCommon.h"
#include <algorithm>
#include <cstdint>

namespace glitchlayer{
    inline double hash01(uint32_t x){
        x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
        return (x & 0xFFFFFF) / static_cast<double>(0x1000000);
    }
}

//   srcOffsetY：絵の中身が画面より srcOffsetY だけ下にずらして描いてあるとき（レーンの絵は上下に余分に描いてある）
inline void drawGlitchTexture(SDL_Renderer* renderer, SDL_Texture* tex, const SDL_Rect& area, double p,
                              int bands = 18, float maxShift = 28.0f, float rgbShift = 6.0f, int srcOffsetY = 0){
    static const SDL_BlendMode PREMULTIPLIED = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    p = std::clamp(p, 0.0, 1.0);
    const float strength = static_cast<float>((1.0 - p) * (1.0 - p));
    const uint32_t seed = SDL_GetTicks() / 30u;
    const float bandH = static_cast<float>(area.h) / bands;
    const Uint8 mainA = static_cast<Uint8>(255.0 * std::clamp(0.5 + 0.5 * p, 0.0, 1.0));

    SDL_RenderSetClipRect(renderer, &area);   // ずらした帯が範囲の外にはみ出さないように
    for(int b = 0; b < bands; b++){
        const uint32_t k = seed * 977u + static_cast<uint32_t>(b) * 17u;
        if(glitchlayer::hash01(k) >= p * 1.15) continue;   // まだ現れていない帯
        float shift = 0.0f;
        if(glitchlayer::hash01(k * 3u + 5u) < 0.55){
            shift = static_cast<float>(glitchlayer::hash01(k * 7u + 11u) * 2.0 - 1.0) * maxShift * strength;
        }
        const int y0 = area.y + static_cast<int>(bandH * b);
        const int y1 = area.y + static_cast<int>(bandH * (b + 1));
        const SDL_Rect src = {area.x, y0 + srcOffsetY, area.w, y1 - y0};
        const SDL_FRect dst = {area.x + shift, static_cast<float>(y0), static_cast<float>(area.w), static_cast<float>(y1 - y0)};

        // 本体（premultiplied なので、薄くするときは色も同じだけ下げる）
        if(SDL_SetTextureBlendMode(tex, PREMULTIPLIED) != 0){
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);   // この混ぜ方が使えない描画方式では、ふつうの半透明で
        }
        SDL_SetTextureColorMod(tex, mainA, mainA, mainA);
        SDL_SetTextureAlphaMod(tex, mainA);
        SDL_RenderCopyF(renderer, tex, &src, &dst);

        // 色ずれ（赤を左、青を右に、足し算で薄く）
        if(strength > 0.03f){
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
            SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(150 * strength));
            SDL_SetTextureColorMod(tex, 255, 40, 60);
            SDL_FRect d1 = dst; d1.x -= rgbShift;
            SDL_RenderCopyF(renderer, tex, &src, &d1);
            SDL_SetTextureColorMod(tex, 40, 200, 255);
            SDL_FRect d2 = dst; d2.x += rgbShift;
            SDL_RenderCopyF(renderer, tex, &src, &d2);
        }
    }
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
    // 混ぜ方を premultiplied に戻しておく（この絵を、あとでふつうに貼るときのため）
    if(SDL_SetTextureBlendMode(tex, PREMULTIPLIED) != 0) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    // 走査線：白っぽい水色の細い横線を 2 本
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for(int k = 0; k < 2; k++){
        const int y = area.y + static_cast<int>(glitchlayer::hash01(seed * 31u + k * 101u) * area.h);
        SDL_SetRenderDrawColor(renderer, 200, 250, 255, static_cast<Uint8>(160 * strength));
        SDL_Rect line = {area.x, y, area.w, 2};
        SDL_RenderFillRect(renderer, &line);
    }
    SDL_RenderSetClipRect(renderer, nullptr);
}