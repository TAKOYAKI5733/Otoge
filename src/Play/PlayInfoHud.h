#pragma once

// =====================================================================
//  PlayInfoHud.h — プレイ画面の左下の難易度・右下の曲名
//
//  ■ 見た目：ホログラム（半透明・厚み・走査線・ときどきグリッチ）
//    ・左下：難易度「MASTER:16」。色は難易度ごとの色（選曲画面の難易度の色と同じ決め方）
//    ・右下：曲名。色はレーンの枠（判定枠・区切り線）と同じホログラムの色（HOLO_FRONT_COLOR）
//    ・厚み：同じ文字を少しずつ上へずらして、暗く薄い層を足し算で重ねる
//    ・走査線：手前の文字を 3px の横の帯に切って、1 本おきに薄くする
//    ・グリッチ：レーンのキーの文字・判定枠と同じタイミング（holoNow()）で、帯を左右にずらし、赤と青の色ずれを重ねる
//  ■ 出てくるとき（appear：0 → 1）：帯がばらばらに現れ、はじめは大きくずれて、だんだんそろう
//
//  ■ 見切れないための位置の決め方（init で 1 回だけ計算）
//    ・使える幅：左下は「画面の左の余白」〜「レーンの左端のビジュアライザーの手前」、右下は「レーンの右側」〜「画面の右の余白」
//    ・余白には、グリッチでずれる量・色ずれ・厚みの分を足しておく（ずれても画面の外に出ない）
//    ・文字がその幅に入らないときは、文字を小さくする（最小 MIN_SCALE 倍）
//    ・それでも入らない長い曲名は、後ろを「…」にして入る長さまで詰める（UTF-8 の 1 文字ずつ測る）
//    ・この表示は低音のズームより後に描くので、ズームしても画面の外に押し出されない
// =====================================================================

#include "GameCommon.h"
#include "Play/drawFrame.h"
#include "Play/loadScore.h"
#include <filesystem>
#include <string>
#include <cctype>

class PlayInfoHud{
public:
    // ----- 調整用 -----
    static constexpr int   FONT_SIZE_DIFF  = 44;
    static constexpr int   FONT_SIZE_TITLE = 40;
    static constexpr int   MARGIN_X        = 40;      // 画面の左右の端からの余白
    static constexpr int   MARGIN_BOTTOM   = 34;      // 画面の下の端からの余白
    static constexpr int   GLITCH_ROOM     = 30;      // グリッチでずれる分として、さらに空けておく幅
    static constexpr int   LANE_GAP        = 150;     // レーンの端（＋レーンの端のビジュアライザー）からの距離
    static constexpr float MIN_SCALE       = 0.55f;   // これより小さくはしない（読めなくなるので）
    static constexpr Uint8 FRONT_ALPHA     = 190;     // 手前の文字の濃さ（半透明）
    static constexpr Uint8 BACK_ALPHA      = 55;      // 厚みの層の濃さ
    static constexpr float DEPTH_PX        = 3.0f;    // 厚みの層 1 枚ごとのずらし量
    static constexpr int   SCANLINE_PX     = 3;       // 走査線の太さ
    static constexpr float APPEAR_SHIFT_PX = 24.0f;   // 出てくるときの帯の最大のずれ
    static constexpr float GLITCH_SHIFT_PX = 10.0f;   // いつものグリッチの帯の最大のずれ

    // laneLeft / laneRight：レーンの左端・右端（画面の x）
    bool init(SDL_Renderer* r, const std::string& scorePath, int difficultyIndex, int laneLeft, int laneRight){
        destroy();
        // 難易度：譜面の "difficulties" の中で、選んだ番号（fileIndex）のもの
        std::string name = "DEFAULT", level = "0";
        for(const DifficultyInfo& d : listDifficulties(scorePath)){
            if(d.fileIndex == difficultyIndex){ name = d.name; level = d.level; break; }
        }
        diffColor_ = difficultyColorOf(name);
        // 曲名：選曲画面と同じく、譜面ファイルの名前
        const std::string title = std::filesystem::path(toUtf8Path(scorePath)).stem().string();

        const int leftMin  = MARGIN_X + GLITCH_ROOM;
        const int rightMax = SCREEN_W - MARGIN_X - GLITCH_ROOM;
        diff_  = makeFitted(r, FONT_SIZE_DIFF,  name + ":" + level, laneLeft - LANE_GAP - leftMin);
        title_ = makeFitted(r, FONT_SIZE_TITLE, title,             rightMax - (laneRight + LANE_GAP));
        diffX_  = static_cast<float>(leftMin);                        // 左下：左をそろえる
        titleX_ = static_cast<float>(rightMax) - title_.w * title_.scale;   // 右下：右をそろえる
        return diff_.tex != nullptr || title_.tex != nullptr;
    }

    void destroy(){
        if(diff_.tex){ SDL_DestroyTexture(diff_.tex); diff_.tex = nullptr; }
        if(title_.tex){ SDL_DestroyTexture(title_.tex); title_.tex = nullptr; }
    }
    ~PlayInfoHud(){ destroy(); }

    // appear：出てくる進み具合（0 = 見えない、1 = 出きった）
    void draw(SDL_Renderer* r, double appear) const{
        if(appear <= 0.0) return;
        const float yDiff  = SCREEN_H - MARGIN_BOTTOM - diff_.h * diff_.scale;
        const float yTitle = SCREEN_H - MARGIN_BOTTOM - title_.h * title_.scale;
        drawHolo(r, diff_,  diffX_,  yDiff,  diffColor_,       appear, 11u);
        drawHolo(r, title_, titleX_, yTitle, HOLO_FRONT_COLOR, appear, 23u);
    }

private:
    struct Text{
        SDL_Texture* tex = nullptr;
        int w = 0, h = 0;
        float scale = 1.0f;
    };
    Text diff_, title_;
    float diffX_ = 0, titleX_ = 0;
    SDL_Color diffColor_ = {255, 255, 255, 255};

    // 選曲画面の難易度の色と同じ決め方（名前に含まれるキーワードで決める。どれも無ければ白）
    static SDL_Color difficultyColorOf(const std::string& name){
        std::string up = name;
        for(char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        struct Rule{ const char* key; SDL_Color c; };
        static const Rule rules[] = {
            {"MASTER", {200, 110, 255, 255}},
            {"HARD",   {255, 170,  60, 255}},
            {"NORMAL", { 80, 170, 255, 255}},
            {"EASY",   { 80, 220, 120, 255}},
        };
        for(const Rule& rule : rules) if(up.find(rule.key) != std::string::npos) return rule.c;
        return SDL_Color{255, 255, 255, 255};
    }

    // UTF-8 の 1 文字の長さ（バイト数）
    static size_t utf8Len(unsigned char c){
        if(c < 0x80) return 1;
        if((c >> 5) == 0x6) return 2;
        if((c >> 4) == 0xE) return 3;
        if((c >> 3) == 0x1E) return 4;
        return 1;
    }

    // maxW に入るように、大きさ（と、必要なら「…」で詰めた文字）を決めて、白い文字の絵を作る
    static Text makeFitted(SDL_Renderer* r, int fontSize, std::string text, int maxW){
        Text t;
        if(maxW <= 0 || text.empty()) return t;
        TTF_Font* font = TTF_OpenFont("fonts/prac.ttf", fontSize);
        if(!font) return t;

        int w = 0, h = 0;
        TTF_SizeUTF8(font, text.c_str(), &w, &h);
        float scale = std::min(1.0f, static_cast<float>(maxW) / std::max(1, w));
        if(scale < MIN_SCALE){
            // 一番小さくしても入らない：後ろから 1 文字ずつ削って「…」を付け、入る長さにする
            scale = MIN_SCALE;
            const int limit = static_cast<int>(maxW / MIN_SCALE);
            std::vector<size_t> ends;   // 文字の区切りの位置
            for(size_t i = 0; i < text.size(); i += utf8Len(static_cast<unsigned char>(text[i]))) ends.push_back(i);
            std::string cut = text;
            for(size_t k = ends.size(); k-- > 1;){
                cut = text.substr(0, ends[k]) + "…";
                TTF_SizeUTF8(font, cut.c_str(), &w, &h);
                if(w <= limit) break;
            }
            text = cut;
        }
        if(SDL_Surface* s = TTF_RenderUTF8_Blended(font, text.c_str(), SDL_Color{255, 255, 255, 255})){
            t.tex = SDL_CreateTextureFromSurface(r, s);
            t.w = s->w;
            t.h = s->h;
            SDL_FreeSurface(s);
        }
        t.scale = scale;
        TTF_CloseFont(font);
        return t;
    }

    static uint32_t hashU(uint32_t x){
        x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
        return x;
    }
    static double hash01(uint32_t x){ return (hashU(x) & 0xFFFFFF) / static_cast<double>(0x1000000); }

    // 文字の絵をホログラムとして描く（key：ずれ方を文字ごとに変えるための数）
    static void drawHolo(SDL_Renderer* r, const Text& t, float x, float y, SDL_Color color, double appear, uint32_t key){
        if(!t.tex) return;
        const HoloFx fx = holoNow();
        const float W = t.w * t.scale, H = t.h * t.scale;
        const float strength = static_cast<float>((1.0 - std::min(1.0, appear)) * (1.0 - std::min(1.0, appear)));   // 出てくるときのずれの強さ
        const uint32_t seed = SDL_GetTicks() / 30u;
        auto mul = [](SDL_Color c, float k, float a){
            return SDL_Color{static_cast<Uint8>(std::min(255.0f, c.r * k)), static_cast<Uint8>(std::min(255.0f, c.g * k)),
                             static_cast<Uint8>(std::min(255.0f, c.b * k)), static_cast<Uint8>(std::clamp(a, 0.0f, 255.0f))};
        };
        const float vis = static_cast<float>(std::min(1.0, appear * 1.3));   // 出始めは全体も薄く

        // 1. 厚み：上へずらした層を、暗く薄く足し算で
        SDL_SetTextureBlendMode(t.tex, SDL_BLENDMODE_ADD);
        for(int k = 2; k >= 1; k--){
            holoTexQuad(r, t.tex, x, y - DEPTH_PX * k, W, H, 0.0f, 1.0f, mul(color, 0.55f, BACK_ALPHA * fx.flicker * vis));
        }
        // 2. 色ずれ：いつものグリッチ中・出てくる途中
        if(fx.glitch || strength > 0.02f){
            const float d = 3.0f + 8.0f * strength;
            const Uint8 ga = static_cast<Uint8>(110 * std::max(fx.glitch ? 1.0f : 0.0f, strength) * vis);
            holoTexQuad(r, t.tex, x - d, y, W, H, 0.0f, 1.0f, SDL_Color{255, 40, 60, ga});
            holoTexQuad(r, t.tex, x + d, y, W, H, 0.0f, 1.0f, SDL_Color{40, 200, 255, ga});
        }
        // 3. 手前：走査線の帯ごとに描く（出てくる途中は、まだ現れていない帯を飛ばして、大きくずらす）
        SDL_SetTextureBlendMode(t.tex, SDL_BLENDMODE_BLEND);
        const SDL_Color front = mul(color, 1.0f, 255.0f);
        const SDL_Color lit = {static_cast<Uint8>(front.r + (255 - front.r) * 0.35f),    // 少し白に寄せて読みやすく
                               static_cast<Uint8>(front.g + (255 - front.g) * 0.35f),
                               static_cast<Uint8>(front.b + (255 - front.b) * 0.35f), 255};
        const int rows = std::max(1, static_cast<int>(H) / SCANLINE_PX);
        for(int i = 0; i < rows; i++){
            const uint32_t k = seed * 977u + key * 131u + static_cast<uint32_t>(i / 3) * 17u;   // 3 本ずつの帯で動かす
            if(appear < 1.0 && hash01(k) >= appear * 1.15) continue;
            float shift = holoGlitchShift(fx, key * 10u + static_cast<uint32_t>(i * 6 / rows)) * GLITCH_SHIFT_PX;
            if(strength > 0.0f && hash01(k * 3u + 5u) < 0.6) shift += static_cast<float>(hash01(k * 7u + 11u) * 2.0 - 1.0) * APPEAR_SHIFT_PX * strength;
            const float v0 = static_cast<float>(i) / rows, v1 = static_cast<float>(i + 1) / rows;
            const float a = ((i % 2 == 0) ? 1.0f : 0.55f) * FRONT_ALPHA * fx.flicker * vis;
            holoTexQuad(r, t.tex, x + shift, y + H * v0, W, H * (v1 - v0), v0, v1, SDL_Color{lit.r, lit.g, lit.b, static_cast<Uint8>(a)});
        }
    }
};