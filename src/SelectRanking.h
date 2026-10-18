#pragma once
// SelectRanking.h
// 選曲画面のランキング表示。
//   ・曲を選んでいる間 : 右側の「RANKING」ウィンドウに上位3位
//   ・F5               : 上位50位までの大きなウィンドウ（↑↓/ホイールでスクロール、F5/ESC で閉じる）
//
// ■ 動作の理屈
//   ・ランキングは GAS から取るので1回1〜2秒かかる。カーソルを動かすたびに取りに行くと
//     通信が渋滞するので、「同じ曲・難易度に 0.3 秒とどまったら」取りに行く（デバウンス）。
//   ・取った結果は「曲ID＋難易度名」をキーに覚えておき、同じ曲に戻ったらすぐ表示する。
//     30秒（大ウィンドウ表示中は10秒）より古ければ、表示はそのままで裏で取り直す。
//   ・同時に出す通信は最大2つまで。
//   ・曲IDと難易度名は、スコア送信時と同じ makeScoreRecord() で作る（作り方が1か所なのでズレない）。
//   ・選曲画面に入るたびに作り直すので、プレイ後に戻ってきたときは最新のランキングが取られる。

#include "GameCommon.h"
#include "ScoreSync.h"
#include "NameEntryScene.h"   // makeScoreRecord()

#include <cstdio>
#include <unordered_map>
#include <unordered_set>

class SelectRanking{
public:
    bool init();      // フォントを開く（選曲画面の開始時に1回）
    void destroy();   // テクスチャとフォントを解放（選曲画面の終了時に1回）

    // 毎フレーム、いま選んでいる曲を教える（fileIndex = JSON 上の難易度番号）
    void setTarget(const std::string& scorePath, int fileIndex, const std::string& title);
    void clearTarget();   // 曲を選んでいないとき（ジャンル選択中）
    void update();        // 毎フレーム：結果の受け取りと、必要なら取得の依頼

    bool isOpen() const { return windowOpen_; }
    void open();
    void close(){ windowOpen_ = false; }
    void handleKey(const SDL_KeyboardEvent& key);   // 大ウィンドウ表示中のキー
    void handleWheel(int wheelY);                   // 大ウィンドウ表示中のホイール

    void drawTop3(SDL_Renderer* renderer, double anim);   // anim = 選曲画面の detailAnimation
    void drawWindow(SDL_Renderer* renderer);

private:
    struct Entry{
        bool loading = false;      // 通信中
        bool hasData = false;      // 一度でも取れたか
        bool lastFailed = false;   // 直近の取得に失敗したか
        Uint32 fetchedAt = 0;      // 最後に結果が返ってきた時刻
        int total = 0;
        std::vector<RankEntry> entries;
    };
    struct CachedText{ SDL_Texture* tex = nullptr; int w = 0, h = 0; };
    enum class Align{ Left, Center, Right };

    static std::string makeKey(const std::string& songId, const std::string& diff){
        return songId + '\x1f' + diff;
    }
    const Entry* current() const{
        auto it = cache_.find(curKey_);
        return it == cache_.end() ? nullptr : &it->second;
    }
    void clampScroll();
    const CachedText* text(SDL_Renderer* r, TTF_Font* font, const std::string& s, SDL_Color c);
    int drawText(SDL_Renderer* r, TTF_Font* font, const std::string& s, SDL_Color c,
                 int x, int y, Align align, Uint8 alpha);
    void drawPanel(SDL_Renderer* r, const SDL_Rect& rect, const std::string& title, bool focused, Uint8 alpha);
    void clearTexts();

    // ----- 状態 -----
    std::string curKey_, curSongId_, curDiff_, curTitle_;
    Uint32 keyChangedAt_ = 0;
    std::unordered_map<std::string, Entry> cache_;
    std::unordered_set<std::string> inFlight_;
    std::unordered_map<std::string, std::pair<std::string, std::string>> idCache_;   // 譜面パス#番号 → (曲ID, 難易度名)
    bool windowOpen_ = false;
    int scroll_ = 0;

    TTF_Font* fontRow_   = nullptr;   // 上位3位の行
    TTF_Font* fontBig_   = nullptr;   // 大ウィンドウの行
    TTF_Font* fontLabel_ = nullptr;   // タブ・注意書き
    std::unordered_map<std::string, CachedText> texts_;

    // ----- 配置（論理解像度 1920x1080） -----
    static constexpr int TOP3_X = 1320;      // DIFFICULTY / INFO と左端を揃える
    static constexpr int TOP3_Y = 712;
    static constexpr int TOP3_W = 520;
    static constexpr int TOP3_ROW_H = 40;
    static constexpr int TOP3_PAD_Y = 14;
    static constexpr SDL_Rect WIN_RECT = {360, 90, 1200, 900};
    static constexpr int WIN_ROWS = 12;
    static constexpr int WIN_ROW_H = 56;

    static constexpr Uint32 DEBOUNCE_MS      = 300;     // 同じ曲にこれだけとどまったら取得
    static constexpr Uint32 STALE_MS         = 30000;   // これより古ければ取り直す
    static constexpr Uint32 STALE_OPEN_MS    = 10000;   // 大ウィンドウ表示中はこちら
    static constexpr size_t MAX_IN_FLIGHT    = 2;
};

// =====================================================================
//  実装（ヘッダだけで完結させるため inline）
// =====================================================================

inline bool SelectRanking::init(){
    fontRow_   = TTF_OpenFont("fonts/prac.ttf", 28);
    fontBig_   = TTF_OpenFont("fonts/prac.ttf", 34);
    fontLabel_ = TTF_OpenFont("fonts/prac.ttf", 24);
    if(!fontRow_ || !fontBig_ || !fontLabel_){
        std::cout << "[ERROR] SelectRanking フォント読込失敗\n";
        return false;
    }
    return true;
}

inline void SelectRanking::destroy(){
    clearTexts();   // テクスチャはフォントより先に消す
    for(TTF_Font** f : {&fontRow_, &fontBig_, &fontLabel_}){
        if(*f){ TTF_CloseFont(*f); *f = nullptr; }
    }
}

inline void SelectRanking::setTarget(const std::string& scorePath, int fileIndex, const std::string& title){
    // makeScoreRecord は譜面 JSON を読むので、同じ曲・難易度では1回だけ呼んで覚えておく
    const std::string pathKey = scorePath + "#" + std::to_string(fileIndex);
    auto it = idCache_.find(pathKey);
    if(it == idCache_.end()){
        ResultData dummy;
        ScoreRecord rec = makeScoreRecord(scorePath, fileIndex, dummy);
        it = idCache_.emplace(pathKey, std::make_pair(rec.songId, rec.difficulty)).first;
    }

    const std::string key = makeKey(it->second.first, it->second.second);
    if(key != curKey_){
        curKey_ = key;
        curSongId_ = it->second.first;
        curDiff_ = it->second.second;
        keyChangedAt_ = SDL_GetTicks();
        scroll_ = 0;
    }
    curTitle_ = title;
}

inline void SelectRanking::clearTarget(){
    curKey_.clear();
    windowOpen_ = false;
}

inline void SelectRanking::update(){
    const Uint32 now = SDL_GetTicks();

    // 1. 届いた結果をキャッシュに入れる（Ranking 以外は選曲画面では使わない）
    SyncResult r;
    while(ScoreSync::instance().poll(r)){
        if(r.kind != SyncResult::Kind::Ranking) continue;
        const std::string key = makeKey(r.songId, r.difficulty);
        inFlight_.erase(key);
        Entry& e = cache_[key];
        e.loading = false;
        e.fetchedAt = now;
        if(r.ok){
            e.hasData = true;
            e.lastFailed = false;
            e.total = r.total;
            e.entries = std::move(r.entries);
        }
        else{
            e.lastFailed = true;   // 前に取れていたデータは残しておく
        }
        if(key == curKey_) clampScroll();
    }

    // 2. 必要なら取得を依頼する
    if(curKey_.empty() || !ScoreSync::instance().enabled()) return;

    const Uint32 debounce = windowOpen_ ? 0 : DEBOUNCE_MS;
    if(now - keyChangedAt_ < debounce) return;              // まだカーソルが動いている途中かもしれない
    if(inFlight_.count(curKey_) || inFlight_.size() >= MAX_IN_FLIGHT) return;

    auto it = cache_.find(curKey_);
    const Uint32 stale = windowOpen_ ? STALE_OPEN_MS : STALE_MS;
    const bool need = (it == cache_.end()) || (!it->second.loading && now - it->second.fetchedAt > stale);
    if(!need) return;

    ScoreSync::instance().requestRanking(curSongId_, curDiff_);
    inFlight_.insert(curKey_);
    cache_[curKey_].loading = true;
}

inline void SelectRanking::open(){
    if(curKey_.empty()) return;
    windowOpen_ = true;
    scroll_ = 0;
}

inline void SelectRanking::clampScroll(){
    const Entry* e = current();
    const int n = e ? static_cast<int>(e->entries.size()) : 0;
    scroll_ = std::clamp(scroll_, 0, std::max(0, n - WIN_ROWS));
}

inline void SelectRanking::handleKey(const SDL_KeyboardEvent& key){
    switch(key.keysym.sym){
        case SDLK_UP:       scroll_ -= 1;        break;
        case SDLK_DOWN:     scroll_ += 1;        break;
        case SDLK_PAGEUP:   scroll_ -= WIN_ROWS; break;
        case SDLK_PAGEDOWN: scroll_ += WIN_ROWS; break;
        case SDLK_F5:
        case SDLK_ESCAPE:
            if(!key.repeat) windowOpen_ = false;
            break;
        default: break;   // それ以外のキーは何もしない（選曲の操作には渡さない）
    }
    clampScroll();
}

inline void SelectRanking::handleWheel(int wheelY){
    scroll_ -= wheelY;   // 奥に回す(+) = 上へ
    clampScroll();
}

// ---------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------

inline void SelectRanking::drawTop3(SDL_Renderer* rd, double anim){
    if(anim < 0.001 || curKey_.empty()) return;

    const Uint8 alpha = static_cast<Uint8>(anim * 255);
    const int offsetX = static_cast<int>((1.0 - anim) * 100);   // INFO と同じスライドイン
    SDL_Rect rect = {TOP3_X + offsetX, TOP3_Y, TOP3_W, TOP3_ROW_H * 3 + TOP3_PAD_Y * 2};
    drawPanel(rd, rect, "RANKING  [F5]", false, alpha);

    const SDL_Color gray = {150, 150, 170, 255};
    const int cy = rect.y + rect.h / 2;
    const Entry* e = current();

    // 状態ごとのメッセージ
    std::string msg;
    if(!ScoreSync::instance().enabled())       msg = "オフライン";
    else if(!e || !e->hasData){
        msg = (e && e->lastFailed && !e->loading) ? "取得できませんでした" : "取得中…";
    }
    else if(e->entries.empty())                msg = "まだ記録がありません";
    if(!msg.empty()){
        drawText(rd, fontRow_, msg, gray, rect.x + rect.w / 2, cy, Align::Center, alpha);
        return;
    }

    // 上位3位：順位 / 名前 / スコア
    static const SDL_Color medal[3] = {{255, 215, 0, 255}, {200, 210, 220, 255}, {230, 150, 90, 255}};
    const int n = std::min<int>(3, static_cast<int>(e->entries.size()));
    for(int i = 0; i < n; i++){
        const RankEntry& en = e->entries[i];
        const int y = rect.y + TOP3_PAD_Y + TOP3_ROW_H * i + TOP3_ROW_H / 2;
        drawText(rd, fontRow_, std::to_string(en.rank), medal[i], rect.x + 50, y, Align::Right, alpha);
        drawText(rd, fontRow_, en.name, SDL_Color{255, 255, 255, 255}, rect.x + 74, y, Align::Left, alpha);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%07d", en.score);
        drawText(rd, fontRow_, buf, medal[i], rect.x + rect.w - 24, y, Align::Right, alpha);
    }
}

inline void SelectRanking::drawWindow(SDL_Renderer* rd){
    if(!windowOpen_) return;

    // 後ろを暗くして、手前のウィンドウに目が行くようにする
    SDL_SetRenderDrawBlendMode(rd, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(rd, 0, 0, 0, 170);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(rd, &full);

    const SDL_Rect& P = WIN_RECT;
    drawPanel(rd, P, "RANKING TOP50", true, 255);

    const SDL_Color white = {255, 255, 255, 255};
    const SDL_Color gray  = {150, 150, 170, 255};
    const SDL_Color gold  = {255, 215, 0, 255};
    const int cx = P.x + P.w / 2;
    const Entry* e = current();

    drawText(rd, fontBig_, curTitle_ + "  [" + curDiff_ + "]", white, cx, P.y + 50, Align::Center, 255);
    if(e && e->hasData){
        drawText(rd, fontLabel_, "全 " + std::to_string(e->total) + " 件", gray, P.x + P.w - 40, P.y + 50, Align::Right, 255);
    }
    if(e && e->loading && e->hasData){
        drawText(rd, fontLabel_, "更新中…", gray, P.x + 40, P.y + 50, Align::Left, 255);
    }

    drawText(rd, fontLabel_, "↑↓ / ホイール: スクロール　　F5 / ESC: 閉じる", gray, cx, P.y + P.h - 30, Align::Center, 255);

    // 状態ごとのメッセージ
    std::string msg;
    if(!ScoreSync::instance().enabled()) msg = "オフライン（config/online.json を確認）";
    else if(!e || !e->hasData){
        msg = (e && e->lastFailed && !e->loading) ? "取得できませんでした（通信を確認してください）" : "取得中…";
    }
    else if(e->entries.empty()) msg = "まだ記録がありません";
    if(!msg.empty()){
        drawText(rd, fontBig_, msg, gray, cx, P.y + P.h / 2, Align::Center, 255);
        return;
    }

    // 表（右寄せの列は右端の x）
    const int colRank  = P.x + 150;
    const int colName  = P.x + 210;
    const int colScore = P.x + 900;
    const int colCombo = P.x + P.w - 60;
    int y = P.y + 115;
    drawText(rd, fontLabel_, "順位", gray, colRank, y, Align::Right, 255);
    drawText(rd, fontLabel_, "名前", gray, colName, y, Align::Left, 255);
    drawText(rd, fontLabel_, "スコア", gray, colScore, y, Align::Right, 255);
    drawText(rd, fontLabel_, "MAXコンボ", gray, colCombo, y, Align::Right, 255);
    SDL_SetRenderDrawColor(rd, 110, 110, 150, 255);
    SDL_Rect line = {P.x + 40, y + 22, P.w - 80, 2};
    SDL_RenderFillRect(rd, &line);
    y += 30 + WIN_ROW_H / 2;

    const int total = static_cast<int>(e->entries.size());
    const int endIdx = std::min(total, scroll_ + WIN_ROWS);
    for(int i = scroll_; i < endIdx; i++){
        const RankEntry& en = e->entries[i];
        const SDL_Color c = (en.rank <= 3) ? gold : white;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%07d", en.score);
        drawText(rd, fontBig_, std::to_string(en.rank), c, colRank, y, Align::Right, 255);
        drawText(rd, fontBig_, en.name, white, colName, y, Align::Left, 255);
        drawText(rd, fontBig_, buf, c, colScore, y, Align::Right, 255);
        drawText(rd, fontBig_, std::to_string(en.maxCombo), white, colCombo, y, Align::Right, 255);
        y += WIN_ROW_H;
    }

    if(total > WIN_ROWS){
        drawText(rd, fontLabel_, std::to_string(scroll_ + 1) + "〜" + std::to_string(endIdx) + "位 / " +
                 std::to_string(total) + "位まで表示", gray, cx, P.y + P.h - 70, Align::Center, 255);
    }
}

// ---------------------------------------------------------------------
// 補助
// ---------------------------------------------------------------------

// 文字テクスチャのキャッシュ。50行を毎フレーム作り直すと CPU を食うので使い回す
inline const SelectRanking::CachedText* SelectRanking::text(SDL_Renderer* r, TTF_Font* font,
                                                            const std::string& s, SDL_Color c){
    if(!font || s.empty()) return nullptr;
    char head[48];
    std::snprintf(head, sizeof(head), "%p|%02x%02x%02x|", static_cast<void*>(font), c.r, c.g, c.b);
    const std::string key = head + s;
    auto it = texts_.find(key);
    if(it != texts_.end()) return &it->second;

    if(texts_.size() > 600) clearTexts();   // 曲を変えるたびに増えるので上限を設ける

    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, s.c_str(), c);
    if(!surf) return nullptr;
    CachedText ct;
    ct.tex = SDL_CreateTextureFromSurface(r, surf);
    ct.w = surf->w;
    ct.h = surf->h;
    SDL_FreeSurface(surf);
    if(!ct.tex) return nullptr;
    SDL_SetTextureBlendMode(ct.tex, SDL_BLENDMODE_BLEND);
    return &(texts_[key] = ct);
}

// y は文字の縦の中心。戻り値は描いた幅
inline int SelectRanking::drawText(SDL_Renderer* r, TTF_Font* font, const std::string& s, SDL_Color c,
                                   int x, int y, Align align, Uint8 alpha){
    const CachedText* t = text(r, font, s, c);
    if(!t) return 0;
    if(align == Align::Center) x -= t->w / 2;
    if(align == Align::Right)  x -= t->w;
    SDL_SetTextureAlphaMod(t->tex, alpha);   // キャッシュを共有しているので毎回設定する
    SDL_Rect dst = {x, y - t->h / 2, t->w, t->h};
    SDL_RenderCopy(r, t->tex, nullptr, &dst);
    return t->w;
}

// 選曲画面の他のウィンドウと同じ見た目（半透明の塗り＋枠線＋左上のタブ）
inline void SelectRanking::drawPanel(SDL_Renderer* rd, const SDL_Rect& rect, const std::string& title,
                                     bool focused, Uint8 alpha){
    const SDL_Color border = focused ? SDL_Color{0, 200, 255, 255} : SDL_Color{90, 90, 120, 255};
    SDL_SetRenderDrawBlendMode(rd, SDL_BLENDMODE_BLEND);

    if(const CachedText* t = text(rd, fontLabel_, title, SDL_Color{15, 15, 25, 255})){
        SDL_Rect tab = {rect.x, rect.y - t->h - 6, t->w + 28, t->h + 6};
        SDL_SetRenderDrawColor(rd, border.r, border.g, border.b, alpha);
        SDL_RenderFillRect(rd, &tab);
        SDL_SetTextureAlphaMod(t->tex, alpha);
        SDL_Rect dst = {tab.x + 14, tab.y + 3, t->w, t->h};
        SDL_RenderCopy(rd, t->tex, nullptr, &dst);
    }

    SDL_SetRenderDrawColor(rd, 10, 10, 20, static_cast<Uint8>(190 * alpha / 255));
    SDL_RenderFillRect(rd, &rect);
    SDL_SetRenderDrawColor(rd, border.r, border.g, border.b, alpha);
    for(int i = 0; i < 3; i++){
        SDL_Rect b = {rect.x + i, rect.y + i, rect.w - i * 2, rect.h - i * 2};
        SDL_RenderDrawRect(rd, &b);
    }
}

inline void SelectRanking::clearTexts(){
    for(auto& kv : texts_) if(kv.second.tex) SDL_DestroyTexture(kv.second.tex);
    texts_.clear();
}