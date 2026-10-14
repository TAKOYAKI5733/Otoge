#pragma once

#include "GameCommon.h"
#include <unordered_map>
#include <vector>
#include <cstdio>

// =====================================================================
//  判定文字の下に出す「FAST / LATE」と「±ms」
//    ・FAST/LATE は GOOD・BAD のときだけ（PERFECT では出さない）
//    ・±ms は叩いた判定すべて（- = 早い、+ = 遅い）
//    ・文字の画像は最初に作って使い回す（毎フレーム TTF で作らない）
// =====================================================================
class TimingDisplay{
public:
    bool showFastLate = true;
    bool showMs = false;

    // プレイ開始時に1回。font は destroy() まで閉じないこと（ms の文字をあとから作るため）
    void init(SDL_Renderer* renderer, TTF_Font* font, const PlayerSettings& s){
        renderer_ = renderer;
        font_ = font;
        showFastLate = s.showFastLate;
        showMs = s.showTimingMs;
        fast_ = makeText("FAST", FAST_COLOR);
        late_ = makeText("LATE", LATE_COLOR);
    }

    // プレイ終了時に1回
    void destroy(){
        freeText(fast_);
        freeText(late_);
        for(auto& kv : msCache_) freeText(kv.second);
        msCache_.clear();
    }

    // judgeRect：判定文字（PERFECT!! など）を描いた位置。その真下に並べる
    // judgeType：1 = PERFECT、2 = GOOD、3 = BAD
    // diffMs：叩いた時刻 − 正しい時刻（- = 早い、+ = 遅い）
    void draw(const SDL_Rect& judgeRect, int judgeType, int32_t diffMs){
        if(!renderer_) return;

        std::vector<const Text*> parts;
        if(showFastLate && judgeType != 1 && diffMs != 0){
            parts.push_back(diffMs < 0 ? &fast_ : &late_);
        }
        if(showMs){
            parts.push_back(&msText(diffMs));
        }
        if(parts.empty()) return;

        // 横に並べたときの全体の幅を求めて、判定文字の中央にそろえる
        int totalW = 0;
        for(const Text* t : parts) totalW += t->w;
        totalW += GAP * static_cast<int>(parts.size() - 1);

        int x = judgeRect.x + judgeRect.w / 2 - totalW / 2;
        int y = judgeRect.y + judgeRect.h + 4;
        for(const Text* t : parts){
            if(t->tex){
                SDL_Rect dst = {x, y, t->w, t->h};
                SDL_RenderCopy(renderer_, t->tex, NULL, &dst);
            }
            x += t->w + GAP;
        }
    }

private:
    struct Text{
        SDL_Texture* tex = nullptr;
        int w = 0, h = 0;
    };

    static constexpr SDL_Color FAST_COLOR = { 80, 170, 255, 255};   // 早い：青
    static constexpr SDL_Color LATE_COLOR = {255, 140,  60, 255};   // 遅い：橙
    static constexpr SDL_Color JUST_COLOR = {255, 255, 255, 255};   // ちょうど：白
    static constexpr int GAP = 14;                                  // FAST と ms の間

    SDL_Renderer* renderer_ = nullptr;
    TTF_Font* font_ = nullptr;
    Text fast_, late_;
    std::unordered_map<int32_t, Text> msCache_;   // ズレ(ms) → 文字の画像

    Text makeText(const char* s, SDL_Color c){
        Text t;
        if(!font_) return t;
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font_, s, c);
        if(!surf) return t;
        t.tex = SDL_CreateTextureFromSurface(renderer_, surf);
        t.w = surf->w;
        t.h = surf->h;
        SDL_FreeSurface(surf);
        return t;
    }

    static void freeText(Text& t){
        if(t.tex) SDL_DestroyTexture(t.tex);
        t.tex = nullptr;
    }

    // ms の文字は、初めて出る値のときだけ作ってキャッシュする
    const Text& msText(int32_t diffMs){
        auto it = msCache_.find(diffMs);
        if(it != msCache_.end()) return it->second;

        char buf[16];
        if(diffMs == 0) std::snprintf(buf, sizeof(buf), "0ms");
        else            std::snprintf(buf, sizeof(buf), "%+dms", static_cast<int>(diffMs));
        SDL_Color c = (diffMs < 0) ? FAST_COLOR : (diffMs > 0) ? LATE_COLOR : JUST_COLOR;

        return msCache_.emplace(diffMs, makeText(buf, c)).first->second;
    }
};