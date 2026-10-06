#include "GameCommon.h"
#include "Play/tutorialOverlay.h"
#include "Displaymode.h"

#define SCREEN_W 1920
#define SCREEN_H 1080

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>
#endif

void renderTransition(SDL_Renderer* renderer, SDL_Texture* prev, SDL_Texture* nex, double progress);

// 🌟 修正: Windows環境では、C言語で書かれたlibSDL2mainライブラリが
//          マングリングされていない"SDL_main"という名前を直接探しにくるため、
//          extern "C" で関数名をそのまま公開する。
//          Linux等それ以外の環境では、従来通り通常のmain()のままにする。
#if defined(_WIN32)
extern "C" int SDL_main(int argc, char* argv[]){
    (void)argc;
    (void)argv;
#else
int main(){
#endif

    #if defined(_WIN32)
        SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");   // D3D9のバッファリング遅延を回避
    #endif

    // ドライバを明示指定するとSDL2はバッチ描画を既定でOFFにするため、明示的にONにする
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0){
        std::cout << "SDL初期化失敗\n";
        return -1;
    }

    if(!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)){
        std::cout << "SDL_image初期化失敗: " << IMG_GetError() << "\n";
        return -1;
    }

    if(TTF_Init() < 0){
        std::cout << "TTF初期化失敗\n";
        return -1;
    }

    if(Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 256) < 0){
        std::cout << "Audio初期化失敗\n";
        TTF_Quit();
        SDL_Quit();
        return -1;
    }
    Mix_AllocateChannels(256);

    SDL_Window* window = SDL_CreateWindow("Otoge test --- Scene_Manager",SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    SDL_RenderSetLogicalSize(renderer, SCREEN_W, SCREEN_H);

    GameScene currentScene = GameScene::Select;
    GameScene nextScene = currentScene;
    std::string selectedScore = "";
    int selectedDifficulty = 0;
    ResultData resultData;
    bool autoplay = false;

    PlayerSettings playerSettings;
    loadPlayerSettings(playerSettings);
    applyWindowMode(window, playerSettings.windowMode);

    // 設定ファイルの値でVSyncとFPS上限を反映
    SDL_RenderSetVSync(renderer, playerSettings.vsync ? 1 : 0);
    g_framePacer.setCap(playerSettings.fpsCap);

    SDL_DisplayMode dm;
    if(SDL_GetWindowDisplayMode(window, &dm) == 0){
        std::cout << "モニターのリフレッシュレート: " << dm.refresh_rate << "Hz\n";
    }

    SDL_Texture* prev = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);
    SDL_Texture* nex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);

    #if defined(_WIN32)
        timeBeginPeriod(1);
    #endif

    while(currentScene != GameScene::Shutdown){
        switch(currentScene){
            case GameScene::Select:{
                nextScene = selectSongScene(window, renderer, selectedScore, selectedDifficulty, playerSettings, autoplay);

                if(nextScene == GameScene::Load){
                    SDL_SetRenderTarget(renderer, prev);
                    selectSongScene(window, renderer, selectedScore, selectedDifficulty, playerSettings, autoplay, prev);

                    SDL_SetRenderTarget(renderer, nex);
                    loadScene(window, renderer, selectedScore, nex);

                    SDL_SetRenderTarget(renderer, NULL);

                    // 経過時間からprogressを求めるので、fpsに関係なく常に480msで遷移する
                    double progress = 0.0;
                    const double transitionMs = 480.0;
                    const Uint64 tStart = SDL_GetPerformanceCounter();
                    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());

                    while(progress < 1.0){
                        double elapsedMs = (SDL_GetPerformanceCounter() - tStart) * 1000.0 / freq;
                        progress = std::min(elapsedMs / transitionMs, 1.0);

                        SDL_Event ev;
                        while(SDL_PollEvent(&ev)){
                            if(ev.type == SDL_QUIT){
                                currentScene = GameScene::Shutdown;
                                nextScene = GameScene::Shutdown;
                                break;
                            }
                        }
                        if(currentScene == GameScene::Shutdown) break;

                        renderTransition(renderer, prev, nex, progress);
                        g_framePacer.endFrame(window);
                    }
                }

                if(currentScene == GameScene::Shutdown){
                    break;
                }

                currentScene = nextScene;
                break;
            }

            case GameScene::Play:{
                currentScene = playGame(window, renderer, selectedScore, selectedDifficulty, resultData, playerSettings, autoplay, nullptr);
                break;
            }
            
            case GameScene::Result:{
                currentScene = resultScene(window, renderer, resultData);
                break;
            }

            case GameScene::Load:{
                // シンプルにLoad処理を行い、戻り値で次にPlayへ行くことを期待する
                loadScene(window, renderer, selectedScore, nullptr);
                currentScene = GameScene::Play;
                break;
            }

            case GameScene::ChartCreate:{
                currentScene = chartCreateScene(window, renderer, selectedScore, nullptr);
                break;
            }

            case GameScene::Setting:{
                currentScene = settingScene(window, renderer, playerSettings);
                break;
            }

            case GameScene::Tutorial:{
                TutorialOverlay overlay;
                if(!overlay.load(renderer, selectedScore)){
                    std::cout << "[チュートリアル] 開始できませんでした\n";
                    currentScene = GameScene::Select;
                    break;
                }
                ResultData tutorialResult;
                GameScene next = playGame(window, renderer, overlay.chartPath(), overlay.chartDifficulty(),
                                        tutorialResult, playerSettings, false, nullptr, &overlay);
                overlay.release();
                currentScene = (next == GameScene::Shutdown) ? GameScene::Shutdown : GameScene::Select;
                break;
            }

            default:{
                currentScene = GameScene::Shutdown;
                break;
            }
        }
    }

    // テクスチャはレンダラーより先に破棄する
    releaseGraphicsCache();
    SDL_DestroyTexture(prev);
    SDL_DestroyTexture(nex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    Mix_CloseAudio();
    TTF_Quit();
    SDL_Quit();

    #if defined(_WIN32)
        timeEndPeriod(1);
    #endif

    std::cout << "ゲームの正常終了\n";
    return 0;
}

void renderTransition(SDL_Renderer* renderer, SDL_Texture* prev, SDL_Texture* nex, double progress){
    double easedProgress = 1.0 - std::pow(1.0 - progress, 3.0);

    SDL_Rect prevRect = {
        static_cast<int>(-SCREEN_W * easedProgress),
        0,
        SCREEN_W,
        SCREEN_H
    };

    SDL_Rect nexRect = {
        static_cast<int>(SCREEN_W * (1.0 - easedProgress)),
        0,
        SCREEN_W,
        SCREEN_H
    };

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_RenderCopy(renderer, prev, NULL, &prevRect);
    SDL_RenderCopy(renderer, nex, NULL, &nexRect);

    SDL_RenderPresent(renderer);
}