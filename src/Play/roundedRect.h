#pragma once

// ====================================================================
//  roundedRect.h — 角丸四角を SDL_RenderGeometry でまとめて描く
//
//  使い方:
//    RoundedRectBatch batch;
//    batch.add(x, y, w, h, radius, color);                       // 塗りつぶしの角丸四角
//    batch.addShadow(x, y, w, h, radius, spread, color);         // 縁の外側の影
//    batch.addInnerShadow(x, y, w, h, radius, spread, color);    // 縁の内側の影(塗りの上に重ねる)
//    batch.flush(renderer);                                      // 1回の描画で全部出す
//
//  ・SDL_RenderGeometry は SDL 2.0.18 以降で使える
//
//  ■ レーンを傾けているとき（g_tiltProj が入っているとき。Play/TiltProjection.h）
//    角丸の外周の点を作ったすぐ後に、点の位置を「傾けた画面の位置」に変換する。
//    縁のなめらかにする帯（1px）は、変換した後の画面の上で付けるので、奥の小さいノーツでも縁がくっきり・なめらか。
//    ※ add などを呼ぶ時点で変換するので、傾けている間に add して、そのまま flush すること
// ====================================================================

#include "GameCommon.h"
#include "Play/TiltProjection.h"
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
        const float r = clampRadius(radius, w, h);
        buildRing(ring_, x, y, w, h, r, segsFor(r, cornerSegs));
        projectRing(ring_);
        const int n = static_cast<int>(ring_.size());

        const int base = static_cast<int>(verts.size());

        // 0: 中心 / 1..n: 外周
        float cxp = x + w * 0.5f, cyp = y + h * 0.5f, ca = 1.0f;
        projectPoint(cxp, cyp, ca);
        verts.push_back({ { cxp, cyp }, fadeColor(c, ca), { 0.0f, 0.0f } });
        for(const P& p : ring_) verts.push_back({ { p.x, p.y }, fadeColor(c, p.a), { 0.0f, 0.0f } });

        for(int i = 0; i < n; i++){
            idx.push_back(base);
            idx.push_back(base + 1 + i);
            idx.push_back(base + 1 + (i + 1) % n);
        }

        if(aa) pushFeather(base + 1, n, c, 1.0f);   // 1pxだけぼかして縁をなめらかに
    }

    // 縁の外側だけに影を描く(内側は塗らない)
    void addShadow(float x, float y, float w, float h, float radius, float spread, SDL_Color c, int cornerSegs = 6){
        if(w <= 0.0f || h <= 0.0f || spread <= 0.0f || c.a == 0) return;
        const float r = clampRadius(radius, w, h);
        buildRing(ring_, x, y, w, h, r, segsFor(r, cornerSegs));
        projectRing(ring_);
        const int n = static_cast<int>(ring_.size());

        const int innerBase = static_cast<int>(verts.size());
        for(const P& p : ring_) verts.push_back({ { p.x, p.y }, fadeColor(c, p.a), { 0.0f, 0.0f } });

        pushFeather(innerBase, n, c, spread);
    }

    // 縁の内側だけに影を描く(縁で色c → 内側へspread px進むと透明)
    // 形の上に重ねて使うので、塗りより「後」にflushすること
    void addInnerShadow(float x, float y, float w, float h, float radius, float spread, SDL_Color c, int cornerSegs = 6){
        if(w <= 0.0f || h <= 0.0f || spread <= 0.0f || c.a == 0) return;
        const float r    = clampRadius(radius, w, h);
        const int   segs = segsFor(r, cornerSegs);
        const float s    = std::min(spread, std::min(w, h) * 0.5f);   // 反対側の縁を越えないように

        // 外周(縁) と、そこから s だけ内側に縮めた角丸四角の外周を、同じ点数で作る
        buildRing(ring_,      x,     y,     w,         h,         r,                     segs);
        buildRing(innerRing_, x + s, y + s, w - 2 * s, h - 2 * s, std::max(0.0f, r - s), segs);
        projectRing(ring_);
        projectRing(innerRing_);
        const int n = static_cast<int>(ring_.size());

        SDL_Color clear = c;
        clear.a = 0;

        const int edgeBase  = static_cast<int>(verts.size());
        for(const P& p : ring_)      verts.push_back({ { p.x, p.y }, fadeColor(c, p.a), { 0.0f, 0.0f } });
        const int innerBase = static_cast<int>(verts.size());
        for(const P& p : innerRing_) verts.push_back({ { p.x, p.y }, clear, { 0.0f, 0.0f } });

        for(int i = 0; i < n; i++){
            const int j = (i + 1) % n;
            const int e0 = edgeBase + i,  e1 = edgeBase + j;
            const int i0 = innerBase + i, i1 = innerBase + j;
            idx.push_back(e0); idx.push_back(e1); idx.push_back(i1);
            idx.push_back(e0); idx.push_back(i1); idx.push_back(i0);
        }
    }

    void addOutline(float x, float y, float w, float h, float radius, float thickness, SDL_Color c, int cornerSegs = 6){
        if(w <= 0.0f || h <= 0.0f || thickness <= 0.0f || c.a == 0) return;
        const float r    = clampRadius(radius, w, h);
        const int   segs = segsFor(r, cornerSegs);
        const float t    = std::min(thickness, std::min(w, h) * 0.5f);

        // 外周と、t だけ内側に縮めた外周を、同じ点数で作る
        buildRing(ring_,      x,     y,     w,         h,         r,                     segs);
        buildRing(innerRing_, x + t, y + t, w - 2 * t, h - 2 * t, std::max(0.0f, r - t), segs);
        projectRing(ring_);
        projectRing(innerRing_);
        const int n = static_cast<int>(ring_.size());

        const int outerBase = static_cast<int>(verts.size());
        for(const P& p : ring_)      verts.push_back({ { p.x, p.y }, fadeColor(c, p.a), { 0.0f, 0.0f } });
        const int innerBase = static_cast<int>(verts.size());
        for(const P& p : innerRing_) verts.push_back({ { p.x, p.y }, fadeColor(c, p.a), { 0.0f, 0.0f } });

        // 外周と内周の間（帯）を三角形で埋める
        for(int i = 0; i < n; i++){
            const int j = (i + 1) % n;
            const int o0 = outerBase + i, o1 = outerBase + j;
            const int i0 = innerBase + i, i1 = innerBase + j;
            idx.push_back(o0); idx.push_back(o1); idx.push_back(i1);
            idx.push_back(o0); idx.push_back(i1); idx.push_back(i0);
        }

        pushFeather(outerBase, n, c, 1.0f);   // 外側の縁を1pxぼかしてなめらかに
    }

    void flush(SDL_Renderer* renderer){
        if(!idx.empty()){
            SDL_RenderGeometry(renderer, nullptr, verts.data(), static_cast<int>(verts.size()),
                               idx.data(), static_cast<int>(idx.size()));
        }
        clear();
    }

private:
    struct P{ float x, y, nx, ny; float a = 1.0f; };   // 外周の点・その点の外向き法線・ぼかしの濃さ
    std::vector<P> ring_;              // 使い回し用(毎回のメモリ確保を避ける)
    std::vector<P> innerRing_;         // 内側の影用

    static float clampRadius(float radius, float w, float h){
        return std::max(0.0f, std::min(radius, std::min(w, h) * 0.5f));
    }
    static int segsFor(float r, int cornerSegs){
        return (r < 0.5f) ? 1 : std::max(1, cornerSegs);
    }

    static SDL_Color fadeColor(SDL_Color c, float a){
        if(a >= 0.999f) return c;
        c.a = static_cast<Uint8>(c.a * a);
        return c;
    }

    // 1点を傾けた画面の位置に変換する（傾けていないときは何もしない）
    static void projectPoint(float& x, float& y, float& a){
        if(!g_tiltProj) return;
        float X, Y, s;
        g_tiltProj->map(x, y, X, Y, s, a);
        x = X;
        y = Y;
    }

    // 外周の点を全部、傾けた画面の位置に変換する。外向きの向き（法線）も画面の上で計算し直す
    //   → pushFeather の 1px の帯が、画面の上でちょうど 1px になる（奥でも細くなりすぎない）
    static void projectRing(std::vector<P>& ring){
        if(!g_tiltProj) return;
        for(P& p : ring){
            float X, Y, s, a, X2, Y2, s2, a2;
            g_tiltProj->map(p.x, p.y, X, Y, s, a);
            g_tiltProj->map(p.x + p.nx, p.y + p.ny, X2, Y2, s2, a2);   // 法線の先の点も変換して、向きを求める
            float nx = X2 - X, ny = Y2 - Y;
            const float len = std::sqrt(nx * nx + ny * ny);
            if(len > 1e-6f){ nx /= len; ny /= len; }
            else{ nx = p.nx; ny = p.ny; }
            p.x = X; p.y = Y; p.nx = nx; p.ny = ny; p.a = a;
        }
    }

    // 外周の点を out に作る(時計回り: 右上の角 → 右下 → 左下 → 左上)
    // segs を外から渡すので、半径が違っても点の数が必ず揃う
    static void buildRing(std::vector<P>& out, float x, float y, float w, float h, float r, int segs){
        out.clear();
        const float kPi = 3.14159265358979323846f;

        const float cx[4] = { x + w - r, x + w - r, x + r, x + r };
        const float cy[4] = { y + r,     y + h - r, y + h - r, y + r };
        const float a0[4] = { -0.5f * kPi, 0.0f, 0.5f * kPi, kPi };   // 各角の弧の開始角(終了角は+90度)

        for(int k = 0; k < 4; k++){
            for(int s = 0; s <= segs; s++){
                const float ang = a0[k] + (0.5f * kPi) * static_cast<float>(s) / static_cast<float>(segs);
                const float nx = std::cos(ang), ny = std::sin(ang);
                out.push_back({ cx[k] + r * nx, cy[k] + r * ny, nx, ny, 1.0f });
            }
        }
    }

    // innerBase から並ぶ n 個の外周頂点の外側に、幅 width の「色 → 透明」の帯を張る
    //   ring_ の点（傾けているときは変換後の画面の点）から、画面の上で width だけ外に出す
    void pushFeather(int innerBase, int n, SDL_Color c, float width){
        const int outerBase = static_cast<int>(verts.size());
        SDL_Color edge = c;
        edge.a = 0;
        for(const P& p : ring_) verts.push_back({ { p.x + p.nx * width, p.y + p.ny * width }, edge, { 0.0f, 0.0f } });

        for(int i = 0; i < n; i++){
            const int j = (i + 1) % n;
            const int in0 = innerBase + i, in1 = innerBase + j;
            const int out0 = outerBase + i, out1 = outerBase + j;
            idx.push_back(in0);  idx.push_back(out0); idx.push_back(out1);
            idx.push_back(in0);  idx.push_back(out1); idx.push_back(in1);
        }
    }
};