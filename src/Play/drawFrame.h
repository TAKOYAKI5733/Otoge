#pragma once

#include "GameCommon.h"
#include "Play/GameContext.h"
#include "Play/type.h"
#include "Play/roundedRect.h"

// ===== ノーツの形（判定ボックスとノーツ描画の両方で使う。ここだけ直せば両方そろう） =====
inline constexpr float NOTE_SIDE_MARGIN = 8.0f;    // ノーツの左右それぞれの、レーンとの隙間(px)
inline constexpr float NOTE_HEAD_H      = 30.0f;   // ノーツのヘッドの高さ(px)
inline constexpr float NOTE_RADIUS      = 4.0f;    // ノーツの角の丸み(px)

// ===== 隠れているレーン（laneActive が false）のノーツ =====
//   レーンが閉じている間のノーツは、「本来の色のまま半透明」の中身 ＋ 「濃いめの縁取り」で描く。
//     ・中身が半透明なので、開いているレーンのノーツとは見分けがつく
//     ・縁取りがあるので、レーンの背景が無い所（背景の灰色・青のグラデーション）でも形がはっきり見える
//   値は 0〜255（小さいほど透明）。LANE ノーツはレーンを開け閉めするので対象外（いつも通り見える）
inline constexpr Uint8 HIDDEN_NOTE_ALPHA         = 110;   // ヘッド・ロングの帯の中身
inline constexpr Uint8 HIDDEN_NOTE_INNER_ALPHA   = 70;    // ロングの芯（帯より少し薄く）
inline constexpr Uint8 HIDDEN_NOTE_OUTLINE_ALPHA = 220;   // 縁取り
inline constexpr float HIDDEN_NOTE_OUTLINE_W     = 2.0f;  // 縁取りの太さ(px)

// ===== ロングノーツの芯 =====
//   芯は帯の真ん中の細い線。両端を帯より内側で止めて、帯より短くする
inline constexpr float  LONG_CORE_WIDTH_RATIO = 0.2f;    // 帯の幅に対する芯の幅
inline constexpr float  LONG_CORE_TAIL_MARGIN = 14.0f;   // 終点側：帯の端からどれだけ内側で止めるか(px)
inline constexpr float  LONG_CORE_HEAD_GAP    = 8.0f;    // ヘッド側：ヘッドの上の端からどれだけ離して止めるか(px)


// ===== 判定ボックス =====
inline constexpr float     JUDGE_BOX_BORDER = 3.0f;
inline constexpr float JUDGE_BOX_SIDE_MARGIN = 2.0f;   // 箱の左右それぞれの、レーンの端からの距離(px)             // 枠の太さ                  // ノーツと枠の間のすき間
inline constexpr SDL_Color JUDGE_BOX_COLOR  = {255, 255, 255, 255};  // 枠の色

// ===== 判定ボックスの押下エフェクト =====
inline constexpr SDL_Color JUDGE_FILL_COLOR   = {255, 255, 255, 255};   // 箱を埋める色
inline constexpr double    JUDGE_FLASH_MS     = 120.0;   // 押した瞬間の強い光が落ち着くまで
inline constexpr double    JUDGE_RELEASE_MS   = 150.0;   // 離してから消えるまで
inline constexpr double    JUDGE_HOLD_LEVEL   = 0.85;    // 押し続けている間の明るさ（0〜1）

// レーンごとの「いつ押したか・いつ離したか」を覚えておく
struct JudgeBoxFx{
    bool   held[6];
    double pressMs[6];
    double releaseMs[6];

    JudgeBoxFx(){
        for(int i = 0; i < 6; i++){
            held[i] = false;
            pressMs[i] = releaseMs[i] = -1e9;
        }
    }

    // 毎フレーム、今押されているかを渡す。押した瞬間・離した瞬間の時刻を記録する
    void update(const bool pressed[], double nowMs){
        for(int i = 0; i < 6; i++){
            if(pressed[i] && !held[i]) pressMs[i] = nowMs;
            if(!pressed[i] && held[i]) releaseMs[i] = nowMs;
            held[i] = pressed[i];
        }
    }

    // 押している間の明るさ：押した瞬間 1.0 → JUDGE_HOLD_LEVEL へ落ち着く
    static double heldLevel(double sincePress){
        double f = std::clamp(1.0 - sincePress / JUDGE_FLASH_MS, 0.0, 1.0);
        return JUDGE_HOLD_LEVEL + (1.0 - JUDGE_HOLD_LEVEL) * f * f;
    }

    // レーン i の今の明るさ（0 = 何もなし、1 = 一番明るい）
    double level(int i, double nowMs) const{
        if(held[i]){
            double lv = heldLevel(nowMs - pressMs[i]);
            if(nowMs - pressMs[i] > JUDGE_FLASH_MS){
                lv += 0.06 * std::sin((nowMs - pressMs[i]) * 0.012);   // 長押し中はゆっくり脈打つ
            }
            return lv;
        }
        // 離した後：離した瞬間の明るさから 0 へ
        double r = (nowMs - releaseMs[i]) / JUDGE_RELEASE_MS;
        if(r >= 1.0) return 0.0;
        double startLv = heldLevel(releaseMs[i] - pressMs[i]);
        return startLv * (1.0 - r);
    }
};

// レーンの背景・区切り線・判定ボックスを描く
//   yOffset: レーンを傾けるとき（Play/LaneTilt.h）は、画面より上の延長部分も描くため、
//            全体を yOffset px 下にずらし、レーンを上の端（y = 0）から SCREEN_H + yOffset まで伸ばして描く。
//            傾けないときは 0（今まで通り）
//   drawLines: レーンの区切り線を描くか。傾けるときは false にする
//              （テクスチャに描いた 1px の線を斜めに引き伸ばすとギザギザになるので、LaneTilt が画面に直接描く）
//   laneLength: レーンを上の端から何 px まで描くか。-1 なら SCREEN_H + yOffset（傾けるときは、下の延長部分も含めた長さを渡す）
//   headH: 判定ボックスの高さ。傾けるときは、画面でノーツと同じ太さに見えるよう LaneTilt::thicknessScale を掛けた値を渡す
// =====================================================================
//  レーンの下のキーの文字：ホログラム（立体の光る文字）＋ ときどきグリッチ
//
//  ■ 立体（厚み）
//    同じ文字の絵を HOLO_LAYERS 枚、少しずつ奥（画面の上）へずらして重ねる。
//    奥の層ほど暗く・青く、一番手前（一番高い層）だけ白く、はっきり描く → 光でできた文字が立っているように見える。
//    ずらす量は「レーンの傾き」に合わせて大きくなる（傾けていなくても、少しだけ厚みを付ける）
//  ■ ホログラムらしさ
//    ・奥の層は「足し算の合成（ADD）」で重ねる → 重なった所が光る
//    ・一番手前の層は細い横の帯（HOLO_SCANLINE_PX）に切って、1 本おきに薄くする → 走査線のしま模様
//    ・全体の明るさを、ほんの少しゆらす（チラつき）
//  ■ グリッチ（ときどき）
//    時間を HOLO_GLITCH_PERIOD_MS ごとの区切りに分け、区切りごとに「グリッチを起こすか・いつ・どれだけ」を
//    時刻から計算した乱数で決める（毎フレーム同じ答えになるので、変なちらつき方をしない）。
//    グリッチ中は、文字を横の帯に切って帯ごとに左右へずらし、赤と青の色ずれ（RGB ずらし）を重ねる
// =====================================================================
inline constexpr int    HOLO_LAYERS            = 6;       // 厚みの層の数
inline constexpr float  HOLO_DEPTH_FLAT_PX     = 3.0f;    // 傾けていないときの厚み(px)
inline constexpr float  HOLO_DEPTH_TILT_PX     = 16.0f;   // 一番傾けたときに足す厚み(px)
inline constexpr SDL_Color HOLO_BACK_COLOR     = {40, 150, 255, 255};   // 奥の層の色（青）
inline constexpr SDL_Color HOLO_FRONT_COLOR    = {200, 250, 255, 255};  // 一番手前の層の色（白っぽい水色）
inline constexpr Uint8  HOLO_BACK_ALPHA        = 70;      // 奥の層の濃さ（足し算で重なるので薄め）
inline constexpr int    HOLO_SCANLINE_PX       = 3;       // 走査線の太さ(px)
inline constexpr float  HOLO_SCANLINE_DIM      = 0.55f;   // 走査線の薄い方の濃さ（濃い方の何倍か）
inline constexpr double HOLO_GLITCH_PERIOD_MS  = 2600.0;  // この時間ごとに「グリッチを起こすか」を決める
inline constexpr double HOLO_GLITCH_CHANCE     = 0.55;    // 1 区切りの中でグリッチが起きる確率
inline constexpr double HOLO_GLITCH_MIN_MS     = 80.0;    // グリッチの長さ（短いとき）
inline constexpr double HOLO_GLITCH_MAX_MS     = 200.0;   // グリッチの長さ（長いとき）
inline constexpr int    HOLO_GLITCH_BANDS      = 6;       // グリッチで切る横の帯の数
inline constexpr float  HOLO_GLITCH_SHIFT_PX   = 14.0f;   // 帯を左右にずらす最大(px)
inline constexpr float  HOLO_GLITCH_RGB_PX     = 4.0f;    // 色ずれの幅(px)

// 整数 → 0〜1 の乱数（同じ数を入れれば、いつも同じ答え）
inline double holoHash01(uint32_t x){
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return (x & 0xFFFFFF) / static_cast<double>(0x1000000);
}

// ----- 判定枠・レーンの区切り線のホログラム（文字より控えめ） -----
inline constexpr float  HOLO_FRAME_DEPTH_RATIO = 0.5f;    // 厚みは文字の何倍か（控えめに半分）
inline constexpr Uint8  HOLO_FRAME_BACK_ALPHA  = 55;      // 判定枠の奥の層の濃さ
inline constexpr Uint8  HOLO_FRAME_FRONT_ALPHA = 225;     // 判定枠の一番手前の濃さ
inline constexpr Uint8  HOLO_LINE_ALPHA        = 170;     // レーンの区切り線の濃さ
inline constexpr float  HOLO_FRAME_GLITCH_PX   = 5.0f;    // グリッチで判定枠を左右にずらす最大(px)。文字（14px）より小さく
inline constexpr float  HOLO_FRAME_RGB_PX      = 2.0f;    // 判定枠・区切り線の色ずれの幅(px)。文字（4px）より小さく
inline constexpr Uint8  HOLO_FRAME_RGB_ALPHA   = 70;      // 色ずれの濃さ

// 今のホログラムの状態（文字・判定枠・区切り線で同じものを使う → グリッチが同時に起きる）
struct HoloFx{
    bool     glitch = false;   // 今グリッチ中か
    uint32_t seed = 0;         // グリッチのずれ方を決める数（30ms ごとに変わる）
    float    flicker = 1.0f;   // 明るさのゆらぎ（0.84〜1.0 くらい）
};

inline HoloFx holoNow(){
    HoloFx fx;
    const double t = static_cast<double>(SDL_GetTicks());   // 見た目だけの演出なので、実時間で動かす
    const uint32_t period = static_cast<uint32_t>(t / HOLO_GLITCH_PERIOD_MS);
    if(holoHash01(period * 3u + 1u) < HOLO_GLITCH_CHANCE){
        const double len   = HOLO_GLITCH_MIN_MS + (HOLO_GLITCH_MAX_MS - HOLO_GLITCH_MIN_MS) * holoHash01(period * 3u + 2u);
        const double start = (HOLO_GLITCH_PERIOD_MS - len) * holoHash01(period * 3u + 3u);
        const double local = t - period * HOLO_GLITCH_PERIOD_MS;
        fx.glitch = (local >= start && local < start + len);
    }
    fx.seed = static_cast<uint32_t>(t / 30.0);
    fx.flicker = static_cast<float>(0.92 + 0.08 * std::sin(t * 0.021) * std::sin(t * 0.0037));
    return fx;
}

// グリッチ中の「左右のずれ」（-1〜1）。key ごとに違う値。6 割だけずれて、残りは 0
inline float holoGlitchShift(const HoloFx& fx, uint32_t key){
    if(!fx.glitch) return 0.0f;
    const double rnd = holoHash01(fx.seed * 131u + key * 17u);
    return (rnd < 0.6) ? static_cast<float>(rnd / 0.6 * 2.0 - 1.0) : 0.0f;
}

// 区切り線の色（ホログラムの手前の色・ゆらぎ込み）
inline SDL_Color holoLineColor(const HoloFx& fx){
    SDL_Color c = HOLO_FRONT_COLOR;
    c.a = static_cast<Uint8>(HOLO_LINE_ALPHA * fx.flicker);
    return c;
}

// 画像の一部（縦 v0〜v1 の帯）を、四角 (x, y, w, h) に貼る。傾けているときは角の点を傾けた位置に動かす
inline void holoTexQuad(SDL_Renderer* r, SDL_Texture* tex, float x, float y, float w, float h,
                        float v0, float v1, SDL_Color c){
    SDL_Vertex v[4] = {
        {{x,     y},     c, {0.0f, v0}},
        {{x + w, y},     c, {1.0f, v0}},
        {{x,     y + h}, c, {0.0f, v1}},
        {{x + w, y + h}, c, {1.0f, v1}},
    };
    tiltApply(v, 4);   // 傾けていなければ何もしない
    const int idx[6] = {0, 1, 2, 2, 1, 3};
    SDL_RenderGeometry(r, tex, v, 4, idx, 6);
}

// キーの文字を、ホログラムとして描く。holo3D = 立体感の強さ（0〜1。レーンの傾きで決まる）
inline void drawHoloLabels(SDL_Renderer* r, SDL_Texture* tex, const SDL_Rect& rect, float holo3D){
    if(!tex) return;
    const float x = static_cast<float>(rect.x), y = static_cast<float>(rect.y);
    const float w = static_cast<float>(rect.w), h = static_cast<float>(rect.h);

    // グリッチ中か・ずれ方・明るさのゆらぎ（判定枠・区切り線と同じもの）
    const HoloFx fx = holoNow();
    const bool glitch = fx.glitch;
    const uint32_t gseed = fx.seed;
    const float flicker = fx.flicker;
    auto mul = [](SDL_Color c, float k){ c.a = static_cast<Uint8>(std::clamp(c.a * k, 0.0f, 255.0f)); return c; };

    // ----- 1. 厚み：奥の層から順に、足し算で重ねる -----
    const float depth = HOLO_DEPTH_FLAT_PX + HOLO_DEPTH_TILT_PX * std::clamp(holo3D, 0.0f, 1.0f);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
    for(int k = 0; k < HOLO_LAYERS - 1; k++){
        const float f = static_cast<float>(k) / (HOLO_LAYERS - 1);          // 0 = 一番奥（レーンの面）
        SDL_Color c = {
            static_cast<Uint8>(HOLO_BACK_COLOR.r + (HOLO_FRONT_COLOR.r - HOLO_BACK_COLOR.r) * f * 0.5f),
            static_cast<Uint8>(HOLO_BACK_COLOR.g + (HOLO_FRONT_COLOR.g - HOLO_BACK_COLOR.g) * f * 0.5f),
            static_cast<Uint8>(HOLO_BACK_COLOR.b + (HOLO_FRONT_COLOR.b - HOLO_BACK_COLOR.b) * f * 0.5f),
            static_cast<Uint8>(HOLO_BACK_ALPHA * (0.6f + 0.4f * f))};
        holoTexQuad(r, tex, x, y - depth * f, w, h, 0.0f, 1.0f, mul(c, flicker));
    }

    // ----- 2. グリッチの色ずれ：赤を左、青を右にずらして足す -----
    const float topY = y - depth;
    if(glitch){
        const float d = HOLO_GLITCH_RGB_PX * static_cast<float>(0.5 + holoHash01(gseed * 7u + 11u));
        holoTexQuad(r, tex, x - d, topY, w, h, 0.0f, 1.0f, SDL_Color{255, 40, 60, 150});
        holoTexQuad(r, tex, x + d, topY, w, h, 0.0f, 1.0f, SDL_Color{40, 200, 255, 150});
    }

    // ----- 3. 一番手前の層：走査線のしま模様（細い横の帯を 1 本おきに薄く）。グリッチ中は帯ごとに左右へずらす -----
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    const int rows = std::max(1, static_cast<int>(h) / HOLO_SCANLINE_PX);
    for(int i = 0; i < rows; i++){
        const float v0 = static_cast<float>(i) / rows, v1 = static_cast<float>(i + 1) / rows;
        float shift = 0.0f;
        if(glitch){
            const int band = i * HOLO_GLITCH_BANDS / rows;                        // この行が入る帯
            shift = holoGlitchShift(fx, static_cast<uint32_t>(band)) * HOLO_GLITCH_SHIFT_PX;   // 6 割の帯だけずらす
        }
        const float a = (i % 2 == 0) ? 1.0f : HOLO_SCANLINE_DIM;
        holoTexQuad(r, tex, x + shift, topY + h * v0, w, h * (v1 - v0), v0, v1,
                    mul(HOLO_FRONT_COLOR, a * flicker * 0.9f));
    }
}

// =====================================================================
//  レーンノーツでレーンが開く・閉じるときのグリッチ演出
//
//  ■ いつ出るか
//    draw_waku_init が呼ばれるたびに laneActive を前のフレームと比べ、変わったレーンだけ
//    LANE_GLITCH_MS の間、演出を出す（判定・ノーツの動きには一切さわらない。レーンの背景の描き方だけ変える）。
//    ・前に呼ばれてから LANE_GLITCH_RESYNC_MS 以上あいたとき（曲の読み込み直後など）は、今の状態を覚え直すだけで演出は出さない
//
//  ■ 見た目（判定枠・区切り線と同じホログラムの色。holoHash01 で 30ms ごとに形が変わる）
//    レーンを LANE_GLITCH_BANDS 本の横の帯に切り、
//    ・開くとき：帯がばらばらに現れていき、最後に全部そろう（現れる割合 = 進み具合）
//    ・閉じるとき：帯がばらばらに抜けていき、最後に全部消える（残る割合 = 1 − 進み具合）
//    残っている帯の半分くらいを左右に少しずらし、青く薄い色を重ねる。
//    レーンの左右の端に赤と青の色ずれの線、帯の境目に白っぽい水色の細い線を 1 本出す。
//    どれも時間とともに弱くなり、LANE_GLITCH_MS で消える。
//
//  ■ プレイの邪魔にならないように
//    ・描くのはレーンの背景の段階（ノーツ・判定枠より奥）なので、ノーツは隠れない
//    ・短い（0.26 秒）・ずれは最大 LANE_GLITCH_SHIFT_PX（レーン幅 120px の 1 割以下）・色は薄め
// =====================================================================
inline constexpr double LANE_GLITCH_MS         = 100.0;   // 演出の長さ(ms)
inline constexpr int    LANE_GLITCH_BANDS      = 14;      // 横の帯の数
inline constexpr float  LANE_GLITCH_SHIFT_PX   = 10.0f;   // 帯を左右にずらす最大(px)
inline constexpr Uint8  LANE_GLITCH_TINT_ALPHA = 70;      // 帯に重ねる青の濃さ（始まりの濃さ）
inline constexpr Uint8  LANE_GLITCH_EDGE_ALPHA = 140;     // 端の色ずれの線の濃さ（始まりの濃さ）
inline constexpr Uint8  LANE_GLITCH_LINE_ALPHA = 110;     // 帯の境目の細い線の濃さ（始まりの濃さ）
inline constexpr Uint32 LANE_GLITCH_RESYNC_MS  = 300;     // これ以上あいて呼ばれたら、覚え直すだけにする

struct LaneToggleFx{
    bool   prev[6]   = {};      // 前に見たときの laneActive
    bool   active[6] = {};      // 演出中か
    bool   opening[6] = {};     // true = 開いた、false = 閉じた
    Uint32 start[6]  = {};      // 演出が始まった時刻（SDL_GetTicks）
    Uint32 lastCall  = 0;
    bool   init      = false;
};

inline LaneToggleFx& laneToggleFx(){
    static LaneToggleFx s;
    return s;
}

// laneActive の変化を見つけて、演出を始める（draw_waku_init の最初で毎フレーム呼ぶ）
inline void updateLaneToggleFx(const bool laneActive[]){
    LaneToggleFx& s = laneToggleFx();
    const Uint32 now = SDL_GetTicks();
    const bool resync = !s.init || (now - s.lastCall) > LANE_GLITCH_RESYNC_MS;
    for(int i = 0; i < 6; i++){
        if(resync){
            s.prev[i] = laneActive[i];
            s.active[i] = false;
        }
        else if(laneActive[i] != s.prev[i]){
            s.prev[i]    = laneActive[i];
            s.active[i]  = true;
            s.opening[i] = laneActive[i];
            s.start[i]   = now;
        }
        if(s.active[i] && now - s.start[i] >= static_cast<Uint32>(LANE_GLITCH_MS)) s.active[i] = false;
    }
    s.init = true;
    s.lastCall = now;
}

// レーンの四角を塗る（傾けているときは傾けた位置に）
inline void fillLaneRect(SDL_Renderer* r, float x, float y, float w, float h, SDL_Color c, int segs){
    if(w <= 0.0f || h <= 0.0f || c.a == 0) return;
    if(g_tiltProj){
        tiltRect(r, x, y, w, h, c, nullptr, segs);
        return;
    }
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    const SDL_FRect rect = {x, y, w, h};
    SDL_RenderFillRectF(r, &rect);
}

// 演出中のレーン 1 本を描く
//   x, w：レーンの左端と幅　laneH：レーンの長さ　base：レーンの色
inline void drawLaneToggleGlitch(SDL_Renderer* r, int lane, float x, float w, float laneH, SDL_Color base){
    const LaneToggleFx& s = laneToggleFx();
    const Uint32 now = SDL_GetTicks();
    const float t = std::clamp(static_cast<float>((now - s.start[lane]) / LANE_GLITCH_MS), 0.0f, 1.0f);   // 進み具合
    const float strength = (1.0f - t) * (1.0f - t);                 // 弱まり方（はじめ強く、すぐ落ち着く）
    const float showRatio = s.opening[lane] ? t : (1.0f - t);       // 帯が見えている割合
    const uint32_t seed = now / 30u;                                 // 30ms ごとに形を変える
    const uint32_t key  = seed * 977u + static_cast<uint32_t>(lane) * 131u;
    const float bandH = laneH / LANE_GLITCH_BANDS;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for(int b = 0; b < LANE_GLITCH_BANDS; b++){
        const uint32_t k = key + static_cast<uint32_t>(b) * 17u;
        if(holoHash01(k) >= showRatio) continue;                     // この帯はまだ現れていない／もう抜けた
        float shift = 0.0f;
        if(holoHash01(k * 3u + 5u) < 0.5){
            shift = static_cast<float>(holoHash01(k * 7u + 11u) * 2.0 - 1.0) * LANE_GLITCH_SHIFT_PX * strength;
        }
        const float y = bandH * b;
        fillLaneRect(r, x + shift, y, w, bandH + 0.5f, base, 2);
        SDL_Color tint = HOLO_BACK_COLOR;
        tint.a = static_cast<Uint8>(LANE_GLITCH_TINT_ALPHA * strength);
        fillLaneRect(r, x + shift, y, w, bandH + 0.5f, tint, 2);
    }

    // 左右の端の色ずれ（赤・青）
    const Uint8 ea = static_cast<Uint8>(LANE_GLITCH_EDGE_ALPHA * strength);
    fillLaneRect(r, x - 3.0f, 0.0f, 2.0f, laneH, SDL_Color{255, 40, 60, ea}, 24);
    fillLaneRect(r, x + w + 1.0f, 0.0f, 2.0f, laneH, SDL_Color{40, 200, 255, ea}, 24);

    // 帯の境目に、白っぽい水色の細い線を 1 本
    const int lineBand = 1 + static_cast<int>(holoHash01(key + 999u) * (LANE_GLITCH_BANDS - 1));
    SDL_Color lc = HOLO_FRONT_COLOR;
    lc.a = static_cast<Uint8>(LANE_GLITCH_LINE_ALPHA * strength);
    fillLaneRect(r, x, bandH * lineBand - 1.0f, w, 2.0f, lc, 1);
}

// レーンの背景の色。前は偶数・奇数で 2 色だったのを、濃い方の 1 色にそろえた
//   （区切り線を「開いているレーンが続く所の左右の端」だけにしたので、続いたレーンが 1 本の広い道に見えるように）
//   ノーツを取ったときの立方体（renderGame.h）もこの色を使う
inline constexpr SDL_Color LANE_BG_COLOR = {0, 0, 5, 255};
inline SDL_Color laneBaseColor(int /*i*/){
    return LANE_BG_COLOR;
}

// =====================================================================
//  レーンの区切り線（枠）の出し方
//
//  ■ どこに線を置くか（判定ラインより上）
//    区切り線は 7 本（k = 0〜6。k 本目は、レーン k−1 とレーン k の境目）。
//    ・k = 0（1 レーンの左）と k = 6（6 レーンの右）：ビジュアライザーがある外側の枠なので、いつも出す
//    ・それ以外：片方のレーンだけが開いているとき（＝開いているレーンが続く所の左右の端）だけ出す
//      両方開いている（続きの途中）・両方閉じている所には出さない
//
//  ■ 判定ラインより下（キーの文字が書いてある所）：今まで通り、7 本ともいつも出す
//
//  ■ 始まり
//    レーンの始まりの演出の間と、その後 LANE_LINE_ALL_MS の間は、7 本とも出しておく。
//    その後、要らない線を LANE_LINE_OUT_INTRO_MS かけてグリッチで消す。
//    プレイ中にレーンが開く・閉じるときも、線が要る・要らないに変わった所は、グリッチで出す・消す（LANE_LINE_FX_MS）
//
//  ■ グリッチ（判定ラインより上の部分だけ）
//    線を LANE_LINE_BANDS 本の縦の切れ端に分け、
//    ・出るとき：切れ端がばらばらに現れていく（現れる割合 = 進み具合）
//    ・消えるとき：切れ端がばらばらに抜けていく（残る割合 = 1 − 進み具合）
//    残っている切れ端の 6 割くらいを左右にずらし、赤と青の色ずれ・明るさのちらつきを付ける。
//    どれも時間とともに弱くなる（30ms ごとに形が変わる）
//    判定・ノーツには一切さわらない（線の描き方だけ）
// =====================================================================
inline constexpr double LANE_LINE_ALL_MS       = 800.0;  // 始まりの演出の後、7 本とも出しておく時間(ms)
inline constexpr double LANE_LINE_OUT_INTRO_MS = 450.0;   // 始まりの後に、要らない線を消すグリッチの長さ(ms)
inline constexpr double LANE_LINE_FX_MS        = 250.0;   // プレイ中にレーンが開く・閉じるときのグリッチの長さ(ms)
inline constexpr int    LANE_LINE_BANDS        = 18;      // 線を分ける切れ端の数
inline constexpr float  LANE_LINE_SHIFT_PX     = 9.0f;    // 切れ端を左右にずらす最大(px)
inline constexpr float  LANE_LINE_RGB_PX       = 4.0f;    // グリッチ中の赤・青の色ずれの幅(px)
inline constexpr Uint8  LANE_LINE_RGB_ALPHA    = 150;     // その濃さ（始まりの濃さ）
inline constexpr float  LANE_LINE_TILT_W       = 2.0f;    // 傾けているときの線の太さ（LaneTilt.h の LINE_WIDTH と同じ）
inline constexpr Uint32 LANE_LINE_RESYNC_MS    = 300;     // これ以上あいて呼ばれたら（曲の始まりなど）、始めからやり直す

struct LaneLineFx{
    bool   shown[7]  = {};     // 今、出ている（出る途中も含む）か
    bool   active[7] = {};     // グリッチ中か
    Uint32 start[7]  = {};     // グリッチが始まった時刻
    double dur[7]    = {};     // そのグリッチの長さ(ms)
    Uint32 allUntil  = 0;      // この時刻までは 7 本とも出す
    Uint32 lastCall  = 0;
    bool   init      = false;
};

inline LaneLineFx& laneLineFx(){
    static LaneLineFx s;
    return s;
}

// k 本目の区切り線が要るか（判定ラインより上）
inline bool laneLineNeeded(const bool laneActive[], int k){
    if(k <= 0 || k >= 6) return true;              // 外側の枠（ビジュアライザーの所）はいつも
    return laneActive[k - 1] != laneActive[k];     // 開いている・閉じているの境目だけ
}

// 毎フレーム 1 回呼ぶ（draw_waku_init の最初）。introRunning：レーンの始まりの演出の途中か
inline void updateLaneLineFx(const bool laneActive[], bool introRunning){
    LaneLineFx& s = laneLineFx();
    const Uint32 now = SDL_GetTicks();
    if(!s.init || (now - s.lastCall) > LANE_LINE_RESYNC_MS){
        // 曲の始まり：7 本とも出した状態から
        for(int k = 0; k < 7; k++){ s.shown[k] = true; s.active[k] = false; }
        s.allUntil = now + static_cast<Uint32>(LANE_LINE_ALL_MS);
    }
    if(introRunning) s.allUntil = now + static_cast<Uint32>(LANE_LINE_ALL_MS);   // 始まりの演出が終わってから数える
    const bool allPhase = static_cast<int32_t>(s.allUntil - now) > 0;
    // 7 本とも出す時間が終わった直後の消え方は、少しゆっくり大きく
    const bool introOut = static_cast<int32_t>(now - s.allUntil) < 600;
    for(int k = 0; k < 7; k++){
        const bool want = allPhase || laneLineNeeded(laneActive, k);
        if(want != s.shown[k]){
            s.shown[k]  = want;
            s.active[k] = true;
            s.start[k]  = now;
            s.dur[k]    = introOut ? LANE_LINE_OUT_INTRO_MS : LANE_LINE_FX_MS;
        }
        if(s.active[k] && now - s.start[k] >= static_cast<Uint32>(s.dur[k])) s.active[k] = false;
    }
    s.init = true;
    s.lastCall = now;
}

// 縦の線を 1 本描く（x、描く側の y0〜y1）。傾けているときは、傾けた位置に、なめらかな縁で描く
inline void drawLaneLineSeg(SDL_Renderer* r, float x, float y0, float y1, SDL_Color c){
    if(y1 <= y0 || c.a == 0) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    if(g_tiltProj){
        // ぼかしの濃さを正しく付けるため、ぼかしが終わる所で 2 本に分ける（LaneTilt::end と同じ）
        const TiltProjection& p = *g_tiltProj;
        const float yf = p.fade ? std::clamp(static_cast<float>(p.unmapY(p.fadeEndY)), y0, y1) : y0;
        if(yf > y0 + 0.5f) tiltLine(r, x, y0, x, yf, LANE_LINE_TILT_W, c);
        tiltLine(r, x, yf, x, y1, LANE_LINE_TILT_W, c);
        return;
    }
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLineF(r, x, y0, x, y1);
}

// 区切り線を全部描く
//   xs：7 本の x　yTop：上の端（レーンカバーの壁の下の端など）　judgeY：判定ライン　yBottom：下の端（どれも描く側の座標）
inline void drawLaneLines(SDL_Renderer* r, const float xs[7], float yTop, float judgeY, float yBottom){
    const HoloFx holo = holoNow();
    const SDL_Color lc = holoLineColor(holo);
    const LaneLineFx& s = laneLineFx();
    const Uint32 now = SDL_GetTicks();

    // ふだんの線（ホログラムのグリッチ中は、今まで通り赤と青の色ずれを薄く重ねる）
    auto plain = [&](float x, float y0, float y1){
        if(holo.glitch){
            drawLaneLineSeg(r, x - HOLO_FRAME_RGB_PX, y0, y1, SDL_Color{255, 40, 60, HOLO_FRAME_RGB_ALPHA});
            drawLaneLineSeg(r, x + HOLO_FRAME_RGB_PX, y0, y1, SDL_Color{40, 200, 255, HOLO_FRAME_RGB_ALPHA});
        }
        drawLaneLineSeg(r, x, y0, y1, lc);
    };

    const float upBot = std::max(yTop, std::min(judgeY, yBottom));   // 判定ラインより上の部分の下の端
    for(int k = 0; k < 7; k++){
        const float x = xs[k];
        // 判定ラインより下（キーの文字の所）：今まで通り、いつも
        if(yBottom > upBot) plain(x, upBot, yBottom);
        if(upBot <= yTop) continue;

        if(!s.active[k]){
            if(s.shown[k]) plain(x, yTop, upBot);
            continue;
        }
        // グリッチで出る・消える途中
        const float t = std::clamp(static_cast<float>((now - s.start[k]) / s.dur[k]), 0.0f, 1.0f);
        const float strength = 1.0f - t * t;                             // 最後まで強めに残し、終わりで落ち着く
        const float showRatio = s.shown[k] ? t : (1.0f - t);            // 見えている切れ端の割合
        const uint32_t key = (now / 30u) * 977u + static_cast<uint32_t>(k) * 131u;
        const float bandH = (upBot - yTop) / LANE_LINE_BANDS;
        for(int b = 0; b < LANE_LINE_BANDS; b++){
            const uint32_t h = key + static_cast<uint32_t>(b) * 17u;
            if(holoHash01(h) >= showRatio) continue;
            float shift = 0.0f;
            if(holoHash01(h * 3u + 5u) < 0.6){
                shift = static_cast<float>(holoHash01(h * 7u + 11u) * 2.0 - 1.0) * LANE_LINE_SHIFT_PX * strength;
            }
            const float y0 = yTop + bandH * b;
            const float y1 = y0 + bandH + 0.5f;
            const float d  = 1.0f + LANE_LINE_RGB_PX * strength;
            const Uint8 ra = static_cast<Uint8>(LANE_LINE_RGB_ALPHA * strength);
            drawLaneLineSeg(r, x + shift - d, y0, y1, SDL_Color{255, 40, 60, ra});
            drawLaneLineSeg(r, x + shift + d, y0, y1, SDL_Color{40, 200, 255, ra});
            SDL_Color c = lc;
            c.a = static_cast<Uint8>(c.a * (0.55 + 0.45 * holoHash01(h * 13u + 3u)));   // 明るさのちらつき
            drawLaneLineSeg(r, x + shift, y0, y1, c);
        }
    }
}

// introRunning：レーンの始まりの演出の途中か（区切り線は、その間と終わってから LANE_LINE_ALL_MS の間、7 本とも出す）
inline void draw_waku_init(SDL_Renderer* renderer, Tex& tex, Sq& sq, bool laneActive[], int yOffset = 0, bool drawLines = true,
                           int laneLength = -1, float headH = NOTE_HEAD_H, float holo3D = 0.0f, bool introRunning = false){
    int laneWidth = SCREEN_W / 16;
    int startX = SCREEN_W / 2 - (laneWidth * 3);
    const int laneH = (laneLength > 0) ? laneLength : SCREEN_H + yOffset;   // レーンの長さ（延長部分を含む）

    // レーンの背景（1 色）
    auto laneColor = [](int i){ return laneBaseColor(i); };

    // レーンが開く・閉じるときのグリッチ（開くときは、演出が終わるまで普通には塗らず、帯で少しずつ出す）
    updateLaneToggleFx(laneActive);
    const LaneToggleFx& toggleFx = laneToggleFx();
    // 区切り線を出す・消す（開いているレーンが続く所の左右の端だけにする）
    updateLaneLineFx(laneActive, introRunning);

    for(int i = 0; i < 6; i++){
        if(!laneActive[i]) continue;
        if(toggleFx.active[i] && toggleFx.opening[i]) continue;   // 開く途中 → 下の drawLaneToggleGlitch で描く
        SDL_Rect rect = {startX + (laneWidth * i), 0, laneWidth, laneH};
        SDL_Color c = laneColor(i);
        if(g_tiltProj){
            // 傾けているとき：レーンの四角を、角の点を傾けた位置に変換して直接描く（帯に分けるのは、ぼかしの濃さを正しく付けるため）
            tiltRect(renderer, static_cast<float>(rect.x), 0.0f, static_cast<float>(rect.w), static_cast<float>(rect.h), c, nullptr, 48);
            continue;
        }
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
        SDL_RenderFillRect(renderer, &rect);
    }

    for(int i = 0; i < 6; i++){
        if(!toggleFx.active[i]) continue;
        drawLaneToggleGlitch(renderer, i, static_cast<float>(startX + laneWidth * i), static_cast<float>(laneWidth),
                             static_cast<float>(laneH), laneColor(i));
    }

    // レーンの区切り線：ホログラムの色（ゆらぎ込み）。判定ラインより上は、開いているレーンが続く所の左右の端だけ（drawLaneLines）
    //   傾けているときの区切り線は renderGame.h で、LaneTilt::end の前に drawLaneLines で描く
    const HoloFx holo = holoNow();
    if(drawLines){
        float xs[7];
        for(int i = 0; i <= 6; i++) xs[i] = static_cast<float>(startX + laneWidth * i);
        const float judgeLineY = static_cast<float>(SCREEN_H * (3.0 / 4.0)) + static_cast<float>(yOffset);
        drawLaneLines(renderer, xs, 0.0f, judgeLineY, static_cast<float>(laneH));
    }

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_Rect wakuRect = sq.waku_init;
    wakuRect.y += yOffset;
    // キーの文字：ホログラムの立体文字（傾けているときは、角の点を傾けた位置に動かして貼る）
    drawHoloLabels(renderer, tex.waku_init, wakuRect, holo3D);

    // 判定ボックス：横線の代わりに、ノーツのヘッドがちょうど収まる角丸の枠を有効なレーンごとに置く
    //   外側の角丸（枠の色）を描き、その内側を同じ形でレーンの色に塗り戻すと、角丸の「枠線」になる
    const float judgeY = static_cast<float>(SCREEN_H * (3.0 / 4.0)) + static_cast<float>(yOffset);

    // 判定枠：キーの文字と同じホログラム（文字より控えめ）
    //   ・一番手前（白っぽい水色）は判定ラインの位置ぴったりに置き（タイミングの見え方を変えないため）、
    //     その奥（画面の上）に青く薄い層を 2 枚ずらして重ねる → 光の枠に厚みが出る
    //     厚みは文字の HOLO_FRAME_DEPTH_RATIO 倍。傾きに合わせて大きくなる
    //   ・グリッチ中は、枠ごとに少し左右へずらし、赤と青の色ずれを薄く重ねる
    const float frameDepth = (HOLO_DEPTH_FLAT_PX + HOLO_DEPTH_TILT_PX * std::clamp(holo3D, 0.0f, 1.0f)) * HOLO_FRAME_DEPTH_RATIO;
    RoundedRectBatch boxBatch;
    for(int i = 0; i < 6; i++){
        // レーンが隠れていても、判定ボックスだけは描く（laneActive は見ない）
        const float shift = holoGlitchShift(holo, 100u + static_cast<uint32_t>(i)) * HOLO_FRAME_GLITCH_PX;
        const float noteX = static_cast<float>(startX + laneWidth * i) + JUDGE_BOX_SIDE_MARGIN + shift;
        const float noteW = static_cast<float>(laneWidth) - JUDGE_BOX_SIDE_MARGIN * 2.0f;
        const float noteY = judgeY - headH * 0.5f;

        // 奥の層（遠い方から順に）
        for(int k = 2; k >= 1; k--){
            const float f = static_cast<float>(k) / 2.0f;   // 1 = 一番奥、0.5 = 半分
            SDL_Color bc = HOLO_BACK_COLOR;
            bc.a = static_cast<Uint8>(HOLO_FRAME_BACK_ALPHA * holo.flicker);
            boxBatch.addOutline(noteX, noteY - frameDepth * f, noteW, headH, NOTE_RADIUS, JUDGE_BOX_BORDER, bc);
        }
        // 色ずれ（グリッチ中だけ）
        if(holo.glitch){
            const float d = HOLO_FRAME_RGB_PX;
            boxBatch.addOutline(noteX - d, noteY, noteW, headH, NOTE_RADIUS, JUDGE_BOX_BORDER,
                                SDL_Color{255, 40, 60, HOLO_FRAME_RGB_ALPHA});
            boxBatch.addOutline(noteX + d, noteY, noteW, headH, NOTE_RADIUS, JUDGE_BOX_BORDER,
                                SDL_Color{40, 200, 255, HOLO_FRAME_RGB_ALPHA});
        }
        // 一番手前（外形はノーツと同じ。枠線は内側へ JUDGE_BOX_BORDER px、内側は塗らない）
        SDL_Color fc = HOLO_FRONT_COLOR;
        fc.a = static_cast<Uint8>(HOLO_FRAME_FRONT_ALPHA * holo.flicker);
        boxBatch.addOutline(noteX, noteY, noteW, headH, NOTE_RADIUS, JUDGE_BOX_BORDER, fc);
    }
    boxBatch.flush(renderer);

    return;
}