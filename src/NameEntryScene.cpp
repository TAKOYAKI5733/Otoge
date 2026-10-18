// NameEntryScene.cpp
// 「名前入力 → 送信 → ランキング表示」画面。流れは NameEntryScene.h を参照。

#include "NameEntryScene.h"
#include "Play/loadScore.h"   // listDifficulties()

#include <cstdio>
#include <unordered_map>

namespace {

// ===== 画面の配置（論理解像度 1920x1080 基準） =====
constexpr SDL_Rect INPUT_PANEL = {460, 200, 1000, 620};
constexpr SDL_Rect INPUT_BOX   = {560, 560, 800, 90};
constexpr SDL_Rect RANK_PANEL  = {360, 110, 1200, 860};
constexpr int VISIBLE_ROWS = 10;
constexpr int ROW_H        = 62;
constexpr size_t NAME_MAX_CHARS = 10;      // GAS 側の NAME_MAX と揃える（NAME_MAX は Linux の既存マクロと衝突するので別名）
constexpr Uint32 IME_GUARD_MS = 150; // 理屈は handleInputEvent() のコメント

// リザルト画面・選曲画面と同じ色
constexpr SDL_Color PANEL_FILL   = { 10,  10,  20, 190};
constexpr SDL_Color PANEL_BORDER = {  0, 200, 255, 255};
constexpr SDL_Color C_WHITE  = {255, 255, 255, 255};
constexpr SDL_Color C_GRAY   = {150, 150, 170, 255};
constexpr SDL_Color C_YELLOW = {255, 215,   0, 255};
constexpr SDL_Color C_RED    = {230,  90,  90, 255};

// ---------------------------------------------------------------------
// UTF-8 の「文字数」を扱う補助
// std::string::size() はバイト数（日本語1文字=3バイト）なので文字数制限には使えない。
// UTF-8 では 2バイト目以降が 10xxxxxx なので、それ以外のバイトを数えれば文字数になる。
// （GAS 側の Array.from(s).length と同じ数え方 = コードポイント数）
// ---------------------------------------------------------------------
size_t utf8Count(const std::string& s){
    size_t n = 0;
    for(unsigned char c : s) if((c & 0xC0) != 0x80) ++n;
    return n;
}

void utf8PopBack(std::string& s){
    if(s.empty()) return;
    size_t i = s.size() - 1;
    while(i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    s.erase(i);
}

// 先頭から maxChars 文字ぶんだけ取り出す
std::string utf8Head(const std::string& s, size_t maxChars){
    size_t i = 0, n = 0;
    while(i < s.size() && n < maxChars){
        unsigned char c = s[i];
        size_t len = (c < 0x80) ? 1 : ((c >> 5) == 0x6) ? 2 : ((c >> 4) == 0xE) ? 3 : ((c >> 3) == 0x1E) ? 4 : 1;
        i += len;
        ++n;
    }
    return s.substr(0, std::min(i, s.size()));
}

// 前後の半角・全角スペースを除く
std::string trimSpaces(std::string s){
    const std::string zen = "\xE3\x80\x80";  // 全角スペース U+3000
    bool changed = true;
    while(changed && !s.empty()){
        changed = false;
        if(s.front() == ' '){ s.erase(0, 1); changed = true; }
        else if(s.compare(0, zen.size(), zen) == 0){ s.erase(0, zen.size()); changed = true; }
        if(!s.empty() && s.back() == ' '){ s.pop_back(); changed = true; }
        else if(s.size() >= zen.size() && s.compare(s.size() - zen.size(), zen.size(), zen) == 0){
            s.erase(s.size() - zen.size()); changed = true;
        }
    }
    return s;
}

std::string withCommas(int v){
    std::string s = std::to_string(v);
    for(int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
    return s;
}

// ---------------------------------------------------------------------
// 文字テクスチャのキャッシュ
// TTF_RenderUTF8 → テクスチャ作成 は重い。ランキング50行を毎フレーム作り直すと CPU を食うので、
// 「フォント＋色＋文字列」をキーにして一度作ったテクスチャを使い回す。
// ---------------------------------------------------------------------
struct TextCache{
    struct Item{ SDL_Texture* tex = nullptr; int w = 0, h = 0; };
    SDL_Renderer* renderer = nullptr;
    std::unordered_map<std::string, Item> items;

    ~TextCache(){ clear(); }
    void clear(){
        for(auto& kv : items) if(kv.second.tex) SDL_DestroyTexture(kv.second.tex);
        items.clear();
    }

    const Item* get(TTF_Font* font, const std::string& s, SDL_Color c){
        if(!font || s.empty()) return nullptr;   // TTF は空文字列を描けない
        char head[48];
        std::snprintf(head, sizeof(head), "%p|%02x%02x%02x|", static_cast<void*>(font), c.r, c.g, c.b);
        std::string key = head + s;
        auto it = items.find(key);
        if(it != items.end()) return &it->second;

        if(items.size() > 400) clear();   // 入力中は文字列が増え続けるので上限を設ける

        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, s.c_str(), c);
        if(!surf) return nullptr;
        Item item;
        item.tex = SDL_CreateTextureFromSurface(renderer, surf);
        item.w = surf->w;
        item.h = surf->h;
        SDL_FreeSurface(surf);
        if(!item.tex) return nullptr;
        SDL_SetTextureBlendMode(item.tex, SDL_BLENDMODE_BLEND);
        return &(items[key] = item);
    }
};

enum class Align{ Left, Center, Right };

// y は文字の縦の中心（ResultScene の drawText と同じ決まり）。戻り値は描いた幅
int drawText(TextCache& cache, TTF_Font* font, const std::string& s, SDL_Color c, int x, int y, Align align){
    const TextCache::Item* t = cache.get(font, s, c);
    if(!t) return 0;
    if(align == Align::Center) x -= t->w / 2;
    if(align == Align::Right)  x -= t->w;
    SDL_Rect dst = {x, y - t->h / 2, t->w, t->h};
    SDL_RenderCopy(cache.renderer, t->tex, nullptr, &dst);
    return t->w;
}

// リザルト画面と同じ見た目のウィンドウ（半透明の塗り＋枠線＋左上のタイトルタブ）
void drawPanel(TextCache& cache, TTF_Font* tabFont, const SDL_Rect& r, const char* title){
    SDL_Renderer* rd = cache.renderer;
    if(const TextCache::Item* t = cache.get(tabFont, title, SDL_Color{15, 15, 25, 255})){
        SDL_Rect tab = {r.x, r.y - t->h - 6, t->w + 28, t->h + 6};
        SDL_SetRenderDrawColor(rd, PANEL_BORDER.r, PANEL_BORDER.g, PANEL_BORDER.b, 255);
        SDL_RenderFillRect(rd, &tab);
        SDL_Rect dst = {tab.x + 14, tab.y + 3, t->w, t->h};
        SDL_RenderCopy(rd, t->tex, nullptr, &dst);
    }
    SDL_SetRenderDrawColor(rd, PANEL_FILL.r, PANEL_FILL.g, PANEL_FILL.b, PANEL_FILL.a);
    SDL_RenderFillRect(rd, &r);
    SDL_SetRenderDrawColor(rd, PANEL_BORDER.r, PANEL_BORDER.g, PANEL_BORDER.b, 255);
    for(int i = 0; i < 3; i++){
        SDL_Rect b = {r.x + i, r.y + i, r.w - i * 2, r.h - i * 2};
        SDL_RenderDrawRect(rd, &b);
    }
}

enum class Phase{ Input, Sending, Result };

} // namespace

// =====================================================================
ScoreRecord makeScoreRecord(const std::string& scorePath, int difficultyIndex, const ResultData& result){
    ScoreRecord rec;

    // パスの区切りは Windows だと '\' になることがあるので、filesystem で分解して OS の差をなくす
    std::filesystem::path p = toUtf8Path(scorePath);
    std::u8string folder = p.parent_path().filename().u8string();
    std::u8string stem   = p.stem().u8string();
    rec.songId.assign(reinterpret_cast<const char*>(folder.data()), folder.size());
    rec.songId += "/";
    rec.songId.append(reinterpret_cast<const char*>(stem.data()), stem.size());

    // 難易度は番号ではなく名前で送る（譜面に難易度が追加されて番号がずれても記録が混ざらない）
    std::vector<DifficultyInfo> diffs = listDifficulties(scorePath);
    if(difficultyIndex >= 0 && difficultyIndex < static_cast<int>(diffs.size())){
        rec.difficulty = diffs[difficultyIndex].name;
    }
    else{
        rec.difficulty = "DEFAULT";
    }

    rec.score = result.score;
    rec.maxCombo = result.maxCombo;
    return rec;
}

// =====================================================================
GameScene nameEntryScene(SDL_Window* window, SDL_Renderer* renderer, const ScoreRecord& record){
    TTF_Font* fontLarge = TTF_OpenFont("fonts/prac.ttf", 56);
    TTF_Font* fontMed   = TTF_OpenFont("fonts/prac.ttf", 40);
    TTF_Font* fontSmall = TTF_OpenFont("fonts/prac.ttf", 32);
    TTF_Font* fontTab   = TTF_OpenFont("fonts/prac.ttf", 24);

    auto closeFonts = [&](){
        for(TTF_Font* f : {fontLarge, fontMed, fontSmall, fontTab}) if(f) TTF_CloseFont(f);
    };
    if(!fontLarge || !fontMed || !fontSmall || !fontTab){
        std::cout << "[ERROR] NameEntrySceneフォント読込失敗\n";
        closeFonts();
        return GameScene::Select;
    }

    TextCache cache;
    cache.renderer = renderer;

    ScoreRecord rec = record;
    Phase phase = Phase::Input;
    std::string name;          // 確定済みの文字
    std::string composition;   // IME で変換中の文字（まだ確定していない）
    std::string hint;          // 「10文字までです」などの注意
    Uint32 lastImeEventAt = 0;
    SyncResult result;
    int scroll = 0;
    GameScene nextScene = GameScene::Select;
    bool running = true;

    // 元々の文字入力の状態を覚えておき、最後に戻す（他の画面の動きを変えないため）
    const bool textInputWasActive = SDL_IsTextInputActive();
    SDL_StartTextInput();   // これで SDL_TEXTINPUT / SDL_TEXTEDITING が届くようになる

    // IME の変換候補ウィンドウを入力枠の下に出す。
    // SDL_SetTextInputRect は「ウィンドウの実際のピクセル座標」を受け取るが、この画面は
    // 論理解像度 1920x1080 で描いているので、ウィンドウ座標に変換してから渡す。
    auto updateImeRect = [&](){
        int wx0, wy0, wx1, wy1;   // SDL_RenderLogicalToWindow（SDL 2.0.18 以降）は int で返す
        SDL_RenderLogicalToWindow(renderer, static_cast<float>(INPUT_BOX.x), static_cast<float>(INPUT_BOX.y), &wx0, &wy0);
        SDL_RenderLogicalToWindow(renderer, static_cast<float>(INPUT_BOX.x + INPUT_BOX.w),
                                  static_cast<float>(INPUT_BOX.y + INPUT_BOX.h), &wx1, &wy1);
        SDL_Rect r = {wx0, wy0, wx1 - wx0, wy1 - wy0};
        SDL_SetTextInputRect(&r);
    };
    updateImeRect();

    // 文字を足す（10文字を超える分は切り捨て）
    auto appendText = [&](const char* utf8){
        const size_t have = utf8Count(name);
        if(have >= NAME_MAX_CHARS){ hint = "10文字までです"; return; }
        std::string add = utf8Head(utf8, NAME_MAX_CHARS - have);
        hint = (utf8Count(utf8) > utf8Count(add)) ? "10文字までです" : "";
        name += add;
    };

    // 名前を決定して送信する
    auto confirm = [&](){
        std::string n = trimSpaces(name);
        if(n.empty()){ hint = "名前を入力してください"; return; }
        rec.name = n;
        SDL_StopTextInput();
        ScoreSync::instance().submit(rec);   // すぐ戻る。結果は下の poll() で受け取る
        phase = Phase::Sending;
    };

    SDL_Event e;
    while(running){
        const Uint32 now = SDL_GetTicks();

        while(SDL_PollEvent(&e) != 0){
            if(e.type == SDL_QUIT){
                // 送信中でも大丈夫：通信が途中で止まった記録は未送信キューに残り、次回起動時に送られる
                nextScene = GameScene::Shutdown;
                running = false;
                break;
            }
            if(e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED){
                updateImeRect();   // ウィンドウサイズが変わると座標の変換結果も変わる
            }

            if(phase == Phase::Input){
                // ■ IME と Enter キーの問題
                //   日本語入力では「変換を確定する Enter」でも SDL_KEYDOWN(Enter) が届く環境がある。
                //   それを「名前の決定」として扱うと、変換を確定した瞬間に送信されてしまう。
                //   そこで Enter を受けたとき、
                //     (1) 変換中の文字が残っている              → IME の Enter なので無視
                //     (2) 直近 150ms 以内に IME のイベントがあった → IME の Enter なので無視
                //   とする。KEYDOWN と確定イベントのどちらが先に届いても、(1)か(2)のどちらかで弾ける。
                //   Esc（変換の取り消しにも使う）も同じ理屈で守る。
                switch(e.type){
                    case SDL_TEXTINPUT:      // 確定した文字（IME を使わない英数字もここに来る）
                        if(!composition.empty()) lastImeEventAt = now;
                        composition.clear();
                        appendText(e.text.text);
                        break;
                    case SDL_TEXTEDITING:    // 変換中の文字。確定・取り消し時は空文字で届く
                        composition = e.edit.text;
                        lastImeEventAt = now;
                        break;
                    case SDL_KEYDOWN:{
                        const bool imeBusy = !composition.empty() || (now - lastImeEventAt < IME_GUARD_MS);
                        const SDL_Keycode k = e.key.keysym.sym;
                        if(k == SDLK_BACKSPACE && composition.empty()){
                            utf8PopBack(name);
                            hint.clear();
                        }
                        else if((k == SDLK_RETURN || k == SDLK_KP_ENTER) && !e.key.repeat && !imeBusy){
                            confirm();
                        }
                        else if(k == SDLK_ESCAPE && !e.key.repeat && !imeBusy){
                            running = false;   // 送信しないで選曲画面へ
                        }
                        break;
                    }
                    default: break;
                }
            }
            else if(phase == Phase::Result && e.type == SDL_KEYDOWN){
                const int maxScroll = std::max(0, static_cast<int>(result.entries.size()) - VISIBLE_ROWS);
                switch(e.key.keysym.sym){
                    case SDLK_UP:   scroll = std::max(0, scroll - 1); break;
                    case SDLK_DOWN: scroll = std::min(maxScroll, scroll + 1); break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                    case SDLK_SPACE:
                        if(!e.key.repeat) running = false;
                        break;
                    default: break;
                }
            }
        }
        if(!running) break;

        // ----- 送信結果を受け取る -----
        if(phase == Phase::Sending){
            SyncResult r;
            // ※ ここでは Submit 以外の結果は読み捨てる。今は ScoreSync を使うのがこの画面だけなので問題ない
            while(ScoreSync::instance().poll(r)){
                if(r.kind != SyncResult::Kind::Submit) continue;
                result = std::move(r);
                phase = Phase::Result;
                // 自分が50位以内なら、自分の行が真ん中あたりに来るようにスクロールしておく
                const int maxScroll = std::max(0, static_cast<int>(result.entries.size()) - VISIBLE_ROWS);
                if(result.myRank >= 1 && result.myRank <= static_cast<int>(result.entries.size())){
                    scroll = std::clamp(result.myRank - 1 - VISIBLE_ROWS / 2, 0, maxScroll);
                }
                break;
            }
        }

        // ----- 描画 -----
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 10, 10, 20, 255);
        SDL_RenderClear(renderer);

        const std::string songLine = rec.songId + "  [" + rec.difficulty + "]";
        const int cx = SCREEN_W / 2;

        if(phase == Phase::Input || phase == Phase::Sending){
            const SDL_Rect& P = INPUT_PANEL;
            drawPanel(cache, fontTab, P, "NAME ENTRY");
            drawText(cache, fontMed, songLine, C_GRAY, cx, P.y + 70, Align::Center);
            char scoreBuf[16];
            std::snprintf(scoreBuf, sizeof(scoreBuf), "%07d", rec.score);
            drawText(cache, fontLarge, std::string("SCORE ") + scoreBuf, C_WHITE, cx, P.y + 150, Align::Center);
            drawText(cache, fontMed, "MAX COMBO " + std::to_string(rec.maxCombo), C_WHITE, cx, P.y + 220, Align::Center);

            if(phase == Phase::Input){
                drawText(cache, fontMed, "名前を入力してください（10文字まで）", C_WHITE, cx, P.y + 310, Align::Center);

                // 入力枠
                SDL_SetRenderDrawColor(renderer, 25, 25, 45, 255);
                SDL_RenderFillRect(renderer, &INPUT_BOX);
                SDL_SetRenderDrawColor(renderer, PANEL_BORDER.r, PANEL_BORDER.g, PANEL_BORDER.b, 255);
                SDL_RenderDrawRect(renderer, &INPUT_BOX);

                // 確定済み（白）→ 変換中（黄色＋下線）→ カーソル の順に横へ並べる
                const int textY = INPUT_BOX.y + INPUT_BOX.h / 2;
                int x = INPUT_BOX.x + 24;
                x += drawText(cache, fontLarge, name, C_WHITE, x, textY, Align::Left);
                const int compW = drawText(cache, fontLarge, composition, C_YELLOW, x, textY, Align::Left);
                if(compW > 0){
                    SDL_SetRenderDrawColor(renderer, C_YELLOW.r, C_YELLOW.g, C_YELLOW.b, 255);
                    SDL_Rect underline = {x, textY + 30, compW, 3};
                    SDL_RenderFillRect(renderer, &underline);
                    x += compW;
                }
                if((now / 500) % 2 == 0){   // 0.5秒ごとに点滅するカーソル
                    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                    SDL_Rect cursor = {x + 4, textY - 30, 3, 60};
                    SDL_RenderFillRect(renderer, &cursor);
                }

                drawText(cache, fontSmall, std::to_string(utf8Count(name)) + " / 10", C_GRAY,
                         INPUT_BOX.x + INPUT_BOX.w, INPUT_BOX.y + INPUT_BOX.h + 30, Align::Right);
                if(!hint.empty()){
                    drawText(cache, fontSmall, hint, C_RED, cx, INPUT_BOX.y + INPUT_BOX.h + 30, Align::Center);
                }
                drawText(cache, fontSmall, "ENTER: 決定　　ESC: 送信しない", C_GRAY, cx, SCREEN_H - 40, Align::Center);
            }
            else{
                std::string dots((now / 400) % 4, '.');
                drawText(cache, fontLarge, "送信中" + dots, C_WHITE, cx, INPUT_BOX.y + INPUT_BOX.h / 2, Align::Center);
            }
        }
        else{   // Phase::Result
            const SDL_Rect& P = RANK_PANEL;
            drawPanel(cache, fontTab, P, "RANKING");
            drawText(cache, fontMed, songLine, C_GRAY, cx, P.y + 55, Align::Center);

            if(!result.ok){
                if(result.queuedOffline){
                    drawText(cache, fontMed, "通信できませんでした。", C_WHITE, cx, P.y + 360, Align::Center);
                    drawText(cache, fontMed, "記録は保存し、つながったら自動で送信します。", C_WHITE, cx, P.y + 420, Align::Center);
                    drawText(cache, fontSmall, result.error, C_GRAY, cx, P.y + 500, Align::Center);
                }
                else{
                    drawText(cache, fontMed, "送信できませんでした", C_RED, cx, P.y + 360, Align::Center);
                    drawText(cache, fontSmall, result.error, C_GRAY, cx, P.y + 420, Align::Center);
                }
            }
            else{
                std::string rankText = result.myRank > 0
                    ? "あなたの順位  " + std::to_string(result.myRank) + "位 / " + std::to_string(result.total) + "件"
                    : "あなたの順位  -";
                drawText(cache, fontLarge, rankText, C_YELLOW, cx, P.y + 125, Align::Center);

                // 表の列（右寄せの列は右端の x を指定）
                const int colRank  = P.x + 150;
                const int colName  = P.x + 210;
                const int colScore = P.x + 900;
                const int colCombo = P.x + P.w - 60;
                int y = P.y + 210;

                drawText(cache, fontSmall, "順位", C_GRAY, colRank, y, Align::Right);
                drawText(cache, fontSmall, "名前", C_GRAY, colName, y, Align::Left);
                drawText(cache, fontSmall, "スコア", C_GRAY, colScore, y, Align::Right);
                drawText(cache, fontSmall, "MAXコンボ", C_GRAY, colCombo, y, Align::Right);
                SDL_SetRenderDrawColor(renderer, 110, 110, 150, 255);
                SDL_Rect line = {P.x + 40, y + 28, P.w - 80, 2};
                SDL_RenderFillRect(renderer, &line);
                y += ROW_H;

                const int endIdx = std::min(static_cast<int>(result.entries.size()), scroll + VISIBLE_ROWS);
                for(int i = scroll; i < endIdx; i++){
                    const RankEntry& en = result.entries[i];
                    if(en.isMine){   // 自分の記録は帯で強調
                        SDL_SetRenderDrawColor(renderer, 255, 215, 0, 45);
                        SDL_Rect band = {P.x + 40, y - ROW_H / 2 + 4, P.w - 80, ROW_H - 8};
                        SDL_RenderFillRect(renderer, &band);
                    }
                    const SDL_Color c = en.isMine ? C_YELLOW : C_WHITE;
                    drawText(cache, fontMed, std::to_string(en.rank), c, colRank, y, Align::Right);
                    drawText(cache, fontMed, en.name, c, colName, y, Align::Left);
                    drawText(cache, fontMed, withCommas(en.score), c, colScore, y, Align::Right);
                    drawText(cache, fontMed, std::to_string(en.maxCombo), c, colCombo, y, Align::Right);
                    y += ROW_H;
                }

                if(result.entries.size() > static_cast<size_t>(VISIBLE_ROWS)){
                    drawText(cache, fontSmall,
                             std::to_string(scroll + 1) + "〜" + std::to_string(endIdx) + "位 / 上位" +
                             std::to_string(result.entries.size()) + "件",
                             C_GRAY, cx, P.y + P.h - 35, Align::Center);
                }
            }
            drawText(cache, fontSmall, "↑↓: スクロール　　ENTER: 選曲へ", C_GRAY, cx, SCREEN_H - 40, Align::Center);
        }

        SDL_RenderPresent(renderer);
        g_framePacer.endFrame(window);   // 待たずに回り続けて CPU を使い切らないように
    }

    if(textInputWasActive) SDL_StartTextInput();
    else SDL_StopTextInput();
    cache.clear();      // テクスチャはフォントより先に消す
    closeFonts();
    return nextScene;
}