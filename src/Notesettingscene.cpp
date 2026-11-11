// =====================================================================
//  ノーツの詳細設定  src/NoteSettingScene.cpp
//
//  ■ 何をする画面か
//    プレイ画面（PlayScene.cpp と同じ描き方：renderGamePlayScreen）を出しながら、
//    左のウィンドウでノーツ速度・レーンの傾きなどを変えると、その場で見た目に反映される。
//    120 BPM のメトロノームが鳴り続け、2分音符（1000ms ごと）の単発ノーツがずっと流れてくる。
//
//  ■ 操作
//    ↑↓        : 項目を選ぶ
//    ←→        : 値を変える（Shift を押しながらだと大きく変わる）
//    ESC        : 保存して戻る
//    BackSpace  : 変更を取り消して戻る
//    レーンのキー : オートプレイが OFF のとき、自分で叩ける（判定・FAST/LATE も出る）
//
//  ■ 仕組み
//    ・時計：曲は流さず、高精度タイマーの経過時間を「音の時刻（audioMs）」にする。
//            メトロノームはこの時刻で鳴らし、ノーツは「音の時刻 − 音声オフセット」（PlayScene と同じ）で動かす。
//            → 音声オフセットを変えると、メトロノームの音とノーツが重なる位置がその場で変わる
//    ・ノーツ：終わりが無いので、先の 12 秒ぶんだけを作っておき、時間が進んだら後ろに足していく。
//            通り過ぎたノーツは erase_some（PlayScene と同じ）が消す
//    ・メトロノームの音：音のファイルを使わず、短い「ピッ」という音をその場で作る（1拍目だけ高い音）
// =====================================================================

#include "GameCommon.h"
#include "Play/PlaySceneManager.h"
#include "Play/Autoplay.h"
#include "Play/LaneTilt.h"
#include "TapSound.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace notesetting{
    // ----- 流れてくる譜面 -----
    inline constexpr double BPM          = 120.0;
    inline constexpr double BEAT_MS      = 60000.0 / BPM;   // 1拍 = 500ms
    inline constexpr int    NOTE_EVERY_BEATS = 2;            // 2分音符 = 2拍ごと = 1000ms ごと
    inline constexpr double LEAD_MS      = 2000.0;           // 画面を開いてから最初の拍までの待ち
    inline constexpr double LOOKAHEAD_MS = 12000.0;          // この先何 ms ぶんのノーツを作っておくか
    inline constexpr int    NOTE_LANES[] = {1, 2, 3, 4};     // ノーツを出すレーン（この順にくり返す。0 と 5 は隠れているレーン）
    inline constexpr double BASE_NOTE_SPEED = 1.8;           // PlayScene と同じ基準の速さ（px/ms）

    // ----- メトロノームの音 -----
    inline constexpr int    CLICK_MS       = 30;             // 音の長さ
    inline constexpr double CLICK_HZ_HIGH  = 1600.0;         // 1拍目
    inline constexpr double CLICK_HZ_LOW   = 1000.0;         // 2〜4拍目
    inline constexpr int    BEATS_PER_BAR  = 4;

    // ----- 左のウィンドウ -----
    inline constexpr SDL_Rect PANEL     = {40, 120, 460, 920};   // 10項目＋説明＋警告が入る高さ（下端 1040）
    inline constexpr int      ROW_TOP   = 120 + 90;
    inline constexpr int      ROW_H     = 66;
    inline constexpr SDL_Color PANEL_FILL   = {10, 10, 20, 215};
    inline constexpr SDL_Color PANEL_BORDER = {0, 200, 255, 255};
}

namespace{

// ---------------------------------------------------------------------
//  メトロノームの音を作る（短いサイン波を、だんだん小さくして消す）
//    Mix_QuickLoad_RAW は渡したメモリをそのまま使うので、buffer は音を使い終わるまで消さないこと
// ---------------------------------------------------------------------
Mix_Chunk* makeClick(std::vector<Uint8>& buffer, double hz, double volume){
    int freq = 0, channels = 0;
    Uint16 format = 0;
    if(Mix_QuerySpec(&freq, &format, &channels) == 0) return nullptr;
    if(format != AUDIO_S16SYS && format != AUDIO_F32SYS) return nullptr;   // ほかの形式は使っていないので作らない

    const int frames = freq * notesetting::CLICK_MS / 1000;
    const int bytesPerSample = (format == AUDIO_S16SYS) ? 2 : 4;
    buffer.assign(static_cast<size_t>(frames) * channels * bytesPerSample, 0);

    for(int i = 0; i < frames; i++){
        const double t   = static_cast<double>(i) / freq;
        const double env = std::exp(-t * 180.0);                         // すぐ小さくなる（「ピッ」と短く聞こえる）
        const double v   = std::sin(2.0 * 3.14159265358979 * hz * t) * env * volume;
        for(int c = 0; c < channels; c++){
            const size_t idx = (static_cast<size_t>(i) * channels + c) * bytesPerSample;
            if(format == AUDIO_S16SYS){
                const Sint16 s = static_cast<Sint16>(std::clamp(v, -1.0, 1.0) * 32000.0);
                std::memcpy(&buffer[idx], &s, sizeof(s));
            }
            else{
                const float f = static_cast<float>(std::clamp(v, -1.0, 1.0));
                std::memcpy(&buffer[idx], &f, sizeof(f));
            }
        }
    }
    return Mix_QuickLoad_RAW(buffer.data(), static_cast<Uint32>(buffer.size()));
}

// ---------------------------------------------------------------------
//  文字の画像のキャッシュ（毎フレーム TTF で作らないように）
// ---------------------------------------------------------------------
struct TextCache{
    struct Item{ SDL_Texture* tex = nullptr; int w = 0, h = 0; };
    SDL_Renderer* renderer = nullptr;
    std::unordered_map<std::string, Item> items;

    const Item& get(TTF_Font* font, const std::string& text, SDL_Color c){
        const std::string key = text + "#" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b)
                              + "@" + std::to_string(reinterpret_cast<uintptr_t>(font));
        auto it = items.find(key);
        if(it != items.end()) return it->second;
        if(items.size() > 400) clear();   // 値を何度も変えて増えすぎたら作り直す

        Item item;
        if(SDL_Surface* s = TTF_RenderUTF8_Blended(font, text.c_str(), c)){
            item.tex = SDL_CreateTextureFromSurface(renderer, s);
            item.w = s->w;
            item.h = s->h;
            SDL_FreeSurface(s);
        }
        return items.emplace(key, item).first->second;
    }

    // x は align に合わせた基準（0 = 左端、1 = 中央、2 = 右端）、y は文字の縦の中心
    void draw(TTF_Font* font, const std::string& text, SDL_Color c, int x, int y, int align){
        const Item& it = get(font, text, c);
        if(!it.tex) return;
        int left = x;
        if(align == 1) left = x - it.w / 2;
        if(align == 2) left = x - it.w;
        SDL_Rect dst = {left, y - it.h / 2, it.w, it.h};
        SDL_RenderCopy(renderer, it.tex, nullptr, &dst);
    }

    void clear(){
        for(auto& kv : items) if(kv.second.tex) SDL_DestroyTexture(kv.second.tex);
        items.clear();
    }
};

// ---------------------------------------------------------------------
//  左のウィンドウの項目
// ---------------------------------------------------------------------
enum Item{
    ITEM_NOTE_SPEED,
    ITEM_LANE_TILT,
    ITEM_LANE_COVER,
    ITEM_KEY_BEAM,
    ITEM_VISUAL_OFFSET,
    ITEM_AUDIO_OFFSET,
    ITEM_FASTLATE,
    ITEM_TIMING_MS,
    ITEM_AUTOPLAY,
    ITEM_METRONOME,
    ITEM_COUNT
};

const char* const ITEM_LABELS[ITEM_COUNT] = {
    "ノーツ速度", "レーンの傾き", "レーンカバー", "キービームの長さ", "表示オフセット", "音声オフセット",
    "FAST/LATE 表示", "±ms 表示", "オートプレイ", "メトロノーム"
};

const char* const ITEM_DESCRIPTIONS[ITEM_COUNT] = {
    "大きいほどノーツが速く流れます",
    "レーンを奥に倒します（0 度 = 傾けない）",
    "レーンの奥の何 % を壁で隠すか（0 % = 隠さない）",
    "押したときの光がレーンの何 % まで届くか（0 % = 出さない）",
    "+ でノーツが早めに表示されます",
    "+ で判定が遅くなります（メトロノームとノーツの重なり方が変わります）",
    "GOOD・BAD のとき FAST / LATE を出します",
    "叩いたズレを ms で出します",
    "OFF にすると、自分のキーで叩けます",
    "120 BPM のメトロノームを鳴らします"
};

}   // namespace

GameScene noteSettingScene(SDL_Window* window, SDL_Renderer* renderer, PlayerSettings& playerSettings){
    using namespace notesetting;

    // 日本語入力（IME）が変換を始めないように止めておく（最後に元に戻す）
    const bool textInputWasActive = (SDL_IsTextInputActive() == SDL_TRUE);
    SDL_StopTextInput();

    PlayerSettings edit = playerSettings;   // この画面で変える設定（ESC で playerSettings に書き戻す）
    bool autoplay = true;
    bool metronomeOn = true;

    // ===== PlayScene と同じ道具をそろえる =====
    int laneWidth = SCREEN_W / 16;
    int startX = SCREEN_W / 2 - (laneWidth * 3);
    int endX = startX + (laneWidth * 4);
    int laneX[6];
    for(int i = 0; i < 6; i++) laneX[i] = startX + laneWidth * i;
    bool laneActive[6] = {false, true, true, true, true, false};
    int judgeY = static_cast<int>(SCREEN_H * (3.0 / 4.0));

    std::vector<Effect> effects;
    std::vector<KeyBeam> keyBeams;
    std::vector<TickParticle> tickParticles;
    JudgeEffect laneJudge[6] = {};
    std::vector<Note> notes;
    bool prevPressed[6] = {false, false, false, false, false, false};
    int comboCount = 0;
    bool isAP = true, isFC = true;

    ScoreTracker scoreTracker;
    scoreTracker.init(1000000);   // ノーツは終わりが無いので、とても大きい数にしておく（スコアはほぼ動かない）
    double visualScore = 0.0;
    int lastDisplayScoreValue = 0;
    int lastCombo = -1;
    ScorePopEffect scorePop;
    ComboPopEffect comboPop;
    SDL_Texture* comboTexture = nullptr;
    SDL_Texture* scoreTexture = nullptr;
    SDL_Rect scoreRect = {(int)(SCREEN_W * (6.0 / 8.0) - 50), 80, 0, 0};
    SDL_Rect comboRect = {(int)(SCREEN_W * (6.0 / 8.0) - 50), (int)SCREEN_H / 3, 0, 0};

    Mix_Chunk* tap_sound   = loadTapSound(edit.tapSound);
    Mix_Chunk* tap_sound_c = loadTapSound(edit.tapSound);
    auto applySeVolume = [&](){
        const int v = static_cast<int>(edit.seVolume * MIX_MAX_VOLUME / 100.0);
        if(tap_sound)   Mix_VolumeChunk(tap_sound, v);
        if(tap_sound_c) Mix_VolumeChunk(tap_sound_c, v);
    };
    applySeVolume();

    std::vector<Uint8> clickHighBuf, clickLowBuf;
    Mix_Chunk* clickHigh = makeClick(clickHighBuf, CLICK_HZ_HIGH, 0.55);
    Mix_Chunk* clickLow  = makeClick(clickLowBuf,  CLICK_HZ_LOW,  0.40);
    for(Mix_Chunk* c : {clickHigh, clickLow}){
        if(c) Mix_VolumeChunk(c, static_cast<int>(edit.seVolume * MIX_MAX_VOLUME / 100.0));
    }
    if(!clickHigh || !clickLow) printf("[ノーツ詳細設定] メトロノームの音を作れませんでした\n");

    TTF_Font* font           = TTF_OpenFont("fonts/prac.ttf", 80);
    TTF_Font* font_init_waku = TTF_OpenFont("fonts/prac.ttf", 60);
    TTF_Font* judgeFont      = TTF_OpenFont("fonts/prac.ttf", 48);
    TTF_Font* rowFont        = TTF_OpenFont("fonts/prac.ttf", 30);
    TTF_Font* smallFont      = TTF_OpenFont("fonts/prac.ttf", 22);
    TTF_Font* timingFont     = TTF_OpenFont("fonts/prac.ttf", 36);
    if(!font || !font_init_waku || !judgeFont || !rowFont || !smallFont || !timingFont){
        printf("[ノーツ詳細設定] フォント読込失敗 %s\n", TTF_GetError());
        for(TTF_Font* f : {font, font_init_waku, judgeFont, rowFont, smallFont, timingFont}) if(f) TTF_CloseFont(f);
        if(tap_sound) Mix_FreeChunk(tap_sound);
        if(tap_sound_c) Mix_FreeChunk(tap_sound_c);
        if(clickHigh) Mix_FreeChunk(clickHigh);
        if(clickLow) Mix_FreeChunk(clickLow);
        if(textInputWasActive) SDL_StartTextInput();
        return GameScene::Select;
    }

    // 判定文字・キーの文字（PlayScene と同じ作り方）
    SDL_Surface* perfect = TTF_RenderUTF8_Blended(judgeFont, "PERFECT!!", {255, 215, 0, 255});
    SDL_Surface* good    = TTF_RenderUTF8_Blended(judgeFont, "GOOD!",     {0, 255, 255, 255});
    SDL_Surface* bad     = TTF_RenderUTF8_Blended(judgeFont, "BAD",       {90, 255, 25, 255});
    SDL_Surface* miss    = TTF_RenderUTF8_Blended(judgeFont, "MISS...",   {100, 100, 100, 255});
    SDL_Surface* waku_init = SDL_CreateRGBSurfaceWithFormat(0, laneWidth * 6, TTF_FontHeight(font_init_waku), 32, SDL_PIXELFORMAT_RGBA32);
    for(int i = 0; i < 6; i++){
        const std::string label = bindingName(edit.laneBindings[i]);
        SDL_Surface* letter = TTF_RenderUTF8_Blended(font_init_waku, label.c_str(), {255, 255, 255, 255});
        if(!letter) continue;
        const float s = std::min(1.0f, (laneWidth - 8.0f) / letter->w);
        const int w = static_cast<int>(letter->w * s), h = static_cast<int>(letter->h * s);
        SDL_Rect dst = {laneWidth * i + laneWidth / 2 - w / 2, 0, w, h};
        SDL_BlitScaled(letter, NULL, waku_init, &dst);
        SDL_FreeSurface(letter);
    }

    Tex tex;
    tex.perfect   = SDL_CreateTextureFromSurface(renderer, perfect);
    tex.good      = SDL_CreateTextureFromSurface(renderer, good);
    tex.bad       = SDL_CreateTextureFromSurface(renderer, bad);
    tex.miss      = SDL_CreateTextureFromSurface(renderer, miss);
    tex.waku_init = SDL_CreateTextureFromSurface(renderer, waku_init);

    Sq sq;
    auto centered = [&](SDL_Surface* s){
        return SDL_Rect{SCREEN_W / 2 - s->w / 2, SCREEN_H / 2 - s->h / 2, s->w, s->h};
    };
    sq.perfect = centered(perfect);
    sq.good    = centered(good);
    sq.bad     = centered(bad);
    sq.miss    = centered(miss);
    sq.waku_init = {startX, (int)(SCREEN_H * (3.0 / 4.0) + 100), waku_init->w, waku_init->h};
    for(SDL_Surface* s : {perfect, good, bad, miss, waku_init}) SDL_FreeSurface(s);
    TTF_CloseFont(judgeFont);

    DigitFont digitFont;
    digitFont.init(renderer, font, " COMBO");

    TimingDisplay timingDisplay;
    timingDisplay.init(renderer, timingFont, edit);

    SpectrumVisualizer visualizer;
    visualizer.attach();

    LaneTilt laneTilt;
    JudgeBoxFx boxFx;

    TextCache text;
    text.renderer = renderer;

    // ===== 時計 =====
    const double perfFreq = static_cast<double>(SDL_GetPerformanceFrequency());
    const Uint64 perfStart = SDL_GetPerformanceCounter();
    auto nowMs = [&](){
        return static_cast<double>(SDL_GetPerformanceCounter() - perfStart) * 1000.0 / perfFreq;
    };

    long long nextClickBeat = 0;     // 次に鳴らす拍の番号
    long long nextNoteIndex = 0;     // 次に作るノーツの番号
    int32_t prevMusicTime = 0;
    double lastMusicTimeExact = -1e18;

    int cursor = 0;
    bool running = true;
    GameScene nextScene = GameScene::Select;
    bool save = true;

    // ノーツを、先の LOOKAHEAD_MS ぶんまで作っておく（時刻順に後ろへ足すので、並び順は崩れない）
    auto refillNotes = [&](double musicTimeMs){
        const double noteEveryMs = BEAT_MS * NOTE_EVERY_BEATS;
        while(nextNoteIndex * noteEveryMs < musicTimeMs + LOOKAHEAD_MS){
            Note n;
            n.lane = NOTE_LANES[nextNoteIndex % static_cast<long long>(std::size(NOTE_LANES))];
            n.targetTime = static_cast<int32_t>(std::lround(nextNoteIndex * noteEveryMs));
            n.type = NoteType::Normal;
            notes.push_back(n);
            nextNoteIndex++;
        }
    };

    // 値を変える（dir = -1 / +1、big = Shift を押している）
    auto changeValue = [&](int dir, bool big){
        switch(cursor){
            case ITEM_NOTE_SPEED:{
                const double step = NOTE_SPEED_RATE_STEP * (big ? 5.0 : 1.0);
                const double next = std::clamp(edit.noteSpeedRate + dir * step, NOTE_SPEED_RATE_MIN, NOTE_SPEED_RATE_MAX);
                edit.noteSpeedRate = std::round(next / NOTE_SPEED_RATE_STEP) * NOTE_SPEED_RATE_STEP;
                break;
            }
            case ITEM_LANE_TILT:
                edit.laneTiltDeg = std::clamp(std::round(edit.laneTiltDeg + dir * LANE_TILT_DEG_STEP * (big ? 5.0 : 1.0)),
                                              LANE_TILT_DEG_MIN, LANE_TILT_DEG_MAX);
                break;
            case ITEM_LANE_COVER:
                edit.laneCoverPercent = std::clamp(edit.laneCoverPercent + dir * (big ? 5 : 1), LANE_COVER_PERCENT_MIN, LANE_COVER_PERCENT_MAX);
                break;
            case ITEM_KEY_BEAM:
                edit.keyBeamPercent = std::clamp(edit.keyBeamPercent + dir * (big ? 5 : 1), KEY_BEAM_PERCENT_MIN, KEY_BEAM_PERCENT_MAX);
                break;
            case ITEM_VISUAL_OFFSET:
                edit.visualOffsetMs = std::clamp(edit.visualOffsetMs + dir * (big ? 10.0 : 1.0), -300.0, 300.0);
                break;
            case ITEM_AUDIO_OFFSET:
                edit.offsetMs = std::clamp(edit.offsetMs + dir * (big ? 10.0 : 1.0), -500.0, 500.0);
                break;
            case ITEM_FASTLATE:
                edit.showFastLate = !edit.showFastLate;
                timingDisplay.showFastLate = edit.showFastLate;
                break;
            case ITEM_TIMING_MS:
                edit.showTimingMs = !edit.showTimingMs;
                timingDisplay.showMs = edit.showTimingMs;
                break;
            case ITEM_AUTOPLAY:
                autoplay = !autoplay;
                break;
            case ITEM_METRONOME:
                metronomeOn = !metronomeOn;
                break;
            default: break;
        }
    };

    auto valueText = [&](int item) -> std::string{
        char buf[64];
        switch(item){
            case ITEM_NOTE_SPEED: std::snprintf(buf, sizeof(buf), "x %.1f", edit.noteSpeedRate); return buf;
            case ITEM_LANE_TILT:
                if(edit.laneTiltDeg < 0.5) return "なし";
                std::snprintf(buf, sizeof(buf), "%.0f 度", edit.laneTiltDeg); return buf;
            case ITEM_LANE_COVER:
                if(edit.laneCoverPercent <= 0) return "なし";
                std::snprintf(buf, sizeof(buf), "%d %%", edit.laneCoverPercent); return buf;
            case ITEM_KEY_BEAM:
                if(edit.keyBeamPercent <= 0) return "なし";
                std::snprintf(buf, sizeof(buf), "%d %%", edit.keyBeamPercent); return buf;
            case ITEM_VISUAL_OFFSET: std::snprintf(buf, sizeof(buf), "%+.0f ms", edit.visualOffsetMs); return buf;
            case ITEM_AUDIO_OFFSET:  std::snprintf(buf, sizeof(buf), "%+.0f ms", edit.offsetMs); return buf;
            case ITEM_FASTLATE:  return edit.showFastLate ? "ON" : "OFF";
            case ITEM_TIMING_MS: return edit.showTimingMs ? "ON" : "OFF";
            case ITEM_AUTOPLAY:  return autoplay ? "ON" : "OFF";
            case ITEM_METRONOME: return metronomeOn ? "ON" : "OFF";
            default: return "";
        }
    };
    auto isLocked = [&](int item){ (void)item; return false; };   // 今は変えられない項目は無い

    while(running){
        // ===== 時刻 =====
        //   audioMs   : メトロノームの時刻（画面を開いてから LEAD_MS 後が 0）
        //   musicTime : ノーツの時刻 = audioMs − 音声オフセット（PlayScene の「曲の位置 − オフセット」と同じ考え方）
        const double audioMs = nowMs() - LEAD_MS;
        double musicTimeExact = audioMs - edit.offsetMs;
        // 音声オフセットを − に変えたときは時刻が進むだけなので問題ない。+ に変えて時刻が戻るのも、この画面では許す
        lastMusicTimeExact = musicTimeExact;
        const int32_t musicTime = static_cast<int32_t>(std::floor(musicTimeExact));

        // ===== メトロノーム =====
        //   拍の時刻（audioMs が拍の倍数）を過ぎたフレームで鳴らす。フレームの間隔ぶん（数 ms）遅れることがある
        while(audioMs >= nextClickBeat * BEAT_MS){
            if(metronomeOn && nextClickBeat >= 0){
                Mix_Chunk* c = (nextClickBeat % BEATS_PER_BAR == 0) ? clickHigh : clickLow;
                if(c) Mix_PlayChannel(-1, c, 0);
            }
            nextClickBeat++;
        }

        refillNotes(musicTimeExact);

        // ===== 入力 =====
        SDL_Event e;
        while(SDL_PollEvent(&e) != 0){
            if(e.type == SDL_QUIT){
                running = false;
                nextScene = GameScene::Shutdown;
            }
            else if(e.type == SDL_KEYDOWN){
                const SDL_Keycode sym = e.key.keysym.sym;
                const bool big = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                const bool isArrow = (sym == SDLK_UP || sym == SDLK_DOWN || sym == SDLK_LEFT || sym == SDLK_RIGHT);
                if(e.key.repeat && !isArrow) continue;

                switch(sym){
                    case SDLK_ESCAPE:    running = false; save = true;  break;
                    case SDLK_BACKSPACE: running = false; save = false; break;
                    case SDLK_UP:    cursor = (cursor + ITEM_COUNT - 1) % ITEM_COUNT; break;
                    case SDLK_DOWN:  cursor = (cursor + 1) % ITEM_COUNT; break;
                    case SDLK_LEFT:  changeValue(-1, big); break;
                    case SDLK_RIGHT: changeValue(+1, big); break;
                    default: break;
                }
            }
        }

        InputDevices::get().refresh();
        const Uint8* keys = SDL_GetKeyboardState(NULL);
        bool currentPressed[7] = {};
        for(int i = 0; i < 6; i++) currentPressed[i] = isBindingPressed(edit.laneBindings[i], keys);

        // ===== 今の設定を、毎フレームそのまま使う（変えたらすぐ見た目に出る） =====
        const double currentNoteSpeed = BASE_NOTE_SPEED * edit.noteSpeedRate;

        GameContext ctx = {
            .renderer = renderer,
            .font = font,
            .scoreTracker = scoreTracker,
            .musicTime = musicTime,
            .isAP = isAP,
            .isFC = isFC,
            .scoreTexture = scoreTexture,
            .comboTexture = comboTexture,
            .scoreRect = scoreRect,
            .comboRect = comboRect,
            .visualScore = visualScore,
            .lastDisplayScoreValue = lastDisplayScoreValue,
            .lastCombo = lastCombo,
            .scorePop = scorePop,
            .comboPop = comboPop,
            .currentPressed = currentPressed,
            .prevPressed = prevPressed,
            .laneJudge = laneJudge,
            .notes = notes,
            .effects = effects,
            .keyBeams = keyBeams,
            .tap_sound = tap_sound,
            .tap_sound_c = tap_sound_c,
            .comboCount = comboCount,
            .startX = startX,
            .endX = endX,
            .judgeY = judgeY,
            .laneWidth = laneWidth,
            .laneActive = laneActive,
            .noteTextureNormal = nullptr,
            .noteTextureDrag = nullptr,
            .noteTextureLane = nullptr,
            .musicTimeExact = musicTimeExact,
            .digitFont = &digitFont,
            .tutorial = nullptr,
            .visualizer = &visualizer,
            .visualOffsetMs = edit.visualOffsetMs,
            .timing = &timingDisplay,
            .timingLog = nullptr,
            .tickParticles = &tickParticles,
            .comboLabel = nullptr,
            .comboLabelW = 0,
            .comboLabelH = 0,
            .laneTilt = &laneTilt,
            .laneTiltDeg = std::clamp(edit.laneTiltDeg, LANE_TILT_DEG_MIN, LANE_TILT_DEG_MAX),
            .laneCoverRatio = edit.laneCoverPercent / 100.0,
            .keyBeamRatio = edit.keyBeamPercent / 100.0
        };

        // ===== 判定（PlayScene と同じ順番） =====
        if(autoplay) applyAutoplay(ctx, prevMusicTime);
        for(int i = 0; i < 6; i++){
            if(ctx.currentPressed[i] && !ctx.prevPressed[i]) keyBeams.push_back({i, ctx.musicTime});
        }
        NoteJudge(ctx, tex, sq);
        erase_some(ctx);

        visualizer.update();
        updateAndRenderScoreTexture(ctx);

        // ===== 描画 =====
        SDL_SetRenderDrawColor(renderer, 155, 155, 155, 255);
        SDL_RenderClear(renderer);
        renderGamePlayScreen(ctx, tex, sq, currentNoteSpeed, laneX, false, &boxFx);

        // 左のウィンドウ
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, PANEL_FILL.r, PANEL_FILL.g, PANEL_FILL.b, PANEL_FILL.a);
        SDL_RenderFillRect(renderer, &PANEL);
        SDL_SetRenderDrawColor(renderer, PANEL_BORDER.r, PANEL_BORDER.g, PANEL_BORDER.b, PANEL_BORDER.a);
        for(int t = 0; t < 3; t++){
            SDL_Rect b = {PANEL.x + t, PANEL.y + t, PANEL.w - t * 2, PANEL.h - t * 2};
            SDL_RenderDrawRect(renderer, &b);
        }
        text.draw(rowFont, "ノーツの詳細設定", SDL_Color{0, 255, 255, 255}, PANEL.x + 24, PANEL.y + 44, 0);

        for(int i = 0; i < ITEM_COUNT; i++){
            const bool selected = (i == cursor);
            const bool locked = isLocked(i);
            SDL_Rect row = {PANEL.x + 16, ROW_TOP + i * ROW_H, PANEL.w - 32, ROW_H - 8};
            SDL_SetRenderDrawColor(renderer, selected ? 45 : 30, selected ? 45 : 30, selected ? 80 : 50, 210);
            SDL_RenderFillRect(renderer, &row);
            if(selected){
                SDL_SetRenderDrawColor(renderer, 0, 255, 255, 255);
                for(int t = 0; t < 2; t++){
                    SDL_Rect b = {row.x + t, row.y + t, row.w - t * 2, row.h - t * 2};
                    SDL_RenderDrawRect(renderer, &b);
                }
            }
            SDL_Color labelColor = locked ? SDL_Color{130, 130, 150, 255}
                                 : selected ? SDL_Color{0, 255, 255, 255} : SDL_Color{255, 255, 255, 255};
            const int cy = row.y + row.h / 2;
            text.draw(rowFont, ITEM_LABELS[i], labelColor, row.x + 16, cy, 0);

            std::string v = valueText(i);
            if(selected && !locked) v = "<  " + v + "  >";
            SDL_Color valueColor = locked ? SDL_Color{130, 130, 150, 255} : SDL_Color{255, 255, 255, 255};
            if(i == ITEM_AUDIO_OFFSET && shouldWarnTapSoundOffset(edit)) valueColor = SDL_Color{255, 170, 60, 255};   // 注意中はオレンジ
            text.draw(rowFont, v, valueColor, row.x + row.w - 16, cy, 2);
        }

        const int descY = ROW_TOP + ITEM_COUNT * ROW_H + 16;
        const char* desc = isLocked(cursor) ? "レーンの傾きが「なし」の間は使いません" : ITEM_DESCRIPTIONS[cursor];
        text.draw(smallFont, desc, SDL_Color{180, 180, 200, 255}, PANEL.x + 24, descY, 0);
        // 🌟 音声オフセットが ±20ms 以上で、ノーツ音が鳴る設定のときは注意を出す（TapSound.h）
        //    ウィンドウの幅が狭いので、設定ウィンドウより短い文にしてある
        if(shouldWarnTapSoundOffset(edit)){
            const SDL_Color warn = {255, 170, 60, 255};
            text.draw(smallFont, "【注意】オフセットが±20ms以上", warn, PANEL.x + 24, descY + 34, 0);
            text.draw(smallFont, "ノーツ音は「なし」がおすすめ", warn, PANEL.x + 24, descY + 62, 0);
        }
        text.draw(smallFont, "↑↓:選択  ←→:変更（Shift で大きく）", SDL_Color{130, 130, 150, 255}, PANEL.x + 24, PANEL.y + PANEL.h - 64, 0);
        text.draw(smallFont, "ESC:保存して戻る  BackSpace:取り消して戻る", SDL_Color{130, 130, 150, 255}, PANEL.x + 24, PANEL.y + PANEL.h - 32, 0);

        SDL_RenderPresent(renderer);
        g_framePacer.endFrame(window);

        prevMusicTime = ctx.musicTime;
    }

    // ===== 後片付け =====
    if(save){
        // この画面で変えた項目だけを書き戻す（キー設定など、ここで触らない項目はそのまま）
        playerSettings.noteSpeedRate   = edit.noteSpeedRate;
        playerSettings.laneTiltDeg     = edit.laneTiltDeg;
        playerSettings.laneCoverPercent = edit.laneCoverPercent;
        playerSettings.keyBeamPercent  = edit.keyBeamPercent;
        playerSettings.visualOffsetMs  = edit.visualOffsetMs;
        playerSettings.offsetMs        = edit.offsetMs;
        playerSettings.showFastLate    = edit.showFastLate;
        playerSettings.showTimingMs    = edit.showTimingMs;
        savePlayerSettings(playerSettings);
    }

    visualizer.detach();
    Mix_HaltChannel(-1);   // 鳴っているメトロノーム・打鍵音を止めてから音を消す
    text.clear();
    timingDisplay.destroy();
    digitFont.destroy();
    laneTilt.destroy();
    for(SDL_Texture* t : {tex.perfect, tex.good, tex.bad, tex.miss, tex.waku_init}) if(t) SDL_DestroyTexture(t);
    for(TTF_Font* f : {font, font_init_waku, rowFont, smallFont, timingFont}) if(f) TTF_CloseFont(f);
    if(tap_sound) Mix_FreeChunk(tap_sound);
    if(tap_sound_c) Mix_FreeChunk(tap_sound_c);
    if(clickHigh) Mix_FreeChunk(clickHigh);
    if(clickLow) Mix_FreeChunk(clickLow);
    if(textInputWasActive) SDL_StartTextInput();

    (void)lastMusicTimeExact;
    return nextScene;
}