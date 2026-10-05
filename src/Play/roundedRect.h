#pragma once

// ====================================================================
//  roundedRect.h — 角丸四角を SDL_RenderGeometry でまとめて描く
//
//  使い方:
//    RoundedRectBatch batch;                // フレームごと(またはループ手前)に用意
//    batch.add(x, y, w, h, radius, color);  // 描きたい分だけ追加(追加した順に描画される)
//    batch.flush(renderer);                 // 1回の描画で全部出す
//
//  ・塗りは「中心から外周へ向かう三角形ファン」(四角は凸なのでこれで正しく塗れる)
//  ・端は1pxぶんだけ透明へ溶かすリングを足して、斜めの縁をなめらかにする(aa=true)
//  ・テクスチャを使わないので ColorMod/AlphaMod が不要。色・透明度は頂点に持たせる
//  ・SDL_RenderGeometry は SDL 2.0.18 以降で使える
// ====================================================================

#include "GameCommon.h"
#include <vector>
#include <cmath>
#include <algorithm>

struct RoundedRectBatch{
    std::vector<SDL_Vertex> verts;
    std::vector<int> idx;

    void clear(){
        verts.clear();
        idx.clear();
    }

    // x,y = 左上, w,h = 大きさ, radius = 角の半径(px), cornerSegs = 角1つあたりの分割数
    void add(float x, float y, float w, float h, float radius, SDL_Color c, int cornerSegs = 6, bool aa = true){
        if(w <= 0.0f || h <= 0.0f || c.a == 0) return;

        const float r = std::max(0.0f, std::min(radius, std::min(w, h) * 0.5f));
        const int segs = (r < 0.5f) ? 1 : std::max(1, cornerSegs);
        const float kPi = 3.14159265358979323846f;

        // 外周の点(時計回り: 右上の角 → 右下 → 左下 → 左上)と、各点の外向き法線
        struct P{ float x, y, nx, ny; };
        std::vector<P> ring;
        ring.reserve(static_cast<size_t>(4 * (segs + 1)));

        const float cx[4] = { x + w - r, x + w - r, x + r, x + r };
        const float cy[4] = { y + r,     y + h - r, y + h - r, y + r };
        const float a0[4] = { -0.5f * kPi, 0.0f, 0.5f * kPi, kPi };   // 各角の弧の開始角(終了角は+90度)

        for(int k = 0; k < 4; k++){
            for(int s = 0; s <= segs; s++){
                const float ang = a0[k] + (0.5f * kPi) * static_cast<float>(s) / static_cast<float>(segs);
                const float nx = std::cos(ang), ny = std::sin(ang);
                ring.push_back({ cx[k] + r * nx, cy[k] + r * ny, nx, ny });
            }
        }

        const int base = static_cast<int>(verts.size());
        const int n = static_cast<int>(ring.size());

        // 0: 中心 / 1..n: 外周
        verts.push_back({ { x + w * 0.5f, y + h * 0.5f }, c, { 0.0f, 0.0f } });
        for(const P& p : ring) verts.push_back({ { p.x, p.y }, c, { 0.0f, 0.0f } });

        for(int i = 0; i < n; i++){
            idx.push_back(base);
            idx.push_back(base + 1 + i);
            idx.push_back(base + 1 + (i + 1) % n);
        }

        if(aa){
            // 外側に1px広げた透明な点を足し、外周との間を三角形で埋めて縁をぼかす
            const int outerBase = static_cast<int>(verts.size());
            SDL_Color edge = c;
            edge.a = 0;
            for(const P& p : ring) verts.push_back({ { p.x + p.nx, p.y + p.ny }, edge, { 0.0f, 0.0f } });

            for(int i = 0; i < n; i++){
                const int j = (i + 1) % n;
                const int in0 = base + 1 + i, in1 = base + 1 + j;
                const int out0 = outerBase + i, out1 = outerBase + j;
                idx.push_back(in0);  idx.push_back(out0); idx.push_back(out1);
                idx.push_back(in0);  idx.push_back(out1); idx.push_back(in1);
            }
        }
    }

    void flush(SDL_Renderer* renderer){
        if(!idx.empty()){
            SDL_RenderGeometry(renderer, nullptr, verts.data(), static_cast<int>(verts.size()),
                               idx.data(), static_cast<int>(idx.size()));
        }
        clear();
    }
};