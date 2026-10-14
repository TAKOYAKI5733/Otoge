#pragma once

#include "GameCommon.h"
#include "Play/GameContext.h"
#include "Play/type.h"

inline constexpr int32_t LONG_RELEASE_GRACE_MS = 200;

// ロングノーツを失敗状態にする。cutTime より上（後ろ）の部分を暗い芯で流す
inline void markLongFailed(Note& note, int32_t cutTime){
    if(note.type != NoteType::Long) return;
    note.isFailed = true;
    note.failCutTime = std::max(cutTime, note.targetTime);
}

// 判定文字と一緒に出す「ズレ」を記録する
inline void recordTiming(JudgeEffect& je, const GameContext& ctx, const Note& note, int judgeType){
    je.hasTiming = true;
    je.diffMs = ctx.musicTime - note.targetTime;   // - = 早い、+ = 遅い
    je.judgeType = judgeType;
    if(ctx.timingLog) ctx.timingLog->push_back({je.diffMs, judgeType});
}

inline void playHitSound(const GameContext& ctx, const Note& note){
    Mix_Chunk* se = ctx.tap_sound;
    if((note.type == NoteType::Normal_c || note.isCyan) && ctx.tap_sound_c){
        se = ctx.tap_sound_c;
    }
    if(se) Mix_PlayChannel(-1, se, 0);
}

inline void toggleLaneActive(GameContext& ctx, const Note& note){
    int startLane = note.lane;
    int endLane = note.lane + note.widthLanes - 1;

    for(int i = startLane; i <= endLane; i++){
        if(i < 0 || i >= 6) continue;
        ctx.laneActive[i] = !ctx.laneActive[i];
    }
}

inline void NoteJudge(GameContext& ctx, Tex& tex, Sq& sq){
    bool consumedThisFrame[6] = {false, false, false, false, false, false};

    for(int i = 0; i < 6; i++){
            if(ctx.currentPressed[i] && !ctx.prevPressed[i] && !consumedThisFrame[i]){
                bool isNoteHit = false;

                for(auto& note : ctx.notes){
                    // ドラッグと、ドラッグロング（始点がドラッグ判定）は叩いて判定しない → 下の「ドラッグの自動判定」で扱う
                    if(note.lane < 0 || note.isHit || note.type == NoteType::Drag || note.isDragLong()) continue;
                    
                    int startLane = note.lane;
                    int endLane   = note.lane + note.widthLanes - 1;
                    bool isAreaPressed = (i >= startLane && i <= endLane);

                    if(isAreaPressed){
                        int timeDiff = std::abs(static_cast<int>(ctx.musicTime - note.targetTime));

                        if(timeDiff > 300) continue;

                        Effect newEffect;
                        newEffect.lane = note.lane;
                        newEffect.spawnTime = ctx.musicTime;

                        if(timeDiff <= 60){
                            ctx.laneJudge[i] = {tex.perfect, sq.perfect, ctx.musicTime};
                            recordTiming(ctx.laneJudge[i], ctx, note, 1);
                            note.isHit = true;
                            newEffect.lane = note.lane;
                            newEffect.widthLanes = note.widthLanes;
                            newEffect.judgeType = 1;
                            ctx.effects.push_back(newEffect);
                            ctx.scoreTracker.registerJudge("PERFECT");
                            ctx.scorePop.startTime = ctx.musicTime;
                            ctx.comboPop.startTime = ctx.musicTime;

                            playHitSound(ctx, note);

                            if(note.type == NoteType::Long){
                                note.isHolding = true;
                                note.lastTickTime = ctx.musicTime;
                            }

                            if(note.type == NoteType::Lane) toggleLaneActive(ctx, note);
                        }

                        else if(timeDiff <= 80){
                            ctx.laneJudge[i] = {tex.good, sq.good, ctx.musicTime};
                            recordTiming(ctx.laneJudge[i], ctx, note, 2);
                            note.isHit = true;
                            newEffect.judgeType = 2;
                            newEffect.lane = note.lane;
                            newEffect.widthLanes = note.widthLanes;
                            ctx.effects.push_back(newEffect);

                            ctx.isAP = false;
                            ctx.scoreTracker.registerJudge("GOOD");
                            ctx.scorePop.startTime = ctx.musicTime;
                            ctx.comboPop.startTime = ctx.musicTime;

                            playHitSound(ctx, note);

                            if(note.type == NoteType::Long){
                                note.isHolding = true;
                                note.lastTickTime = ctx.musicTime;
                            }

                            if(note.type == NoteType::Lane) toggleLaneActive(ctx, note);
                        }
                        
                        else if(timeDiff <= 90){
                            ctx.laneJudge[i] = {tex.bad, sq.bad, ctx.musicTime};
                            recordTiming(ctx.laneJudge[i], ctx, note, 3);
                            note.isHit = true;
                            markLongFailed(note, note.targetTime);
                            newEffect.judgeType = 3;
                            newEffect.lane = note.lane;
                            newEffect.widthLanes = note.widthLanes;

                            ctx.isAP = ctx.isFC = false;
                            ctx.effects.push_back(newEffect);
                            ctx.scoreTracker.registerJudge("BAD");
                            if(note.type == NoteType::Lane) toggleLaneActive(ctx, note);
                        }
                        isNoteHit = true;

                        for(int l = std::max(0, startLane); l <= std::min(5, endLane); l++){
                            consumedThisFrame[l] = true;
                        }

                        break;
                    }
                }
            }
            ctx.prevPressed[i] = ctx.currentPressed[i];
        }

        //トレースノーツ(ドラッグ)の自動判定 (毎フレーム監視)
        //   ドラッグロングの始点もここで判定する（押しっぱなしで通過すれば PERFECT → そのままホールドへ）
        for(auto& note : ctx.notes){
            if(note.lane < 0 || note.isHit || !(note.type == NoteType::Drag || note.isDragLong())) continue;

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

                    // ドラッグロング：始点を取れたら、ここからは普通のロングと同じホールド判定（下の 4.）に渡す
                    if(note.isDragLong()){
                        note.isHolding = true;
                        note.lastTickTime = ctx.musicTime;
                    }
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
                markLongFailed(note, note.targetTime);
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
            if(!isHoldingArea && !ln.releaseGrace){
                if(endTime - ctx.musicTime <= LONG_RELEASE_GRACE_MS){
                    // 終点直前：失敗にせず、ホールドしている扱いのまま終点まで流す
                    // 判定と音は下の「終点に無事到達した場合」で、終点の時刻に出す
                    ln.releaseGrace = true;
                }
                else{
                    // 早すぎる：MISS
                    ln.isHolding = false;
                    ln.isHit = true;
                    ctx.laneJudge[i] = {tex.miss, sq.miss, ctx.musicTime};
                    Mix_PlayChannel(-1, ctx.tap_sound, 0);
                    ctx.scoreTracker.registerJudge("MISS");
                    markLongFailed(ln, ctx.musicTime);

                    ctx.isAP = ctx.isFC = false;
                    continue;
                }
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