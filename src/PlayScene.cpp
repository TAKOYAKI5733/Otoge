/*g++ -std=c++20 main.cpp PlayScene.cpp -o otoge -lSDL2 -lSDL2_ttf -lSDL2_mixerでコンパイル可*/
#include "GameCommon.h"
#include "Play/PauseMenu.h"
#include "Play/PlaySceneManager.h"
#include "Play/Autoplay.h"
#include "TapSound.h"
#include "Play/LongHoldSound.h"
#include "Play/JudgeStatsPanel.h"   // 右の JUDGE ウィンドウ（判定の数をリアルタイムに出す）
#include "Play/RankGaugePanel.h"    // 右上の RANK ウィンドウ（今の評価とスコアのバー）
#include "Play/LaneTilt.h"          // レーンを奥に倒して描く（設定の「レーンの傾き」）
#include "Play/PreciseInput.h"      // キーを押した瞬間の時刻を、別スレッドで約 1ms ごとに記録する
#include "Play/PlayBackground.h"    // 背景：藍色を含んだ灰色 ＋ 真ん中にぼかしたジャケット
#include "Play/CubeWallet.h"        // ゲーム内の通貨「Cube」の保存と読み込み
#include "Play/GlitchLayer.h"       // ポーズメニューをグリッチで出す
#include "Play/PlayInfoHud.h"       // 左下の難易度・右下の曲名（ホログラム）
#include "Play/BSpeedEvent.h"       // B_SpeedEvent（速度で動かすノーツの速さ）
#include "Play/FalseNotes.h"        // false ノーツ（見た目だけのノーツ）

//グローバル変数
double bpm = 120;

struct PlayInputGuard{
    PlayInputGuard(){
        cursorWasShown_ = (SDL_ShowCursor(SDL_QUERY) != 0);
        textInputWasActive_ = (SDL_IsTextInputActive() == SDL_TRUE);

        SDL_ShowCursor(SDL_DISABLE);   // ウィンドウの中にあるときだけポインターが消える
        SDL_StopTextInput();           // 日本語入力(IME)の変換を止める
    }

    ~PlayInputGuard(){
        SDL_ShowCursor(cursorWasShown_ ? SDL_ENABLE : SDL_DISABLE);
        if(textInputWasActive_) SDL_StartTextInput();
    }

    PlayInputGuard(const PlayInputGuard&) = delete;
    PlayInputGuard& operator=(const PlayInputGuard&) = delete;

private:
    bool cursorWasShown_ = true;
    bool textInputWasActive_ = false;
};

//playGame関数の制作
GameScene playGame(SDL_Window* window, SDL_Renderer* renderer, const std::string& selectedScorePath, int selectedDifficulty, ResultData& outResult, PlayerSettings& playerSettings, bool isAutoplay, SDL_Texture* targetTex, TutorialOverlay* tutorial, const PlayTestOptions* test){
    PlayInputGuard inputGuard;

    SDL_Texture* noteTextureNormal = IMG_LoadTexture(renderer, "images/normal.png");
    SDL_Texture* noteTextureDrag   = IMG_LoadTexture(renderer, "images/drag.png");
    SDL_Texture* noteTextureLane   = IMG_LoadTexture(renderer, "images/lane.png");

    if(!noteTextureNormal || !noteTextureDrag || !noteTextureLane){
        printf("ノーツ画像の読込失敗: %s\n", IMG_GetError());
    }

    //変数定義
    std::vector<Effect> effects;
    std::vector<KeyBeam> keyBeams;
    std::vector<TickParticle> tickParticles;
    JudgeEffect laneJudge[6] = {};
    JudgeEffect lastTimedJudge = {};   // 最後に叩いた判定（±ms の表示を残すため。GameContext.h）
    std::vector<Note> notes;
    std::string bgmName;
    std::vector<A_SpeedEvent> speedEvents;      // A_SpeedEvent：座標の式の速さ（前の SpeedEvent）
    std::vector<B_SpeedEvent> bSpeedEvents;     // B_SpeedEvent：速度の式の倍率（Play/BSpeedEvent.h）
    ScrollMap scrollMap;                        // B_SpeedEvent から作るスクロール位置 S(t)
    std::vector<FalseNote> falseNotes;          // false ノーツ（見た目だけ・判定なし。Play/FalseNotes.h）

    double noteSpeed = 1.8;
    bool prevPressed[6] = {false, false, false, false, false, false};
    double offsetMs = 0;
    int laneWidth = SCREEN_W / 16;
    int startX = SCREEN_W / 2 - (laneWidth * 3);
    int endX = startX + (laneWidth * 4);

    int laneX[6] = {
        startX + laneWidth * 0,
        startX + laneWidth * 1,
        startX + laneWidth * 2,
        startX + laneWidth * 3,
        startX + laneWidth * 4,
        startX + laneWidth * 5
    };

    bool laneActive[6] = {
        false,
        true,
        true,
        true,
        true,
        false
    };

    int judgeY = static_cast<int>(SCREEN_H * (3.0 / 4.0));
    int comboCount = 0;
    bool isAP = true;
    bool isFC = true;

    ScoreTracker scoreTracker;
    int lastScore = -1;

    double visualScore = 0.0;
    int lastDisplayScoreValue = -1;
    int lastCombo = -1;
    ScorePopEffect scorePop;

    ComboPopEffect comboPop;

    //反例処理 + 定義
    if(!loadScore(selectedScorePath, bgmName, notes, speedEvents, bpm, offsetMs, selectedDifficulty)){
        return GameScene::Select;
    }
    // B_SpeedEvent（無い譜面では空のまま → スクロール位置は時刻そのまま＝今まで通りの動き）
    loadBSpeedEvents(selectedScorePath, selectedDifficulty, bSpeedEvents);
    //   B_SpeedEvent の値は A_SpeedEvent と同じ単位：今のノーツ速度（プレイヤーの速度設定を掛ける前の値）を「ふつう」にする
    scrollMap.build(bSpeedEvents, noteSpeed);
    // false ノーツ（無い譜面では空のまま）
    loadFalseNotes(selectedScorePath, selectedDifficulty, falseNotes);

    offsetMs += playerSettings.offsetMs;

        // ===== テストプレイ：途中から始める =====
    const int32_t testStartMs = (test != nullptr) ? std::max<int32_t>(0, test->startMs) : 0;
    if(testStartMs > 0){
        // 開始位置より前のレーンノーツの開閉を、先に済ませておく（W/O の開閉が途中から正しくなる）
        for(const auto& n : notes){
            if(n.targetTime >= testStartMs || n.type != NoteType::Lane) continue;
            for(int l = n.lane; l < n.lane + n.widthLanes; l++){
                if(l >= 0 && l < 6) laneActive[l] = !laneActive[l];
            }
        }
        // 開始位置より前のノーツは消す（コンボ・スコアの満点もこの後で数え直される）
        std::erase_if(notes, [&](const Note& n){ return n.targetTime < testStartMs; });
    }

    int theoreticaMaxCombo = 0;
    for(const auto& note : notes){
        if(note.lane < 0){
            continue;
        }

        if(note.type == NoteType::Long){
            theoreticaMaxCombo += 2;
        }
        else{
            theoreticaMaxCombo++;
        }
    }
    scoreTracker.init(theoreticaMaxCombo);
    int totalTicks = 0;
    for(const auto& note : notes){
        if(note.lane >= 0) totalTicks += static_cast<int>(note.tickTimes.size());
    }
    scoreTracker.setTotalCombo(theoreticaMaxCombo + totalTicks);

    // ===== プレイヤーのノーツ速度（設定の「ノーツ速度」）=====
    //   譜面の速さ（基準の速さ・速度イベント・ノーツごとの速さ）を、全部同じ倍率で速く／遅くする。
    //   全部に同じ倍率を掛けるので、譜面作者が付けた「途中で速くなる」などの変化の割合はそのまま残る。
    //   判定の時刻は変えない（見た目の流れる速さだけが変わる）
    const double speedRate = std::clamp(playerSettings.noteSpeedRate, NOTE_SPEED_RATE_MIN, NOTE_SPEED_RATE_MAX);
    noteSpeed *= speedRate;
    for(auto& ev : speedEvents) ev.targetspeed *= speedRate;
    for(auto& n : notes){
        if(n.hasCustomSpeed) n.customSpeed *= speedRate;
    }
    for(auto& f : falseNotes){
        f.speed *= speedRate;   // false ノーツの進む速さにも、ノーツ速度の設定を掛ける（ふつうのノーツと同じ速さに見えるように）
        if(f.posMode == 1) f.y *= speedRate;   // スナップの高さも、ふつうのノーツの間隔と同じだけ広げる
        f.longPx *= speedRate;                 // ロングの形の長さも、ふつうのロングと同じだけ伸ばす
    }

    double currentNoteSpeed = noteSpeed;

    // ===== レーンの傾き（設定の「レーンの傾き」）=====
    //   描くときに台形に変形するだけなので、ノーツの時刻・判定はそのまま。
    //   チュートリアルは説明の矢印や文字を「傾いていないレーン」に合わせて置いているので、傾けない
    LaneTilt laneTilt;
    const double laneTiltDeg = (tutorial != nullptr) ? 0.0
                             : std::clamp(playerSettings.laneTiltDeg, LANE_TILT_DEG_MIN, LANE_TILT_DEG_MAX);

    std::string bgm_fullPath = "sounds/" + bgmName;
    Mix_Music* bgm = Mix_LoadMUS(bgm_fullPath.c_str());

    if(!bgm){
        printf("音源の読込失敗\n");
        return GameScene::Select;
    }

    Mix_VolumeMusic(static_cast<int>(playerSettings.bgmVolume * MIX_MAX_VOLUME / 100.0));

    Mix_Chunk* tap_sound   = loadTapSound(playerSettings.tapSound);
    Mix_Chunk* tap_sound_c = loadTapSound(playerSettings.tapSound);
    if(!tap_sound || !tap_sound_c){
        printf("効果音読込失敗\n");
    }
    else{
        Mix_VolumeChunk(tap_sound,   static_cast<int>(playerSettings.seVolume * MIX_MAX_VOLUME / 100.0));
        Mix_VolumeChunk(tap_sound_c, static_cast<int>(playerSettings.seVolume * MIX_MAX_VOLUME / 100.0));
    }

    LongHoldSound holdSound;
    holdSound.init("sounds/Long_effect.wav", playerSettings.seVolume);

    //SDL系統処理
    TTF_Font* font = TTF_OpenFont("fonts/prac.ttf", 80);
    TTF_Font* font_init_waku = TTF_OpenFont("fonts/prac.ttf", 60);

    if(!font){
        printf("読込失敗 %s\n", TTF_GetError());
        return GameScene::Select;
    }

    const int JUDGE_FONT_SIZE = 48;
    TTF_Font* judgeFont = TTF_OpenFont("fonts/prac.ttf", JUDGE_FONT_SIZE);
    if(!judgeFont) judgeFont = font;   // 開けなかったときは従来のフォントで代用

    SDL_Surface *sPerfect, *perfect, *good, *bad, *miss, *waku_init;
    sPerfect = renderSPerfectText(judgeFont);   // S-PERFECT：上が PERFECT の色、下が GOOD の色のグラデーション（type.h）
    perfect = TTF_RenderUTF8_Blended(judgeFont, "PERFECT!!", JUDGE_COLOR_PERFECT);
    good    = TTF_RenderUTF8_Blended(judgeFont, "GOOD!",     JUDGE_COLOR_GOOD);
    bad     = TTF_RenderUTF8_Blended(judgeFont, "FAIR",      JUDGE_COLOR_FAIR);   // 前の BAD
    miss    = TTF_RenderUTF8_Blended(judgeFont, "MISS...",   {100, 100, 100, 255});

    {
        std::string keyLabels[6];
        for(int i = 0; i < 6; i++) keyLabels[i] = bindingName(playerSettings.laneBindings[i]);

        waku_init = SDL_CreateRGBSurfaceWithFormat(0, laneWidth * 6, TTF_FontHeight(font_init_waku),
                                                   32, SDL_PIXELFORMAT_RGBA32);
        for(int i = 0; i < 6; i++){
            SDL_Surface* letter = TTF_RenderUTF8_Blended(font_init_waku, keyLabels[i].c_str(), {255, 255, 255, 255});
            if(!letter) continue;
            // 「Left Shift」のような長い名前はレーンの幅に縮める
            const float s = std::min(1.0f, (laneWidth - 8.0f) / letter->w);
            const int w = static_cast<int>(letter->w * s), h = static_cast<int>(letter->h * s);
            SDL_Rect dst = {laneWidth * i + laneWidth / 2 - w / 2, 0, w, h};
            SDL_BlitScaled(letter, NULL, waku_init, &dst);
        }
    }

    if(judgeFont != font) TTF_CloseFont(judgeFont);   // 画像にしたらフォントは不要

    SDL_Texture *comboTexture, *scoreTexture;
    Tex tex;
    tex.sPerfect = sPerfect ? SDL_CreateTextureFromSurface(renderer, sPerfect) : nullptr;
    tex.perfect = SDL_CreateTextureFromSurface(renderer, perfect);
    tex.good = SDL_CreateTextureFromSurface(renderer, good);
    tex.bad = SDL_CreateTextureFromSurface(renderer, bad);
    tex.miss = SDL_CreateTextureFromSurface(renderer, miss);
    tex.waku_init = SDL_CreateTextureFromSurface(renderer, waku_init);

    SDL_Texture* comboLabelTex = nullptr;
    int comboLabelW = 0, comboLabelH = 0;
    if(TTF_Font* lf = TTF_OpenFont("fonts/prac.ttf", 30)){
        const char* word = "COMBO";
        const int spacing = 10;                         // 字の間(px)
        std::vector<SDL_Surface*> glyphs;
        for(const char* c = word; *c; c++){
            char s[2] = {*c, '\0'};
            if(SDL_Surface* g = TTF_RenderUTF8_Blended(lf, s, {255, 255, 255, 255})){
                glyphs.push_back(g);
                comboLabelW += g->w;
                comboLabelH = std::max(comboLabelH, g->h);
            }
        }
        comboLabelW += spacing * std::max(0, static_cast<int>(glyphs.size()) - 1);
        if(SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, comboLabelW, comboLabelH, 32, SDL_PIXELFORMAT_RGBA32)){
            int x = 0;
            for(SDL_Surface* g : glyphs){
                SDL_SetSurfaceBlendMode(g, SDL_BLENDMODE_NONE);   // 透明のまま写す（ふちが黒くならないように）
                SDL_Rect d = {x, 0, g->w, g->h};
                SDL_BlitSurface(g, NULL, surf, &d);
                x += g->w + spacing;
            }
            comboLabelTex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_FreeSurface(surf);
        }
        for(SDL_Surface* g : glyphs) SDL_FreeSurface(g);
        TTF_CloseFont(lf);
    }

    comboTexture = nullptr;
    scoreTexture = nullptr;

    SDL_Rect scoreRect = {(int)(SCREEN_W * (6.0 / 8.0) - 50), 80, 0, 0};
    SDL_Rect comboRect = {(int)(SCREEN_W * (6.0 / 8.0) - 50), (int)SCREEN_H / 3, 0, 0};

    Sq sq;
    // 判定文字：画面の横中央、縦も中央あたりに置く
    const int JUDGE_TEXT_CENTER_Y = SCREEN_H / 2;
    auto centered = [&](SDL_Surface* s){
        return SDL_Rect{SCREEN_W / 2 - s->w / 2, JUDGE_TEXT_CENTER_Y - s->h / 2, s->w, s->h};
    };
    sq.sPerfect = sPerfect ? centered(sPerfect) : centered(perfect);
    sq.perfect = centered(perfect);
    sq.good    = centered(good);
    sq.bad     = centered(bad);
    sq.miss    = centered(miss);

        // 画像の左端を一番左のレーン（W）の左端にそろえる
    sq.waku_init = {startX, (int)(SCREEN_H * (3.0 / 4.0) + 100), waku_init->w, waku_init->h};

    if(sPerfect) SDL_FreeSurface(sPerfect);
    SDL_FreeSurface(perfect);
    SDL_FreeSurface(good);
    SDL_FreeSurface(bad);
    SDL_FreeSurface(miss);
    SDL_FreeSurface(waku_init);

    // ==========================================
    // 高精度タイマー（SDL_GetTicksの1ms単位ではなく、サブミリ秒単位）
    // ==========================================
    const double perfFreq = static_cast<double>(SDL_GetPerformanceFrequency());
    const Uint64 perfStart = SDL_GetPerformanceCounter();
    auto nowMs = [&](){
        return static_cast<double>(SDL_GetPerformanceCounter() - perfStart) * 1000.0 / perfFreq;
    };

    // ===== 始まりの演出 =====
    //   時間はシーンが始まってから（globalMs − sceneStartMs。ポーズ中は止まる）
    //   0 〜 LANE_FRAME_MS                         ：レーンの外枠の線が、判定ラインの所から上下・左右へ伸びる
    //   LANE_GLITCH_START 〜 ＋LANE_GLITCH_MS       ：レーン（背景・区切り線・判定枠・ノーツ）がグリッチで出てくる
    //   HUD_START 〜 ＋HUD_MS                       ：左下の難易度・右下の曲名がグリッチで出てくる
    //   レーンが出きってから LANE_LINE_ALL_MS        ：区切り線を 7 本とも出しておく（drawFrame.h）
    //   その後 LANE_LINE_OUT_INTRO_MS                ：要らない区切り線がグリッチで消える（drawFrame.h）
    constexpr double LANE_FRAME_MS     = 320.0;
    constexpr double LANE_GLITCH_START = 260.0;
    constexpr double LANE_GLITCH_MS    = 450.0;
    constexpr double HUD_START         = 650.0;
    constexpr double HUD_MS            = 400.0;

    // ===== 始まってから曲が鳴るまで =====
    //   右上の RANK・JUDGE ウィンドウ（judgestats::ALL_APPEARED_MS で出きる）と、上の始まりの演出が
    //   全部終わった時刻（INTRO_END_MS）から、PLAY_LEAD_AFTER_PANEL_MS 後に曲が始まる
    //     ※ この待ちの間に、最初のノーツが画面の上から流れてくる。短くしすぎると最初のノーツが途中から現れる
    //   チュートリアル・譜面エディタからのテストプレイは、今まで通り（右上のウィンドウが出きってから）にする
    //     （テストプレイは、途中から始めるときの位置合わせが 1.5 秒の待ちを前提にしているため）
    const double PLAY_LEAD_AFTER_PANEL_MS = 820.0;
    const double INTRO_END_MS = std::max({
        static_cast<double>(judgestats::ALL_APPEARED_MS),
        LANE_GLITCH_START + LANE_GLITCH_MS,                                              // レーンが出きる
        HUD_START + HUD_MS,                                                              // 難易度・曲名が出きる
        LANE_GLITCH_START + LANE_GLITCH_MS + LANE_LINE_ALL_MS + LANE_LINE_OUT_INTRO_MS,  // 要らない区切り線が消えきる
    });
    const bool waitAllIntro = (tutorial == nullptr && test == nullptr);
    const double delayTimeMs = (waitAllIntro ? INTRO_END_MS : static_cast<double>(judgestats::ALL_APPEARED_MS))
                             + PLAY_LEAD_AFTER_PANEL_MS;
    const double sceneStartMs = nowMs();   // ウィンドウのアニメーションの基準
    double musicStartTimeMs = sceneStartMs + delayTimeMs;

    double testSeekMs = 0.0;
    if(testStartMs > 0){
        testSeekMs = std::max(0.0, testStartMs + offsetMs - 1500.0);
        musicStartTimeMs -= testSeekMs;
    }

    bool bgmStarted = false;
    double bgmStartedAtMs = 0.0;   // 実際に曲を鳴らし始めた時刻（ずれ直しを最初だけ速くするのに使う）

    double lastDriftCheckMs = 0.0;
    double clockDriftCorrectionMs = 0.0;   // 実際に時刻へ加えている補正量
    double driftTargetMs = 0.0;            // 本来あるべき補正量（ここへ少しずつ近づける）
    double prevGlobalMs = nowMs();
    double lastMusicTimeExact = -1e18;     // 時刻が逆戻りしないようにするための記録

    if(targetTex != nullptr){
        SDL_SetRenderTarget(renderer, targetTex);

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        //レーンなどの枠の描画
        draw_waku_init(renderer, tex, sq, laneActive);

        SDL_DestroyTexture(tex.waku_init);

        SDL_SetRenderTarget(renderer, NULL);

        TTF_CloseFont(font);

        return GameScene::Play;
    }

    // スコア・コンボ用の数字テクスチャ（ここで1回だけ作る）
    DigitFont digitFont;
    if(!digitFont.init(renderer, font, " COMBO")){
        printf("数字テクスチャの作成失敗: %s\n", TTF_GetError());
    }

    // 右の JUDGE ウィンドウ（文字は先にテクスチャにしておく）
    JudgeStatsPanel judgePanel;
    if(!judgePanel.init(renderer)){
        printf("JUDGE ウィンドウのフォント読込失敗: %s\n", TTF_GetError());
    }
    // 右上の RANK ウィンドウ（評価の文字・評価名も先にテクスチャにしておく）
    RankGaugePanel rankPanel;
    if(!rankPanel.init(renderer)){
        printf("RANK ウィンドウのフォント読込失敗: %s\n", TTF_GetError());
    }

    // ===== 処理落ち調査用（原因が分かったら 0 にしてよい） =====
    #define PLAY_PROFILE 1
    #if PLAY_PROFILE
    const double profFreq = static_cast<double>(SDL_GetPerformanceFrequency());
    auto profMs = [&](Uint64 a, Uint64 b){ return static_cast<double>(b - a) * 1000.0 / profFreq; };
    Uint64 profPrevEnd = SDL_GetPerformanceCounter();
    #endif

    SpectrumVisualizer visualizer;
    visualizer.attach();

    // ===== ゲーム内の通貨「Cube」 =====
    //   ノーツを取ると立方体が 1〜2 個出て、テレポートし終わるたびに 1 増える（数えるのは renderGame.h の erase_some）
    //   左上に「今持っている数 ＋ このプレイで集めた数」を出す。保存はこの関数の最後（オートプレイ・テストプレイでは保存しない）
    CubeHud cubeHud;
    cubeHud.base = loadCubeWallet();
    if(TTF_Font* cf = TTF_OpenFont("fonts/prac.ttf", 28)){
        if(SDL_Surface* cs = TTF_RenderUTF8_Blended(cf, "CUBE", {255, 255, 255, 255})){
            cubeHud.label  = SDL_CreateTextureFromSurface(renderer, cs);
            cubeHud.labelW = cs->w;
            cubeHud.labelH = cs->h;
            SDL_FreeSurface(cs);
        }
        TTF_CloseFont(cf);
    }

    // ===== ポーズ =====
    TTF_Font* menuFont  = TTF_OpenFont("fonts/prac.ttf", 44);
    TTF_Font* labelFont = TTF_OpenFont("fonts/prac.ttf", 24);
    PauseMenu pauseMenu(tutorial == nullptr);   // チュートリアルではリトライを出さない
    double pauseStartMs  = 0.0;                 // ポーズした瞬間の実時間
    double pausedTotalMs = 0.0;                 // これまでにポーズしていた時間の合計

    // ===== ポーズメニューの出し方：グリッチ（Play/GlitchLayer.h） =====
    //   開いてから PAUSE_GLITCH_MS の間は、メニュー（暗くする幕も含む）を透明な絵 pauseLayer に描いてから、
    //   横の帯に切ってずらしながら貼る。それを過ぎたら、いつも通り画面に直接描く
    constexpr double PAUSE_GLITCH_MS = 350.0;
    double pauseOpenedMs = -1e9;          // 最後にポーズを開いた実時間
    SDL_Texture* pauseLayer = nullptr;    // 初めてポーズしたときに作る

    // ポーズを始める（ESC と、ウィンドウのフォーカスが外れたときに呼ぶ）
    auto beginPause = [&](double realMs){
        if(pauseMenu.isActive()) return;
        pauseMenu.open(realMs);
        pauseOpenedMs = realMs;
        pauseStartMs = realMs;
        if(bgmStarted) Mix_PauseMusic();
    };

    //描画処理
    bool running = true;
    SDL_Event e;
    GameScene nextScene = GameScene::Select;
    int32_t prevMusicTime = 0;
    JudgeBoxFx boxFx;

    TTF_Font* timingFont = TTF_OpenFont("fonts/prac.ttf", 36);
    TimingDisplay timingDisplay;
    std::vector<TimingSample> timingLog;
    timingDisplay.init(renderer, timingFont, playerSettings);

    // ===== 押した瞬間の時刻を正確に取る（Play/PreciseInput.h） =====
    //   オートプレイのときは使わない。この関数を抜けると（どの return でも）デストラクタでスレッドが止まる
    PreciseInput preciseInput;
    if(!isAutoplay) preciseInput.start(playerSettings.laneBindings);

    // ===== 背景（ジャケットのぼかし） =====
    PlayBackground playBackground;
    playBackground.load(renderer, selectedScorePath);


    // ===== 曲の終わりの演出 =====
    //   曲が終わってノーツも全部なくなったら、すぐリザルトへは行かず、
    //   レーン（背景・区切り線・判定枠）を LANE_OUTRO_MS かけてグリッチで消し（始まりの演出の逆）、
    //   さらに OUTRO_HOLD_MS 待ってからリザルトへ進む
    constexpr double LANE_OUTRO_MS = 500.0;
    constexpr double OUTRO_HOLD_MS = 250.0;
    double outroStartMs = -1.0;   // 終わりの演出を始めた時刻（globalMs）。-1 = まだ

    // ===== 曲の進み具合のバー（画面の上の端） =====
    //   曲の長さに対して、今どこまで進んだかの割合だけ、左から右へ伸びる
    //   枠なし・Cube の色（立方体の辺と同じ白っぽい水色）の半透明の 1 色だけ
    //   曲の長さは、曲のファイルの長さ（SDL_mixer 2.6 以上の Mix_MusicDuration）。
    //   取れないとき（古い SDL_mixer など）は、譜面の最後のノーツ（ロングは終点）の時刻にする
    constexpr int       PROGRESS_BAR_H     = 6;                       // バーの太さ(px)
    constexpr SDL_Color PROGRESS_BAR_COLOR = {200, 250, 255, 110};    // Cube の色（HOLO_FRONT_COLOR）・半透明
    double songLengthMs = 0.0;
#if defined(SDL_MIXER_VERSION_ATLEAST)
#if SDL_MIXER_VERSION_ATLEAST(2, 6, 0)
    songLengthMs = Mix_MusicDuration(bgm) * 1000.0;
#endif
#endif
    if(songLengthMs <= 0.0){
        for(const Note& n : notes){
            songLengthMs = std::max(songLengthMs, static_cast<double>(n.targetTime) + std::max<int32_t>(0, n.durationMs));
        }
    }

    // 左下の難易度・右下の曲名（チュートリアルでは出さない）
    PlayInfoHud infoHud;
    if(tutorial == nullptr) infoHud.init(renderer, selectedScorePath, selectedDifficulty, laneX[0], laneX[0] + laneWidth * 6);

    // ===== 低音に合わせて、プレイ画面全体を少しズームする =====
    //   ・画面を一度 sceneTex（画面と同じ大きさの絵）に描き、それを真ん中を中心に少し大きくして画面に貼る
    //   ・大きさ = 1 + 低音の強さ × BASS_ZOOM_AMOUNT
    //     低音の強さは、ビジュアライザーの一番下の BASS_BANDS 本（約 40〜130Hz：バスドラム・ベースのあたり）の平均。
    //     いつも鳴っている低音で常に大きくならないよう、BASS_ZOOM_THRESHOLD より上の分だけを使う
    //   ・ズームは、大きくなるときは速く、戻るときはゆっくり（BASS_ZOOM_ATTACK_MS / RELEASE_MS）→ ドンッと弾む感じ
    //   ・ポーズメニューはズームしない（文字が揺れて読みにくくならないように）
    constexpr int    BASS_BANDS          = 15;      // 使う帯の数（SpectrumVisualizer の 64 本のうち、下から）
    constexpr float  BASS_ZOOM_THRESHOLD = 0.45f;   // 低音の強さ（0〜1）がこれより上の分だけズームに使う
    constexpr float  BASS_ZOOM_AMOUNT    = 0.05f;  // 一番強いときに、どれだけ大きくするか（0.025 = 2.5%）
    constexpr float  BASS_ZOOM_ATTACK_MS = 15.0f;   // 大きくなる速さ（小さいほど速い）
    constexpr float  BASS_ZOOM_RELEASE_MS = 120.0f; // 戻る速さ（大きいほどゆっくり）
    float bassZoom = 0.0f;                          // 今のズームの強さ（0〜1）
    //   設定の「低音ズーム」が OFF のときは作らない（いつも今まで通り、画面に直接描く）
    SDL_Texture* sceneTex = playerSettings.bassZoom
        ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H) : nullptr;
    if(sceneTex) SDL_SetTextureScaleMode(sceneTex, SDL_ScaleModeLinear);   // 少しだけ大きくするので、なめらかに

    // ===== 低音のズームに合わせて、画面の端だけを少しぼかす =====
    //   ・ぼかし方（ズームブラー）：sceneTex を「少しずつ大きさを変えて」BASS_BLUR_COPIES 枚重ねた絵を blurTex に作る。
    //     真ん中から離れるほど、大きさの違いで位置が大きくずれるので、端ほど強く流れるようにぼける（真ん中はほとんどずれない）
    //   ・blurTex の透明度を、blurMaskTex（真ん中が透明 → 端が不透明の丸いグラデーション）で置きかえる
    //     → レーンのある真ん中はぼけた絵が乗らず、くっきりのまま（プレイに影響しない）
    //   ・できた blurTex を、ズームの強さ × BASS_BLUR_MAX_ALPHA の濃さで、ズームした画面の上に重ねる
    //   ・ズームがほぼ 0 のときは何もしない（重い処理を毎フレームしない）
    constexpr int   BASS_BLUR_COPIES    = 4;       // 重ねる枚数（多いほどなめらか、少し重くなる）
    constexpr float BASS_BLUR_SPREAD    = 0.035f;  // 一番大きい 1 枚を、どれだけ大きくするか（ズームが最大のとき。0.035 = 3.5%）
    constexpr float BASS_BLUR_INNER     = 0.45f;   // 真ん中からこの割合までは、ぼかさない（1.0 = 画面の端）
    constexpr float BASS_BLUR_OUTER     = 1.05f;   // ここで一番強いぼかしになる（画面の角は 1.0 より外）
    constexpr Uint8 BASS_BLUR_MAX_ALPHA = 230;     // ズームが最大のときの、ぼかしの濃さ（0〜255）
    SDL_Texture* blurTex     = nullptr;
    SDL_Texture* blurMaskTex = nullptr;
    // 透明度だけを置きかえる混ぜ方：色はそのまま（dst × 1）、透明度は描くもの（src × 1）
    const SDL_BlendMode BLEND_REPLACE_ALPHA = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ONE,  SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD);
    if(sceneTex){
        blurTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);
        if(blurTex){
            SDL_SetTextureScaleMode(blurTex, SDL_ScaleModeLinear);
            SDL_SetTextureBlendMode(blurTex, SDL_BLENDMODE_BLEND);
        }
        // マスク：小さく作って、なめらかに引き伸ばす（真ん中からの距離は、画面の縦横の半分を 1 として測る＝横長の楕円）
        constexpr int MW = 192, MH = 108;
        if(SDL_Surface* m = SDL_CreateRGBSurfaceWithFormat(0, MW, MH, 32, SDL_PIXELFORMAT_RGBA32)){
            Uint8* px = static_cast<Uint8*>(m->pixels);
            for(int y = 0; y < MH; y++){
                for(int x = 0; x < MW; x++){
                    const float dx = (x + 0.5f) / MW * 2.0f - 1.0f;
                    const float dy = (y + 0.5f) / MH * 2.0f - 1.0f;
                    const float r  = std::sqrt(dx * dx + dy * dy);
                    float t = std::clamp((r - BASS_BLUR_INNER) / (BASS_BLUR_OUTER - BASS_BLUR_INNER), 0.0f, 1.0f);
                    t = t * t * (3.0f - 2.0f * t);   // なめらかに 0 → 1
                    Uint8* d = px + y * m->pitch + x * 4;
                    d[0] = d[1] = d[2] = 255;
                    d[3] = static_cast<Uint8>(t * 255.0f);
                }
            }
            blurMaskTex = SDL_CreateTextureFromSurface(renderer, m);
            SDL_FreeSurface(m);
            if(blurMaskTex){
                SDL_SetTextureScaleMode(blurMaskTex, SDL_ScaleModeLinear);
                // この混ぜ方が使えない環境では、ぼかし自体をやめる（そのまま重ねると画面の端が白くなるため）
                if(SDL_SetTextureBlendMode(blurMaskTex, BLEND_REPLACE_ALPHA) != 0){
                    printf("画面の端のぼかしは使えません: %s\n", SDL_GetError());
                    SDL_DestroyTexture(blurMaskTex);
                    blurMaskTex = nullptr;
                }
            }
        }
    }

    // ===== 押しているロングの左右の割合で、レーンを左右に傾ける =====
    //   ・押しているロング（ホールド中）ごとに「真ん中からの距離 d」（-3 〜 +3、- が左）を求める
    //     （d = ロングの中心のレーン位置 − 3。レーン 0 なら -2.5、レーン 5 なら +2.5）
    //   ・割合 = Σ(d × 幅) ÷ Σ(3 × 幅) → -1 〜 +1。端のロングほど d が大きいので、強く傾く
    //     （真ん中寄りのロングは少しだけ、端のロングは大きく傾ける。押していなければ 0）
    //   ・角度 = 割合 × LANE_ROLL_MAX_DEG。レーンの中心線を軸に、右が多いと右側が奥へ、左が多いと左側が奥へ下がる
    //   ・急に傾かないように、LANE_ROLL_SMOOTH_MS でなめらかに近づける
    //   ・チュートリアル・設定の「レーンの左右の傾き」が OFF のときは傾けない
    constexpr double LANE_ROLL_MAX_DEG   = 24.0;    // 一番傾いたときの角度（度）。奥行きのある傾きなので、平面の回転より大きめ
    constexpr double LANE_ROLL_SMOOTH_MS = 100.0;   // 近づく速さ（大きいほどゆっくり）
    double laneRollDeg = 0.0;
    //   laneLayerTex は、始まりの演出（レーンをグリッチで出す）でも使うので、設定に関係なくいつも作る
    //   （左右に傾けるかどうかは、下の laneRollEnabled で決める）
    const bool laneRollEnabled = playerSettings.laneRoll && tutorial == nullptr;
    SDL_Texture* laneLayerTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                                  SCREEN_W, SCREEN_H + LANE_ROLL_OVERDRAW_PX * 2);   // 上下に余分に描く分だけ高く（GameContext.h）
    if(laneLayerTex){
        SDL_SetTextureScaleMode(laneLayerTex, SDL_ScaleModeLinear);   // 回したときにふちがギザギザにならないように
        // 中身は「透明度を掛けた色」で入っているので、その前提で重ねる（そのまま BLEND で貼ると、半透明の所が暗くなる）
        SDL_SetTextureBlendMode(laneLayerTex, SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
            SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD));
    }
    bool preciseLanes[6] = {};
    for(int i = 0; i < 6; i++) preciseLanes[i] = preciseInput.lanePrecise(i);

    while(running){

        #if PLAY_PROFILE
        Uint64 prof0 = SDL_GetPerformanceCounter();
        #endif

        double realMs = nowMs();
        double globalMs = pauseMenu.isActive() ? (pauseStartMs - pausedTotalMs) : (realMs - pausedTotalMs);
        double frameDtMs = globalMs - prevGlobalMs;
        prevGlobalMs = globalMs;

        if(!bgmStarted && globalMs >= musicStartTimeMs + testSeekMs){
            Mix_PlayMusic(bgm, 1);
            if(testSeekMs > 0.0) Mix_SetMusicPosition(testSeekMs / 1000.0);   // 途中から鳴らす
            bgmStarted = true;

            // 🌟 実際に鳴らし始めた瞬間を、曲の時計の基準にする
            //   開始の予定時刻を過ぎた「最初のフレーム」で鳴らすので、予定より最大で1フレーム（60fps なら 16ms）遅れて鳴る。
            //   前は予定時刻のまま数えていたので、その分だけ曲と判定がずれていた（ずれ直しで数秒かけて合っていた）。
            //   基準を今に合わせると時刻が少し戻るが、下の「逆戻り防止」でその間ノーツは止まるだけ（曲が始まる前なので見えない）
            const double now = nowMs() - pausedTotalMs;
            musicStartTimeMs = now - testSeekMs;
            lastDriftCheckMs = now;
            bgmStartedAtMs = now;
        }

        // ① 「本来あるべき補正量」だけを更新する
        //   曲の頭の 3 秒は 100ms ごと・ずれの 50% ずつ（早く合わせる）、その後は 200ms ごと・20% ずつ（ぶれないように）
        //   Mix_GetMusicPosition は音を送るまとまり（256 サンプル ≒ 6ms）ごとにしか進まないので、3ms 以下のずれは直さない
        //   曲が鳴り終わった後は Mix_GetMusicPosition がもう進まない（曲の頭や終わりのまま）ので、合わせない（合わせると時刻が止まり、曲の後のノーツが流れてこない）
        const bool earlyPhase = bgmStarted && (globalMs - bgmStartedAtMs) < 3000.0;
        if(bgmStarted && Mix_PlayingMusic() && !Mix_PausedMusic() && (globalMs - lastDriftCheckMs) >= (earlyPhase ? 100.0 : 200.0)){
            lastDriftCheckMs = globalMs;

            double actualPosMs = Mix_GetMusicPosition(bgm) * 1000.0;
            if(actualPosMs >= 0.0){
                double predictedRawMs = (globalMs - musicStartTimeMs) + driftTargetMs;
                double drift = actualPosMs - predictedRawMs;

                if(std::abs(drift) > 3.0){
                    driftTargetMs += drift * (earlyPhase ? 0.5 : 0.2);
                }
            }
        }

        // ② 実際の補正は毎フレーム少しずつ（経過時間の3%まで）近づける
        //    → 時刻が一気に飛ぶ／戻ることがなくなり、ノーツが止まって見えない
        {
            double diff = driftTargetMs - clockDriftCorrectionMs;
            if(std::abs(diff) > 50.0){
                clockDriftCorrectionMs = driftTargetMs;   // 大きすぎるズレは一度で合わせる
            }
            else{
                double maxStep = frameDtMs * 0.03;
                clockDriftCorrectionMs += std::clamp(diff, -maxStep, maxStep);
            }
        }

        double rawElapsedMs = (globalMs - musicStartTimeMs) + clockDriftCorrectionMs;
        double musicTimeExact = rawElapsedMs - offsetMs;                            // 描画用（小数ms）
        if(musicTimeExact < lastMusicTimeExact) musicTimeExact = lastMusicTimeExact; // 逆戻り防止
        lastMusicTimeExact = musicTimeExact;
        int32_t musicTime = static_cast<int32_t>(std::floor(musicTimeExact));       // 判定用は従来通り整数ms

        // タイムベース・スピードイベント処理システム
        updateNoteSpeed(musicTime, noteSpeed, currentNoteSpeed, speedEvents);

        while(SDL_PollEvent(&e) != 0){
            if(e.type == SDL_QUIT){
                running = false;
                nextScene = GameScene::Shutdown;
            }
            else if(e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST){
                // 別のウィンドウに切り替えたら自動でポーズ
                //   配信モードのときは、OBS を操作しただけで止まらないように、自動ではポーズしない
                if(!playerSettings.streamMode) beginPause(realMs);
            }
            else if(e.type == SDL_KEYDOWN){
                if(!pauseMenu.isActive()){
                    if(e.key.keysym.sym == SDLK_ESCAPE && !e.key.repeat) beginPause(realMs);
                }
                else{
                    PauseAction action = pauseMenu.handleKey(e.key, realMs);
                    if(action == PauseAction::Retry){
                        running = false;
                        nextScene = GameScene::Play;     // main.cpp が同じ曲・難易度で playGame を呼び直す
                    }
                    else if(action == PauseAction::Retire){
                        running = false;
                        nextScene = GameScene::Select;
                    }
                }
            }
        }

        // カウントダウンが終わったフレームで、ゲーム内時間と曲を再開する
        if(pauseMenu.updateCountdown(realMs)){
            pausedTotalMs += realMs - pauseStartMs;
            if(bgmStarted) Mix_ResumeMusic();
        }
        bool paused = pauseMenu.isActive();

        InputDevices::get().refresh();   // コントローラーの抜き差しに合わせる
        const Uint8* currentKeyState = SDL_GetKeyboardState(NULL);
        bool currentPressed[7] = {};
        for(int i = 0; i < 6; i++){
            currentPressed[i] = isBindingPressed(playerSettings.laneBindings[i], currentKeyState);
        }
        currentPressed[6] = (currentKeyState[SDL_SCANCODE_ESCAPE] != 0);

        // ===== 別スレッドが記録した「押した瞬間」を受け取って、曲の時刻に直す =====
        //   曲の時刻 = (実時間 − ポーズしていた時間 − 曲の始まり) + ずれ直し − オフセット（上の musicTimeExact と同じ式）
        //   ・ポーズ中・ほかのウィンドウを触っているときに押したものは捨てる
        //   ・このフレームの時刻より後にはしない（受け取るのは、このフレームまでに押されたものだけ）
        int32_t pressTimes[6];
        {
            Uint64 raw[6] = {};
            preciseInput.takePresses(raw);
            const bool focused = (SDL_GetKeyboardFocus() == window);
            for(int i = 0; i < 6; i++){
                pressTimes[i] = NO_PRESS_TIME;
                if(raw[i] == 0 || paused || !focused) continue;
                const double tReal = static_cast<double>(raw[i] - perfStart) * 1000.0 / perfFreq;
                const double tMusic = (tReal - pausedTotalMs - musicStartTimeMs) + clockDriftCorrectionMs - offsetMs;
                pressTimes[i] = std::min(musicTime, static_cast<int32_t>(std::floor(tMusic)));
            }
        }

        // ===== 押している状態と離した瞬間も、同じ別スレッドから受け取る（ロングのホールド判定用） =====
        //   押した瞬間（上）と同じ所で見た状態を使うので、SDL のキーの状態と食い違ってロングが途切れることがない
        bool preciseHeld[6] = {};
        int32_t releaseTimes[6];
        {
            Uint64 rawRel[6] = {};
            preciseInput.takeReleases(rawRel);
            const bool focused = (SDL_GetKeyboardFocus() == window);
            for(int i = 0; i < 6; i++){
                preciseHeld[i] = preciseLanes[i] && focused && !paused && preciseInput.laneDown(i);
                releaseTimes[i] = NO_PRESS_TIME;
                if(rawRel[i] == 0 || paused || !focused) continue;
                const double tReal = static_cast<double>(rawRel[i] - perfStart) * 1000.0 / perfFreq;
                const double tMusic = (tReal - pausedTotalMs - musicStartTimeMs) + clockDriftCorrectionMs - offsetMs;
                releaseTimes[i] = std::min(musicTime, static_cast<int32_t>(std::floor(tMusic)));
            }
        }

        // レーンの左右の傾き：押しているロングの「真ん中からの距離 × 幅」の割合から角度を決めて、なめらかに近づける
        if(laneLayerTex && laneRollEnabled){
            double sum = 0.0, total = 0.0;
            for(const Note& n : notes){
                if(n.type != NoteType::Long || !n.isHolding) continue;
                const double d = (n.lane + n.widthLanes * 0.5) - 3.0;   // 真ん中からの距離（- = 左、+ = 右）
                sum   += d * n.widthLanes;
                total += 3.0 * n.widthLanes;                            // 一番端（d = ±3）のときに割合が ±1 になる
            }
            const double target = (total > 0.0) ? std::clamp(sum / total, -1.0, 1.0) * LANE_ROLL_MAX_DEG : 0.0;
            const double dt = std::clamp(frameDtMs, 0.0, 100.0);
            laneRollDeg += (target - laneRollDeg) * (1.0 - std::exp(-dt / LANE_ROLL_SMOOTH_MS));
        }

            //GameContextの初期化
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
            .noteTextureNormal = noteTextureNormal,
            .noteTextureDrag = noteTextureDrag,
            .noteTextureLane = noteTextureLane,
            .musicTimeExact = musicTimeExact,    // GameContext.h の最後に追加したメンバ
            .digitFont = &digitFont,
            .tutorial = tutorial,
            .visualizer = &visualizer,
            .visualOffsetMs = playerSettings.visualOffsetMs,
            .timing = &timingDisplay,
            .timingLog = &timingLog,
            .tickParticles = &tickParticles,
            .comboLabel = comboLabelTex,
            .comboLabelW = comboLabelW,
            .comboLabelH = comboLabelH,
            .laneTilt = &laneTilt,
            .laneTiltDeg = laneTiltDeg,
            .laneCoverRatio = (tutorial != nullptr) ? 0.0 : playerSettings.laneCoverPercent / 100.0,  // チュートリアルでは隠さない
            .keyBeamRatio = playerSettings.keyBeamPercent / 100.0,
            .pressTimes = pressTimes,
            .preciseLanes = preciseLanes,
            .background = &playBackground,
            .laneRollDeg = laneRollDeg,
            .laneLayerTex = laneLayerTex,
            .cubeHud = &cubeHud,
            .preciseHeld = preciseHeld,
            .releaseTimes = releaseTimes,
            .isAutoplay = isAutoplay,
            .scrollMap = scrollMap.empty() ? nullptr : &scrollMap,
            .falseNotes = falseNotes.empty() ? nullptr : &falseNotes
            };
        ctx.lastTimedJudge = &lastTimedJudge;
        {
            const double introMs = globalMs - sceneStartMs;
            ctx.laneFrameIntro = std::clamp(introMs / LANE_FRAME_MS, 0.0, 1.0);
            ctx.laneIntro      = laneLayerTex ? std::clamp((introMs - LANE_GLITCH_START) / LANE_GLITCH_MS, 0.0, 1.0) : 1.0;
            // 終わりの演出の途中：レーンをグリッチで消していく（laneIntro を 1 → 0 に戻す。外枠の線は出さない）
            if(outroStartMs >= 0.0){
                const double outP = std::clamp((globalMs - outroStartMs) / LANE_OUTRO_MS, 0.0, 1.0);
                ctx.laneIntro      = laneLayerTex ? 1.0 - outP : 1.0;
                ctx.laneFrameIntro = 0.0;
                ctx.laneOutro      = true;
            }
        }

        if(paused){
            for(int i = 0; i < 6; i++) prevPressed[i] = currentPressed[i];
        }

        #if PLAY_PROFILE
        Uint64 prof1 = SDL_GetPerformanceCounter();
        #endif

        if(!paused){
            if(isAutoplay) applyAutoplay(ctx, prevMusicTime);

            for(int i = 0; i < 6; i++){
                // 正確に見ているレーンは「押した瞬間」の記録で出す（フレームの間に押して離した速い連打でも光る）
                const bool pressedNow = preciseLanes[i] ? (pressTimes[i] != NO_PRESS_TIME)
                                                        : (ctx.currentPressed[i] && !ctx.prevPressed[i]);
                if(pressedNow){
                    KeyBeam beam;
                    beam.lane = i;
                    beam.spawnTime = ctx.musicTime;
                    ctx.keyBeams.push_back(beam);
                }
            }

            //ノーツの判定処理 noteJudge.h
            NoteJudge(ctx, tex, sq);

            //ノーツの消去処理 renderGame.h
            erase_some(ctx);

            // 曲が終わってノーツも全部なくなった：終わりの演出（レーンをグリッチで消す）を始め、終わったらリザルトへ
            if(!Mix_PlayingMusic() && notes.empty() && nextScene != GameScene::Shutdown){
                if(outroStartMs < 0.0) outroStartMs = globalMs;
                if(globalMs - outroStartMs >= LANE_OUTRO_MS + OUTRO_HOLD_MS){
                    running = false;
                    nextScene = GameScene::Result;
                }
            }
        }

        // JUDGE ウィンドウの数を、今のフレームの判定まで反映する
        judgePanel.update(scoreTracker, timingLog);

        holdSound.update(notes, paused);

        visualizer.update();

        // 低音の強さ → ズームの強さ（大きくなるときは速く、戻るときはゆっくり。フレームレートに関係なく同じ速さ）
        {
            float bass = 0.0f;
            for(int b = 0; b < BASS_BANDS; b++) bass += visualizer.levels[b];
            bass /= BASS_BANDS;
            const float target = playerSettings.bassZoom
                ? std::clamp((bass - BASS_ZOOM_THRESHOLD) / (1.0f - BASS_ZOOM_THRESHOLD), 0.0f, 1.0f) : 0.0f;
            const float dt = static_cast<float>(std::clamp(frameDtMs, 0.0, 100.0));
            const float tau = (target > bassZoom) ? BASS_ZOOM_ATTACK_MS : BASS_ZOOM_RELEASE_MS;
            bassZoom += (target - bassZoom) * (1.0f - std::exp(-dt / tau));
        }

        // ここからポーズメニューの手前までは sceneTex に描く（後でズームして画面に貼る）
        if(sceneTex) SDL_SetRenderTarget(renderer, sceneTex);

        SDL_SetRenderDrawColor(ctx.renderer, 155, 155, 155, 255);
        SDL_RenderClear(ctx.renderer);

        #if PLAY_PROFILE
        Uint64 prof2 = SDL_GetPerformanceCounter();
        #endif

        //スコア・コンボの表示値とアニメーション計算
        updateAndRenderScoreTexture(ctx);

        //ノーツの描画・アニメーション処理（この中で SDL_RenderPresent している）
                renderGamePlayScreen(ctx, tex, sq, currentNoteSpeed, laneX, false, &boxFx);

        // 右の RANK / JUDGE ウィンドウ（ポーズメニューより奥に描く）
        //   時計は globalMs（ポーズ中は止まる）なので、出てくる途中でポーズしてもアニメーションが止まる
        //   RANK は「画面に出しているスコア」（右上の数字のカウントアップ）で評価とバーを決める
        //   → 数字・評価・バーの3つがずれない
        {
            const double sceneMs = globalMs - sceneStartMs;
            rankPanel.update(lastDisplayScoreValue, sceneMs);
            rankPanel.draw(renderer, sceneMs);
            judgePanel.draw(renderer, sceneMs);
        }

        // sceneTex を、画面の真ん中を中心に (1 + ズーム) 倍にして画面に貼る
        if(sceneTex){
            SDL_SetRenderTarget(renderer, nullptr);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            const float scale = 1.0f + BASS_ZOOM_AMOUNT * bassZoom;
            const SDL_FRect dst = {SCREEN_W * 0.5f * (1.0f - scale), SCREEN_H * 0.5f * (1.0f - scale),
                                   SCREEN_W * scale, SCREEN_H * scale};
            SDL_RenderCopyF(renderer, sceneTex, nullptr, &dst);

            // 画面の端のぼかし（ズームしているときだけ）
            if(blurTex && blurMaskTex && bassZoom > 0.01f){
                // ① blurTex に、大きさを少しずつ変えた sceneTex を重ねる（k 枚目は 1/(k+1) の濃さ → 全部の平均になる）
                SDL_SetRenderTarget(renderer, blurTex);
                for(int k = 0; k < BASS_BLUR_COPIES; k++){
                    const float sk = scale * (1.0f + BASS_BLUR_SPREAD * bassZoom * (k + 1) / BASS_BLUR_COPIES);
                    const SDL_FRect d = {SCREEN_W * 0.5f * (1.0f - sk), SCREEN_H * 0.5f * (1.0f - sk),
                                         SCREEN_W * sk, SCREEN_H * sk};
                    SDL_SetTextureBlendMode(sceneTex, k == 0 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
                    SDL_SetTextureAlphaMod(sceneTex, static_cast<Uint8>(255 / (k + 1)));
                    SDL_RenderCopyF(renderer, sceneTex, nullptr, &d);
                }
                SDL_SetTextureBlendMode(sceneTex, SDL_BLENDMODE_BLEND);  // 元に戻す（作ったときの初期値）
                SDL_SetTextureAlphaMod(sceneTex, 255);

                // ② 透明度をマスクで置きかえる（真ん中 = 透明、端 = 不透明）
                SDL_RenderCopy(renderer, blurMaskTex, nullptr, nullptr);

                // ③ 画面に戻って、ズームの強さに合わせた濃さで重ねる
                SDL_SetRenderTarget(renderer, nullptr);
                SDL_SetTextureAlphaMod(blurTex, static_cast<Uint8>(BASS_BLUR_MAX_ALPHA * std::min(bassZoom, 1.0f)));
                SDL_RenderCopy(renderer, blurTex, nullptr, nullptr);
            }
        }

        // 曲の進み具合のバー（画面の上の端。低音のズームの後に描くので、ズームで画面の外に押し出されない）
        if(songLengthMs > 0.0){
            const double progress = std::clamp(musicTimeExact / songLengthMs, 0.0, 1.0);
            const float w = static_cast<float>(SCREEN_W * progress);
            if(w > 0.0f){
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, PROGRESS_BAR_COLOR.r, PROGRESS_BAR_COLOR.g, PROGRESS_BAR_COLOR.b, PROGRESS_BAR_COLOR.a);
                const SDL_FRect bar = {0.0f, 0.0f, w, static_cast<float>(PROGRESS_BAR_H)};
                SDL_RenderFillRectF(renderer, &bar);
            }
        }

        // 左下の難易度・右下の曲名（低音のズームの後に描くので、ズームで画面の外に押し出されない）
        infoHud.draw(renderer, std::clamp((globalMs - sceneStartMs - HUD_START) / HUD_MS, 0.0, 1.0));

        // ポーズメニュー：開いた直後はグリッチで出す
        if(pauseMenu.isActive() && realMs - pauseOpenedMs < PAUSE_GLITCH_MS){
            if(!pauseLayer){
                pauseLayer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);
            }
            if(pauseLayer){
                SDL_Texture* prevTarget = SDL_GetRenderTarget(renderer);
                SDL_SetRenderTarget(renderer, pauseLayer);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                SDL_RenderClear(renderer);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                pauseMenu.draw(renderer, menuFont, font, labelFont, realMs);
                SDL_SetRenderTarget(renderer, prevTarget);
                const SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
                drawGlitchTexture(renderer, pauseLayer, full, (realMs - pauseOpenedMs) / PAUSE_GLITCH_MS);
            }
            else{
                pauseMenu.draw(renderer, menuFont, font, labelFont, realMs);   // 絵が作れない環境：そのまま
            }
        }
        else{
            pauseMenu.draw(renderer, menuFont, font, labelFont, realMs);
        }
        SDL_RenderPresent(renderer);

        #if PLAY_PROFILE
        Uint64 prof3 = SDL_GetPerformanceCounter();
        #endif

        // フレーム間隔の調整とFPS計測（VSync OFF時はここでFPS上限を守る）
        g_framePacer.endFrame(window);

        #if PLAY_PROFILE
        {
            Uint64 prof4 = SDL_GetPerformanceCounter();
            double frameMs = profMs(profPrevEnd, prof4);
            // 120fps(8.3ms)の1.5倍を超えたフレームだけ内訳を表示
            if(frameMs > 12.5 && musicTime > 0){
                printf("[SPIKE] t=%dms frame=%.1fms | 前フレーム終了→開始 %.1f / 時刻・入力 %.1f / 判定 %.1f / 描画+Present %.1f / 待機 %.1f\n",
                       musicTime, frameMs,
                       profMs(profPrevEnd, prof0), profMs(prof0, prof1), profMs(prof1, prof2),
                       profMs(prof2, prof3), profMs(prof3, prof4));
            }
            profPrevEnd = prof4;
        }
        #endif

        prevMusicTime = ctx.musicTime;
    }

    outResult.score = scoreTracker.getScore();
    outResult.maxCombo = scoreTracker.getMaxComboAchieved();
    outResult.isAP = isAP;
    outResult.isFC = isFC;
    outResult.perfectCount = scoreTracker.getPerfectCount();
    outResult.goodCount = scoreTracker.getGoodCount();
    outResult.badCount = scoreTracker.getBadCount();
    outResult.missCount = scoreTracker.getMissCount();
    outResult.timings = timingLog;
    outResult.isAutoplay = isAutoplay;

    visualizer.detach();

    // 集めた Cube を持ち物に足して保存する：曲を最後まで遊んでリザルトへ進んだときだけ
    //   リトライ・リタイア（・遊んでいる途中でゲームを閉じたとき）は、このプレイで集めた分は増えない
    //   オートプレイ・譜面エディタのテストプレイも保存しない
    if(nextScene == GameScene::Result && !isAutoplay && test == nullptr && cubeHud.earned > 0){
        saveCubeWallet(cubeHud.base + cubeHud.earned);
    }
    if(cubeHud.label) SDL_DestroyTexture(cubeHud.label);
    if(sceneTex) SDL_DestroyTexture(sceneTex);
    if(blurTex) SDL_DestroyTexture(blurTex);
    if(blurMaskTex) SDL_DestroyTexture(blurMaskTex);
    if(laneLayerTex) SDL_DestroyTexture(laneLayerTex);
    if(pauseLayer) SDL_DestroyTexture(pauseLayer);
    infoHud.destroy();

    Mix_HaltMusic();
    digitFont.destroy();
    judgePanel.destroy();
    rankPanel.destroy();
    TTF_CloseFont(font);
    TTF_CloseFont(font_init_waku);
    TTF_CloseFont(menuFont);
    TTF_CloseFont(labelFont);
    timingDisplay.destroy();
    if(timingFont) TTF_CloseFont(timingFont);
    Mix_FreeMusic(bgm);
    Mix_FreeChunk(tap_sound);
    Mix_FreeChunk(tap_sound_c);
    holdSound.destroy();
    if(comboTexture) SDL_DestroyTexture(comboTexture);
    if(scoreTexture) SDL_DestroyTexture(scoreTexture);
    if(noteTextureNormal) SDL_DestroyTexture(noteTextureNormal);
    if(noteTextureDrag) SDL_DestroyTexture(noteTextureDrag);
    if(noteTextureLane) SDL_DestroyTexture(noteTextureLane);
    if(comboLabelTex) SDL_DestroyTexture(comboLabelTex);
    if(tex.sPerfect) SDL_DestroyTexture(tex.sPerfect);
    SDL_DestroyTexture(tex.perfect);
    SDL_DestroyTexture(tex.good);
    SDL_DestroyTexture(tex.bad);
    SDL_DestroyTexture(tex.miss);
    SDL_DestroyTexture(tex.waku_init);

    return nextScene;
}