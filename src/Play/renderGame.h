#pragma once

#include "GameCommon.h"
#include "Play/type.h"
#include "Play/GameContext.h"
#include "Play/roundedRect.h"
#include "Play/tutorialOverlay.h"
#include "Play/visualizer.h"
#include "Play/drawFrame.h"

constexpr SDL_Color kPerfectColor = {250, 250, 150, 255};
constexpr SDL_Color kShadowColor  = {0, 0, 0, 0};
constexpr float kShadowSpread = 0.0f;

inline double evaluatNotePathY(const Note& note, double musicTime){
    const auto& path = note.path;
    if(path.empty()) return 0.0;

    if(musicTime == note.targetTime) return 0.0;

    if(musicTime <= path.front().time) return path.front().y;
    if(musicTime >= path.back().time) return path.back().y;

    for(size_t i = 1; i < path.size(); i++){
        if(musicTime <= path[i].time){
            const auto& k0 = path[i - 1];
            const auto& k1 = path[i];
            double t = static_cast<double>(musicTime - k0.time) / static_cast<double>(k1.time - k0.time);

            if(k1.easing == 2) t = easeOutCubic(t);
            else if(k1.easing == 3) t = easeInCubic(t);

            return k0.y + (k1.y - k0.y) * t;
        }
    }
    return path.back().y;
}

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

// ===== 隠れているレーン（laneActive が false）のノーツ =====
//   レーンが閉じている間のノーツは、半透明にしてうっすら見えるだけにする
//   値は 0〜255（小さいほど透明）。LANE ノーツはレーンを開け閉めするので対象外（いつも通り見える）
inline constexpr Uint8 HIDDEN_NOTE_ALPHA       = 200;    // ヘッド・ロングの帯
inline constexpr Uint8 HIDDEN_NOTE_INNER_ALPHA = 110;    // ロングの芯（帯より少し薄く）

// ===== 判定文字の上のコンボ数 =====
inline constexpr double JUDGE_COMBO_SCALE      = 1.3;    // 大きさ（数字の元の大きさの何倍か。元は 80px）
inline constexpr double JUDGE_COMBO_SLIDE_MS     = 120.0;   // 下から上がりきるまでの時間
inline constexpr int    JUDGE_COMBO_SLIDE_PX     = 18;      // どれだけ下から上がってくるか(px)
inline constexpr double JUDGE_COMBO_SLIDE_FADE   = 0.4;     // 上がり始めの濃さ（いつもの濃さの何倍か）
inline constexpr Uint8  JUDGE_COMBO_ALPHA      = 110;    // 透明度（0 = 見えない、255 = はっきり）
inline constexpr int    JUDGE_COMBO_Y          = 300;    // 縦の中心の位置(px)。判定文字は 540 あたり
inline constexpr int    JUDGE_COMBO_LABEL_GAP  = 0;      // 「COMBO」とコンボ数の間(px)。離れて見えるときはマイナスに

// ノーツの中心の y 座標を求める（ノーツ本体の描画と補助線で共通に使う）
inline int computeNoteY(const GameContext& ctx, const Note& note, double currentNoteSpeed){
    const double t = ctx.musicTimeExact + ctx.visualOffsetMs;   // 表示用の時刻（判定には使わない）
    if(note.hasCustomPath()){
        double distFromLine = evaluatNotePathY(note, t);
        return ctx.judgeY - static_cast<int>(distFromLine);
    }
    double effectiveSpeed = note.hasCustomSpeed ? note.customSpeed : currentNoteSpeed;
    return ctx.judgeY - static_cast<int>((static_cast<double>(note.targetTime) - t) * effectiveSpeed);
}

// 太さのある線を描く（2点を結ぶ細長い四角形を三角形2枚で描く）
inline void drawThickLine(SDL_Renderer* renderer, float x1, float y1, float x2, float y2,
                          float thickness, SDL_Color color){
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
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}

// 同じ時刻・別レーンのノーツどうしを線でつなぐ
// outIsSync には、線でつないだノーツの番号（ctx.notes の添字）に 1 が入る
inline void drawSyncLines(GameContext& ctx, double currentNoteSpeed, int laneX[6], std::vector<char>& outIsSync){
    struct Point{ float x; float y; size_t index; };
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
            if(y < -50 || y > SCREEN_H + 50) continue;       // 画面外は描かない

            float cx = laneX[note.lane] + ctx.laneWidth * note.widthLanes / 2.0f;
            group.push_back({cx, static_cast<float>(y), j});
        }

        // 2. 2つ以上残っていれば、左から順に隣どうしを結び、結んだノーツに印を付ける
        if(group.size() >= 2){
            std::sort(group.begin(), group.end(),
                      [](const Point& a, const Point& b){ return a.x < b.x; });

            for(size_t k = 0; k + 1 < group.size(); k++){
                if(group[k + 1].x - group[k].x < 1.0f) continue;   // 同じレーンどうしは結ばない
                drawThickLine(ctx.renderer, group[k].x, group[k].y, group[k + 1].x, group[k + 1].y,
                              SYNC_LINE_THICKNESS, SYNC_LINE_COLOR);
                outIsSync[group[k].index]     = 1;
                outIsSync[group[k + 1].index] = 1;
            }
        }

        i = j;   // 次の時刻のグループへ
    }
}

// 押したレーンの判定ボックスを埋める（長押し中は埋まったまま）
inline void drawJudgeBoxFx(GameContext& ctx, int laneX[6], JudgeBoxFx& fx){
    const double now = ctx.musicTimeExact;
    fx.update(ctx.currentPressed, now);

    const float judgeY = static_cast<float>(ctx.judgeY);

    RoundedRectBatch fillBatch;
    for(int i = 0; i < 6; i++){

        double lv = fx.level(i, now);
        if(lv <= 0.001) continue;

        const float noteX = static_cast<float>(laneX[i]) + JUDGE_BOX_SIDE_MARGIN;
        const float noteW = static_cast<float>(ctx.laneWidth) - JUDGE_BOX_SIDE_MARGIN * 2.0f;
        const float noteY = judgeY - NOTE_HEAD_H * 0.5f;

        // 長押し中は枠線もシアンにして「押さえている」ことを示す
        if(fx.held[i]){
            fillBatch.addOutline(noteX, noteY, noteW, NOTE_HEAD_H, NOTE_RADIUS, JUDGE_BOX_BORDER, JUDGE_FILL_COLOR);
        }

        // 枠の内側を埋める
        const float b = JUDGE_BOX_BORDER;
        SDL_Color c = JUDGE_FILL_COLOR;
        c.a = static_cast<Uint8>(std::clamp(lv, 0.0, 1.0) * 230);
        fillBatch.add(noteX + b, noteY + b, noteW - b * 2.0f, NOTE_HEAD_H - b * 2.0f, std::max(0.0f, NOTE_RADIUS - b), c);
    }
    fillBatch.flush(ctx.renderer);
}

inline void renderGamePlayScreen(GameContext& ctx, Tex& tex, Sq& sq, double currentNoteSpeed, int laneX[6], bool present = true, JudgeBoxFx* boxFx = nullptr){
    GradientBackground(ctx.renderer, SCREEN_W, SCREEN_H, ctx.musicTime);
    draw_waku_init(ctx.renderer, tex, sq, ctx.laneActive);
    if(ctx.visualizer) ctx.visualizer->render(ctx.renderer);
    if(boxFx) drawJudgeBoxFx(ctx, laneX, *boxFx);

    SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_BLEND);

    std::vector<char> isSync;
    drawSyncLines(ctx, currentNoteSpeed, laneX, isSync);

    SDL_SetRenderDrawColor(ctx.renderer, 255, 255, 255, 255);
    
    RoundedRectBatch shadowBatch;  // 影
    RoundedRectBatch bodyBatch;    // ロングノーツの帯
    RoundedRectBatch innerBatch;   // 帯の中の芯
    RoundedRectBatch noteBatch;    // ノーツのヘッド
    RoundedRectBatch syncFrameBatch;    //同時押しの枠

    // 表示用の時刻（ノーツの位置と同じ時計）。判定には使わない
    //   判定は ctx.musicTime で行い、その瞬間（note.hitTime）を「表示の時刻」が過ぎたときに
    //   叩いた見た目（消える・破裂・ホールド）にする → 表示調整をずらしても、ノーツが箱にぴったり重なって破裂する
    const double visualTime = ctx.musicTimeExact + ctx.visualOffsetMs;

    for(const auto& note : ctx.notes){
        if(note.lane < 0 || note.lane >= 6) continue;

            const bool isLong = (note.type == NoteType::Long);

            // 判定が出た瞬間を、表示の時刻で過ぎたか
            const bool hitShown = note.isHit && visualTime >= note.hitTime;

            // 終点まで押し切ったロングで、終点の瞬間がまだ表示に来ていない（判定は済んだが、見た目はまだホールド中）
            const bool finishPending = isLong && note.isHit && !note.isHolding && !note.isFailed && !hitShown;

            if(hitShown && !note.isHolding && !note.isFailed) continue;

            double effectiveSpeed = note.hasCustomSpeed ? note.customSpeed : currentNoteSpeed;
            int noteY = computeNoteY(ctx, note, currentNoteSpeed);

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
            const NoteType colorType = (note.type == NoteType::Long) ? note.headType : note.type;

            SDL_Color col;
            if(laneIsInactive && colorType != NoteType::Lane)          col = {20, 20, 20, HIDDEN_NOTE_ALPHA};   // 隠れている：半透明
            else if(colorType == NoteType::Drag)                       col = {250, 250, 150, 255};
            else if(colorType == NoteType::Lane)                       col = {255, 50, 50, 255};
            else if(colorType == NoteType::Normal_c || note.isCyan)    col = {80, 220, 255, 255};
            else                                                       col = {255, 255, 255, 255};

                    // レーンの範囲（複数レーン幅のノーツは、またいでいるレーン全体）
            const float laneLeft = static_cast<float>(laneX[note.lane]);
            const float laneSpan = static_cast<float>(ctx.laneWidth * note.widthLanes);

            // ノーツはレーンの左右に NOTE_SIDE_MARGIN ずつ隙間を空けて、中央に置く
            const float noteX  = laneLeft + NOTE_SIDE_MARGIN;
            const float noteW  = std::max(4.0f, laneSpan - NOTE_SIDE_MARGIN * 2.0f);
            const float noteH  = NOTE_HEAD_H;
            const float radius = NOTE_RADIUS;

            // 見た目のホールド中：叩いた瞬間が表示に来てから、終点の瞬間が表示に来るまで
            const bool holdShown = (note.isHolding && hitShown) || finishPending;

            // 見た目のホールド中はヘッドを判定ラインに固定する（それまでは普通に流れてくる）
            const int headY = holdShown ? ctx.judgeY : noteY;

            // ② ロングノーツの帯 ＋ 中央の芯
            if(note.type == NoteType::Long){
                // ヘッド（computeNoteY）と同じ「表示用の時刻」を使う。判定には使わない
                //   ここを ctx.musicTimeExact のままにすると、表示調整を入れたときに
                //   ヘッドだけがずれて、ロングの長さが変わってしまう
                const double visualTime = ctx.musicTimeExact + ctx.visualOffsetMs;

                int tailY = ctx.judgeY - static_cast<int>((static_cast<double>(note.targetTime + note.durationMs) - visualTime) * effectiveSpeed);

                // 帯の下端：通常はヘッドの位置。失敗したら「失敗した時刻」の位置にして、判定ラインを越えて流す
                int bottomY = headY;
                if(note.isFailed){
                    bottomY = ctx.judgeY - static_cast<int>((static_cast<double>(note.failCutTime) - visualTime) * effectiveSpeed);
                }

                // ホールド中、終端がヘッド（判定ライン）より下に来たら止める（帯が裏返らないように）
                if(holdShown && tailY > bottomY) tailY = bottomY;

                float top    = static_cast<float>(tailY) - noteH * 0.5f;
                float bottom = static_cast<float>(bottomY);

                float shapeBottom = bottom + noteH * 0.5f;
                shadowBatch.addInnerShadow(noteX, top, noteW, shapeBottom - top, radius, kShadowSpread, kShadowColor);
                bodyBatch.add(noteX, top, noteW, bottom - top, radius, col);

                // --- 芯：帯の横幅の30%、左右中央に置く ---
                const float innerRatio  = 0.3f;                              // 帯に対する芯の幅の割合
                const float innerW      = noteW * innerRatio;
                const float innerX      = noteX + (noteW - innerW) * 0.5f;   // 中央寄せ
                const float innerMargin = 6.0f;                              // 終点側を帯より少し内側に
                const float innerRadius = std::min(radius, innerW * 0.5f);

                SDL_Color innerCol;
                if(laneIsInactive){
                    innerCol = {col.r, col.g, col.b, HIDDEN_NOTE_INNER_ALPHA};   // 隠れている：帯より薄く
                }
                else if(note.isFailed){
                    innerCol = {static_cast<Uint8>(col.r * 0.15),            // 失敗：初期状態(0.6倍)より黒く
                                static_cast<Uint8>(col.g * 0.15),
                                static_cast<Uint8>(col.b * 0.15), 230};
                }
                else if(holdShown){
                    innerCol = {0, 255, 255, 255};
                }
                else{
                    innerCol = {static_cast<Uint8>(col.r * 0.6),             // 叩く前：ノーツ色を暗くした色
                                static_cast<Uint8>(col.g * 0.6),
                                static_cast<Uint8>(col.b * 0.6), 200};
                }

            innerBatch.add(innerX, top + innerMargin, innerW, bottom - (top + innerMargin), innerRadius, innerCol);
        }

        // ③ ヘッド：叩いた見た目になる前、またはホールド中なら描く
        if(!hitShown || note.isHolding){
            const float headTop = static_cast<float>(headY) - noteH * 0.5f;

            const size_t noteIndex = static_cast<size_t>(&note - ctx.notes.data());
            if(isSync[noteIndex]){
                const float f = SYNC_FRAME_WIDTH;
                const float g = SYNC_FRAME_GAP;
                // 外側：金色の縁
                syncFrameBatch.add(noteX - f, headTop - f, noteW + f * 2.0f, noteH + f * 2.0f,
                                   radius + f, SYNC_FRAME_COLOR);
                // 内側：ヘッドとの間に暗い隙間を作る（この上にヘッドが乗る）
                syncFrameBatch.add(noteX - g, headTop - g, noteW + g * 2.0f, noteH + g * 2.0f,
                                   radius + g, SYNC_FRAME_GAP_COLOR);
            }

            // ロングノーツの影は②で全体ぶん付けたので、ここでは単発系だけ
            if(note.type != NoteType::Long){
                shadowBatch.addInnerShadow(noteX, headTop, noteW, noteH, radius, kShadowSpread, kShadowColor);
            }
            noteBatch.add(noteX, headTop, noteW, noteH, radius, col);
        }
    }

    // 帯 → ヘッドの順にまとめて描く（ヘッドが必ず帯の手前に来る）
    bodyBatch.flush(ctx.renderer);
    innerBatch.flush(ctx.renderer);
    syncFrameBatch.flush(ctx.renderer);
    noteBatch.flush(ctx.renderer);
    shadowBatch.flush(ctx.renderer);

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
            SDL_Rect flash = {static_cast<int>(laneLeft), static_cast<int>(cy) - 40, static_cast<int>(laneSpan), 80};
            SDL_RenderFillRect(ctx.renderer, &flash);
        }

        // ② 広がる枠（太さ 4px）
        Uint8 a = static_cast<Uint8>(255 * (1.0 - p));
        int w = static_cast<int>(noteW * (1.0 + ease * 0.8));
        int h = static_cast<int>(NOTE_HEAD_H + ease * 80.0);
        SDL_SetRenderDrawColor(ctx.renderer, c.r, c.g, c.b, a);
        for(int t = 0; t < 4; t++){
            SDL_Rect ring = {static_cast<int>(cx) - w / 2 + t, static_cast<int>(cy) - h / 2 + t, w - t * 2, h - t * 2};
            SDL_RenderDrawRect(ctx.renderer, &ring);
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
            drawRotatedSquareFilled(ctx.renderer, x, y, size, 0.0f, c);
        }
    }

    // キービーム：焼き込みテクスチャ1枚を貼る（1本あたり40回 → 1回）
    {
        const int32_t beamDurationMs = 600;
        const int beamHeight = 500;
        SDL_Texture* beamTex = getBeamTexture(ctx.renderer);

        if(beamTex){
            for(const auto& beam : ctx.keyBeams){
                if(beam.lane < 0 || beam.lane >= 6) continue;

                double timeProgress = static_cast<double>(ctx.musicTime - beam.spawnTime) / beamDurationMs;
                if(timeProgress >= 1.0) continue;

                double timeFade = 1.0 - easeOutCubic(timeProgress);

                SDL_SetTextureAlphaMod(beamTex, static_cast<Uint8>(255 * timeFade));
                const int beamX = laneX[beam.lane] + static_cast<int>(JUDGE_BOX_SIDE_MARGIN);
                const int beamW = ctx.laneWidth - static_cast<int>(JUDGE_BOX_SIDE_MARGIN * 2.0f);
                SDL_Rect beamRect = {beamX, ctx.judgeY - beamHeight, beamW, beamHeight};
                SDL_RenderCopy(ctx.renderer, beamTex, NULL, &beamRect);
            }
        }
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

        // 判定の時刻と表示の時刻の両方で 500ms 過ぎてから消す
        //   （判定の MISS より先に消さない／表示がまだ追いついていないノーツを消さない）
        bool shouldErase = (ctx.musicTime - endTime > 500) && (visualTime - endTime > 500.0);

        return shouldErase;
    });
}