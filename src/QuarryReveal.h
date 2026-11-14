#pragma once

// =====================================================================
//  QuarryReveal.h — ウィンドウを「Cube から採掘機（Quarry）のように」作り出す出方
//
//  ■ 流れ（時間は QuarryReveal::TOTAL_MS ぶん。ms = 出し始めてからの時間）
//    ① 光の線：Cube の中心から、ウィンドウの一番近い角へ、光の線が伸びる              （BEAM_MS）
//    ② 枠（縦）：その角から、ウィンドウの縦の辺が伸びる（y 軸の向き）                    （VERT_MS）
//    ③ 枠（横）：縦の辺が横へ広がって、ウィンドウの大きさの枠になる（x 軸の向き）        （HORZ_MS）
//                 枠の中には、Cube の色をさらに透明にした薄い斜線を引く
//    ④ 生成：枠の上の端から下へ、中身が少しずつできていく                                （BUILD_MS）
//             できていく先頭には横の光の線。生成中は大きなグリッチ（drawBuilding）：
//               ・先頭のすぐ上の帯が、細かく大きく左右にずれる
//               ・一瞬だけ、できた所の一部の塊が別の場所へ飛ぶ（元の場所は穴になる）
//               ・3D メガネのように赤と青に分かれる（ときどき大きく）
//             まだできていない所には、斜線が残っている
//    ①の光の線は、②③の間にだんだん薄くなって消える
//
//  ■ 使い方
//    1. 出したいウィンドウ（枠・タイトルのタブ・中身）を、画面と同じ大きさの透明な絵（content）に描く
//       （透明な絵に描くと色が「透明度を掛けた値」で入る：premultiplied）
//    2. QuarryReveal::draw(renderer, content, 枠の四角, 中身の範囲, Cube の中心, 経過時間) を毎フレーム呼ぶ
//    3. QuarryReveal::done(経過時間) が true になったら、ふつうに画面へ直接描く
// =====================================================================

#include "GameCommon.h"
#include "Play/PlaySceneManager.h"   // drawThickLine・HOLO_FRONT_COLOR など
#include <algorithm>
#include <cmath>
#include <cstdint>

struct QuarryReveal{
    // ----- 調整用 -----
    static constexpr double BEAM_MS  = 110.0;
    static constexpr double VERT_MS  = 100.0;
    static constexpr double HORZ_MS  = 120.0;
    static constexpr double BUILD_MS = 300.0;
    static constexpr double TOTAL_MS = BEAM_MS + VERT_MS + HORZ_MS + BUILD_MS;

    static constexpr SDL_Color LINE_COLOR  = {200, 250, 255, 255};   // 光の線・枠（Cube の辺と同じ色）
    static constexpr Uint8     LINE_ALPHA  = 210;
    static constexpr float     LINE_W      = 2.0f;
    static constexpr Uint8     HATCH_ALPHA = 40;                     // 斜線（Cube の色をさらに透明に）
    static constexpr int       HATCH_GAP   = 16;                     // 斜線の間隔(px)
    // ----- 生成中のグリッチ（drawBuilding）-----
    static constexpr int    GLITCH_ZONE    = 90;      // 生成中の先頭の、細かくずれる帯の高さ(px)
    static constexpr float  GLITCH_SHIFT   = 42.0f;   // その帯を左右にずらす最大(px)
    static constexpr double SLOT_MS        = 45.0;    // 「一瞬」の長さ：この時間ごとにずれ方を変える
    static constexpr double CHUNK_CHANCE   = 0.55;    // 一瞬ごとに、できた所の一部が別の場所へずれる確率
    static constexpr int    CHUNK_MAX      = 2;       //   一度にずれる塊の数の最大
    static constexpr float  CHUNK_JUMP_X   = 160.0f;  //   ずれる量の最大(px)：横
    static constexpr float  CHUNK_JUMP_Y   = 40.0f;   //                      縦
    static constexpr float  SPLIT_BASE     = 7.0f;    // 3D メガネのような赤・青の分かれ(px)：いつも
    static constexpr float  SPLIT_SPIKE    = 26.0f;   //   ときどき大きく分かれるときの量(px)
    static constexpr double SPIKE_CHANCE   = 0.3;     //   大きく分かれる確率（一瞬ごと）
    static constexpr Uint8  SPLIT_ALPHA    = 150;     // 赤・青の濃さ
    static constexpr double JOLT_CHANCE    = 0.15;    // 一瞬ごとに、できた所全体が横へずれる確率
    static constexpr float  JOLT_X         = 22.0f;   //   そのずれの最大(px)

    static bool done(double ms){ return ms >= TOTAL_MS; }

    // frame：ウィンドウの枠の四角　area：中身として見せる範囲（タイトルのタブなど、枠の外にはみ出す物も入れる）
    // (cubeX, cubeY)：光の線が出る所（Cube の中心）
    // keepBeam：光の線を消さずに残す（Cube のウィンドウのように、できあがった後も線で結ぶ物用）。
    //           そのときの線の色・太さは beamColor / beamW（できあがった後の線と同じにすると、つなぎ目が見えない）
    static void draw(SDL_Renderer* r, SDL_Texture* content, const SDL_Rect& frame, const SDL_Rect& area,
                     float cubeX, float cubeY, double ms,
                     bool keepBeam = false, SDL_Color beamColor = LINE_COLOR, float beamW = LINE_W){
        if(ms <= 0.0) return;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

        // Cube に一番近い角（枠はそこから伸びる）
        const float L = static_cast<float>(frame.x), T = static_cast<float>(frame.y);
        const float R = static_cast<float>(frame.x + frame.w), B = static_cast<float>(frame.y + frame.h);
        const float cx = (std::fabs(cubeX - L) <= std::fabs(cubeX - R)) ? L : R;   // 角の x
        const float cy = (std::fabs(cubeY - T) <= std::fabs(cubeY - B)) ? T : B;   // 角の y
        const float ox = (cx == L) ? R : L;                                         // 反対側の x
        const float oy = (cy == T) ? B : T;                                         // 反対側の y

        const double t1 = BEAM_MS, t2 = t1 + VERT_MS, t3 = t2 + HORZ_MS;
        auto ease = [](double t){ t = std::clamp(t, 0.0, 1.0); return 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t); };
        SDL_Color lc = LINE_COLOR;
        lc.a = LINE_ALPHA;

        // ① 光の線（伸びきったら、枠ができる間にだんだん薄くなる）
        {
            const float e = static_cast<float>(ease(ms / BEAM_MS));
            float fade = (ms < t1) ? 1.0f : static_cast<float>(std::clamp(1.0 - (ms - t1) / (t3 - t1), 0.0, 1.0));
            if(keepBeam) fade = 1.0f;
            if(fade > 0.0f){
                SDL_Color bc = beamColor;
                bc.a = static_cast<Uint8>((keepBeam ? beamColor.a : LINE_ALPHA) * fade);
                const float ex = cubeX + (cx - cubeX) * e, ey = cubeY + (cy - cubeY) * e;
                drawThickLine(r, cubeX, cubeY, ex, ey, keepBeam ? beamW : LINE_W, bc);
                if(ms < t1){   // 先頭の小さな光
                    SDL_SetRenderDrawColor(r, 255, 255, 255, 220);
                    SDL_FRect tip = {ex - 3.0f, ey - 3.0f, 6.0f, 6.0f};
                    SDL_RenderFillRectF(r, &tip);
                }
            }
        }
        if(ms < t1) return;

        // ② 縦の辺：角から反対側の y へ
        if(ms < t2){
            const float e = static_cast<float>(ease((ms - t1) / VERT_MS));
            drawThickLine(r, cx, cy, cx, cy + (oy - cy) * e, LINE_W, lc);
            return;
        }

        // ③ 横に広がる：今の枠の四角（角の x から、反対側の x へ広がる）
        const float ex = (ms < t3) ? static_cast<float>(ease((ms - t2) / HORZ_MS)) : 1.0f;
        const float curX = cx + (ox - cx) * ex;
        const SDL_FRect cur = {std::min(cx, curX), T, std::fabs(curX - cx), B - T};

        // ④ 生成：上の端から下へ（scanY より上ができている）
        const double bp = (ms < t3) ? 0.0 : std::clamp((ms - t3) / BUILD_MS, 0.0, 1.0);
        const float areaTop = static_cast<float>(std::min(area.y, frame.y));
        const float areaBot = static_cast<float>(std::max(area.y + area.h, frame.y + frame.h));
        const float scanY = areaTop + (areaBot - areaTop) * static_cast<float>(bp);

        // 斜線：まだできていない所（scanY より下）だけ
        if(cur.w > 1.0f){
            const int clipTop = static_cast<int>(std::max(cur.y, scanY));
            SDL_Rect clip = {static_cast<int>(cur.x), clipTop, static_cast<int>(cur.w), static_cast<int>(cur.y + cur.h) - clipTop};
            if(clip.h > 0){
                SDL_RenderSetClipRect(r, &clip);
                SDL_SetRenderDrawColor(r, LINE_COLOR.r, LINE_COLOR.g, LINE_COLOR.b, HATCH_ALPHA);
                const int x0 = static_cast<int>(cur.x), y0 = static_cast<int>(cur.y);
                const int w = static_cast<int>(cur.w), h = static_cast<int>(cur.h);
                for(int k = 0; k <= w + h; k += HATCH_GAP){
                    SDL_RenderDrawLine(r, x0 + k, y0, x0 + k - h, y0 + h);   // 右上から左下への斜線
                }
                SDL_RenderSetClipRect(r, nullptr);
            }
        }

        // 中身：できている所（scanY より上）を貼る。生成中は大きなグリッチを重ねる
        if(bp > 0.0 && content){
            if(bp >= 1.0) setPremul(content);
            if(bp >= 1.0){
                SDL_Rect src = {area.x, static_cast<int>(areaTop), area.w, static_cast<int>(areaBot - areaTop)};
                SDL_RenderCopy(r, content, &src, &src);
            }
            else{
                drawBuilding(r, content, area, static_cast<int>(areaTop), scanY, bp);
            }
        }

        // 枠（生成が終わるまで。中身の枠がそろったら、中身の枠に任せる）
        if(bp < 1.0){
            drawThickLine(r, cur.x, cur.y, cur.x + cur.w, cur.y, LINE_W, lc);
            drawThickLine(r, cur.x, cur.y + cur.h, cur.x + cur.w, cur.y + cur.h, LINE_W, lc);
            drawThickLine(r, cur.x, cur.y, cur.x, cur.y + cur.h, LINE_W, lc);
            drawThickLine(r, cur.x + cur.w, cur.y, cur.x + cur.w, cur.y + cur.h, LINE_W, lc);
        }
        // 生成中の先頭の光の線（枠の中に来てから）
        if(bp > 0.0 && bp < 1.0 && scanY >= cur.y){
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
            SDL_FRect line = {cur.x, scanY - 1.0f, cur.w, 2.0f};
            SDL_RenderFillRectF(r, &line);
            SDL_SetRenderDrawColor(r, LINE_COLOR.r, LINE_COLOR.g, LINE_COLOR.b, 70);
            SDL_FRect glow = {cur.x, scanY - 5.0f, cur.w, 10.0f};
            SDL_RenderFillRectF(r, &glow);
        }
    }

private:
    static void setPremul(SDL_Texture* t){
        static const SDL_BlendMode PREMUL = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
            SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
        if(SDL_SetTextureBlendMode(t, PREMUL) != 0) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }

    // 生成中の中身（上の端 top から scanY まで）を、グリッチさせて貼る。bp = 生成の進み具合（0〜1）
    //   ・できあがった所：一瞬ごとに、一部の塊が別の場所へ飛ぶ（元の場所は穴になる）／全体が横へずれる
    //   ・3D メガネ：赤を左、青を右にずらした写しを足す。ときどき大きく分かれる
    //   ・先頭の帯（GLITCH_ZONE）：高さばらばらの細い帯ごとに大きく左右へずれ、帯ごとにも赤・青に分かれる
    //   ・生成の最後の 15% で、だんだん落ち着く
    static void drawBuilding(SDL_Renderer* r, SDL_Texture* content, const SDL_Rect& area, int top, float scanY, double bp){
        const float calm = (bp < 0.85) ? 1.0f : static_cast<float>((1.0 - bp) / 0.15);   // 強さ（最後に 0 へ）
        const int   ax = area.x, aw = area.w;
        const int   scan = static_cast<int>(scanY);
        const int   solidBot = std::max(top, scan - GLITCH_ZONE);
        // 乱数の元：一瞬（SLOT_MS）ごとに変わる。ウィンドウごとに違う動きになるよう、位置も混ぜる
        const uint32_t slot = static_cast<uint32_t>(SDL_GetTicks() / SLOT_MS);
        const uint32_t base = slot * 2654435761u ^ (static_cast<uint32_t>(area.x) * 73856093u) ^ (static_cast<uint32_t>(area.y) * 19349663u);
        auto rnd = [&](uint32_t k){ return hash01(base + k * 0x9E3779B9u); };

        // 全体の横ずれ（ときどき）
        const float jolt = (rnd(1) < JOLT_CHANCE * calm) ? static_cast<float>(rnd(2) * 2.0 - 1.0) * JOLT_X : 0.0f;

        // 飛ぶ塊を決める（できあがった所の中だけ）
        struct Chunk{ SDL_Rect src; float dx, dy; };
        Chunk chunks[CHUNK_MAX];
        int chunkCount = 0;
        if(solidBot - top > 20 && rnd(3) < CHUNK_CHANCE * calm){
            const int n = 1 + static_cast<int>(rnd(4) * CHUNK_MAX);
            for(int i = 0; i < n && i < CHUNK_MAX; i++){
                const uint32_t k = 10u + static_cast<uint32_t>(i) * 7u;
                const int w = static_cast<int>(aw * (0.15 + 0.35 * rnd(k)));
                const int h = std::min(solidBot - top, 12 + static_cast<int>(rnd(k + 1) * 60));
                const int x = ax + static_cast<int>((aw - w) * rnd(k + 2));
                const int y = top + static_cast<int>((solidBot - top - h) * rnd(k + 3));
                const float dx = static_cast<float>((rnd(k + 4) < 0.5 ? -1.0 : 1.0) * (0.35 + 0.65 * rnd(k + 5))) * CHUNK_JUMP_X * calm;
                const float dy = static_cast<float>(rnd(k + 6) * 2.0 - 1.0) * CHUNK_JUMP_Y * calm;
                chunks[chunkCount++] = {SDL_Rect{x, y, w, h}, dx, dy};
            }
        }

        // ---- 1. できあがった所（塊の元の場所は抜いて、穴にする）----
        setPremul(content);
        auto copyRow = [&](int y0, int y1){   // y0〜y1 の行を、塊の所を抜いて貼る
            if(y1 <= y0) return;
            // この行の範囲にかかる塊の左右の端（左から順に）
            Chunk sorted[CHUNK_MAX];
            int c = 0;
            for(int i = 0; i < chunkCount; i++){
                if(chunks[i].src.y < y1 && chunks[i].src.y + chunks[i].src.h > y0) sorted[c++] = chunks[i];
            }
            std::sort(sorted, sorted + c, [](const Chunk& a, const Chunk& b){ return a.src.x < b.src.x; });
            int x = ax;
            for(int i = 0; i < c; i++){
                const int l = std::max(x, sorted[i].src.x);
                if(l > x){
                    SDL_Rect s = {x, y0, l - x, y1 - y0};
                    SDL_FRect d = {x + jolt, static_cast<float>(y0), static_cast<float>(l - x), static_cast<float>(y1 - y0)};
                    SDL_RenderCopyF(r, content, &s, &d);
                }
                x = std::max(x, sorted[i].src.x + sorted[i].src.w);
            }
            if(ax + aw > x){
                SDL_Rect s = {x, y0, ax + aw - x, y1 - y0};
                SDL_FRect d = {x + jolt, static_cast<float>(y0), static_cast<float>(ax + aw - x), static_cast<float>(y1 - y0)};
                SDL_RenderCopyF(r, content, &s, &d);
            }
        };
        // 塊の上下の端で行を区切って貼る
        {
            int cuts[2 * CHUNK_MAX + 2];
            int m = 0;
            cuts[m++] = top;
            for(int i = 0; i < chunkCount; i++){
                cuts[m++] = chunks[i].src.y;
                cuts[m++] = chunks[i].src.y + chunks[i].src.h;
            }
            cuts[m++] = solidBot;
            std::sort(cuts, cuts + m);
            for(int i = 0; i + 1 < m; i++) copyRow(std::clamp(cuts[i], top, solidBot), std::clamp(cuts[i + 1], top, solidBot));
        }

        // ---- 2. 先頭の帯：高さばらばらの細い帯ごとに大きく左右へずらす ----
        struct Band{ int y, h; float shift; };
        Band bands[64];
        int bandCount = 0;
        for(int y = solidBot; y < scan && bandCount < 64; ){
            const uint32_t k = 100u + static_cast<uint32_t>(y);
            const int hh = std::min(3 + static_cast<int>(rnd(k) * 12), scan - y);
            // 先頭（scanY）に近いほど大きくずれる
            const float nearEdge = 0.35f + 0.65f * static_cast<float>(y - solidBot + hh) / std::max(1, scan - solidBot);
            const float shift = (rnd(k + 1) < 0.75)
                ? static_cast<float>(rnd(k + 2) * 2.0 - 1.0) * GLITCH_SHIFT * nearEdge * calm : 0.0f;
            bands[bandCount++] = {y, hh, shift + jolt};
            SDL_Rect s = {ax, y, aw, hh};
            SDL_FRect d = {ax + shift + jolt, static_cast<float>(y), static_cast<float>(aw), static_cast<float>(hh)};
            SDL_RenderCopyF(r, content, &s, &d);
            y += hh;
        }

        // ---- 3. 飛んだ塊：別の場所に貼る（白っぽく光る縁付き）----
        for(int i = 0; i < chunkCount; i++){
            const Chunk& ch = chunks[i];
            SDL_FRect d = {ch.src.x + ch.dx, ch.src.y + ch.dy, static_cast<float>(ch.src.w), static_cast<float>(ch.src.h)};
            SDL_RenderCopyF(r, content, &ch.src, &d);
            SDL_SetRenderDrawColor(r, LINE_COLOR.r, LINE_COLOR.g, LINE_COLOR.b, static_cast<Uint8>(90 * calm));
            SDL_RenderDrawRectF(r, &d);
        }

        // ---- 4. 3D メガネ：赤を左・青を右にずらした写しを足す ----
        const bool spike = rnd(5) < SPIKE_CHANCE * calm;
        const float split = (SPLIT_BASE + (spike ? SPLIT_SPIKE * static_cast<float>(0.5 + 0.5 * rnd(6)) : 0.0f)) * calm;
        const float splitY = spike ? static_cast<float>(rnd(7) * 2.0 - 1.0) * 4.0f * calm : 0.0f;   // 大きく分かれるときは少し上下にも
        if(split > 0.5f){
            SDL_SetTextureBlendMode(content, SDL_BLENDMODE_ADD);
            SDL_SetTextureAlphaMod(content, static_cast<Uint8>(SPLIT_ALPHA * calm));
            auto ghost = [&](Uint8 cr, Uint8 cg, Uint8 cb, float sign){
                SDL_SetTextureColorMod(content, cr, cg, cb);
                // できあがった所
                if(solidBot > top){
                    SDL_Rect s = {ax, top, aw, solidBot - top};
                    SDL_FRect d = {ax + jolt + sign * split, top + sign * splitY, static_cast<float>(aw), static_cast<float>(solidBot - top)};
                    SDL_RenderCopyF(r, content, &s, &d);
                }
                // 先頭の帯：帯ごとのずれ ＋ さらに大きく分かれる
                for(int i = 0; i < bandCount; i++){
                    SDL_Rect s = {ax, bands[i].y, aw, bands[i].h};
                    SDL_FRect d = {ax + bands[i].shift + sign * split * 1.6f, static_cast<float>(bands[i].y),
                                   static_cast<float>(aw), static_cast<float>(bands[i].h)};
                    SDL_RenderCopyF(r, content, &s, &d);
                }
                // 飛んだ塊にも
                for(int i = 0; i < chunkCount; i++){
                    const Chunk& ch = chunks[i];
                    SDL_FRect d = {ch.src.x + ch.dx + sign * split * 1.4f, ch.src.y + ch.dy,
                                   static_cast<float>(ch.src.w), static_cast<float>(ch.src.h)};
                    SDL_RenderCopyF(r, content, &ch.src, &d);
                }
            };
            ghost(255, 30, 50, -1.0f);    // 赤：左
            ghost(30, 170, 255, 1.0f);    // 青：右
            SDL_SetTextureColorMod(content, 255, 255, 255);
            SDL_SetTextureAlphaMod(content, 255);
        }
        setPremul(content);
    }

    static double hash01(uint32_t x){
        x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
        return (x & 0xFFFFFF) / static_cast<double>(0x1000000);
    }
};

// 透明な絵（画面と同じ大きさ）を作る／描く先にする小道具
inline SDL_Texture* quarryMakeLayer(SDL_Renderer* r){
    return SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);
}
// layer を透明にして描く先にする。戻り値（前の描く先）を、描き終わったら SDL_SetRenderTarget に渡す
inline SDL_Texture* quarryBeginLayer(SDL_Renderer* r, SDL_Texture* layer){
    SDL_Texture* prev = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, layer);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    return prev;
}