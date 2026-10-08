#pragma once

#include "GameCommon.h"
#include "Play/type.h"
#include "Play/GameContext.h"
#include "Play/roundedRect.h"
#include "Play/tutorialOverlay.h"
#include "Play/visualizer.h"

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

// ===== 同時押しの補助線 =====
inline constexpr float     SYNC_LINE_THICKNESS = 6.0f;                 // 線の太さ(px)
inline constexpr SDL_Color SYNC_LINE_COLOR     = {255, 255, 255, 160};  // 線の色（半透明の白）

// ノーツの中心の y 座標を求める（ノーツ本体の描画と補助線で共通に使う）
inline int computeNoteY(const GameContext& ctx, const Note& note, double currentNoteSpeed){
    if(note.hasCustomPath()){
        double distFromLine = evaluatNotePathY(note, ctx.musicTime);
        return ctx.judgeY - static_cast<int>(distFromLine);
    }
    double effectiveSpeed = note.hasCustomSpeed ? note.customSpeed : currentNoteSpeed;
    return ctx.judgeY - static_cast<int>((static_cast<double>(note.targetTime) - ctx.musicTime) * effectiveSpeed);
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

// 同じ時刻・別レーンのノーツどうしを線でつなぐ
inline void drawSyncLines(GameContext& ctx, double currentNoteSpeed, int laneX[6]){
    struct Point{ float x; float y; };
    std::vector<Point> group;
    group.reserve(6);

    const size_t n = ctx.notes.size();
    size_t i = 0;
    while(i < n){
        // 1. targetTime が同じ連続部分 [i, j) をひとまとまりにする
        const int32_t t = ctx.notes[i].targetTime;
        size_t j = i;
        group.clear();

        for(; j < n && ctx.notes[j].targetTime == t; j++){
            const Note& note = ctx.notes[j];
            if(note.lane < 0 || note.lane >= 6) continue;
            if(note.isHit) continue;                         // 叩いたノーツは線から外す

            int y = computeNoteY(ctx, note, currentNoteSpeed);
            if(y < -50 || y > SCREEN_H + 50) continue;       // 画面外は描かない

            float cx = laneX[note.lane] + ctx.laneWidth * note.widthLanes / 2.0f;
            group.push_back({cx, static_cast<float>(y)});
        }

        // 2. 2つ以上残っていれば、左から順に隣どうしを結ぶ
        if(group.size() >= 2){
            std::sort(group.begin(), group.end(),
                      [](const Point& a, const Point& b){ return a.x < b.x; });

            for(size_t k = 0; k + 1 < group.size(); k++){
                if(group[k + 1].x - group[k].x < 1.0f) continue;   // 同じレーンどうしは結ばない
                drawThickLine(ctx.renderer, group[k].x, group[k].y, group[k + 1].x, group[k + 1].y,
                              SYNC_LINE_THICKNESS, SYNC_LINE_COLOR);
            }
        }

        i = j;   // 次の時刻のグループへ
    }
}

inline void renderGamePlayScreen(GameContext& ctx, Tex& tex, Sq& sq, double currentNoteSpeed, int laneX[6]){
    GradientBackground(ctx.renderer, SCREEN_W, SCREEN_H, ctx.musicTime);
    draw_waku_init(ctx.renderer, tex, sq, ctx.laneActive);
    if(ctx.visualizer) ctx.visualizer->render(ctx.renderer);

    SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_BLEND);

    drawSyncLines(ctx, currentNoteSpeed, laneX);

    SDL_SetRenderDrawColor(ctx.renderer, 255, 255, 255, 255);
    SDL_Texture* lastModTexture = nullptr;
    
    RoundedRectBatch shadowBatch;  // 影（一番奥）
    RoundedRectBatch bodyBatch;    // ロングノーツの帯（一番奥）
    RoundedRectBatch innerBatch;   // 帯の中の芯（帯の手前）
    RoundedRectBatch noteBatch;    // ノーツのヘッド（一番手前）

    for(const auto& note : ctx.notes){
        if(note.lane < 0 || note.lane >= 6) continue;

                if(note.isHit && !note.isHolding && !note.isFailed && ctx.musicTime >= note.targetTime) continue;

        double effectiveSpeed = note.hasCustomSpeed ? note.customSpeed : currentNoteSpeed;
        int noteY = computeNoteY(ctx, note, currentNoteSpeed);

        if(noteY < -50) continue;

        bool laneIsInactive = !ctx.laneActive[note.lane];

        // ① 色を先に決める（ヘッドと帯で同じ色を使うため）
        SDL_Color col;
        if(laneIsInactive && !(note.type == NoteType::Lane)) col = {20, 20, 20, 255};
        else if(note.type == NoteType::Drag)                 col = {250, 250, 150, 255};
        else if(note.type == NoteType::Lane)                 col = {255, 50, 50, 255};
        else if(note.type == NoteType::Normal_c || note.isCyan)  col = {80, 220, 255, 255};
        else                                                 col = {255, 255, 255, 255};

        const float noteX  = static_cast<float>(laneX[note.lane]);
        const float noteW  = static_cast<float>(ctx.laneWidth * note.widthLanes);
        const float noteH  = 30.0f;   // ヘッドの高さ
        const float radius = 8.0f;    // 角の丸み（ヘッドと帯で共通）

        // ホールド中はヘッドを判定ラインに固定する
        const int headY = note.isHolding ? ctx.judgeY : noteY;

        // ② ロングノーツの帯：ヘッドと同じ形を縦に引き伸ばす
                // ② ロングノーツの帯 ＋ 中央の芯
                // ② ロングノーツの帯 ＋ 中央の芯
        if(note.type == NoteType::Long){
            int tailY = ctx.judgeY - static_cast<int>((static_cast<double>(note.targetTime + note.durationMs) - ctx.musicTimeExact) * effectiveSpeed);

            // 帯の下端：通常はヘッドの位置。失敗したら「失敗した時刻」の位置にして、判定ラインを越えて流す
            int bottomY = headY;
            if(note.isFailed){
                bottomY = ctx.judgeY - static_cast<int>((static_cast<double>(note.failCutTime) - ctx.musicTimeExact) * effectiveSpeed);
            }

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
                innerCol = {col.r, col.g, col.b, 150};                   // 無効レーン：暗いまま
            }
            else if(note.isFailed){
                innerCol = {static_cast<Uint8>(col.r * 0.15),            // 失敗：初期状態(0.6倍)より黒く
                            static_cast<Uint8>(col.g * 0.15),
                            static_cast<Uint8>(col.b * 0.15), 230};
            }
            else if(note.isHolding){
                innerCol = {0, 255, 255, 255};
            }
            else{
                innerCol = {static_cast<Uint8>(col.r * 0.6),             // 叩く前：ノーツ色を暗くした色
                            static_cast<Uint8>(col.g * 0.6),
                            static_cast<Uint8>(col.b * 0.6), 200};
            }

            innerBatch.add(innerX, top + innerMargin, innerW, bottom - (top + innerMargin), innerRadius, innerCol);
        }

        // ③ ヘッド：未判定、またはホールド中なら描く
        if(!note.isHit || note.isHolding){
            const float headTop = static_cast<float>(headY) - noteH * 0.5f;

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
    noteBatch.flush(ctx.renderer);
    shadowBatch.flush(ctx.renderer);

    SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_BLEND);
    for(const auto &fx : ctx.effects){
        if(fx.lane < 0 || fx.lane >= 6) continue;

        double progress = static_cast<double>(ctx.musicTime - fx.spawnTime) / fx.duration;
        if(progress > 1.0) progress = 1.0;

        int alpha = static_cast<int>((1.0 - progress) * 200);

        int baseWidth = ctx.laneWidth * fx.widthLanes;

        int currentWidth = static_cast<int>(baseWidth * (1.0 + progress * 0.5));

        SDL_Rect fxRect;
            if(fx.judgeType == 1){
                SDL_SetRenderDrawColor(ctx.renderer, 250, 250, 150, alpha);
            }
            else if(fx.judgeType == 2){
                SDL_SetRenderDrawColor(ctx.renderer, 0, 255, 255, alpha);
            }
            else if(fx.judgeType == 3){
                SDL_SetRenderDrawColor(ctx.renderer, 180, 50, 50, alpha);
            }
            else{
                SDL_SetRenderDrawColor(ctx.renderer, 255, 255, 255, alpha);
            }

            fxRect.w = currentWidth;
            fxRect.h = 40;
            fxRect.x = laneX[fx.lane] + (baseWidth / 2) - (fxRect.w / 2);
            fxRect.y = ctx.judgeY - (fxRect.h / 2);
        SDL_RenderFillRect(ctx.renderer, &fxRect);
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
                SDL_Rect beamRect = {laneX[beam.lane], ctx.judgeY - beamHeight, ctx.laneWidth, beamHeight};
                SDL_RenderCopy(ctx.renderer, beamTex, NULL, &beamRect);
            }
        }
    }

    for(int i = 0; i < 6; i++){
        if(ctx.laneJudge[i].texture != nullptr){
            SDL_RenderCopy(ctx.renderer, ctx.laneJudge[i].texture, NULL, &ctx.laneJudge[i].rect);
        }
    }

    // スコア・コンボ：数字テクスチャを並べて貼るだけ（毎フレームの文字生成なし）
    if(ctx.digitFont != nullptr){
        char scoreStr[16];
        snprintf(scoreStr, sizeof(scoreStr), "%07d", ctx.lastDisplayScoreValue);
        ctx.digitFont->drawCentered(ctx.renderer, scoreStr, false,
                                    static_cast<int>(SCREEN_W * (7.0 / 8.0)), 80,
                                    ctx.scoreScale, SDL_Color{255, 255, 255, 255});

        int currentCombo = ctx.scoreTracker.getCurrentCombo();
        if(currentCombo > 0){
            SDL_Color comboColor = {255, 255, 255, 255};
            if(ctx.isAP) comboColor = {255, 215, 0, 255};
            else if(ctx.isFC) comboColor = {0, 255, 255, 255};

            char comboStr[16];
            snprintf(comboStr, sizeof(comboStr), "%d", currentCombo);
            ctx.digitFont->drawCentered(ctx.renderer, comboStr, true,
                                        static_cast<int>(SCREEN_W * (7.0 / 8.0)), SCREEN_W / 3,
                                        ctx.comboScale, comboColor);
        }
    }

    if(ctx.tutorial) ctx.tutorial->draw(ctx.renderer, ctx.musicTime);
    SDL_RenderPresent(ctx.renderer);
}

inline void erase_some(GameContext& ctx){
    std::erase_if(ctx.effects, [&ctx](const Effect& fx){
        return (ctx.musicTime - fx.spawnTime) >= static_cast<int32_t>(fx.duration);
    });

    std::erase_if(ctx.keyBeams, [&ctx](const KeyBeam& beam){
            return (ctx.musicTime - beam.spawnTime) >= 600;
        });

    std::erase_if(ctx.notes, [&ctx](const Note& note){
        if(note.isHolding){
            return false;
        }

        int32_t endTime = note.targetTime;
        if(note.type == NoteType::Long){
            endTime = note.targetTime + note.durationMs;
        }

        bool shouldErase = (ctx.musicTime > endTime) && (ctx.musicTime - endTime > 500);

        return shouldErase;
    });
}