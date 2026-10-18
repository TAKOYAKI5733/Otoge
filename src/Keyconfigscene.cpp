// =====================================================================
//  KeyConfigScene.cpp — キー設定の画面
//
//  ・画面上部：キーボードの図（白い枠・白い文字）。押している間はそのキーが色反転する
//  ・その下   ：つながっているコントローラーのボタン・十字キー・軸。押している間は色反転する
//  ・中央     ：6つのレーンの枠。クリック（または ←→ と ENTER）で選ぶと入力待ちになり、
//               次に押したキー／ボタンがそのレーンに割り当てられる（ESC でキャンセル）
//               割り当てた入力を押している間はレーンの枠も反転するので、6点同時押しのテストにもなる
//  ・ESC（入力待ちでないとき）：保存して選曲画面へ戻る
// =====================================================================
#include "GameCommon.h"
#include "InputBinding.h"
#include <unordered_map>
#include <cstdio>

namespace {

constexpr int SW = 1920;
constexpr int SH = 1080;

const SDL_Color WHITE = {255, 255, 255, 255};
const SDL_Color DARK  = {15, 15, 25, 255};      // 背景の色（反転したときの文字の色）
const SDL_Color CYAN  = {0, 255, 255, 255};
const SDL_Color GRAY  = {150, 150, 170, 255};

// ---------------------------------------------------------------------
//  文字の画像を作り置きする（キーボードの図は文字が多いので、毎フレーム作らない）
//    ・白で作っておき、色は SDL_SetTextureColorMod で変える（反転したときは暗い色に）
// ---------------------------------------------------------------------
class TextCache{
public:
    struct Entry{ SDL_Texture* tex = nullptr; int w = 0, h = 0; };

    explicit TextCache(SDL_Renderer* r) : renderer_(r) {}
    ~TextCache(){
        for(auto& kv : map_) if(kv.second.tex) SDL_DestroyTexture(kv.second.tex);
    }

    const Entry& get(TTF_Font* font, const std::string& text){
        const std::string key = std::to_string(reinterpret_cast<uintptr_t>(font)) + "|" + text;
        auto it = map_.find(key);
        if(it != map_.end()) return it->second;

        Entry e;
        if(font && !text.empty()){
            if(SDL_Surface* s = TTF_RenderUTF8_Blended(font, text.c_str(), WHITE)){
                e.tex = SDL_CreateTextureFromSurface(renderer_, s);
                e.w = s->w;
                e.h = s->h;
                SDL_FreeSurface(s);
                if(e.tex) SDL_SetTextureBlendMode(e.tex, SDL_BLENDMODE_BLEND);
            }
        }
        return map_.emplace(key, e).first->second;
    }

    enum class Align{ Left, Center, Right };

    // y は文字の縦の中心
    void draw(TTF_Font* font, const std::string& text, SDL_Color c, int x, int y, Align align = Align::Center){
        const Entry& e = get(font, text);
        if(!e.tex) return;
        int left = x;
        if(align == Align::Center) left = x - e.w / 2;
        if(align == Align::Right)  left = x - e.w;
        SDL_SetTextureColorMod(e.tex, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(e.tex, c.a);
        SDL_Rect dst = {left, y - e.h / 2, e.w, e.h};
        SDL_RenderCopy(renderer_, e.tex, nullptr, &dst);
    }

    // 幅に収まらないときは縮めて描く（長いキーの名前用）
    void drawFit(TTF_Font* font, const std::string& text, SDL_Color c, int cx, int cy, int maxW){
        const Entry& e = get(font, text);
        if(!e.tex) return;
        const float s = (e.w > maxW) ? static_cast<float>(maxW) / e.w : 1.0f;
        const int w = static_cast<int>(e.w * s), h = static_cast<int>(e.h * s);
        SDL_SetTextureColorMod(e.tex, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(e.tex, c.a);
        SDL_Rect dst = {cx - w / 2, cy - h / 2, w, h};
        SDL_RenderCopy(renderer_, e.tex, nullptr, &dst);
    }

private:
    SDL_Renderer* renderer_;
    std::unordered_map<std::string, Entry> map_;
};

// 枠（塗り＋線）
void drawBox(SDL_Renderer* r, const SDL_Rect& rect, SDL_Color fill, SDL_Color line, int thickness){
    if(fill.a > 0){
        SDL_SetRenderDrawColor(r, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(r, &rect);
    }
    SDL_SetRenderDrawColor(r, line.r, line.g, line.b, line.a);
    for(int t = 0; t < thickness; t++){
        SDL_Rect q = {rect.x + t, rect.y + t, rect.w - t * 2, rect.h - t * 2};
        SDL_RenderDrawRect(r, &q);
    }
}

bool inside(const SDL_Rect& r, int x, int y){
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// ---------------------------------------------------------------------
//  キーボードの図（日本語配列・テンキー付き）
//    ・位置はスキャンコード（キーの物理的な場所）で決まる
//    ・文字・数字のキーは、OS の配列設定から名前を取る（dynamic = true）
// ---------------------------------------------------------------------
struct KeyDef{
    SDL_Scancode sc;
    float x, y, w, h;      // 単位：キー1個ぶん
    const char* label;
    bool dynamic;          // true = SDL_GetKeyName で名前を取る
};

std::vector<KeyDef> buildKeyboard(){
    std::vector<KeyDef> k;
    auto add = [&](SDL_Scancode sc, float x, float y, float w, const char* label, bool dyn = false, float h = 1.0f){
        k.push_back({sc, x, y, w, h, label, dyn});
    };
    auto run = [&](float x, float y, std::initializer_list<SDL_Scancode> scs){   // 文字キーを1u ずつ並べる
        for(SDL_Scancode sc : scs){ add(sc, x, y, 1.0f, "", true); x += 1.0f; }
    };

    // ----- ファンクションの列 -----
    float y = 0.0f;
    add(SDL_SCANCODE_ESCAPE, 0, y, 1, "Esc");
    const char* fn[12] = {"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
    for(int i = 0; i < 12; i++){
        const float x = 2.0f + i + (i >= 4 ? 0.5f : 0.0f) + (i >= 8 ? 0.5f : 0.0f);
        add(static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + i), x, y, 1, fn[i]);
    }
    add(SDL_SCANCODE_PRINTSCREEN, 15.5f, y, 1, "PrtSc");
    add(SDL_SCANCODE_SCROLLLOCK,  16.5f, y, 1, "ScrLk");
    add(SDL_SCANCODE_PAUSE,       17.5f, y, 1, "Pause");

    // ----- 数字の列 -----
    y = 1.25f;
    add(SDL_SCANCODE_GRAVE, 0, y, 1, "半/全");
    run(1, y, {SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5,
               SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_0});
    add(SDL_SCANCODE_MINUS,          11, y, 1, "-");
    add(SDL_SCANCODE_EQUALS,         12, y, 1, "^");
    add(SDL_SCANCODE_INTERNATIONAL3, 13, y, 1, "¥");
    add(SDL_SCANCODE_BACKSPACE,      14, y, 1, "BS");
    add(SDL_SCANCODE_INSERT,   15.5f, y, 1, "Ins");
    add(SDL_SCANCODE_HOME,     16.5f, y, 1, "Home");
    add(SDL_SCANCODE_PAGEUP,   17.5f, y, 1, "PgUp");
    add(SDL_SCANCODE_NUMLOCKCLEAR, 19, y, 1, "Num");
    add(SDL_SCANCODE_KP_DIVIDE,    20, y, 1, "/");
    add(SDL_SCANCODE_KP_MULTIPLY,  21, y, 1, "*");
    add(SDL_SCANCODE_KP_MINUS,     22, y, 1, "-");

    // ----- Q の列 -----
    y = 2.25f;
    add(SDL_SCANCODE_TAB, 0, y, 1.5f, "Tab");
    run(1.5f, y, {SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R, SDL_SCANCODE_T,
                  SDL_SCANCODE_Y, SDL_SCANCODE_U, SDL_SCANCODE_I, SDL_SCANCODE_O, SDL_SCANCODE_P});
    add(SDL_SCANCODE_LEFTBRACKET,  11.5f, y, 1, "@");
    add(SDL_SCANCODE_RIGHTBRACKET, 12.5f, y, 1, "[");
    add(SDL_SCANCODE_RETURN,       13.5f, y, 1.5f, "Enter", false, 2.0f);   // 日本語配列の縦長 Enter
    add(SDL_SCANCODE_DELETE,   15.5f, y, 1, "Del");
    add(SDL_SCANCODE_END,      16.5f, y, 1, "End");
    add(SDL_SCANCODE_PAGEDOWN, 17.5f, y, 1, "PgDn");
    add(SDL_SCANCODE_KP_7, 19, y, 1, "7");
    add(SDL_SCANCODE_KP_8, 20, y, 1, "8");
    add(SDL_SCANCODE_KP_9, 21, y, 1, "9");
    add(SDL_SCANCODE_KP_PLUS, 22, y, 1, "+", false, 2.0f);

    // ----- A の列 -----
    y = 3.25f;
    add(SDL_SCANCODE_CAPSLOCK, 0, y, 1.5f, "英数");
    run(1.5f, y, {SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_G,
                  SDL_SCANCODE_H, SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L});
    add(SDL_SCANCODE_SEMICOLON,  10.5f, y, 1, ";");
    add(SDL_SCANCODE_APOSTROPHE, 11.5f, y, 1, ":");
    add(SDL_SCANCODE_BACKSLASH,  12.5f, y, 1, "]");
    add(SDL_SCANCODE_KP_4, 19, y, 1, "4");
    add(SDL_SCANCODE_KP_5, 20, y, 1, "5");
    add(SDL_SCANCODE_KP_6, 21, y, 1, "6");

    // ----- Z の列 -----
    y = 4.25f;
    add(SDL_SCANCODE_LSHIFT, 0, y, 2.25f, "Shift");
    run(2.25f, y, {SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V,
                   SDL_SCANCODE_B, SDL_SCANCODE_N, SDL_SCANCODE_M});
    add(SDL_SCANCODE_COMMA,          9.25f, y, 1, ",");
    add(SDL_SCANCODE_PERIOD,        10.25f, y, 1, ".");
    add(SDL_SCANCODE_SLASH,         11.25f, y, 1, "/");
    add(SDL_SCANCODE_INTERNATIONAL1, 12.25f, y, 1, "\\");
    add(SDL_SCANCODE_RSHIFT,        13.25f, y, 1.75f, "Shift");
    add(SDL_SCANCODE_UP, 16.5f, y, 1, "↑");
    add(SDL_SCANCODE_KP_1, 19, y, 1, "1");
    add(SDL_SCANCODE_KP_2, 20, y, 1, "2");
    add(SDL_SCANCODE_KP_3, 21, y, 1, "3");
    add(SDL_SCANCODE_KP_ENTER, 22, y, 1, "Ent", false, 2.0f);

    // ----- 一番下の列 -----
    y = 5.25f;
    float x = 0.0f;
    auto bottom = [&](SDL_Scancode sc, float w, const char* label){ add(sc, x, y, w, label); x += w; };
    bottom(SDL_SCANCODE_LCTRL, 1.5f, "Ctrl");
    bottom(SDL_SCANCODE_LGUI, 1.25f, "Win");
    bottom(SDL_SCANCODE_LALT, 1.25f, "Alt");
    bottom(SDL_SCANCODE_INTERNATIONAL5, 1.25f, "無変換");
    bottom(SDL_SCANCODE_SPACE, 3.5f, "Space");
    bottom(SDL_SCANCODE_INTERNATIONAL4, 1.25f, "変換");
    bottom(SDL_SCANCODE_INTERNATIONAL2, 1.25f, "かな");
    bottom(SDL_SCANCODE_RALT, 1.25f, "Alt");
    bottom(SDL_SCANCODE_APPLICATION, 1.0f, "Menu");
    bottom(SDL_SCANCODE_RCTRL, 1.5f, "Ctrl");
    add(SDL_SCANCODE_LEFT,  15.5f, y, 1, "←");
    add(SDL_SCANCODE_DOWN,  16.5f, y, 1, "↓");
    add(SDL_SCANCODE_RIGHT, 17.5f, y, 1, "→");
    add(SDL_SCANCODE_KP_0,      19, y, 2, "0");
    add(SDL_SCANCODE_KP_PERIOD, 21, y, 1, ".");
    return k;
}

constexpr float KEY_U    = 54.0f;                    // キー1個ぶんの大きさ(px)
constexpr float KB_W_U   = 23.0f;                    // 図全体の幅（キーの数）
constexpr int   KB_X     = static_cast<int>((SW - KB_W_U * KEY_U) / 2);
constexpr int   KB_Y     = 40;
constexpr int   KEY_GAP  = 4;                        // キーとキーのすき間(px)

// ----- その他の配置 -----
constexpr int PAD_Y      = 420;                      // コントローラーの欄
constexpr int SLOT_Y     = 610;                      // レーンの枠
constexpr int SLOT_W     = 240;
constexpr int SLOT_H     = 170;
constexpr int SLOT_GAP   = 20;
constexpr int SLOT_X     = (SW - (SLOT_W * LANE_COUNT + SLOT_GAP * (LANE_COUNT - 1))) / 2;

SDL_Rect slotRect(int i){ return {SLOT_X + i * (SLOT_W + SLOT_GAP), SLOT_Y, SLOT_W, SLOT_H}; }
const SDL_Rect BTN_RESET = {SW / 2 - 340, 940, 320, 64};
const SDL_Rect BTN_BACK  = {SW / 2 + 20,  940, 320, 64};

} // namespace

// =====================================================================
GameScene keyConfigScene(SDL_Window* window, SDL_Renderer* renderer, PlayerSettings& settings){
    TTF_Font* keyFont   = TTF_OpenFont("fonts/prac.ttf", 18);
    TTF_Font* font      = TTF_OpenFont("fonts/prac.ttf", 28);
    TTF_Font* bigFont   = TTF_OpenFont("fonts/prac.ttf", 36);
    TTF_Font* smallFont = TTF_OpenFont("fonts/prac.ttf", 22);
    if(!keyFont || !font || !bigFont || !smallFont){
        std::cout << "キー設定のフォント読込失敗\n";
        if(keyFont) TTF_CloseFont(keyFont);
        if(font) TTF_CloseFont(font);
        if(bigFont) TTF_CloseFont(bigFont);
        if(smallFont) TTF_CloseFont(smallFont);
        return GameScene::Select;
    }

    {
        TextCache text(renderer);
        const std::vector<KeyDef> keys = buildKeyboard();

        LaneBindings bindings = settings.laneBindings;   // 編集中の割り当て（保存するまで settings は変えない）
        int waitingLane = -1;      // 入力待ちのレーン（-1 = 待っていない）
        int cursor = 0;            // キーボード操作で選んでいるレーン
        int mouseX = -1, mouseY = -1;
        bool running = true;
        GameScene nextScene = GameScene::Select;
        SDL_Event e;

        // 割り当てる：同じ入力が別のレーンにあれば、入れ替える（1つの入力が2つのレーンに入らないように）
        auto assign = [&](int lane, const InputBinding& b){
            for(int j = 0; j < LANE_COUNT; j++){
                if(j != lane && bindings[j] == b) bindings[j] = bindings[lane];
            }
            bindings[lane] = b;
        };

        auto saveAndExit = [&](){
            settings.laneBindings = bindings;
            savePlayerSettings(settings);
            running = false;
            nextScene = GameScene::Select;
        };

        while(running){
            const Uint32 now = SDL_GetTicks();
            InputDevices::get().refresh();   // コントローラーの抜き差しに合わせる

            while(SDL_PollEvent(&e) != 0){
                if(e.type == SDL_QUIT){
                    running = false;
                    nextScene = GameScene::Shutdown;
                    break;
                }

                // ----- 入力待ち：次に押されたものを割り当てる -----
                if(waitingLane >= 0){
                    if(e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE){
                        waitingLane = -1;   // キャンセル
                        continue;
                    }
                    if(e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT){
                        waitingLane = -1;   // 右クリックでもキャンセル
                        continue;
                    }
                    InputBinding b;
                    if(captureBinding(e, b)){
                        assign(waitingLane, b);
                        cursor = waitingLane;
                        waitingLane = -1;
                    }
                    continue;   // 入力待ちの間は、ほかの操作をしない
                }

                // ----- 通常 -----
                if(e.type == SDL_MOUSEMOTION){
                    mouseX = e.motion.x;
                    mouseY = e.motion.y;
                }
                else if(e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT){
                    const int mx = e.button.x, my = e.button.y;
                    for(int i = 0; i < LANE_COUNT; i++){
                        if(inside(slotRect(i), mx, my)){
                            waitingLane = i;
                            cursor = i;
                        }
                    }
                    if(inside(BTN_RESET, mx, my)) bindings = defaultLaneBindings();
                    if(inside(BTN_BACK,  mx, my)) saveAndExit();
                }
                else if(e.type == SDL_KEYDOWN && !e.key.repeat){
                    switch(e.key.keysym.sym){
                        case SDLK_ESCAPE: saveAndExit(); break;
                        case SDLK_LEFT:   cursor = (cursor + LANE_COUNT - 1) % LANE_COUNT; break;
                        case SDLK_RIGHT:  cursor = (cursor + 1) % LANE_COUNT; break;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER: waitingLane = cursor; break;
                        default: break;
                    }
                }
            }

            const Uint8* ks = SDL_GetKeyboardState(nullptr);

            // ================= 描画 =================
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, DARK.r, DARK.g, DARK.b, 255);
            SDL_RenderClear(renderer);

            // ----- 1. キーボードの図 -----
            for(const KeyDef& k : keys){
                SDL_Rect r = {
                    KB_X + static_cast<int>(k.x * KEY_U) + KEY_GAP / 2,
                    KB_Y + static_cast<int>(k.y * KEY_U) + KEY_GAP / 2,
                    static_cast<int>(k.w * KEY_U) - KEY_GAP,
                    static_cast<int>(k.h * KEY_U) - KEY_GAP
                };
                const bool pressed = ks[k.sc] != 0;

                // 押している間は色反転：白で塗って、文字を暗い色に
                drawBox(renderer, r, pressed ? WHITE : SDL_Color{0, 0, 0, 0}, WHITE, 2);

                std::string label = k.label;
                if(k.dynamic){
                    const char* n = SDL_GetKeyName(SDL_GetKeyFromScancode(k.sc));
                    if(n && *n) label = n;
                }
                text.drawFit(keyFont, label, pressed ? DARK : WHITE, r.x + r.w / 2, r.y + r.h / 2, r.w - 6);

                // レーンに割り当てているキーには、左上に小さくレーンの番号を出す
                for(int i = 0; i < LANE_COUNT; i++){
                    if(bindings[i] == InputBinding::key(k.sc)){
                        text.draw(keyFont, std::to_string(i + 1), pressed ? DARK : CYAN, r.x + 9, r.y + 12);
                    }
                }
            }

            // ----- 2. コントローラー -----
            {
                const auto& joys = InputDevices::get().joysticks();
                text.draw(font, "コントローラー", WHITE, KB_X, PAD_Y, TextCache::Align::Left);
                if(joys.empty()){
                    text.draw(smallFont, "接続されていません（USB でつなぐと自動で表示されます）", GRAY,
                              KB_X + 220, PAD_Y, TextCache::Align::Left);
                }

                // 1台につき1行：名前 → ボタン → 十字キー → 軸
                int row = 0;
                for(SDL_Joystick* j : joys){
                    if(row >= 2) break;   // 3台目以降は省略（割り当て・判定には使える）
                    const int y = PAD_Y + 50 + row * 56;
                    const char* name = SDL_JoystickName(j);
                    text.drawFit(smallFont, name ? name : "Controller", GRAY, KB_X + 110, y, 220);

                    int x = KB_X + 240;
                    auto padBox = [&](const std::string& label, bool on, int w){
                        if(x + w > SW - 40) return;
                        SDL_Rect r = {x, y - 20, w, 40};
                        drawBox(renderer, r, on ? WHITE : SDL_Color{0, 0, 0, 0}, WHITE, 2);
                        text.drawFit(keyFont, label, on ? DARK : WHITE, r.x + r.w / 2, r.y + r.h / 2, w - 6);
                        x += w + 6;
                    };

                    for(int b = 0; b < SDL_JoystickNumButtons(j); b++){
                        padBox(std::to_string(b + 1), SDL_JoystickGetButton(j, b) != 0, 40);
                    }
                    for(int h = 0; h < SDL_JoystickNumHats(j); h++){
                        const Uint8 v = SDL_JoystickGetHat(j, h);
                        padBox("↑", (v & SDL_HAT_UP) != 0, 40);
                        padBox("↓", (v & SDL_HAT_DOWN) != 0, 40);
                        padBox("←", (v & SDL_HAT_LEFT) != 0, 40);
                        padBox("→", (v & SDL_HAT_RIGHT) != 0, 40);
                    }
                    for(int a = 0; a < SDL_JoystickNumAxes(j) && a < 8; a++){
                        const int d = SDL_JoystickGetAxis(j, a) - axisRestValue(j, a);   // 離した状態からどれだけ倒したか
                        padBox("A" + std::to_string(a + 1) + "+", d >  AXIS_PRESS_THRESHOLD, 56);
                        padBox("A" + std::to_string(a + 1) + "-", d < -AXIS_PRESS_THRESHOLD, 56);
                    }
                    row++;
                }
            }

            // ----- 3. 注意書き -----
            text.draw(font, "※ 6点同時押しができるキーをお選びください", SDL_Color{255, 210, 80, 255}, SW / 2, 540);
            text.draw(smallFont, "キーボードによっては、同時に押せるキーの数や組み合わせに制限があります。6つを同時に押して、6つとも反転するか確かめてください。",
                      GRAY, SW / 2, 576);

            // ----- 4. レーンの枠 -----
            int pressedCount = 0;
            for(int i = 0; i < LANE_COUNT; i++){
                const SDL_Rect r = slotRect(i);
                const bool waiting = (waitingLane == i);
                const bool on = !waiting && isBindingPressed(bindings[i], ks);
                const bool hover = waitingLane < 0 && inside(r, mouseX, mouseY);
                const bool selected = waitingLane < 0 && cursor == i;
                if(on) pressedCount++;

                SDL_Color line = WHITE;
                int thick = 2;
                if(waiting){
                    // 入力待ち：シアンの枠を点滅させる
                    const Uint8 a = static_cast<Uint8>(140 + 115 * (0.5 + 0.5 * std::sin(now * 0.012)));
                    line = SDL_Color{0, 255, 255, a};
                    thick = 4;
                }
                else if(hover || selected){
                    line = CYAN;
                    thick = 3;
                }
                drawBox(renderer, r, on ? WHITE : SDL_Color{30, 30, 50, 200}, line, thick);

                const SDL_Color tc = on ? DARK : WHITE;
                text.draw(smallFont, "LANE " + std::to_string(i + 1), on ? DARK : GRAY, r.x + r.w / 2, r.y + 28);
                if(waiting){
                    text.draw(font, "入力待ち…", CYAN, r.x + r.w / 2, r.y + r.h / 2 + 4);
                }
                else{
                    text.drawFit(bigFont, bindingName(bindings[i]), tc, r.x + r.w / 2, r.y + r.h / 2 + 4, r.w - 20);
                }
                text.draw(keyFont, waiting ? "ESC：キャンセル" : "クリックで変更", on ? DARK : GRAY, r.x + r.w / 2, r.y + r.h - 24);
            }

            // ----- 5. 同時押しテスト -----
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "同時押しテスト：%d / %d", pressedCount, LANE_COUNT);
                text.draw(font, buf, pressedCount == LANE_COUNT ? CYAN : WHITE, SW / 2, SLOT_Y + SLOT_H + 50);
            }

            // ----- 6. ボタン -----
            auto button = [&](const SDL_Rect& r, const char* label){
                const bool hover = waitingLane < 0 && inside(r, mouseX, mouseY);
                drawBox(renderer, r, hover ? SDL_Color{45, 45, 80, 220} : SDL_Color{30, 30, 50, 200},
                        hover ? CYAN : SDL_Color{110, 110, 150, 255}, hover ? 3 : 1);
                text.draw(font, label, hover ? CYAN : WHITE, r.x + r.w / 2, r.y + r.h / 2);
            };
            button(BTN_RESET, "初期設定に戻す");
            button(BTN_BACK,  "保存して戻る");

            // ----- 7. 入力待ちの案内 -----
            if(waitingLane >= 0){
                char buf[96];
                std::snprintf(buf, sizeof(buf), "LANE %d に割り当てるキー／ボタンを押してください", waitingLane + 1);
                text.draw(font, buf, CYAN, SW / 2, SH - 50);
            }
            else{
                text.draw(smallFont, "クリック / ←→＋ENTER：変更    押したまま：同時押しテスト    ESC：保存して戻る",
                          SDL_Color{130, 130, 150, 255}, SW / 2, SH - 50);
            }

            SDL_RenderPresent(renderer);
            g_framePacer.endFrame(window);
        }

        TTF_CloseFont(keyFont);
        TTF_CloseFont(font);
        TTF_CloseFont(bigFont);
        TTF_CloseFont(smallFont);
        return nextScene;
    }   // ← ここで TextCache が片付き、作った文字の画像も消える
}