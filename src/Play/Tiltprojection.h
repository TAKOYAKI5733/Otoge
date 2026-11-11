#pragma once

// =====================================================================
//  TiltProjection.h — レーンを傾けるときの「座標の変換」  src/Play/TiltProjection.h
//
//  ■ 考え方（前の方式との違い）
//    前は、レーン・ノーツを一度テクスチャ（画像）に描いて、その画像を台形に引き伸ばしていた。
//    画像を引き伸ばすと、ピクセルの段差まで一緒に伸びるのでギザギザになり、ぼかしでごまかすしかなかった。
//    今は、ノーツや判定ボックスの「形の角の点（頂点）」の位置を、1つずつ傾けた位置に計算し直してから、
//    画面に直接描く。形はいつも画面の解像度でそのまま描かれるので、どの角度でもくっきりしている。
//    透視投影では、まっすぐな線は傾けてもまっすぐなので、角の点を動かすだけで形は正しく保たれる。
//
//  ■ 使い方
//    LaneTilt::begin() が g_tiltProj に変換を入れ、LaneTilt::end() が nullptr に戻す。
//    その間に描かれる物は、roundedRect.h（角丸四角）・renderGame.h（線・粒・ビーム）・drawFrame.h（レーン）が
//    g_tiltProj を見て、自分で頂点を変換してから描く。g_tiltProj が nullptr なら今まで通り。
//
//  ■ 座標
//    描く側の座標は「延長ぶん extra だけ下にずらした、まっすぐの座標」（y = 0 が延長したレーンの一番奥）。
//    判定ラインからの高さ  d = 判定ライン −（y − extra）
//    見える大きさ          s = C / (C + d·sinθ)
//    画面の位置            X = 中心 + (x − 中心)·s·zoom
//                          Y = 判定ライン − d·cosθ·s·zoom
// =====================================================================

#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>

struct TiltProjection{
    double cameraDist = 1400.0;
    double cosT = 1.0, sinT = 0.0;
    double pivot = 0.0;      // 判定ラインの画面の y
    double cx = 0.0;         // 遠くなるほど寄っていく先の x
    double zoom = 1.0;       // 全体を大きくする倍率
    double extra = 0.0;      // 描く側が y をずらしている量
    double dMin = -1e9;      // これより手前（下）の点は、ここで止める（カメラの後ろに回り込んで計算が壊れないように）
    double fadeTopY = 0.0;   // ぼかし：一番奥の画面の y（ここで透明）
    double fadeEndY = 0.0;   // ぼかし：ここから下は不透明
    bool   fade = false;
    SDL_Texture* feather = nullptr;   // なめらかな線を描くための、両端が透明な小さな画像

    // 描く側の座標 (x, y) → 画面の座標 (X, Y)。scale は「その場所で物が何倍に見えるか」、alpha はぼかしの濃さ(0〜1)
    void map(double x, double y, float& X, float& Y, float& scale, float& alpha) const{
        double d = pivot - (y - extra);
        d = std::max(d, dMin);
        const double s = cameraDist / (cameraDist + d * sinT);
        X = static_cast<float>(cx + (x - cx) * s * zoom);
        Y = static_cast<float>(pivot - d * cosT * s * zoom);
        scale = static_cast<float>(s * zoom);
        alpha = fadeAt(Y);
    }

    // 画面の y → 描く側の y（map の逆）。帯に分けて描くとき、画面の上で等間隔に分けるのに使う
    double unmapY(double Y) const{
        const double Yr = (pivot - Y) / zoom;
        const double d  = Yr * cameraDist / (cameraDist * cosT - Yr * sinT);
        return pivot - d + extra;
    }

    float fadeAt(float Y) const{
        if(!fade || fadeEndY <= fadeTopY + 0.5) return 1.0f;
        return static_cast<float>(std::clamp((Y - fadeTopY) / (fadeEndY - fadeTopY), 0.0, 1.0));
    }
};

// 今、傾けて描いている最中なら変換が入っている（描いていないときは nullptr）
inline const TiltProjection* g_tiltProj = nullptr;

inline SDL_Color tiltFadeColor(SDL_Color c, float alpha){
    c.a = static_cast<Uint8>(c.a * alpha);
    return c;
}

// SDL_Vertex の並びの位置（と、ぼかしの濃さ）を、まとめて変換する
inline void tiltApply(SDL_Vertex* v, int n){
    if(!g_tiltProj) return;
    for(int i = 0; i < n; i++){
        float X, Y, s, a;
        g_tiltProj->map(v[i].position.x, v[i].position.y, X, Y, s, a);
        v[i].position = {X, Y};
        v[i].color = tiltFadeColor(v[i].color, a);
    }
}

// 四角（塗り、または画像）を、縦に segs 本の帯に分けて傾けて描く
//   帯の境目は「画面の上で等間隔」になるように置く（奥の詰まった所に帯を使いすぎない）。
//   画像を貼るときは、帯ごとに画像の縦の位置（v）も正しく計算するので、奥ほど詰まって正しく見える
inline void tiltRect(SDL_Renderer* r, float x, float y, float w, float h, SDL_Color c,
                     SDL_Texture* tex = nullptr, int segs = 24){
    if(!g_tiltProj || w <= 0.0f || h <= 0.0f) return;
    const TiltProjection& p = *g_tiltProj;
    float X0, Y0, s0, a0, X1, Y1, s1, a1;
    p.map(x, y, X0, Y0, s0, a0);
    p.map(x, y + h, X1, Y1, s1, a1);
    if(Y1 - Y0 < 0.01f) return;

    segs = std::max(1, segs);
    SDL_Vertex v[2 * 65];
    int idx[6 * 64];
    segs = std::min(segs, 64);
    for(int k = 0; k <= segs; k++){
        const double Ys = Y0 + (Y1 - Y0) * k / segs;
        const double ys = std::clamp(p.unmapY(Ys), static_cast<double>(y), static_cast<double>(y + h));
        float XL, XR, Y, s, a;
        p.map(x, ys, XL, Y, s, a);
        p.map(x + w, ys, XR, Y, s, a);
        const float tv = static_cast<float>((ys - y) / h);
        const SDL_Color col = tiltFadeColor(c, a);
        v[k * 2 + 0] = {{XL, Y}, col, {0.0f, tv}};
        v[k * 2 + 1] = {{XR, Y}, col, {1.0f, tv}};
    }
    for(int k = 0; k < segs; k++){
        const int a = k * 2, b = a + 1, cc = a + 2, d = a + 3;
        int* q = &idx[k * 6];
        q[0] = a; q[1] = b; q[2] = d; q[3] = a; q[4] = d; q[5] = cc;
    }
    SDL_RenderGeometry(r, tex, v, (segs + 1) * 2, idx, segs * 6);
}

// 太さのある線を、傾けて、なめらかな縁で描く
//   両端の位置を変換し、太さはその場所の大きさ（scale）に合わせる。
//   縁は、両端が透明な小さな画像（feather）を線の横方向に引き伸ばして塗るので、斜めでも段差が出ない
inline void tiltLine(SDL_Renderer* r, float x1, float y1, float x2, float y2, float width, SDL_Color c){
    if(!g_tiltProj) return;
    const TiltProjection& p = *g_tiltProj;
    float X1, Y1, s1, a1, X2, Y2, s2, a2;
    p.map(x1, y1, X1, Y1, s1, a1);
    p.map(x2, y2, X2, Y2, s2, a2);

    const float dx = X2 - X1, dy = Y2 - Y1;
    const float len = std::sqrt(dx * dx + dy * dy);
    if(len < 0.01f) return;
    const float nx = -dy / len, ny = dx / len;
    const float h1 = width * s1 * 0.5f + 1.0f;   // +1 は縁をなめらかにする分
    const float h2 = width * s2 * 0.5f + 1.0f;

    const SDL_Color c1 = tiltFadeColor(c, a1), c2 = tiltFadeColor(c, a2);
    SDL_Vertex v[4] = {
        {{X1 - nx * h1, Y1 - ny * h1}, c1, {0.0f, 0.5f}},
        {{X1 + nx * h1, Y1 + ny * h1}, c1, {1.0f, 0.5f}},
        {{X2 + nx * h2, Y2 + ny * h2}, c2, {1.0f, 0.5f}},
        {{X2 - nx * h2, Y2 - ny * h2}, c2, {0.0f, 0.5f}},
    };
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, p.feather, v, 4, idx, 6);
}