// =====================================================================
//  MenuScene.cpp — メニュー画面（タイトル画面の次）
//
//  ■ 出てくる順番（タイトル画面が暗くなって終わったところから始まる）
//    0ms      ：真っ暗
//    0〜400ms ：背景が黒から選曲画面と同じ土台の色へ
//    200〜650ms：画面の真ん中の「回る台座 ＋ 立方体」（CubeShowcase.h）がグリッチで出てくる（Play/GlitchLayer.h）
//    650〜1280ms：画面の下 3 割のメニューのウィンドウが、Cube から作り出される（QuarryReveal.h）
//                Cube から光の線 → 枠が縦 → 横に広がる（中に薄い斜線）→ 上から下へ中身ができる（先頭はグリッチ）
//    メニューの操作は、ウィンドウが出始めてから受け付ける
//
//  ■ メニュー（←→ で選ぶ・ENTER で決定。マウスは乗せると選び、クリックで決定）
//    PLAY   ：選曲画面へ
//    SHOP   ：Cube などの通貨で取引する（今はウィンドウだけ）
//    STORY  ：Cube でストーリーを開放・閲覧する（今はウィンドウだけ）
//    CREATE ：譜面作成画面へ（新しい譜面）
//    SETTING：選曲画面と同じ設定ウィンドウ（Settingspanel.h）を開く
//    ESC    ：タイトル画面へ戻る
//
//  ■ ほかの画面とのつなぎ（台座と立方体は、どの画面でも同じ位置・同じ回り方のまま）
//    ・PLAY（→ 選曲画面）：メニューのウィンドウが下へ下がって消える。選曲画面ではジャンルのウィンドウが Cube から作り出され、
//                         背景が土台の色から選んでいる曲のぼかしへなめらかに変わる（SelectScene.cpp）
//    ・選曲画面から戻ってきたとき：ジャンルのウィンドウがグリッチで消えた後（SelectScene.cpp）、
//                         こちらではメニューのウィンドウが Cube から作り出される（立方体のグリッチや暗転はしない）
//    ・ESC（→ タイトル画面）：立方体がグリッチで消え、メニューも薄くなって消えてから、画面全体が暗くなってタイトルへ
//    どちらの画面から来たかは g_sceneHandoff（CubeShowcase.h）で受け渡す
//
//  ■ 選んでいる項目は、少し上へ出っ張る（位置はなめらかに動く：スライド）
//  ■ 真ん中の立方体は、選曲画面と同じくクリックで速く回る（0.3 秒以内の 20 連打で十二面体になるのも同じ）
// =====================================================================

#include "GameCommon.h"
#include "Play/PlaySceneManager.h"
#include "Play/GlitchLayer.h"
#include "CubeShowcase.h"
#include "QuarryReveal.h"        // ウィンドウを Cube から作り出す出方（光の線 → 枠 → 上から生成）
#include "Settingspanel.h"
#include "HoloWindow.h"         // ウィンドウの見た目（Cube の色のホログラム・走査線・たまにグリッチ）
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

namespace menuscene{

// ----- 調整用 -----
constexpr SDL_Color BASE_COLOR     = {15, 15, 25, 255};    // 背景（選曲画面の土台と同じ色）
constexpr double BG_FADE_MS        = 400.0;                // 背景が黒から土台の色になるまで
constexpr double SHOWCASE_START_MS = 200.0;                // 台座と立方体のグリッチが始まる時刻
constexpr double SHOWCASE_GLITCH_MS = 450.0;               //   その長さ
constexpr double MENU_START_MS     = 650.0;                // メニューのウィンドウが出始める時刻
//   メニューのウィンドウ・SHOP・STORY のウィンドウの出方の長さは QuarryReveal::TOTAL_MS（QuarryReveal.h）
constexpr double MENU_DROP_MS      = 320.0;                // 選曲画面へ行くとき：ウィンドウが下へ下がって消える長さ
constexpr double CUBE_OUT_MS       = 400.0;                // タイトルへ戻るとき：立方体がグリッチで消える長さ
constexpr double TITLE_FADE_MS     = 400.0;                //   その後、画面全体が暗くなる長さ

// メニューのウィンドウ：画面の下 3 割
constexpr SDL_Rect MENU_WIN = {160, 760, 1600, 280};
constexpr float  MENU_OFFSCREEN_DY = static_cast<float>(SCREEN_H - MENU_WIN.y + 60);   // ウィンドウを画面の下へ隠すときのずらし量
constexpr int  CARD_GAP     = 24;     // 項目どうしのすき間
constexpr int  CARD_PAD_X   = 40;     // ウィンドウの左右の端から項目まで
constexpr int  CARD_TOP     = 70;     // ウィンドウの上の端から項目の上まで
constexpr int  CARD_H       = 170;
constexpr double CARD_JUT   = 16.0;   // 選んでいる項目を上へ出す量(px)
constexpr double CARD_EASE  = 0.2;    // 出っ張りのなめらかさ（1 フレームで近づく割合）

enum class Item{ Play, Shop, Story, Create, Setting };
struct MenuItem{
    Item kind;
    const char* title;   // 英語の大きい名前
    const char* sub;     // 日本語の小さい説明
};
const MenuItem ITEMS[] = {
    {Item::Play,    "PLAY",    "曲を選んで遊ぶ"},
    {Item::Shop,    "SHOP",    "Cube で取引する"},
    {Item::Story,   "STORY",   "ストーリーを開放・閲覧"},
    {Item::Create,  "CREATE",  "譜面を作る"},
    {Item::Setting, "SETTING", "設定"},
};
constexpr int ITEM_COUNT = static_cast<int>(sizeof(ITEMS) / sizeof(ITEMS[0]));

// (cx, cy) を中心に文字を描く
inline void drawText(SDL_Renderer* r, TTF_Font* font, const std::string& text, SDL_Color c, int cx, int cy){
    if(!font || text.empty()) return;
    SDL_Surface* s = TTF_RenderUTF8_Blended(font, text.c_str(), c);
    if(!s) return;
    if(SDL_Texture* t = SDL_CreateTextureFromSurface(r, s)){
        SDL_Rect dst = {cx - s->w / 2, cy - s->h / 2, s->w, s->h};
        SDL_RenderCopy(r, t, nullptr, &dst);
        SDL_DestroyTexture(t);
    }
    SDL_FreeSurface(s);
}

// ウィンドウの上の「タブ」（HoloWindow.h のホログラムのタブ。選曲画面のウィンドウと同じ見た目）
inline void drawTab(SDL_Renderer* r, TTF_Font* font, const char* title, const SDL_Rect& win){
    drawHoloWindowTab(r, font, title, win, 255);
}

// 項目 i の四角（出っ張り jut を引いた位置）
inline SDL_Rect cardRect(int i, double jut){
    const int cardW = (MENU_WIN.w - CARD_PAD_X * 2 - CARD_GAP * (ITEM_COUNT - 1)) / ITEM_COUNT;
    return SDL_Rect{MENU_WIN.x + CARD_PAD_X + i * (cardW + CARD_GAP),
                    MENU_WIN.y + CARD_TOP - static_cast<int>(std::lround(jut)), cardW, CARD_H};
}

// 透明な絵を作る（premultiplied で中身が入る前提の混ぜ方にしておく）
inline SDL_Texture* makeLayer(SDL_Renderer* r){
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);
    if(t){
        const SDL_BlendMode pm = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
            SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
        // この混ぜ方が使えない描画方式では、ふつうの半透明の混ぜ方にする（半透明の所が少し暗くなるだけ）
        if(SDL_SetTextureBlendMode(t, pm) != 0) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    }
    return t;
}

// layer を透明にして、描く先にする（戻すときは SDL_SetRenderTarget(r, prev)）
inline SDL_Texture* beginLayer(SDL_Renderer* r, SDL_Texture* layer){
    SDL_Texture* prev = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, layer);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    return prev;
}

} // namespace menuscene

GameScene menuScene(SDL_Window* window, SDL_Renderer* renderer, PlayerSettings& playerSettings, std::string& outSelectedScorePath){
    using namespace menuscene;

    TTF_Font* titleFont = TTF_OpenFont("fonts/prac.ttf", 52);
    TTF_Font* font      = TTF_OpenFont("fonts/prac.ttf", 40);
    TTF_Font* labelFont = TTF_OpenFont("fonts/prac.ttf", 24);

    SettingsPanel settings;
    settings.init(playerSettings);

    SDL_Texture* showcaseLayer = makeLayer(renderer);   // 台座と立方体のグリッチ用
    SDL_Texture* menuLayer     = makeLayer(renderer);   // メニューのウィンドウの出方（Cube から作り出す）・下がる・薄くなる用
    SDL_Texture* popupLayer    = makeLayer(renderer);   // SHOP・STORY のウィンドウの出方（Cube から作り出す）用

    int cursor = 0;
    double jut[ITEM_COUNT] = {};          // 項目ごとの今の出っ張り
    int popup = -1;                       // 開いている SHOP・STORY のウィンドウ（項目の番号。-1 = 無し）
    Uint32 popupStart = 0;

    const Uint32 enterTick = SDL_GetTicks();
    GameScene nextScene = GameScene::Menu;
    bool running = true;

    // 選曲画面から戻ってきたか（そのときは暗転・立方体のグリッチをせず、ウィンドウだけ下から上げる）
    const bool fromSelect = (g_sceneHandoff == SceneHandoff::SelectToMenu);
    g_sceneHandoff = SceneHandoff::None;

    // 画面を出ていくときの動き：0 = まだ、1 = 選曲画面へ（ウィンドウが下がる）、2 = タイトルへ（立方体が消えて暗転）
    int exitMode = 0;
    Uint32 exitStart = 0;
    auto beginExit = [&](int mode){
        if(exitMode != 0) return;
        exitMode = mode;
        exitStart = SDL_GetTicks();
    };

    auto decide = [&](int i){
        switch(ITEMS[i].kind){
            case Item::Play:
                beginExit(1);                     // ウィンドウが下がってから選曲画面へ
                break;
            case Item::Create:
                outSelectedScorePath = "";        // 新しい譜面を作る
                nextScene = GameScene::ChartCreate;
                running = false;
                break;
            case Item::Setting:
                settings.open(playerSettings);
                break;
            case Item::Shop:
            case Item::Story:
                popup = i;                        // 今はウィンドウだけ
                popupStart = SDL_GetTicks();
                break;
        }
    };

    while(running){
        const double el = static_cast<double>(SDL_GetTicks() - enterTick);
        const double exitEl = (exitMode != 0) ? static_cast<double>(SDL_GetTicks() - exitStart) : 0.0;
        // メニューの操作を受け付けるか（出ていく途中は受け付けない）
        const bool menuReady = (exitMode == 0) && (fromSelect || el >= MENU_START_MS);

        // 出ていく動きが終わったら、次の画面へ
        if(exitMode == 1 && exitEl >= MENU_DROP_MS){
            g_sceneHandoff = SceneHandoff::MenuToSelect;   // 選曲画面で、ジャンルのウィンドウを Cube から作り出す
            nextScene = GameScene::Select;
            break;
        }
        if(exitMode == 2 && exitEl >= CUBE_OUT_MS + TITLE_FADE_MS){
            nextScene = GameScene::Title;
            break;
        }

        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type == SDL_QUIT){
                nextScene = GameScene::Shutdown;
                running = false;
                break;
            }

            if(exitMode != 0) continue;   // 出ていく途中は、ウィンドウを閉じる以外の操作は受け付けない

            // 設定ウィンドウを開いているとき：キーは全部そちらへ（選曲画面と同じ）
            if(settings.isOpen()){
                if(e.type == SDL_KEYDOWN){
                    settings.handleKey(e.key, window, renderer, playerSettings);
                    if(settings.consumeCalibrationRequest()){ nextScene = GameScene::Calibration; running = false; }
                    if(settings.consumeKeyConfigRequest())  { nextScene = GameScene::KeyConfig;   running = false; }
                    if(settings.consumeNoteSettingRequest()){ nextScene = GameScene::NoteSetting; running = false; }
                }
                continue;
            }

            // SHOP・STORY のウィンドウ：ESC / ENTER / クリックで閉じる
            if(popup >= 0){
                if((e.type == SDL_KEYDOWN && !e.key.repeat &&
                    (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER))
                   || (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)){
                    popup = -1;
                }
                continue;
            }

            if(e.type == SDL_KEYDOWN){
                switch(e.key.keysym.sym){
                    case SDLK_ESCAPE:
                        if(!e.key.repeat) beginExit(2);   // 立方体が消えて暗くなってからタイトルへ
                        break;
                    case SDLK_LEFT:
                        if(menuReady) cursor = (cursor + ITEM_COUNT - 1) % ITEM_COUNT;
                        break;
                    case SDLK_RIGHT:
                        if(menuReady) cursor = (cursor + 1) % ITEM_COUNT;
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        if(menuReady && !e.key.repeat) decide(cursor);
                        break;
                    default:
                        break;
                }
            }
            else if(e.type == SDL_MOUSEMOTION && menuReady){
                for(int i = 0; i < ITEM_COUNT; i++){
                    const SDL_Rect r = cardRect(i, jut[i]);
                    if(e.motion.x >= r.x && e.motion.x < r.x + r.w && e.motion.y >= r.y && e.motion.y < r.y + r.h) cursor = i;
                }
            }
            else if(e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT){
                const int mx = e.button.x, my = e.button.y;
                if(hitShowcaseCube(mx, my)){
                    countShowcaseClick();   // 選曲画面と同じ：速く回る・20 連打で十二面体
                    g_showcase.boost = std::min(SPIN_BOOST_MAX, g_showcase.boost + SPIN_BOOST_ADD);
                }
                else if(menuReady){
                    for(int i = 0; i < ITEM_COUNT; i++){
                        const SDL_Rect r = cardRect(i, jut[i]);
                        if(mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h){
                            cursor = i;
                            decide(i);
                            break;
                        }
                    }
                }
            }
        }
        if(!running) break;

        // 選んでいる項目の出っ張り：なめらかに近づける
        for(int i = 0; i < ITEM_COUNT; i++){
            const double target = (i == cursor) ? CARD_JUT : 0.0;
            jut[i] += (target - jut[i]) * CARD_EASE;
        }

        // ===== 1. 背景：黒 → 土台の色 =====
        const double bgT = fromSelect ? 1.0 : std::clamp(el / BG_FADE_MS, 0.0, 1.0);   // 選曲画面から戻ったときは最初から土台の色
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(BASE_COLOR.r * bgT), static_cast<Uint8>(BASE_COLOR.g * bgT),
                               static_cast<Uint8>(BASE_COLOR.b * bgT), 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        // ===== 2. 台座と立方体：グリッチで出てくる（タイトルへ戻るときはグリッチで消える）=====
        if(fromSelect || el >= SHOWCASE_START_MS){
            double p = fromSelect ? 1.0 : (el - SHOWCASE_START_MS) / SHOWCASE_GLITCH_MS;   // 1 以上 = 出きっている
            if(exitMode == 2) p = 1.0 - exitEl / CUBE_OUT_MS;                               // 1 → 0 で消える
            if(p <= 0.0){
                // 消えきった：何も描かない
            }
            else if(p < 1.0 && showcaseLayer){
                SDL_Texture* prev = beginLayer(renderer, showcaseLayer);
                draw_CubeShowcase(renderer);
                SDL_SetRenderTarget(renderer, prev);
                // 範囲：台座と立方体のまわり（回っても入る大きさ）
                const SDL_Rect area = {static_cast<int>(SCREEN_W * 0.5f - 220), static_cast<int>(PEDESTAL_Y - 360), 440, 440};
                drawGlitchTexture(renderer, showcaseLayer, area, p);
            }
            else{
                draw_CubeShowcase(renderer);
            }
        }

        // ===== 3. メニューのウィンドウ：Cube から作り出される・下がって消える・薄くなって消える =====
        auto drawMenuWindow = [&](){
            drawTab(renderer, labelFont, "MENU", MENU_WIN);
            drawHoloWindow(renderer, MENU_WIN, 1.0, true, 3, 0x3E40u);
            for(int i = 0; i < ITEM_COUNT; i++){
                const bool sel = (i == cursor);
                const SDL_Rect r = cardRect(i, jut[i]);
                drawHoloWindow(renderer, r, 1.0, sel, sel ? 3 : 1, 0x3E50u + static_cast<uint32_t>(i));
                drawText(renderer, titleFont, ITEMS[i].title,
                         sel ? SDL_Color{0, 255, 255, 255} : SDL_Color{235, 235, 245, 255}, r.x + r.w / 2, r.y + r.h / 2 - 22);
                drawText(renderer, labelFont, ITEMS[i].sub, SDL_Color{160, 170, 190, 255}, r.x + r.w / 2, r.y + r.h / 2 + 38);
            }
            drawText(renderer, labelFont, "[←→]:選ぶ   [ENTER]:決定   [ESC]:タイトルへ", SDL_Color{150, 150, 170, 255},
                     MENU_WIN.x + MENU_WIN.w / 2, MENU_WIN.y + MENU_WIN.h - 24);
        };

        // ウィンドウを、ずらし（dy）・大きさ（scale：ウィンドウの真ん中が中心）・濃さ（a）を付けて描く
        auto drawMenuWindowAt = [&](float dy, float scale, Uint8 a){
            if(dy == 0.0f && scale == 1.0f && a == 255){
                drawMenuWindow();
                return;
            }
            if(!menuLayer) return;
            SDL_Texture* prev = beginLayer(renderer, menuLayer);
            drawMenuWindow();
            SDL_SetRenderTarget(renderer, prev);
            const float cx = MENU_WIN.x + MENU_WIN.w * 0.5f, cy = MENU_WIN.y + MENU_WIN.h * 0.5f;
            //   画面全体の絵を (cx, cy) を中心に scale 倍して、dy だけ下へずらす：左上は (cx − cx × scale, cy − cy × scale + dy)
            const SDL_FRect dst = {cx - cx * scale, cy - cy * scale + dy, SCREEN_W * scale, SCREEN_H * scale};
            SDL_SetTextureColorMod(menuLayer, a, a, a);   // premultiplied なので、色も同じだけ下げる
            SDL_SetTextureAlphaMod(menuLayer, a);
            SDL_RenderCopyF(renderer, menuLayer, nullptr, &dst);
            SDL_SetTextureColorMod(menuLayer, 255, 255, 255);
            SDL_SetTextureAlphaMod(menuLayer, 255);
        };

        if(exitMode == 1){
            // 選曲画面へ：下へ下がって消える（だんだん速く）
            const double t = std::clamp(exitEl / MENU_DROP_MS, 0.0, 1.0);
            drawMenuWindowAt(static_cast<float>(t * t * t) * MENU_OFFSCREEN_DY, 1.0f, 255);
        }
        else if(exitMode == 2){
            // タイトルへ：立方体が消えるのと一緒に薄くなって消える
            const double t = std::clamp(exitEl / CUBE_OUT_MS, 0.0, 1.0);
            drawMenuWindowAt(0.0f, 1.0f, static_cast<Uint8>(255.0 * (1.0 - t)));
        }
        else if(fromSelect || el >= MENU_START_MS){
            // 出てくる：Cube から光の線 → 枠（縦 → 横・薄い斜線）→ 上から生成（QuarryReveal.h）
            //   タイトルから来たときは MENU_START_MS から、選曲画面から戻ってきたときはすぐに始める
            const double qms = fromSelect ? el : el - MENU_START_MS;
            if(QuarryReveal::done(qms) || !menuLayer){
                drawMenuWindow();
            }
            else{
                SDL_Texture* prev = beginLayer(renderer, menuLayer);
                drawMenuWindow();
                SDL_SetRenderTarget(renderer, prev);
                float cubeX, cubeY;
                showcaseCubeCenter(cubeX, cubeY);
                const SDL_Rect area = {MENU_WIN.x - 8, MENU_WIN.y - 50, MENU_WIN.w + 16, MENU_WIN.h + 58};   // 上のタブも入れる
                QuarryReveal::draw(renderer, menuLayer, MENU_WIN, area, cubeX, cubeY, qms);
            }
        }

        // ===== 4. SHOP・STORY のウィンドウ（今はウィンドウだけ）=====
        if(popup >= 0){
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 140);
            SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
            SDL_RenderFillRect(renderer, &full);

            const SDL_Rect win = {SCREEN_W / 2 - 420, 300, 840, 360};
            auto drawPopup = [&](){
                drawTab(renderer, labelFont, ITEMS[popup].title, win);
                drawHoloWindow(renderer, win, 1.0, true, 3, 0x3E60u);
                drawText(renderer, titleFont, ITEMS[popup].title, SDL_Color{0, 255, 255, 255}, win.x + win.w / 2, win.y + 90);
                const char* body = (ITEMS[popup].kind == Item::Shop)
                    ? "Cube などの通貨で取引できるようになります" : "Cube でストーリーを開放・閲覧できるようになります";
                drawText(renderer, font, body, SDL_Color{230, 230, 240, 255}, win.x + win.w / 2, win.y + 180);
                drawText(renderer, font, "準備中", SDL_Color{150, 215, 255, 255}, win.x + win.w / 2, win.y + 240);
                drawText(renderer, labelFont, "[ESC / ENTER / クリック]:閉じる", SDL_Color{150, 150, 170, 255},
                         win.x + win.w / 2, win.y + win.h - 30);
            };
            // 出てくる：メニューのウィンドウと同じく、Cube から作り出す（QuarryReveal.h）
            const double qms = static_cast<double>(SDL_GetTicks() - popupStart);
            if(!QuarryReveal::done(qms) && popupLayer){
                SDL_Texture* prev = beginLayer(renderer, popupLayer);
                drawPopup();
                SDL_SetRenderTarget(renderer, prev);
                float cubeX, cubeY;
                showcaseCubeCenter(cubeX, cubeY);
                const SDL_Rect area = {win.x - 8, win.y - 50, win.w + 16, win.h + 58};
                QuarryReveal::draw(renderer, popupLayer, win, area, cubeX, cubeY, qms);
            }
            else{
                drawPopup();
            }
        }

        // ===== 5. 設定ウィンドウ（一番手前）=====
        settings.update();
        settings.draw(renderer, font, labelFont, playerSettings);

        // ===== 6. タイトルへ戻るとき：立方体が消えた後、画面全体を暗くする =====
        if(exitMode == 2 && exitEl > CUBE_OUT_MS){
            const double t = std::clamp((exitEl - CUBE_OUT_MS) / TITLE_FADE_MS, 0.0, 1.0);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<Uint8>(255.0 * t));
            SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
            SDL_RenderFillRect(renderer, &full);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }

    if(settings.isOpen()) settings.close(window, playerSettings);
    settings.destroy();
    if(showcaseLayer) SDL_DestroyTexture(showcaseLayer);
    if(menuLayer)     SDL_DestroyTexture(menuLayer);
    if(popupLayer)    SDL_DestroyTexture(popupLayer);
    if(titleFont) TTF_CloseFont(titleFont);
    if(font)      TTF_CloseFont(font);
    if(labelFont) TTF_CloseFont(labelFont);
    return nextScene;
}