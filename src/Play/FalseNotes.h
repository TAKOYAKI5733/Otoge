#pragma once

// =====================================================================
//  FalseNotes.h — false ノーツ（当たり判定のない、見た目だけのノーツ）
//
//  ■ 目的
//    ノーツでお絵描きをする、ニセのノーツでプレイヤーをだます、などの演出用。
//    判定・コンボ・スコア・タップ音・Cube には一切かかわらない（noteJudge.h はこのノーツを知らない）
//
//  ■ 1 つの false ノーツが持つもの（譜面ファイルでは各難易度の "falseNotes"）
//    start / end       ：出現する時刻・消滅する時刻(ms)。この間だけ見える
//    x / width         ：出現する横の位置（レーン単位。0 = 1 レーンの左端、6 = 6 レーンの右端。小数も可）と幅（レーン数）
//    y                 ：出現する縦の位置（判定ラインからの高さ px。0 = 判定ライン、上がプラス）。posMode = 0 のとき
//    posMode / aheadMs ：縦の位置の決め方。0 = px 固定（y）、1 = スナップ
//                        スナップ：「start + aheadMs の時刻のノーツ」が出現の瞬間にいる高さ（aheadPx）に出る。流れはしない
//                        （エディタで「16 分の 3 マス先」のように決めたものを、保存のときに px にしたのが aheadPx。
//                         ふつうのノーツ速度のときの px なので、プレイヤーのノーツ速度の設定を PlayScene.cpp で掛ける）
//    dir / speed       ：進む方向（度。0 = 上（奥）、90 = 右、180 = 下（手前）、270 = 左）と速さ
//                        速さは A_SpeedEvent と同じ単位（1.8 = ふつうのノーツと同じ速さ。0 = その場に止まる）
//                        プレイヤーのノーツ速度の設定も、ふつうのノーツと同じく掛かる
//    appear / appearMs ：出現の方法（0 = すぐ、1 = グリッチ）とグリッチの長さ(ms)
//    disappear / disappearMs：消滅の方法（0 = すぐ、1 = グリッチ）とグリッチの長さ(ms)
//    type / long / longPx：見た目（0 = 通常、3 = DRAG、4 = LANE）、ロングの形か、ロングの帯の長さ(px)
//    changes           ：途中で見た目を変える [{time, type, long}]（time の時刻から、その見た目になる）
//
//  ■ 位置の計算（スクロールや B_SpeedEvent には影響されない、画面の上の自由な位置）
//    出てからの時間 e = 今 − start
//    x(px) = レーンの左端 + x × レーン幅 + sin(dir) × 速さ × e
//    y(px) = 判定ライン − 高さ         − cos(dir) × 速さ × e
//      高さ：px 固定なら y。スナップなら aheadPx × ノーツ速度の設定（読み込みのときに y に入れておく）
//    （レーンと同じ描き先に描くので、レーンを傾けると一緒に傾く）
//
//  ■ ブラウン管を通したような見た目（見えている間ずっと）
//    ・本体を少し薄くし（FALSE_CRT_ALPHA）、左に赤・右に青のにじみ（FALSE_CRT_FRINGE px）をうすく重ねる
//    ・形の上に、FALSE_CRT_LINE_GAP px おきに暗い走査線を引く。線はゆっくり下へ流れる
//    ・ときどき（FALSE_CRT_GLITCH_SLOT_MS ごとに FALSE_CRT_GLITCH_CHANCE の確率で）、形の一部の帯が左右にずれ、
//      赤と青の色ずれが付く（ノーツごとに起きる時が違う）
//
//  ■ グリッチの出方・消え方（粗い）
//    ノーツの形（ヘッド・ロングの帯）を横の太い帯（ヘッドの高さあたり FALSE_GLITCH_BANDS 本）に切り、
//    出るときは、帯がばらばらに現れていく（現れる割合 = 進み具合）。帯は左右に大きくずれ、赤と青の色ずれが付く。
//    消えるときはその逆。どちらも進み具合に合わせてずれが小さくなる（FALSE_GLITCH_SLOT_MS ごとに形が変わる）
//
//  ■ ロングの形の長さ（longPx）
//    エディタで「何分の何マス」で決めた長さを、保存のときに「出現の瞬間にその長さのロングが見える長さ(px)」にしたもの。
//    ふつうのノーツ速度のときの px なので、プレイヤーのノーツ速度の設定を PlayScene.cpp で掛ける
// =====================================================================

#include "GameCommon.h"
#include "Play/loadScore.h"     // json・toUtf8Path
#include "Play/roundedRect.h"
#include "Play/drawFrame.h"     // NOTE_SIDE_MARGIN・NOTE_RADIUS
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <vector>

struct FalseNoteLook{
    int32_t time = 0;     // この時刻から（changes のとき）
    int  type = 0;        // 0 = 通常、3 = DRAG、4 = LANE
    bool isLong = false;  // ロングの形
};

struct FalseNote{
    int32_t start = 0, end = 1000;
    double  x = 0.0;              // レーン単位
    int     width = 1;            // レーン数
    double  y = 400.0;            // 判定ラインからの高さ(px)
    int     posMode = 0;          // 0 = px 固定（y）、1 = スナップ（aheadMs 先のノーツの位置）
    int32_t aheadMs = 0;
    double  dirDeg = 180.0;       // 0 = 上、90 = 右、180 = 下、270 = 左
    double  speed = 0.0;          // A_SpeedEvent と同じ単位（px/ms）
    int     appear = 0;           // 0 = すぐ、1 = グリッチ
    int32_t appearMs = 250;
    int     disappear = 0;        // 0 = すぐ、1 = グリッチ
    int32_t disappearMs = 250;
    int     type = 0;
    bool    isLong = false;
    double  longPx = 200.0;       // ロングの形のときの帯の長さ(px)
    std::vector<FalseNoteLook> changes;   // 時刻の順
};

// 譜面ファイルから、この難易度の false ノーツを読む（無ければ空）
//   難易度の選び方は loadScore.h と同じ
inline bool loadFalseNotes(const std::string& filename, int difficultyIndex, std::vector<FalseNote>& out){
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
    if(!root->contains("falseNotes") || !root->at("falseNotes").is_array()) return true;
    for(const auto& it : root->at("falseNotes")){
        try{
            FalseNote f;
            f.start       = it.value("start", 0);
            f.end         = it.value("end", f.start + 1000);
            f.x           = it.value("x", 0.0);
            f.width       = std::clamp(it.value("width", 1), 1, 6);
            f.y           = it.value("y", 400.0);
            f.posMode     = it.value("posMode", 0);
            f.aheadMs     = it.value("aheadMs", 0);
            if(f.posMode == 1) f.y = it.value("aheadPx", f.y);   // スナップ：出現の瞬間の高さ（ノーツ速度の設定は PlayScene.cpp で掛ける）
            f.dirDeg      = it.value("dir", 180.0);
            f.speed       = it.value("speed", 0.0);
            f.appear      = it.value("appear", 0);
            f.appearMs    = std::max(0, it.value("appearMs", 250));
            f.disappear   = it.value("disappear", 0);
            f.disappearMs = std::max(0, it.value("disappearMs", 250));
            f.type        = it.value("type", 0);
            f.isLong      = it.value("long", false);
            f.longPx      = std::max(0.0, it.value("longPx", 200.0));
            if(it.contains("changes") && it.at("changes").is_array()){
                for(const auto& c : it.at("changes")){
                    FalseNoteLook l;
                    l.time   = c.value("time", f.start);
                    l.type   = c.value("type", 0);
                    l.isLong = c.value("long", false);
                    f.changes.push_back(l);
                }
                std::stable_sort(f.changes.begin(), f.changes.end(),
                                 [](const FalseNoteLook& a, const FalseNoteLook& b){ return a.time < b.time; });
            }
            if(f.end > f.start) out.push_back(f);
        }
        catch(const json::exception& e){
            printf("JSON / falseNotes / error : %s\n", e.what());
        }
    }
    return true;
}

// 見た目の色（renderGame.h のノーツと同じ）
inline SDL_Color falseNoteColor(int type){
    if(type == 3) return SDL_Color{250, 250, 150, 255};   // DRAG
    if(type == 4) return SDL_Color{255,  50,  50, 255};   // LANE
    return SDL_Color{255, 255, 255, 255};                  // 通常
}

inline double falseHash01(uint32_t x){
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return (x & 0xFFFFFF) / static_cast<double>(0x1000000);
}

inline constexpr int    FALSE_GLITCH_BANDS   = 3;       // グリッチで切る横の帯の数（ヘッドの高さあたり。少ないほど粗い）
inline constexpr float  FALSE_GLITCH_SHIFT   = 46.0f;   // 帯を左右にずらす最大(px)
inline constexpr float  FALSE_GLITCH_RGB     = 14.0f;   // 赤・青の色ずれの最大(px)
inline constexpr Uint32 FALSE_GLITCH_SLOT_MS = 70;      // 出る・消えるグリッチの形が変わる間隔

inline constexpr double FALSE_CRT_ALPHA          = 0.82;   // 本体の濃さ
inline constexpr float  FALSE_CRT_FRINGE         = 2.0f;   // 赤・青のにじみのずれ(px)
inline constexpr Uint8  FALSE_CRT_FRINGE_ALPHA   = 55;
inline constexpr float  FALSE_CRT_LINE_GAP       = 3.0f;   // 走査線の間隔(px)
inline constexpr Uint8  FALSE_CRT_LINE_ALPHA     = 60;     // 走査線の暗さ
inline constexpr double FALSE_CRT_LINE_SPEED     = 6.0;    // 走査線が流れる速さ(px/秒)
inline constexpr Uint32 FALSE_CRT_GLITCH_SLOT_MS = 80;
inline constexpr double FALSE_CRT_GLITCH_CHANCE  = 0.04;   // ノーツ 1 つで、だいたい 2 秒に 1 回、80ms だけ

// false ノーツを全部描く（レーンと同じ描き先に、ふつうのノーツより奥に描く）
//   visualTime：表示用の時刻　laneLeft・laneWidth：レーンの左端と幅　judgeY：判定ライン（描く側の座標）
//   headH(y)：その位置でのヘッドの太さ（レーンを傾けたとき用）
//   ※ プレイヤーのノーツ速度の設定は、読み込んだ後に speed へ掛けておく（PlayScene.cpp）
inline void drawFalseNotes(SDL_Renderer* r, const std::vector<FalseNote>& notes, double visualTime,
                           float laneLeft, float laneWidth, float judgeY,
                           const std::function<float(double)>& headH){
    static RoundedRectBatch batch;
    const Uint32 ticks = SDL_GetTicks();
    const uint32_t slot = static_cast<uint32_t>(ticks / FALSE_GLITCH_SLOT_MS);
    const uint32_t crtSlot = static_cast<uint32_t>(ticks / FALSE_CRT_GLITCH_SLOT_MS);
    const float lineOff = static_cast<float>(std::fmod(ticks / 1000.0 * FALSE_CRT_LINE_SPEED, static_cast<double>(FALSE_CRT_LINE_GAP)));
    for(size_t ni = 0; ni < notes.size(); ni++){
        const FalseNote& f = notes[ni];
        if(visualTime < f.start || visualTime >= f.end) continue;
        const double e = visualTime - f.start;

        // 今の見た目（途中の変更があれば、今の時刻までで一番新しいもの）
        int type = f.type;
        bool isLong = f.isLong;
        for(const FalseNoteLook& l : f.changes){
            if(l.time > visualTime) break;
            type = l.type;
            isLong = l.isLong;
        }
        const SDL_Color col = falseNoteColor(type);

        // 位置
        const double a  = f.dirDeg * 3.14159265358979 / 180.0;
        const double sp = f.speed;
        const float cx  = laneLeft + static_cast<float>(f.x) * laneWidth + static_cast<float>(std::sin(a) * sp * e);
        const float cy  = judgeY - static_cast<float>(f.y) - static_cast<float>(std::cos(a) * sp * e);
        const float noteX = cx + NOTE_SIDE_MARGIN;
        const float noteW = laneWidth * f.width - NOTE_SIDE_MARGIN * 2.0f;
        const float noteH = headH(cy);
        const float headTop = cy - noteH * 0.5f;
        const float bandTop = isLong ? headTop - static_cast<float>(f.longPx) : headTop;   // 形の上の端

        // グリッチの進み具合（1 = ふつうに見えている）
        double vis = 1.0;
        if(f.appear == 1 && f.appearMs > 0 && e < f.appearMs) vis = e / f.appearMs;
        const double left = f.end - visualTime;
        if(f.disappear == 1 && f.disappearMs > 0 && left < f.disappearMs) vis = std::min(vis, left / f.disappearMs);

        if(vis >= 1.0){
            // ブラウン管を通したような見た目：にじみ → 少し薄い本体（ロングの形：帯 → 芯 → ヘッド）→ 走査線 → ときどきグリッチ
            const float shapeBot = headTop + noteH;
            const Uint8 ca = static_cast<Uint8>(col.a * FALSE_CRT_ALPHA);
            batch.add(noteX - FALSE_CRT_FRINGE, bandTop, noteW, shapeBot - bandTop, NOTE_RADIUS, SDL_Color{255, 60, 90, FALSE_CRT_FRINGE_ALPHA});
            batch.add(noteX + FALSE_CRT_FRINGE, bandTop, noteW, shapeBot - bandTop, NOTE_RADIUS, SDL_Color{60, 200, 255, FALSE_CRT_FRINGE_ALPHA});
            if(isLong && f.longPx > 0.0){
                batch.add(noteX, bandTop, noteW, cy - bandTop, NOTE_RADIUS, SDL_Color{col.r, col.g, col.b, ca});
                const float innerW = noteW * 0.3f;
                const SDL_Color inner = {static_cast<Uint8>(col.r * 0.6), static_cast<Uint8>(col.g * 0.6), static_cast<Uint8>(col.b * 0.6), ca};
                const float innerTop = bandTop + 8.0f, innerBot = headTop - 8.0f;
                if(innerBot > innerTop) batch.add(noteX + (noteW - innerW) * 0.5f, innerTop, innerW, innerBot - innerTop, std::min(NOTE_RADIUS, innerW * 0.5f), inner);
            }
            batch.add(noteX, headTop, noteW, noteH, NOTE_RADIUS, SDL_Color{col.r, col.g, col.b, ca});
            for(float ly = bandTop + lineOff; ly < shapeBot; ly += FALSE_CRT_LINE_GAP){
                batch.add(noteX, ly, noteW, 1.0f, 0.0f, SDL_Color{0, 0, 0, FALSE_CRT_LINE_ALPHA}, 1, false);   // ぼかさない（ぼかすと線が太くなる）
            }
            // ときどきのグリッチ：形の一部の帯（ヘッドの高さの 4 割くらい）が左右にずれる
            const uint32_t gk = crtSlot * 2654435761u + static_cast<uint32_t>(ni) * 40503u;
            if(falseHash01(gk) < FALSE_CRT_GLITCH_CHANCE){
                const float gh = std::max(3.0f, noteH * (0.3f + 0.3f * static_cast<float>(falseHash01(gk + 1u))));
                const float gy = bandTop + static_cast<float>(falseHash01(gk + 2u)) * std::max(0.0f, shapeBot - bandTop - gh);
                const float gs = (falseHash01(gk + 3u) < 0.5 ? -1.0f : 1.0f) * (5.0f + 8.0f * static_cast<float>(falseHash01(gk + 4u)));
                batch.add(noteX + gs - 4.0f, gy, noteW, gh, 0.0f, SDL_Color{255, 40, 60, 110});
                batch.add(noteX + gs + 4.0f, gy, noteW, gh, 0.0f, SDL_Color{40, 200, 255, 110});
                batch.add(noteX + gs, gy, noteW, gh, 0.0f, SDL_Color{col.r, col.g, col.b, ca});
            }
            continue;
        }

        // グリッチ：形を横の太い帯に切って、ばらばらに出す・大きくずらす・色をずらす
        const float strength = static_cast<float>(1.0 - vis);
        const float shapeH = (headTop + noteH) - bandTop;
        const int bands = std::max(FALSE_GLITCH_BANDS, static_cast<int>(shapeH / (noteH / FALSE_GLITCH_BANDS)));
        const float bh = shapeH / bands;
        for(int b = 0; b < bands; b++){
            const uint32_t k = slot * 977u + static_cast<uint32_t>(ni) * 7919u + static_cast<uint32_t>(b) * 131u;
            if(falseHash01(k) >= vis) continue;   // まだ出ていない（もう消えた）帯
            const float y0 = bandTop + bh * b;
            const float shift = (falseHash01(k * 3u + 1u) < 0.7)
                ? static_cast<float>(falseHash01(k * 7u + 5u) * 2.0 - 1.0) * FALSE_GLITCH_SHIFT * strength : 0.0f;
            // この帯の色：ヘッドの所はノーツの色、帯の所は帯の色（芯は省く）
            const SDL_Color c = col;
            const float d = FALSE_GLITCH_RGB * strength;
            const Uint8 ra = static_cast<Uint8>(170 * strength);
            batch.add(noteX + shift - d, y0, noteW, bh + 0.5f, 0.0f, SDL_Color{255, 40, 60, ra});
            batch.add(noteX + shift + d, y0, noteW, bh + 0.5f, 0.0f, SDL_Color{40, 200, 255, ra});
            SDL_Color m = c;
            m.a = static_cast<Uint8>(c.a * (0.6 + 0.4 * falseHash01(k * 13u + 3u)));
            batch.add(noteX + shift, y0, noteW, bh + 0.5f, 0.0f, m);
        }
    }
    batch.flush(r);
}