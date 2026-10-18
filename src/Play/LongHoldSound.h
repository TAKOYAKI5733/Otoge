#pragma once

#include "GameCommon.h"
#include "Play/type.h"
#include <vector>

// =====================================================================
//  ロングを押している間だけ、ループの効果音を鳴らす
//    ・ミキサーのチャンネルを1つ「ロング専用」に取っておき、そこでくり返し鳴らす
//    ・押しているロングが1つでもあれば鳴らし、1つもなくなったら短くフェードアウトして止める
//    ・同時に何本押していても、鳴るのは1つだけ（音が重なって大きくならない）
// =====================================================================
class LongHoldSound{
public:
    static constexpr int FADE_IN_MS  = 30;   // 鳴り始め（ブツッという音を防ぐ）
    static constexpr int FADE_OUT_MS = 80;   // 止めるとき

    // プレイ開始時に1回。path の音が無ければ何もしない（ゲームはそのまま動く）
    void init(const char* path, int seVolumePercent){
        chunk_ = Mix_LoadWAV(path);
        if(!chunk_){
            printf("[ロング音] 読込失敗（無しで続けます）: %s\n", path);
            return;
        }
        Mix_VolumeChunk(chunk_, static_cast<int>(seVolumePercent * MIX_MAX_VOLUME / 100.0));

        // チャンネル 0 を予約：Mix_PlayChannel(-1, ...) のタップ音などには使われなくなる
        Mix_ReserveChannels(1);
        channel_ = 0;
    }

    // 毎フレーム、判定（NoteJudge）の後に呼ぶ
    void update(const std::vector<Note>& notes, bool paused){
        if(!chunk_) return;

        // ポーズ中は一時停止、再開したら続きから
        if(paused){
            if(playing_ && !pausedNow_){ Mix_Pause(channel_); pausedNow_ = true; }
            return;
        }
        if(pausedNow_){ Mix_Resume(channel_); pausedNow_ = false; }

        // 「今、指で押さえているロング」があるか
        //   releaseGrace = 終点の直前で指を離した状態（判定は成功扱いだが、もう押していない）→ 鳴らさない
        bool holding = false;
        for(const Note& n : notes){
            if(n.type == NoteType::Long && n.isHolding && !n.releaseGrace){
                holding = true;
                break;
            }
        }

        if(holding && !playing_){
            Mix_FadeInChannel(channel_, chunk_, -1, FADE_IN_MS);   // -1 = ずっとくり返す
            playing_ = true;
        }
        else if(!holding && playing_){
            Mix_FadeOutChannel(channel_, FADE_OUT_MS);
            playing_ = false;
        }
    }

    // プレイ終了時に1回
    void destroy(){
        if(channel_ >= 0) Mix_HaltChannel(channel_);
        if(chunk_){
            Mix_FreeChunk(chunk_);
            chunk_ = nullptr;
        }
        if(channel_ >= 0) Mix_ReserveChannels(0);   // 予約を戻す（ほかの画面が全部のチャンネルを使えるように）
        channel_ = -1;
        playing_ = false;
        pausedNow_ = false;
    }

private:
    Mix_Chunk* chunk_ = nullptr;
    int  channel_ = -1;
    bool playing_ = false;
    bool pausedNow_ = false;
};