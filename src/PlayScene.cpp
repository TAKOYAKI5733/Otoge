/*g++ -std=c++20 main.cpp PlayScene.cpp -o otoge -lSDL2 -lSDL2_ttf -lSDL2_mixerでコンパイル可*/
#include "GameCommon.h"
#include "Play/PlaySceneManager.h"

//グローバル変数
double bpm = 120;

//playGame関数の制作
GameScene playGame(SDL_Window* window, SDL_Renderer* renderer, const std::string& selectedScorePath, SDL_Texture* targetTex){

    //変数定義
    std::vector<Effect> effects;
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
    if(!loadScore(selectedScorePath, bgmName, notes, speedEvents, bpm, offsetMs)){
        return GameScene::Select;
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
    
    Mix_Chunk* tap_sound = Mix_LoadWAV("sounds/tapsound_2.wav");
    if(!tap_sound){
        printf("効果音読込失敗\n");
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


    uint32_t delayTimeMs = 1500;
    uint32_t musicStartTime = SDL_GetTicks() + delayTimeMs;
    bool bgmStarted = false;

    uint32_t lastDriftCheckTime = 0;
    double clockDriftCorrectionMs = 0.0;

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

    //描画処理
    bool running = true;
    SDL_Event e;
    GameScene nextScene = GameScene::Select;

    while(running){

        uint32_t globalTime = SDL_GetTicks();

        double rawElapsedMs = static_cast<double>(static_cast<int32_t>(globalTime - musicStartTime)) + clockDriftCorrectionMs;
        int32_t musicTime = static_cast<int32_t>(rawElapsedMs - offsetMs);

        if(!bgmStarted && globalTime >= musicStartTime){
            Mix_PlayMusic(bgm, 1);
            bgmStarted = true;
        }

        if(bgmStarted && (globalTime - lastDriftCheckTime) >= 200){
            lastDriftCheckTime = globalTime;

            double actualPosMs = Mix_GetMusicPosition(bgm) * 1000.0;
            if(actualPosMs >= 0.0){
                double predictedRawMs = static_cast<double>(static_cast<int32_t>(globalTime - musicStartTime)) + clockDriftCorrectionMs;
                double drift = actualPosMs - predictedRawMs;

                if(std::abs(drift) > 5.0){
                    clockDriftCorrectionMs += drift * 0.2;
                }
            }
        }

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
            .tap_sound = tap_sound,
            .comboCount = comboCount,
            .startX = startX,
            .endX = endX,
            .judgeY = judgeY,
            .laneWidth = laneWidth,
            .laneActive = laneActive
            };

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

        //スコア・コンボの描画・アニメーション処理
        updateAndRenderScoreTexture(ctx);

        //ノーツの描画・アニメーション処理
        renderGamePlayScreen(ctx, tex, sq, currentNoteSpeed, laneX);
    }

    Mix_HaltMusic();
    TTF_CloseFont(font);
    TTF_CloseFont(font_init_waku);
    Mix_FreeMusic(bgm);
    Mix_FreeChunk(tap_sound);
    if(comboTexture) SDL_DestroyTexture(comboTexture);
    if(scoreTexture) SDL_DestroyTexture(scoreTexture);
    SDL_DestroyTexture(tex.perfect);
    SDL_DestroyTexture(tex.good);
    SDL_DestroyTexture(tex.bad);
    SDL_DestroyTexture(tex.miss);
    SDL_DestroyTexture(tex.waku_init);

    return nextScene;
}