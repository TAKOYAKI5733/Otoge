#pragma once

// ====================================================================
//  bpmDetect.h — 音源ファイルからBPM / テンポ変化を推定する
//
//  共通の下ごしらえ:
//   1. Mix_LoadWAV でPCMにデコード(mp3/ogg/wav など SDL_mixer が読める形式)
//   2. モノラル化 + 約11kHzへ間引き
//   3. 3帯域(低/中/高)のエネルギー増加量を足して「オンセット強度」を作る(約200Hz)
//
//  detectBpmFromFile      : 曲全体で一定テンポと仮定して1つのBPMを返す
//  detectTempoMapFromFile : 8秒窓でテンポを測り、変化点(ms)とBPMの一覧を返す
//
//  どちらも「自己相関で大まかなテンポ → 拍位置のくし形照合で細かく詰める」方式。
//  倍テン/半テン(202 と 101 など)は原理的に区別できないため、呼び出し側で選ばせる。
// ====================================================================

#include "GameCommon.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

struct BpmAnalysisResult{
    bool ok = false;
    std::string error;
    double bpm = 0.0;          // 推定BPM
    double beatPhaseMs = 0.0;  // 0ms以降で最初に拍が来る位置の目安(±数十ms)
    double confidence = 0.0;   // 0.0〜1.0 の目安(拍位置の鮮明さ)
};

struct BpmSegment{
    double startMs = 0.0;      // このテンポが始まる位置(最初の区間は0)
    double bpm = 0.0;
    double clarity = 0.0;      // 0.0〜1.0 の目安
};

struct TempoMapResult{
    bool ok = false;
    std::string error;
    std::vector<BpmSegment> segments;  // 時刻順。segments[0] が曲頭のテンポ
};

namespace bpmdetect_detail{

struct Envelope{
    std::vector<double> env;
    double rate = 0.0;         // env の 1 秒あたりのサンプル数(約200)
};

// 共通の下ごしらえ(デコード → オンセット強度)
inline bool buildEnvelope(const std::string& path, Envelope& out, std::string& err){
    const double kPi = 3.14159265358979323846;

    Mix_Chunk* chunk = Mix_LoadWAV(path.c_str());
    if(!chunk){
        err = std::string("load failed: ") + Mix_GetError();
        return false;
    }

    int freq = 0;
    Uint16 fmt = 0;
    int ch = 0;
    if(Mix_QuerySpec(&freq, &fmt, &ch) == 0 || fmt != AUDIO_S16SYS || ch < 1 || freq < 8000){
        Mix_FreeChunk(chunk);
        err = "unsupported audio format (16bit integer only)";
        return false;
    }

    const int16_t* pcm = reinterpret_cast<const int16_t*>(chunk->abuf);
    const size_t totalFrames = chunk->alen / (sizeof(int16_t) * static_cast<size_t>(ch));

    // モノラル化 + 間引き
    const size_t dec = std::max<size_t>(1, static_cast<size_t>(freq) / 11025);
    const double fs = static_cast<double>(freq) / static_cast<double>(dec);
    const size_t maxFrames = std::min(totalFrames, static_cast<size_t>(freq) * 240); // 先頭240秒まで
    const size_t n2 = maxFrames / dec;

    if(static_cast<double>(n2) < fs * 10.0){
        Mix_FreeChunk(chunk);
        err = "audio is too short (need 10s or more)";
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
    Mix_FreeChunk(chunk);

    // 3帯域のエネルギー変化
    const size_t hop = std::max<size_t>(1, static_cast<size_t>(std::llround(fs / 200.0)));
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

    // 局所平均(±0.5秒)を引いて正の部分だけ残し、5点で軽く平滑化
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
        err = "no rhythmic content found (silent?)";
        return false;
    }

    out.env = std::move(env);
    out.rate = envRate;
    return true;
}

// env を小数位置で線形補間して読む
inline double sampleEnv(const std::vector<double>& env, double pos){
    const long i = static_cast<long>(pos);
    if(pos < 0.0 || i + 1 >= static_cast<long>(env.size())) return 0.0;
    const double fr = pos - static_cast<double>(i);
    return env[static_cast<size_t>(i)] * (1.0 - fr) + env[static_cast<size_t>(i) + 1] * fr;
}

struct Fit{
    double bpm = 0.0;
    double anchor = 0.0;   // 範囲内で拍が来る位置の1つ(envサンプル単位・絶対位置)
    double score = -1.0;   // 最良位相での「1拍あたりの平均オンセット強度」
    double mean = 0.0;     // 全位相の平均(鮮明さの計算用)
    double clarity() const { return (score > 1.0e-12) ? std::clamp(1.0 - mean / score, 0.0, 1.0) : 0.0; }
};

// [i0,i1) の範囲で、指定BPMのくし形を当てたときの最良位相
inline Fit combFit(const Envelope& e, double bpm, size_t i0, size_t i1){
    Fit r;
    r.bpm = bpm;
    const double P = e.rate * 60.0 / bpm;
    const int phases = static_cast<int>(std::ceil(P));
    double sumScore = 0.0;
    for(int ph = 0; ph < phases; ph++){
        double s = 0.0;
        int cnt = 0;
        for(double pos = static_cast<double>(i0) + ph; pos < static_cast<double>(i1) - 1.0; pos += P){
            s += sampleEnv(e.env, pos);
            cnt++;
        }
        if(cnt > 0) s /= cnt;
        sumScore += s;
        if(s > r.score){
            r.score = s;
            r.anchor = static_cast<double>(i0) + ph;
        }
    }
    r.mean = sumScore / phases;
    return r;
}

// 中心BPMの周辺を、粗い刻み(0.1)→細かい刻み(0.01)の順で探索
inline Fit refineAround(const Envelope& e, double center, size_t i0, size_t i1, double relRange){
    Fit best;
    for(double b = center * (1.0 - relRange); b <= center * (1.0 + relRange); b += 0.1){
        const Fit r = combFit(e, b, i0, i1);
        if(r.score > best.score) best = r;
    }
    const double c2 = best.bpm;
    for(double b = c2 - 0.15; b <= c2 + 0.15; b += 0.01){
        const Fit r = combFit(e, b, i0, i1);
        if(r.score > best.score) best = r;
    }
    return best;
}

// [i0,i1) の自己相関から大まかなBPMを返す(60〜240BPM)。
// priorCenter>0 のときは、その付近を優先する弱い事前分布(対数正規)を掛ける。
inline double coarseBpm(const Envelope& e, size_t i0, size_t i1, double priorCenter, double priorSigmaOct){
    const int lagMin = std::max(1, static_cast<int>(std::floor(e.rate * 60.0 / 240.0)));
    const int lagMax = static_cast<int>(std::ceil(e.rate * 60.0 / 60.0));
    const int lagLimit = lagMax * 2 + 2;

    const size_t n = i1 - i0;
    if(n <= static_cast<size_t>(lagLimit) * 2) return 0.0;

    std::vector<double> ac(static_cast<size_t>(lagLimit) + 1, 0.0);
    for(int lag = lagMin; lag <= lagLimit; lag++){
        const size_t cnt = n - static_cast<size_t>(lag);
        double s = 0.0;
        for(size_t i = 0; i < cnt; i++) s += e.env[i0 + i] * e.env[i0 + i + static_cast<size_t>(lag)];
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

// ちょうど2倍/半分の関係なら基準側のオクターブに寄せる
inline double foldToOctave(double b, double ref){
    if(std::abs(b / (2.0 * ref) - 1.0) < 0.04) return b / 2.0;
    if(std::abs(b / (0.5 * ref) - 1.0) < 0.04) return b * 2.0;
    return b;
}

} // namespace bpmdetect_detail


// --------------------------------------------------------------------
//  曲全体で一定テンポと仮定して1つのBPMを返す
// --------------------------------------------------------------------
inline BpmAnalysisResult detectBpmFromFile(const std::string& path){
    using namespace bpmdetect_detail;
    BpmAnalysisResult res;

    Envelope e;
    if(!buildEnvelope(path, e, res.error)) return res;

    const size_t nF = e.env.size();
    const double coarse = coarseBpm(e, 0, nF, 140.0, 1.0);   // 140BPM付近をやや優先(σ=1オクターブ)
    if(coarse <= 0.0){
        res.error = "audio is too short";
        return res;
    }
    const Fit f = refineAround(e, coarse, 0, nF, 0.025);

    res.ok = true;
    res.bpm = f.bpm;
    res.beatPhaseMs = (f.anchor + 0.5) / e.rate * 1000.0;
    res.confidence = f.clarity();
    return res;
}


// --------------------------------------------------------------------
//  途中のテンポ変化を検出する(曲全体が一定でなくてもよい)
//
//   1. 全体推定で基準のテンポ(オクターブ)を決める
//   2. 8秒窓を2秒ずつずらしてテンポを測る
//   3. 「別のテンポが2窓連続」で変化とみなし、区間に分ける
//   4. 各区間を区間内部だけで測り直す
//   5. 隣り合う区間の境界を、両側の拍の並びが最もよく合う位置に詰める
// --------------------------------------------------------------------
inline TempoMapResult detectTempoMapFromFile(const std::string& path){
    using namespace bpmdetect_detail;
    TempoMapResult res;

    Envelope e;
    if(!buildEnvelope(path, e, res.error)) return res;

    const size_t nF = e.env.size();
    const double rate = e.rate;

    // 1. 全体推定
    const double gCoarse = coarseBpm(e, 0, nF, 140.0, 1.0);
    if(gCoarse <= 0.0){
        res.error = "audio is too short";
        return res;
    }
    const Fit gFit = refineAround(e, gCoarse, 0, nF, 0.025);
    const double G = gFit.bpm;

    double globalMean = 0.0;
    for(double v : e.env) globalMean += v;
    globalMean /= static_cast<double>(nF);

    // 「拍が実際に鳴っている」かの基準: 全体の拍位置でのオンセット強度の70パーセンタイル
    double beatRef = 0.0;
    {
        std::vector<double> gv;
        const double Pg = rate * 60.0 / G;
        for(double p = gFit.anchor; p < static_cast<double>(nF) - 1.0; p += Pg) gv.push_back(sampleEnv(e.env, p));
        if(!gv.empty()){
            std::sort(gv.begin(), gv.end());
            beatRef = gv[static_cast<size_t>(static_cast<double>(gv.size()) * 0.7)];
        }
    }

    // 2. 窓ごとのテンポ
    const size_t W = static_cast<size_t>(std::llround(rate * 8.0));
    const size_t H = static_cast<size_t>(std::llround(rate * 2.0));

    struct Win{
        size_t i0 = 0, i1 = 0;
        bool valid = false;
        double bpm = 0.0;
        double clarity = 0.0;
    };
    std::vector<Win> wins;
    const double kMinClarity = 0.30;   // これ未満の窓(拍が薄い箇所)は判定に使わない
    const double kMinCoverage = 0.70;  // 拍位置にオンセットがある割合がこれ未満の窓は判定に使わない

    for(size_t i0 = 0; i0 + W <= nF; i0 += H){
        Win w;
        w.i0 = i0;
        w.i1 = i0 + W;

        double winMean = 0.0;
        for(size_t i = w.i0; i < w.i1; i++) winMean += e.env[i];
        winMean /= static_cast<double>(W);

        double cb = coarseBpm(e, w.i0, w.i1, G, 0.8);
        if(cb > 0.0 && winMean >= 0.15 * globalMean){
            cb = foldToOctave(cb, G);
            const Fit f = refineAround(e, cb, w.i0, w.i1, 0.025);
            w.bpm = f.bpm;
            w.clarity = f.clarity();

            // 窓内の拍位置のうち、実際にオンセットがある割合(ブレイクや別テンポにまたがる窓を除外する)
            double coverage = 1.0;
            if(beatRef > 1.0e-12){
                const double P = rate * 60.0 / f.bpm;
                int total = 0, hit = 0;
                for(double p = f.anchor; p < static_cast<double>(w.i1) - 1.0; p += P){
                    total++;
                    if(sampleEnv(e.env, p) >= 0.4 * beatRef) hit++;
                }
                coverage = (total > 0) ? static_cast<double>(hit) / total : 0.0;
            }
            w.valid = (w.clarity >= kMinClarity) && (coverage >= kMinCoverage);
        }
        wins.push_back(w);
    }

    // 3. 区間に分ける
    struct Seg{
        size_t firstWin = 0, lastWin = 0;
        double sumBpm = 0.0;
        int cnt = 0;
        double bpm = 0.0;
        double anchor = 0.0;
        double clarity = 0.0;
        double boundaryEnv = 0.0;   // この区間の開始位置(envサンプル単位)
    };
    std::vector<Seg> segs;

    const double kSame = 0.012;   // 1.2%以内なら同じテンポ
    auto same = [&](double a, double b){ return std::abs(a / b - 1.0) < kSame; };

    int pendingFirst = -1;
    int pendingCnt = 0;
    double pendingBpm = 0.0;

    for(size_t k = 0; k < wins.size(); k++){
        if(!wins[k].valid) continue;

        if(segs.empty()){
            Seg s;
            s.firstWin = s.lastWin = k;
            s.sumBpm = wins[k].bpm;
            s.cnt = 1;
            segs.push_back(s);
            continue;
        }

        Seg& cur = segs.back();
        const double curBpm = cur.sumBpm / cur.cnt;

        if(same(wins[k].bpm, curBpm)){
            cur.lastWin = k;
            cur.sumBpm += wins[k].bpm;
            cur.cnt++;
            pendingFirst = -1;
            pendingCnt = 0;
        }
        else{
            if(pendingFirst >= 0 && same(wins[k].bpm, pendingBpm)){
                pendingBpm = (pendingBpm * pendingCnt + wins[k].bpm) / (pendingCnt + 1);
                pendingCnt++;
            }
            else{
                pendingFirst = static_cast<int>(k);
                pendingCnt = 1;
                pendingBpm = wins[k].bpm;
            }

            if(pendingCnt >= 2){   // 2窓連続で別テンポ → 確定
                Seg s;
                s.firstWin = static_cast<size_t>(pendingFirst);
                s.lastWin = k;
                s.sumBpm = pendingBpm * pendingCnt;
                s.cnt = pendingCnt;
                segs.push_back(s);
                pendingFirst = -1;
                pendingCnt = 0;
            }
        }
    }

    if(segs.empty()){
        res.error = "could not find a stable tempo (beats too weak?)";
        return res;
    }

    // 4. 各区間を、区間内部だけで測り直す
    for(size_t s = 0; s < segs.size(); s++){
        Seg& sg = segs[s];
        const size_t ts = (s == 0) ? 0 : wins[sg.firstWin].i0 + W / 4;
        const size_t te = (s + 1 == segs.size()) ? nF : wins[sg.lastWin].i1 - W / 4;
        const double mean = sg.sumBpm / sg.cnt;

        if(te > ts + static_cast<size_t>(rate * 3.0)){
            const Fit f = refineAround(e, mean, ts, te, 0.02);
            sg.bpm = f.bpm;
            sg.anchor = f.anchor;
            sg.clarity = f.clarity();
        }
        else{
            sg.bpm = mean;
            sg.anchor = static_cast<double>(wins[sg.firstWin].i0);
            sg.clarity = wins[sg.firstWin].clarity;
        }
    }

    // 5. 境界を詰める
    segs[0].boundaryEnv = 0.0;
    for(size_t s = 1; s < segs.size(); s++){
        const Seg& L = segs[s - 1];
        Seg& R = segs[s];

        const double cLast  = static_cast<double>(wins[L.lastWin].i0 + W / 2);   // 旧テンポと判定された最後の窓の中心
        const double cFirst = static_cast<double>(wins[R.firstWin].i0 + W / 2);  // 新テンポと判定された最初の窓の中心
        const double lo = std::max(0.0, cLast - rate * 1.0);
        const double hi = cFirst + rate * 1.0;
        const double zs = std::max(0.0, lo - rate * 2.0);
        const double ze = std::min(static_cast<double>(nF - 2), hi + rate * 2.0);

        double mu = 0.0;
        int muN = 0;
        for(size_t i = static_cast<size_t>(zs); i <= static_cast<size_t>(ze); i++){ mu += e.env[i]; muN++; }
        mu = (muN > 0) ? mu / muN : 0.0;

        const double PL = rate * 60.0 / L.bpm;
        const double PR = rate * 60.0 / R.bpm;

        // 各テンポの拍位置(区間内の測り直しで得た位相に合わせて延長)
        std::vector<double> lb, rb;
        {
            double p = L.anchor + std::ceil((zs - L.anchor) / PL) * PL;
            for(; p <= ze; p += PL) lb.push_back(p);
            p = R.anchor + std::ceil((zs - R.anchor) / PR) * PR;
            for(; p <= ze; p += PR) rb.push_back(p);
        }

        std::vector<double> vl(lb.size()), vr(rb.size());
        for(size_t i = 0; i < lb.size(); i++) vl[i] = sampleEnv(e.env, lb[i]) - mu;
        for(size_t i = 0; i < rb.size(); i++) vr[i] = sampleEnv(e.env, rb[i]) - mu;

        // 候補は「新テンポの拍位置」。t より前は旧テンポの拍、t 以降は新テンポの拍として合計
        struct Cand{ double t, J, raw; };
        std::vector<Cand> cand;
        for(size_t j = 0; j < rb.size(); j++){
            const double t = rb[j];
            if(t < lo || t > hi) continue;
            double J = 0.0;
            for(size_t i = 0; i < lb.size(); i++) if(lb[i] < t) J += vl[i];
            for(size_t i = j; i < rb.size(); i++) J += vr[i];
            cand.push_back({t, J, vr[j] + mu});
        }

        double bestT = (cLast + cFirst) * 0.5;
        if(!cand.empty()){
            double maxJ = cand[0].J;
            for(const auto& c : cand) maxJ = std::max(maxJ, c.J);
            const double tol = 0.03 * std::max(std::abs(maxJ), 1.0e-9);

            // ほぼ同点の候補のうち、「その拍に実際にオンセットがある」最も早い位置を選ぶ
            //  (音のない拍を境界にしてしまう1拍ズレや、ブレイクの頭に置かれるのを防ぐ)
            bool found = false;
            for(const auto& c : cand){
                if(c.J >= maxJ - tol && c.raw >= 0.4 * beatRef){ bestT = c.t; found = true; break; }
            }
            if(!found){
                for(const auto& c : cand){
                    if(c.J >= maxJ - tol){ bestT = c.t; break; }
                }
            }
        }
        R.boundaryEnv = bestT;
    }

    res.ok = true;
    for(size_t s = 0; s < segs.size(); s++){
        BpmSegment out;
        out.startMs = (s == 0) ? 0.0 : segs[s].boundaryEnv / rate * 1000.0;
        out.bpm = segs[s].bpm;
        out.clarity = segs[s].clarity;
        res.segments.push_back(out);
    }
    return res;
}