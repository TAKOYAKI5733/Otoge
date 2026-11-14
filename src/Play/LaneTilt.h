#pragma once

#include "GameCommon.h"
#include "Play/TiltProjection.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

// =====================================================================
//  レーンの傾き（奥に倒す）  src/Play/LaneTilt.h
//
//  ■ やり方（頂点を変換して、画面に直接描く）
//    begin() で「傾けた画面の位置への変換」（TiltProjection）を用意して g_tiltProj に入れる。
//    その後に描かれるレーン・ノーツ・判定ボックス・エフェクトは、形の角の点（頂点）を1つずつ変換してから
//    画面に直接描かれる（roundedRect.h・renderGame.h・drawFrame.h が g_tiltProj を見て変換する）。
//    画像に描いてから引き伸ばす方式ではないので、どの角度でも、形の縁は画面の解像度でくっきり描かれる。
//    end() で区切り線（フレーム）を描き、g_tiltProj を空に戻す。
//
//  ■ 延長線上まで描く（画面の上の端より先・下の端より先）
//    倒すと、画面の上の端（元の y = 0）が画面の途中まで下がるので、その先 extra px ぶんのレーンとノーツも描く。
//    大きく倒すと画面の下も足りなくなるので、下にも extraBottom px 足す。
//    描く側は、y 座標をすべて extra だけ下にずらし、レーンを textureLength() の長さまで描く（begin() の戻り値 = extra）
//
//  ■ 透視投影の式
//    判定ラインからの高さ d の点を、判定ラインを軸に θ 度 奥へ倒すと
//       上方向の高さ   d·cosθ          奥行き   d·sinθ
//    カメラから距離 C の所に画面があるとすると、奥行き z の物は s = C / (C + z) 倍の大きさに見える
//       x = 中心 + (元の x − 中心)·s·zoom
//       y = 判定ライン − d·cosθ·s·zoom
//
//  ■ 傾けるほど全体を大きくする（zoom）
//       zoom = 1 + ZOOM_REF_D·sinθ / C
//    判定ラインから ZOOM_REF_D px 上のノーツの幅が、傾けていないときと同じになる倍率
// =====================================================================

namespace lanetilt{
    inline constexpr double CAMERA_DIST   = 1400.0;   // カメラと画面の距離(px)。小さいほど遠近感が強くなる
    inline constexpr int    MAX_EXTRA     = 6000;     // 画面の上に足す長さの上限(px)。60 度でも画面の上まで届く長さ

    inline constexpr double ZOOM_REF_D    = 405.0;    // この高さ(px、判定ラインから上)のノーツの幅を、傾ける前と同じにする
    inline constexpr double ZOOM_STRENGTH = 1.0;      // 大きくする強さ（0 = 大きくしない、1 = 上の計算どおり）

    inline constexpr float  LINE_WIDTH    = 2.0f;     // 区切り線の太さ(px、判定ライン上での太さ。奥ほど細くなる)
    inline constexpr SDL_Color LINE_COLOR = {255, 255, 255, 255};

    inline constexpr bool   KEEP_NOTE_SHAPE = true;   // 傾けてもノーツの太さ（幅との割合）を変えない
}

class LaneTilt{
public:
    LaneTilt() = default;
    ~LaneTilt(){ destroy(); }
    LaneTilt(const LaneTilt&) = delete;
    LaneTilt& operator=(const LaneTilt&) = delete;

    static bool active(double tiltDeg){ return tiltDeg >= 0.05; }

    // ここから後の描画を、傾けて描くようにする。
    //   戻り値: 描く側が y 座標をずらす量（extra）。傾けないときは -1（いつも通りに描く）
    //   fadeRatio : レーンの奥の端をぼかす割合（0 = ぼかさない）
    //   offsetY：出てくる画面の y を全部この分だけ下にずらす（レーンの左右の傾きで、上下に余分に描くとき。ふだんは 0）
    int begin(SDL_Renderer* renderer, double tiltDeg, double pivotY, double fadeRatio = 0.0, double centerX = SCREEN_W / 2.0,
              double offsetY = 0.0){
        if(!active(tiltDeg)) return -1;
        ensureResources(renderer, tiltDeg, pivotY, offsetY);

        const double th = tiltDeg * 3.14159265358979 / 180.0;
        proj_.cameraDist = lanetilt::CAMERA_DIST;
        proj_.cosT  = std::cos(th);
        proj_.sinT  = std::sin(th);
        proj_.pivot = pivotY;
        proj_.cx    = centerX;
        proj_.zoom  = zoomFor(tiltDeg);
        proj_.extra = extra_;
        // カメラの後ろ（C + d·sinθ ≦ 0）に回り込む手前で止める。そこまで手前の物は画面の外なので見た目は変わらない
        proj_.dMin  = -0.7 * lanetilt::CAMERA_DIST / std::max(proj_.sinT, 1e-6);
        proj_.feather = featherTex_;
        proj_.offsetY = offsetY;

        // ぼかし：延長したレーンの一番奥（y = 0）と一番手前（y = textureLength）が画面のどこに来るかから決める
        proj_.fade = false;
        float X, yTop, yBottom, s, a;
        proj_.map(centerX, 0.0, X, yTop, s, a);
        proj_.map(centerX, textureLength(), X, yBottom, s, a);
        fadeRatio = std::clamp(fadeRatio, 0.0, 1.0);
        proj_.fadeTopY = yTop;
        proj_.fadeEndY = yTop + (yBottom - yTop) * static_cast<float>(fadeRatio);
        proj_.fade = (fadeRatio > 0.0);

        g_tiltProj = &proj_;
        return extra_;
    }

    // 延長したレーンの長さ（上の延長 + 画面 + 下の延長）。レーンはこの長さまで描く
    int textureLength() const{ return SCREEN_H + extra_ + extraBottom_; }

    // 区切り線（フレーム）を描いて、傾けて描くのを終える
    //   lineXs     : 区切り線の x（ずらす前の、画面の x）。lineCount 本
    //   lineStartY : 区切り線を描き始める y（描く側の座標）。レーンカバーの壁の下の端を渡すと、壁の上には線を描かない
    //   （pivotY・centerX は begin() で渡した値を使う。引数は前の版と同じ形にしておくためのもの）
    //   lineColor：区切り線の色。rgbSplitPx が 0 より大きいときは、赤と青の色ずれの線を左右に薄く重ねる（ホログラムのグリッチ）
    void end(SDL_Renderer* renderer, double /*tiltDeg*/, double /*pivotY*/, double /*centerX*/,
             const float* lineXs, int lineCount, double lineStartY = 0.0,
             SDL_Color lineColor = lanetilt::LINE_COLOR, float rgbSplitPx = 0.0f, Uint8 rgbSplitAlpha = 0){
        if(g_tiltProj == &proj_){
            // 線は「描き始め → ぼかしが終わる所 → 手前の端」の2本に分けて描く（ぼかしの濃さを正しく付けるため）
            const double yLen  = textureLength();
            const double yStart = std::clamp(lineStartY, 0.0, yLen);
            const double yFade = std::max(yStart, proj_.fade ? std::clamp(proj_.unmapY(proj_.fadeEndY), 0.0, yLen) : 0.0);
            auto drawAt = [&](float x, SDL_Color c){
                if(proj_.fade && yFade > yStart + 0.5){
                    tiltLine(renderer, x, static_cast<float>(yStart), x, static_cast<float>(yFade), lanetilt::LINE_WIDTH, c);
                }
                tiltLine(renderer, x, static_cast<float>(yFade), x, static_cast<float>(yLen), lanetilt::LINE_WIDTH, c);
            };
            for(int i = 0; i < lineCount; i++){
                if(rgbSplitPx > 0.0f){
                    drawAt(lineXs[i] - rgbSplitPx, SDL_Color{255, 40, 60, rgbSplitAlpha});
                    drawAt(lineXs[i] + rgbSplitPx, SDL_Color{40, 200, 255, rgbSplitAlpha});
                }
                drawAt(lineXs[i], lineColor);
            }
        }
        g_tiltProj = nullptr;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    }

    void destroy(){
        if(g_tiltProj == &proj_) g_tiltProj = nullptr;
        if(featherTex_){ SDL_DestroyTexture(featherTex_); featherTex_ = nullptr; }
        texDeg_ = -1.0;
    }

    // 画面の上で「判定ラインから、見えているレーンの奥の端まで」の ratio 倍の所に届くには、
    // 描く側の座標で判定ラインから何 px 上まで描けばよいか（キービームの長さに使う）
    double heightForScreenRatio(double tiltDeg, double pivotY, double ratio){
        const double th = tiltDeg * 3.14159265358979 / 180.0;
        const double C = lanetilt::CAMERA_DIST, cosT = std::cos(th), sinT = std::sin(th), zoom = zoomFor(tiltDeg);
        auto screenY = [&](double d){ return pivotY - d * cosT * (C / (C + d * sinT)) * zoom; };
        const double visibleTop = std::max(0.0, screenY(pivotY + extra_));   // 画面の上より上は見えない
        const double ys  = pivotY - ratio * (pivotY - visibleTop);
        const double Yr  = (pivotY - ys) / zoom;
        return Yr * C / (C * cosT - Yr * sinT);
    }

    // ノーツの太さ（縦の長さ）を、傾けても「幅との割合」が変わらないようにするための倍率
    //   倒すと、縦の長さは cosθ·s²·zoom 倍、幅は s·zoom 倍 に見えるので、縦を先に 1 / (cosθ·s) 倍しておく
    //   d は判定ラインからの高さ（px、上がプラス）
    static double thicknessScale(double tiltDeg, double d){
        if(!active(tiltDeg) || !lanetilt::KEEP_NOTE_SHAPE) return 1.0;
        const double th = tiltDeg * 3.14159265358979 / 180.0;
        const double s  = lanetilt::CAMERA_DIST / (lanetilt::CAMERA_DIST + d * std::sin(th));
        return 1.0 / std::max(std::cos(th) * s, 0.01);
    }

    static double zoomFor(double tiltDeg){
        const double sinT = std::sin(tiltDeg * 3.14159265358979 / 180.0);
        return 1.0 + lanetilt::ZOOM_STRENGTH * lanetilt::ZOOM_REF_D * sinT / lanetilt::CAMERA_DIST;
    }

    // 倒して zoom 倍にしたレーンの奥の端が画面の上（y = 0）に届くには、画面の上の端より何 px 先まで描けばよいか
    //   margin：画面の上の端より、さらにこの px 上まで届かせる（レーンの左右の傾きで上下に余分に描くとき）
    static int extraFor(double tiltDeg, double pivotY, double margin = 0.0){
        const double th = tiltDeg * 3.14159265358979 / 180.0;
        const double C  = lanetilt::CAMERA_DIST;
        const double Y  = (pivotY + margin) / zoomFor(tiltDeg);
        const double denom = C * std::cos(th) - Y * std::sin(th);
        if(denom <= 1.0) return lanetilt::MAX_EXTRA;
        const double d = Y * C / denom;
        const int extra = static_cast<int>(std::ceil(d - pivotY)) + 8;
        return std::clamp(extra, 0, lanetilt::MAX_EXTRA);
    }

    // 倒して zoom 倍にしたレーンの手前の端が画面の下（y = SCREEN_H）に届くには、画面の下の端より何 px 先まで描けばよいか
    //   margin：画面の下の端より、さらにこの px 下まで届かせる
    static int extraBottomFor(double tiltDeg, double pivotY, double margin = 0.0){
        const double th = tiltDeg * 3.14159265358979 / 180.0;
        const double C  = lanetilt::CAMERA_DIST;
        const double Y  = -(SCREEN_H - pivotY + margin) / zoomFor(tiltDeg);
        const double d  = Y * C / (C * std::cos(th) - Y * std::sin(th));
        const int extra = static_cast<int>(std::ceil((pivotY - d) - SCREEN_H)) + 8;
        return std::clamp(extra, 0, lanetilt::MAX_EXTRA);
    }

private:
    TiltProjection proj_;
    SDL_Texture* featherTex_ = nullptr;   // なめらかな線用：横 8px の、両端が透明・真ん中が白の小さな画像
    int    extra_ = 0;
    int    extraBottom_ = 0;
    double texDeg_ = -1.0;
    double texMargin_ = 0.0;   // extra_ を計算したときの「上下に余分に届かせる量」

    void ensureResources(SDL_Renderer* renderer, double tiltDeg, double pivotY, double margin){
        if(texDeg_ != tiltDeg || texMargin_ != margin){
            extra_       = extraFor(tiltDeg, pivotY, margin);
            extraBottom_ = extraBottomFor(tiltDeg, pivotY, margin);
            texDeg_      = tiltDeg;
            texMargin_   = margin;
        }
        if(featherTex_) return;

        // 線を描くための小さな画像。線の横方向に引き伸ばすと、縁が 1px ほどなめらかに消える
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, 8, 1, 32, SDL_PIXELFORMAT_RGBA32);
        if(!s) return;
        const Uint8 profile[8] = {0, 90, 255, 255, 255, 255, 90, 0};
        Uint32* px = static_cast<Uint32*>(s->pixels);
        for(int i = 0; i < 8; i++) px[i] = SDL_MapRGBA(s->format, 255, 255, 255, profile[i]);
        featherTex_ = SDL_CreateTextureFromSurface(renderer, s);
        SDL_FreeSurface(s);
        if(featherTex_){
            SDL_SetTextureBlendMode(featherTex_, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(featherTex_, SDL_ScaleModeLinear);
        }
    }
};