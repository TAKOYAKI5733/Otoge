/*g++ -std=c++20 main.cpp PlayScene.cpp -o otoge -lSDL2 -lSDL2_ttf -lSDL2_mixerでコンパイル可*/
#include "GameCommon.h"
#include "Play/PauseMenu.h"
#include "Play/PlaySceneManager.h"
#include "Play/Autoplay.h"
#include "TapSound.h"

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
    JudgeEffect laneJudge[6] = {};
    std::vector<Note> notes;
    std::string bgmName;
    std::vector<SpeedEvent> speedEvents;

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

    double currentNoteSpeed = noteSpeed;

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

    SDL_Surface *perfect, *good, *bad, *miss, *waku_init;
    perfect = TTF_RenderUTF8_Blended(judgeFont, "PERFECT!!", {255, 215, 0, 255});
    good    = TTF_RenderUTF8_Blended(judgeFont, "GOOD!",     {0, 255, 255, 255});
    bad     = TTF_RenderUTF8_Blended(judgeFont, "BAD",       {90, 255, 25, 255});
    miss    = TTF_RenderUTF8_Blended(judgeFont, "MISS...",   {100, 100, 100, 255});

    {
        const char* keyLabels[6] = {"W", "E", "R", "U", "I", "O"};
        waku_init = SDL_CreateRGBSurfaceWithFormat(0, laneWidth * 6, TTF_FontHeight(font_init_waku),
                                                   32, SDL_PIXELFORMAT_RGBA32);
        for(int i = 0; i < 6; i++){
            SDL_Surface* letter = TTF_RenderUTF8_Blended(font_init_waku, keyLabels[i], {255, 255, 255, 255});
            if(!letter) continue;
            SDL_Rect dst = {laneWidth * i + laneWidth / 2 - letter->w / 2, 0, letter->w, letter->h};
            SDL_BlitSurface(letter, NULL, waku_init, &dst);
            SDL_FreeSurface(letter);
        }
    }

    if(judgeFont != font) TTF_CloseFont(judgeFont);   // 画像にしたらフォントは不要
    
    SDL_Texture *comboTexture, *scoreTexture;
    Tex tex;
    tex.perfect = SDL_CreateTextureFromSurface(renderer, perfect);
    tex.good = SDL_CreateTextureFromSurface(renderer, good);
    tex.bad = SDL_CreateTextureFromSurface(renderer, bad);
    tex.miss = SDL_CreateTextureFromSurface(renderer, miss);
    tex.waku_init = SDL_CreateTextureFromSurface(renderer, waku_init);

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
    sq.perfect = centered(perfect);
    sq.good    = centered(good);
    sq.bad     = centered(bad);
    sq.miss    = centered(miss);

        // 画像の左端を一番左のレーン（W）の左端にそろえる
    sq.waku_init = {startX, (int)(SCREEN_H * (3.0 / 4.0) + 100), waku_init->w, waku_init->h};

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

    const double delayTimeMs = 1500.0;
    double musicStartTimeMs = nowMs() + delayTimeMs;

    double testSeekMs = 0.0;
    if(testStartMs > 0){
        testSeekMs = std::max(0.0, testStartMs + offsetMs - 1500.0);
        musicStartTimeMs -= testSeekMs;
    }

    bool bgmStarted = false;

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

    // ===== 処理落ち調査用（原因が分かったら 0 にしてよい） =====
    #define PLAY_PROFILE 1
    #if PLAY_PROFILE
    const double profFreq = static_cast<double>(SDL_GetPerformanceFrequency());
    auto profMs = [&](Uint64 a, Uint64 b){ return static_cast<double>(b - a) * 1000.0 / profFreq; };
    Uint64 profPrevEnd = SDL_GetPerformanceCounter();
    #endif

    SpectrumVisualizer visualizer;
    visualizer.attach();

    // ===== ポーズ =====
    TTF_Font* menuFont  = TTF_OpenFont("fonts/prac.ttf", 44);
    TTF_Font* labelFont = TTF_OpenFont("fonts/prac.ttf", 24);
    PauseMenu pauseMenu(tutorial == nullptr);   // チュートリアルではリトライを出さない
    double pauseStartMs  = 0.0;                 // ポーズした瞬間の実時間
    double pausedTotalMs = 0.0;                 // これまでにポーズしていた時間の合計

    // ポーズを始める（ESC と、ウィンドウのフォーカスが外れたときに呼ぶ）
    auto beginPause = [&](double realMs){
        if(pauseMenu.isActive()) return;
        pauseMenu.open(realMs);
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
        }

        // ① 200msごとに「本来あるべき補正量」だけを更新する
        if(bgmStarted && (globalMs - lastDriftCheckMs) >= 200.0){
            lastDriftCheckMs = globalMs;

            double actualPosMs = Mix_GetMusicPosition(bgm) * 1000.0;
            if(actualPosMs >= 0.0){
                double predictedRawMs = (globalMs - musicStartTimeMs) + driftTargetMs;
                double drift = actualPosMs - predictedRawMs;

                if(std::abs(drift) > 5.0){
                    driftTargetMs += drift * 0.2;
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
                beginPause(realMs);   // 別のウィンドウに切り替えたら自動でポーズ
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

        const Uint8* currentKeyState = SDL_GetKeyboardState(NULL);
            bool currentPressed[7] = {
                (currentKeyState[SDL_SCANCODE_W] != 0),
                (currentKeyState[SDL_SCANCODE_E] != 0),
                (currentKeyState[SDL_SCANCODE_R] != 0),
                (currentKeyState[SDL_SCANCODE_U] != 0),
                (currentKeyState[SDL_SCANCODE_I] != 0),
                (currentKeyState[SDL_SCANCODE_O] != 0),
                (currentKeyState[SDL_SCANCODE_ESCAPE] != 0)
            };

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
            .timingLog = &timingLog
            };

        if(paused){
            for(int i = 0; i < 6; i++) prevPressed[i] = currentPressed[i];
        }

        #if PLAY_PROFILE
        Uint64 prof1 = SDL_GetPerformanceCounter();
        #endif

        if(!paused){
            if(isAutoplay) applyAutoplay(ctx, prevMusicTime);

            for(int i = 0; i < 6; i++){
                if(ctx.currentPressed[i] && !ctx.prevPressed[i]){
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

            if(!Mix_PlayingMusic() && notes.empty() && nextScene != GameScene::Shutdown){
                running = false;
                nextScene = GameScene::Result;
            }
        }

        visualizer.update();

        SDL_SetRenderDrawColor(ctx.renderer, 155, 155, 155, 255);
        SDL_RenderClear(ctx.renderer);

        #if PLAY_PROFILE
        Uint64 prof2 = SDL_GetPerformanceCounter();
        #endif

        //スコア・コンボの表示値とアニメーション計算
        updateAndRenderScoreTexture(ctx);

        //ノーツの描画・アニメーション処理（この中で SDL_RenderPresent している）
                renderGamePlayScreen(ctx, tex, sq, currentNoteSpeed, laneX, false, &boxFx);
        pauseMenu.draw(renderer, menuFont, font, labelFont, realMs);
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

    Mix_HaltMusic();
    digitFont.destroy();
    TTF_CloseFont(font);
    TTF_CloseFont(font_init_waku);
    TTF_CloseFont(menuFont);
    TTF_CloseFont(labelFont);
    timingDisplay.destroy();
    if(timingFont) TTF_CloseFont(timingFont);
    Mix_FreeMusic(bgm);
    Mix_FreeChunk(tap_sound);
    Mix_FreeChunk(tap_sound_c);
    if(comboTexture) SDL_DestroyTexture(comboTexture);
    if(scoreTexture) SDL_DestroyTexture(scoreTexture);
    if(noteTextureNormal) SDL_DestroyTexture(noteTextureNormal);
    if(noteTextureDrag) SDL_DestroyTexture(noteTextureDrag);
    if(noteTextureLane) SDL_DestroyTexture(noteTextureLane);
    SDL_DestroyTexture(tex.perfect);
    SDL_DestroyTexture(tex.good);
    SDL_DestroyTexture(tex.bad);
    SDL_DestroyTexture(tex.miss);
    SDL_DestroyTexture(tex.waku_init);

    return nextScene;
}