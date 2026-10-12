#pragma once

#include "GameCommon.h"
#include "Displaymode.h"
#include <string>
#include <algorithm>
#include <cstdio>
#include <cmath>

// =====================================================================
//  選曲画面の上に左からスライドインする設定ウィンドウ
//  使い方：
//    init()            … 選曲シーンの開始時に1回
//    open(settings)    … F2 で開く
//    handleKey(...)    … 開いている間のキー入力を全部渡す
//    update()          … 毎フレーム1回（スライドのアニメーション）
//    draw(...)         … 毎フレーム、選曲画面を描いた「後」に呼ぶ（一番手前に出す）
//    destroy()         … 選曲シーンの終了時に1回
// =====================================================================
class SettingsPanel{
public:
    void init(){
        previewTap_ = Mix_LoadWAV("sounds/tapsound_2.wav");
    }

    void destroy(){
        if(previewTap_){
            Mix_FreeChunk(previewTap_);
            previewTap_ = nullptr;
        }
    }

    bool isOpen() const { return open_; }

    // 開く：表示モードは閉じたときに反映するので、開いた時点の値を覚えておく
    void open(const PlayerSettings& s){
        open_ = true;
        cursor_ = 0;
        openedWindowMode_ = s.windowMode;
    }

    // 閉じる：表示モードが変わっていれば反映し、ファイルに保存する
    void close(SDL_Window* window, PlayerSettings& s){
        if(!open_) return;
        open_ = false;
        if(s.windowMode != openedWindowMode_){
            applyWindowMode(window, s.windowMode);
        }
        savePlayerSettings(s);
    }

    // 開いている間のキー入力を処理する
    void handleKey(const SDL_KeyboardEvent& key, SDL_Window* window, SDL_Renderer* renderer, PlayerSettings& s){
        SDL_Keycode sym = key.keysym.sym;
        bool isArrow = (sym == SDLK_UP || sym == SDLK_DOWN || sym == SDLK_LEFT || sym == SDLK_RIGHT);
        if(key.repeat && !isArrow) return;   // 押しっぱなしの連続入力は矢印だけ受け付ける

        bool big = (key.keysym.mod & KMOD_SHIFT) != 0;

        switch(sym){
            case SDLK_ESCAPE:
            case SDLK_F2:
                close(window, s);
                break;
            case SDLK_UP:
                cursor_ = (cursor_ + ROW_COUNT - 1) % ROW_COUNT;
                break;
            case SDLK_DOWN:
                cursor_ = (cursor_ + 1) % ROW_COUNT;
                break;
            case SDLK_LEFT:
                changeValue(-1, big, renderer, s);
                break;
            case SDLK_RIGHT:
                changeValue(+1, big, renderer, s);
                break;
            case SDLK_RETURN:
                if(cursor_ == ROW_VSYNC) changeValue(+1, false, renderer, s);   // ON/OFF は ENTER でも切替
                break;
            default:
                break;
        }
    }

    // 毎フレーム：開閉に向かってアニメーション値を近づける
    void update(){
        double target = open_ ? 1.0 : 0.0;
        anim_ += (target - anim_) * 0.18;
    }

    void draw(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* smallFont, const PlayerSettings& s) const{
        if(anim_ < 0.001) return;

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        Uint8 a = static_cast<Uint8>(255 * anim_);

        // 1. 後ろの選曲画面を暗くする
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<Uint8>(150 * anim_));
        SDL_Rect full = {0, 0, SW, SH};
        SDL_RenderFillRect(renderer, &full);

        // 2. ウィンドウ本体：画面左の外から PANEL_X までスライド
        int x = static_cast<int>(-PANEL_W - 20 + (PANEL_X + PANEL_W + 20) * anim_);
        SDL_Rect panel = {x, PANEL_Y, PANEL_W, PANEL_H};

        drawTab(renderer, smallFont, "SETTINGS", panel, a);
        drawFrame(renderer, panel, SDL_Color{10, 10, 20, static_cast<Uint8>(225 * anim_)},
                  SDL_Color{0, 200, 255, a}, 3);

        // 3. 各項目
        for(int i = 0; i < ROW_COUNT; i++){
            bool selected = (i == cursor_);
            SDL_Rect row = {x + 30, PANEL_Y + 40 + i * ROW_H, PANEL_W - 60, ROW_H - 16};

            drawFrame(renderer, row,
                      selected ? SDL_Color{45, 45, 80, static_cast<Uint8>(210 * anim_)}
                               : SDL_Color{30, 30, 50, static_cast<Uint8>(170 * anim_)},
                      selected ? SDL_Color{0, 255, 255, a}
                               : SDL_Color{110, 110, 150, a},
                      selected ? 3 : 1);

            int cy = row.y + row.h / 2;
            SDL_Color labelColor = selected ? SDL_Color{0, 255, 255, 255} : SDL_Color{255, 255, 255, 255};
            drawText(renderer, font, ROW_LABELS[i], labelColor, row.x + 30, cy, a, Align::Left);

            // 値：選択中だけ「< 値 >」の形にして、左右で変えられることを示す
            std::string value = valueText(i, s);
            if(selected) value = "<  " + value + "  >";
            drawText(renderer, font, value, SDL_Color{255, 255, 255, 255}, row.x + row.w - 30, cy, a, Align::Right);
        }

        // 4. 選択中の項目の説明と、操作方法
        int descY = PANEL_Y + 40 + ROW_COUNT * ROW_H + 20;
        drawText(renderer, smallFont, ROW_DESCRIPTIONS[cursor_], SDL_Color{180, 180, 200, 255},
                 x + 40, descY, a, Align::Left);
        drawText(renderer, smallFont, "上下:選択   左右:変更   Shift+左右:大きく変更   F2/ESC:閉じて保存",
                 SDL_Color{130, 130, 150, 255}, x + 40, PANEL_Y + PANEL_H - 40, a, Align::Left);
    }

private:
    // ----- 画面サイズとレイアウト -----
    static constexpr int SW = 1920;
    static constexpr int SH = 1080;
    static constexpr int PANEL_X = 60;
    static constexpr int PANEL_Y = 140;
    static constexpr int PANEL_W = 760;
    static constexpr int PANEL_H = 780;
    static constexpr int ROW_H   = 88;

    // ----- 項目 -----
    enum Row{
        ROW_OFFSET,
        ROW_BGM,
        ROW_SE,
        ROW_VSYNC,
        ROW_FPS,
        ROW_WINDOW,
        ROW_COUNT
    };

    static constexpr const char* ROW_LABELS[ROW_COUNT] = {
        "オフセット",
        "BGM 音量",
        "SE 音量",
        "VSync",
        "FPS 上限",
        "表示モード"
    };

    static constexpr const char* ROW_DESCRIPTIONS[ROW_COUNT] = {
        "+ でノーツが遅れて、- で早く来ます（Shift で 10ms 単位）",
        "流れている BGM にすぐ反映されます",
        "変えるたびに打鍵音を鳴らします",
        "ON = 滑らか ／ OFF + FPS上限 = 低遅延",
        "VSync OFF のときに使われる上限です",
        "このウィンドウを閉じたときに反映されます"
    };

    static constexpr int FPS_OPTIONS[] = {0, 60, 120, 144, 165, 240, 360};   // 0 = 無制限
    static constexpr int FPS_COUNT = static_cast<int>(sizeof(FPS_OPTIONS) / sizeof(FPS_OPTIONS[0]));

    static constexpr const char* WINDOW_MODE_NAMES[3] = {"ウィンドウ", "ボーダーレス", "フルスクリーン"};

    // ----- 状態 -----
    bool open_ = false;
    int cursor_ = 0;
    int openedWindowMode_ = 0;
    double anim_ = 0.0;              // 0.0 = 隠れている、1.0 = 出きっている
    Mix_Chunk* previewTap_ = nullptr;

    static int volumeToMix(int percent){
        return static_cast<int>(percent * MIX_MAX_VOLUME / 100.0);
    }

    // 今の FPS 上限に一番近い選択肢の番号（設定ファイルに選択肢外の値が入っていても動くように）
    static int nearestFpsIndex(int fps){
        if(fps <= 0) return 0;
        int best = 1;
        for(int i = 1; i < FPS_COUNT; i++){
            if(std::abs(FPS_OPTIONS[i] - fps) < std::abs(FPS_OPTIONS[best] - fps)) best = i;
        }
        return best;
    }

    // 値を変更して、その場で反映できるものは反映する
    void changeValue(int dir, bool big, SDL_Renderer* renderer, PlayerSettings& s){
        switch(cursor_){
            case ROW_OFFSET:
                s.offsetMs = std::clamp(s.offsetMs + dir * (big ? 10.0 : 1.0), -500.0, 500.0);
                break;

            case ROW_BGM:
                s.bgmVolume = std::clamp(s.bgmVolume + dir * (big ? 10 : 1), 0, 100);
                Mix_VolumeMusic(volumeToMix(s.bgmVolume));
                break;

            case ROW_SE:
                s.seVolume = std::clamp(s.seVolume + dir * (big ? 10 : 1), 0, 100);
                if(previewTap_){
                    Mix_VolumeChunk(previewTap_, volumeToMix(s.seVolume));
                    Mix_PlayChannel(-1, previewTap_, 0);
                }
                break;

            case ROW_VSYNC:
                s.vsync = !s.vsync;
                SDL_RenderSetVSync(renderer, s.vsync ? 1 : 0);
                break;

            case ROW_FPS:{
                int idx = std::clamp(nearestFpsIndex(s.fpsCap) + dir, 0, FPS_COUNT - 1);
                s.fpsCap = FPS_OPTIONS[idx];
                g_framePacer.setCap(s.fpsCap);
                break;
            }

            case ROW_WINDOW:
                s.windowMode = std::clamp(s.windowMode + dir, 0, 2);   // 反映は close() で
                break;

            default:
                break;
        }
    }

    // 各項目の値を表示用の文字列にする
    static std::string valueText(int row, const PlayerSettings& s){
        char buf[64];
        switch(row){
            case ROW_OFFSET: std::snprintf(buf, sizeof(buf), "%+.0f ms", s.offsetMs); return buf;
            case ROW_BGM:    std::snprintf(buf, sizeof(buf), "%d %%", s.bgmVolume);   return buf;
            case ROW_SE:     std::snprintf(buf, sizeof(buf), "%d %%", s.seVolume);    return buf;
            case ROW_VSYNC:  return s.vsync ? "ON" : "OFF";
            case ROW_FPS:
                if(s.fpsCap <= 0) return "無制限";
                std::snprintf(buf, sizeof(buf), "%d fps", s.fpsCap);
                return buf;
            case ROW_WINDOW: return WINDOW_MODE_NAMES[std::clamp(s.windowMode, 0, 2)];
            default:         return "";
        }
    }

    // ----- 描画の小道具 -----
    enum class Align{ Left, Right };

    // 半透明の塗り＋枠線（選曲画面の枠と同じ見た目）
    static void drawFrame(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color fill, SDL_Color border, int thickness){
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        for(int t = 0; t < thickness; t++){
            SDL_Rect r = {rect.x + t, rect.y + t, rect.w - t * 2, rect.h - t * 2};
            SDL_RenderDrawRect(renderer, &r);
        }
    }

    // ウィンドウ上辺に乗せるタイトルタブ
    static void drawTab(SDL_Renderer* renderer, TTF_Font* font, const char* title, const SDL_Rect& panel, Uint8 alpha){
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, title, SDL_Color{15, 15, 25, 255});
        if(!surf) return;
        SDL_Rect tab = {panel.x, panel.y - surf->h - 6, surf->w + 28, surf->h + 6};
        SDL_SetRenderDrawColor(renderer, 0, 200, 255, alpha);
        SDL_RenderFillRect(renderer, &tab);

        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(tex, alpha);
        SDL_Rect dst = {tab.x + 14, tab.y + 3, surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }

    // 文字を描く。y は文字の縦の中心、x は Left なら左端・Right なら右端
    static void drawText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text,
                         SDL_Color color, int x, int y, Uint8 alpha, Align align){
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
        if(!surf) return;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(tex, alpha);

        int left = (align == Align::Right) ? x - surf->w : x;
        SDL_Rect dst = {left, y - surf->h / 2, surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }
};