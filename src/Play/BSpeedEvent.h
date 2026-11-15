#pragma once

// =====================================================================
//  BSpeedEvent.h — B_SpeedEvent（速度で動かすノーツの速さ）
//
//  ■ A_SpeedEvent との違い
//    A：座標を求める式    y = 判定ライン − (ノーツの時刻 − 今) × 速さ
//       速さを変えると、画面のノーツ全部の「間隔」が一緒に伸び縮みする。ノーツを止めることはできない
//    B：速度を求める式    ノーツが 1ms に進む量 = 速さ × 倍率 m(今)
//       m を 0 にすると、画面のノーツが全部その場で止まる。m を 2 にすると 2 倍の速さで流れる
//
//  ■ B_SpeedEvent の値の単位は A_SpeedEvent と同じ（B_SPEED_BASE = 1.8 が「ふつう」）
//    譜面に書く値 target は A_SpeedEvent と同じ「速さ」で、倍率 m = target ÷ B_SPEED_BASE に直して使う。
//    → B の 1.8 は A の 1.8 と同じ速さ、B の 3.6 は A の 3.6 と同じ速さ、B の 0 は停止
//    （プレイヤーのノーツ速度の設定は、A と同じく後から掛かる）
//
//  ■ 仕組み（スクロール位置）
//    倍率 m(t) を時間で足し合わせた「スクロール位置」S(t) = ∫ m(τ) dτ を使い、
//        y = 判定ライン − (S(ノーツの時刻) − S(今)) × 速さ（A_SpeedEvent・ノーツ個別の速さ）
//    で位置を出す。
//    ・B_SpeedEvent が 1 つもない間は m = 1 なので S(t) = t となり、前の「座標を求める式」とまったく同じになる
//    ・B_SpeedEvent の時刻を過ぎると、m が変わり、ノーツの動きは「速度」で決まるようになる
//      （＝定められた時刻で、計算のしかたが座標の式から速度の式へ切り替わる）
//    ・どのノーツも S(ノーツの時刻) = S(今) になる瞬間＝自分の時刻ぴったりに判定ラインに来るので、
//      止めたり速めたりしても、判定の時刻はずれない
//
//  ■ 1 つのイベントの中身（A_SpeedEvent と同じ形）
//    time（変わり始める時刻）・target（目標の速さ。A_SpeedEvent と同じ単位。1.8 = ふつう、0 = 停止）・
//    duration（変化にかける時間 ms）・easing（1 = 直線、2 = easeOutCubic、3 = easeInCubic、それ以外 = すぐ切り替え）
//    変化の途中で次のイベントが始まったら、その時の倍率から次の目標へ向かう（A_SpeedEvent と同じ）
//
//  ■ S(t) の計算
//    イベントごとに区間を作り、区間の始まりの S を先に求めておく。
//    区間の中の S は、イージングの式をそのまま積分した式で出す（細かく足し合わせないので速く、ずれない）
//      直線         ∫₀ᵘ τ dτ           = u² / 2
//      easeOutCubic ∫₀ᵘ 1−(1−τ)³ dτ    = u − (1 − (1−u)⁴) / 4
//      easeInCubic  ∫₀ᵘ τ³ dτ          = u⁴ / 4
//
//  ■ 譜面ファイル：各難易度の "bSpeedEvents" に
//      {"time": 12000, "target": 0.0, "easing": 1, "duration": 0}
//    の形で入る（A_SpeedEvent の "speedEvents" と同じ形）
// =====================================================================

#include "GameCommon.h"
#include "Play/loadScore.h"   // json・toUtf8Path
#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>

// A_SpeedEvent と同じ単位にするための「ふつうの速さ」（PlayScene.cpp のノーツ速度の初めの値と同じ）
inline constexpr double B_SPEED_BASE = 1.8;

struct B_SpeedEvent{
    int32_t time     = 0;
    double  target   = B_SPEED_BASE;   // 目標の速さ（A_SpeedEvent と同じ単位。1.8 = ふつう、0 = 停止、マイナスで逆向き）
    int     easing   = 1;
    int32_t duration = 500;
};

class ScrollMap{
public:
    // イベント（時刻の順に並べ直す）から区間を作る
    //   baseSpeed：「ふつう」の速さ。target ÷ baseSpeed を倍率にする（target が A_SpeedEvent と同じ単位になる）
    void build(std::vector<B_SpeedEvent> events, double baseSpeed = B_SPEED_BASE){
        if(baseSpeed <= 0.0) baseSpeed = B_SPEED_BASE;
        segs_.clear();
        std::stable_sort(events.begin(), events.end(), [](const B_SpeedEvent& a, const B_SpeedEvent& b){ return a.time < b.time; });
        for(const B_SpeedEvent& ev : events){
            Seg s;
            s.t0     = static_cast<double>(ev.time);
            s.m0     = multiplierAt(s.t0);   // その時の倍率から変わり始める
            s.m1     = ev.target / baseSpeed;   // A と同じ単位の速さ → 倍率
            s.dur    = std::max(0.0, static_cast<double>(ev.duration));
            s.easing = (s.dur > 0.0 && ev.easing >= 1 && ev.easing <= 3) ? ev.easing : 0;   // 0 = すぐ切り替え
            s.s0     = at(s.t0);
            segs_.push_back(s);
        }
    }

    bool empty() const{ return segs_.empty(); }

    // スクロール位置 S(t)（単位は ms。B_SpeedEvent がなければ t のまま）
    double at(double t) const{
        const Seg* s = find(t);
        if(!s) return t;
        return s->s0 + integral(*s, t - s->t0);
    }

    // 時刻 t の倍率 m(t)
    double multiplierAt(double t) const{
        const Seg* s = find(t);
        if(!s) return 1.0;
        const double dt = t - s->t0;
        if(s->easing == 0 || dt >= s->dur) return s->m1;
        return s->m0 + (s->m1 - s->m0) * ease(s->easing, dt / s->dur);
    }

private:
    struct Seg{
        double t0 = 0.0;      // 始まる時刻
        double m0 = 1.0;      // 始まりの倍率
        double m1 = 1.0;      // 目標の倍率
        double dur = 0.0;     // 変化にかける時間
        int    easing = 0;    // 0 = すぐ切り替え、1〜3 = イージング
        double s0 = 0.0;      // 始まりのスクロール位置 S(t0)
    };
    std::vector<Seg> segs_;

    // t を含む区間（t より前で一番近い区間）。なければ nullptr（一番最初のイベントより前）
    const Seg* find(double t) const{
        if(segs_.empty() || t < segs_.front().t0) return nullptr;
        auto it = std::upper_bound(segs_.begin(), segs_.end(), t, [](double v, const Seg& s){ return v < s.t0; });
        return &*(it - 1);
    }

    static double ease(int easing, double u){
        u = std::clamp(u, 0.0, 1.0);
        switch(easing){
            case 1:  return u;
            case 2:  return 1.0 - std::pow(1.0 - u, 3.0);
            case 3:  return u * u * u;
            default: return 1.0;
        }
    }
    // ∫₀ᵘ ease(τ) dτ
    static double easeIntegral(int easing, double u){
        u = std::clamp(u, 0.0, 1.0);
        switch(easing){
            case 1:  return u * u * 0.5;
            case 2:  return u - (1.0 - std::pow(1.0 - u, 4.0)) * 0.25;
            case 3:  return u * u * u * u * 0.25;
            default: return u;
        }
    }
    // 区間の始まりから dt ms の間に進むスクロール量
    static double integral(const Seg& s, double dt){
        if(s.easing == 0) return s.m1 * dt;
        if(dt <= s.dur){
            return s.m0 * dt + (s.m1 - s.m0) * s.dur * easeIntegral(s.easing, dt / s.dur);
        }
        const double during = s.m0 * s.dur + (s.m1 - s.m0) * s.dur * easeIntegral(s.easing, 1.0);
        return during + s.m1 * (dt - s.dur);
    }
};

// 譜面ファイルから、この難易度の B_SpeedEvent を読む（無ければ空のまま。読めなくても遊べるので false でも続けてよい）
//   難易度の選び方は loadScore.h と同じ（"difficulties" があればその中の difficultyIndex 番目、なければ一番外側）
inline bool loadBSpeedEvents(const std::string& filename, int difficultyIndex, std::vector<B_SpeedEvent>& out){
    out.clear();
    std::ifstream file(toUtf8Path(filename));
    if(!file.is_open()) return false;
    json j;
    try{
        file >> j;
    }
    catch(const json::exception&){
        return false;
    }
    const json* root = &j;
    if(j.contains("difficulties") && j.at("difficulties").is_array()){
        const auto& diffs = j.at("difficulties");
        if(difficultyIndex < 0 || static_cast<size_t>(difficultyIndex) >= diffs.size()) return false;
        root = &diffs[difficultyIndex];
    }
    if(!root->contains("bSpeedEvents") || !root->at("bSpeedEvents").is_array()) return true;
    for(const auto& item : root->at("bSpeedEvents")){
        try{
            B_SpeedEvent ev;
            ev.time     = item.at("time").get<int32_t>();
            ev.target   = item.value("target", B_SPEED_BASE);
            ev.easing   = item.value("easing", 1);
            ev.duration = item.value("duration", 500);
            out.push_back(ev);
        }
        catch(const json::exception& e){
            printf("JSON / bSpeedEvents / error : %s\n", e.what());
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const B_SpeedEvent& a, const B_SpeedEvent& b){ return a.time < b.time; });
    return true;
}