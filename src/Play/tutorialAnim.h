#pragma once

#include <string>
#include <algorithm>
#include <cmath>
 
enum class TutAnim{ None, Fade, Pop, SlideUp, SlideDown, Wipe };
 
// 文字列から種類を求める。知らない名前なら false(呼び出し側で警告を出す)
inline bool parseTutAnim(const std::string& s, TutAnim& out){
    if(s == "none")      { out = TutAnim::None;      return true; }
    if(s == "fade")      { out = TutAnim::Fade;      return true; }
    if(s == "pop")       { out = TutAnim::Pop;       return true; }
    if(s == "slideUp")   { out = TutAnim::SlideUp;   return true; }
    if(s == "slideDown") { out = TutAnim::SlideDown; return true; }
    if(s == "wipe")      { out = TutAnim::Wipe;      return true; }
    return false;
}
 
struct TutAnimState{
    bool  visible = false;   // この時刻に表示するか
    float alpha   = 0.0f;    // 0〜1
    float scale   = 1.0f;    // 中心を基準にした拡大率
    float dx      = 0.0f;    // 位置のずれ(px)
    float dy      = 0.0f;
    float reveal  = 1.0f;    // 左から何割まで見えているか(wipe用)
};
 
// ※ 名前は他と重ならないように tut を付けている。
//    プロジェクト側の tutEaseOutCubic(Graphics.h)や C++20 の std::lerp と
//    名前が重なると「どれを呼ぶか決められない」というビルドエラーになる。
inline double tutClamp01(double v){ return std::max(0.0, std::min(1.0, v)); }
 
inline double tutEaseOutCubic(double t){
    const double u = 1.0 - t;
    return 1.0 - u * u * u;
}
 
// 少し行き過ぎてから1に収まる(t=0 で 0、t=1 で 1、途中で最大およそ1.1)
inline double tutEaseOutBack(double t){
    const double c1 = 1.70158, c3 = c1 + 1.0;
    const double x = t - 1.0;
    return 1.0 + c3 * x * x * x + c1 * x * x;
}
 
inline double tutLerp(double a, double b, double t){ return a + (b - a) * t; }
 
constexpr float kTutSlideDistancePx = 40.0f;
 
inline TutAnimState computeTutAnim(TutAnim anim, double t, double start, double end, double inMs, double outMs){
    TutAnimState s;
 
    if(!(end > start)) return s;
    if(t < start || t >= end) return s;
 
    const double dur = end - start;
    inMs  = std::max(0.0, inMs);
    outMs = std::max(0.0, outMs);
    if(inMs + outMs > dur){            // 短い表示では、現れる/消える時間を同じ割合で縮める
        const double k = dur / (inMs + outMs);
        inMs  *= k;
        outMs *= k;
    }
 
    const double inP  = (inMs  > 0.0) ? tutClamp01((t - start) / inMs)  : 1.0;
    const double outP = (outMs > 0.0) ? tutClamp01((end - t) / outMs)   : 1.0;
 
    bool entering = inP  < 1.0;
    bool exiting  = outP < 1.0;
    if(entering && exiting){           // 境界ぴったりで両方立つ場合は、進みの小さい側を採る
        if(inP <= outP) exiting = false;
        else            entering = false;
    }
 
    s.visible = true;
    s.alpha = 1.0f;
 
    if(anim == TutAnim::None) return s;
 
    const double e = entering ? inP : (exiting ? outP : 1.0);
    const double eased = tutEaseOutCubic(e);
 
    switch(anim){
        case TutAnim::Fade:
            s.alpha = static_cast<float>(eased);
            break;
 
        case TutAnim::Pop:
            s.alpha = static_cast<float>(eased);
            if(entering)      s.scale = static_cast<float>(tutLerp(0.6,  1.0, tutEaseOutBack(e)));
            else if(exiting)  s.scale = static_cast<float>(tutLerp(0.85, 1.0, eased));
            break;
 
        case TutAnim::SlideUp:
            s.alpha = static_cast<float>(eased);
            if(entering)      s.dy =  static_cast<float>((1.0 - eased) * kTutSlideDistancePx);
            else if(exiting)  s.dy = -static_cast<float>((1.0 - eased) * kTutSlideDistancePx);
            break;
 
        case TutAnim::SlideDown:
            s.alpha = static_cast<float>(eased);
            if(entering)      s.dy = -static_cast<float>((1.0 - eased) * kTutSlideDistancePx);
            else if(exiting)  s.dy =  static_cast<float>((1.0 - eased) * kTutSlideDistancePx);
            break;
 
        case TutAnim::Wipe:
            if(entering)      s.reveal = static_cast<float>(eased);
            else if(exiting)  s.alpha  = static_cast<float>(eased);
            break;
 
        case TutAnim::None:
            break;
    }
    return s;
}
