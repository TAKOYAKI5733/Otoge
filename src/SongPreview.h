#pragma once
// =====================================================================
//  選曲画面の試聴（SongPreview.h）
//    ・譜面の "previewStart"（曲ファイル上の位置 ms）から LENGTH_MS だけ流す
//    ・最初はフェードイン、終わりの FADE_OUT_MS でフェードアウト
//    ・フェードアウトしきって音が止まったら、また開始位置からフェードインする（これを繰り返す）
//  ※ SDL / SDL_mixer は GameCommon.h から読むので、GameCommon.h の後に include すること
// =====================================================================
#include <algorithm>

namespace songpreview {
constexpr int LENGTH_MS   = 10000;   // 1回の試聴の長さ（フェードアウトを含む）
constexpr int FADE_IN_MS  = 1000;    // フェードインにかける時間
constexpr int FADE_OUT_MS = 1500;    // フェードアウトにかける時間（LENGTH_MS の最後の部分）
}

class SongPreview{
public:
    // music を試聴として鳴らし始める。startMs = 曲ファイル上の位置(ms)
    void start(Mix_Music* music, double startMs){
        music_ = music;
        active_ = (music != nullptr);
        startSec_ = clampStartSec(music, startMs);
        if(active_) begin();
    }

    // 試聴をやめる（曲を解放する前・デフォルト BGM に切り替える前に必ず呼ぶ）
    void stop(){
        active_ = false;
        music_ = nullptr;
    }

    // 毎フレーム呼ぶ
    void update(){
        if(!active_ || !music_) return;

        const Uint32 elapsed = SDL_GetTicks() - beginTicks_;

        // 1. 終わりが近づいたらフェードアウトを始める（1回だけ）
        if(!fadingOut_ && elapsed >= static_cast<Uint32>(songpreview::LENGTH_MS - songpreview::FADE_OUT_MS)){
            Mix_FadeOutMusic(songpreview::FADE_OUT_MS);
            fadingOut_ = true;
        }

        // 2. 音が止まった = フェードアウトが終わった（または曲が先に終わった）→ もう一度
        if(Mix_PlayingMusic() == 0){
            begin();
        }
    }

private:
    Mix_Music* music_ = nullptr;
    bool   active_ = false;
    bool   fadingOut_ = false;
    double startSec_ = 0.0;
    Uint32 beginTicks_ = 0;

    // 開始位置からフェードインで鳴らす
    void begin(){
        // loops = -1：曲が10秒より短くても途中で無音にならないように
        if(Mix_FadeInMusicPos(music_, -1, songpreview::FADE_IN_MS, startSec_) != 0){
            Mix_FadeInMusic(music_, -1, songpreview::FADE_IN_MS);   // 位置指定に失敗したら曲の頭から
        }
        beginTicks_ = SDL_GetTicks();
        fadingOut_ = false;
    }

    // 曲の長さを超えた位置が入っていたら、曲の頭からにする
    static double clampStartSec(Mix_Music* music, double startMs){
        double sec = std::max(0.0, startMs / 1000.0);
        if(music){
            const double dur = Mix_MusicDuration(music);   // 分からないときは 0 以下
            if(dur > 0.0 && sec >= dur - 1.0) sec = 0.0;
        }
        return sec;
    }
};