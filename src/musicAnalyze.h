#pragma once

// ====================================================================
//  musicAnalyze.h — 「Music Settings」用：オフセット・BPM・BPM の変化を自動で決める
//
//  やること（analyzeMusic）
//    1. 音源をオンセット強度（音の立ち上がりの強さ）の列にする（約 400 回/秒 = 2.5ms 刻み）
//    2. 最初の約 20 秒で BPM と拍の位置（位相）を細かく詰め、「音源の先頭に一番近い拍」をオフセットにする
//    3. 今のグリッド（今の BPM で刻んだ拍の線）が、曲の拍に乗り続けているかを 8 秒の窓で 1 秒ずつ確かめる
//       ・別のテンポのほうがはっきりよく合う窓、または拍の線が少しずつずれていく窓が 4 つ続いたら「変化の候補」
//       ・窓 1 つ 1 つの BPM は、ボーカルやフィル・ハーフタイムで ±数 BPM 揺れるので、それだけでは決めない
//    4. 候補が出たら、変化点を「変化直前の拍の上」から選び、そこを拍の 1 つ目に固定して新しい BPM を測る
//    5. 変化点のあと 16 秒で「新しいテンポ」と「前のテンポのまま」を比べ、新しいほうがはっきりよく合うときだけ
//       BPM イベントを置く（揺れによるまちがいの変化を置かないため）。0.25 BPM 未満の差も置かない
//    6. 区間が決まったら、その区間全体で BPM を測り直す
//    7. 最後に見直し：変化ごとに、その区間全体（次の変化か曲の終わりまで）で次の 2 つを確かめ、どちらかがだめなら消す
//       ・新しいテンポのほうが「変化を置かずに前のテンポのグリッドを続けた場合」よりはっきりよく合う
//       ・区間の中の 8 秒窓の BPM の真ん中の値（中央値）が、前のテンポより新しいテンポに近い
//         （音が詰まった曲では、本当のテンポから区間のあいだにちょうど 1〜2 拍ずれるテンポが、
//           長い範囲の比べ方だけだと同じくらいよく見えてしまう。短い窓ならだまされない）
//       ・窓が 3 つ入らない短い区間（約 10 秒未満）は確かめられないので、変化として置かない
//       消したら前の区間とつないで測り直し、残った最初の区間で BPM とオフセットを曲全体ぶん詰め直す
//    8. 整数に近い BPM（±0.15 以内）は、整数にしても拍の合い方がほとんど落ちないときだけ整数にする
//
//  倍テン・半テン（200 と 100 など）は区別しない。結果を見て x2・/2 で直す。
//  bpmdetect.h とは独立している（同じ考え方を、精度を上げて書き直したもの）。
// ====================================================================

#include "GameCommon.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <cstdint>

// 拍子は解析しない（エディタで手で決める）。sigNum / sigDen はいつも 4/4 のまま（ChartCreate.cpp との互換のために残している）
struct MusicBpmChange{
    double audioMs = 0.0;   // 音源上の位置
    double chartMs = 0.0;   // 譜面上の時刻（= audioMs - offsetMs）
    double bpm = 0.0;
    int sigNum = 4;            // 使わない（互換用）
    int sigDen = 4;
    bool tempoChange = true;   // いつも true（BPM が変わる所だけ）
    double dottedBpm = 0.0;    // いつも 0（BPM を 1.5 倍にすることはない）
};

struct MusicAnalysis{
    bool ok = false;
    std::string error;
    double offsetMs = 0.0;                 // 譜面の 0ms が、音源の何 ms にあたるか
    double bpm = 0.0;                      // 曲頭の BPM
    double clarity = 0.0;                  // 0.0〜1.0：拍がどれだけはっきりしているか（目安）
    double lengthSec = 0.0;                // 解析した長さ
    std::string pattern = "拍だけ";        // 比べるときに使った拍の型（この方式は拍の上だけで比べる）
    int sigNum = 4;                        // 使わない（互換用）
    int sigDen = 4;
    double dottedBpm = 0.0;                // いつも 0（互換用）
    std::vector<MusicBpmChange> changes;   // 途中の BPM 変化（時刻順）
};

namespace music_detail{

inline constexpr double kTargetRate   = 400.0;   // オンセット強度の細かさ（回/秒）
inline constexpr double kWinSec       = 8.0;     // 窓の長さ
inline constexpr double kHopSec       = 1.0;     // 窓をずらす量
inline constexpr double kInitSec      = 20.0;    // 最初の BPM を測る長さ
inline constexpr int    kConfirmWins  = 4;       // 合わない窓が何個続いたら変化の候補にするか
inline constexpr double kAltBetter    = 1.25;    // 別のテンポの合い方が今のグリッドの何倍以上なら「合わない窓」
inline constexpr double kDriftMs      = 20.0;    // 拍の線がこれ以上ずれていたら「ずれている窓」
inline constexpr double kVerifySec    = 16.0;    // 変化の確かめに使う長さ
inline constexpr double kAccept       = 1.15;    // 新しいテンポの合い方が前のテンポの何倍以上なら変化の候補として残す
inline constexpr double kKeepLong     = 1.10;    // 最後の見直し：区間全体で、新しいテンポが前のテンポの何倍以上合えば変化を残すか
inline constexpr double kKeepShiftMs  = 30.0;    // 最後の見直しで、前のテンポのグリッドをずらしてよい量（前の BPM のわずかな誤差のぶん）
inline constexpr double kIgnoreChange = 0.25;    // これ未満の BPM の差は変化として置かない（0.1〜0.2 の揺れは無視）
inline constexpr double kRoundRange   = 0.15;    // 整数からこれ以内なら、整数にしてよいか調べる
inline constexpr double kRoundKeep    = 0.98;    // 整数にしても拍の合い方がこの割合以上なら整数にする

struct Env{
    std::vector<double> v;
    double rate = 0.0;   // 1 秒あたりのサンプル数
    double size() const { return static_cast<double>(v.size()); }
};

// 小数の位置で線形補間して読む（範囲外は 0）
inline double at(const Env& e, double pos){
    if(pos < 0.0) return 0.0;
    const size_t i = static_cast<size_t>(pos);
    if(i + 1 >= e.v.size()) return 0.0;
    const double fr = pos - static_cast<double>(i);
    return e.v[i] * (1.0 - fr) + e.v[i + 1] * fr;
}

// ---------------------------------------------------------------------
//  PCM（16bit）→ オンセット強度
//    モノラル化 → 約 11kHz に間引き → 低・中・高の 3 帯域のエネルギーの増え方を足す
//    → 前後 0.5 秒の平均を引いて正の部分だけ残す → 5 点で軽くならす
// ---------------------------------------------------------------------
inline bool buildEnvFromPcm(const int16_t* pcm, size_t totalFrames, int ch, int freq, Env& out, std::string& err){
    const double kPi = 3.14159265358979323846;

    const size_t dec = std::max<size_t>(1, static_cast<size_t>(freq) / 11025);
    const double fs = static_cast<double>(freq) / static_cast<double>(dec);
    const size_t n2 = totalFrames / dec;   // 曲全体（長さの制限なし）

    if(static_cast<double>(n2) < fs * 10.0){
        err = "曲が短すぎます（10 秒以上必要）";
        return false;
    }

    std::vector<float> x(n2);
    for(size_t i = 0; i < n2; i++){
        double s = 0.0;
        for(size_t d = 0; d < dec; d++){
            const size_t fi = i * dec + d;
            for(int c = 0; c < ch; c++) s += pcm[fi * static_cast<size_t>(ch) + static_cast<size_t>(c)];
        }
        x[i] = static_cast<float>(s / (32768.0 * ch * static_cast<double>(dec)));
    }

    const size_t hop = std::max<size_t>(1, static_cast<size_t>(std::llround(fs / kTargetRate)));
    const double envRate = fs / static_cast<double>(hop);
    const size_t nF = n2 / hop;

    const double aLow = 1.0 - std::exp(-2.0 * kPi * 150.0 / fs);
    const double aMid = 1.0 - std::exp(-2.0 * kPi * 2000.0 / fs);
    double lpLow = 0.0, lpMid = 0.0;

    std::vector<double> eLow(nF), eMid(nF), eHigh(nF);
    const double invHop = 1.0 / static_cast<double>(hop);
    for(size_t f = 0; f < nF; f++){
        double sl = 0.0, sm = 0.0, sh = 0.0;
        for(size_t i = f * hop; i < (f + 1) * hop; i++){
            const double v = x[i];
            lpLow += aLow * (v - lpLow);
            lpMid += aMid * (v - lpMid);
            const double low = lpLow;
            const double mid = lpMid - lpLow;
            const double high = v - lpMid;
            sl += low * low;
            sm += mid * mid;
            sh += high * high;
        }
        eLow[f]  = std::log(1.0 + 1.0e4 * sl * invHop);
        eMid[f]  = std::log(1.0 + 1.0e4 * sm * invHop);
        eHigh[f] = std::log(1.0 + 1.0e4 * sh * invHop);
    }

    std::vector<double> env(nF, 0.0);
    for(size_t f = 1; f < nF; f++){
        const double dl = eLow[f]  - eLow[f - 1];
        const double dm = eMid[f]  - eMid[f - 1];
        const double dh = eHigh[f] - eHigh[f - 1];
        env[f] = (dl > 0.0 ? dl : 0.0) + (dm > 0.0 ? dm : 0.0) + (dh > 0.0 ? dh : 0.0);
    }

    {
        std::vector<double> ps(nF + 1, 0.0);
        for(size_t i = 0; i < nF; i++) ps[i + 1] = ps[i] + env[i];

        const long nL = static_cast<long>(nF);
        const long W = static_cast<long>(envRate * 0.5);
        std::vector<double> tmp(nF);
        for(long i = 0; i < nL; i++){
            const long lo = std::max(0L, i - W);
            const long hi = std::min(nL, i + W + 1);
            const double mean = (ps[hi] - ps[lo]) / static_cast<double>(hi - lo);
            tmp[i] = std::max(0.0, env[i] - mean);
        }

        const double kern[5] = {1.0, 4.0, 6.0, 4.0, 1.0};
        for(long i = 0; i < nL; i++){
            double s = 0.0, w = 0.0;
            for(int j = -2; j <= 2; j++){
                const long idx = i + j;
                if(idx < 0 || idx >= nL) continue;
                s += tmp[idx] * kern[j + 2];
                w += kern[j + 2];
            }
            env[i] = s / w;
        }
    }

    double total = 0.0;
    for(double v : env) total += v;
    if(total < 1.0e-9){
        err = "拍が見つかりません（無音？）";
        return false;
    }

    out.v = std::move(env);
    out.rate = envRate;
    return true;
}

inline bool buildEnvFromFile(const std::string& path, Env& out, std::string& err){
    Mix_Chunk* chunk = Mix_LoadWAV(path.c_str());
    if(!chunk){
        err = std::string("読み込み失敗: ") + Mix_GetError();
        return false;
    }

    int freq = 0;
    Uint16 fmt = 0;
    int ch = 0;
    if(Mix_QuerySpec(&freq, &fmt, &ch) == 0 || fmt != AUDIO_S16SYS || ch < 1 || freq < 8000){
        Mix_FreeChunk(chunk);
        err = "対応していない音声形式です（16bit のみ）";
        return false;
    }

    const int16_t* pcm = reinterpret_cast<const int16_t*>(chunk->abuf);
    const size_t totalFrames = chunk->alen / (sizeof(int16_t) * static_cast<size_t>(ch));
    const bool ok = buildEnvFromPcm(pcm, totalFrames, ch, freq, out, err);
    Mix_FreeChunk(chunk);
    return ok;
}

// 2 サンプルずつ平均して半分の細かさにする（大まかなテンポを速く求めるため）
inline Env halve(const Env& e){
    Env h;
    h.rate = e.rate * 0.5;
    h.v.resize(e.v.size() / 2);
    for(size_t i = 0; i < h.v.size(); i++) h.v[i] = 0.5 * (e.v[i * 2] + e.v[i * 2 + 1]);
    return h;
}

// ---------------------------------------------------------------------
//  自己相関で大まかな BPM（60〜240）。priorCenter > 0 ならその付近を少し優先する
// ---------------------------------------------------------------------
inline double coarseBpm(const Env& e, size_t i0, size_t i1, double priorCenter, double priorSigmaOct){
    const int lagMin = std::max(1, static_cast<int>(std::floor(e.rate * 60.0 / 240.0)));
    const int lagMax = static_cast<int>(std::ceil(e.rate * 60.0 / 60.0));
    const int lagLimit = lagMax * 2 + 2;

    if(i1 > e.v.size()) i1 = e.v.size();
    if(i1 <= i0) return 0.0;
    const size_t n = i1 - i0;
    if(n <= static_cast<size_t>(lagLimit) * 2) return 0.0;

    std::vector<double> ac(static_cast<size_t>(lagLimit) + 1, 0.0);
    for(int lag = lagMin; lag <= lagLimit; lag++){
        const size_t cnt = n - static_cast<size_t>(lag);
        double s = 0.0;
        for(size_t i = 0; i < cnt; i++) s += e.v[i0 + i] * e.v[i0 + i + static_cast<size_t>(lag)];
        ac[static_cast<size_t>(lag)] = s / static_cast<double>(cnt);
    }

    int bestLag = lagMin;
    double bestScore = -1.0;
    for(int lag = lagMin; lag <= lagMax; lag++){
        double prior = 1.0;
        if(priorCenter > 0.0){
            const double bpmAt = 60.0 * e.rate / lag;
            const double oct = std::log2(bpmAt / priorCenter) / priorSigmaOct;
            prior = std::exp(-0.5 * oct * oct);
        }
        const double s = (ac[static_cast<size_t>(lag)] + 0.5 * ac[static_cast<size_t>(lag) * 2]) * prior;
        if(s > bestScore){
            bestScore = s;
            bestLag = lag;
        }
    }

    double lagF = static_cast<double>(bestLag);
    if(bestLag > lagMin && bestLag < lagMax){
        const double y0 = ac[static_cast<size_t>(bestLag) - 1];
        const double y1 = ac[static_cast<size_t>(bestLag)];
        const double y2 = ac[static_cast<size_t>(bestLag) + 1];
        const double denom = y0 - 2.0 * y1 + y2;
        if(std::abs(denom) > 1.0e-12) lagF += 0.5 * (y0 - y2) / denom;
    }
    return 60.0 * e.rate / lagF;
}

// ちょうど 2 倍・半分の関係なら、基準のほうに寄せる（区間ごとに倍テンがばらつかないように）
inline double foldToOctave(double b, double ref){
    if(std::abs(b / (2.0 * ref) - 1.0) < 0.04) return b / 2.0;
    if(std::abs(b / (0.5 * ref) - 1.0) < 0.04) return b * 2.0;
    return b;
}

// anchor を拍の 1 つとしたとき、[lo, hi) に入る拍の位置のオンセット強度の平均
inline double combScore(const Env& e, double anchor, double period, double lo, double hi){
    double p = anchor + std::ceil((lo - anchor) / period) * period;
    double s = 0.0;
    int cnt = 0;
    for(; p < hi - 1.0; p += period){
        s += at(e, p);
        cnt++;
    }
    return (cnt > 0) ? s / cnt : 0.0;
}

struct Fit{
    double bpm = 0.0;
    double anchor = 0.0;   // 拍の位置の 1 つ（サンプル単位・絶対位置）
    double score = -1.0;   // 一番よい位相での、拍の位置の平均オンセット強度
    double mean = 0.0;     // すべての位相の平均（はっきりさの計算用）
    double clarity() const { return (score > 1.0e-12) ? std::clamp(1.0 - mean / score, 0.0, 1.0) : 0.0; }
};

// BPM を決めて、[lo, hi) で一番よく拍に乗る位相を探す
//   fine = true なら、見つけた位相のまわりを 0.1 サンプル（0.25ms）刻みで詰める
inline Fit fitFree(const Env& e, double bpm, double lo, double hi, bool fine){
    Fit r;
    r.bpm = bpm;
    const double P = e.rate * 60.0 / bpm;
    const int phases = static_cast<int>(std::ceil(P));
    double sum = 0.0;
    for(int ph = 0; ph < phases; ph++){
        const double s = combScore(e, lo + ph, P, lo, hi);
        sum += s;
        if(s > r.score){
            r.score = s;
            r.anchor = lo + ph;
        }
    }
    r.mean = sum / phases;

    if(fine){
        const double c = r.anchor;
        for(double d = -1.0; d <= 1.0001; d += 0.1){
            const double s = combScore(e, c + d, P, lo, hi);
            if(s > r.score){
                r.score = s;
                r.anchor = c + d;
            }
        }
    }
    return r;
}

// 中心の BPM のまわりで、位相も一緒に一番よい BPM を探す（0.1 → 0.01 → 0.002 刻み）
inline Fit refineFree(const Env& e, double center, double lo, double hi, double relRange, bool fine){
    Fit best;
    for(double b = center * (1.0 - relRange); b <= center * (1.0 + relRange); b += 0.1){
        const Fit r = fitFree(e, b, lo, hi, false);
        if(r.score > best.score) best = r;
    }
    const double c2 = best.bpm;
    for(double b = c2 - 0.12; b <= c2 + 0.12; b += 0.01){
        const Fit r = fitFree(e, b, lo, hi, false);
        if(r.score > best.score) best = r;
    }
    if(fine){
        const double c3 = best.bpm;
        best = fitFree(e, c3, lo, hi, true);
        for(double b = c3 - 0.012; b <= c3 + 0.012; b += 0.002){
            const Fit r = fitFree(e, b, lo, hi, true);
            if(r.score > best.score) best = r;
        }
    }
    return best;
}

// 拍の 1 つを anchor に固定して、[lo, hi) で一番よく拍に乗る BPM を探す（0.05 → 0.01 → 0.002 刻み）
inline double anchoredScore(const Env& e, double bpm, double anchor, double lo, double hi){
    return combScore(e, anchor, e.rate * 60.0 / bpm, std::max(anchor, lo), hi);
}

inline double refineAnchored(const Env& e, double center, double anchor, double lo, double hi, double relRange){
    double bestB = center;
    double bestS = -1.0;
    auto tryB = [&](double b){
        const double s = anchoredScore(e, b, anchor, lo, hi);
        if(s > bestS){ bestS = s; bestB = b; }
    };
    for(double b = center * (1.0 - relRange); b <= center * (1.0 + relRange); b += 0.05) tryB(b);
    const double c2 = bestB;
    for(double b = c2 - 0.06; b <= c2 + 0.06; b += 0.01) tryB(b);
    const double c3 = bestB;
    for(double b = c3 - 0.012; b <= c3 + 0.012; b += 0.002) tryB(b);
    return bestB;
}

// 整数に近く、整数にしても拍の合い方がほとんど落ちないなら整数に。そうでなければ小数第 2 位まで
template<class ScoreFn>
inline double tidyBpm(double b, ScoreFn score){
    const double r = std::round(b);
    if(std::abs(b - r) <= kRoundRange && score(r) >= kRoundKeep * score(b)) return r;
    return std::round(b * 100.0) / 100.0;
}

} // namespace music_detail


namespace music_detail{

// 今のグリッド（anchor から period ごとの線）を、±半拍の範囲でずらしたとき一番よく合うずらし量と、そのときの合い方
struct ShiftFit{ double shift = 0.0; double score = 0.0; };
inline ShiftFit bestShift(const Env& e, double anchor, double period, double lo, double hi){
    ShiftFit best;
    best.score = combScore(e, anchor, period, lo, hi);
    const int half = static_cast<int>(period * 0.5);
    for(int d = -half; d <= half; d++){
        const double sc = combScore(e, anchor + d, period, lo, hi);
        if(sc > best.score){ best.score = sc; best.shift = d; }
    }
    const double c = best.shift;
    for(double d = c - 1.0; d <= c + 1.0001; d += 0.1){
        const double sc = combScore(e, anchor + d, period, lo, hi);
        if(sc > best.score){ best.score = sc; best.shift = d; }
    }
    return best;
}

// bestShift の、ずらす量を ±maxShift サンプルまでに限った版
inline double bestShiftLimited(const Env& e, double anchor, double period, double lo, double hi, double maxShift){
    double best = combScore(e, anchor, period, lo, hi);
    for(double d = -maxShift; d <= maxShift + 1.0e-9; d += 0.5){
        best = std::max(best, combScore(e, anchor + d, period, lo, hi));
    }
    return best;
}

// 範囲の平均の強さ（音が詰まった曲だと、どんなグリッドでもそこそこ合ってしまうので、その「底上げ」分）
inline double regionMean(const Env& e, double lo, double hi){
    const size_t a = static_cast<size_t>(std::max(0.0, lo));
    const size_t b = std::min(e.v.size(), static_cast<size_t>(std::max(0.0, hi)));
    if(b <= a) return 0.0;
    double s = 0.0;
    for(size_t i = a; i < b; i++) s += e.v[i];
    return s / static_cast<double>(b - a);
}

} // namespace music_detail

// --------------------------------------------------------------------
//  オンセット強度から、オフセット・BPM・BPM の変化を求める
// --------------------------------------------------------------------
inline MusicAnalysis analyzeEnvelope(const music_detail::Env& e){
    using namespace music_detail;
    MusicAnalysis res;

    const double rate = e.rate;
    const double nF = e.size();
    res.lengthSec = nF / rate;
    const Env e2 = halve(e);

    // ----- 1. 曲全体の大まかなテンポ（倍テン・半テンをそろえる基準と、「拍が鳴っている」の基準に使う） -----
    const double gCoarse = coarseBpm(e2, 0, e2.v.size(), 140.0, 1.0);
    if(gCoarse <= 0.0){
        res.error = "曲が短すぎます";
        return res;
    }
    const Fit gFit = refineFree(e, gCoarse, 0.0, nF, 0.025, false);
    const double G = gFit.bpm;

    double globalMean = 0.0;
    for(double v : e.v) globalMean += v;
    globalMean /= nF;

    double beatRef = 0.0;
    {
        std::vector<double> gv;
        const double Pg = rate * 60.0 / G;
        for(double p = gFit.anchor; p < nF - 1.0; p += Pg) gv.push_back(at(e, p));
        if(!gv.empty()){
            std::sort(gv.begin(), gv.end());
            beatRef = gv[static_cast<size_t>(static_cast<double>(gv.size()) * 0.7)];
        }
    }

    // ----- 2. 窓ごとの「その窓だけで一番合うテンポ」（比べる相手として使う） -----
    const size_t W = static_cast<size_t>(std::llround(rate * kWinSec));
    const size_t H = static_cast<size_t>(std::llround(rate * kHopSec));

    struct Win{
        double i0 = 0, i1 = 0;
        bool valid = false;
        double altBpm = 0.0;
        double altScore = 0.0;
    };
    std::vector<Win> wins;
    for(size_t i0 = 0; i0 + W <= e.v.size(); i0 += H){
        Win w;
        w.i0 = static_cast<double>(i0);
        w.i1 = static_cast<double>(i0 + W);

        double winMean = 0.0;
        for(size_t i = i0; i < i0 + W; i++) winMean += e.v[i];
        winMean /= static_cast<double>(W);

        double cb = coarseBpm(e2, i0 / 2, (i0 + W) / 2, G, 0.8);
        if(cb > 0.0 && winMean >= 0.15 * globalMean){
            cb = foldToOctave(cb, G);
            const Fit f = refineFree(e, cb, w.i0, w.i1, 0.025, false);
            w.altBpm = f.bpm;
            w.altScore = f.score;

            // 窓の中の拍の位置のうち、実際に音がある割合（ブレイクなど拍が薄い窓は判定に使わない）
            double coverage = 1.0;
            if(beatRef > 1.0e-12){
                const double P = rate * 60.0 / f.bpm;
                int total = 0, hit = 0;
                for(double p = f.anchor; p < w.i1 - 1.0; p += P){
                    total++;
                    if(at(e, p) >= 0.4 * beatRef) hit++;
                }
                coverage = (total > 0) ? static_cast<double>(hit) / total : 0.0;
            }
            w.valid = (f.clarity() >= 0.30) && (coverage >= 0.70);
        }
        wins.push_back(w);
    }

    // ----- 3. 最初のテンポとオフセット -----
    double initLo = 0.0;
    for(const auto& w : wins){ if(w.valid){ initLo = w.i0; break; } }
    const double initHi = std::min(nF, initLo + rate * kInitSec);

    struct Section{ double A, B, lo; };   // 区間：「拍の 1 つ目」「BPM」「区間の始まり」
    std::vector<Section> sections;
    double initLoUsed = initLo;
    double curA, curB, curLo;   // 今の区間の「拍の 1 つ目」「BPM」「区間の始まり」
    {
        double cb = coarseBpm(e2, static_cast<size_t>(initLo / 2), static_cast<size_t>(initHi / 2), G, 0.8);
        cb = (cb > 0.0) ? foldToOctave(cb, G) : G;
        const Fit f = refineFree(e, cb, initLo, initHi, 0.02, true);
        const double b = tidyBpm(f.bpm, [&](double bb){ return fitFree(e, bb, initLo, initHi, true).score; });
        const Fit fb = fitFree(e, b, initLo, initHi, true);

        // 音源の先頭（0）に一番近い拍
        const double P = rate * 60.0 / b;
        curA = fb.anchor - std::round(fb.anchor / P) * P;
        curB = b;
        curLo = 0.0;
        res.clarity = fb.clarity();
    }

    // 区間が決まったとき：区間全体（[curLo, end)）で BPM を測り直す
    auto finalizeCur = [&](double end){
        if(end - std::max(curA, curLo) < rate * 4.0) return;   // 短すぎる区間は測り直さない
        double b = refineAnchored(e, curB, curA, curLo, end, 0.005);
        b = tidyBpm(b, [&](double bb){ return anchoredScore(e, bb, curA, curLo, end); });
        curB = b;
    };

    // ----- 4. 今のグリッドが曲の拍に乗り続けているかを、窓ごとに確かめる -----
    int badRun = 0;
    size_t badStart = 0;
    for(size_t k = 0; k < wins.size(); k++){
        const Win& w = wins[k];
        if(w.i0 < curLo + rate * 2.0) continue;   // 区間の頭の 2 秒はまたいで判定しない
        if(!w.valid) continue;                    // 拍が薄い窓は数えない（続けて数えるのも止めない）

        const double Pc = rate * 60.0 / curB;
        const double sCur = combScore(e, curA, Pc, w.i0, w.i1);
        const ShiftFit sf = bestShift(e, curA, Pc, w.i0, w.i1);
        const double driftMs = std::abs(sf.shift) / rate * 1000.0;

        const bool otherTempo = std::abs(w.altBpm - curB) >= kIgnoreChange && w.altScore > kAltBetter * sCur;
        const bool drifting   = driftMs > kDriftMs && sf.score > kAltBetter * sCur;

        if(otherTempo || drifting){
            if(badRun == 0) badStart = k;
            badRun++;
        }
        else{
            badRun = 0;
        }
        if(badRun < kConfirmWins) continue;
        badRun = 0;

        // ----- 5. 変化の候補：変化点と新しい BPM を求めて、本当に変わったか確かめる -----
        const Win& wb = wins[badStart];
        const double lo = std::max(curA + Pc * 0.5, std::max(curLo, wb.i0));
        const double hi = std::min(nF - 2.0, wins[k].i0 + W * 0.5);
        if(hi <= lo) continue;

        // 新しいテンポの見当：合わなかった窓の範囲で測る
        double nbRough;
        {
            const double r0 = wins[badStart].i0 + W * 0.5, r1 = std::min(nF, wins[k].i1);
            double cb = coarseBpm(e2, static_cast<size_t>(r0 / 2), static_cast<size_t>(r1 / 2), curB, 0.8);
            cb = (cb > 0.0) ? foldToOctave(cb, G) : curB;
            nbRough = refineFree(e, cb, r0, r1, 0.025, false).bpm;
        }

        // 前のテンポの拍の位置は、候補の直前（約 6 秒）で BPM と位相を合わせ直したものを使う
        const double fitLo = std::max(curA, std::max(curLo, lo - rate * 6.0));
        const double fitHi = std::max(fitLo + Pc * 4.0, lo + rate * 0.5);
        const Fit local = refineFree(e, curB, fitLo, fitHi, 0.01, true);
        const double oldA = local.anchor;
        const double PL = rate * 60.0 / local.bpm;
        const double PR = rate * 60.0 / nbRough;

        const double zs = std::max(curA, lo - rate * 2.0);
        const double ze = std::min(nF - 2.0, hi + rate * 2.0);
        double mu = 0.0;
        int muN = 0;
        for(double i = zs; i <= ze; i += 1.0){ mu += at(e, i); muN++; }
        mu = (muN > 0) ? mu / muN : 0.0;

        // 候補 = 前のテンポの拍。「そこより前は前のテンポ、そこからは新しいテンポ」で拍の合い方を合計する
        struct Cand{ double t, J, raw; };
        std::vector<Cand> cand;
        for(double t = oldA + std::ceil((lo - oldA) / PL) * PL; t <= hi; t += PL){
            double J = 0.0;
            for(double p = oldA + std::ceil((zs - oldA) / PL) * PL; p < t - 0.5; p += PL) J += at(e, p) - mu;
            for(double p = t; p <= ze; p += PR) J += at(e, p) - mu;
            cand.push_back({t, J, at(e, t)});
        }
        if(cand.empty()) continue;

        double boundary = cand[0].t;
        {
            double maxJ = cand[0].J;
            for(const auto& c : cand) maxJ = std::max(maxJ, c.J);
            const double tol = 0.03 * std::max(std::abs(maxJ), 1.0e-9);
            bool found = false;
            for(const auto& c : cand){
                if(c.J >= maxJ - tol && c.raw >= 0.4 * beatRef){ boundary = c.t; found = true; break; }
            }
            if(!found){
                for(const auto& c : cand){
                    if(c.J >= maxJ - tol){ boundary = c.t; break; }
                }
            }
        }

        // 変化点を拍の 1 つ目に固定して新しい BPM を測る（変化点の位置も ±2.5ms で詰める）
        const double vEnd = std::min(nF, boundary + rate * kVerifySec);
        double nb = refineAnchored(e, nbRough, boundary, boundary, vEnd, 0.02);
        for(int iter = 0; iter < 2; iter++){
            const double c = boundary;
            double bestS = anchoredScore(e, nb, c, c, vEnd);
            for(double d = -1.0; d <= 1.0001; d += 0.1){
                const double sc = anchoredScore(e, nb, c + d, c + d, vEnd);
                if(sc > bestS){ bestS = sc; boundary = c + d; }
            }
            nb = refineAnchored(e, nb, boundary, boundary, vEnd, 0.002);
        }
        nb = tidyBpm(nb, [&](double bb){ return anchoredScore(e, bb, boundary, boundary, vEnd); });

        // 確かめ：変化点のあと 16 秒で、新しいテンポのほうが「前のテンポのまま（位相を合わせ直しても）」よりはっきり合うか
        //   比べるのは「拍の位置の強さ − 範囲の平均の強さ」（音が詰まった曲で、どのテンポもよく見えてしまうのを防ぐ）
        const double vMean = regionMean(e, boundary, vEnd);
        const double sNew = anchoredScore(e, nb, boundary, boundary, vEnd) - vMean;
        const double sOld = std::max(combScore(e, oldA, PL, boundary, vEnd),
                                     bestShift(e, oldA, PL, boundary, vEnd).score) - vMean;
        if(std::abs(nb - curB) < kIgnoreChange || sNew <= 0.0 || sNew < kAccept * std::max(sOld, 0.0)){
            // 揺れだった：変化は置かない。
            //   ただし拍の線がずれてきたせいで候補になった場合に備えて、ここまでの範囲で今の BPM を測り直しておく
            //   （最初の 20 秒で測った BPM のわずかな誤差が、曲の後半でたまっていくのを防ぐ）
            const double upTo = std::min(nF, wins[k].i1);
            double b = refineAnchored(e, curB, curA, curLo, upTo, 0.005);
            b = tidyBpm(b, [&](double bb){ return anchoredScore(e, bb, curA, curLo, upTo); });
            curB = b;
            continue;
        }

        // 変化の候補として区間を区切る：前の区間を測り直してから、新しい区間を始める
        finalizeCur(boundary);
        sections.push_back({curA, curB, curLo});
        curA = boundary;
        curB = nb;
        curLo = boundary;
    }
    finalizeCur(nF);
    sections.push_back({curA, curB, curLo});

    // ----- 7. 見直し：区間全体で比べて、はっきりよく合わない変化を消す -----
    // 区間 [lo, hi) の中に収まる、拍がはっきりした窓の BPM の中央値（3 つ未満なら 0 = 判断しない）
    auto windowMedian = [&](double lo, double hi){
        std::vector<double> v;
        for(const auto& w : wins){
            if(w.valid && w.i0 >= lo && w.i1 <= hi) v.push_back(w.altBpm);
        }
        if(v.size() < 3) return 0.0;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    auto sectionEnd = [&](size_t i){ return (i + 1 < sections.size()) ? sections[i + 1].lo : nF; };
    auto remeasure = [&](size_t i){
        Section& sc = sections[i];
        const double end = sectionEnd(i);
        if(end - std::max(sc.A, sc.lo) < rate * 4.0) return;
        double b = refineAnchored(e, sc.B, sc.A, sc.lo, end, 0.01);
        b = tidyBpm(b, [&](double bb){ return anchoredScore(e, bb, sc.A, sc.lo, end); });
        sc.B = b;
    };
    for(bool removed = true; removed && sections.size() > 1; ){
        removed = false;
        for(size_t i = 1; i < sections.size(); i++){
            const Section& prev = sections[i - 1];
            const Section& cur  = sections[i];
            const double end = sectionEnd(i);
            //   前のテンポは「変化を置かずに続けたグリッド」。前の BPM のわずかな誤差のぶん（±30ms）だけずらしてよい
            const double mean = regionMean(e, cur.lo, end);
            const double sNew = anchoredScore(e, cur.B, cur.A, cur.lo, end) - mean;
            const double sOld = bestShiftLimited(e, prev.A, rate * 60.0 / prev.B, cur.lo, end,
                                                 kKeepShiftMs / 1000.0 * rate) - mean;
            const double med = windowMedian(cur.lo, end);
            //   窓が 3 つ入らない短い区間（約 10 秒未満）は確かめられないので、変化として置かない
            const bool votesForOld = (med <= 0.0) || (std::abs(med - prev.B) <= std::abs(med - cur.B));
            if(std::abs(cur.B - prev.B) < kIgnoreChange || sNew <= 0.0 || sNew < kKeepLong * std::max(sOld, 0.0) || votesForOld){
                sections.erase(sections.begin() + static_cast<long>(i));
                remeasure(i - 1);
                removed = true;
                break;
            }
        }
    }

    // ----- 最初の区間：BPM と位相を区間全体で詰め直して、オフセットを決める -----
    {
        Section& s0 = sections[0];
        const double end = sectionEnd(0);
        const Fit f = refineFree(e, s0.B, initLoUsed, end, 0.002, true);
        const double b = tidyBpm(f.bpm, [&](double bb){ return fitFree(e, bb, initLoUsed, end, true).score; });
        const Fit fb = fitFree(e, b, initLoUsed, end, true);
        const double P = rate * 60.0 / b;
        s0.A = fb.anchor - std::round(fb.anchor / P) * P;   // 音源の先頭に一番近い拍
        s0.B = b;
        res.clarity = fb.clarity();
    }

    res.offsetMs = sections[0].A / rate * 1000.0;
    res.bpm = sections[0].B;
    for(size_t i = 1; i < sections.size(); i++){
        MusicBpmChange ch;
        ch.audioMs = sections[i].A / rate * 1000.0;
        ch.chartMs = ch.audioMs - res.offsetMs;
        ch.bpm = sections[i].B;
        res.changes.push_back(ch);
    }
    res.ok = true;
    return res;
}

// 音源ファイルを解析する（曲全体を読むので、長い曲だと数秒かかる）
inline MusicAnalysis analyzeMusic(const std::string& path){
    MusicAnalysis res;
    music_detail::Env e;
    if(!music_detail::buildEnvFromFile(path, e, res.error)) return res;
    return analyzeEnvelope(e);
}