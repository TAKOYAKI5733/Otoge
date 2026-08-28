#include "GameCommon.h"

#define SCREEN_W 1920
#define SCREEN_H 1080

void renderTransition(SDL_Renderer* renderer, SDL_Texture* prev, SDL_Texture* nex, double progress);

int main(){
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0){
        std::cout << "SDL初期化失敗\n";
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

    SDL_Window* window = SDL_CreateWindow("Otoge test --- Scene_Manager",SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    GameScene currentScene = GameScene::Select;
    GameScene nextScene = currentScene;
    std::string selectedScore = "";
    int selectedDifficulty = 0;

    SDL_Texture* prev = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);
    SDL_Texture* nex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);

    while(currentScene != GameScene::Shutdown){
        switch(currentScene){
            case GameScene::Select:{
                nextScene = selectSongScene(window, renderer, selectedScore, selectedDifficulty);

                if(nextScene == GameScene::Load){
                    SDL_SetRenderTarget(renderer, prev);
                    selectSongScene(window, renderer, selectedScore, selectedDifficulty, prev);

                    SDL_SetRenderTarget(renderer, nex);
                    loadScene(window, renderer, selectedScore, nex);

                    SDL_SetRenderTarget(renderer, NULL);

                    double progress = 0.0;
                    bool transitionRunning = true;

                    while(transitionRunning && progress < 1.0){
                        progress += 0.035;
                        if(progress >= 1.0){
                            progress = 1.0;
                            transitionRunning = false;
                        }

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
                        SDL_Delay(16);
                    }
                }

                if(currentScene == GameScene::Shutdown){
                    break;
                }

                currentScene = nextScene;
                break;
            }

            case GameScene::Play:{
                playGame(window, renderer, selectedScore, selectedDifficulty, nullptr);
                currentScene = GameScene::Result;
                break;
            }
            
            case GameScene::Result:{
                currentScene = GameScene::Select;
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

            default:{
                currentScene = GameScene::Shutdown;
                break;
            }
        }
    }

    SDL_DestroyTexture(prev);
    SDL_DestroyTexture(nex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    Mix_CloseAudio();
    TTF_Quit();
    SDL_Quit();

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