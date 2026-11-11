#pragma once

// =====================================================================
//  PlayBackground.h — プレイ画面の背景
//
//  ・全体：レーンの色（ほぼ黒・濃い紺）と見分けがつく、少し藍色を含んだ灰色で塗る
//  ・真ん中：その曲のジャケット絵を、大きくぼかして薄く敷く（ふちは丸く透明になっていく）
//
//  ■ ぼかし方（毎フレームの計算はしない。読み込むときに1回だけ作る）
//    1. ジャケット絵を 64×64 まで縮める（周りの色を平均しながら縮めるので、この時点でかなりぼける）
//    2. その小さな絵に「まわり 2 マスの平均」を 3 回かける（ボックスぼかし 3 回 ≒ なめらかなぼかし）
//    3. 真ん中から離れるほど透明にする（丸いふち）
//    4. 描くときは、なめらかに引き伸ばす設定（線形補間）で大きく貼る → 引き伸ばしでさらにぼける
//
//  ■ ジャケット絵の探し方（選曲画面の loadJacketFor と同じ順番）
//    譜面ファイルの "jacket" → jackets/<譜面ファイル名>.png → .jpg。見つからなければ灰色だけ
// =====================================================================

#include "GameCommon.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>

class PlayBackground{
public:
    // ----- 調整用 -----
    static constexpr SDL_Color BASE_COLOR = {60, 62, 86, 255};   // 背景の色（少し藍色を含んだ灰色）
    static constexpr int   JACKET_DRAW_SIZE = 1100;   // ジャケットを描く大きさ(px、正方形)。レーン（幅 720px）の左右から少しはみ出す
    static constexpr Uint8 JACKET_ALPHA     = 150;    // ジャケットの濃さ（0〜255）
    static constexpr float JACKET_DARKEN    = 0.75f;  // ジャケットを少し暗くする（ノーツを見やすくするため）
    static constexpr int   BLUR_SIZE        = 64;     // 縮める大きさ（小さいほど強くぼける）
    static constexpr int   BLUR_RADIUS      = 2;      // ボックスぼかしの半径（マス）
    static constexpr int   BLUR_PASSES      = 3;      // ボックスぼかしをかける回数
    static constexpr float FADE_START       = 0.55f;  // 真ん中からこの割合より外側を、だんだん透明にする（1.0 = 円のふち）

    PlayBackground() = default;
    ~PlayBackground(){ release(); }
    PlayBackground(const PlayBackground&) = delete;
    PlayBackground& operator=(const PlayBackground&) = delete;

    // 譜面のパスからジャケット絵を探して、ぼかした絵を作る。見つからなければ灰色だけになる
    void load(SDL_Renderer* renderer, const std::string& scorePath){
        release();
        SDL_Surface* src = nullptr;
        for(const std::string& path : jacketCandidates(scorePath)){
            src = IMG_Load(path.c_str());
            if(src) break;
        }
        if(!src) return;

        SDL_Surface* rgba = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(src);
        if(!rgba) return;

        std::vector<float> img = shrinkAverage(rgba, BLUR_SIZE);   // RGBA を 0〜1 の小数で
        SDL_FreeSurface(rgba);
        for(int p = 0; p < BLUR_PASSES; p++) boxBlur(img, BLUR_SIZE, BLUR_RADIUS);

        // 小さな絵を作る：色を少し暗くし、真ん中から離れるほど透明に
        SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, BLUR_SIZE, BLUR_SIZE, 32, SDL_PIXELFORMAT_RGBA32);
        if(!out) return;
        Uint8* px = static_cast<Uint8*>(out->pixels);
        const float c = (BLUR_SIZE - 1) * 0.5f;
        for(int y = 0; y < BLUR_SIZE; y++){
            for(int x = 0; x < BLUR_SIZE; x++){
                const float* s = &img[(static_cast<size_t>(y) * BLUR_SIZE + x) * 4];
                const float dx = (x - c) / c, dy = (y - c) / c;
                const float r = std::sqrt(dx * dx + dy * dy);                     // 0 = 真ん中、1 = 円のふち
                float t = std::clamp((r - FADE_START) / (1.0f - FADE_START), 0.0f, 1.0f);
                const float edge = 1.0f - t * t * (3.0f - 2.0f * t);              // なめらかに 1 → 0
                Uint8* d = px + y * out->pitch + x * 4;
                d[0] = static_cast<Uint8>(std::clamp(s[0] * JACKET_DARKEN, 0.0f, 1.0f) * 255.0f);
                d[1] = static_cast<Uint8>(std::clamp(s[1] * JACKET_DARKEN, 0.0f, 1.0f) * 255.0f);
                d[2] = static_cast<Uint8>(std::clamp(s[2] * JACKET_DARKEN, 0.0f, 1.0f) * 255.0f);
                d[3] = static_cast<Uint8>(std::clamp(s[3] * edge, 0.0f, 1.0f) * 255.0f);
            }
        }
        jacket_ = SDL_CreateTextureFromSurface(renderer, out);
        SDL_FreeSurface(out);
        if(jacket_){
            SDL_SetTextureBlendMode(jacket_, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(jacket_, SDL_ScaleModeLinear);   // 大きく引き伸ばすときに、なめらかに（＝さらにぼける）
            SDL_SetTextureAlphaMod(jacket_, JACKET_ALPHA);
        }
    }

    void release(){
        if(jacket_){
            SDL_DestroyTexture(jacket_);
            jacket_ = nullptr;
        }
    }

    // 背景を描く（renderGamePlayScreen の一番最初に呼ぶ）
    void draw(SDL_Renderer* renderer) const{
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, BASE_COLOR.r, BASE_COLOR.g, BASE_COLOR.b, 255);
        SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
        SDL_RenderFillRect(renderer, &full);

        if(jacket_){
            SDL_Rect dst = {(SCREEN_W - JACKET_DRAW_SIZE) / 2, (SCREEN_H - JACKET_DRAW_SIZE) / 2,
                            JACKET_DRAW_SIZE, JACKET_DRAW_SIZE};
            SDL_RenderCopy(renderer, jacket_, nullptr, &dst);
        }
    }

private:
    SDL_Texture* jacket_ = nullptr;

    // 選曲画面と同じ順番で、ジャケット絵の場所の候補を作る
    static std::vector<std::string> jacketCandidates(const std::string& scorePath){
        std::vector<std::string> out;
        try{
            std::ifstream file(toUtf8Path(scorePath));
            nlohmann::json j;
            if(file.is_open()){
                file >> j;
                const std::string jacket = j.value("jacket", std::string(""));
                if(!jacket.empty()) out.push_back("jackets/" + jacket);
            }
        }
        catch(const std::exception&){
            // 譜面が読めなくても、ファイル名からの候補は試す
        }
        const std::string title = std::filesystem::path(toUtf8Path(scorePath)).stem().string();
        out.push_back("jackets/" + title + ".png");
        out.push_back("jackets/" + title + ".jpg");
        return out;
    }

    // 周りの色を平均しながら size × size に縮める（大きな絵を小さくするとき、色のちらつきが出ない）
    static std::vector<float> shrinkAverage(SDL_Surface* s, int size){
        std::vector<float> out(static_cast<size_t>(size) * size * 4, 0.0f);
        const Uint8* px = static_cast<const Uint8*>(s->pixels);
        for(int y = 0; y < size; y++){
            const int y0 = y * s->h / size, y1 = std::max(y0 + 1, (y + 1) * s->h / size);
            for(int x = 0; x < size; x++){
                const int x0 = x * s->w / size, x1 = std::max(x0 + 1, (x + 1) * s->w / size);
                double acc[4] = {0, 0, 0, 0};
                int n = 0;
                for(int sy = y0; sy < y1 && sy < s->h; sy++){
                    const Uint8* row = px + sy * s->pitch;
                    for(int sx = x0; sx < x1 && sx < s->w; sx++){
                        for(int k = 0; k < 4; k++) acc[k] += row[sx * 4 + k];
                        n++;
                    }
                }
                float* d = &out[(static_cast<size_t>(y) * size + x) * 4];
                for(int k = 0; k < 4; k++) d[k] = n ? static_cast<float>(acc[k] / n / 255.0) : 0.0f;
            }
        }
        return out;
    }

    // ボックスぼかし：まわり radius マスの平均にする（横 → 縦の順。端は端の色を使う）
    static void boxBlur(std::vector<float>& img, int size, int radius){
        std::vector<float> tmp(img.size());
        auto at = [&](const std::vector<float>& v, int x, int y, int k){
            x = std::clamp(x, 0, size - 1);
            y = std::clamp(y, 0, size - 1);
            return v[(static_cast<size_t>(y) * size + x) * 4 + k];
        };
        const float div = 1.0f / (radius * 2 + 1);
        for(int y = 0; y < size; y++) for(int x = 0; x < size; x++) for(int k = 0; k < 4; k++){
            float sum = 0.0f;
            for(int d = -radius; d <= radius; d++) sum += at(img, x + d, y, k);
            tmp[(static_cast<size_t>(y) * size + x) * 4 + k] = sum * div;
        }
        for(int y = 0; y < size; y++) for(int x = 0; x < size; x++) for(int k = 0; k < 4; k++){
            float sum = 0.0f;
            for(int d = -radius; d <= radius; d++) sum += at(tmp, x, y + d, k);
            img[(static_cast<size_t>(y) * size + x) * 4 + k] = sum * div;
        }
    }
};