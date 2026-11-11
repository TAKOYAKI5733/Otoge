#pragma once

#include "GameCommon.h"
#include "Play/GameContext.h"
#include "Play/type.h"
#include "Play/roundedRect.h"

// ===== ノーツの形（判定ボックスとノーツ描画の両方で使う。ここだけ直せば両方そろう） =====
inline constexpr float NOTE_SIDE_MARGIN = 8.0f;    // ノーツの左右それぞれの、レーンとの隙間(px)
inline constexpr float NOTE_HEAD_H      = 30.0f;   // ノーツのヘッドの高さ(px)
inline constexpr float NOTE_RADIUS      = 4.0f;    // ノーツの角の丸み(px)

// ===== 隠れているレーン（laneActive が false）のノーツ =====
//   レーンが閉じている間のノーツは、「本来の色のまま半透明」の中身 ＋ 「濃いめの縁取り」で描く。
//     ・中身が半透明なので、開いているレーンのノーツとは見分けがつく
//     ・縁取りがあるので、レーンの背景が無い所（背景の灰色・青のグラデーション）でも形がはっきり見える
//   値は 0〜255（小さいほど透明）。LANE ノーツはレーンを開け閉めするので対象外（いつも通り見える）
inline constexpr Uint8 HIDDEN_NOTE_ALPHA         = 110;   // ヘッド・ロングの帯の中身
inline constexpr Uint8 HIDDEN_NOTE_INNER_ALPHA   = 70;    // ロングの芯（帯より少し薄く）
inline constexpr Uint8 HIDDEN_NOTE_OUTLINE_ALPHA = 220;   // 縁取り
inline constexpr float HIDDEN_NOTE_OUTLINE_W     = 2.0f;  // 縁取りの太さ(px)

// ===== ロングノーツの芯 =====
//   芯は帯の真ん中の細い線。両端を帯より内側で止めて、帯より短くする
inline constexpr float  LONG_CORE_WIDTH_RATIO = 0.2f;    // 帯の幅に対する芯の幅
inline constexpr float  LONG_CORE_TAIL_MARGIN = 14.0f;   // 終点側：帯の端からどれだけ内側で止めるか(px)
inline constexpr float  LONG_CORE_HEAD_GAP    = 8.0f;    // ヘッド側：ヘッドの上の端からどれだけ離して止めるか(px)


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

// レーンの背景・区切り線・判定ボックスを描く
//   yOffset: レーンを傾けるとき（Play/LaneTilt.h）は、画面より上の延長部分も描くため、
//            全体を yOffset px 下にずらし、レーンを上の端（y = 0）から SCREEN_H + yOffset まで伸ばして描く。
//            傾けないときは 0（今まで通り）
//   drawLines: レーンの区切り線を描くか。傾けるときは false にする
//              （テクスチャに描いた 1px の線を斜めに引き伸ばすとギザギザになるので、LaneTilt が画面に直接描く）
//   laneLength: レーンを上の端から何 px まで描くか。-1 なら SCREEN_H + yOffset（傾けるときは、下の延長部分も含めた長さを渡す）
//   headH: 判定ボックスの高さ。傾けるときは、画面でノーツと同じ太さに見えるよう LaneTilt::thicknessScale を掛けた値を渡す
inline void draw_waku_init(SDL_Renderer* renderer, Tex& tex, Sq& sq, bool laneActive[], int yOffset = 0, bool drawLines = true,
                           int laneLength = -1, float headH = NOTE_HEAD_H){
    int laneWidth = SCREEN_W / 16;
    int startX = SCREEN_W / 2 - (laneWidth * 3);
    const int laneH = (laneLength > 0) ? laneLength : SCREEN_H + yOffset;   // レーンの長さ（延長部分を含む）

    // レーンの背景（偶数・奇数で色を変える）
    auto laneColor = [](int i){
        return (i % 2 == 0) ? SDL_Color{0, 0, 5, 255} : SDL_Color{25, 25, 35, 255};
    };

    for(int i = 0; i < 6; i++){
        if(!laneActive[i]) continue;
        SDL_Rect rect = {startX + (laneWidth * i), 0, laneWidth, laneH};
        SDL_Color c = laneColor(i);
        if(g_tiltProj){
            // 傾けているとき：レーンの四角を、角の点を傾けた位置に変換して直接描く（帯に分けるのは、ぼかしの濃さを正しく付けるため）
            tiltRect(renderer, static_cast<float>(rect.x), 0.0f, static_cast<float>(rect.w), static_cast<float>(rect.h), c, nullptr, 48);
            continue;
        }
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
        SDL_RenderFillRect(renderer, &rect);
    }

    for(int i = 0; i <= 6 && drawLines; i++){
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        int laneX = startX + (laneWidth * i);
        SDL_RenderDrawLine(renderer, laneX, 0, laneX, laneH);
    }

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_Rect wakuRect = sq.waku_init;
    wakuRect.y += yOffset;
    if(g_tiltProj){
        // キーの文字の画像も、四角の角を変換して貼る（帯に分けて、奥の詰まり方を正しくする）
        tiltRect(renderer, static_cast<float>(wakuRect.x), static_cast<float>(wakuRect.y),
                 static_cast<float>(wakuRect.w), static_cast<float>(wakuRect.h), SDL_Color{255, 255, 255, 255}, tex.waku_init, 8);
    }
    else{
        SDL_RenderCopy(renderer, tex.waku_init, NULL, &wakuRect);
    }

    // 判定ボックス：横線の代わりに、ノーツのヘッドがちょうど収まる角丸の枠を有効なレーンごとに置く
    //   外側の角丸（枠の色）を描き、その内側を同じ形でレーンの色に塗り戻すと、角丸の「枠線」になる
    const float judgeY = static_cast<float>(SCREEN_H * (3.0 / 4.0)) + static_cast<float>(yOffset);

    RoundedRectBatch boxBatch;
    for(int i = 0; i < 6; i++){
        // レーンが隠れていても、判定ボックスだけは描く（laneActive は見ない）
        const float noteX = static_cast<float>(startX + laneWidth * i) + JUDGE_BOX_SIDE_MARGIN;
        const float noteW = static_cast<float>(laneWidth) - JUDGE_BOX_SIDE_MARGIN * 2.0f;
        const float noteY = judgeY - headH * 0.5f;

        // 外形はノーツと同じ。枠線は内側へ JUDGE_BOX_BORDER px、内側は塗らない
        boxBatch.addOutline(noteX, noteY, noteW, headH, NOTE_RADIUS, JUDGE_BOX_BORDER, JUDGE_BOX_COLOR);
    }
    boxBatch.flush(renderer);

    return;
}