#pragma once

// ====================================================================
//  editorTempo.h — 譜面エディタ用のテンポ表(BPM + BPMイベント)
//
//  「拍」は 4分音符=1 の単位で、時刻0msが拍0。
//  区間ごとにBPMが変わっても、拍と時刻を相互に変換できる。
//    tempoBeatAt : 時刻(ms)  → 拍
//    tempoTimeAt : 拍        → 時刻(ms)
//  BPMを変えたときは「古いテンポ表での拍位置」を「新しいテンポ表の時刻」に
//  戻すことで、ノーツの拍位置を保ったまま動かせる(remapTime)。
// ====================================================================

#include <vector>
#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>

struct TempoPoint{
    double timeMs = 0.0;   // この時刻からbpmに切り替わる
    double bpm = 120.0;
};

struct TempoMap{
    double baseBpm = 120.0;          // 最初のBPMイベントより前(時刻0を含む)のBPM
    std::vector<TempoPoint> pts;     // 時刻順
};

inline TempoMap makeTempoMap(double baseBpm, std::vector<TempoPoint> events){
    TempoMap m;
    m.baseBpm = (baseBpm > 0.0) ? baseBpm : 1.0;
    events.erase(std::remove_if(events.begin(), events.end(),
        [](const TempoPoint& p){ return !(p.bpm > 0.0); }), events.end());
    std::stable_sort(events.begin(), events.end(),
        [](const TempoPoint& a, const TempoPoint& b){ return a.timeMs < b.timeMs; });
    m.pts = std::move(events);
    return m;
}

inline bool tempoEqual(const TempoMap& a, const TempoMap& b){
    if(a.baseBpm != b.baseBpm || a.pts.size() != b.pts.size()) return false;
    for(size_t i = 0; i < a.pts.size(); i++){
        if(a.pts[i].timeMs != b.pts[i].timeMs || a.pts[i].bpm != b.pts[i].bpm) return false;
    }
    return true;
}

// x ms の時点で有効なBPM(x ちょうどに切り替わるイベントも含む)
inline double tempoBpmAt(const TempoMap& m, double x){
    double v = m.baseBpm;
    for(const auto& p : m.pts){
        if(p.timeMs <= x) v = p.bpm;
        else break;
    }
    return v;
}

namespace tempo_detail{

// x ms の直前の区間のBPM(x ちょうどに切り替わるイベントは含まない)
inline double bpmBefore(const TempoMap& m, double x){
    double v = m.baseBpm;
    for(const auto& p : m.pts){
        if(p.timeMs < x) v = p.bpm;
        else break;
    }
    return v;
}

inline double nextChange(const TempoMap& m, double x){
    for(const auto& p : m.pts) if(p.timeMs > x) return p.timeMs;
    return std::numeric_limits<double>::infinity();
}

inline double prevChange(const TempoMap& m, double x){
    double v = -std::numeric_limits<double>::infinity();
    for(const auto& p : m.pts){
        if(p.timeMs < x) v = p.timeMs;
        else break;
    }
    return v;
}

} // namespace tempo_detail

// 時刻(ms) → 拍
inline double tempoBeatAt(const TempoMap& m, double t){
    using namespace tempo_detail;
    double cur = 0.0, sum = 0.0;
    if(t >= 0.0){
        while(cur < t){
            const double r = tempoBpmAt(m, cur) / 60000.0;
            const double end = std::min(nextChange(m, cur), t);
            sum += (end - cur) * r;
            cur = end;
        }
    }
    else{
        while(cur > t){
            const double r = bpmBefore(m, cur) / 60000.0;
            const double start = std::max(prevChange(m, cur), t);
            sum -= (cur - start) * r;
            cur = start;
        }
    }
    return sum;
}

// 拍 → 時刻(ms)
inline double tempoTimeAt(const TempoMap& m, double beat){
    using namespace tempo_detail;
    double cur = 0.0;
    if(beat >= 0.0){
        double remaining = beat;
        for(;;){
            const double r = tempoBpmAt(m, cur) / 60000.0;
            const double nb = nextChange(m, cur);
            if(!std::isfinite(nb)) return cur + remaining / r;
            const double segBeats = (nb - cur) * r;
            if(remaining <= segBeats) return cur + remaining / r;
            remaining -= segBeats;
            cur = nb;
        }
    }
    double remaining = -beat;
    for(;;){
        const double r = bpmBefore(m, cur) / 60000.0;
        const double pb = prevChange(m, cur);
        if(!std::isfinite(pb)) return cur - remaining / r;
        const double segBeats = (cur - pb) * r;
        if(remaining <= segBeats) return cur - remaining / r;
        remaining -= segBeats;
        cur = pb;
    }
}

// 時刻を、gridBeats(拍)刻みのグリッドのうち最も近い線に吸着させる
inline double tempoSnap(const TempoMap& m, double tMs, double gridBeats){
    const double k = std::round(tempoBeatAt(m, tMs) / gridBeats);
    return tempoTimeAt(m, k * gridBeats);
}

// 現在位置から、次(dir>0)/前(dir<0)のグリッド線へ進む。ちょうど線上にいるときは1マス動く
inline double tempoStep(const TempoMap& m, double tMs, double gridBeats, int dir){
    const double k = tempoBeatAt(m, tMs) / gridBeats;
    const double kk = (dir > 0) ? std::floor(k + 1.0e-9) + 1.0 : std::ceil(k - 1.0e-9) - 1.0;
    return tempoTimeAt(m, kk * gridBeats);
}

// tempoStep の整数ms版。時刻は整数msなので、線が1ms未満の丸めで「今いる位置」と
// 同じになってしまう場合(切り捨てで線のわずか手前に着地したときなど)は、
// さらに次の線まで進める。これをしないとホイール/W/Sがその位置から動かなくなる。
inline int32_t tempoStepMs(const TempoMap& m, int32_t fromMs, double gridBeats, int dir){
    const double k = tempoBeatAt(m, static_cast<double>(fromMs)) / gridBeats;
    double kk = (dir > 0) ? std::floor(k + 1.0e-9) + 1.0 : std::ceil(k - 1.0e-9) - 1.0;
    for(int i = 0; i < 4096; i++){
        const int32_t t = static_cast<int32_t>(std::lround(tempoTimeAt(m, kk * gridBeats)));
        if((dir > 0 && t > fromMs) || (dir < 0 && t < fromMs)) return t;
        kk += (dir > 0) ? 1.0 : -1.0;
    }
    return fromMs + ((dir > 0) ? 1 : -1);   // グリッドが1msより細かすぎる場合は1msずつ
}

// 古いテンポ表での拍位置を保ったまま、新しいテンポ表での時刻(ms, 整数)に変換する
inline int32_t remapTime(const TempoMap& from, const TempoMap& to, int32_t t){
    return static_cast<int32_t>(std::lround(tempoTimeAt(to, tempoBeatAt(from, static_cast<double>(t)))));
}

inline bool tempoBreakpointsSameTimes(const TempoMap& a, const TempoMap& b){
    if(a.pts.size() != b.pts.size()) return false;
    for(size_t i = 0; i < a.pts.size(); i++){
        if(a.pts[i].timeMs != b.pts[i].timeMs) return false;
    }
    return true;
}