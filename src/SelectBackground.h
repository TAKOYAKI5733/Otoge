#pragma once

// =====================================================================
//  SelectBackground.h — 選曲画面の背景
//
//  ・選んでいる曲のジャケット絵を、プレイ画面と同じぼかし方（PlayBackground::buildBlurredJacket）で真ん中に敷く
//    ぼかした絵は曲ごとに 1 回だけ作って取っておく（64×64 の小さな絵なので、たくさん持っても軽い）
//  ・別の曲に移ったら、前の見た目から新しい見た目へ、なめらかに切り替える（中割り）
//  ・低音に合わせて、背景だけを少しズームする（一覧・ジャケット・文字はズームしない）
//
//  ■ 切り替え（中割り）のしかた
//    背景は毎フレーム、画面と同じ大きさの絵 bg_ に組み立ててから画面に貼る。
//    曲が変わった瞬間に、その時点の bg_ を snap_ に写して取っておき（スナップショット）、
//      bg_ = 「新しい曲の背景（土台の色 ＋ ぼかし）」の上に、snap_ を (1 − t) の濃さで重ねたもの
//    とする。t は 0 → 1 に FADE_MS かけて進み、なめらかな曲線（smoothstep：始めと終わりがゆっくり）を通す。
//    → 色が「前の絵 × (1−t) ＋ 新しい絵 × t」になり、グラデーションのように切り替わる。
//    切り替えの途中でまた曲が変わっても、その瞬間の見た目をそのまま写すので、途中から飛ばずにつながる
//
//  ■ 低音のズーム
//    JacketSpectrum の一番下の LOW_BARS 本（約 40〜125Hz）の平均が ZOOM_THRESHOLD より上の分を使い、
//    大きくなるときは速く・戻るときはゆっくり近づける（プレイ画面と同じやり方）。
//    bg_ を (1 + ZOOM_AMOUNT × 強さ) 倍にして、画面の真ん中を中心に貼る。設定の「低音ズーム」が OFF なら大きくしない
// =====================================================================

#include "Play/PlayBackground.h"
#include <unordered_map>
#include <vector>
#include <string>

class SelectBackground{
public:
    // ----- 調整用 -----
    static constexpr SDL_Color BASE_COLOR = {15, 15, 25, 255};   // 土台の色（今までの選曲画面の色）
    static constexpr double FADE_MS        = 450.0;   // 曲を変えたときの切り替えの長さ(ms)
    static constexpr int    LOW_BARS       = 2;       // 低音として使うバーの本数（JacketSpectrum の下から）
    static constexpr float  ZOOM_THRESHOLD = 0.5f;    // 低音の強さ（0〜1）がこれより上の分だけ使う
    static constexpr float  ZOOM_AMOUNT    = 0.04f;   // 一番強いときに、どれだけ大きくするか（0.04 = 4%）
    static constexpr float  ZOOM_ATTACK_MS  = 15.0f;  // 大きくなる速さ（小さいほど速い）
    static constexpr float  ZOOM_RELEASE_MS = 140.0f; // 戻る速さ（大きいほどゆっくり）

    SelectBackground() = default;
    ~SelectBackground(){ destroy(); }
    SelectBackground(const SelectBackground&) = delete;
    SelectBackground& operator=(const SelectBackground&) = delete;

    void init(SDL_Renderer* renderer){
        destroy();
        bg_   = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);
        snap_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);
        if(bg_){
            SDL_SetTextureScaleMode(bg_, SDL_ScaleModeLinear);   // ズームするとき、なめらかに
            SDL_SetTextureBlendMode(bg_, SDL_BLENDMODE_NONE);    // 不透明な絵として貼る
        }
        if(snap_) SDL_SetTextureBlendMode(snap_, SDL_BLENDMODE_BLEND);
        lastTick_ = SDL_GetTicks();
    }

    void destroy(){
        for(auto& kv : cache_) if(kv.second) SDL_DestroyTexture(kv.second);
        cache_.clear();
        if(bg_){ SDL_DestroyTexture(bg_); bg_ = nullptr; }
        if(snap_){ SDL_DestroyTexture(snap_); snap_ = nullptr; }
        cur_ = nullptr;
        curKey_.clear();
        hasFrame_ = snapValid_ = false;
    }

    // 今の曲を伝える（毎フレーム呼んでよい。変わったときだけ切り替えが始まる）
    //   key：曲を見分ける文字（譜面のパス）。"" なら「ジャケット無し」（ジャンル選択中など）
    //   candidates：ジャケット絵の場所の候補（初めての曲のときだけ使う）
    void setSong(SDL_Renderer* renderer, const std::string& key, const std::vector<std::string>& candidates){
        if(key == curKey_) return;

        // 今の見た目（前のフレームの bg_）を snap_ に写して取っておく
        if(hasFrame_ && bg_ && snap_){
            SDL_Texture* prevTarget = SDL_GetRenderTarget(renderer);
            SDL_SetRenderTarget(renderer, snap_);
            SDL_RenderCopy(renderer, bg_, nullptr, nullptr);   // bg_ は BLENDMODE_NONE なので、そのまま写る
            SDL_SetRenderTarget(renderer, prevTarget);
            snapValid_ = true;
        }

        curKey_ = key;
        cur_ = key.empty() ? nullptr : blurredFor(renderer, key, candidates);   // 見つからなかった曲は nullptr（土台の色だけ）
        fadeStart_ = SDL_GetTicks();
    }

    // 曲のぼかした絵（初めての曲だけ作って取っておく）。選曲画面のジャケットの帯（SelectScene.cpp）も、これを使う
    //   読めなかった曲は nullptr。返した絵はこのクラスが持っているので、使う側で消さないこと
    SDL_Texture* blurredFor(SDL_Renderer* renderer, const std::string& key, const std::vector<std::string>& candidates){
        auto it = cache_.find(key);
        if(it == cache_.end()){
            it = cache_.emplace(key, PlayBackground::buildBlurredJacket(renderer, candidates)).first;
        }
        return it->second;
    }

    // 低音の強さ（0〜1）からズームの強さを更新する（毎フレーム 1 回）
    void updateZoom(float low01, bool enabled){
        const Uint32 now = SDL_GetTicks();
        const float dt = static_cast<float>(std::min<Uint32>(now - lastTick_, 100));
        lastTick_ = now;
        const float target = enabled
            ? std::clamp((low01 - ZOOM_THRESHOLD) / (1.0f - ZOOM_THRESHOLD), 0.0f, 1.0f) : 0.0f;
        const float tau = (target > zoom_) ? ZOOM_ATTACK_MS : ZOOM_RELEASE_MS;
        zoom_ += (target - zoom_) * (1.0f - std::exp(-dt / tau));
    }

    // 背景を描く（画面を消した直後、一覧などより先に呼ぶ）
    void draw(SDL_Renderer* renderer){
        if(!bg_){   // 絵が作れなかった環境：土台の色とジャケットだけ、そのまま描く
            fillBase(renderer);
            PlayBackground::drawBlurredJacket(renderer, cur_);
            return;
        }

        // 1. bg_ に組み立てる：土台の色 → 新しい曲のぼかし → 前の見た目を (1 − t) の濃さで
        SDL_Texture* prevTarget = SDL_GetRenderTarget(renderer);
        SDL_SetRenderTarget(renderer, bg_);
        fillBase(renderer);
        PlayBackground::drawBlurredJacket(renderer, cur_);
        if(snapValid_){
            double t = std::clamp((SDL_GetTicks() - fadeStart_) / FADE_MS, 0.0, 1.0);
            t = t * t * (3.0 - 2.0 * t);   // smoothstep：始めと終わりをゆっくりに
            if(t < 1.0){
                SDL_SetTextureAlphaMod(snap_, static_cast<Uint8>(255.0 * (1.0 - t)));
                SDL_RenderCopy(renderer, snap_, nullptr, nullptr);
            }
            else{
                snapValid_ = false;   // 切り替え終わり
            }
        }
        SDL_SetRenderTarget(renderer, prevTarget);
        hasFrame_ = true;

        // 2. 画面に、低音のズームの分だけ大きくして貼る（真ん中が中心）
        const float scale = 1.0f + ZOOM_AMOUNT * zoom_;
        const SDL_FRect dst = {SCREEN_W * 0.5f * (1.0f - scale), SCREEN_H * 0.5f * (1.0f - scale),
                               SCREEN_W * scale, SCREEN_H * scale};
        SDL_RenderCopyF(renderer, bg_, nullptr, &dst);
    }

private:
    SDL_Texture* bg_   = nullptr;   // 今の背景（毎フレーム組み立てる。ズーム前）
    SDL_Texture* snap_ = nullptr;   // 曲を変えた瞬間の背景（切り替えの「前の絵」）
    SDL_Texture* cur_  = nullptr;   // 今の曲のぼかした絵（cache_ の中のもの。nullptr = 無し）
    std::string  curKey_;
    std::unordered_map<std::string, SDL_Texture*> cache_;   // 曲 → ぼかした絵（読めなかった曲は nullptr）
    Uint32 fadeStart_ = 0;
    Uint32 lastTick_  = 0;
    float  zoom_ = 0.0f;
    bool   hasFrame_  = false;   // bg_ に一度でも描いたか（最初の曲は切り替えなしで出す）
    bool   snapValid_ = false;   // 切り替え中か

    static void fillBase(SDL_Renderer* renderer){
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, BASE_COLOR.r, BASE_COLOR.g, BASE_COLOR.b, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    }
};