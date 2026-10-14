#pragma once

#include "GameCommon.h"
#include "Play/GameContext.h"
#include "Play/type.h"
#include "Play/roundedRect.h"

// ===== ノーツの形（判定ボックスとノーツ描画の両方で使う。ここだけ直せば両方そろう） =====
inline constexpr float NOTE_SIDE_MARGIN = 8.0f;    // ノーツの左右それぞれの、レーンとの隙間(px)
inline constexpr float NOTE_HEAD_H      = 30.0f;   // ノーツのヘッドの高さ(px)
inline constexpr float NOTE_RADIUS      = 4.0f;    // ノーツの角の丸み(px)

// ===== 判定ボックス =====
inline constexpr float     JUDGE_BOX_BORDER = 3.0f;
inline constexpr float JUDGE_BOX_SIDE_MARGIN = 2.0f;   // 箱の左右それぞれの、レーンの端からの距離(px)             // 枠の太さ                  // ノーツと枠の間のすき間
inline constexpr SDL_Color JUDGE_BOX_COLOR  = {255, 255, 255, 255};  // 枠の色

// ===== 判定ボックスの押下エフェクト =====
inline constexpr SDL_Color JUDGE_FILL_COLOR   = {255, 255, 255, 255};   // 箱を埋める色
inline constexpr double    JUDGE_FLASH_MS     = 120.0;   // 押した瞬間の強い光が落ち着くまで
inline constexpr double    JUDGE_RELEASE_MS   = 150.0;   // 離してから消えるまで
inline constexpr double    JUDGE_HOLD_LEVEL   = 0.85;    // 押し続けている間の明るさ（0〜1）

// レーンごとの「いつ押したか・いつ離したか」を覚えておく
struct JudgeBoxFx{
    bool   held[6];
    double pressMs[6];
    double releaseMs[6];

    JudgeBoxFx(){
        for(int i = 0; i < 6; i++){
            held[i] = false;
            pressMs[i] = releaseMs[i] = -1e9;
        }
    }

    // 毎フレーム、今押されているかを渡す。押した瞬間・離した瞬間の時刻を記録する
    void update(const bool pressed[], double nowMs){
        for(int i = 0; i < 6; i++){
            if(pressed[i] && !held[i]) pressMs[i] = nowMs;
            if(!pressed[i] && held[i]) releaseMs[i] = nowMs;
            held[i] = pressed[i];
        }
    }

    // 押している間の明るさ：押した瞬間 1.0 → JUDGE_HOLD_LEVEL へ落ち着く
    static double heldLevel(double sincePress){
        double f = std::clamp(1.0 - sincePress / JUDGE_FLASH_MS, 0.0, 1.0);
        return JUDGE_HOLD_LEVEL + (1.0 - JUDGE_HOLD_LEVEL) * f * f;
    }

    // レーン i の今の明るさ（0 = 何もなし、1 = 一番明るい）
    double level(int i, double nowMs) const{
        if(held[i]){
            double lv = heldLevel(nowMs - pressMs[i]);
            if(nowMs - pressMs[i] > JUDGE_FLASH_MS){
                lv += 0.06 * std::sin((nowMs - pressMs[i]) * 0.012);   // 長押し中はゆっくり脈打つ
            }
            return lv;
        }
        // 離した後：離した瞬間の明るさから 0 へ
        double r = (nowMs - releaseMs[i]) / JUDGE_RELEASE_MS;
        if(r >= 1.0) return 0.0;
        double startLv = heldLevel(releaseMs[i] - pressMs[i]);
        return startLv * (1.0 - r);
    }
};

inline void draw_waku_init(SDL_Renderer* renderer, Tex& tex, Sq& sq, bool laneActive[]){
    int laneWidth = SCREEN_W / 16;
    int startX = SCREEN_W / 2 - (laneWidth * 3);

    // レーンの背景（偶数・奇数で色を変える）
    auto laneColor = [](int i){
        return (i % 2 == 0) ? SDL_Color{0, 0, 5, 255} : SDL_Color{25, 25, 35, 255};
    };

    for(int i = 0; i < 6; i++){
        if(!laneActive[i]) continue;
        SDL_Rect rect = {startX + (laneWidth * i), 0, laneWidth, SCREEN_H};
        SDL_Color c = laneColor(i);
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
        SDL_RenderFillRect(renderer, &rect);
    }

    for(int i = 0; i <= 6; i++){
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        int laneX = startX + (laneWidth * i);
        SDL_RenderDrawLine(renderer, laneX, 0, laneX, SCREEN_W);
    }

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderCopy(renderer, tex.waku_init, NULL, &sq.waku_init);

    // 判定ボックス：横線の代わりに、ノーツのヘッドがちょうど収まる角丸の枠を有効なレーンごとに置く
    //   外側の角丸（枠の色）を描き、その内側を同じ形でレーンの色に塗り戻すと、角丸の「枠線」になる
    const float judgeY = static_cast<float>(SCREEN_H * (3.0 / 4.0));
    const float b      = JUDGE_BOX_BORDER;

    RoundedRectBatch boxBatch;
    for(int i = 0; i < 6; i++){
        // レーンが隠れていても、判定ボックスだけは描く（laneActive は見ない）
        const float noteX = static_cast<float>(startX + laneWidth * i) + JUDGE_BOX_SIDE_MARGIN;
        const float noteW = static_cast<float>(laneWidth) - JUDGE_BOX_SIDE_MARGIN * 2.0f;
        const float noteY = judgeY - NOTE_HEAD_H * 0.5f;

        // 外形はノーツと同じ。枠線は内側へ JUDGE_BOX_BORDER px、内側は塗らない
        boxBatch.addOutline(noteX, noteY, noteW, NOTE_HEAD_H, NOTE_RADIUS, JUDGE_BOX_BORDER, JUDGE_BOX_COLOR);
    }
    boxBatch.flush(renderer);

    return;
}