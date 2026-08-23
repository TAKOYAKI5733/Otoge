#pragma once

#include "GameCommon.h"
#include "Play/scoreTracker.h"
#include "Play/type.h"
#include "Play/GameContext.h"
#include "Play/renderGame.h"

inline void updateAndRenderScoreTexture(GameContext& ctx){
    int currentComboNum = ctx.scoreTracker.getCurrentCombo();
    int currentScoreNum = ctx.scoreTracker.getScore();
    int currentTargetScore = ctx.scoreTracker.getVisualTargetScore();

    // スコアのなめらかなイージング補間
    ctx.visualScore += (currentTargetScore - ctx.visualScore) * 0.15;
    int drawScoreVal = static_cast<int>(std::round(ctx.visualScore));

    // スコアポップアップのアニメーション計算
    double score_scale = 1.0;
    if(ctx.scorePop.startTime > 0){
        int32_t elapsed = ctx.musicTime - ctx.scorePop.startTime;
        if(elapsed >= static_cast<int32_t>(ctx.scorePop.duration)){
            ctx.scorePop.startTime = 0;
        }
        else{
            double t = static_cast<double>(elapsed) / ctx.scorePop.duration;
            score_scale = 1.0 + 0.15 * (1.0 - easeOutCubic(t));
        }
    }

    // コンボポップアップのアニメーション計算
    double comboScale = 1.0;
    if(ctx.comboPop.startTime > 0){
        int32_t elapsed = ctx.musicTime - ctx.comboPop.startTime;
        if(elapsed >= static_cast<int32_t>(ctx.comboPop.duration)){
            ctx.comboPop.startTime = 0;
        }
        else{
            double t = static_cast<double>(elapsed) / ctx.comboPop.duration;
            comboScale = 1.0 + 0.35 * (1.0 - easeOutCubic(t));
        }
    }

    // ==========================================
    // 1. スコア用テクスチャの独立した更新処理
    // ==========================================
    if(drawScoreVal != ctx.lastDisplayScoreValue || !ctx.scoreTexture){
        ctx.lastDisplayScoreValue = drawScoreVal;

        if(ctx.scoreTexture){
            SDL_DestroyTexture(ctx.scoreTexture);
            ctx.scoreTexture = nullptr;
        }

        char scoreStr[16];
        snprintf(scoreStr, sizeof(scoreStr), "%07d", drawScoreVal);

        SDL_Color scoreColor = {255, 255, 255, 255};
        SDL_Surface* scoreSurf = TTF_RenderUTF8_Solid(ctx.font, scoreStr, scoreColor);
        if(scoreSurf){
            ctx.scoreTexture = SDL_CreateTextureFromSurface(ctx.renderer, scoreSurf);
            SDL_FreeSurface(scoreSurf);
        }
    }

    // ==========================================
    // 2. コンボ用テクスチャの独立した更新処理
    // ==========================================
    if(currentComboNum != ctx.lastCombo || !ctx.comboTexture){
        ctx.lastCombo = currentComboNum;

        if(ctx.comboTexture){
            SDL_DestroyTexture(ctx.comboTexture);
            ctx.comboTexture = nullptr;
        }

        if(currentComboNum > 0){
            SDL_Color comboColor = {255, 255, 255, 255};
            if(ctx.isAP) comboColor = {255, 215, 0, 255};
            else if(ctx.isFC) comboColor = {0, 255, 255, 255};

            std::string comboStr = std::to_string(currentComboNum) + " COMBO";
            SDL_Surface* surf = TTF_RenderUTF8_Solid(ctx.font, comboStr.c_str(), comboColor);
            if(surf){
                ctx.comboTexture = SDL_CreateTextureFromSurface(ctx.renderer, surf);
                SDL_FreeSurface(surf);
            }
        }
    }

    // ==========================================
    // 3. 描画用矩形（Rect）のサイズ・座標計算
    // ==========================================
    if(ctx.scoreTexture){
        int w = 0, h = 0;
        SDL_QueryTexture(ctx.scoreTexture, nullptr, nullptr, &w, &h);
        ctx.scoreRect.w = static_cast<int>(w * score_scale);
        ctx.scoreRect.h = static_cast<int>(h * score_scale);
        ctx.scoreRect.x = static_cast<int>(SCREEN_W * (7.0 / 8.0) - ctx.scoreRect.w / 2);
        ctx.scoreRect.y = static_cast<int>(80 - ctx.scoreRect.h / 2);
    }

    if(ctx.comboTexture && currentComboNum > 0){
        int w = 0, h = 0;
        SDL_QueryTexture(ctx.comboTexture, nullptr, nullptr, &w, &h);
        ctx.comboRect.w = static_cast<int>(w * comboScale);
        ctx.comboRect.h = static_cast<int>(h * comboScale);
        ctx.comboRect.x = static_cast<int>(SCREEN_W * (7.0 / 8.0) - ctx.comboRect.w / 2);
        ctx.comboRect.y = static_cast<int>(SCREEN_W / 3 - ctx.comboRect.h / 2); // プレビュー等に合わせて調整
    }
}