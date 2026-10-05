#pragma once

#include "GameCommon.h"
#include "Play/type.h"
#include "Play/GameContext.h"
#include "Play/roundedRect.h"
#include "Play/tutorialOverlay.h"
#include "Play/visualizer.h"

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

inline void renderGamePlayScreen(GameContext& ctx, Tex& tex, Sq& sq, double currentNoteSpeed, int laneX[6]){
    GradientBackground(ctx.renderer, SCREEN_W, SCREEN_H, ctx.musicTime);
    draw_waku_init(ctx.renderer, tex, sq, ctx.laneActive);
    if(ctx.visualizer) ctx.visualizer->render(ctx.renderer);

    SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(ctx.renderer, 255, 255, 255, 255);
    RoundedRectBatch noteBatch;
    
    for(const auto& note : ctx.notes){
        if(note.lane < 0 || note.lane >= 6) continue;

        if(note.isHit && !note.isHolding && ctx.musicTime >= note.targetTime) continue;

        double effectiveSpeed = note.hasCustomSpeed ? note.customSpeed : currentNoteSpeed;

        int noteY;
        if(note.hasCustomPath()){
            double distFromLine = evaluatNotePathY(note, ctx.musicTimeExact);
            noteY = ctx.judgeY - static_cast<int>(distFromLine);
        }
        else{
            noteY = ctx.judgeY - static_cast<int>((static_cast<double>(note.targetTime) - ctx.musicTimeExact) * effectiveSpeed);
        }
        
        if(noteY < -50) continue;

        bool laneIsInactive = !ctx.laneActive[note.lane];

        if(note.type == NoteType::Long){
            int tailY = ctx.judgeY - static_cast<int>((static_cast<double>(note.targetTime + note.durationMs) - ctx.musicTimeExact) * effectiveSpeed);

            int drawNoteY = noteY;
            if(note.isHolding){
                drawNoteY = ctx.judgeY;
            }

            SDL_Rect bodyRect;
            bodyRect.x = laneX[note.lane];
            bodyRect.w = ctx.laneWidth * note.widthLanes;

            bodyRect.y = tailY;
            bodyRect.h = drawNoteY - tailY;

            if(laneIsInactive){
                SDL_SetRenderDrawColor(ctx.renderer, 0, 0, 0, 150);
            }
            else if(note.isHolding){
                SDL_SetRenderDrawColor(ctx.renderer, 0, 150, 255, 255);
            }
            else{
                SDL_SetRenderDrawColor(ctx.renderer, 0, 150, 255, 100);
            }

            SDL_RenderFillRect(ctx.renderer, &bodyRect);
        }

        if(!note.isHit){
            // 色は頂点に持たせるので、テクスチャも ColorMod/AlphaMod も使わない
            SDL_Color col;
            if(laneIsInactive && !(note.type == NoteType::Lane)) col = {20, 20, 20, 200};
            else if(note.type == NoteType::Drag)                 col = {250, 250, 150, 255};
            else if(note.type == NoteType::Lane)                 col = {255, 50, 50, 255};
            else                                                 col = {255, 255, 255, 255};

            // ここでは溜めるだけ。実際の描画はループの後で1回にまとめて行う
            noteBatch.add(static_cast<float>(laneX[note.lane]),
                          static_cast<float>(noteY - 15),
                          static_cast<float>(ctx.laneWidth * note.widthLanes),
                          30.0f,    // 高さ
                          8.0f,     // 角の半径(px)。丸みを変えるならこの数値
                          col);
        }
    }

    // 溜めたノーツのヘッドを1回の描画でまとめて出す(ロングノーツの帯より手前に描かれる)
    noteBatch.flush(ctx.renderer);

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