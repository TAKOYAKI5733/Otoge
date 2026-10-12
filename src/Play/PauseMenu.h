#pragma once

#include "GameCommon.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

// ポーズメニューで「プレイ画面を終える」選択がされたときの結果
enum class PauseAction{
    None,     // 何もしない（メニュー操作中・再開のカウントダウン中など）
    Retry,    // 最初からやり直す
    Retire    // 選曲画面に戻る
};

// =====================================================================
//  プレイ中のポーズメニュー
//    状態は3つ：None（プレイ中） → Menu（メニュー表示） → Countdown（3,2,1） → None
//    時間はすべて「実時間（ポーズ中も進む時計）」で受け取る
// =====================================================================
class PauseMenu{
public:
    // チュートリアルではリトライを出さない（allowRetry = false）
    explicit PauseMenu(bool allowRetry){
        items_.push_back({"再開", ItemKind::Resume});
        if(allowRetry) items_.push_back({"リトライ", ItemKind::Retry});
        items_.push_back({"リタイア", ItemKind::Retire});
    }

    // ポーズ中（メニュー表示中 or カウントダウン中）か
    bool isActive() const { return state_ != State::None; }

    // メニューを開く
    void open(double realMs){
        state_ = State::Menu;
        cursor_ = 0;
        stateStartMs_ = realMs;
    }

    // ポーズ中のキー入力
    PauseAction handleKey(const SDL_KeyboardEvent& key, double realMs){
        SDL_Keycode sym = key.keysym.sym;
        if(key.repeat && sym != SDLK_UP && sym != SDLK_DOWN) return PauseAction::None;

        // カウントダウン中：ESC でメニューに戻れる
        if(state_ == State::Countdown){
            if(sym == SDLK_ESCAPE){
                state_ = State::Menu;
                stateStartMs_ = realMs;
            }
            return PauseAction::None;
        }
        if(state_ != State::Menu) return PauseAction::None;

        const int n = static_cast<int>(items_.size());
        switch(sym){
            case SDLK_UP:
                cursor_ = (cursor_ + n - 1) % n;
                break;
            case SDLK_DOWN:
                cursor_ = (cursor_ + 1) % n;
                break;
            case SDLK_ESCAPE:              // もう一度 ESC でも再開
                startCountdown(realMs);
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                switch(items_[cursor_].kind){
                    case ItemKind::Resume: startCountdown(realMs);  break;
                    case ItemKind::Retry:  return PauseAction::Retry;
                    case ItemKind::Retire: return PauseAction::Retire;
                }
                break;
            default:
                break;
        }
        return PauseAction::None;
    }

    // 毎フレーム呼ぶ。カウントダウンが終わったフレームだけ true を返す（ここでプレイを再開する）
    bool updateCountdown(double realMs){
        if(state_ == State::Countdown && realMs - stateStartMs_ >= COUNTDOWN_MS){
            state_ = State::None;
            return true;
        }
        return false;
    }

    // プレイ画面を描いた「後」に呼ぶ
    void draw(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* bigFont, TTF_Font* smallFont, double realMs) const{
        if(!isActive()) return;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        if(state_ == State::Menu) drawMenu(renderer, font, smallFont, realMs);
        else                      drawCountdown(renderer, bigFont, smallFont, realMs);
    }

private:
    enum class State{ None, Menu, Countdown };
    enum class ItemKind{ Resume, Retry, Retire };
    struct Item{
        const char* label;
        ItemKind kind;
    };

    static constexpr int    SW = 1920;
    static constexpr int    SH = 1080;
    static constexpr double COUNTDOWN_MS = 3000.0;   // 再開までのカウントダウン（3,2,1）
    static constexpr double OPEN_ANIM_MS = 150.0;    // メニューが出てくるアニメーションの長さ
    static constexpr int    WIN_W  = 560;
    static constexpr int    ITEM_H = 90;

    std::vector<Item> items_;
    State state_ = State::None;
    int cursor_ = 0;
    double stateStartMs_ = 0.0;   // 今の状態に入った実時間

    void startCountdown(double realMs){
        state_ = State::Countdown;
        stateStartMs_ = realMs;
    }

    void drawMenu(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* smallFont, double realMs) const{
        double t = std::clamp((realMs - stateStartMs_) / OPEN_ANIM_MS, 0.0, 1.0);
        Uint8 a = static_cast<Uint8>(255 * t);

        // 1. プレイ画面を暗くする
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<Uint8>(160 * t));
        SDL_Rect full = {0, 0, SW, SH};
        SDL_RenderFillRect(renderer, &full);

        // 2. ウィンドウ：少し下から浮き上がりながらフェードイン
        const int n = static_cast<int>(items_.size());
        int winH = 60 + n * ITEM_H + 80;
        int x = (SW - WIN_W) / 2;
        int y = (SH - winH) / 2 + static_cast<int>((1.0 - t) * 30);
        SDL_Rect win = {x, y, WIN_W, winH};

        drawTab(renderer, smallFont, "PAUSE", win, a);
        drawFrame(renderer, win, SDL_Color{10, 10, 20, static_cast<Uint8>(230 * t)}, SDL_Color{0, 200, 255, a}, 3);

        // 3. 項目
        for(int i = 0; i < n; i++){
            bool selected = (i == cursor_);
            SDL_Rect row = {x + 40, y + 40 + i * ITEM_H, WIN_W - 80, ITEM_H - 18};

            drawFrame(renderer, row,
                      selected ? SDL_Color{45, 45, 80, static_cast<Uint8>(210 * t)}
                               : SDL_Color{30, 30, 50, static_cast<Uint8>(170 * t)},
                      selected ? SDL_Color{0, 255, 255, a}
                               : SDL_Color{110, 110, 150, a},
                      selected ? 3 : 1);

            SDL_Color c = selected ? SDL_Color{0, 255, 255, 255} : SDL_Color{255, 255, 255, 255};
            drawTextCentered(renderer, font, items_[i].label, c, row.x + row.w / 2, row.y + row.h / 2, a, 1.0);
        }

        // 4. 操作方法
        drawTextCentered(renderer, smallFont, "上下:選択   ENTER:決定   ESC:再開",
                         SDL_Color{150, 150, 170, 255}, x + WIN_W / 2, y + winH - 35, a, 1.0);
    }

    void drawCountdown(SDL_Renderer* renderer, TTF_Font* bigFont, TTF_Font* smallFont, double realMs) const{
        double elapsed = realMs - stateStartMs_;

        // ノーツの位置が見えるよう、メニューより薄く暗くする
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 90);
        SDL_Rect full = {0, 0, SW, SH};
        SDL_RenderFillRect(renderer, &full);

        // 3 → 2 → 1。各数字は出た瞬間に大きく、すぐ元の大きさに縮みながら少し薄くなる
        int number = std::clamp(3 - static_cast<int>(elapsed / 1000.0), 1, 3);
        double frac = std::fmod(elapsed, 1000.0) / 1000.0;
        double scale = 1.6 - 0.6 * std::min(1.0, frac * 4.0);
        Uint8 alpha = static_cast<Uint8>(255 * (1.0 - frac * 0.5));

        drawTextCentered(renderer, bigFont, std::to_string(number), SDL_Color{255, 255, 255, 255},
                         SW / 2, SH / 2 - 80, alpha, scale);
        drawTextCentered(renderer, smallFont, "ESC でメニューに戻る", SDL_Color{200, 200, 220, 255},
                         SW / 2, SH / 2 + 40, 200, 1.0);
    }

    // ----- 描画の小道具（選曲画面のウィンドウと同じ見た目） -----
    static void drawFrame(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color fill, SDL_Color border, int thickness){
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        for(int t = 0; t < thickness; t++){
            SDL_Rect r = {rect.x + t, rect.y + t, rect.w - t * 2, rect.h - t * 2};
            SDL_RenderDrawRect(renderer, &r);
        }
    }

    static void drawTab(SDL_Renderer* renderer, TTF_Font* font, const char* title, const SDL_Rect& win, Uint8 alpha){
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, title, SDL_Color{15, 15, 25, 255});
        if(!surf) return;
        SDL_Rect tab = {win.x, win.y - surf->h - 6, surf->w + 28, surf->h + 6};
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

    // (cx, cy) を中心に、scale 倍で文字を描く
    static void drawTextCentered(SDL_Renderer* renderer, TTF_Font* font, const std::string& text,
                                 SDL_Color color, int cx, int cy, Uint8 alpha, double scale){
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
        if(!surf) return;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(tex, alpha);

        int w = static_cast<int>(surf->w * scale);
        int h = static_cast<int>(surf->h * scale);
        SDL_Rect dst = {cx - w / 2, cy - h / 2, w, h};
        SDL_RenderCopy(renderer, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }
};