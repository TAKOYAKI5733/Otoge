#pragma once

#include "GameCommon.h"
#include "Play/type.h"
#include "Play/GameContext.h"
#include "Play/roundedRect.h"
#include "Play/tutorialOverlay.h"
#include "Play/visualizer.h"
#include "Play/drawFrame.h"
#include "Play/LaneTilt.h"
#include "Play/PlayBackground.h"   // 背景：藍色を含んだ灰色 ＋ 真ん中にぼかしたジャケット

constexpr SDL_Color kPerfectColor = {250, 250, 150, 255};
constexpr SDL_Color kShadowColor  = {0, 0, 0, 0};
constexpr float kShadowSpread = 0.0f;

// ===== 判定文字のポップ =====
inline constexpr double JUDGE_POP_MS    = 120.0;   // 出てから元の大きさに収まるまで
inline constexpr double JUDGE_POP_AMOUNT = 0.15;    // 出た瞬間の大きさ（元の何倍か）

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

// ノーツの中心の y 座標を求める（ノーツ本体の描画と補助線で共通に使う）
inline int computeNoteY(const GameContext& ctx, const Note& note, double currentNoteSpeed){
    const double t = ctx.musicTimeExact + ctx.visualOffsetMs;   // 表示用の時刻（判定には使わない）
    double effectiveSpeed = note.hasCustomSpeed ? note.customSpeed : currentNoteSpeed;
    return ctx.judgeY - static_cast<int>((static_cast<double>(note.targetTime) - t) * effectiveSpeed);
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

inline void renderGamePlayScreen(GameContext& ctx, Tex& tex, Sq& sq, double currentNoteSpeed, int laneX[6], bool present = true, JudgeBoxFx* boxFx = nullptr){
    // 背景：プレイ画面はジャケットのぼかし（PlayBackground）、それ以外（ノーツの詳細設定など）は今まで通り
    if(ctx.background) ctx.background->draw(ctx.renderer);
    else GradientBackground(ctx.renderer, SCREEN_W, SCREEN_H, ctx.musicTime);

    // ビジュアライザーは画面の左右の端に出るもので、レーンとは重ならない。
    // レーンと一緒に傾けないよう、傾ける範囲（↓の begin〜end）より前に、画面へそのまま描く
    if(ctx.visualizer) ctx.visualizer->render(ctx.renderer);

    // ===== ここから「レーンの傾き」の対象 =====
    //   傾きが 0 でなければ、ここから end() までの描画は画面ではなく、傾ける用の縦に長いテクスチャに入る。
    //   テクスチャは画面より上に tiltYOff px 長いので（延長線上のレーン・ノーツ用）、全部を tiltYOff だけ下にずらして描く。
    //   ノーツ・判定ボックス・破裂・キービームの縦の位置は、どれも ctx.judgeY から計算しているので、
    //   ctx.judgeY を一時的に tiltYOff だけ下げるだけで、まとめてずれる（end() の前に元に戻す）
    const int baseJudgeY = ctx.judgeY;
    const int tiltYOff = (ctx.laneTilt != nullptr)
        ? ctx.laneTilt->begin(ctx.renderer, ctx.laneTiltDeg, baseJudgeY) : -1;
    const bool tilted = (tiltYOff >= 0);
    const int  yOff   = tilted ? tiltYOff : 0;
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
    draw_waku_init(ctx.renderer, tex, sq, ctx.laneActive, yOff, !tilted, tilted ? ctx.laneTilt->textureLength() : -1, judgeHeadH);
    if(boxFx) drawJudgeBoxFx(ctx, laneX, *boxFx, judgeHeadH);

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

        // ② ロングノーツの帯 ＋ まわりの光 ＋ 中央の芯（ヘッドより先に足す = ヘッドの奥）
        if(isLong){
            int tailY = ctx.judgeY - static_cast<int>((static_cast<double>(note.targetTime + note.durationMs) - visualTime) * effectiveSpeed);

            // 帯の下端：通常はヘッドの位置。失敗したら「失敗した時刻」の位置にして、判定ラインを越えて流す
            int bottomY = headY;
            if(note.isFailed){
                bottomY = ctx.judgeY - static_cast<int>((static_cast<double>(note.failCutTime) - visualTime) * effectiveSpeed);
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
            const float innerTop    = top + LONG_CORE_TAIL_MARGIN;
            const float innerBottom = bottom - noteH * 0.5f - LONG_CORE_HEAD_GAP;
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
        if(!hitShown || note.isHolding){
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
        else                       c = SDL_Color{230, 70, 70, 255}; // BAD：赤

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

    // ロングの中継点：正方形の枠が飛び散り、だんだん止まりながら消える
    if(ctx.tickParticles){
        for(const TickParticle& p : *ctx.tickParticles){
            const double el = visualTime - p.spawnTime;   // ホールドの見た目と同じく表示の時刻で進める
            const double t  = el / TICK_PARTICLE_LIFE_MS;
            if(t < 0.0 || t >= 1.0) continue;

            const double e = easeOutCubic(t);                                    // 最初は速く、だんだん止まる
            const float  dist = static_cast<float>(e * TICK_PARTICLE_LIFE_MS);   // 速さ × 時間
            const float  x = p.x + p.vx * dist;
            const float  y = p.y + p.vy * dist;
            const float  size = p.size * static_cast<float>(1.0 - 0.3 * t);      // 少し小さくなる
            SDL_Color c = p.color;
            c.a = static_cast<Uint8>(p.color.a * (1.0 - t));                          // だんだん消える
            drawRotatedSquareFilled(ctx.renderer, x, y + static_cast<float>(yOff), size, 0.0f, c);   // 粒は画面の座標で持っているので、傾けるときはずらす
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
                const int beamX = laneX[beam.lane] + static_cast<int>(JUDGE_BOX_SIDE_MARGIN);
                const int beamW = ctx.laneWidth - static_cast<int>(JUDGE_BOX_SIDE_MARGIN * 2.0f);
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
        double topScreen = 0.0;   // 画面で見えているレーンの奥の端
        if(tilted){
            float X, Y, s, a;
            g_tiltProj->map(SCREEN_W / 2.0, 0.0, X, Y, s, a);   // 延長したレーンの一番奥が来る画面の y
            topScreen = std::max(0.0, static_cast<double>(Y));   // 画面の上より上は見えないので、画面の上まで
        }
        const double ratio = std::clamp(ctx.laneCoverRatio, 0.0, 1.0);
        const double coverScreenY = topScreen + ratio * (baseJudgeY - topScreen);   // 壁の下の端（画面の y）
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
    if(tilted){
        // レーンの区切り線の x（draw_waku_init と同じ並び：一番左のレーンの左端から、レーン幅ごとに 7 本）
        float lineXs[7];
        for(int i = 0; i <= 6; i++) lineXs[i] = static_cast<float>(laneX[0] + ctx.laneWidth * i);
        // 区切り線は壁の下の端（coverEndY）から手前だけ描く（壁の上に線が出ないように）
        ctx.laneTilt->end(ctx.renderer, ctx.laneTiltDeg, static_cast<double>(baseJudgeY), SCREEN_W / 2.0, lineXs, 7,
                          coverEndY);
        SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_BLEND);
    }

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

            SDL_SetTextureAlphaMod(je.texture, 255);
            SDL_RenderCopy(ctx.renderer, je.texture, NULL, &dst);

            // FAST/LATE・±ms は元の大きさの位置に出す（ポップに合わせて揺れないように）
            if(ctx.timing && je.hasTiming){
                ctx.timing->draw(je.rect, je.judgeType, je.diffMs);
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

    // 消えたロングの中継点のエフェクトを片付ける
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