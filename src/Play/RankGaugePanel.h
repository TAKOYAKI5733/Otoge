#pragma once
// =====================================================================
//  プレイ画面の右上に出す「RANK」ウィンドウ（RankGaugePanel.h）
//    ・今のスコアでの評価（SSS / SS / S / AAA / AA / A / B / C / F）を大きく出す
//      → 評価が上がった瞬間に、文字が一瞬大きくなって元に戻る（ポップ）
//    ・スコアに合わせて伸びるバー。評価が切り替わるスコアの位置に縦線と評価名を引く
//      → 線を越えるとその評価になる、と見て分かる
//
//  スコアと評価の関係はリザルト画面（ResultScene.cpp の calcRank）と同じにしてある
//  共通部品（ウィンドウの枠・文字のテクスチャ・出現アニメーション）は JudgeStatsPanel.h
// =====================================================================
#include "Play/JudgeStatsPanel.h"

namespace rankgauge {

// ----- 位置と大きさ（JUDGE ウィンドウの上） -----
constexpr SDL_Rect RECT = {1350, 210, 540, 190};

// ----- バー -----
//   スコアは 0〜1,000,000 だが、評価の線は 600,000 より上に集まっているので、
//   バーは BAR_MIN_SCORE から始める（ここより低いスコアのときは空のまま）
constexpr int BAR_MIN_SCORE = 500000;
constexpr int BAR_MAX_SCORE = 1000000;
constexpr int BAR_PAD_X     = 24;     // ウィンドウの左右の余白
constexpr int BAR_Y         = 122;    // バーの上端（ウィンドウの上端から）
constexpr int BAR_H         = 16;     // バーの太さ
constexpr int TICK_OVER     = 5;      // 縦線をバーの上下にはみ出させる長さ
constexpr int LABEL_ROW1_Y  = 154;    // 評価名（1段目）の縦の中心
constexpr int LABEL_ROW2_Y  = 174;    // 評価名（2段目）の縦の中心。線が近いところで名前が重ならないよう交互に置く
constexpr double BAR_EASE   = 0.18;   // バーが目標まで近づく速さ（1フレームあたりの割合）

// ----- 評価の文字 -----
constexpr int    RANK_Y        = 60;     // 評価の文字の縦の中心（ウィンドウの上端から）
constexpr double RANK_POP_MS   = 220.0;  // 評価が上がったときのポップの長さ
constexpr double RANK_POP_SIZE = 0.25;   // ポップした瞬間の大きさ（元の何倍大きいか）

// 評価の切り替わるスコア（低い順）。リザルト画面の calcRank と同じ値
struct RankStep{ const char* name; int minScore; SDL_Color color; };
constexpr RankStep STEPS[] = {
    {"F",   0,      {211, 211, 211, 200}},
    {"C",   600000, {200, 200, 200, 255}},
    {"B",   700000, {255, 165,   0, 255}},
    {"A",   800000, {  0, 255, 120, 255}},
    {"AA",  850000, {  0, 255, 120, 255}},
    {"AAA", 880000, {  0, 255, 120, 255}},
    {"S",   900000, {  0, 255, 255, 255}},
    {"SS",  950000, {255, 255, 100, 255}},
    {"SSS", 990000, {255, 215,   0, 255}},
};
constexpr int STEP_COUNT = static_cast<int>(sizeof(STEPS) / sizeof(STEPS[0]));

// スコア → 評価の番号（STEPS の添字）
inline int rankIndexOf(int score){
    int idx = 0;
    for(int i = 0; i < STEP_COUNT; i++){
        if(score >= STEPS[i].minScore) idx = i;
    }
    return idx;
}

// スコア → バーの割合（0〜1）
inline double barFraction(double score){
    return std::clamp((score - BAR_MIN_SCORE) / static_cast<double>(BAR_MAX_SCORE - BAR_MIN_SCORE), 0.0, 1.0);
}

} // namespace rankgauge

class RankGaugePanel{
public:
    static constexpr int PANEL_INDEX = 0;   // 出てくる順番（いちばん最初）

    bool init(SDL_Renderer* renderer){
        using namespace rankgauge;
        renderer_ = renderer;
        TTF_Font* rankFont  = TTF_OpenFont("fonts/prac.ttf", 80);
        TTF_Font* labelFont = TTF_OpenFont("fonts/prac.ttf", 16);
        TTF_Font* tabFont   = TTF_OpenFont("fonts/prac.ttf", 20);
        const bool ok = rankFont && labelFont && tabFont;

        // 評価の文字と、線の下の評価名は、全部ここで1回だけ作る（プレイ中は貼るだけ）
        for(int i = 0; i < STEP_COUNT; i++){
            rankText_[i]  = judgestats::makeHudText(renderer_, rankFont,  STEPS[i].name, STEPS[i].color);
            labelText_[i] = judgestats::makeHudText(renderer_, labelFont, STEPS[i].name, STEPS[i].color);
        }
        tab_ = judgestats::makeHudText(renderer_, tabFont, "RANK", SDL_Color{15, 15, 25, 255});

        if(rankFont)  TTF_CloseFont(rankFont);
        if(labelFont) TTF_CloseFont(labelFont);
        if(tabFont)   TTF_CloseFont(tabFont);
        ready_ = ok;
        return ok;
    }

    void destroy(){
        for(auto& t : rankText_)  judgestats::freeHudText(t);
        for(auto& t : labelText_) judgestats::freeHudText(t);
        judgestats::freeHudText(tab_);
        ready_ = false;
    }

    // 毎フレーム呼ぶ。displayScore = 画面に出しているスコア（数字のカウントアップと合わせるため）
    void update(int displayScore, double sceneMs){
        using namespace rankgauge;
        const int score = std::max(0, displayScore);

        // 評価が上がったら、その時刻を覚えてポップさせる（下がったときはポップしない）
        const int idx = rankIndexOf(score);
        if(idx > rankIndex_) popStartMs_ = sceneMs;
        rankIndex_ = idx;

        // バーは目標の長さへ少しずつ近づける（スコアが一気に増えてもなめらかに伸びる）
        const double target = barFraction(score);
        barFrac_ += (target - barFrac_) * BAR_EASE;
        if(std::abs(target - barFrac_) < 0.0005) barFrac_ = target;
    }

    void draw(SDL_Renderer* renderer, double sceneMs){
        using namespace rankgauge;
        using judgestats::HAlign;
        if(!ready_) return;

        const double ap = judgestats::appearOf(PANEL_INDEX, sceneMs);
        if(ap <= 0.001) return;
        const Uint8 a = static_cast<Uint8>(255 * ap);
        const SDL_Rect r = judgestats::slidIn(RECT, ap);

        judgestats::drawHudPanel(renderer, r, tab_, a);

        // ----- 評価の文字（ポップ付き） -----
        const double pt = std::clamp((sceneMs - popStartMs_) / RANK_POP_MS, 0.0, 1.0);
        const double scale = 1.0 + RANK_POP_SIZE * (1.0 - judgestats::easeOutCubic01(pt));
        judgestats::blitHudText(renderer, rankText_[rankIndex_], r.x + r.w / 2, r.y + RANK_Y,
                                HAlign::Center, a, scale);

        // ----- バー -----
        const int barX = r.x + BAR_PAD_X;
        const int barW = r.w - BAR_PAD_X * 2;
        const int barY = r.y + BAR_Y;
        auto xOf = [&](int score){
            return barX + static_cast<int>(std::lround(barFraction(score) * barW));
        };

        // 下地
        judgestats::fillHud(renderer, SDL_Rect{barX, barY, barW, BAR_H}, SDL_Color{40, 40, 60, 255}, a);

        // 伸びている部分：今の評価の色
        const int fillW = static_cast<int>(std::lround(barFrac_ * barW));
        if(fillW > 0){
            SDL_Color c = STEPS[rankIndex_].color;
            c.a = 255;
            judgestats::fillHud(renderer, SDL_Rect{barX, barY, fillW, BAR_H}, c, a);
            // 先端に白い線（今どこまで来ているか分かりやすく）
            judgestats::fillHud(renderer, SDL_Rect{barX + fillW - 2, barY, 2, BAR_H}, judgestats::COLOR_WHITE, a);
        }

        // 枠
        SDL_SetRenderDrawColor(renderer, 110, 110, 150, a);
        SDL_Rect frame = {barX, barY, barW, BAR_H};
        SDL_RenderDrawRect(renderer, &frame);

        // ----- 評価が切り替わる位置の縦線と評価名 -----
        //   F（0 点）は線を引かない。名前は線が近い所で重ならないよう、1段目・2段目に交互に置く
        for(int i = 1; i < STEP_COUNT; i++){
            if(STEPS[i].minScore <= BAR_MIN_SCORE) continue;   // バーの外（左端より低いスコア）
            const int lx = xOf(STEPS[i].minScore);
            const bool reached = (rankIndex_ >= i);

            // 届いた線は評価の色、まだの線は灰色
            SDL_Color lc = reached ? STEPS[i].color : SDL_Color{150, 150, 170, 255};
            lc.a = 255;
            judgestats::fillHud(renderer, SDL_Rect{lx - 1, barY - TICK_OVER, 2, BAR_H + TICK_OVER * 2}, lc, a);

            const int ly = (i % 2 == 1) ? r.y + LABEL_ROW1_Y : r.y + LABEL_ROW2_Y;
            const Uint8 la = reached ? a : static_cast<Uint8>(a * 0.45);   // まだ届いていない評価は薄く
            judgestats::blitHudText(renderer, labelText_[i], lx, ly, HAlign::Center, la);
        }
    }

private:
    using HudText = judgestats::HudText;

    SDL_Renderer* renderer_ = nullptr;
    bool ready_ = false;

    HudText rankText_[rankgauge::STEP_COUNT];
    HudText labelText_[rankgauge::STEP_COUNT];
    HudText tab_;

    int    rankIndex_ = 0;          // 今の評価（STEPS の添字）
    double barFrac_ = 0.0;          // 今のバーの長さ（0〜1）
    double popStartMs_ = -1e9;      // 評価が上がった時刻
};