#pragma once
// =====================================================================
//  プレイ画面の右に出すウィンドウ（JudgeStatsPanel.h）
//    ・共通の部品（ウィンドウの枠・文字のテクスチャ・出現アニメーション）
//    ・JUDGE ウィンドウ：判定の数をリアルタイムに出す（リザルトの SCORE ウィンドウと同じ並び・色）
//        PERFECT
//        GOOD     [FAST 数] [LATE 数]
//        BAD      [FAST 数] [LATE 数]
//        MISS
//        ─────
//        FAST / LATE（合計）
//        ─────
//        MAX COMBO
//    ・RANK ウィンドウは RankGaugePanel.h（ここの共通部品を使う）
//
//  出てくる順番（リザルト画面と同じく、ウィンドウごとに少しずつずらす）
//    0 番目：RANK  … プレイ画面が始まって APPEAR_DELAY_MS 後に出始める
//    1 番目：JUDGE … さらに STAGGER_MS 後に出始める
//    PlayScene は ALL_APPEARED_MS（全部が出きる時刻）より後に曲を始める
//
//  重くしないための工夫
//    ・変わらない文字は最初に1回だけテクスチャにする
//    ・数字は「値が変わったときだけ」作り直す（毎フレーム TTF で文字を作らない）
//    ・FAST / LATE の数は timingLog の「新しく増えた分」だけを数える
// =====================================================================
#include "GameCommon.h"
#include "Play/scoreTracker.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace judgestats {

// ----- タイミング -----
constexpr double APPEAR_DELAY_MS = 100.0;   // プレイ画面が始まってから、最初のウィンドウが出始めるまで
constexpr double APPEAR_MS       = 450.0;   // 1つのウィンドウが出始めてから出きるまで（リザルト画面と同じ）
constexpr double STAGGER_MS      = 130.0;   // ウィンドウごとの出始めのずれ（リザルト画面と同じ）
constexpr int    PANEL_COUNT     = 2;       // RANK と JUDGE
constexpr int    APPEAR_SLIDE_PX = 60;      // 右から何 px スライドしてくるか

// 全部のウィンドウが出きる時刻（= 100 + 130 + 450 = 680ms）
constexpr double ALL_APPEARED_MS = APPEAR_DELAY_MS + STAGGER_MS * (PANEL_COUNT - 1) + APPEAR_MS;

// ----- JUDGE ウィンドウの位置と大きさ -----
//   レーンは x = 600〜1320。その右（1350〜1890）に置く。上には RANK ウィンドウ（RankGaugePanel.h）
constexpr SDL_Rect RECT = {1350, 450, 540, 400};

// 列の位置（ウィンドウの左端 + PAD_X からの距離）。リザルト画面と同じ考え方で幅だけ詰めたもの
constexpr int PAD_X           = 24;
constexpr int COL_COUNT_RIGHT = 220;   // 判定の数（右揃え）
constexpr int COL_FAST_TAG    = 248;   // 「FAST」の文字（左揃え）
constexpr int COL_FAST_RIGHT  = 352;   // FAST の数（右揃え）
constexpr int COL_LATE_TAG    = 380;   // 「LATE」の文字（左揃え）
constexpr int COL_LATE_RIGHT  = 492;   // LATE の数（右揃え）= ウィンドウの右端 - PAD_X

constexpr int ROW_TOP  = 42;   // 1行目の縦の中心（ウィンドウの上端から）
constexpr int ROW_STEP = 44;   // 行の間隔
constexpr int SEP_GAP  = 8;    // 区切り線のところで足す隙間

// ----- 色（リザルト画面・選曲画面と同じ） -----
constexpr SDL_Color PANEL_FILL   = { 10,  10,  20, 190};
constexpr SDL_Color PANEL_BORDER = {  0, 200, 255, 255};
constexpr int       PANEL_BORDER_W = 3;
constexpr SDL_Color SEP_COLOR    = { 70,  70,  90, 255};

constexpr SDL_Color COLOR_PERFECT = {255, 215,   0, 255};
constexpr SDL_Color COLOR_GOOD    = {  0, 255, 255, 255};
constexpr SDL_Color COLOR_BAD     = {230,  70,  70, 255};
constexpr SDL_Color COLOR_MISS    = {130, 130, 130, 255};
constexpr SDL_Color COLOR_FAST    = { 80, 170, 255, 255};
constexpr SDL_Color COLOR_LATE    = {255, 140,  60, 255};
constexpr SDL_Color COLOR_WHITE   = {255, 255, 255, 255};
constexpr SDL_Color COLOR_DIM     = { 90,  90, 110, 255};   // 0 のときの数字

inline double easeOutCubic01(double t){
    t = std::clamp(t, 0.0, 1.0);
    return 1.0 - std::pow(1.0 - t, 3.0);
}

// index 番目のウィンドウの出現の進み具合（0〜1）。sceneMs = プレイ画面が始まってからの時間
inline double appearOf(int index, double sceneMs){
    return easeOutCubic01((sceneMs - APPEAR_DELAY_MS - index * STAGGER_MS) / APPEAR_MS);
}

// ---------------------------------------------------------------------
//  文字のテクスチャ（作るのは1回、貼るのは毎フレーム）
// ---------------------------------------------------------------------
struct HudText{ SDL_Texture* tex = nullptr; int w = 0; int h = 0; };
enum class HAlign{ Left, Center, Right };

inline HudText makeHudText(SDL_Renderer* r, TTF_Font* f, const std::string& s, SDL_Color c){
    HudText t;
    if(!r || !f || s.empty()) return t;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), c);
    if(!surf) return t;
    t.tex = SDL_CreateTextureFromSurface(r, surf);
    if(t.tex) SDL_SetTextureBlendMode(t.tex, SDL_BLENDMODE_BLEND);
    t.w = surf->w;
    t.h = surf->h;
    SDL_FreeSurface(surf);
    return t;
}

inline void freeHudText(HudText& t){
    if(t.tex) SDL_DestroyTexture(t.tex);
    t = HudText{};
}

// y は文字の縦の中心。scale で中心を保ったまま拡大・縮小する
inline void blitHudText(SDL_Renderer* r, const HudText& t, int x, int y, HAlign align, Uint8 alpha, double scale = 1.0){
    if(!t.tex) return;
    const int w = static_cast<int>(t.w * scale);
    const int h = static_cast<int>(t.h * scale);
    int left = x;
    if(align == HAlign::Center) left = x - w / 2;
    if(align == HAlign::Right)  left = x - w;
    SDL_SetTextureAlphaMod(t.tex, alpha);
    SDL_Rect d = {left, y - h / 2, w, h};
    SDL_RenderCopy(r, t.tex, NULL, &d);
}

inline void fillHud(SDL_Renderer* r, const SDL_Rect& rect, SDL_Color c, Uint8 alpha){
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, static_cast<Uint8>(c.a * alpha / 255));
    SDL_RenderFillRect(r, &rect);
}

// 選曲画面・リザルト画面と同じウィンドウ：左上のタブ＋半透明の塗り＋シアンの枠
inline void drawHudPanel(SDL_Renderer* r, const SDL_Rect& rect, const HudText& tabText, Uint8 alpha){
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    if(tabText.tex){
        SDL_Rect tab = {rect.x, rect.y - tabText.h - 6, tabText.w + 28, tabText.h + 6};
        fillHud(r, tab, PANEL_BORDER, alpha);
        blitHudText(r, tabText, tab.x + 14, tab.y + 3 + tabText.h / 2, HAlign::Left, alpha);
    }
    fillHud(r, rect, PANEL_FILL, alpha);
    SDL_SetRenderDrawColor(r, PANEL_BORDER.r, PANEL_BORDER.g, PANEL_BORDER.b, alpha);
    for(int t = 0; t < PANEL_BORDER_W; t++){
        SDL_Rect b = {rect.x + t, rect.y + t, rect.w - t * 2, rect.h - t * 2};
        SDL_RenderDrawRect(r, &b);
    }
}

// 出現途中のウィンドウの位置：右から APPEAR_SLIDE_PX だけスライドしてくる
inline SDL_Rect slidIn(const SDL_Rect& r, double appear){
    return SDL_Rect{r.x + static_cast<int>((1.0 - appear) * APPEAR_SLIDE_PX), r.y, r.w, r.h};
}

} // namespace judgestats

// =====================================================================
//  JUDGE ウィンドウ
// =====================================================================
class JudgeStatsPanel{
public:
    static constexpr int PANEL_INDEX = 1;   // 出てくる順番（RANK の次）

    // フォントを開き、変わらない文字を先にテクスチャにする。失敗しても落ちない（描かないだけ）
    bool init(SDL_Renderer* renderer){
        using namespace judgestats;
        renderer_ = renderer;
        font_ = TTF_OpenFont("fonts/prac.ttf", 26);
        tagFont_ = TTF_OpenFont("fonts/prac.ttf", 20);
        if(!font_ || !tagFont_) return false;

        const char* labels[ROW_COUNT] = {"PERFECT", "GOOD", "BAD", "MISS", "FAST", "LATE", "MAX COMBO"};
        const SDL_Color colors[ROW_COUNT] = {COLOR_PERFECT, COLOR_GOOD, COLOR_BAD, COLOR_MISS,
                                             COLOR_FAST, COLOR_LATE, COLOR_WHITE};
        for(int i = 0; i < ROW_COUNT; i++) labels_[i] = makeHudText(renderer_, font_, labels[i], colors[i]);

        fastTag_ = makeHudText(renderer_, tagFont_, "FAST", COLOR_FAST);
        lateTag_ = makeHudText(renderer_, tagFont_, "LATE", COLOR_LATE);
        tab_     = makeHudText(renderer_, tagFont_, "JUDGE", SDL_Color{15, 15, 25, 255});
        return true;
    }

    void destroy(){
        using namespace judgestats;
        for(auto& t : labels_) freeHudText(t);
        freeHudText(fastTag_);
        freeHudText(lateTag_);
        freeHudText(tab_);
        for(auto& n : nums_) freeHudText(n.text);
        if(font_)    TTF_CloseFont(font_);
        if(tagFont_) TTF_CloseFont(tagFont_);
        font_ = tagFont_ = nullptr;
    }

    // 毎フレーム呼ぶ：今の数を集める（FAST / LATE は増えた分だけ数える）
    void update(const ScoreTracker& st, const std::vector<TimingSample>& log){
        if(log.size() < processed_){   // 記録が減った（作り直された）ときは数え直す
            processed_ = 0;
            for(int k = 0; k < 4; k++) fastOf_[k] = lateOf_[k] = 0;
        }
        for(; processed_ < log.size(); processed_++){
            const TimingSample& s = log[processed_];
            if(s.judgeType == 1) continue;   // PERFECT は FAST / LATE に数えない（リザルトと同じ）
            const int jt = std::clamp(s.judgeType, 0, 3);
            if(s.diffMs < 0)      fastOf_[jt]++;
            else if(s.diffMs > 0) lateOf_[jt]++;
        }

        values_[N_PERFECT]  = st.getPerfectCount();
        values_[N_GOOD]     = st.getGoodCount();
        values_[N_GOOD_F]   = fastOf_[2];
        values_[N_GOOD_L]   = lateOf_[2];
        values_[N_BAD]      = st.getBadCount();
        values_[N_BAD_F]    = fastOf_[3];
        values_[N_BAD_L]    = lateOf_[3];
        values_[N_MISS]     = st.getMissCount();
        values_[N_FAST]     = fastOf_[2] + fastOf_[3];
        values_[N_LATE]     = lateOf_[2] + lateOf_[3];
        values_[N_MAXCOMBO] = st.getMaxComboAchieved();
    }

    // sceneMs = プレイ画面が始まってからの時間（ポーズ中は止まる時計）
    void draw(SDL_Renderer* renderer, double sceneMs){
        using namespace judgestats;
        if(!font_) return;

        const double ap = appearOf(PANEL_INDEX, sceneMs);
        if(ap <= 0.001) return;
        const Uint8 a = static_cast<Uint8>(255 * ap);
        const SDL_Rect r = slidIn(RECT, ap);

        drawHudPanel(renderer, r, tab_, a);

        //   row: 項目名の番号, count: 数の番号, fast / late: 内訳の番号（-1 = なし）
        struct Line{ int row; int count; int fast; int late; };
        const Line lines[ROW_COUNT] = {
            {0, N_PERFECT,  -1,       -1},
            {1, N_GOOD,     N_GOOD_F, N_GOOD_L},
            {2, N_BAD,      N_BAD_F,  N_BAD_L},
            {3, N_MISS,     -1,       -1},
            {4, N_FAST,     -1,       -1},
            {5, N_LATE,     -1,       -1},
            {6, N_MAXCOMBO, -1,       -1},
        };

        const int left = r.x + PAD_X;
        int y = r.y + ROW_TOP;
        for(int i = 0; i < ROW_COUNT; i++){
            if(i == 4 || i == 6){
                // 判定数 / FAST・LATE / MAX COMBO の間に細い区切り
                fillHud(renderer, SDL_Rect{left, y - ROW_STEP / 2, r.w - PAD_X * 2, 1}, SEP_COLOR, a);
                y += SEP_GAP;
            }
            const Line& ln = lines[i];
            blitHudText(renderer, labels_[ln.row], left, y, HAlign::Left, a);
            blitHudText(renderer, number(ln.count, false), left + COL_COUNT_RIGHT, y, HAlign::Right, a);

            if(ln.fast >= 0){
                blitHudText(renderer, fastTag_, left + COL_FAST_TAG, y, HAlign::Left, a);
                blitHudText(renderer, number(ln.fast, true), left + COL_FAST_RIGHT, y, HAlign::Right, a);
                blitHudText(renderer, lateTag_, left + COL_LATE_TAG, y, HAlign::Left, a);
                blitHudText(renderer, number(ln.late, true), left + COL_LATE_RIGHT, y, HAlign::Right, a);
            }
            y += ROW_STEP;
        }
    }

private:
    using HudText = judgestats::HudText;
    struct Num{ HudText text; int value = -1; bool dimZero = false; };

    // 数の番号
    enum{ N_PERFECT, N_GOOD, N_GOOD_F, N_GOOD_L, N_BAD, N_BAD_F, N_BAD_L, N_MISS,
          N_FAST, N_LATE, N_MAXCOMBO, N_COUNT };
    static constexpr int ROW_COUNT = 7;

    SDL_Renderer* renderer_ = nullptr;
    TTF_Font* font_ = nullptr;
    TTF_Font* tagFont_ = nullptr;

    HudText labels_[ROW_COUNT];
    HudText fastTag_, lateTag_, tab_;
    Num     nums_[N_COUNT];
    int     values_[N_COUNT] = {};

    size_t processed_ = 0;      // timingLog のうち、もう数えた数
    int fastOf_[4] = {}, lateOf_[4] = {};

    // 数のテクスチャ：値（と 0 のときの色）が変わったときだけ作り直す
    const HudText& number(int idx, bool dimZero){
        Num& n = nums_[idx];
        const int v = values_[idx];
        if(n.value != v || n.dimZero != dimZero || !n.text.tex){
            judgestats::freeHudText(n.text);
            const SDL_Color c = (dimZero && v == 0) ? judgestats::COLOR_DIM : judgestats::COLOR_WHITE;
            n.text = judgestats::makeHudText(renderer_, font_, std::to_string(v), c);
            n.value = v;
            n.dimZero = dimZero;
        }
        return n.text;
    }
};