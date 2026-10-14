#include "GameCommon.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// =====================================================================
//  判定調整シーン： 1.音声調整 → 2.表示調整 → 3.確認
//    ・叩く回数は自由。ENTER を押した時点の値で次へ進む（叩かなければ今の値のまま）
//    ・値は常に「+12 ms」のように表示し、←→ で 1ms（Shift で 10ms）ずつ微調整できる
//    ・確認で ENTER を押すと settings.json に保存して選曲画面へ戻る
// =====================================================================

namespace {

constexpr int    SW = 1920;
constexpr int    SH = 1080;
constexpr double PI = 3.14159265358979;

constexpr double BEAT_MS        = 500.0;    // 拍の間隔（120 BPM）
constexpr double LEAD_MS        = 1500.0;   // 段階に入ってから最初の拍までの待ち時間
constexpr double OFFSET_LIMIT   = 500.0;    // 音声オフセットの範囲 ±
constexpr double VISUAL_LIMIT   = 300.0;    // 表示オフセットの範囲 ±
constexpr int    GRAPH_TAPS     = 32;       // グラフに出す直近のタップ数
constexpr double GRAPH_RANGE_MS = 150.0;    // グラフの左右の端（±ms）
constexpr double BURST_MS       = 220.0;    // ノーツが判定ラインで破裂する演出の長さ

// レーン（表示調整・確認で使う）
constexpr int    LANE_CX    = 630;
constexpr int    LANE_W     = 200;
constexpr int    LANE_TOP   = 200;
constexpr int    JUDGE_Y    = 820;
constexpr double NOTE_SPEED = 0.9;          // px/ms

// 右側の数値ウィンドウ
constexpr SDL_Rect INFO_RECT = {1060, 220, 640, 640};

enum class Step{ Audio, Visual, Confirm };

// ---------------------------------------------------------------------
// メトロノーム：1拍分（クリック音＋無音）の音を作り、ループ再生する
//   ループはサンプル単位で正確なので、拍の間隔がフレームレートに左右されない
// ---------------------------------------------------------------------
struct Metronome{
    std::vector<Uint8> buffer;   // Mix_QuickLoad_RAW はこの領域を参照するので、chunk より長く持つ
    Mix_Chunk* chunk = nullptr;
    int channel = -1;

    bool create(){
        int freq = 0;
        Uint16 format = 0;
        int channels = 0;
        if(!Mix_QuerySpec(&freq, &format, &channels)) return false;
        if(format != AUDIO_S16SYS || channels <= 0) return false;   // ゲームは16bitで開いている

        const int frames      = static_cast<int>(freq * BEAT_MS / 1000.0);
        const int clickFrames = freq * 30 / 1000;   // 30ms のクリック

        std::vector<Sint16> samples(static_cast<size_t>(frames) * channels, 0);
        for(int f = 0; f < clickFrames && f < frames; f++){
            double t   = static_cast<double>(f) / freq;
            double env = std::exp(-t * 120.0);                      // すぐ減衰する「カッ」という音
            double v   = std::sin(2.0 * PI * 1500.0 * t) * env * 0.6;
            Sint16 s   = static_cast<Sint16>(v * 32767.0);
            for(int c = 0; c < channels; c++) samples[static_cast<size_t>(f) * channels + c] = s;
        }

        buffer.resize(samples.size() * sizeof(Sint16));
        std::memcpy(buffer.data(), samples.data(), buffer.size());
        chunk = Mix_QuickLoad_RAW(buffer.data(), static_cast<Uint32>(buffer.size()));
        return chunk != nullptr;
    }

    void start(int volume){
        stop();
        if(!chunk) return;
        Mix_VolumeChunk(chunk, volume);
        channel = Mix_PlayChannel(-1, chunk, -1);   // -1 = 無限ループ
    }

    void stop(){
        if(channel >= 0){
            Mix_HaltChannel(channel);
            channel = -1;
        }
    }

    void destroy(){
        stop();
        if(chunk){
            Mix_FreeChunk(chunk);   // QuickLoad_RAW の chunk は buffer を解放しない
            chunk = nullptr;
        }
    }
};

// ---------------------------------------------------------------------
// 叩いたズレの記録
// ---------------------------------------------------------------------
struct TapStats{
    std::vector<double> errors;   // 叩いた時刻 − 正しい時刻（+ = 遅い）

    void add(double e){ errors.push_back(e); }
    void clear(){ errors.clear(); }
    int count() const { return static_cast<int>(errors.size()); }

    // 中央値：たまに大きく外したタップがあっても引っ張られにくい
    double median() const{
        if(errors.empty()) return 0.0;
        std::vector<double> v = errors;
        size_t mid = v.size() / 2;
        std::nth_element(v.begin(), v.begin() + mid, v.end());
        double m = v[mid];
        if(v.size() % 2 == 0){
            double lower = *std::max_element(v.begin(), v.begin() + mid);
            m = (m + lower) * 0.5;
        }
        return m;
    }
};

// ---------------------------------------------------------------------
// 描画の小道具
// ---------------------------------------------------------------------
enum class Align{ Left, Center, Right };

void drawText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, SDL_Color color,
              int x, int y, Align align, Uint8 alpha = 255){
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

void drawFrame(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color fill, SDL_Color border, int thickness){
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    for(int t = 0; t < thickness; t++){
        SDL_Rect r = {rect.x + t, rect.y + t, rect.w - t * 2, rect.h - t * 2};
        SDL_RenderDrawRect(renderer, &r);
    }
}

void drawTab(SDL_Renderer* renderer, TTF_Font* font, const char* title, const SDL_Rect& win){
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, title, SDL_Color{15, 15, 25, 255});
    if(!surf) return;
    SDL_Rect tab = {win.x, win.y - surf->h - 6, surf->w + 28, surf->h + 6};
    SDL_SetRenderDrawColor(renderer, 0, 200, 255, 255);
    SDL_RenderFillRect(renderer, &tab);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect dst = {tab.x + 14, tab.y + 3, surf->w, surf->h};
    SDL_RenderCopy(renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

// ms の値を「+12 ms」「-3 ms」「±0 ms」の形にする
std::string formatMs(double v){
    double r = std::round(v);
    if(std::fabs(r) < 0.5) return "±0 ms";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%+.0f ms", r);
    return buf;
}

// ズレの大きさで色を変える（小さいほど青、大きいほど橙）
SDL_Color errorColor(double e){
    double a = std::fabs(e);
    if(a <= 10.0) return SDL_Color{0, 255, 255, 255};
    if(a <= 25.0) return SDL_Color{255, 255, 255, 255};
    return SDL_Color{255, 160, 60, 255};
}

// 直近のズレを横一列のグラフにする（左 = 早い、右 = 遅い）
void drawErrorGraph(SDL_Renderer* renderer, TTF_Font* smallFont, const TapStats& taps,
                    int x, int y, int w, double centerValue){
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // 軸
    SDL_SetRenderDrawColor(renderer, 110, 110, 150, 255);
    SDL_Rect axis = {x, y, w, 2};
    SDL_RenderFillRect(renderer, &axis);
    SDL_Rect center = {x + w / 2 - 1, y - 30, 2, 60};
    SDL_RenderFillRect(renderer, &center);

    auto toX = [&](double e){
        double c = std::clamp(e, -GRAPH_RANGE_MS, GRAPH_RANGE_MS);
        return x + static_cast<int>((c + GRAPH_RANGE_MS) / (GRAPH_RANGE_MS * 2.0) * w);
    };

    // 直近のタップ：古いものほど薄く
    int n = taps.count();
    int first = std::max(0, n - GRAPH_TAPS);
    for(int i = first; i < n; i++){
        double age = static_cast<double>(n - 1 - i) / GRAPH_TAPS;   // 0 = 最新
        Uint8 a = static_cast<Uint8>(255 * (1.0 - age * 0.8));
        bool latest = (i == n - 1);
        SDL_Color c = latest ? SDL_Color{255, 255, 255, 255} : SDL_Color{0, 200, 255, 255};
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, a);
        SDL_Rect tick = {toX(taps.errors[i]) - (latest ? 2 : 1), y - (latest ? 22 : 14), latest ? 4 : 3, latest ? 44 : 28};
        SDL_RenderFillRect(renderer, &tick);
    }

    // 今の値（中央値＋微調整）の位置に金色の印
    if(n > 0){
        SDL_SetRenderDrawColor(renderer, 255, 190, 30, 255);
        SDL_Rect mark = {toX(centerValue) - 3, y + 10, 6, 18};
        SDL_RenderFillRect(renderer, &mark);
    }

    SDL_Color gray = {150, 150, 170, 255};
    drawText(renderer, smallFont, "早い", gray, x, y + 50, Align::Left);
    drawText(renderer, smallFont, "遅い", gray, x + w, y + 50, Align::Right);
    drawText(renderer, smallFont, "-150", gray, x, y - 50, Align::Left);
    drawText(renderer, smallFont, "+150", gray, x + w, y - 50, Align::Right);
}

} // namespace

// =====================================================================
GameScene calibrationScene(SDL_Window* window, SDL_Renderer* renderer, PlayerSettings& settings){
    TTF_Font* bigFont   = TTF_OpenFont("fonts/prac.ttf", 80);
    TTF_Font* font      = TTF_OpenFont("fonts/prac.ttf", 40);
    TTF_Font* smallFont = TTF_OpenFont("fonts/prac.ttf", 24);
    if(!bigFont || !font || !smallFont){
        std::cout << "判定調整のフォント読込失敗\n";
        if(bigFont) TTF_CloseFont(bigFont);
        if(font) TTF_CloseFont(font);
        if(smallFont) TTF_CloseFont(smallFont);
        return GameScene::Select;
    }

    Metronome metro;
    if(!metro.create()){
        std::cout << "[警告]メトロノーム音の作成に失敗（音声調整は音なしになります）\n";
    }
    const int seVolume = static_cast<int>(settings.seVolume * MIX_MAX_VOLUME / 100.0);

    // 高精度タイマー（PlayScene と同じ方式）
    const double perfFreq = static_cast<double>(SDL_GetPerformanceFrequency());
    const Uint64 perfStart = SDL_GetPerformanceCounter();
    auto nowMs = [&](){
        return static_cast<double>(SDL_GetPerformanceCounter() - perfStart) * 1000.0 / perfFreq;
    };

    // ----- 状態 -----
    Step step = Step::Audio;
    TapStats taps;
    double audioOffset  = settings.offsetMs;         // 編集中の音声オフセット
    double visualOffset = settings.visualOffsetMs;   // 編集中の表示オフセット
    double nudge = 0.0;                              // 音声調整・表示調整での微調整
    int confirmTarget = 0;                           // 確認での微調整の対象（0:音声 1:表示）

    double beatOriginMs = 0.0;       // 最初の拍の時刻（実時間）
    bool   metroPending = false;     // メトロノームを鳴らし始める前か
    double lastTapMs = -1e9;         // 最後に叩いた実時間（叩いたときの光る演出用）
    double lastError = 0.0;
    double visualAtEntry = visualOffset;   // 表示調整に入ったときの値（Backspace で戻す先）

    // 今の段階で、ENTER を押したら決まる値
    auto currentValue = [&](){
        if(step == Step::Audio){
            double base = taps.count() > 0 ? taps.median() : audioOffset;
            return std::clamp(std::round(base + nudge), -OFFSET_LIMIT, OFFSET_LIMIT);
        }
        if(step == Step::Visual){
            return std::clamp(std::round(visualOffset), -VISUAL_LIMIT, VISUAL_LIMIT);   // ←→ で直接動かす
        }
        return 0.0;
    };

    // 段階に入る：記録を消し、拍の基準を「今から LEAD_MS 後」にする
    auto enterStep = [&](Step s){
        step = s;
        taps.clear();
        nudge = 0.0;
        metro.stop();
        beatOriginMs = nowMs() + LEAD_MS;
        metroPending = true;                  // どの段階もメトロノームを鳴らす（表示調整は音に見た目を合わせる）
        visualAtEntry = visualOffset;
    };
    enterStep(Step::Audio);

    bool running = true;
    GameScene nextScene = GameScene::Select;
    SDL_Event e;

    while(running){
        const double frameMs = nowMs();   // このフレームの時刻（PlayScene と同じく、フレームの頭で1回だけ取る）

        // メトロノームを鳴らし始める。実際に鳴らした瞬間を拍の基準にする
        if(metroPending && frameMs >= beatOriginMs){
            metro.start(seVolume);
            beatOriginMs = nowMs();
            metroPending = false;
        }

        // 確認での「曲の時刻」と「表示の時刻」（PlayScene と同じ式）
        const double musicTime  = (frameMs - beatOriginMs) - audioOffset;
        const double renderTime = musicTime + visualOffset;

        // ----- 入力 -----
        while(SDL_PollEvent(&e) != 0){
            if(e.type == SDL_QUIT){
                running = false;
                nextScene = GameScene::Shutdown;
                continue;
            }
            if(e.type != SDL_KEYDOWN) continue;

            SDL_Keycode sym = e.key.keysym.sym;
            bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
            bool isTapKey = (sym == SDLK_w || sym == SDLK_e || sym == SDLK_r || sym == SDLK_u ||
                             sym == SDLK_i || sym == SDLK_o || sym == SDLK_SPACE);

            if(isTapKey){
                if(e.key.repeat) continue;
                if(step == Step::Visual) continue;   // 表示調整は叩かずに目で合わせる

                double err;
                if(step == Step::Confirm){
                    // 確認：オフセットを入れた曲の時刻で、一番近い拍とのズレ
                    double k = std::round(musicTime / BEAT_MS);
                    if(k < 0) continue;
                    err = musicTime - k * BEAT_MS;
                }
                else{
                    // 音声調整・表示調整：オフセットなしの生の時刻で、一番近い拍とのズレ
                    double rel = frameMs - beatOriginMs;
                    if(rel < -BEAT_MS / 2.0) continue;   // 最初の拍より前の空打ちは数えない
                    double k = std::round(rel / BEAT_MS);
                    err = rel - k * BEAT_MS;
                }
                taps.add(err);
                lastError = err;
                lastTapMs = frameMs;
                continue;
            }

            switch(sym){
                case SDLK_LEFT:
                case SDLK_RIGHT:{
                    double amount = (sym == SDLK_RIGHT ? 1.0 : -1.0) * (shift ? 10.0 : 1.0);
                    if(step == Step::Visual){
                        visualOffset = std::clamp(visualOffset + amount, -VISUAL_LIMIT, VISUAL_LIMIT);
                    }
                    else if(step == Step::Confirm){
                        if(confirmTarget == 0) audioOffset  = std::clamp(audioOffset + amount, -OFFSET_LIMIT, OFFSET_LIMIT);
                        else                   visualOffset = std::clamp(visualOffset + amount, -VISUAL_LIMIT, VISUAL_LIMIT);
                    }
                    else{
                        nudge += amount;
                    }
                    break;
                }

                case SDLK_UP:
                case SDLK_DOWN:
                    if(step == Step::Confirm && !e.key.repeat) confirmTarget = 1 - confirmTarget;
                    break;

                case SDLK_BACKSPACE:
                    if(e.key.repeat) break;
                    taps.clear();
                    if(step == Step::Audio)  nudge = 0.0;
                    if(step == Step::Visual) visualOffset = visualAtEntry;   // 入ったときの値に戻す
                    break;

                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    if(e.key.repeat) break;
                    if(step == Step::Audio){
                        audioOffset = currentValue();
                        enterStep(Step::Visual);
                    }
                    else if(step == Step::Visual){
                        visualOffset = currentValue();   // ←→ で動かした値をそのまま使う
                        enterStep(Step::Confirm);
                    }
                    else{
                        // 確認：保存して選曲画面へ
                        settings.offsetMs       = std::round(audioOffset);
                        settings.visualOffsetMs = std::round(visualOffset);
                        savePlayerSettings(settings);
                        running = false;
                        nextScene = GameScene::Select;
                    }
                    break;

                case SDLK_ESCAPE:
                    if(e.key.repeat) break;
                    if(step == Step::Audio){
                        running = false;                 // 保存せずに戻る
                        nextScene = GameScene::Select;
                    }
                    else if(step == Step::Visual) enterStep(Step::Audio);
                    else                          enterStep(Step::Visual);
                    break;

                default:
                    break;
            }
        }

        // ----- 描画 -----
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 15, 15, 25, 255);
        SDL_RenderClear(renderer);

        // 1. 段階の表示（今の段階をシアン、終わった段階を白、まだの段階を灰色）
        {
            const char* names[3] = {"1. 音声調整", "2. 表示調整", "3. 確認"};
            int cur = static_cast<int>(step);
            int xs[3] = {560, 960, 1360};
            for(int i = 0; i < 3; i++){
                SDL_Color c = (i == cur) ? SDL_Color{0, 255, 255, 255}
                            : (i < cur)  ? SDL_Color{255, 255, 255, 255}
                                         : SDL_Color{100, 100, 120, 255};
                drawText(renderer, font, names[i], c, xs[i], 70, Align::Center);
                if(i < 2) drawText(renderer, font, ">", SDL_Color{100, 100, 120, 255}, xs[i] + 200, 70, Align::Center);
            }
        }

        // 2. 説明文
        const char* instruction =
            (step == Step::Audio)  ? "拍の音に合わせて W E R U I O / Space を叩いてください" :
            (step == Step::Visual) ? "叩かずに見てください。ノーツが破裂する瞬間と重なるように ←→ で合わせます" :
                                     "音とノーツに合わせて叩いてください。ズレが ±0 に近ければ調整完了です";
        drawText(renderer, smallFont, instruction, SDL_Color{200, 200, 220, 255}, SW / 2, 140, Align::Center);

        // 3. 左側：音声調整は叩いたときに光る円、表示調整・確認は流れるノーツ
        if(step == Step::Audio){
            double since = frameMs - lastTapMs;
            double glow = std::clamp(1.0 - since / 250.0, 0.0, 1.0);
            int r = 120 + static_cast<int>(glow * 30);
            // 拍に合わせて光らせると目で合わせてしまうので、光るのは「叩いたとき」だけ
            SDL_Rect pad = {LANE_CX - r, 520 - r, r * 2, r * 2};
            drawFrame(renderer, pad,
                      SDL_Color{30, 30, 50, 200},
                      SDL_Color{0, static_cast<Uint8>(150 + 105 * glow), 255, 255}, 3);
            drawText(renderer, bigFont, "TAP", SDL_Color{255, 255, 255, 255}, LANE_CX, 520, Align::Center,
                     static_cast<Uint8>(120 + 135 * glow));
        }
        else{
            // レーンと判定ボックス（ノーツ＝幅 LANE_W-24・高さ24 が、すき間 1px でちょうど収まる枠）
            SDL_Rect lane = {LANE_CX - LANE_W / 2, LANE_TOP, LANE_W, JUDGE_Y + 60 - LANE_TOP};
            drawFrame(renderer, lane, SDL_Color{30, 30, 50, 200}, SDL_Color{110, 110, 150, 255}, 2);
            SDL_Rect judgeBox = {lane.x + 12, JUDGE_Y - 12, LANE_W - 24, 24};
            drawFrame(renderer, judgeBox, SDL_Color{0, 0, 0, 0}, SDL_Color{255, 255, 255, 255}, 3);

            // ノーツ：プレイ画面と同じく「音声オフセット込みの時刻 ＋ 表示オフセット」で位置を決める
            const double t = renderTime;
            int kFirst = std::max(0, static_cast<int>(std::floor((t - BURST_MS) / BEAT_MS)));
            int kLast  = static_cast<int>(std::ceil((t + (JUDGE_Y - LANE_TOP) / NOTE_SPEED) / BEAT_MS));
            for(int k = kFirst; k <= kLast; k++){
                double target = k * BEAT_MS;

                if(t < target){
                    // 判定ラインに着くまでは普通に流す
                    int y = JUDGE_Y - static_cast<int>((target - t) * NOTE_SPEED);
                    SDL_Rect note = {lane.x + 12, y - 12, LANE_W - 24, 24};
                    SDL_RenderSetClipRect(renderer, &lane);
                    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                    SDL_RenderFillRect(renderer, &note);
                    SDL_RenderSetClipRect(renderer, nullptr);
                    continue;
                }

                // 判定ラインに着いた瞬間に破裂：最初が一番明るく、広がりながら消える
                double p = (t - target) / BURST_MS;
                if(p >= 1.0) continue;
                double ease = 1.0 - (1.0 - p) * (1.0 - p);   // 最初に速く広がる

                if(p < 0.15){
                    // 破裂の一瞬だけ、レーン全体を白く光らせる（タイミングを見やすくする）
                    Uint8 fa = static_cast<Uint8>(200 * (1.0 - p / 0.15));
                    SDL_SetRenderDrawColor(renderer, 255, 255, 255, fa);
                    SDL_Rect flash = {lane.x, JUDGE_Y - 40, LANE_W, 80};
                    SDL_RenderFillRect(renderer, &flash);
                }

                Uint8 a = static_cast<Uint8>(255 * (1.0 - p));
                int w = static_cast<int>((LANE_W - 24) * (1.0 + ease * 0.8));
                int h = static_cast<int>(24 + ease * 80);
                SDL_Rect ring = {LANE_CX - w / 2, JUDGE_Y - h / 2, w, h};
                drawFrame(renderer, ring, SDL_Color{0, 0, 0, 0}, SDL_Color{0, 255, 255, a}, 4);
            }

            // 叩いたズレを判定ラインの横に出す（叩くのは確認だけ）
            double since = frameMs - lastTapMs;
            if(step == Step::Confirm && since < 600.0){
                Uint8 a = static_cast<Uint8>(255 * (1.0 - since / 600.0));
                drawText(renderer, font, formatMs(lastError), errorColor(lastError),
                         lane.x + LANE_W + 30, JUDGE_Y, Align::Left, a);
            }
        }

        // 4. 右側：数値のウィンドウ
        {
            const SDL_Rect& w = INFO_RECT;
            const char* tabName = (step == Step::Audio) ? "AUDIO" : (step == Step::Visual) ? "VISUAL" : "CHECK";
            drawTab(renderer, smallFont, tabName, w);
            drawFrame(renderer, w, SDL_Color{10, 10, 20, 220}, SDL_Color{0, 200, 255, 255}, 3);
            int cx = w.x + w.w / 2;

            if(step == Step::Visual){
                // 表示調整：叩かないので、値と合わせ方のヒントだけを出す
                drawText(renderer, font, "表示オフセット", SDL_Color{255, 255, 255, 255}, cx, w.y + 70, Align::Center);
                drawText(renderer, bigFont, "<  " + formatMs(currentValue()) + "  >", SDL_Color{255, 190, 30, 255},
                         cx, w.y + 180, Align::Center);
                drawText(renderer, smallFont, "音声オフセット " + formatMs(audioOffset) + " を反映して流しています",
                         SDL_Color{180, 180, 200, 255}, cx, w.y + 270, Align::Center);

                SDL_Color hint = {200, 200, 220, 255};
                drawText(renderer, font, "破裂が音より 遅れて 見える → →", hint, cx, w.y + 380, Align::Center);
                drawText(renderer, font, "破裂が音より 早く 見える   → ←", hint, cx, w.y + 450, Align::Center);

                drawText(renderer, smallFont, "ぴったり重なって見えたら ENTER", SDL_Color{150, 150, 170, 255},
                         cx, w.y + w.h - 50, Align::Center);
            }
            else if(step == Step::Audio){
                const char* label = "音声オフセット";
                double value = currentValue();
                drawText(renderer, font, label, SDL_Color{255, 255, 255, 255}, cx, w.y + 70, Align::Center);
                drawText(renderer, bigFont, formatMs(value), SDL_Color{255, 190, 30, 255}, cx, w.y + 180, Align::Center);

                std::string detail;
                if(taps.count() > 0){
                    detail = "叩いた " + std::to_string(taps.count()) + " 回の中央値 " + formatMs(taps.median());
                }
                else{
                    detail = "まだ叩いていません（今の設定 " + formatMs(audioOffset) + "）";
                }
                if(std::fabs(nudge) >= 0.5) detail += "  ／  微調整 " + formatMs(nudge);
                drawText(renderer, smallFont, detail, SDL_Color{180, 180, 200, 255}, cx, w.y + 270, Align::Center);

                drawErrorGraph(renderer, smallFont, taps, w.x + 60, w.y + 420, w.w - 120, value - nudge);
            }
            else{
                // 確認：2つの値を並べ、↑↓ で選んだほうを ←→ で動かせる
                const char* labels[2] = {"音声オフセット", "表示オフセット"};
                double values[2] = {audioOffset, visualOffset};
                for(int i = 0; i < 2; i++){
                    bool sel = (i == confirmTarget);
                    SDL_Rect row = {w.x + 40, w.y + 40 + i * 100, w.w - 80, 84};
                    drawFrame(renderer, row,
                              sel ? SDL_Color{45, 45, 80, 210} : SDL_Color{30, 30, 50, 170},
                              sel ? SDL_Color{0, 255, 255, 255} : SDL_Color{110, 110, 150, 255},
                              sel ? 3 : 1);
                    drawText(renderer, font, labels[i], sel ? SDL_Color{0, 255, 255, 255} : SDL_Color{255, 255, 255, 255},
                             row.x + 30, row.y + row.h / 2, Align::Left);
                    std::string v = formatMs(values[i]);
                    if(sel) v = "<  " + v + "  >";
                    drawText(renderer, font, v, SDL_Color{255, 190, 30, 255}, row.x + row.w - 30, row.y + row.h / 2, Align::Right);
                }

                std::string result = (taps.count() > 0)
                    ? "ズレの中央値 " + formatMs(taps.median()) + "（" + std::to_string(taps.count()) + " 回）"
                    : "叩いてズレを確認してください";
                SDL_Color rc = (taps.count() > 0) ? errorColor(taps.median()) : SDL_Color{180, 180, 200, 255};
                drawText(renderer, font, result, rc, cx, w.y + 290, Align::Center);

                drawErrorGraph(renderer, smallFont, taps, w.x + 60, w.y + 430, w.w - 120, taps.median());

                drawText(renderer, smallFont, "遅い（+）なら音声オフセットを + に、早い（-）なら - に",
                         SDL_Color{150, 150, 170, 255}, cx, w.y + w.h - 70, Align::Center);
                drawText(renderer, smallFont, "ENTER で保存して終了", SDL_Color{150, 150, 170, 255},
                         cx, w.y + w.h - 35, Align::Center);
            }
        }

        // 5. 操作方法
        const char* help =
            (step == Step::Confirm) ? "叩く: W E R U I O / Space    上下: 調整する値    左右: ±1ms（Shift: ±10ms）    Backspace: 記録を消す    ENTER: 保存    ESC: 戻る" :
            (step == Step::Visual)  ? "左右: ±1ms（Shift: ±10ms）    Backspace: 元の値に戻す    ENTER: 決定して次へ    ESC: 戻る" :
                                      "叩く: W E R U I O / Space    左右: ±1ms（Shift: ±10ms）    Backspace: やり直し    ENTER: 決定して次へ    ESC: 戻る";
        drawText(renderer, smallFont, help, SDL_Color{130, 130, 150, 255}, SW / 2, SH - 50, Align::Center);

        SDL_RenderPresent(renderer);
        g_framePacer.endFrame(window);
    }

    metro.destroy();
    TTF_CloseFont(bigFont);
    TTF_CloseFont(font);
    TTF_CloseFont(smallFont);
    return nextScene;
}