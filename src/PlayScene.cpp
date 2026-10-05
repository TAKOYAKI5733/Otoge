/*g++ -std=c++20 main.cpp PlayScene.cpp -o otoge -lSDL2 -lSDL2_ttf -lSDL2_mixerでコンパイル可*/
#include "GameCommon.h"
#include "Play/PlaySceneManager.h"

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
GameScene playGame(SDL_Window* window, SDL_Renderer* renderer, const std::string& selectedScorePath, int selectedDifficulty, ResultData& outResult, PlayerSettings& playerSettings, bool isAutoplay, SDL_Texture* targetTex, TutorialOverlay* tutorial){
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
    
    Mix_Chunk* tap_sound = Mix_LoadWAV("sounds/tapsound_2.wav");
    if(!tap_sound){
        printf("効果音読込失敗\n");
    }
    else{
        Mix_VolumeChunk(tap_sound, static_cast<int>(playerSettings.seVolume * MIX_MAX_VOLUME / 100.0));
    }

    //SDL系統処理
    TTF_Font* font = TTF_OpenFont("fonts/prac.ttf", 80);
    TTF_Font* font_init_waku = TTF_OpenFont("fonts/prac.ttf", 60);

    if(!font){
        printf("読込失敗 %s\n", TTF_GetError());
        return GameScene::Select;
    }

    SDL_Surface *perfect, *good, *bad, *miss, *waku_init;
    perfect = TTF_RenderUTF8_Solid(font, "PERFECT!!", {255, 215, 0, 255});
    good = TTF_RenderUTF8_Solid(font, "GOOD!", {0, 255, 255, 255});
    bad = TTF_RenderUTF8_Solid(font, "BAD", {90, 255, 25, 255});
    miss = TTF_RenderUTF8_Solid(font, "MISS...", {100, 100, 100, 255});
    waku_init = TTF_RenderUTF8_Solid(font_init_waku, "E   R   U   I", {255, 255, 255, 255});
    
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
    sq.perfect = {(int)(SCREEN_W * (3.0 / 4.0) + 50), (int)(SCREEN_H * (3.0 / 4.0) + 75), perfect->w, perfect->h};
    sq.good = {(int)(SCREEN_W * (3.0 / 4.0) + 50), (int)(SCREEN_H * (3.0 / 4.0) + 75), good->w, good->h};
    sq.bad = {(int)(SCREEN_W * (3.0 / 4.0) + 50), (int)(SCREEN_H * (3.0 / 4.0) + 75), bad->w, bad->h};
    sq.miss = {(int)(SCREEN_W * (3.0 / 4.0) + 50), (int)(SCREEN_H * (3.0 / 4.0) + 75), miss->w, miss->h};

    sq.waku_init = {(int)(SCREEN_W * (13.0 / 32.0) - 10), (int)(SCREEN_H * (3.0 / 4.0) + 100), waku_init->w, waku_init->h};

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

    //描画処理
    bool running = true;
    SDL_Event e;
    GameScene nextScene = GameScene::Select;

    int32_t prevMusicTime = 0;

    while(running){

        #if PLAY_PROFILE
        Uint64 prof0 = SDL_GetPerformanceCounter();
        #endif

        double globalMs = nowMs();
        double frameDtMs = globalMs - prevGlobalMs;
        prevGlobalMs = globalMs;

        if(!bgmStarted && globalMs >= musicStartTimeMs){
            Mix_PlayMusic(bgm, 1);
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
        }

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
            if(currentKeyState[SDL_SCANCODE_ESCAPE] != 0){
                running = false;
                nextScene = GameScene::Select;
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
            .digitFont = &digitFont
            };
            ctx.tutorial = tutorial;

        if(isAutoplay){
            for(int i = 0; i < 6; i++) ctx.currentPressed[i] = false;

            for(const auto& note : ctx.notes){
                if(note.lane < 0 || note.lane >= 6) continue;

                if(note.type == NoteType::Long && note.isHolding){
                    ctx.currentPressed[note.lane] = true;
                    continue;
                }

                if(note.isHit) continue;

                if(note.type == NoteType::Drag){
                    if(ctx.musicTime >= note.targetTime && ctx.musicTime <= note.targetTime + 60){
                        ctx.currentPressed[note.lane] = true;
                    }
                }
                else{
                    if(prevMusicTime < note.targetTime && ctx.musicTime >= note.targetTime){
                        ctx.currentPressed[note.lane] = true;
                    }
                }
            }
        }

        for(int i = 0; i < 6; i++){
            if(ctx.currentPressed[i] && !ctx.prevPressed[i]){
                KeyBeam beam;
                beam.lane = i;
                beam.spawnTime = ctx.musicTime;
                ctx.keyBeams.push_back(beam);
            }
        }

        #if PLAY_PROFILE
        Uint64 prof1 = SDL_GetPerformanceCounter();
        #endif

        //ノーツの判定処理 noteJudge.h    
        NoteJudge(ctx, tex, sq);

        //ノーツの消去処理 renderGame.h
        erase_some(ctx);

        if(!Mix_PlayingMusic() && notes.empty() && nextScene != GameScene::Shutdown){
            running = false;
            nextScene = GameScene::Result;
        }

        for(int i = 0; i < 6; i++){
            if(laneJudge[i].texture != nullptr){
                if((musicTime - laneJudge[i].spawnTime) >= static_cast<int32_t>(laneJudge[i].duration)){
                    laneJudge[i].texture = nullptr;
                }
            }
        }

        SDL_SetRenderDrawColor(ctx.renderer, 155, 155, 155, 255);
        SDL_RenderClear(ctx.renderer);

        #if PLAY_PROFILE
        Uint64 prof2 = SDL_GetPerformanceCounter();
        #endif

        //スコア・コンボの表示値とアニメーション計算
        updateAndRenderScoreTexture(ctx);

        //ノーツの描画・アニメーション処理（この中で SDL_RenderPresent している）
        renderGamePlayScreen(ctx, tex, sq, currentNoteSpeed, laneX);

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

    Mix_HaltMusic();
    digitFont.destroy();
    TTF_CloseFont(font);
    TTF_CloseFont(font_init_waku);
    Mix_FreeMusic(bgm);
    Mix_FreeChunk(tap_sound);
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