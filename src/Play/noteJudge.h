#pragma once

#include "GameCommon.h"
#include "Play/GameContext.h"
#include "Play/type.h"

inline void toggleLaneActive(GameContext& ctx, const Note& note){
    int startLane = note.lane;
    int endLane = note.lane + note.widthLanes - 1;

    for(int i = startLane; i <= endLane; i++){
        if(i < 0 || i >= 6) continue;
        ctx.laneActive[i] = !ctx.laneActive[i];
    }
}

inline void NoteJudge(GameContext& ctx, Tex& tex, Sq& sq){
    for(int i = 0; i < 6; i++){
            if(ctx.currentPressed[i] && !ctx.prevPressed[i]){
                bool isNoteHit = false;

                for(auto& note : ctx.notes){
                    if(note.lane < 0 || note.isHit || note.type == NoteType::Drag) continue;
                    
                    bool isAreaPressed = false;
                    if(note.widthLanes > 1){
                        int startLane = note.lane;
                        int endLane = note.lane + note.widthLanes - 1;
                        
                        for(int l = startLane; l <= endLane; l++){
                            if(ctx.currentPressed[l]){
                                isAreaPressed = true;
                                break;
                            }
                        }
                    }
                    else{
                        isAreaPressed = (note.lane == i);
                    }

                    if(isAreaPressed){
                        int timeDiff = std::abs(static_cast<int>(ctx.musicTime - note.targetTime));

                        if(timeDiff > 300) continue;

                        Effect newEffect;
                        newEffect.lane = note.lane;
                        newEffect.spawnTime = ctx.musicTime;

                        if(timeDiff <= 60){
                            ctx.laneJudge[i] = {tex.perfect, sq.perfect, ctx.musicTime};
                            note.isHit = true;
                            newEffect.lane = note.lane;
                            newEffect.widthLanes = note.widthLanes;
                            newEffect.judgeType = 1;
                            ctx.effects.push_back(newEffect);
                            ctx.scoreTracker.registerJudge("PERFECT");
                            ctx.scorePop.startTime = ctx.musicTime;
                            ctx.comboPop.startTime = ctx.musicTime;

                            if(ctx.tap_sound){
                                Mix_PlayChannel(-1, ctx.tap_sound, 0);
                            }

                            if(note.type == NoteType::Long){
                                note.isHolding = true;
                                note.lastTickTime = ctx.musicTime;
                            }

                            if(note.type == NoteType::Lane) toggleLaneActive(ctx, note);
                        }

                        else if(timeDiff <= 80){
                            ctx.laneJudge[i] = {tex.good, sq.good, ctx.musicTime};
                            note.isHit = true;
                            newEffect.judgeType = 2;
                            newEffect.lane = note.lane;
                            newEffect.widthLanes = note.widthLanes;
                            ctx.effects.push_back(newEffect);

                            ctx.isAP = false;
                            ctx.scoreTracker.registerJudge("GOOD");
                            ctx.scorePop.startTime = ctx.musicTime;
                            ctx.comboPop.startTime = ctx.musicTime;

                            if(ctx.tap_sound){
                                Mix_PlayChannel(-1, ctx.tap_sound, 0);
                            }

                            if(note.type == NoteType::Long){
                                note.isHolding = true;
                                note.lastTickTime = ctx.musicTime;
                            }

                            if(note.type == NoteType::Lane) toggleLaneActive(ctx, note);
                        }
                        
                        else if(timeDiff <= 90){
                            ctx.laneJudge[i] = {tex.bad, sq.bad, ctx.musicTime};
                            note.isHit = true;
                            newEffect.judgeType = 3;
                            newEffect.lane = note.lane;
                            newEffect.widthLanes = note.widthLanes;

                            ctx.isAP = ctx.isFC = false;
                            ctx.effects.push_back(newEffect);
                            ctx.scoreTracker.registerJudge("BAD");
                            if(note.type == NoteType::Lane) toggleLaneActive(ctx, note);
                        }
                        isNoteHit = true;
                        break;
                    }
                }
                if(!isNoteHit){
                    Effect blankEffect;
                    blankEffect.lane = i;
                    blankEffect.spawnTime = ctx.musicTime;
                    blankEffect.judgeType = 0;
                    ctx.effects.push_back(blankEffect);
                }
            }
            ctx.prevPressed[i] = ctx.currentPressed[i];
        }

        //トレースノーツ(ドラッグ)の自動判定 (毎フレーム監視)
        for(auto& note : ctx.notes){
            if(note.lane < 0 || note.isHit || !(note.type == NoteType::Drag)) continue;

            int i = note.lane;
            int32_t timeDiff = ctx.musicTime - note.targetTime;

            bool isAreaPresssed = false;
            if(note.widthLanes > 1){
                int startLane = note.lane;
                int endLane = note.lane + note.widthLanes - 1;
                for(int l = startLane; l <= endLane; l++){
                    if(ctx.currentPressed[l]){
                        isAreaPresssed = true;
                        break;
                    }
                }
            }
            else{
                isAreaPresssed = ctx.currentPressed[note.lane];
            }

            // 判定ラインに重なる前後60ms以内に、そのレーンが現在押されていれば自動Perfect
            if(isAreaPresssed){
                if(timeDiff >= 0 && timeDiff <= 60){
                    ctx.laneJudge[i] = {tex.perfect, sq.perfect, ctx.musicTime};
                    note.isHit = true;
                    ctx.scoreTracker.registerJudge("PERFECT");
                    ctx.scorePop.startTime = ctx.musicTime;
                    ctx.comboPop.startTime = ctx.musicTime;

                    Effect dragEffect;
                    dragEffect.lane = i;
                    dragEffect.spawnTime = ctx.musicTime;
                    dragEffect.widthLanes = note.widthLanes;
                    dragEffect.judgeType = 1;
                    ctx.effects.push_back(dragEffect);

                    Mix_PlayChannel(-1, ctx.tap_sound, 0);
                }
            }
        }

        // --- 3. MISS判定 (スルーしたノーツの処理) ---
        for(auto& note: ctx.notes){
            if(note.lane < 0) continue;

            if(note.isHit || note.isHolding) continue;

            if(ctx.musicTime > note.targetTime && (ctx.musicTime - note.targetTime > 200)){
                ctx.laneJudge[note.lane]  = {tex.miss, sq.miss, ctx.musicTime};
                note.isHit = true;
                ctx.scoreTracker.registerJudge("MISS");
                
                ctx.isAP = ctx.isFC = false;
                if(note.type == NoteType::Lane) toggleLaneActive(ctx, note);
            }
        }

    
    // --- 4. ロングノーツの継続ホールド・終点判定 ---
    for(auto& ln : ctx.notes){
        if(ln.type == NoteType::Long && ln.isHolding){
            int i = ln.lane;
            int32_t endTime = ln.targetTime + ln.durationMs;

            bool isHoldingArea = false;
            if(ln.widthLanes > 1){
                int startLane = ln.lane;
                int endLane = ln.lane + ln.widthLanes - 1;
                for(int l = startLane; l <= endLane; l++){
                    if(ctx.currentPressed[l]){
                        isHoldingArea = true;
                        break;
                    }
                }
            }
            else{
                isHoldingArea = ctx.currentPressed[ln.lane];
            }

            // 途中で指を離した場合
            if(!isHoldingArea){
                ln.isHolding = false;
                ln.isHit = true;
                if(endTime - ctx.musicTime <= 200){
                    ctx.laneJudge[i] = {tex.perfect, sq.perfect, ctx.musicTime};
                    Mix_PlayChannel(-1, ctx.tap_sound, 0);
                    ctx.scoreTracker.registerJudge("PERFECT");
                    ctx.scorePop.startTime = ctx.musicTime;
                    ctx.comboPop.startTime = ctx.musicTime;
                }
                else{
                    ctx.laneJudge[i] = {tex.miss, sq.miss, ctx.musicTime};
                    Mix_PlayChannel(-1, ctx.tap_sound, 0);
                    ctx.scoreTracker.registerJudge("MISS");

                    ctx.isAP = ctx.isFC = false;
                }
                continue;
            }

            // 終点に無事到達した場合
            if(ctx.musicTime >= endTime){
                ln.isHolding = false;
                ln.isHit = true;
                ctx.scoreTracker.registerJudge("PERFECT");
                ctx.scorePop.startTime = ctx.musicTime;
                ctx.comboPop.startTime = ctx.musicTime;

                ctx.laneJudge[i] = {tex.perfect, sq.perfect, ctx.musicTime};
                ctx.comboCount++;
                Mix_PlayChannel(-1, ctx.tap_sound, 0);
            }
        }
    }
}