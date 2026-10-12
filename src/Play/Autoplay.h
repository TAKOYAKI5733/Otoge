#pragma once

#include "GameCommon.h"
#include "Play/type.h"
#include "Play/GameContext.h"

// =====================================================================
//  オートプレイ：毎フレーム ctx.currentPressed を「人間ならこう押す」状態に書き換える
//    ・ノーツの幅（widthLanes）ぶんのレーンをすべて押す
//    ・幅広ロングのホールド中でも、範囲内にタップが来るレーンは直前で離して叩き直す
//  判定（NoteJudge）より前に呼ぶ
// =====================================================================

// タップ系ノーツが来るこの時間(ms)前から、そのレーンのホールドを離しておく
inline constexpr int32_t AUTOPLAY_RELEASE_LEAD_MS = 40;

// ノーツがまたいでいるレーンの範囲 [start, end]（画面外のレーン番号は切り捨てる）
inline void autoplayLaneRange(const Note& note, int& start, int& end){
    start = std::max(0, note.lane);
    end   = std::min(5, note.lane + std::max(1, note.widthLanes) - 1);
}

inline void applyAutoplay(GameContext& ctx, int32_t prevMusicTime){
    bool press[6]   = {false, false, false, false, false, false};
    bool tapSoon[6] = {false, false, false, false, false, false};   // もうすぐタップが来るレーン

    // ---- 1回目：タップ系とドラッグ ----
    for(const auto& note : ctx.notes){
        if(note.lane < 0 || note.lane >= 6) continue;

        // ノーツは時刻順に並んでいるので、これより先はまだ関係ない
        if(note.targetTime > ctx.musicTime + AUTOPLAY_RELEASE_LEAD_MS) break;

        if(note.isHit) continue;   // 叩き済み（ホールド中のロングは2回目で扱う）

        int start, end;
        autoplayLaneRange(note, start, end);

        if(note.type == NoteType::Drag){
            // ドラッグは「押されている状態」で判定されるので、判定時間の間ずっと押す
            if(ctx.musicTime >= note.targetTime && ctx.musicTime <= note.targetTime + 60){
                for(int l = start; l <= end; l++) press[l] = true;
            }
            continue;
        }

        if(prevMusicTime < note.targetTime && ctx.musicTime >= note.targetTime){
            // 判定ラインを越えた瞬間のフレームで、幅ぶん全部押す
            for(int l = start; l <= end; l++) press[l] = true;
        }
        else if(ctx.musicTime < note.targetTime){
            // まだ来ていない：このレーンはホールドしていても直前で離しておく
            for(int l = start; l <= end; l++) tapSoon[l] = true;
        }
    }

    // ---- 2回目：ホールド中のロングノーツ ----
    for(const auto& note : ctx.notes){
        if(note.type != NoteType::Long || !note.isHolding) continue;

        int start, end;
        autoplayLaneRange(note, start, end);

        // 範囲のうち、タップが来ないレーンを押し続ける（どれか1つでも押していればホールドは続く）
        bool anyHeld = false;
        for(int l = start; l <= end; l++){
            if(!tapSoon[l]){
                press[l] = true;
                anyHeld = true;
            }
        }
        // 範囲の全レーンにタップが来る場合は、ホールドを切らさないよう左端だけは押し続ける
        if(!anyHeld) press[start] = true;
    }

    for(int i = 0; i < 6; i++) ctx.currentPressed[i] = press[i];
}