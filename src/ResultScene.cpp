#include "GameCommon.h"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstdio>

#define SCREEN_W 1920
#define SCREEN_H 1080

// =====================================================================
//  リザルト画面：選曲画面と同じウィンドウ表示で3つに分ける
//    ・RANK   （左上）評価（SSS など）と ALL PERFECT / FULL COMBO
//    ・SCORE  （左）  スコア・判定数・FAST/LATE・MAX COMBO
//    ・TIMING （右）  叩いたズレの分布グラフ
// =====================================================================

namespace {

// 判定の幅（noteJudge.h と同じ値にしておく）
constexpr int PERFECT_MS = 60;
constexpr int GOOD_MS    = 80;
constexpr int BAD_MS     = 90;

// グラフ：-BAD_MS 〜 +BAD_MS を BIN_MS ずつに区切った棒グラフ
constexpr int BIN_MS    = 5;
constexpr int BIN_COUNT = BAD_MS * 2 / BIN_MS;   // 36本

// ウィンドウの位置 {x, y, w, h}
constexpr SDL_Rect RANK_RECT  = {  80, 110, 560, 300};
constexpr SDL_Rect SCORE_RECT = {  80, 480, 780, 520};
constexpr SDL_Rect GRAPH_RECT = { 940, 110, 900, 890};

// 選曲画面のウィンドウと同じ色
constexpr SDL_Color PANEL_FILL   = { 10,  10,  20, 190};
constexpr SDL_Color PANEL_BORDER = {  0, 200, 255, 255};
constexpr int       PANEL_BORDER_W = 3;

constexpr SDL_Color COLOR_PERFECT = {255, 215,   0, 255};
constexpr SDL_Color COLOR_GOOD    = {  0, 255, 255, 255};
constexpr SDL_Color COLOR_BAD     = {230,  70,  70, 255};
constexpr SDL_Color COLOR_MISS    = {130, 130, 130, 255};
constexpr SDL_Color COLOR_FAST    = { 80, 170, 255, 255};
constexpr SDL_Color COLOR_LATE    = {255, 140,  60, 255};
constexpr SDL_Color COLOR_WHITE   = {255, 255, 255, 255};
constexpr SDL_Color COLOR_GRAY    = {150, 150, 170, 255};

// リザルトの曲
constexpr const char* RESULT_BGM_PATH = "sounds/月の降る街.mp3";   // 無ければ曲なしで表示する
constexpr int BGM_FADE_IN_MS  = 1200;   // 画面に入ったとき、だんだん大きく
constexpr int BGM_FADE_OUT_MS = 600;    // ENTER を押したとき、だんだん小さく

// 出てくるアニメーション
constexpr double APPEAR_MS  = 450.0;   // 1つのウィンドウが出きるまで
constexpr double STAGGER_MS = 130.0;   // ウィンドウごとの出始めのずれ

std::string calcRank(int score){
    if(score >= 990000) return "SSS";
    else if(score >= 950000) return "SS";
    else if(score >= 900000) return "S";
    else if(score >= 880000) return "AAA";
    else if(score >= 850000) return "AA";
    else if(score >= 800000) return "A";
    else if(score >= 700000) return "B";
    else if(score >= 600000) return "C";
    return "F";
}

SDL_Color rankColor(const std::string& rank){
    if(rank == "SSS") return {255, 215, 0, 255};
    if(rank == "SS")  return {255, 255, 100, 255};
    if(rank == "S")   return {0, 255, 255, 255};
    if(rank == "AAA" || rank == "AA" || rank == "A") return {0, 255, 120, 255};
    if(rank == "B")   return {255, 165, 0, 255};
    if(rank == "C")   return {200, 200, 200, 255};
    return {211, 211, 211, 200};
}

double easeOutCubic01(double t){
    t = std::clamp(t, 0.0, 1.0);
    return 1.0 - std::pow(1.0 - t, 3.0);
}

enum class Align{ Left, Center, Right };

// y は文字の縦の中心
void drawText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, SDL_Color color,
              int x, int y, Align align, Uint8 alpha){
    if(!font || text.empty()) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if(!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(tex, alpha);

    int left = x;
    if(align == Align::Center) left = x - surf->w / 2;
    if(align == Align::Right)  left = x - surf->w;
    SDL_Rect dst = {left, y - surf->h / 2, surf->w, surf->h};
    SDL_RenderCopy(renderer, tex, NULL, &dst);

    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

void fillRect(SDL_Renderer* renderer, const SDL_Rect& r, SDL_Color c, Uint8 alpha){
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, static_cast<Uint8>(c.a * alpha / 255));
    SDL_RenderFillRect(renderer, &r);
}

// 選曲画面と同じウィンドウ：半透明の塗り＋枠線＋左上のタイトルタブ
void drawPanel(SDL_Renderer* renderer, TTF_Font* tabFont, const SDL_Rect& r, const char* title, Uint8 alpha){
    // タブ
    if(tabFont){
        SDL_Surface* surf = TTF_RenderUTF8_Blended(tabFont, title, SDL_Color{15, 15, 25, 255});
        if(surf){
            SDL_Rect tab = {r.x, r.y - surf->h - 6, surf->w + 28, surf->h + 6};
            fillRect(renderer, tab, PANEL_BORDER, alpha);
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(tex, alpha);
            SDL_Rect dst = {tab.x + 14, tab.y + 3, surf->w, surf->h};
            SDL_RenderCopy(renderer, tex, NULL, &dst);
            SDL_DestroyTexture(tex);
            SDL_FreeSurface(surf);
        }
    }
    // 本体
    fillRect(renderer, r, PANEL_FILL, alpha);
    SDL_SetRenderDrawColor(renderer, PANEL_BORDER.r, PANEL_BORDER.g, PANEL_BORDER.b, alpha);
    for(int t = 0; t < PANEL_BORDER_W; t++){
        SDL_Rect b = {r.x + t, r.y + t, r.w - t * 2, r.h - t * 2};
        SDL_RenderDrawRect(renderer, &b);
    }
}

// 何番目のウィンドウかと経過時間から、出現の進み具合(0〜1)を求める
double appearOf(int index, double elapsedMs){
    return easeOutCubic01((elapsedMs - index * STAGGER_MS) / APPEAR_MS);
}

// 出現途中は少し下にずらす
SDL_Rect shifted(const SDL_Rect& r, double appear){
    return SDL_Rect{r.x, r.y + static_cast<int>((1.0 - appear) * 40.0), r.w, r.h};
}

} // namespace

// =====================================================================
GameScene resultScene(SDL_Window* window, SDL_Renderer* renderer, const ResultData& result, SDL_Texture* targetTex){
    TTF_Font* fontRank  = TTF_OpenFont("fonts/prac.ttf", 150);
    TTF_Font* fontLarge = TTF_OpenFont("fonts/prac.ttf", 80);
    TTF_Font* fontMed   = TTF_OpenFont("fonts/prac.ttf", 44);
    TTF_Font* fontSmall = TTF_OpenFont("fonts/prac.ttf", 32);
    TTF_Font* fontTab   = TTF_OpenFont("fonts/prac.ttf", 24);

    auto closeFonts = [&](){
        for(TTF_Font* f : {fontRank, fontLarge, fontMed, fontSmall, fontTab}){
            if(f) TTF_CloseFont(f);
        }
    };

    if(!fontRank || !fontLarge || !fontMed || !fontSmall || !fontTab){
        std::cout << "[ERROR] ResultSceneフォント読込失敗\n";
        closeFonts();
        return GameScene::Select;
    }

    const std::string rank = calcRank(result.score);
    const SDL_Color rColor = rankColor(rank);

    // ----- ズレの集計（一度だけ） -----
    int fastCount = 0, lateCount = 0;
    int bins[BIN_COUNT] = {};
    double sum = 0.0, sumSq = 0.0;
    for(const TimingSample& s : result.timings){
        // FAST/LATE は PERFECT 以外で数える（プレイ中の表示と同じ）
        if(s.judgeType != 1){
            if(s.diffMs < 0) fastCount++;
            else if(s.diffMs > 0) lateCount++;
        }
        int idx = (static_cast<int>(s.diffMs) + BAD_MS) / BIN_MS;
        idx = std::clamp(idx, 0, BIN_COUNT - 1);
        bins[idx]++;
        sum   += s.diffMs;
        sumSq += static_cast<double>(s.diffMs) * s.diffMs;
    }
    const int sampleCount = static_cast<int>(result.timings.size());
    const int maxBin = std::max(1, *std::max_element(bins, bins + BIN_COUNT));
    const double mean   = sampleCount > 0 ? sum / sampleCount : 0.0;
    const double stddev = sampleCount > 0 ? std::sqrt(std::max(0.0, sumSq / sampleCount - mean * mean)) : 0.0;

    // ----- 描画 -----
    auto drawAll = [&](double elapsedMs){
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 10, 10, 20, 255);
        SDL_RenderClear(renderer);

        // ===== 1. RANK（左上） =====
        {
            double ap = appearOf(0, elapsedMs);
            Uint8 a = static_cast<Uint8>(255 * ap);
            SDL_Rect r = shifted(RANK_RECT, ap);
            drawPanel(renderer, fontTab, r, "RANK", a);

            int cx = r.x + r.w / 2;
            drawText(renderer, fontRank, rank, rColor, cx, r.y + 130, Align::Center, a);

            std::string status;
            SDL_Color statusColor = COLOR_PERFECT;
            if(result.isAutoplay){
                status = "AUTO PLAY";
                statusColor = SDL_Color{190, 140, 255, 255};   // 紫：自分のプレイではないと分かる色
            }
            else if(result.isAP) status = "ALL PERFECT";
            else if(result.isFC) status = "FULL COMBO";
            if(!status.empty()){
                drawText(renderer, fontMed, status, statusColor, cx, r.y + r.h - 55, Align::Center, a);
            }
        }

        // ===== 2. SCORE（左） =====
        {
            double ap = appearOf(1, elapsedMs);
            Uint8 a = static_cast<Uint8>(255 * ap);
            SDL_Rect r = shifted(SCORE_RECT, ap);
            drawPanel(renderer, fontTab, r, "SCORE", a);

            const int left  = r.x + 40;
            const int right = r.x + r.w - 40;

            // スコアはカウントアップ
            double countT = easeOutCubic01((elapsedMs - STAGGER_MS - 200.0) / 800.0);
            char scoreBuf[16];
            std::snprintf(scoreBuf, sizeof(scoreBuf), "%07d", static_cast<int>(result.score * countT));
            drawText(renderer, fontMed, "SCORE", COLOR_GRAY, left, r.y + 60, Align::Left, a);
            drawText(renderer, fontLarge, scoreBuf, COLOR_WHITE, right, r.y + 60, Align::Right, a);

            // 区切り線
            fillRect(renderer, SDL_Rect{left, r.y + 115, r.w - 80, 2}, SDL_Color{110, 110, 150, 255}, a);

            struct Row{ const char* label; int value; SDL_Color color; };
            const Row rows[] = {
                {"PERFECT",   result.perfectCount, COLOR_PERFECT},
                {"GOOD",      result.goodCount,    COLOR_GOOD},
                {"BAD",       result.badCount,     COLOR_BAD},
                {"MISS",      result.missCount,    COLOR_MISS},
                {"FAST",      fastCount,           COLOR_FAST},
                {"LATE",      lateCount,           COLOR_LATE},
                {"MAX COMBO", result.maxCombo,     COLOR_WHITE},
            };
            int y = r.y + 150;
            for(int i = 0; i < 7; i++){
                if(i == 4 || i == 6){
                    // 判定数 / FAST・LATE / MAX COMBO の間に細い区切り
                    fillRect(renderer, SDL_Rect{left, y - 22, r.w - 80, 1}, SDL_Color{70, 70, 90, 255}, a);
                    y += 6;
                }
                drawText(renderer, fontSmall, rows[i].label, rows[i].color, left, y, Align::Left, a);
                drawText(renderer, fontSmall, std::to_string(rows[i].value), COLOR_WHITE, right, y, Align::Right, a);
                y += 48;
            }
        }

        // ===== 3. TIMING（右） =====
        {
            double ap = appearOf(2, elapsedMs);
            Uint8 a = static_cast<Uint8>(255 * ap);
            SDL_Rect r = shifted(GRAPH_RECT, ap);
            drawPanel(renderer, fontTab, r, "TIMING", a);

            // グラフの描画範囲
            const int px = r.x + 50;
            const int pw = r.w - 100;
            const int py = r.y + 100;
            const int ph = r.h - 340;
            const int bottom = py + ph;

            auto toX = [&](double ms){
                return px + static_cast<int>((ms + BAD_MS) / (BAD_MS * 2.0) * pw);
            };

            drawText(renderer, fontMed, "FAST", COLOR_FAST, px, r.y + 50, Align::Left, a);
            drawText(renderer, fontMed, "LATE", COLOR_LATE, px + pw, r.y + 50, Align::Right, a);

            // 判定の幅を背景の帯で示す（外側から BAD → GOOD → PERFECT）
            fillRect(renderer, SDL_Rect{toX(-BAD_MS),     py, toX(BAD_MS)     - toX(-BAD_MS),     ph}, SDL_Color{230,  70,  70, 28}, a);
            fillRect(renderer, SDL_Rect{toX(-GOOD_MS),    py, toX(GOOD_MS)    - toX(-GOOD_MS),    ph}, SDL_Color{  0, 255, 255, 28}, a);
            fillRect(renderer, SDL_Rect{toX(-PERFECT_MS), py, toX(PERFECT_MS) - toX(-PERFECT_MS), ph}, SDL_Color{255, 215,   0, 28}, a);

            // 棒：下から伸びる
            double growT = easeOutCubic01((elapsedMs - STAGGER_MS * 2 - 150.0) / 700.0);
            const int barW = std::max(2, pw / BIN_COUNT - 2);
            for(int i = 0; i < BIN_COUNT; i++){
                if(bins[i] == 0) continue;
                double centerMs = -BAD_MS + (i + 0.5) * BIN_MS;
                double absC = std::fabs(centerMs);
                SDL_Color c = (absC <= PERFECT_MS) ? COLOR_PERFECT : (absC <= GOOD_MS) ? COLOR_GOOD : COLOR_BAD;

                int h = static_cast<int>(static_cast<double>(bins[i]) / maxBin * (ph - 20) * growT);
                int x = toX(-BAD_MS + i * BIN_MS) + 1;
                fillRect(renderer, SDL_Rect{x, bottom - h, barW, h}, c, a);
            }

            // 0ms の線と、平均の位置（金色の印）
            fillRect(renderer, SDL_Rect{toX(0) - 1, py, 2, ph}, COLOR_WHITE, a);
            if(sampleCount > 0){
                fillRect(renderer, SDL_Rect{toX(mean) - 3, bottom + 4, 6, 16}, SDL_Color{255, 190, 30, 255}, a);
            }
            fillRect(renderer, SDL_Rect{px, bottom, pw, 2}, SDL_Color{110, 110, 150, 255}, a);

            // 目盛り
            const int ticks[] = {-BAD_MS, -PERFECT_MS, 0, PERFECT_MS, BAD_MS};
            for(int t : ticks){
                char buf[16];
                if(t == 0) std::snprintf(buf, sizeof(buf), "0");
                else       std::snprintf(buf, sizeof(buf), "%+d", t);
                drawText(renderer, fontSmall, buf, COLOR_GRAY, toX(t), bottom + 40, Align::Center, a);
            }

            // 数値
            const int statY = bottom + 110;
            if(sampleCount > 0){
                char buf[64];
                std::snprintf(buf, sizeof(buf), "平均  %+.1f ms", mean);
                drawText(renderer, fontMed, buf, COLOR_WHITE, px, statY, Align::Left, a);
                std::snprintf(buf, sizeof(buf), "ばらつき  %.1f ms", stddev);
                drawText(renderer, fontMed, buf, COLOR_WHITE, px + pw, statY, Align::Right, a);
            }
            else{
                drawText(renderer, fontMed, "記録なし", COLOR_GRAY, r.x + r.w / 2, statY, Align::Center, a);
            }
        }

        drawText(renderer, fontSmall, "Press ENTER to continue", COLOR_GRAY,
                 SCREEN_W / 2, SCREEN_H - 40, Align::Center, 255);
    };

    // 遷移用の1枚絵：アニメーションが終わった状態で描く
    if(targetTex != nullptr){
        SDL_SetRenderTarget(renderer, targetTex);
        drawAll(1e9);
        SDL_SetRenderTarget(renderer, NULL);
        closeFonts();
        return GameScene::Result;
    }

    // リザルトの曲：フェードインで流し始め、ループさせる
    Mix_HaltMusic();   // プレイ曲が残っていれば止める
    Mix_Music* bgm = Mix_LoadMUS(RESULT_BGM_PATH);
    if(bgm){
        Mix_FadeInMusic(bgm, -1, BGM_FADE_IN_MS);   // -1 = 無限ループ
    }
    else{
        std::cout << "[情報]リザルトの曲が見つからない: " << RESULT_BGM_PATH << "\n";
    }

    bool running = true;
    bool leaving = false;            // ENTER が押されて、フェードアウト中か
    Uint32 leaveTicks = 0;           // フェードアウトを始めた時刻
    SDL_Event e;
    GameScene nextScene = GameScene::Select;
    const Uint32 startTicks = SDL_GetTicks();

    while(running){
        while(SDL_PollEvent(&e) != 0){
            if(e.type == SDL_QUIT){
                running = false;
                nextScene = GameScene::Shutdown;
            }
            if(!leaving && e.type == SDL_KEYDOWN && !e.key.repeat &&
               (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_SPACE)){
                // すぐには抜けず、曲をフェードアウトさせてから選曲画面へ
                leaving = true;
                leaveTicks = SDL_GetTicks();
                nextScene = GameScene::Select;
                if(bgm) Mix_FadeOutMusic(BGM_FADE_OUT_MS);
            }
        }

        // フェードアウトが終わったら抜ける（曲が無いときも同じ時間だけ待つ）
        if(leaving && SDL_GetTicks() - leaveTicks >= static_cast<Uint32>(BGM_FADE_OUT_MS)){
            running = false;
        }

        drawAll(static_cast<double>(SDL_GetTicks() - startTicks));

        // フェードアウトに合わせて画面も暗くする
        if(leaving){
            double t = std::clamp(static_cast<double>(SDL_GetTicks() - leaveTicks) / BGM_FADE_OUT_MS, 0.0, 1.0);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<Uint8>(255 * t));
            SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
            SDL_RenderFillRect(renderer, &full);
        }

        SDL_RenderPresent(renderer);
        g_framePacer.endFrame(window);   // 待たずに回り続けて CPU を使い切らないように
    }

    if(bgm){
        Mix_HaltMusic();
        Mix_FreeMusic(bgm);
    }
    closeFonts();
    return nextScene;
}