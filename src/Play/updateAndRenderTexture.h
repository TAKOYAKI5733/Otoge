#pragma once

#include "GameCommon.h"
#include "Play/scoreTracker.h"
#include "Play/type.h"
#include "Play/GameContext.h"
#include "Play/renderGame.h"

// スコアとコンボの「表示する値」と「ポップアップの拡大率」だけを計算する。
// テクスチャの生成はしない（描画は renderGame.h で DigitFont を使って行う）。
inline void updateAndRenderScoreTexture(GameContext& ctx){
    int currentTargetScore = ctx.scoreTracker.getVisualTargetScore();

    // スコアのなめらかなイージング補間（fpsが変わっても同じ速さ）
    ctx.visualScore += (currentTargetScore - ctx.visualScore) * lerpFactor(0.15);
    ctx.lastDisplayScoreValue = static_cast<int>(std::round(ctx.visualScore));

    // スコアポップアップのアニメーション計算
    ctx.scoreScale = 1.0;
    if(ctx.scorePop.startTime > 0){
        int32_t elapsed = ctx.musicTime - ctx.scorePop.startTime;
        if(elapsed >= static_cast<int32_t>(ctx.scorePop.duration)){
            ctx.scorePop.startTime = 0;
        }
        else{
            double t = static_cast<double>(elapsed) / ctx.scorePop.duration;
            ctx.scoreScale = 1.0 + 0.15 * (1.0 - easeOutCubic(t));
        }
    }

    // コンボポップアップのアニメーション計算
    ctx.comboScale = 1.0;
    if(ctx.comboPop.startTime > 0){
        int32_t elapsed = ctx.musicTime - ctx.comboPop.startTime;
        if(elapsed >= static_cast<int32_t>(ctx.comboPop.duration)){
            ctx.comboPop.startTime = 0;
        }
        else{
            double t = static_cast<double>(elapsed) / ctx.comboPop.duration;
            ctx.comboScale = 1.0 + 0.35 * (1.0 - easeOutCubic(t));
        }
    }
}