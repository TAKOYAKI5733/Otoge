#pragma once

// =====================================================================
//  HoloWindow.h — ホログラムのウィンドウ（ChartCreate 以外の、ゲームの全部のウィンドウで使う）  src/HoloWindow.h
//
//  ■ 見た目
//    ・色は Cube の色（HOLO_WINDOW_COLOR = 立方体の辺の白っぽい水色 {200, 250, 255}）だけで作る
//    ・中身：暗い半透明の地 ＋ 上が少し明るい Cube の色のうすい層（上から光が当たっているように見せる）
//    ・枠：Cube の色の線。四隅だけ少し太く明るい「かぎ」を付ける。内側にもう 1 本うすい線
//    ・全体が少しだけ明るさをゆらす（ホログラムのちらつき）
//
//  ■ ブラウン管の線（走査線）
//    ・横の細い線を SCANLINE_GAP px おきに引く。線は SCANLINE_SPEED px/秒 でゆっくり下へ流れる
//    ・それとは別に、少し明るい太い帯（ROLL_BAND）が ROLL_PERIOD_MS かけて上から下へ通り抜ける
//      （ブラウン管の画面をゆっくり走る明るい帯）
//    ・線は 1 回の SDL_RenderFillRects でまとめて描く（線の本数が多くても描く命令は 1 回）
//
//  ■ たまに起きるグリッチ
//    ・時間を GLITCH_SLOT_MS ごとに区切り、区切りごとに GLITCH_CHANCE の確率でグリッチが起きる
//      （平均すると 1 つのウィンドウで 7 秒に 1 回くらい。90ms で消えるので「一瞬見える」くらい）
//    ・起きたとき：ウィンドウを横に切った細い帯 1〜2 本が左右に数 px ずれ、帯のふちに赤と青の色ずれが出る。
//      ずれた所は枠の縦の線もいっしょにずれる
//    ・どのウィンドウがいつグリッチするかは seed（ウィンドウごとに違う数）で決まる。
//      同じ seed・同じ時刻なら同じ形になる（holoWindowHash：同じ数を入れると同じ答えになる乱数）
//
//  ■ 使い方
//      drawHoloWindow(renderer, rect, 1.0, true, 3, 11u);         // ウィンドウ本体（中身はこの後に描く）
//      drawHoloWindowTab(renderer, font, "MENU", rect, 255);     // 上の辺の左に乗せるタブ
//    alpha（0〜1）：出る・消えるときの濃さ。focused：操作中のウィンドウなら true（枠が濃くなる）
// =====================================================================

#include "GameCommon.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

inline constexpr SDL_Color HOLO_WINDOW_COLOR = {200, 250, 255, 255};   // Cube の色
inline constexpr SDL_Color HOLO_WINDOW_BASE  = {  6,  18,  26, 255};   // 地の暗い色（少しだけ青緑に寄せる）
inline constexpr SDL_Color HOLO_WINDOW_TEXT  = { 10,  24,  32, 255};   // タブの上の文字の色

inline constexpr Uint8  HOLO_BASE_ALPHA      = 175;    // 地の濃さ
inline constexpr Uint8  HOLO_TINT_TOP_ALPHA  = 30;     // Cube の色の層：上の端の濃さ
inline constexpr Uint8  HOLO_TINT_BOT_ALPHA  = 8;      // Cube の色の層：下の端の濃さ
inline constexpr Uint8  HOLO_BORDER_FOCUS    = 235;    // 操作中のウィンドウの枠の濃さ
inline constexpr Uint8  HOLO_BORDER_NORMAL   = 110;    // それ以外の枠の濃さ
inline constexpr int    HOLO_CORNER_LEN      = 16;     // 四隅のかぎの長さ(px)

inline constexpr int    SCANLINE_GAP         = 4;      // 走査線の間隔(px)
inline constexpr Uint8  SCANLINE_ALPHA       = 16;     // 走査線の濃さ
inline constexpr double SCANLINE_SPEED       = 6.0;    // 走査線が下へ流れる速さ(px/秒)
inline constexpr double ROLL_PERIOD_MS       = 6500.0; // 明るい帯が上から下まで通る時間
inline constexpr Uint8  ROLL_BAND_ALPHA      = 18;     // 明るい帯の一番明るい所の濃さ

inline constexpr Uint32 GLITCH_SLOT_MS       = 90;     // グリッチの区切りの長さ（= 1 回のグリッチが見えている時間）
inline constexpr double GLITCH_CHANCE        = 0.013;  // 区切りごとにグリッチが起きる確率
inline constexpr int    GLITCH_SHIFT_MAX     = 10;     // 帯が左右にずれる最大(px)

// 同じ数を入れると、いつも同じ 0〜1 の数が返る乱数
inline double holoWindowHash(uint32_t x){
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return (x & 0xFFFFFF) / static_cast<double>(0x1000000);
}

// y0〜y1 の横長の帯を、上の色 c0 から下の色 c1 へなめらかに変わる色で塗る
inline void holoWindowGradient(SDL_Renderer* r, float x0, float x1, float y0, float y1, SDL_Color c0, SDL_Color c1){
    if(y1 <= y0 || x1 <= x0) return;
    SDL_Vertex v[4] = {
        {{x0, y0}, c0, {0, 0}},
        {{x1, y0}, c0, {0, 0}},
        {{x1, y1}, c1, {0, 0}},
        {{x0, y1}, c1, {0, 0}},
    };
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}

// 帯の一部（y0〜y1 のうち、クリップ範囲 cy0〜cy1 に入る所）だけを、色を途中の値に直して塗る
inline void holoWindowGradientClipped(SDL_Renderer* r, float x0, float x1, float y0, float y1, Uint8 a0, Uint8 a1,
                                      float cy0, float cy1, SDL_Color c){
    const float ya = std::max(y0, cy0), yb = std::min(y1, cy1);
    if(yb <= ya || y1 <= y0) return;
    auto alphaAt = [&](float y){ return static_cast<Uint8>(a0 + (a1 - a0) * ((y - y0) / (y1 - y0))); };
    holoWindowGradient(r, x0, x1, ya, yb, SDL_Color{c.r, c.g, c.b, alphaAt(ya)}, SDL_Color{c.r, c.g, c.b, alphaAt(yb)});
}

// ホログラムのウィンドウ本体
//   rect：ウィンドウの四角 / alpha：濃さ（0〜1）/ focused：操作中か / borderW：枠の太さ / seed：グリッチの起き方を決める数
inline void drawHoloWindow(SDL_Renderer* r, const SDL_Rect& rect, double alpha = 1.0, bool focused = true,
                           int borderW = 3, uint32_t seed = 0){
    if(rect.w <= 0 || rect.h <= 0 || alpha <= 0.0) return;
    const Uint32 ticks = SDL_GetTicks();
    const double t = ticks / 1000.0;
    const double flick = 0.955 + 0.045 * std::sin(t * 7.3 + seed * 0.37) * std::sin(t * 2.9 + seed);
    const double a = std::clamp(alpha, 0.0, 1.0) * flick;
    auto A = [&](double v){ return static_cast<Uint8>(std::clamp(v * a, 0.0, 255.0)); };
    const SDL_Color C = HOLO_WINDOW_COLOR;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const float x0 = static_cast<float>(rect.x), x1 = static_cast<float>(rect.x + rect.w);
    const float y0 = static_cast<float>(rect.y), y1 = static_cast<float>(rect.y + rect.h);

    // 1. 地 ＋ 上が少し明るい Cube の色の層
    SDL_SetRenderDrawColor(r, HOLO_WINDOW_BASE.r, HOLO_WINDOW_BASE.g, HOLO_WINDOW_BASE.b, A(HOLO_BASE_ALPHA));
    SDL_RenderFillRect(r, &rect);
    holoWindowGradient(r, x0, x1, y0, y1, SDL_Color{C.r, C.g, C.b, A(HOLO_TINT_TOP_ALPHA)}, SDL_Color{C.r, C.g, C.b, A(HOLO_TINT_BOT_ALPHA)});

    // 2. 走査線（ゆっくり下へ流れる）
    {
        static std::vector<SDL_Rect> lines;
        lines.clear();
        const int off = static_cast<int>(std::fmod(t * SCANLINE_SPEED, static_cast<double>(SCANLINE_GAP)));
        for(int y = rect.y + off; y < rect.y + rect.h; y += SCANLINE_GAP){
            lines.push_back(SDL_Rect{rect.x, y, rect.w, 1});
        }
        SDL_SetRenderDrawColor(r, C.r, C.g, C.b, A(SCANLINE_ALPHA));
        if(!lines.empty()) SDL_RenderFillRects(r, lines.data(), static_cast<int>(lines.size()));
    }

    // 3. 上から下へ通り抜ける明るい帯
    {
        const float bandH = std::clamp(rect.h * 0.25f, 40.0f, 160.0f);
        const double phase = std::fmod(ticks + seed * 811.0, ROLL_PERIOD_MS) / ROLL_PERIOD_MS;
        const float top = y0 - bandH + static_cast<float>(phase) * (rect.h + bandH);
        const float mid = top + bandH * 0.5f;
        holoWindowGradientClipped(r, x0, x1, top, mid, 0, A(ROLL_BAND_ALPHA), y0, y1, C);
        holoWindowGradientClipped(r, x0, x1, mid, top + bandH, A(ROLL_BAND_ALPHA), 0, y0, y1, C);
    }

    // 4. 枠：外の線 ＋ 内側のうすい線 ＋ 四隅のかぎ
    const Uint8 borderA = A(focused ? HOLO_BORDER_FOCUS : HOLO_BORDER_NORMAL);
    SDL_SetRenderDrawColor(r, C.r, C.g, C.b, borderA);
    for(int k = 0; k < borderW; k++){
        SDL_Rect b = {rect.x + k, rect.y + k, rect.w - k * 2, rect.h - k * 2};
        SDL_RenderDrawRect(r, &b);
    }
    {
        const int in = borderW + 3;
        if(rect.w > in * 2 && rect.h > in * 2){
            SDL_Rect b = {rect.x + in, rect.y + in, rect.w - in * 2, rect.h - in * 2};
            SDL_SetRenderDrawColor(r, C.r, C.g, C.b, A(35));
            SDL_RenderDrawRect(r, &b);
        }
    }
    {
        const int L = std::min({HOLO_CORNER_LEN, rect.w / 2, rect.h / 2});
        const int T = borderW + 1;
        const SDL_Rect corners[8] = {
            {rect.x - 1,              rect.y - 1,              L, T}, {rect.x - 1,              rect.y - 1,              T, L},
            {rect.x + rect.w + 1 - L, rect.y - 1,              L, T}, {rect.x + rect.w + 1 - T, rect.y - 1,              T, L},
            {rect.x - 1,              rect.y + rect.h + 1 - T, L, T}, {rect.x - 1,              rect.y + rect.h + 1 - L, T, L},
            {rect.x + rect.w + 1 - L, rect.y + rect.h + 1 - T, L, T}, {rect.x + rect.w + 1 - T, rect.y + rect.h + 1 - L, T, L},
        };
        SDL_SetRenderDrawColor(r, C.r, C.g, C.b, A(focused ? 255 : 170));
        SDL_RenderFillRects(r, corners, 8);
    }

    // 5. たまに起きるグリッチ（横に切った細い帯が数 px ずれる ＋ 赤と青の色ずれ）
    const uint32_t slot = ticks / GLITCH_SLOT_MS;
    const uint32_t key = slot * 2654435761u ^ (seed * 40503u + 977u);
    if(holoWindowHash(key) < GLITCH_CHANCE){
        const int bands = 1 + static_cast<int>(holoWindowHash(key + 1u) * 2.0);
        for(int b = 0; b < bands; b++){
            const uint32_t k = key * 31u + static_cast<uint32_t>(b) * 7u;
            const int hh = 3 + static_cast<int>(holoWindowHash(k + 2u) * 10.0);
            const int by = rect.y + static_cast<int>(holoWindowHash(k + 3u) * std::max(1, rect.h - hh));
            const int dir = holoWindowHash(k + 4u) < 0.5 ? -1 : 1;
            const int shift = dir * (3 + static_cast<int>(holoWindowHash(k + 5u) * (GLITCH_SHIFT_MAX - 3)));

            SDL_Rect band = {rect.x + shift, by, rect.w, hh};
            SDL_SetRenderDrawColor(r, C.r, C.g, C.b, A(26));
            SDL_RenderFillRect(r, &band);

            SDL_Rect red  = {band.x - 3, by, band.w, 1};
            SDL_Rect blue = {band.x + 3, by + hh - 1, band.w, 1};
            SDL_SetRenderDrawColor(r, 255, 60, 90, A(55));
            SDL_RenderFillRect(r, &red);
            SDL_SetRenderDrawColor(r, 60, 160, 255, A(55));
            SDL_RenderFillRect(r, &blue);

            const SDL_Rect edges[2] = {{rect.x + shift, by, borderW, hh}, {rect.x + rect.w - borderW + shift, by, borderW, hh}};
            SDL_SetRenderDrawColor(r, C.r, C.g, C.b, borderA);
            SDL_RenderFillRects(r, edges, 2);
        }
    }
}

// ウィンドウの上の辺の左に乗せるタイトルのタブ（Cube の色で塗り、文字は暗い色）
//   alpha：0〜255
inline void drawHoloWindowTab(SDL_Renderer* r, TTF_Font* font, const char* title, const SDL_Rect& win, Uint8 alpha){
    if(!font || !title || alpha == 0) return;
    SDL_Surface* s = TTF_RenderUTF8_Blended(font, title, HOLO_WINDOW_TEXT);
    if(!s) return;
    const SDL_Rect tab = {win.x, win.y - s->h - 6, s->w + 28, s->h + 6};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, HOLO_WINDOW_COLOR.r, HOLO_WINDOW_COLOR.g, HOLO_WINDOW_COLOR.b, static_cast<Uint8>(alpha * 0.92));
    SDL_RenderFillRect(r, &tab);
    const double t = SDL_GetTicks() / 1000.0;
    const int off = static_cast<int>(std::fmod(t * SCANLINE_SPEED, static_cast<double>(SCANLINE_GAP)));
    SDL_SetRenderDrawColor(r, HOLO_WINDOW_BASE.r, HOLO_WINDOW_BASE.g, HOLO_WINDOW_BASE.b, static_cast<Uint8>(alpha * 0.10));
    for(int y = tab.y + off; y < tab.y + tab.h; y += SCANLINE_GAP){
        SDL_Rect line = {tab.x, y, tab.w, 1};
        SDL_RenderFillRect(r, &line);
    }
    if(SDL_Texture* tex = SDL_CreateTextureFromSurface(r, s)){
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(tex, alpha);
        SDL_Rect dst = {tab.x + 14, tab.y + 3, s->w, s->h};
        SDL_RenderCopy(r, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(s);
}