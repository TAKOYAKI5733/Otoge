#pragma once

#include "GameCommon.h"
#include "Play/type.h"
#include "Play/GameContext.h"
#include "Play/roundedRect.h"
#include "Play/tutorialOverlay.h"
#include "Play/visualizer.h"
#include "Play/drawFrame.h"
#include "Play/LaneTilt.h"
#include "Play/PlayBackground.h"
#include "Play/FalseNotes.h"      // false ノーツ（見た目だけのノーツ）
#include "Play/BSpeedEvent.h"     // B_SpeedEvent：スクロール位置 S(t)
#include "Play/GlitchLayer.h"      // 始まりの演出：レーンをグリッチで出す   // 背景：藍色を含んだ灰色 ＋ 真ん中にぼかしたジャケット

constexpr SDL_Color kPerfectColor = {250, 250, 150, 255};
constexpr SDL_Color kShadowColor  = {0, 0, 0, 0};
constexpr float kShadowSpread = 0.0f;

// ===== 判定文字のポップ =====
inline constexpr double JUDGE_POP_MS    = 120.0;   // 出てから元の大きさに収まるまで
inline constexpr double JUDGE_POP_AMOUNT = 0.15;    // 出た瞬間の大きさ（元の何倍か）

// ===== 判定文字のホログラム =====
//   ・半透明：全体の濃さを JUDGE_HOLO_ALPHA にし、ほんの少し明るさをゆらす
//   ・走査線：文字を JUDGE_HOLO_LINE px の横の帯に分けて貼り、1 本おきに薄くする（帯はゆっくり下へ流れる）
//     → 線は文字の上にだけ出る（文字の無い所に線は描かれない）
//   ・グリッチ：JUDGE_HOLO_GLITCH_SLOT_MS ごとに JUDGE_HOLO_GLITCH_CHANCE の確率で、
//     文字の一部の帯が左右に数 px ずれ、赤と青の色ずれが重なる。出た直後の一瞬（JUDGE_HOLO_SPAWN_MS）も色ずれを出す
inline constexpr double JUDGE_HOLO_ALPHA          = 0.78;
inline constexpr int    JUDGE_HOLO_LINE           = 3;      // 走査線の帯の高さ(元の文字の px)
inline constexpr double JUDGE_HOLO_LINE_DIM       = 0.55;   // 薄い方の帯の濃さ（濃い方を 1 として）
inline constexpr double JUDGE_HOLO_LINE_SPEED     = 8.0;    // 走査線が流れる速さ(px/秒)
inline constexpr Uint32 JUDGE_HOLO_GLITCH_SLOT_MS = 70;
inline constexpr double JUDGE_HOLO_GLITCH_CHANCE  = 0.05;   // だいたい 1.4 秒に 1 回、70ms だけ
inline constexpr double JUDGE_HOLO_SPAWN_MS       = 60.0;

inline double judgeHoloHash(uint32_t x){
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return (x & 0xFFFFFF) / static_cast<double>(0x1000000);
}

// tex（元の大きさ srcW × srcH）を dst に、ホログラムの見た目で描く。sinceSpawnMs = 出てからの時間
inline void drawHoloJudgeText(SDL_Renderer* r, SDL_Texture* tex, int srcW, int srcH, const SDL_Rect& dst, double sinceSpawnMs){
    if(!tex || srcW <= 0 || srcH <= 0) return;
    const Uint32 ticks = SDL_GetTicks();
    const double t = ticks / 1000.0;
    const double flick = 0.93 + 0.07 * std::sin(t * 11.0) * std::sin(t * 3.7);
    const double baseA = 255.0 * JUDGE_HOLO_ALPHA * flick;
    const double sy = static_cast<double>(dst.h) / srcH;

    // グリッチ：ずらす帯（元の文字の y の範囲）と、ずらす量
    const uint32_t slot = ticks / JUDGE_HOLO_GLITCH_SLOT_MS;
    const bool glitch = judgeHoloHash(slot * 2654435761u + 17u) < JUDGE_HOLO_GLITCH_CHANCE;
    const bool spawnSplit = sinceSpawnMs < JUDGE_HOLO_SPAWN_MS;
    int gy0 = 0, gy1 = 0, gShift = 0;
    if(glitch){
        const int gh = std::max(2, static_cast<int>(srcH * (0.15 + 0.2 * judgeHoloHash(slot * 31u + 3u))));
        gy0 = static_cast<int>((srcH - gh) * judgeHoloHash(slot * 31u + 5u));
        gy1 = gy0 + gh;
        gShift = (judgeHoloHash(slot * 31u + 7u) < 0.5 ? -1 : 1) * (4 + static_cast<int>(judgeHoloHash(slot * 31u + 9u) * 8.0));
    }

    // 色ずれ（赤を左・青を右に、うすく重ねる）
    if(glitch || spawnSplit){
        const int split = glitch ? 4 : 3;
        const Uint8 ga = static_cast<Uint8>(baseA * 0.35);
        SDL_SetTextureAlphaMod(tex, ga);
        SDL_SetTextureColorMod(tex, 255, 60, 90);
        SDL_Rect d1 = {dst.x - split, dst.y, dst.w, dst.h};
        SDL_RenderCopy(r, tex, nullptr, &d1);
        SDL_SetTextureColorMod(tex, 60, 170, 255);
        SDL_Rect d2 = {dst.x + split, dst.y, dst.w, dst.h};
        SDL_RenderCopy(r, tex, nullptr, &d2);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
    }

    // 本体：横の帯に分けて貼る（1 本おきに薄く＝走査線。帯の区切りはゆっくり下へ流れる）
    const int off = static_cast<int>(std::fmod(t * JUDGE_HOLO_LINE_SPEED, JUDGE_HOLO_LINE * 2.0));
    for(int y = -off; y < srcH; y += JUDGE_HOLO_LINE){
        const int y0 = std::max(0, y), y1 = std::min(srcH, y + JUDGE_HOLO_LINE);
        if(y1 <= y0) continue;
        const bool dim = (((y + off) / JUDGE_HOLO_LINE) & 1) != 0;
        SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(baseA * (dim ? JUDGE_HOLO_LINE_DIM : 1.0)));
        const int dx = (glitch && y0 < gy1 && y1 > gy0) ? gShift : 0;
        SDL_Rect src = {0, y0, srcW, y1 - y0};
        const int dy0 = dst.y + static_cast<int>(std::lround(y0 * sy));
        const int dy1 = dst.y + static_cast<int>(std::lround(y1 * sy));
        SDL_Rect d = {dst.x + dx, dy0, dst.w, std::max(1, dy1 - dy0)};
        SDL_RenderCopy(r, tex, &src, &d);
    }
    SDL_SetTextureAlphaMod(tex, 255);
}

// 終わりで少し行き過ぎてから戻るイージング（1.0 を一度超えて 1.0 に収まる）
inline double easeOutBack(double t){
    const double c1 = 1.70158;
    const double c3 = c1 + 1.0;
    return 1.0 + c3 * std::pow(t - 1.0, 3.0) + c1 * std::pow(t - 1.0, 2.0);
}

// ===== 同時押しの補助線 =====
inline constexpr float     SYNC_LINE_THICKNESS = 6.0f;                 // 線の太さ(px)
inline constexpr SDL_Color SYNC_LINE_COLOR     = {255, 255, 255, 160};  // 線の色（半透明の白）


inline constexpr float     SYNC_FRAME_WIDTH = 5.0f;                    // 枠の太さ（隙間込み）
inline constexpr float     SYNC_FRAME_GAP   = 2.0f;                    // ヘッドと枠の間の暗い隙間
inline constexpr SDL_Color SYNC_FRAME_COLOR = {255, 190, 30, 0};     // 濃い金色
inline constexpr SDL_Color SYNC_FRAME_GAP_COLOR = {20, 20, 30, 0};   // 隙間の色    // ← 追加：Drag より淡い黄色

// ===== 隠れているレーンのノーツ・ロングの芯の形 =====
//   HIDDEN_NOTE_* と LONG_CORE_* は、チュートリアルのノーツの図（tutorialOverlay.h）でも同じ見た目にするため、
//   Play/drawFrame.h に移した（ノーツの形の値は drawFrame.h にまとめてある）

// ===== ロングを押せているときの、芯の光るアニメーション（控えめ） =====
inline constexpr double LONG_GLOW_PERIOD_MS   = 420.0;   // 明るさが脈打つ周期（長いほどゆったり）
inline constexpr float  LONG_GLOW_SPREAD      = 4.0f;    // 芯のまわりの光の広がり(px、片側)
inline constexpr Uint8  LONG_GLOW_ALPHA_MIN   = 20;      // まわりの光の濃さ（暗いとき）
inline constexpr Uint8  LONG_GLOW_ALPHA_MAX   = 50;      // まわりの光の濃さ（明るいとき）
inline constexpr Uint8  LONG_CORE_WHITEN      = 120;      // 明るいとき、芯をどれだけ白っぽくするか（0 = シアンのまま）
//   ※ 芯の中を流れる光は使わないことにしたので、処理ごと消した（脈打つ光とまわりの光だけ）

// ===== ノーツの重なりの順番（レイヤー） =====
//   奥 → 手前： ロング ＜ ドラッグ ＜ 単発（通常・通常C） ＜ レーン
//   ・ロングどうしは、後から来る（時刻が遅い）ロングほど手前。ロング1本ぶん（帯・芯・光・ヘッド）を
//     まとめて重ねるので、前のロングの帯が後のロングのヘッドや芯に被らない
//   ・ドラッグ・単発・レーンは、どのロングよりも手前に出る
//   レイヤーごとに RoundedRectBatch を1つ持ち、奥のレイヤーから順に描く。
//   1つの RoundedRectBatch の中は「足した順」に重なる（1回の SDL_RenderGeometry は三角形を順番に描く）ので、
//   ノーツを時刻の順（ctx.notes の並び）に足せば、後から来るものが手前になる
enum NoteLayer{ LAYER_LONG, LAYER_DRAG, LAYER_NORMAL, LAYER_LANE, LAYER_COUNT };

// ===== レーンを傾けたときの、ノーツの立体感 =====
//   傾けているときだけ、ノーツを「厚みのある板」に見せる（傾けていなければ今まで通り）
//     ・側面：ヘッドの手前（画面の下側）に、ノーツの色を暗くした細い帯を付ける → 厚みがあるように見える
//     ※ ノーツの影（レーンに落とす影）は付けないことにした
//   傾きに合わせて強くなる（NOTE_3D_FULL_DEG で最大。傾き 0 なら何も付かない）
//   値は「判定ラインでの px」。奥のノーツは、ノーツの太さと同じだけ伸び縮みする
inline constexpr float NOTE_3D_FULL_DEG      = 45.0f;   // この角度以上で、立体感が一番強くなる
inline constexpr float NOTE_3D_DEPTH_PX      = 5.0f;    // 側面の厚み
inline constexpr float NOTE_3D_SIDE_DARKEN   = 0.45f;   // 側面の明るさ（ノーツの色の何倍か）

// ===== 破裂エフェクトの、重なった四角 =====
//   判定ラインで、大きさの違う 3 つの四角（ひし形）が回りながら広がって消える
//     ・大きい四角ほど濃く、小さくなるほど透明になる（BURST_SQ_ALPHA）
//     ・傾けているときは、大きい四角ほど高く浮かせる（BURST_SQ_LIFT_PX だけ奥へずらす）
//       ＋ 一番大きい四角の影をレーンに落とす → 重なった板が浮いているように見える
//   四角はレーンの面に置いた形で描くので、傾けると奥に倒れて見える（レーンやノーツと同じ見え方）
inline constexpr int   BURST_SQ_COUNT = 3;
inline constexpr float BURST_SQ_SIZE[BURST_SQ_COUNT]    = {1.00f, 0.66f, 0.40f};   // 大きさ（一番大きい四角に対する割合）
inline constexpr float BURST_SQ_ALPHA[BURST_SQ_COUNT]   = {1.00f, 0.55f, 0.28f};   // 濃さ（小さいほど透明）
inline constexpr float BURST_SQ_LIFT_PX[BURST_SQ_COUNT] = {26.0f, 13.0f, 4.0f};    // 傾けたときに浮かせる高さ(px、判定ラインで)
inline constexpr float BURST_SQ_SPIN[BURST_SQ_COUNT]    = {0.35f, -0.55f, 0.80f};  // 消えるまでに回る角度(ラジアン)。向きを交互に
inline constexpr float BURST_SQ_THICK[BURST_SQ_COUNT]   = {3.0f, 2.5f, 2.0f};      // 線の太さ(px)
inline constexpr float BURST_SQ_START = 0.50f;   // 出た瞬間の大きさ（ノーツの幅の何倍か）
inline constexpr float BURST_SQ_GROW  = 0.55f;   // 消えるまでに、さらにノーツの幅の何倍ぶん広がるか
inline constexpr Uint8 BURST_SQ_SHADOW_A = 70;   // 一番大きい四角の影の濃さ

// ===== ロングを押している間の、回り続ける四角 =====
//   始点の破裂のうち「重なった四角」だけを残して、押している間ずっと判定ラインの所で回し続ける
//   ・回る速さは一定（等速）。下の（低い・小さい）四角ほど速く回る。向きは破裂と同じく交互
//   ・押し始めは破裂と同じ大きさから少し広がって止まる（破裂の四角がそのまま残ったように見える）
//   ・傾けているときは、破裂と同じく大きい四角ほど高く浮かせ、一番大きい四角の影をレーンに落とす
inline constexpr float HOLD_SQ_SPEED[BURST_SQ_COUNT] = {1.2f, 1.9f, 2.8f};   // 回る速さ(ラジアン/秒)。[0] = 一番大きい（一番上）
inline constexpr float HOLD_SQ_GROW   = 0.45f;   // 押し始めから広がる量（BURST_SQ_GROW の何倍まで広がって止まるか）
inline constexpr double HOLD_SQ_GROW_MS = 220.0; // 広がりきるまでの時間
inline constexpr float HOLD_SQ_ALPHA  = 0.85f;   // 濃さ（破裂の四角の濃さの何倍か）

// ===== レーンの左右の傾き（ロール） =====
//   レーンの真ん中の縦の線（x = レーンの中心）を回転の軸にして、レーン全体を奥行きのある形で左右に傾ける。
//   傾いて奥に行く側は小さく、手前に来る側は大きく見える（遠近）。判定ラインはまっすぐ横のまま。
//     奥行き  z  = (x − 中心) × sin(角度)          … + が奥
//     縮み    s  = C ÷ (C + z)                       … 奥ほど小さい（C = 視点までの距離）
//     横      X  = 中心 + (x − 中心) × cos(角度) × s
//     縦      Y  = 判定ライン + (y − 判定ライン) × s … 判定ラインの高さは変わらない
//
//   ■ ギザギザしないための描き方
//     レーンを描いた絵（laneLayerTex）を、横 LANE_ROLL_COLUMNS × 縦 LANE_ROLL_ROWS の細かいマス目に切って、
//     マス目の角の点を上の式で動かして貼る（SDL_RenderGeometry）。
//     ・前は縦の帯だけに切っていた。帯 1 本は台形なのに、GPU は三角形 2 枚に分けてまっすぐ（比例で）貼るので、
//       三角形の境目（帯の対角線）で横の線が折れ曲がり、帯ごとにギザギザが出ていた。
//       縦にも細かく切ると 1 マスがとても小さくなり、折れ曲がりが見えないほど小さくなる
//     ・絵の色は、まわりの 4 点の色を混ぜて拾う（ScaleModeLinear）。斜めになった線や端がなめらかになる
inline constexpr int    LANE_ROLL_COLUMNS = 64;       // 横の分け方（多いほどなめらか）
inline constexpr int    LANE_ROLL_ROWS    = 32;       // 縦の分け方（多いほど横の線がまっすぐ）
inline constexpr double LANE_ROLL_CAMERA  = 1400.0;   // 視点までの距離 C(px)。小さいほど遠近が強い

//   tex は上下に margin ずつ余分に描いた絵（画面の y = -margin 〜 SCREEN_H + margin が入っている）
inline void drawRolledLayer(SDL_Renderer* r, SDL_Texture* tex, double angleDeg, double centerX, double pivotY, double margin){
    constexpr int N = LANE_ROLL_COLUMNS, M = LANE_ROLL_ROWS;
    static SDL_Vertex v[(N + 1) * (M + 1)];
    static int idx[N * M * 6];
    static bool idxReady = false;
    if(!idxReady){
        // マス目 1 つ = 三角形 2 枚。点の番号 = 行 × (N + 1) + 列
        int k = 0;
        for(int j = 0; j < M; j++){
            for(int i = 0; i < N; i++){
                const int a = j * (N + 1) + i, b = a + 1, c = a + (N + 1), d = c + 1;
                idx[k++] = a; idx[k++] = b; idx[k++] = c;
                idx[k++] = c; idx[k++] = b; idx[k++] = d;
            }
        }
        idxReady = true;
    }
    const double ang = angleDeg * 3.14159265358979 / 180.0;
    const double sn = std::sin(ang), cs = std::cos(ang);
    const double yMin = -margin, yMax = SCREEN_H + margin;
    for(int i = 0; i <= N; i++){
        const double x = static_cast<double>(SCREEN_W) * i / N;
        const double z = (x - centerX) * sn;
        const double s = LANE_ROLL_CAMERA / (LANE_ROLL_CAMERA + z);
        const float X  = static_cast<float>(centerX + (x - centerX) * cs * s);
        const float u  = static_cast<float>(i) / N;
        for(int j = 0; j <= M; j++){
            const double y = yMin + (yMax - yMin) * j / M;
            const float Y  = static_cast<float>(pivotY + (y - pivotY) * s);
            v[j * (N + 1) + i] = {SDL_FPoint{X, Y}, SDL_Color{255, 255, 255, 255}, SDL_FPoint{u, static_cast<float>(j) / M}};
        }
    }
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
    SDL_RenderGeometry(r, tex, v, (N + 1) * (M + 1), idx, N * M * 6);
}

// ===== ロングの帯を描く範囲 =====
//   長いロングを速いノーツ速度で流すと、終点が描き先の何十万 px も上になることがある。
//   見えない所まで帯・芯・光を作ると、とても重くなる（特に流れる光は、長さに比例して数が増える）ので、
//   描き先の上の端（y = 0）より LONG_CLIP_MARGIN だけ上で切る。切った所は画面の外なので見た目は変わらない
inline constexpr float  LONG_CLIP_MARGIN      = 200.0f;

// ===== 判定文字の上のコンボ数 =====
inline constexpr double JUDGE_COMBO_SCALE      = 1.3;    // 大きさ（数字の元の大きさの何倍か。元は 80px）
inline constexpr double JUDGE_COMBO_SLIDE_MS     = 120.0;   // 下から上がりきるまでの時間
inline constexpr int    JUDGE_COMBO_SLIDE_PX     = 18;      // どれだけ下から上がってくるか(px)
inline constexpr double JUDGE_COMBO_SLIDE_FADE   = 0.4;     // 上がり始めの濃さ（いつもの濃さの何倍か）
inline constexpr Uint8  JUDGE_COMBO_ALPHA      = 90;    // 透明度（0 = 見えない、255 = はっきり）
inline constexpr int    JUDGE_COMBO_Y          = 300;    // 縦の中心の位置(px)。判定文字は 540 あたり
inline constexpr int    JUDGE_COMBO_LABEL_GAP  = 0;

// ===== キービーム =====
//   長さは「判定ラインから、画面で見えているレーンの奥の端まで」の何割か で決める。
//   傾けていないときは、判定ライン(y = 810)から画面の上(y = 0)までの 810px が「見えているレーン」なので、
//   0.62 × 810 ≒ 500px（今までの長さ）になる。傾けても、画面の上では同じ割合の所まで届く
//   割合は設定（ノーツの詳細設定の「キービームの長さ」）で変えられ、ctx.keyBeamRatio で渡される

// ===== レーンカバー（レーンの奥を隠す壁） =====
//   設定の「レーンカバー」の % だけ、画面で見えているレーンの奥側を壁でふさぐ。
//   ノーツは壁の下の端から急に現れるように見える（見える時間が短くなる＝難しくなる）
inline constexpr SDL_Color LANE_COVER_COLOR      = {14, 14, 24, 255};    // 壁の色（不透明）
inline constexpr SDL_Color LANE_COVER_EDGE_COLOR = {0, 200, 255, 255};   // 壁の下の端の線の色
inline constexpr float     LANE_COVER_EDGE_H     = 4.0f;                 // 下の端の線の太さ(px、判定ラインでの太さ)
inline constexpr float     LANE_COVER_SIDE       = 4.0f;                 // 壁をレーンの左右の端よりどれだけ広くするか(px)      // 「COMBO」とコンボ数の間(px)。離れて見えるときはマイナスに

// スクロール位置 S(t)（Play/BSpeedEvent.h）。B_SpeedEvent がない譜面では t のまま
//   ノーツの位置 = 判定ライン − (S(ノーツの時刻) − S(今)) × 速さ
//   ・B_SpeedEvent がない間は S(t) = t なので、前の「座標を求める式」(ノーツの時刻 − 今) × 速さ と同じ
//   ・B_SpeedEvent の倍率が 0 の間は S(今) が増えないので、ノーツが全部その場で止まる
inline double scrollAt(const GameContext& ctx, double t){
    return ctx.scrollMap ? ctx.scrollMap->at(t) : t;
}

// ノーツの中心の y 座標を求める（ノーツ本体の描画と補助線で共通に使う）
inline int computeNoteY(const GameContext& ctx, const Note& note, double currentNoteSpeed){
    const double t = ctx.musicTimeExact + ctx.visualOffsetMs;   // 表示用の時刻（判定には使わない）
    double effectiveSpeed = note.hasCustomSpeed ? note.customSpeed : currentNoteSpeed;
    return ctx.judgeY - static_cast<int>((scrollAt(ctx, static_cast<double>(note.targetTime)) - scrollAt(ctx, t)) * effectiveSpeed);
}

// 太さのある線を描く（2点を結ぶ細長い四角形を三角形2枚で描く）
inline void drawThickLine(SDL_Renderer* renderer, float x1, float y1, float x2, float y2,
                          float thickness, SDL_Color color){
    // レーンを傾けているときは、両端を変換して、縁のなめらかな線で描く（Play/TiltProjection.h）
    if(g_tiltProj){
        tiltLine(renderer, x1, y1, x2, y2, thickness, color);
        return;
    }
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if(len < 0.5f) return;

    // 線に垂直な方向へ、太さの半分だけずらした4点を作る
    float nx = -dy / len * thickness * 0.5f;
    float ny =  dx / len * thickness * 0.5f;

    SDL_Vertex v[4] = {
        {{x1 + nx, y1 + ny}, color, {0.0f, 0.0f}},
        {{x2 + nx, y2 + ny}, color, {0.0f, 0.0f}},
        {{x2 - nx, y2 - ny}, color, {0.0f, 0.0f}},
        {{x1 - nx, y1 - ny}, color, {0.0f, 0.0f}},
    };
    int indices[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, nullptr, v, 4, indices, 6);
}

// 回った正方形の枠（中継点のエフェクト用）
inline void drawRotatedSquareFrame(SDL_Renderer* r, float cx, float cy, float size, float angle,
                                   float thickness, SDL_Color c){
    const float h = size * 0.5f;
    const float cs = std::cos(angle), sn = std::sin(angle);
    float px[4], py[4];
    const float lx[4] = {-h,  h, h, -h};
    const float ly[4] = {-h, -h, h,  h};
    for(int k = 0; k < 4; k++){
        px[k] = cx + lx[k] * cs - ly[k] * sn;
        py[k] = cy + lx[k] * sn + ly[k] * cs;
    }
    for(int k = 0; k < 4; k++){
        const int n = (k + 1) % 4;
        drawThickLine(r, px[k], py[k], px[n], py[n], thickness, c);
    }
}

// 回った正方形を塗りつぶす（中継点のエフェクト用）
//   4つの角を求めて、三角形2枚で塗る
inline void drawRotatedSquareFilled(SDL_Renderer* r, float cx, float cy, float size, float angle, SDL_Color c){
    const float h = size * 0.5f;
    const float cs = std::cos(angle), sn = std::sin(angle);
    const float lx[4] = {-h,  h, h, -h};
    const float ly[4] = {-h, -h, h,  h};
    SDL_Vertex v[4];
    for(int k = 0; k < 4; k++){
        v[k].position  = {cx + lx[k] * cs - ly[k] * sn, cy + lx[k] * sn + ly[k] * cs};
        v[k].color     = c;
        v[k].tex_coord = {0.0f, 0.0f};
    }
    tiltApply(v, 4);   // レーンを傾けているときは、4つの角を傾けた位置に変換する（傾けていなければ何もしない）
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}

// 同じ時刻・別レーンのノーツどうしを線でつなぐ
// outIsSync には、線でつないだノーツの番号（ctx.notes の添字）に 1 が入る
inline void drawSyncLines(GameContext& ctx, double currentNoteSpeed, int laneX[6], std::vector<char>& outIsSync){
    struct Point{ float x; float left; float right; float y; size_t index; };   // x = 真ん中、left / right = ノーツの左右の縁
    std::vector<Point> group;
    group.reserve(6);

    const size_t n = ctx.notes.size();
    outIsSync.assign(n, 0);

    const double visualTime = ctx.musicTimeExact + ctx.visualOffsetMs;   // 表示用の時刻

    size_t i = 0;
    while(i < n){
        // 1. targetTime が同じ連続部分 [i, j) をひとまとまりにする
        const int32_t t = ctx.notes[i].targetTime;
        size_t j = i;
        group.clear();

        for(; j < n && ctx.notes[j].targetTime == t; j++){
            const Note& note = ctx.notes[j];
            if(note.lane < 0 || note.lane >= 6) continue;
            if(note.isHit && visualTime >= note.hitTime) continue;   // 叩いた見た目になったノーツは線から外す

            int y = computeNoteY(ctx, note, currentNoteSpeed);
            // 描き先の外は描かない。下の端は「判定ライン + 画面の残り(SCREEN_H / 4)」で決める
            //   （レーンを傾けるときは ctx.judgeY が下にずれるので、下の端も一緒にずれる）
            if(y < -50 || y > ctx.judgeY + SCREEN_H / 4 + 50) continue;

            const float laneL = static_cast<float>(laneX[note.lane]);
            const float laneW = static_cast<float>(ctx.laneWidth * note.widthLanes);
            group.push_back({laneL + laneW * 0.5f, laneL + NOTE_SIDE_MARGIN, laneL + laneW - NOTE_SIDE_MARGIN,
                             static_cast<float>(y), j});
        }

        // 2. 2つ以上残っていれば、左から順に隣どうしを結び、結んだノーツに印を付ける
        if(group.size() >= 2){
            std::sort(group.begin(), group.end(),
                      [](const Point& a, const Point& b){ return a.x < b.x; });

            for(size_t k = 0; k + 1 < group.size(); k++){
                if(group[k + 1].x - group[k].x < 1.0f) continue;   // 同じレーンどうしは結ばない
                // 線は、左のノーツの右の縁から、右のノーツの左の縁まで（ノーツの上を横切らない）
                //   となりのレーンどうしで縁と縁がくっついている（すき間が無い）ときは、線は引かずに印だけ付ける
                const float x0 = group[k].right;
                const float x1 = group[k + 1].left;
                if(x1 - x0 > 1.0f){
                    drawThickLine(ctx.renderer, x0, group[k].y, x1, group[k + 1].y, SYNC_LINE_THICKNESS, SYNC_LINE_COLOR);
                }
                outIsSync[group[k].index]     = 1;
                outIsSync[group[k + 1].index] = 1;
            }
        }

        i = j;   // 次の時刻のグループへ
    }
}

// 押したレーンの判定ボックスを埋める（長押し中は埋まったまま）
inline void drawJudgeBoxFx(GameContext& ctx, int laneX[6], JudgeBoxFx& fx, float headH = NOTE_HEAD_H){
    const double now = ctx.musicTimeExact;
    fx.update(ctx.currentPressed, now);

    const float judgeY = static_cast<float>(ctx.judgeY);

    RoundedRectBatch fillBatch;
    for(int i = 0; i < 6; i++){

        double lv = fx.level(i, now);
        if(lv <= 0.001) continue;

        const float noteX = static_cast<float>(laneX[i]) + JUDGE_BOX_SIDE_MARGIN;
        const float noteW = static_cast<float>(ctx.laneWidth) - JUDGE_BOX_SIDE_MARGIN * 2.0f;
        const float noteY = judgeY - headH * 0.5f;

        // 長押し中は枠線もシアンにして「押さえている」ことを示す
        if(fx.held[i]){
            fillBatch.addOutline(noteX, noteY, noteW, headH, NOTE_RADIUS, JUDGE_BOX_BORDER, JUDGE_FILL_COLOR);
        }

        // 枠の内側を埋める
        const float b = JUDGE_BOX_BORDER;
        SDL_Color c = JUDGE_FILL_COLOR;
        c.a = static_cast<Uint8>(std::clamp(lv, 0.0, 1.0) * 230);
        fillBatch.add(noteX + b, noteY + b, noteW - b * 2.0f, headH - b * 2.0f, std::max(0.0f, NOTE_RADIUS - b), c);
    }
    fillBatch.flush(ctx.renderer);
}

// =====================================================================
//  ノーツを取ったとき・ロングの中継点で出る、半透明の立方体
//
//  ■ 動き（cubeStateAt）　※ 今は落ちている途中でテレポートに移るので、跳ね返りまでは行かない（テレポートの位置を求めるのに使う）：出てからの時間 t から、毎フレーム位置を計算する（毎フレーム足していく方法と違い、fps に関係なく同じ動き）
//    ・上へ飛び出し、重力 CUBE_GRAVITY で落ちる：y = vy·t + ½·g·t²
//    ・床（判定ボックスの下の端）に着いたら、そのときの速さの CUBE_BOUNCE 倍で上へ跳ね返る。
//      床に着く時刻は ½·g·τ² + v·τ − 落ちる高さ = 0 を τ について解いて求める
//    ・跳ね返るたびに、横の速さと回る速さを CUBE_FRICTION 倍にする（だんだん落ち着く）
//    ・CUBE_MAX_BOUNCES 回跳ねたか、跳ね返る速さがほぼ 0 になったら床に止まる
//
//  ■ 形（drawCube）：本当の立方体の 8 つの角を 3 軸で回し、画面に写す（遠近なし）
//    ・面の外向きの向き（法線）が画面の手前を向いている面だけ描く（立方体はへこみの無い形なので、見える面どうしは重ならない）
//    ・面の色 = レーンの色 ＋ 光（左上・手前から当たる光に向いているほど明るい）。半透明（CUBE_FACE_ALPHA）
//    ・辺はホログラムの色（判定枠と同じ）。12 本の辺のうち、となりの面のどちらかが見えている辺だけ描く
//    ・傾けているときは、角の点を傾けた位置に動かして描く（tiltApply / drawThickLine）
// =====================================================================
inline constexpr Uint8  CUBE_FACE_ALPHA  = 55;                     // 面の濃さ（枠より透明な塗りつぶし）
inline constexpr Uint8  CUBE_EDGE_ALPHA  = 190;                    // 辺の濃さ
inline constexpr float  CUBE_EDGE_W      = 1.5f;                   // 辺の太さ(px)
inline constexpr SDL_Color CUBE_LIGHT    = {70, 85, 110, 255};     // 光で足す色（面がレーンの色だけだと暗すぎて見えないため）
inline constexpr double CUBE_POP_MS      = 60.0;                   // 出てすぐ、この時間で 0 → 本来の大きさになる

struct CubeState{ float x, y; float rx, ry, rz; };

inline CubeState cubeStateAt(const TickParticle& p, double t){
    const double G = CUBE_GRAVITY;
    double x = 0.0, y = 0.0, v = p.vy, vx = p.vx, rem = t;
    double spinT = 0.0, spinK = 1.0;      // 回った量 = 回る速さ × spinT
    double drop = p.floorDy;              // 今の位置から床までの高さ（下がプラス）
    bool resting = false;
    for(int b = 0; b <= CUBE_MAX_BOUNCES; b++){
        const double tau = (-v + std::sqrt(std::max(0.0, v * v + 2.0 * G * drop))) / G;   // 床に着くまでの時間
        if(rem <= tau){
            y += v * rem + 0.5 * G * rem * rem;
            x += vx * rem;
            spinT += spinK * rem;
            rem = 0.0;
            break;
        }
        y += drop;                       // 床に着いた
        x += vx * tau;
        spinT += spinK * tau;
        rem -= tau;
        v = -(v + G * tau) * CUBE_BOUNCE;   // 跳ね返る（上向き = マイナス）
        vx *= CUBE_FRICTION;
        spinK *= CUBE_FRICTION;
        drop = 0.0;
        if(std::abs(v) < 0.02 || b == CUBE_MAX_BOUNCES){ resting = true; break; }
    }
    (void)resting;   // 止まったあとは床の上（y はそのまま）
    return {p.x + static_cast<float>(x), p.y + static_cast<float>(y),
            p.rotX + static_cast<float>(p.spinX * spinT),
            p.rotY + static_cast<float>(p.spinY * spinT),
            p.rotZ + static_cast<float>(p.spinZ * spinT)};
}

//   sx, sy：回した後に横・縦へ伸ばす倍率（テレポートで細長くなるとき）
//   edge：辺の色　faceMul：面の濃さの倍率（0 で面を描かない）
inline void drawCube(SDL_Renderer* r, float cx, float cy, float size, float ax, float ay, float az,
                     SDL_Color base, float alpha, float sx = 1.0f, float sy = 1.0f,
                     SDL_Color edge = HOLO_FRONT_COLOR, float faceMul = 1.0f){
    const float h = size * 0.5f;
    const float cxs = std::cos(ax), sxs = std::sin(ax);
    const float cys = std::cos(ay), sys = std::sin(ay);
    const float czs = std::cos(az), szs = std::sin(az);
    // 回す：X 軸 → Y 軸 → Z 軸の順
    auto rot = [&](float x, float y, float z, float& ox, float& oy, float& oz){
        float y1 = y * cxs - z * sxs, z1 = y * sxs + z * cxs;          // X 軸
        float x2 = x * cys + z1 * sys, z2 = -x * sys + z1 * cys;       // Y 軸
        ox = x2 * czs - y1 * szs; oy = x2 * szs + y1 * czs; oz = z2;   // Z 軸
    };

    // 8 つの角（番号の bit0 = x、bit1 = y、bit2 = z が + 側か）
    float px[8], py[8];
    for(int i = 0; i < 8; i++){
        float X, Y, Z;
        rot((i & 1) ? h : -h, (i & 2) ? h : -h, (i & 4) ? h : -h, X, Y, Z);
        px[i] = cx + X * sx;
        py[i] = cy + Y * sy;   // 画面の y は下がプラス
    }

    // 6 つの面：番号 = 軸 × 2 + (+側なら 1)。角は 4 つ（ぐるっと一周の順）
    static constexpr int FACE[6][4] = {
        {0, 2, 6, 4}, {1, 3, 7, 5},   // −x, +x
        {0, 1, 5, 4}, {2, 3, 7, 6},   // −y（上）, +y（下）
        {0, 1, 3, 2}, {4, 5, 7, 6},   // −z（手前）, +z（奥）
    };
    // 光の来る向き（左・上・手前。画面の y は下がプラス、z は奥がプラス）
    const float L[3] = {-0.45f, -0.70f, -0.55f};
    const float Ln = 1.0f / std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);

    bool visible[6] = {};
    SDL_Vertex v[4 * 6];
    int idx[6 * 6];
    int nv = 0, ni = 0;
    for(int f = 0; f < 6; f++){
        const int axis = f / 2;
        const float sgn = (f % 2) ? 1.0f : -1.0f;
        float n[3];
        rot(axis == 0 ? sgn : 0.0f, axis == 1 ? sgn : 0.0f, axis == 2 ? sgn : 0.0f, n[0], n[1], n[2]);
        if(n[2] >= -0.02f) continue;   // 奥を向いている面は見えない
        visible[f] = true;

        const float lit = std::max(0.0f, (n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) * Ln);
        const float shade = 0.25f + 0.75f * lit;
        const SDL_Color c = {
            static_cast<Uint8>(std::min(255.0f, base.r + CUBE_LIGHT.r * shade)),
            static_cast<Uint8>(std::min(255.0f, base.g + CUBE_LIGHT.g * shade)),
            static_cast<Uint8>(std::min(255.0f, base.b + CUBE_LIGHT.b * shade)),
            static_cast<Uint8>(std::clamp(CUBE_FACE_ALPHA * alpha * faceMul, 0.0f, 255.0f))};
        for(int k = 0; k < 4; k++){
            v[nv + k] = {SDL_FPoint{px[FACE[f][k]], py[FACE[f][k]]}, c, SDL_FPoint{0.0f, 0.0f}};
        }
        const int q[6] = {0, 1, 2, 0, 2, 3};
        for(int k = 0; k < 6; k++) idx[ni + k] = nv + q[k];
        nv += 4;
        ni += 6;
    }
    if(nv == 0) return;
    if(faceMul > 0.0f){
        tiltApply(v, nv);   // 傾けているときは、角を傾けた位置へ
        SDL_RenderGeometry(r, nullptr, v, nv, idx, ni);
    }

    // 辺：12 本。角 a と、1 つの軸だけ違う角 b を結ぶ辺のとなりの面は「残り 2 つの軸の、a と同じ側の面」
    SDL_Color ec = edge;
    ec.a = static_cast<Uint8>(std::clamp(edge.a / 255.0f * CUBE_EDGE_ALPHA * alpha, 0.0f, 255.0f));
    for(int a = 0; a < 8; a++){
        for(int axis = 0; axis < 3; axis++){
            if(a & (1 << axis)) continue;          // −側の角から +側へ、の 1 回だけ数える
            const int b = a | (1 << axis);
            const int o1 = (axis + 1) % 3, o2 = (axis + 2) % 3;
            const int f1 = o1 * 2 + ((a >> o1) & 1);
            const int f2 = o2 * 2 + ((a >> o2) & 1);
            if(!visible[f1] && !visible[f2]) continue;
            drawThickLine(r, px[a], py[a], px[b], py[b], CUBE_EDGE_W, ec);
        }
    }
}

// =====================================================================
//  立方体のテレポート（落ちている途中で止まって、光の柱になって消える）
//
//  u = テレポートの進み具合（0〜1、CUBE_TELEPORT_MS で 1）
//    ・止まった位置で、横は細く（1 → 0）、縦は長く（1 → 2.5 倍）伸びていく → 光に吸い込まれるように消える
//    ・はじめの方は位置が小刻みにぶれて、赤と青の色ずれの枠が左右に出る（ホログラムのグリッチと同じ色）
//    ・立方体の中心から上へ、白っぽい水色の光の柱が伸びて、細くなりながら消える
//    ・横の細い線（走査線）が数本、立方体の所に一瞬出る
//  回る向きは止まった時の向きから、少し速く回り続ける
// =====================================================================
inline constexpr float CUBE_TP_STRETCH   = 1.5f;     // 縦にどれだけ伸びるか（1.5 = 最後に 2.5 倍）
inline constexpr float CUBE_TP_BEAM_LEN  = 220.0f;   // 光の柱の長さ(px)
inline constexpr Uint8 CUBE_TP_BEAM_ALPHA = 150;     // 光の柱の濃さ
inline constexpr float CUBE_TP_JITTER_PX = 3.0f;     // ぶれの大きさ(px)
inline constexpr float CUBE_TP_RGB_PX    = 3.0f;     // 色ずれの幅(px)

inline void drawCubeTeleport(SDL_Renderer* r, const TickParticle& p, float x, float y, float rx, float ry, float rz,
                             double u, double el){
    const float uf   = static_cast<float>(std::clamp(u, 0.0, 1.0));
    const float ein  = uf * uf;                                  // だんだん速く（細くなる・伸びる）
    const float eout = 1.0f - (1.0f - uf) * (1.0f - uf);         // はじめ速く（柱が伸びる）
    const float sx = 1.0f - ein;
    const float sy = 1.0f + CUBE_TP_STRETCH * ein;
    const float spin = static_cast<float>((el - p.teleportMs) * 0.02);   // 止まってからも少し速く回る
    const SDL_Color base = laneBaseColor(p.lane);
    const float a = p.alpha;

    // ぶれ（はじめの 6 割だけ。30ms ごとに向きが変わる）
    const uint32_t seed = static_cast<uint32_t>(el / 30.0) * 2654435761u + static_cast<uint32_t>(p.x * 7.0f);
    float jx = 0.0f;
    if(uf < 0.6f) jx = static_cast<float>(holoHash01(seed) * 2.0 - 1.0) * CUBE_TP_JITTER_PX * (1.0f - uf / 0.6f);

    // 光の柱（立方体の中心から上へ）
    const float beamW = p.size * 0.7f * (1.0f - uf) + 1.5f;
    const float beamH = CUBE_TP_BEAM_LEN * eout;
    SDL_Color bc = HOLO_FRONT_COLOR;
    bc.a = static_cast<Uint8>(CUBE_TP_BEAM_ALPHA * (1.0f - uf) * a);
    fillLaneRect(r, x - beamW * 0.5f, y - beamH, beamW, beamH, bc, 6);
    SDL_Color bc2 = HOLO_BACK_COLOR;   // 柱のまわりのにじみ（青く薄く、太め）
    bc2.a = static_cast<Uint8>(CUBE_TP_BEAM_ALPHA * 0.35f * (1.0f - uf) * a);
    fillLaneRect(r, x - beamW * 1.2f, y - beamH * 0.8f, beamW * 2.4f, beamH * 0.8f, bc2, 6);

    // 色ずれの枠（はじめの方だけ）
    if(uf < 0.5f){
        const float k = 1.0f - uf / 0.5f;
        drawCube(r, x + jx - CUBE_TP_RGB_PX, y, p.size, rx, ry + spin, rz, base, a * k, sx, sy, SDL_Color{255, 40, 60, 140}, 0.0f);
        drawCube(r, x + jx + CUBE_TP_RGB_PX, y, p.size, rx, ry + spin, rz, base, a * k, sx, sy, SDL_Color{40, 200, 255, 140}, 0.0f);
    }
    // 立方体本体（細く・長くなっていく）
    drawCube(r, x + jx, y, p.size, rx, ry + spin, rz, base, a * (1.0f - 0.5f * uf), sx, sy);

    // 走査線：立方体のまわりに横の細い線を 3 本
    for(int k = 0; k < 3; k++){
        const float off = static_cast<float>(holoHash01(seed + 31u * (k + 1)) * 2.0 - 1.0) * p.size * sy * 0.6f;
        const float w = p.size * (1.6f - uf);
        SDL_Color lc = HOLO_FRONT_COLOR;
        lc.a = static_cast<Uint8>(110 * (1.0f - uf) * a);
        fillLaneRect(r, x - w * 0.5f, y + off, w, 1.5f, lc, 1);
    }
}

// =====================================================================
//  左上の Cube の数（傾けない・回さない。画面にまっすぐ描く）
//    小さな立方体のアイコン（ゆっくり回る）＋「CUBE」＋ このプレイで集めた数（毎回 0 から）。増えた瞬間に数を少し大きくして戻す
//    持っている総数は、選曲画面の真ん中の立方体をクリックすると見られる
// =====================================================================
inline constexpr int    CUBE_HUD_X       = 60;      // アイコンの中心の x
inline constexpr int    CUBE_HUD_Y       = 60;      // アイコン・文字の中心の y
inline constexpr float  CUBE_HUD_ICON    = 34.0f;   // アイコンの大きさ(px)
inline constexpr double CUBE_HUD_NUM_SCALE = 0.5;   // 数字の大きさ（数字の画像 80pt の何倍か）
inline constexpr double CUBE_HUD_POP_MS  = 160.0;   // 増えたときの、数が大きくなって戻るまでの時間
inline constexpr double CUBE_HUD_POP_AMT = 0.25;    // そのときにどれだけ大きくするか

inline void drawCubeHud(GameContext& ctx, double visualTime){
    if(!ctx.cubeHud) return;
    const CubeHud& h = *ctx.cubeHud;
    SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_BLEND);

    // アイコン（g_tiltProj はここでは nullptr なので、そのまま画面に描かれる）
    const float t = static_cast<float>(SDL_GetTicks()) * 0.0012f;
    drawCube(ctx.renderer, static_cast<float>(CUBE_HUD_X), static_cast<float>(CUBE_HUD_Y), CUBE_HUD_ICON,
             0.5f + t * 0.4f, t, 0.3f, SDL_Color{60, 70, 95, 255}, 1.0f, 1.0f, 1.0f, HOLO_FRONT_COLOR, 2.0f);

    int x = CUBE_HUD_X + static_cast<int>(CUBE_HUD_ICON * 0.5f) + 16;
    if(h.label){
        SDL_SetTextureColorMod(h.label, HOLO_FRONT_COLOR.r, HOLO_FRONT_COLOR.g, HOLO_FRONT_COLOR.b);
        SDL_Rect dst = {x, CUBE_HUD_Y - h.labelH / 2, h.labelW, h.labelH};
        SDL_RenderCopy(ctx.renderer, h.label, nullptr, &dst);
        x += h.labelW + 14;
    }

    if(ctx.digitFont){
        char num[24];
        std::snprintf(num, sizeof(num), "%d", h.earned);   // このプレイで集めた数だけ（総数は選曲画面で）
        const double pt = std::clamp((visualTime - h.popMs) / CUBE_HUD_POP_MS, 0.0, 1.0);
        const double scale = CUBE_HUD_NUM_SCALE * (1.0 + CUBE_HUD_POP_AMT * (1.0 - pt) * (1.0 - pt));
        // drawCentered は中心で置くので、左をそろえるために幅を先に測る
        int w = 0;
        for(const char* c = num; *c; c++) w += ctx.digitFont->digitW[*c - '0'];
        const int cx = x + static_cast<int>(w * CUBE_HUD_NUM_SCALE * 0.5);
        ctx.digitFont->drawCentered(ctx.renderer, num, false, cx, CUBE_HUD_Y, scale, SDL_Color{255, 255, 255, 255});
    }
}

// =====================================================================
//  レーンの左右の端のビジュアライザー（1レーン目の枠の左・6レーン目の枠の右）
//
//  ■ 形
//    曲の周波数帯ごとの強さ（SpectrumVisualizer::levels、64 本。0 = 低音）を、
//    レーンの外側へ伸びる横の線にして縦に並べる。下が低音・上が高音（画面の端のビジュアライザーと同じ向き）。
//    左側は左へ、右側は右へ伸びる（レーンの枠から外へ広がる）。
//
//  ■ 色と演出（判定枠・区切り線と同じホログラム。drawFrame.h の HoloFx を使う）
//    ・一番手前は白っぽい水色（HOLO_FRONT_COLOR）。ゆらぎ（flicker）で明るさが少し揺れる
//    ・その奥（画面の上）に、青く薄い層を 2 枚ずらして重ねる → 光の線に厚みが出る。厚みは傾きに合わせて大きくなる
//    ・1 本おきに少し薄くする → 走査線のようなしま模様
//    ・グリッチ中は、8 本ずつの帯ごとに左右へずらし、赤と青の色ずれを薄く重ねる
//      （holoNow() を判定枠と一緒に使うので、グリッチは文字・判定枠・区切り線と同時に起きる）
//
//  ■ 傾き
//    レーンを描くのと同じ所（傾ける範囲・左右に傾ける範囲の中）で描くので、レーンと一緒に倒れて・傾く。
//    RoundedRectBatch は g_tiltProj を見て自分で点を変換するので、ここでは何もしなくてよい。
//    縦の並びは「画面に見えている範囲（上の端 〜 下の端）」に合わせる。傾けているときは unmapY で描く側の座標に戻す
// =====================================================================
inline constexpr float EDGE_VIS_GAP        = 6.0f;     // レーンの枠から、線の付け根までのすき間(px)
inline constexpr float EDGE_VIS_MIN_LEN    = 3.0f;     // 音が無いときの線の長さ(px)
inline constexpr float EDGE_VIS_MAX_LEN    = 110.0f;   // 一番大きい音のときに足す長さ(px)
inline constexpr float EDGE_VIS_FILL       = 0.55f;    // 1 本ぶんの間隔のうち、線の太さにする割合（残りはすき間）
inline constexpr float EDGE_VIS_DIM        = 0.7f;     // しま模様の薄い方の濃さ（濃い方の何倍か）
inline constexpr Uint8 EDGE_VIS_FRONT_ALPHA = 200;     // 一番手前の濃さ
inline constexpr Uint8 EDGE_VIS_BACK_ALPHA  = 50;      // 奥の層の濃さ
inline constexpr float EDGE_VIS_GLITCH_PX  = 8.0f;     // グリッチで左右にずらす最大(px)
inline constexpr int   EDGE_VIS_GLITCH_GROUP = 8;      // グリッチで一緒にずれる線の本数

//   laneLeft / laneRight：1レーン目の左端・6レーン目の右端（描く側の座標）
//   topY / bottomY     ：線を並べる範囲（描く側の座標。bottomY に低音が来る）
//   holo3D             ：厚みの強さ（0〜1。判定枠と同じ値）
inline void drawLaneEdgeVisualizer(SDL_Renderer* r, const SpectrumVisualizer& vis, float laneLeft, float laneRight,
                                   float topY, float bottomY, float holo3D){
    if(!vis.attached || bottomY - topY < 10.0f) return;
    constexpr int N = SpectrumVisualizer::BAR_COUNT;

    const HoloFx holo = holoNow();
    const float depth = (HOLO_DEPTH_FLAT_PX + HOLO_DEPTH_TILT_PX * std::clamp(holo3D, 0.0f, 1.0f)) * HOLO_FRAME_DEPTH_RATIO;
    const float step  = (bottomY - topY) / N;          // 1 本ぶんの間隔
    const float thick = std::max(1.5f, step * EDGE_VIS_FILL);

    static RoundedRectBatch batch;   // static：中の配列を毎フレーム作り直さない（flush で空になる）
    auto bar = [&](float x, float y, float w, SDL_Color c){
        if(w > 0.0f) batch.add(x, y, w, thick, 0.0f, c, 1, false);
    };

    for(int b = 0; b < N; b++){
        const float level = std::clamp(vis.levels[b], 0.0f, 1.0f);
        const float len = EDGE_VIS_MIN_LEN + level * EDGE_VIS_MAX_LEN;
        const float y   = bottomY - step * (b + 1) + (step - thick) * 0.5f;   // b = 0（低音）が一番下
        const float dim = (b % 2 == 0) ? 1.0f : EDGE_VIS_DIM;

        // 左右で別のずれ方にする（key を変える）
        const uint32_t group = static_cast<uint32_t>(b / EDGE_VIS_GLITCH_GROUP);
        const float shiftL = holoGlitchShift(holo, 300u + group) * EDGE_VIS_GLITCH_PX;
        const float shiftR = holoGlitchShift(holo, 400u + group) * EDGE_VIS_GLITCH_PX;

        const float xL = laneLeft - EDGE_VIS_GAP - len + shiftL;   // 左側：付け根が右、左へ伸びる
        const float xR = laneRight + EDGE_VIS_GAP + shiftR;        // 右側：付け根が左、右へ伸びる

        // 奥の層（遠い方から）：画面の上へずらして、青く薄く
        for(int k = 2; k >= 1; k--){
            const float lift = depth * (static_cast<float>(k) / 2.0f);
            SDL_Color bc = HOLO_BACK_COLOR;
            bc.a = static_cast<Uint8>(EDGE_VIS_BACK_ALPHA * holo.flicker * dim);
            bar(xL, y - lift, len, bc);
            bar(xR, y - lift, len, bc);
        }
        // 色ずれ（グリッチ中だけ）
        if(holo.glitch){
            const float d = HOLO_FRAME_RGB_PX;
            const SDL_Color red  = {255, 40, 60, HOLO_FRAME_RGB_ALPHA};
            const SDL_Color blue = {40, 200, 255, HOLO_FRAME_RGB_ALPHA};
            bar(xL - d, y, len, red);  bar(xL + d, y, len, blue);
            bar(xR - d, y, len, red);  bar(xR + d, y, len, blue);
        }
        // 一番手前
        SDL_Color fc = HOLO_FRONT_COLOR;
        fc.a = static_cast<Uint8>(EDGE_VIS_FRONT_ALPHA * holo.flicker * dim);
        bar(xL, y, len, fc);
        bar(xR, y, len, fc);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    batch.flush(r);
}

inline void renderGamePlayScreen(GameContext& ctx, Tex& tex, Sq& sq, double currentNoteSpeed, int laneX[6], bool present = true, JudgeBoxFx* boxFx = nullptr){
    // 背景：プレイ画面はジャケットのぼかし（PlayBackground）、それ以外（ノーツの詳細設定など）は今まで通り
    if(ctx.background) ctx.background->draw(ctx.renderer);
    else GradientBackground(ctx.renderer, SCREEN_W, SCREEN_H, ctx.musicTime);

    // ビジュアライザーは画面の左右の端に出るもので、レーンとは重ならない。
    // レーンと一緒に傾けないよう、傾ける範囲（↓の begin〜end）より前に、画面へそのまま描く
    if(ctx.visualizer) ctx.visualizer->render(ctx.renderer);

    // ===== ここから「レーンの左右の傾き」の対象 =====
    //   角度が付いているときは、レーン・ノーツ・エフェクトを透明な絵（laneLayerTex）に描き、最後に回して貼る。
    //   判定文字・コンボ・スコア・チュートリアルの文字は回さない（読みやすさのため）
    //   透明な絵に半透明の物を重ねると、色が「透明度を掛けた値」で入る（premultiplied）ので、貼るときもその前提の混ぜ方にする
    const bool useRoll = (ctx.laneLayerTex != nullptr) && std::abs(ctx.laneRollDeg) > 0.01;
    //   始まりの演出でレーンをグリッチで出す間も、同じ透明な絵に描いてから、帯に切って貼る
    const bool introGlitch = (ctx.laneLayerTex != nullptr) && ctx.laneIntro < 1.0;
    const bool useLayer = useRoll || introGlitch;
    //   laneLayerTex は画面より上下に rollMargin ずつ大きい。中身は全部 rollMargin だけ下にずらして描く
    //   （傾けて奥の側が縮んでも、画面の上下の端までレーンが届くように、上下に余分に描いておく）
    const int rollMargin = useLayer ? LANE_ROLL_OVERDRAW_PX : 0;
    SDL_Texture* rollPrevTarget = nullptr;
    if(useLayer){
        rollPrevTarget = SDL_GetRenderTarget(ctx.renderer);
        SDL_SetRenderTarget(ctx.renderer, ctx.laneLayerTex);
        SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ctx.renderer, 0, 0, 0, 0);
        SDL_RenderClear(ctx.renderer);
        SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_BLEND);
    }

    // ===== ここから「レーンの傾き」の対象 =====
    //   傾きが 0 でなければ、ここから end() までの描画は画面ではなく、傾ける用の縦に長いテクスチャに入る。
    //   テクスチャは画面より上に tiltYOff px 長いので（延長線上のレーン・ノーツ用）、全部を tiltYOff だけ下にずらして描く。
    //   ノーツ・判定ボックス・破裂・キービームの縦の位置は、どれも ctx.judgeY から計算しているので、
    //   ctx.judgeY を一時的に tiltYOff だけ下げるだけで、まとめてずれる（end() の前に元に戻す）
    const int baseJudgeY = ctx.judgeY;
    const int tiltYOff = (ctx.laneTilt != nullptr)
        ? ctx.laneTilt->begin(ctx.renderer, ctx.laneTiltDeg, baseJudgeY, 0.0, SCREEN_W / 2.0, rollMargin) : -1;
    const bool tilted = (tiltYOff >= 0);
    //   傾けていないときも、左右の傾きで上下に余分に描くときは rollMargin だけ下にずらす（ctx.judgeY も一緒にずれる）
    const int  yOff   = tilted ? tiltYOff : rollMargin;
    ctx.judgeY = baseJudgeY + yOff;

    // ノーツの太さ：傾けても画面で「傾けていないときと同じ形」に見えるよう、縦の長さを位置ごとに伸ばして描く
    //   y はテクスチャの中の y（ctx.judgeY もずらした後の値なので、判定ラインからの高さ = ctx.judgeY − y）
    //   傾けないときは、いつも NOTE_HEAD_H（今まで通り）
    auto headHAt = [&](double y) -> float{
        if(!tilted) return NOTE_HEAD_H;
        return static_cast<float>(NOTE_HEAD_H * LaneTilt::thicknessScale(ctx.laneTiltDeg, ctx.judgeY - y));
    };
    const float judgeHeadH = headHAt(ctx.judgeY);              // 判定ライン上の太さ（判定ボックス・破裂に使う）
    const float judgeScaleY = judgeHeadH / NOTE_HEAD_H;         // 判定ライン上で、縦をどれだけ伸ばしているか

    // 傾けるときは、区切り線は end() で描き、レーンは下の延長部分まで（textureLength）描く
    // キーの文字のホログラムの厚みは、ノーツの立体感と同じ強さ（傾きに合わせて強くなる）
    const float holo3D = tilted ? std::clamp(static_cast<float>(ctx.laneTiltDeg) / NOTE_3D_FULL_DEG, 0.0f, 1.0f) : 0.0f;
    //   傾けていないときのレーンの長さ：ふだんは画面の下まで（-1）、左右の傾きで余分に描くときは、上下の余分を含めた高さ
    const int laneLen = tilted ? ctx.laneTilt->textureLength() : (rollMargin > 0 ? SCREEN_H + rollMargin * 2 : -1);
    //   最後の引数：始まりの演出の途中か（区切り線は、その間と終わってから数秒は 7 本とも出す：drawFrame.h の drawLaneLines）
    //   終わりの演出（ctx.laneOutro）で消していくときは「始まり」扱いにしない（区切り線を 7 本に戻さない）
    draw_waku_init(ctx.renderer, tex, sq, ctx.laneActive, yOff, !tilted, laneLen, judgeHeadH, holo3D,
                   ctx.laneIntro < 1.0 && !ctx.laneOutro);
    if(boxFx) drawJudgeBoxFx(ctx, laneX, *boxFx, judgeHeadH);

    // レーンの左右の端のビジュアライザー（レーンと一緒に傾ける）
    //   縦の範囲は「画面に見えている上の端 〜 下の端」。画面の y は、上下に余分に描いているときは rollMargin だけ下にずれている
    if(ctx.visualizer){
        double visTop    = static_cast<double>(rollMargin);
        double visBottom = static_cast<double>(rollMargin + SCREEN_H);
        if(tilted){
            const double len = static_cast<double>(ctx.laneTilt->textureLength());
            const double top = g_tiltProj->unmapY(visTop);
            //   地平線より上（どこまで描いても画面の上の端に届かない）ときは計算が裏返るので、レーンの一番奥にする
            visTop    = (top < ctx.judgeY) ? std::clamp(top, 0.0, len) : 0.0;
            visBottom = std::clamp(g_tiltProj->unmapY(visBottom), 0.0, len);
        }
        const float laneLeft  = static_cast<float>(laneX[0]);
        const float laneRight = static_cast<float>(laneX[0] + ctx.laneWidth * 6);
        drawLaneEdgeVisualizer(ctx.renderer, *ctx.visualizer, laneLeft, laneRight,
                               static_cast<float>(visTop), static_cast<float>(visBottom), holo3D);
    }

    SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_BLEND);

    std::vector<char> isSync;
    drawSyncLines(ctx, currentNoteSpeed, laneX, isSync);

    SDL_SetRenderDrawColor(ctx.renderer, 255, 255, 255, 255);
    
    // レイヤーごとのまとめ描き（奥から LAYER_LONG → LAYER_DRAG → LAYER_NORMAL → LAYER_LANE の順に描く）
    //   1つのノーツの部品（同時押しの枠 → 帯 → まわりの光 → 芯 → ヘッド → 縁取り）は、そのノーツのレイヤーに続けて足す
    static RoundedRectBatch layers[LAYER_COUNT];   // static：中の配列を毎フレーム作り直さない（flush で空になる）

    // 立体感の強さ（0 = 付けない、1 = 一番強い）。傾けていないときは 0
    const float tilt3D = tilted
        ? std::clamp(static_cast<float>(ctx.laneTiltDeg) / NOTE_3D_FULL_DEG, 0.0f, 1.0f) : 0.0f;
    RoundedRectBatch shadowBatch;                   // 影（kShadowColor が透明なら何も足さない）
    constexpr bool useShadow = (kShadowColor.a > 0);

    // 表示用の時刻（ノーツの位置と同じ時計）。判定には使わない
    //   判定は ctx.musicTime で行い、その瞬間（note.hitTime）を「表示の時刻」が過ぎたときに
    //   叩いた見た目（消える・破裂・ホールド）にする → 表示調整をずらしても、ノーツが箱にぴったり重なって破裂する
    const double visualTime = ctx.musicTimeExact + ctx.visualOffsetMs;

    // 押せているロングの脈打つ明るさ：時間で決まる値は、ロングの数に関係なくフレームに1回だけ計算する
    const float pulse = static_cast<float>(0.5 - 0.5 * std::cos(
        std::fmod(visualTime, LONG_GLOW_PERIOD_MS) / LONG_GLOW_PERIOD_MS * 2.0 * 3.14159265358979));

    // ctx.notes は時刻の順に並んでいる（drawSyncLines も同じ前提）ので、この順に足せば後から来るノーツが手前になる
    for(const auto& note : ctx.notes){
        if(note.lane < 0 || note.lane >= 6) continue;

        const bool isLong = (note.type == NoteType::Long);

        // 判定が出た瞬間を、表示の時刻で過ぎたか
        const bool hitShown = note.isHit && visualTime >= note.hitTime;

        // 終点まで押し切ったロングで、終点の瞬間がまだ表示に来ていない（判定は済んだが、見た目はまだホールド中）
        const bool finishPending = isLong && note.isHit && !note.isHolding && !note.isFailed && !hitShown;

        if(hitShown && !note.isHolding && !note.isFailed) continue;

        const double effectiveSpeed = note.hasCustomSpeed ? note.customSpeed : currentNoteSpeed;
        const int noteY = computeNoteY(ctx, note, currentNoteSpeed);

        if(noteY < -50) continue;

        bool laneIsInactive = true;
        for(int l = note.lane; l < note.lane + note.widthLanes; l++){
            if(l >= 0 && l < 6 && ctx.laneActive[l]){
                laneIsInactive = false;
                break;
            }
        }

        // ① 色を先に決める（ヘッドと帯で同じ色を使うため）
        // ロングは「ロングになる前の種類」で色を決める（エディタと同じ：DRAG 黄、通常C 水色）
        const NoteType colorType = isLong ? note.headType : note.type;

        SDL_Color col;
        if(colorType == NoteType::Drag)                            col = {250, 250, 150, 255};
        else if(colorType == NoteType::Lane)                       col = {255, 50, 50, 255};
        else if(colorType == NoteType::Normal_c || note.isCyan)    col = {80, 220, 255, 255};
        else                                                       col = {255, 255, 255, 255};

        // このノーツを描くレイヤー：ロングは（ドラッグロングでも）全部ロングのレイヤー、
        //   それ以外はノーツの種類で決める
        NoteLayer layerId = LAYER_NORMAL;
        if(isLong)                                layerId = LAYER_LONG;
        else if(colorType == NoteType::Drag)      layerId = LAYER_DRAG;
        else if(colorType == NoteType::Lane)      layerId = LAYER_LANE;
        RoundedRectBatch& layer = layers[layerId];

        // 隠れているレーンのノーツ：色はそのまま、中身だけ半透明にして、縁取り用の色を作る
        const bool hiddenLook = laneIsInactive && colorType != NoteType::Lane;
        const SDL_Color outlineCol = {col.r, col.g, col.b, HIDDEN_NOTE_OUTLINE_ALPHA};
        if(hiddenLook) col.a = HIDDEN_NOTE_ALPHA;

        // レーンの範囲（複数レーン幅のノーツは、またいでいるレーン全体）
        const float laneLeft = static_cast<float>(laneX[note.lane]);
        const float laneSpan = static_cast<float>(ctx.laneWidth * note.widthLanes);

        // ノーツはレーンの左右に NOTE_SIDE_MARGIN ずつ隙間を空けて、中央に置く
        const float noteX  = laneLeft + NOTE_SIDE_MARGIN;
        const float noteW  = std::max(4.0f, laneSpan - NOTE_SIDE_MARGIN * 2.0f);
        const float radius = NOTE_RADIUS;

        // 見た目のホールド中：叩いた瞬間が表示に来てから、終点の瞬間が表示に来るまで
        const bool holdShown = (note.isHolding && hitShown) || finishPending;

        // 見た目のホールド中はヘッドを判定ラインに固定する（それまでは普通に流れてくる）
        const int headY = holdShown ? ctx.judgeY : noteY;

        // ヘッドの太さ（傾けているときは、ヘッドの位置に合わせて縦を伸ばす）
        const float noteH = headHAt(headY);

        // ヘッドを描くか（③と同じ条件）。ロングを途中で離した後は、ヘッドは描かず帯だけが流れていく
        const bool headDrawn = !hitShown || note.isHolding;

        // ② ロングノーツの帯 ＋ まわりの光 ＋ 中央の芯（ヘッドより先に足す = ヘッドの奥）
        if(isLong){
            //   位置は computeNoteY と同じく、スクロール位置 S の差で出す（B_SpeedEvent で止まる・速まるのに合わせる）
            const double visualScroll = scrollAt(ctx, visualTime);
            int tailY = ctx.judgeY - static_cast<int>((scrollAt(ctx, static_cast<double>(note.targetTime + note.durationMs)) - visualScroll) * effectiveSpeed);

            // 帯の下端：通常はヘッドの位置。失敗したら「失敗した時刻」の位置にして、判定ラインを越えて流す
            int bottomY = headY;
            if(note.isFailed){
                bottomY = ctx.judgeY - static_cast<int>((scrollAt(ctx, static_cast<double>(note.failCutTime)) - visualScroll) * effectiveSpeed);
            }

            // ホールド中、終端がヘッド（判定ライン）より下に来たら止める（帯が裏返らないように）
            if(holdShown && tailY > bottomY) tailY = bottomY;

            float top = static_cast<float>(tailY) - headHAt(tailY) * 0.5f;   // 終点の丸い端も、終点の位置の太さで
            top = std::max(top, -LONG_CLIP_MARGIN);   // 見えない上の方は切る（重くならないように）
            const float bottom = static_cast<float>(bottomY);

            if constexpr(useShadow){
                const float shapeBottom = bottom + noteH * 0.5f;
                shadowBatch.addInnerShadow(noteX, top, noteW, shapeBottom - top, radius, kShadowSpread, kShadowColor);
            }
            layer.add(noteX, top, noteW, bottom - top, radius, col);
            if(hiddenLook){
                // 帯の縁取りはヘッドの上の端までにする（ヘッドの真ん中を線が横切らないように）
                const float outlineH = std::max(radius * 2.0f, (bottom - noteH * 0.5f) - top);
                layer.addOutline(noteX, top, noteW, outlineH, radius, HIDDEN_NOTE_OUTLINE_W, outlineCol);
            }

            // --- 芯：帯の横幅の LONG_CORE_WIDTH_RATIO、左右中央に置く ---
            //   上（終点側）は帯の端から LONG_CORE_TAIL_MARGIN 内側、下（ヘッド側）はヘッドの上の端から
            //   LONG_CORE_HEAD_GAP 離して止める → 帯より短い芯になる
            const float innerW      = noteW * LONG_CORE_WIDTH_RATIO;
            const float innerX      = noteX + (noteW - innerW) * 0.5f;   // 中央寄せ
            const float innerRadius = std::min(radius, innerW * 0.5f);
            //   途中で離した後（ヘッドを描かない）は、無いヘッドのぶん（ヘッドの半分 ＋ LONG_CORE_HEAD_GAP）を空けず、
            //   上と同じく帯の端から LONG_CORE_TAIL_MARGIN 内側まで芯を伸ばす
            //   （前は、ここでもヘッドのぶんを空けていたので、離すと芯の下の一定の長さが消えていた）
            const float innerTop    = top + LONG_CORE_TAIL_MARGIN;
            const float innerBottom = headDrawn ? bottom - noteH * 0.5f - LONG_CORE_HEAD_GAP
                                                : bottom - LONG_CORE_TAIL_MARGIN;
            const float innerH      = innerBottom - innerTop;

            SDL_Color innerCol;
            if(hiddenLook){
                innerCol = {col.r, col.g, col.b, HIDDEN_NOTE_INNER_ALPHA};   // 隠れている：帯より薄く
            }
            else if(note.isFailed){
                innerCol = {static_cast<Uint8>(col.r * 0.15),            // 失敗：初期状態(0.6倍)より黒く
                            static_cast<Uint8>(col.g * 0.15),
                            static_cast<Uint8>(col.b * 0.15), 230};
            }
            else if(holdShown){
                // 押せている：シアン → 少しだけ白っぽいシアン へ脈打つ
                innerCol = {static_cast<Uint8>(LONG_CORE_WHITEN * pulse), 255, 255, 255};
            }
            else{
                innerCol = {static_cast<Uint8>(col.r * 0.6),             // 叩く前：ノーツ色を暗くした色
                            static_cast<Uint8>(col.g * 0.6),
                            static_cast<Uint8>(col.b * 0.6), 200};
            }

            // 芯が短すぎる（ロングがとても短い、またはホールドの終わり際）ときは描かない
            if(innerH > 1.0f){
                if(holdShown && !hiddenLook && !note.isFailed){
                    // 押せているとき：芯より少し広い四角を1枚、薄いシアンで重ねる（芯の奥。pulse で濃さが脈打つ）
                    const float g = LONG_GLOW_SPREAD;
                    const Uint8 glowA = static_cast<Uint8>(LONG_GLOW_ALPHA_MIN + (LONG_GLOW_ALPHA_MAX - LONG_GLOW_ALPHA_MIN) * pulse);
                    layer.add(innerX - g, innerTop - g * 0.5f, innerW + g * 2.0f, innerH + g, innerRadius + g,
                              SDL_Color{0, 255, 255, glowA});
                }
                layer.add(innerX, innerTop, innerW, innerH, innerRadius, innerCol);
            }
        }

        // ③ ヘッド：叩いた見た目になる前、またはホールド中なら描く（同時押しの枠 → ヘッド → 縁取り）
        if(headDrawn){
            const float headTop = static_cast<float>(headY) - noteH * 0.5f;

            const size_t noteIndex = static_cast<size_t>(&note - ctx.notes.data());
            if(isSync[noteIndex]){
                const float f = SYNC_FRAME_WIDTH;
                const float g = SYNC_FRAME_GAP;
                // 外側：金色の縁 → 内側：ヘッドとの間の暗い隙間（この上にヘッドが乗る）
                //   色が透明（SYNC_FRAME_COLOR の a = 0）なら add の中で何もしない
                layer.add(noteX - f, headTop - f, noteW + f * 2.0f, noteH + f * 2.0f, radius + f, SYNC_FRAME_COLOR);
                layer.add(noteX - g, headTop - g, noteW + g * 2.0f, noteH + g * 2.0f, radius + g, SYNC_FRAME_GAP_COLOR);
            }

            // ロングノーツの影は②で全体ぶん付けたので、ここでは単発系だけ
            if constexpr(useShadow){
                if(!isLong) shadowBatch.addInnerShadow(noteX, headTop, noteW, noteH, radius, kShadowSpread, kShadowColor);
            }
            // 傾けているとき：側面（ヘッドのすぐ下の暗い帯）を付ける
            //   側面はヘッドの下半分から下へはみ出す形で先に描き、その上にヘッドを重ねる → はみ出た分だけが厚みに見える
            if(tilt3D > 0.0f && !hiddenLook){
                const float scaleY = noteH / NOTE_HEAD_H;                       // 奥ほど縦に伸びている分（傾けても形を保つため）
                const float depth  = NOTE_3D_DEPTH_PX * tilt3D * scaleY;
                const SDL_Color side = {static_cast<Uint8>(col.r * NOTE_3D_SIDE_DARKEN),
                                        static_cast<Uint8>(col.g * NOTE_3D_SIDE_DARKEN),
                                        static_cast<Uint8>(col.b * NOTE_3D_SIDE_DARKEN), col.a};
                layer.add(noteX, headTop + noteH * 0.5f, noteW, noteH * 0.5f + depth, radius, side);
            }
            layer.add(noteX, headTop, noteW, noteH, radius, col);
            if(hiddenLook){
                layer.addOutline(noteX, headTop, noteW, noteH, radius, HIDDEN_NOTE_OUTLINE_W, outlineCol);
            }
        }
    }

    // false ノーツ（見た目だけ。判定なし）：ふつうのノーツより奥に描く（Play/FalseNotes.h）
    if(ctx.falseNotes && !ctx.falseNotes->empty()){
        drawFalseNotes(ctx.renderer, *ctx.falseNotes, visualTime, static_cast<float>(laneX[0]), static_cast<float>(ctx.laneWidth),
                       static_cast<float>(ctx.judgeY), [&](double y){ return headHAt(y); });
    }

    // 奥のレイヤーから順に描く：ロング → ドラッグ → 単発 → レーン
    for(int i = 0; i < LAYER_COUNT; i++) layers[i].flush(ctx.renderer);
    if constexpr(useShadow) shadowBatch.flush(ctx.renderer);

    SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_BLEND);
    for(const auto &fx : ctx.effects){
        if(fx.lane < 0 || fx.lane >= 6) continue;
        if(fx.judgeType == 0) continue;   // 判定のないエフェクト（空打ち）は出さない

        // 叩いた時刻（spawnTime）を表示の時刻が過ぎてから破裂を始める（ノーツが消える瞬間と同じ）
        double p = (visualTime - static_cast<double>(fx.spawnTime)) / fx.duration;
        if(p < 0.0 || p >= 1.0) continue;
        double ease = 1.0 - (1.0 - p) * (1.0 - p);   // 最初に速く広がる

        // ノーツの外形（判定ボックスと同じ計算）
        const float laneLeft = static_cast<float>(laneX[fx.lane]);
        const float laneSpan = static_cast<float>(ctx.laneWidth * fx.widthLanes);
        const float noteW    = laneSpan - NOTE_SIDE_MARGIN * 2.0f;
        const float cx       = laneLeft + laneSpan * 0.5f;
        const float cy       = static_cast<float>(ctx.judgeY);

        // 判定ごとの色
        SDL_Color c;
        if(fx.judgeType == 1)      c = kPerfectColor;               // PERFECT：淡い黄色
        else if(fx.judgeType == 2) c = SDL_Color{0, 255, 255, 255}; // GOOD：シアン
        else                       c = JUDGE_COLOR_FAIR;            // FAIR（前の BAD）：明るい緑

        // ① 破裂の一瞬だけ、ノーツの周り（レーン幅 × 80px）を白く光らせる
        if(p < 0.15){
            Uint8 fa = static_cast<Uint8>(200 * (1.0 - p / 0.15));
            SDL_SetRenderDrawColor(ctx.renderer, 255, 255, 255, fa);
            const int flashH = static_cast<int>(80 * judgeScaleY);   // 傾けているときはノーツと同じだけ縦に伸ばす
            SDL_Rect flash = {static_cast<int>(laneLeft), static_cast<int>(cy) - flashH / 2, static_cast<int>(laneSpan), flashH};
            if(g_tiltProj){
                tiltRect(ctx.renderer, static_cast<float>(flash.x), static_cast<float>(flash.y),
                         static_cast<float>(flash.w), static_cast<float>(flash.h), SDL_Color{255, 255, 255, fa}, nullptr, 2);
            }
            else{
                SDL_RenderFillRect(ctx.renderer, &flash);
            }
        }

        // ② 広がる枠（太さ 4px）
        Uint8 a = static_cast<Uint8>(255 * (1.0 - p));
        int w = static_cast<int>(noteW * (1.0 + ease * 0.8));
        int h = static_cast<int>((NOTE_HEAD_H + ease * 80.0) * judgeScaleY);
        if(g_tiltProj){
            // 傾けているとき：枠を「上・下・左・右の4本の細い四角」にして、角を変換して描く
            const float L = cx - w / 2.0f, T = cy - h / 2.0f, W = static_cast<float>(w), H = static_cast<float>(h), b = 4.0f;
            const SDL_Color rc = {c.r, c.g, c.b, a};
            tiltRect(ctx.renderer, L, T, W, b, rc, nullptr, 1);
            tiltRect(ctx.renderer, L, T + H - b, W, b, rc, nullptr, 1);
            tiltRect(ctx.renderer, L, T + b, b, H - b * 2.0f, rc, nullptr, 2);
            tiltRect(ctx.renderer, L + W - b, T + b, b, H - b * 2.0f, rc, nullptr, 2);
        }
        else{
            SDL_SetRenderDrawColor(ctx.renderer, c.r, c.g, c.b, a);
            for(int t = 0; t < 4; t++){
                SDL_Rect ring = {static_cast<int>(cx) - w / 2 + t, static_cast<int>(cy) - h / 2 + t, w - t * 2, h - t * 2};
                SDL_RenderDrawRect(ctx.renderer, &ring);
            }
        }

        // ③ 重なった四角（ひし形）：大きさの違う 3 つが回りながら広がり、だんだん消える
        //   ・傾けているときは、大きい四角ほど奥（画面の上）へずらして「高く浮いている」ように見せる
        //     （レーンの面より上にある物は、斜め上から見ると奥にずれて見えるため）
        //   ・浮いている感じを強めるため、一番大きい四角の影を、ずらさずにレーンへ落とす
        //   ・小さい四角から順に描き、大きい（高い）四角が手前に重なるようにする
        {
            const float fade = static_cast<float>(1.0 - p);
            const float big  = noteW * (BURST_SQ_START + BURST_SQ_GROW * static_cast<float>(ease));
            const float liftScale = tilt3D * judgeScaleY;   // 傾けていなければ 0（浮かせない）

            if(tilt3D > 0.0f){
                const SDL_Color sh = {0, 0, 0, static_cast<Uint8>(BURST_SQ_SHADOW_A * tilt3D * fade)};
                drawRotatedSquareFrame(ctx.renderer, cx, cy, big, 0.785398f + BURST_SQ_SPIN[0] * static_cast<float>(ease),
                                       BURST_SQ_THICK[0] + 1.0f, sh);
            }
            for(int k = BURST_SQ_COUNT - 1; k >= 0; k--){
                const float size  = big * BURST_SQ_SIZE[k];
                const float angle = 0.785398f + BURST_SQ_SPIN[k] * static_cast<float>(ease);   // 45° から回し始める（ひし形）
                const float lift  = BURST_SQ_LIFT_PX[k] * liftScale;
                const SDL_Color sc = {c.r, c.g, c.b, static_cast<Uint8>(255.0f * BURST_SQ_ALPHA[k] * fade)};
                drawRotatedSquareFrame(ctx.renderer, cx, cy - lift, size, angle, BURST_SQ_THICK[k], sc);
            }
        }
    }

    // ロングを押している間：破裂の「重なった四角」だけが判定ラインの所に残って、等速で回り続ける（上の HOLD_SQ_ の説明）
    for(const Note& ln : ctx.notes){
        if(ln.type != NoteType::Long || !ln.isHolding || ln.lane < 0 || ln.lane >= 6) continue;
        const double heldMs = std::max(0.0, visualTime - static_cast<double>(ln.hitTime));   // 押し始めてからの時間（表示の時刻）
        const float laneLeft = static_cast<float>(laneX[ln.lane]);
        const float laneSpan = static_cast<float>(ctx.laneWidth * ln.widthLanes);
        const float noteW    = laneSpan - NOTE_SIDE_MARGIN * 2.0f;
        const float cx       = laneLeft + laneSpan * 0.5f;
        const float cy       = static_cast<float>(ctx.judgeY);

        const double g = std::min(1.0, heldMs / HOLD_SQ_GROW_MS);
        const float grow = static_cast<float>(1.0 - (1.0 - g) * (1.0 - g));                 // はじめ速く広がって止まる
        const float big  = noteW * (BURST_SQ_START + BURST_SQ_GROW * HOLD_SQ_GROW * grow);
        const float fadeIn = static_cast<float>(std::min(1.0, heldMs / 80.0));               // 押した瞬間にいきなり出ないように
        const float liftScale = tilt3D * judgeScaleY;
        const float sec = static_cast<float>(visualTime / 1000.0);                           // 回る角度は時刻から（一定の速さ）

        if(tilt3D > 0.0f){
            const SDL_Color sh = {0, 0, 0, static_cast<Uint8>(BURST_SQ_SHADOW_A * tilt3D * fadeIn)};
            const float dir0 = (BURST_SQ_SPIN[0] >= 0.0f) ? 1.0f : -1.0f;
            drawRotatedSquareFrame(ctx.renderer, cx, cy, big, 0.785398f + dir0 * HOLD_SQ_SPEED[0] * sec,
                                   BURST_SQ_THICK[0] + 1.0f, sh);
        }
        for(int k = BURST_SQ_COUNT - 1; k >= 0; k--){
            const float dir   = (BURST_SQ_SPIN[k] >= 0.0f) ? 1.0f : -1.0f;
            const float angle = 0.785398f + dir * HOLD_SQ_SPEED[k] * sec;
            const float lift  = BURST_SQ_LIFT_PX[k] * liftScale;
            const SDL_Color sc = {kPerfectColor.r, kPerfectColor.g, kPerfectColor.b,
                                  static_cast<Uint8>(255.0f * BURST_SQ_ALPHA[k] * HOLD_SQ_ALPHA * fadeIn)};
            drawRotatedSquareFrame(ctx.renderer, cx, cy - lift, big * BURST_SQ_SIZE[k], angle, BURST_SQ_THICK[k], sc);
        }
    }

    // ノーツを取ったとき・ロングの中継点：半透明の立方体が飛び出し、落ちて判定ボックスの下の端で跳ね返る
    if(ctx.tickParticles){
        SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_BLEND);
        for(const TickParticle& p : *ctx.tickParticles){
            const double el = visualTime - p.spawnTime;   // ホールドの見た目と同じく表示の時刻で進める
            const double t  = el / TICK_PARTICLE_LIFE_MS;
            if(t < 0.0 || t >= 1.0) continue;

            // 立方体は画面の座標で持っているので、傾けるときは yOff だけずらす
            if(el < p.teleportMs){
                // 飛び出して落ちている途中
                const CubeState st = cubeStateAt(p, el);
                const float pop = static_cast<float>(std::min(1.0, el / CUBE_POP_MS));   // 出た瞬間は小さく
                drawCube(ctx.renderer, st.x, st.y + static_cast<float>(yOff), p.size * pop, st.rx, st.ry, st.rz,
                         laneBaseColor(p.lane), p.alpha);
            }
            else{
                // 止まった位置でテレポート
                const double u = (el - p.teleportMs) / CUBE_TELEPORT_MS;
                if(u >= 1.0) continue;
                const CubeState st = cubeStateAt(p, p.teleportMs);
                drawCubeTeleport(ctx.renderer, p, st.x, st.y + static_cast<float>(yOff), st.rx, st.ry, st.rz, u, el);
            }
        }
    }

    // キービーム：焼き込みテクスチャ1枚を貼る（1本あたり40回 → 1回）
    //   長さの設定が 0 % のときは出さない
    if(ctx.keyBeamRatio > 0.0){
        const double keyBeamRatio = std::clamp(ctx.keyBeamRatio, 0.0, 1.0);
        const int32_t beamDurationMs = 600;
        // 傾けているときは、画面で見たときに keyBeamRatio の所まで届く長さを、テクスチャの中の長さに直して使う
        //   （傾けると奥ほど詰まって見えるので、テクスチャの中の長さは長くなる）
        const int beamHeight = tilted
            ? static_cast<int>(ctx.laneTilt->heightForScreenRatio(ctx.laneTiltDeg, baseJudgeY, keyBeamRatio))
            : static_cast<int>(keyBeamRatio * baseJudgeY);
        SDL_Texture* beamTex = getBeamTexture(ctx.renderer);

        if(beamTex){
            for(const auto& beam : ctx.keyBeams){
                if(beam.lane < 0 || beam.lane >= 6) continue;

                double timeProgress = static_cast<double>(ctx.musicTime - beam.spawnTime) / beamDurationMs;
                if(timeProgress >= 1.0) continue;

                double timeFade = 1.0 - easeOutCubic(timeProgress);
                const Uint8 beamAlpha = static_cast<Uint8>(255 * timeFade);

                SDL_SetTextureAlphaMod(beamTex, beamAlpha);   // SDL_RenderCopy（傾けていないとき）用
                // レーンの幅ぴったり（左右の端は区切り線の位置）
                const int beamX = laneX[beam.lane];
                const int beamW = ctx.laneWidth;
                SDL_Rect beamRect = {beamX, ctx.judgeY - beamHeight, beamW, beamHeight};
                if(g_tiltProj){
                    // 傾けているとき：ビームの画像を、帯に分けて角を変換して貼る（奥ほど詰まって正しく見える）
                    //   SDL_RenderGeometry では SDL_SetTextureAlphaMod が効かない（環境による）ので、
                    //   消えていく濃さは頂点の色の透明度（beamAlpha）で渡す。
                    //   テクスチャの透明度は 255 に戻しておき、二重に薄くならないようにする
                    SDL_SetTextureAlphaMod(beamTex, 255);
                    tiltRect(ctx.renderer, static_cast<float>(beamRect.x), static_cast<float>(beamRect.y),
                             static_cast<float>(beamRect.w), static_cast<float>(beamRect.h),
                             SDL_Color{255, 255, 255, beamAlpha}, beamTex, 24);
                }
                else{
                    SDL_RenderCopy(ctx.renderer, beamTex, NULL, &beamRect);
                }
            }
        }
    }

    // ===== レーンカバー（壁） =====
    //   レーン・ノーツ・エフェクトを全部描いた後に、奥側を不透明な壁でふさぐ。
    //   壁の下の端は「画面の上で、判定ラインから見えているレーンの奥の端までの (1 − 割合) の所」。
    //   傾けているときは、その画面の y を、描く側の座標に戻して（unmapY）壁を描くので、傾けても画面の上で同じ割合になる
    float coverEndY = 0.0f;   // 壁の下の端（描く側の座標）。区切り線はここから下だけ描く
    if(ctx.laneCoverRatio > 0.0){
        //   ここでの「画面の y」は、左右の傾きで上下に余分に描いているときは rollMargin だけ下にずれた値で考える
        const double screenTop = static_cast<double>(rollMargin);   // 画面の上の端
        double topScreen = screenTop;   // 画面で見えているレーンの奥の端
        if(tilted){
            float X, Y, s, a;
            g_tiltProj->map(SCREEN_W / 2.0, 0.0, X, Y, s, a);   // 延長したレーンの一番奥が来る画面の y
            topScreen = std::max(screenTop, static_cast<double>(Y));   // 画面の上より上は見えないので、画面の上まで
        }
        const double ratio = std::clamp(ctx.laneCoverRatio, 0.0, 1.0);
        const double judgeScreenY = baseJudgeY + rollMargin;
        const double coverScreenY = topScreen + ratio * (judgeScreenY - topScreen);   // 壁の下の端（画面の y）
        coverEndY = static_cast<float>(tilted ? g_tiltProj->unmapY(coverScreenY) : coverScreenY);

        const float left  = static_cast<float>(laneX[0]) - LANE_COVER_SIDE;
        const float width = static_cast<float>(ctx.laneWidth * 6) + LANE_COVER_SIDE * 2.0f;
        const float edgeH = LANE_COVER_EDGE_H * judgeScaleY;   // 傾けているときは、ノーツと同じだけ縦に伸ばす
        if(tilted){
            tiltRect(ctx.renderer, left, 0.0f, width, coverEndY, LANE_COVER_COLOR, nullptr, 8);
            tiltRect(ctx.renderer, left, coverEndY - edgeH, width, edgeH, LANE_COVER_EDGE_COLOR, nullptr, 1);
        }
        else{
            SDL_SetRenderDrawColor(ctx.renderer, LANE_COVER_COLOR.r, LANE_COVER_COLOR.g, LANE_COVER_COLOR.b, LANE_COVER_COLOR.a);
            SDL_FRect wall = {left, 0.0f, width, coverEndY};
            SDL_RenderFillRectF(ctx.renderer, &wall);
            SDL_SetRenderDrawColor(ctx.renderer, LANE_COVER_EDGE_COLOR.r, LANE_COVER_EDGE_COLOR.g, LANE_COVER_EDGE_COLOR.b, LANE_COVER_EDGE_COLOR.a);
            SDL_FRect edge = {left, coverEndY - edgeH, width, edgeH};
            SDL_RenderFillRectF(ctx.renderer, &edge);
        }
    }

    // ===== 「レーンの傾き」の対象はここまで =====
    //   レーン・ノーツ・判定ボックス・破裂・キービームを描いた絵を、判定ラインを軸に奥へ倒して画面に貼る。
    //   この後の判定文字・コンボ・スコアは傾けない（読みやすさのため、画面にまっすぐ描く）
    ctx.judgeY = baseJudgeY;   // ずらしていた判定ラインを元に戻す（判定や、この後の描画は元の位置で行う）
    // 始まりの演出のレーンの外枠を、傾けた位置に描くため、傾ける変換を覚えておく（end() の後は使えなくなるので）
    TiltProjection introProj{};
    double introTexLen = 0.0;
    if(tilted && ctx.laneIntro < 1.0 && g_tiltProj){
        introProj = *g_tiltProj;
        introTexLen = ctx.laneTilt->textureLength();
    }
    if(tilted){
        // レーンの区切り線の x（draw_waku_init と同じ並び：一番左のレーンの左端から、レーン幅ごとに 7 本）
        float lineXs[7];
        for(int i = 0; i <= 6; i++) lineXs[i] = static_cast<float>(laneX[0] + ctx.laneWidth * i);
        // 区切り線は壁の下の端（coverEndY）から手前だけ描く（壁の上に線が出ないように）
        //   判定ラインより上は、開いているレーンが続く所の左右の端だけ（drawFrame.h の drawLaneLines。傾ける変換が残っている end() の前に描く）
        drawLaneLines(ctx.renderer, lineXs, coverEndY, static_cast<float>(baseJudgeY + yOff),
                      static_cast<float>(ctx.laneTilt->textureLength()));
        ctx.laneTilt->end(ctx.renderer, ctx.laneTiltDeg, static_cast<double>(baseJudgeY), SCREEN_W / 2.0, lineXs, 0);
        SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_BLEND);
    }

    // ===== 「レーンの左右の傾き」の対象はここまで：レーンの中心線を軸にして、左右に傾けて貼る =====
    if(useLayer){
        SDL_SetRenderTarget(ctx.renderer, rollPrevTarget);
        if(introGlitch){
            // 始まりの演出：レーン全体（背景・区切り線・判定枠・ノーツ）を、横の帯に切ってグリッチさせながら出す
            if(ctx.laneIntro > 0.0){
                const SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
                drawGlitchTexture(ctx.renderer, ctx.laneLayerTex, full, ctx.laneIntro, 24, 40.0f, 8.0f, rollMargin);
            }
        }
        else{
            // レーンの真ん中の縦の線を軸にして、奥行きのある形で傾ける（判定ラインの高さは変わらない）
            const double laneCenterX = laneX[0] + ctx.laneWidth * 3.0;   // 6 レーンの真ん中
            drawRolledLayer(ctx.renderer, ctx.laneLayerTex, ctx.laneRollDeg, laneCenterX, static_cast<double>(baseJudgeY),
                            static_cast<double>(rollMargin));
        }
        SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_BLEND);
    }

    // 始まりの演出：レーンの外枠（一番左・一番右の線と、判定ライン）が、判定ラインの所から伸びていく。
    //   レーンが出きるまで表示する。傾けているときは、レーンと同じ傾きの位置に描く
    if(ctx.laneIntro < 1.0 && ctx.laneFrameIntro > 0.0){
        const double e = 1.0 - std::pow(1.0 - std::clamp(ctx.laneFrameIntro, 0.0, 1.0), 3.0);   // はじめ速く、だんだんゆっくり
        const float L = static_cast<float>(laneX[0]), R = static_cast<float>(laneX[0] + ctx.laneWidth * 6);
        const float midX = (L + R) * 0.5f;
        SDL_Color c = HOLO_FRONT_COLOR;
        c.a = 230;
        // (x, y：傾ける前の、描く側の座標) → 画面の座標
        auto toScreen = [&](float x, float y, float& X, float& Y){
            if(tilted && introTexLen > 0.0){
                float s, a;
                introProj.map(x, y, X, Y, s, a);
                Y -= static_cast<float>(rollMargin);   // 透明な絵の中のずれを戻す
            }
            else{
                X = x;
                Y = y - static_cast<float>(rollMargin);
            }
        };
        const float judgeYDraw = static_cast<float>(baseJudgeY + yOff);                      // 描く側の座標の判定ライン
        const float topYDraw   = tilted ? 0.0f : static_cast<float>(rollMargin);             // 線の上の端
        const float botYDraw   = tilted ? static_cast<float>(introTexLen) : static_cast<float>(rollMargin + SCREEN_H);
        const float y0 = judgeYDraw - (judgeYDraw - topYDraw) * static_cast<float>(e);
        const float y1 = judgeYDraw + (botYDraw - judgeYDraw) * static_cast<float>(e);
        float ax, ay, bx, by;
        for(float x : {L, R}){
            toScreen(x, y0, ax, ay);
            toScreen(x, y1, bx, by);
            drawThickLine(ctx.renderer, ax, ay, bx, by, 3.0f, c);
        }
        // 判定ライン：真ん中から左右へ
        toScreen(midX - (midX - L) * static_cast<float>(e), judgeYDraw, ax, ay);
        toScreen(midX + (R - midX) * static_cast<float>(e), judgeYDraw, bx, by);
        drawThickLine(ctx.renderer, ax, ay, bx, by, 3.0f, c);
    }

    // 左上の Cube の数（傾けない）
    drawCubeHud(ctx, ctx.musicTimeExact + ctx.visualOffsetMs);

    {
        int latest = -1;
        for(int i = 0; i < 6; i++){
            if(ctx.laneJudge[i].texture == nullptr) continue;
            if(latest < 0 || ctx.laneJudge[i].spawnTime >= ctx.laneJudge[latest].spawnTime) latest = i;
        }

        if(latest >= 0){
            const JudgeEffect& je = ctx.laneJudge[latest];

            // 出てからの進み具合 0→1。大きさは JUDGE_POP_START → 少し行き過ぎて → 1.0
            double t = std::clamp((ctx.musicTimeExact - je.spawnTime) / JUDGE_POP_MS, 0.0, 1.0);
            double scale = 1.0 + JUDGE_POP_AMOUNT * (1.0 - easeOutCubic(t));

            // 元の位置の中心を保ったまま拡大・縮小する
            int w  = static_cast<int>(je.rect.w * scale);
            int h  = static_cast<int>(je.rect.h * scale);
            int cx = je.rect.x + je.rect.w / 2;
            int cy = je.rect.y + je.rect.h / 2;
            SDL_Rect dst = {cx - w / 2, cy - h / 2, w, h};

            drawHoloJudgeText(ctx.renderer, je.texture, je.rect.w, je.rect.h, dst, ctx.musicTimeExact - je.spawnTime);

            // FAST/LATE・±ms は元の大きさの位置に出す（ポップに合わせて揺れないように）
            //   ドラッグ・ロングの中継点や終点など、叩いた瞬間で測らない判定（hasTiming = false）が出ても、
            //   ±ms は「最後に叩いた判定」（ctx.lastTimedJudge）の値をそのまま出し続ける（次に叩くまで変わらない）
            //   FAST/LATE は、出ている判定文字が叩いた判定のときだけ（judgeType = 1 を渡すと FAST/LATE は出ない）
            if(ctx.timing){
                if(je.hasTiming){
                    ctx.timing->draw(je.rect, je.judgeType, je.diffMs);
                }
                else if(ctx.lastTimedJudge && ctx.lastTimedJudge->hasTiming){
                    ctx.timing->draw(je.rect, 1, ctx.lastTimedJudge->diffMs);
                }
            }
        }
    }

        {
        const int combo = ctx.scoreTracker.getCurrentCombo();
        if(ctx.digitFont != nullptr && combo > 0){
            // コンボが増えた時刻からの進み具合 0 → 1
            const double ct = std::clamp((ctx.musicTimeExact - ctx.comboPop.startTime) / JUDGE_COMBO_SLIDE_MS, 0.0, 1.0);
            const double e  = easeOutCubic(ct);   // 最初は速く、最後はゆっくり止まる

            // スライドアップ：下にずれた位置から、本来の位置まで上がる
            const int slideY = static_cast<int>(JUDGE_COMBO_SLIDE_PX * (1.0 - e));

            // 上がりながら、薄い状態からいつもの濃さに戻る
            const double fade = JUDGE_COMBO_SLIDE_FADE + (1.0 - JUDGE_COMBO_SLIDE_FADE) * e;

            // 色は今までと同じ（AP 中は金、FC 中は水色、それ以外は白）で、半透明にする
            const Uint8 alpha = static_cast<Uint8>(JUDGE_COMBO_ALPHA * fade);
            SDL_Color cc = {255, 255, 255, alpha};
            if(ctx.isAP)      cc = {255, 215, 0,   alpha};
            else if(ctx.isFC) cc = {0,   255, 255, alpha};

            // 「COMBO」は動かさない（いつもの濃さで、決まった位置に出す）
            if(ctx.comboLabel){
                const int numberH = static_cast<int>(ctx.digitFont->digitH * JUDGE_COMBO_SCALE);
                SDL_Rect ld = {SCREEN_W / 2 - ctx.comboLabelW / 2,
                               JUDGE_COMBO_Y - numberH / 2 - JUDGE_COMBO_LABEL_GAP - ctx.comboLabelH,
                               ctx.comboLabelW, ctx.comboLabelH};
                SDL_SetTextureColorMod(ctx.comboLabel, cc.r, cc.g, cc.b);
                SDL_SetTextureAlphaMod(ctx.comboLabel, JUDGE_COMBO_ALPHA);
                SDL_RenderCopy(ctx.renderer, ctx.comboLabel, NULL, &ld);
            }

            char cs[16];
            snprintf(cs, sizeof(cs), "%d", combo);
            ctx.digitFont->drawCentered(ctx.renderer, cs, false, SCREEN_W / 2, JUDGE_COMBO_Y + slideY,
                                        JUDGE_COMBO_SCALE, cc);
        }
    }

    // スコア・コンボ：数字テクスチャを並べて貼るだけ（毎フレームの文字生成なし）
    if(ctx.digitFont != nullptr){
        char scoreStr[16];
        snprintf(scoreStr, sizeof(scoreStr), "%07d", ctx.lastDisplayScoreValue);
        ctx.digitFont->drawCentered(ctx.renderer, scoreStr, false,
                                    static_cast<int>(SCREEN_W * (7.0 / 8.0)), 80,
                                    ctx.scoreScale, SDL_Color{255, 255, 255, 255});
    }

    if(ctx.tutorial) ctx.tutorial->draw(ctx.renderer, ctx.musicTime);
    if(present) SDL_RenderPresent(ctx.renderer);
}

// ===== 通り過ぎたノーツを消すまでの時間 =====
//   ノーツの時刻（ロングは終点の時刻）を、判定の時刻・表示の時刻の両方でこれだけ過ぎたら、ノーツを消す。
//   長くすると、見逃したノーツが判定ラインを越えて画面の下まで流れていくのが見える
//   （レーンを傾けて手前を延長したときや、ノーツ速度が遅いときに、途中で消えて見えないように）。
//   ※ 曲が終わってからリザルトへ移るのは「ノーツが全部消えてから」なので、最後のノーツからこの時間だけ待つことになる
inline constexpr int32_t NOTE_ERASE_AFTER_MS = 1500;   // 前は 500

inline void erase_some(GameContext& ctx){
    // 破裂・中継点のエフェクトは表示の時刻で進むので、片付けも表示の時刻で判断する
    //   （表示調整がマイナスのとき、破裂が始まる前に消してしまわないように）
    const double visualTime = ctx.musicTimeExact + ctx.visualOffsetMs;

    std::erase_if(ctx.effects, [visualTime](const Effect& fx){
        return (visualTime - static_cast<double>(fx.spawnTime)) >= static_cast<double>(fx.duration);
    });

    std::erase_if(ctx.keyBeams, [&ctx](const KeyBeam& beam){
            return (ctx.musicTime - beam.spawnTime) >= 600;
        });

    // テレポートし終わった立方体を Cube として数える（1 個 = 1 Cube）
    if(ctx.tickParticles && ctx.cubeHud){
        for(TickParticle& p : *ctx.tickParticles){
            if(p.collected) continue;
            if(visualTime - p.spawnTime >= p.teleportMs + CUBE_TELEPORT_MS){
                p.collected = true;
                ctx.cubeHud->earned++;
                ctx.cubeHud->popMs = visualTime;
            }
        }
    }

    // 消えた立方体を片付ける
    if(ctx.tickParticles){
        std::erase_if(*ctx.tickParticles, [visualTime](const TickParticle& p){
            return visualTime - p.spawnTime >= TICK_PARTICLE_LIFE_MS;
        });
    }

    std::erase_if(ctx.notes, [&ctx, visualTime](const Note& note){
        if(note.isHolding){
            return false;
        }

        int32_t endTime = note.targetTime;
        if(note.type == NoteType::Long){
            endTime = note.targetTime + note.durationMs;
        }

        // 判定の時刻と表示の時刻の両方で NOTE_ERASE_AFTER_MS 過ぎてから消す
        //   （判定の MISS より先に消さない／表示がまだ追いついていないノーツを消さない）
        bool shouldErase = (ctx.musicTime - endTime > NOTE_ERASE_AFTER_MS) &&
                           (visualTime - endTime > static_cast<double>(NOTE_ERASE_AFTER_MS));

        return shouldErase;
    });
}