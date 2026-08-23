#pragma once

#include "GameCommon.h"
#include "Play/type.h"
#include "Play/GameContext.h"

inline void renderGamePlayScreen(GameContext& ctx, Tex& tex, Sq& sq, double currentNoteSpeed, int laneX[6]){
    GradientBackground(ctx.renderer, SCREEN_W, SCREEN_H, ctx.musicTime);
    draw_waku_init(ctx.renderer, tex, sq, ctx.laneActive);

    SDL_SetRenderDrawColor(ctx.renderer, 255, 255, 255, 255);
    for(const auto& note : ctx.notes){
        if(note.lane < 0 || note.lane >= 6) continue;

        if(!ctx.laneActive[note.lane] && note.type != NoteType::Lane) continue;

        if(note.isHit && !note.isHolding && ctx.musicTime >= note.targetTime) continue;

        double effectiveSpeed = note.hasCustomSpeed ? note.customSpeed : currentNoteSpeed;

        int noteY = ctx.judgeY - static_cast<int>((static_cast<double>(note.targetTime) - ctx.musicTime) * effectiveSpeed);
        if(noteY < -50) continue;

        if(note.type == NoteType::Long){
            int tailY = ctx.judgeY - static_cast<int>((static_cast<double>(note.targetTime + note.durationMs) - ctx.musicTime) * effectiveSpeed);

            int drawNoteY = noteY;
            if(note.isHolding){
                drawNoteY = ctx.judgeY;
            }

            SDL_Rect bodyRect;
                bodyRect.x = laneX[note.lane];
                bodyRect.w = ctx.laneWidth * note.widthLanes;

                if(note.isHolding){
                    SDL_SetRenderDrawColor(ctx.renderer, 0, 150, 255, 255);
                }
                else{
                    SDL_SetRenderDrawColor(ctx.renderer, 0, 150, 255, 100);
                }

            bodyRect.y = tailY;
            bodyRect.h = drawNoteY - tailY;

            SDL_RenderFillRect(ctx.renderer, &bodyRect);
        }

        if(!note.isHit){
            SDL_Rect noteRect;
                noteRect.w = ctx.laneWidth * note.widthLanes;
                noteRect.h = 30;
                noteRect.x = laneX[note.lane];
                noteRect.y = noteY - 15;
                
                // 🌟 5. トレースノーツの描画（金色に変えて差別化）
                if(note.type == NoteType::Drag) SDL_SetRenderDrawColor(ctx.renderer, 250, 250, 150, 255); 
                else if(note.type == NoteType::Lane) SDL_SetRenderDrawColor(ctx.renderer, 255, 50, 50, 255); 
                else SDL_SetRenderDrawColor(ctx.renderer, 255, 255, 255, 255);
            SDL_RenderFillRect(ctx.renderer, &noteRect);
        }
    }

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

    for(int i = 0; i < 6; i++){
        if(ctx.laneJudge[i].texture != nullptr){
            SDL_RenderCopy(ctx.renderer, ctx.laneJudge[i].texture, NULL, &ctx.laneJudge[i].rect);
        }
    }

    int currentComboCount = ctx.scoreTracker.getCurrentCombo();
    if(ctx.scoreTexture != nullptr){
        SDL_RenderCopy(ctx.renderer, ctx.scoreTexture, NULL, &ctx.scoreRect);
    }
    if(ctx.comboTexture != nullptr && ctx.scoreTracker.getCurrentCombo() > 0){
        SDL_RenderCopy(ctx.renderer, ctx.comboTexture, NULL, &ctx.comboRect);
    }

    SDL_RenderPresent(ctx.renderer);
}

inline void erase_some(GameContext& ctx){
    std::erase_if(ctx.effects, [&ctx](const Effect& fx){
        return (ctx.musicTime - fx.spawnTime) >= static_cast<int32_t>(fx.duration);
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