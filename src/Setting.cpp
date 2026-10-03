#include "GameCommon.h"
#include <nlohmann/json.hpp>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

using json = nlohmann::json;

#define SCREEN_W 1920
#define SCREEN_H 1080

static const char* SETTING_PATH = "settings.json";

void loadPlayerSettings(PlayerSettings& settings){
    std::ifstream file(SETTING_PATH);
    if(!file.is_open()) return;

    json j;
    try{
        file >> j;
    }
    catch(const json::parse_error&){
        return;
    }

    settings.offsetMs = j.value("offsetMs", 0.0);
    settings.bgmVolume = j.value("bgmVolume", 70);
    settings.seVolume = j.value("seVolume", 100);
}

void savePlayerSettings(const PlayerSettings& settings){
    json j;
    j["offsetMs"] = settings.offsetMs;
    j["bgmVolume"] = settings.bgmVolume;
    j["seVolume"] = settings.seVolume;

    std::ofstream out(SETTING_PATH);
    if(out.is_open()){
        out << j.dump(2);
    }
    else{
        printf("[Setting] 保存失敗 : %s\n", SETTING_PATH);
    }
}

GameScene settingScene(SDL_Window* window, SDL_Renderer* renderer, PlayerSettings& playerSettings, SDL_Texture* targetTex){
    if(targetTex != nullptr){
        return GameScene::Setting;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    ImFontConfig font_cfg;
    font_cfg.OversampleH = 2;
    font_cfg.OversampleV = 2;

    io.Fonts->AddFontFromFileTTF(
        "fonts/prac.ttf",
        18.0f,
        &font_cfg,
        io.Fonts->GetGlyphRangesJapanese()
    );

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    PlayerSettings edit = playerSettings;

    Mix_Chunk* previewTap = Mix_LoadWAV("sounds/tapsound_2.wav");

    bool running = true;
    SDL_Event e;
    GameScene nextScene = GameScene::Select;

    while(running){
        while(SDL_PollEvent(&e) != 0){
            ImGui_ImplSDL2_ProcessEvent(&e);

            if(e.type == SDL_QUIT){
                running = false;
                nextScene = GameScene::Shutdown;
            }

            bool imguiWantsKeyboard = io.WantCaptureKeyboard;
            if(!imguiWantsKeyboard && e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE){
                running = false;
                nextScene = GameScene::Select;
            }
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(SCREEN_W / 2.0f - 300.0f, SCREEN_H / 2.0f - 220.0f), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(600, 440), ImGuiCond_Once);
        ImGui::Begin("Settings");

        ImGui::Text("Personal Audio Offset");
        ImGui::InputDouble("Offset(ms)", &edit.offsetMs, 1.0, 10.0, "%.1f");
        ImGui::TextDisabled("+:notes come later  -:notes come earlier");

        ImGui::Separator();

        ImGui::Text("Volume");
        if(ImGui::SliderInt("BGM Volume", &edit.bgmVolume, 0, 100)){
            Mix_VolumeMusic(static_cast<int>(edit.bgmVolume * MIX_MAX_VOLUME / 100.0));
        }
        if(ImGui::SliderInt("SE Volume", &edit.seVolume, 0, 100)){
            if(previewTap) Mix_VolumeChunk(previewTap, static_cast<int>(edit.seVolume * MIX_MAX_VOLUME / 100.0));
        }
        if(ImGui::Button("Preview SE")){
            if(previewTap){
                Mix_VolumeChunk(previewTap, static_cast<int>(edit.seVolume * MIX_MAX_VOLUME / 100.0));
                Mix_PlayChannel(-1, previewTap, 0);
            }
        }

        ImGui::Separator();

        if(ImGui::Button("SAVE")){
            playerSettings = edit;
            savePlayerSettings(playerSettings);
        }
        ImGui::SameLine();
        if(ImGui::Button("BACK")){
            running = false;
            nextScene = GameScene::Select;
        }

        ImGui::TextDisabled("ESC to go back without saving");

        ImGui::End();

        SDL_SetRenderDrawColor(renderer, 15, 15, 25, 255);
        SDL_RenderClear(renderer);

        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    if(previewTap) Mix_FreeChunk(previewTap);

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    return nextScene;
}