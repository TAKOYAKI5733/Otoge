#include "GameCommon.h"
#include <nlohmann/json.hpp>
#include <limits>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

using json = nlohmann::json;

#define SCREEN_W 1920
#define SCREEN_H 1080

struct EditorNote{
    int noteTypeInt = 0;
    int lane;
    int width = 1;
    int32_t time = 0;
    int32_t duration = 0; //0のときはノーマルノーツと処理
    bool hasCustomSpeed = false;
    double customSpeed = 1.0;

    bool isLong() const {return duration > 0;}
};

struct EditorSpeedEvent{
    int32_t time = 0;
    double target = 1.0;
    int easing = 1;
    int32_t duration = 500;
};

struct ChartMeta{
    std::string bgm;
    double bpm = 120.0;
    std::string composer = "Unknown";
    std::string level = "1";
    std::string chartCreator = "Unknown";
    double offsetMs = 0.0;
};

struct FlashEffect{
    int lane;
    uint32_t spawnTime;
};

struct EditorBpmEvent{
    int32_t time = 0;
    double bpm = 120.0;
};

//JSON保存・読込

inline void saveChart(const std::string& path, const ChartMeta& meta, const std::vector<EditorNote>& notes, const std::vector<EditorSpeedEvent>& speedEvents, const std::vector<EditorBpmEvent>& bpmEvents){
    json j;
    j["bgm"] = meta.bgm;
    j["bpm"] = meta.bpm;
    j["composer"] = meta.composer;
    j["level"] = meta.level;
    j["chartCreator"] = meta.chartCreator;
    j["offset"] = meta.offsetMs;

    json notesArr = json::array();
    for(const auto& n : notes){
        json item;
        item["type"] = n.noteTypeInt;
        item["time"] = n.time;
        item["lane"] = n.lane;
        if(n.width != 1) item["width"] = n.width;
        if(n.duration > 0) item["duration"] = n.duration;
        if(n.hasCustomSpeed) item["speed"] = n.customSpeed;
        notesArr.push_back(item);
    }
    j["notes"] = notesArr;

    json speedArr = json::array();
    for(const auto& s : speedEvents){
        json item;
        item["time"] = s.time;
        item["target"] = s.target;
        item["easing"] = s.easing;
        item["duration"] = s.duration;
        speedArr.push_back(item);
    }
    j["speedEvents"] = speedArr;

    json bpmArr = json::array();
    for(const auto& b : bpmEvents){
        json item;
        item["time"] = b.time;
        item["bpm"] = b.bpm;
        bpmArr.push_back(item);
    }
    j["bpmEvents"] = bpmArr;

    std::ofstream out(path);
    if(out.is_open()){
        out << j.dump(2);
        printf("[譜面エディタ] 保存完了 : %s\n", path.c_str());
    }
    else{
        printf("[譜面エディタ] 保存失敗 : %s\n", path.c_str());
    }
}

inline bool loadChartForEdit(const std::string & path, ChartMeta& meta, std::vector<EditorNote>& notes, std::vector<EditorSpeedEvent>& speedEvents, std::vector<EditorBpmEvent>& bpmEvents){
    std::ifstream file(path);
    if(!file.is_open()) return false;

    json j;
    try{
        file >> j;
    }
    catch(const json::parse_error&){
        return false;
    }

    meta.bgm = j.value("bgm", std::string(""));
    meta.bpm = j.value("bpm", 120.0);
    meta.composer = j.value("composer", std::string("Unknown"));
    meta.level = j.value("level", std::string("Unknown"));
    meta.chartCreator = j.value("chartCreator", std::string("Unknown"));
    meta.offsetMs = j.value("offset", 0.0);

    notes.clear();
    if(j.contains("notes")){
        for(const auto& item : j.at("notes")){
            EditorNote n;
            n.noteTypeInt = item.value("type", 0);
            n.time = item.value("time", 0);
            n.lane = item.value("lane", 0);
            n.width = item.value("width", 1);
            n.duration = item.value("duration", 0);
            if(item.contains("speed")){
                n.hasCustomSpeed = true;
                n.customSpeed = item.at("speed").get<double>();
            }
            notes.push_back(n);
        }
    }

    speedEvents.clear();
    if(j.contains("speedEvents")){
        for(const auto& item : j.at("speedEvents")){
            EditorSpeedEvent s;
            s.time = item.value("time", 0);
            s.target = item.value("target", 1.0);
            s.easing = item.value("easing", 1);
            s.duration = item.value("duration", 500);
            speedEvents.push_back(s);
        }
    }

    bpmEvents.clear();
    if(j.contains("bpmEvents")){
        for(const auto& item : j.at("bpmEvents")){
            EditorBpmEvent b;
            b.time = item.value("time", 0);
            b.bpm = item.value("bpm", 120.0);
            bpmEvents.push_back(b);
        }
    }

    return true;
}

//譜面エディター
GameScene chartCreateScene(SDL_Window* window, SDL_Renderer* renderer, std::string& scorePath, SDL_Texture* targetTex){
    if(targetTex != nullptr){
        return GameScene::ChartCreate;
    }

    //Dear ImGui 初期化(後に更に追求)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    //譜面データ
    ChartMeta meta;
    std::vector<EditorNote> notes;
    std::vector<EditorSpeedEvent> speedEvents;
    std::vector<EditorBpmEvent> bpmEvents;
    std::vector<FlashEffect> flashEffects;

    if(!scorePath.empty()){
        loadChartForEdit(scorePath, meta, notes, speedEvents, bpmEvents);
    }

    char savePathBuf[256];
    snprintf(savePathBuf, sizeof(savePathBuf), "%s", scorePath.empty() ? "scores/new_chart.json" : scorePath.c_str());
    char bgmBuf[256];
    snprintf(bgmBuf, sizeof(bgmBuf), "%s", meta.bgm.c_str());
    char composerBuf[128];
    snprintf(composerBuf, sizeof(composerBuf), "%s", meta.composer.c_str());
    char levelBuf[32];
    snprintf(levelBuf, sizeof(levelBuf), "%s", meta.level.c_str());
    char creatorBuf[128];
    snprintf(creatorBuf, sizeof(creatorBuf), "%s", meta.chartCreator.c_str());

    Mix_Music* bgm = nullptr;
    bool isPlaying = false;
    int32_t scrollTimeMs = 0;
    uint32_t lastFrameticks = SDL_GetTicks();

    int editorAudioLatencyMs = 40;
    int32_t pendingAudioLatencyMs = 0;
    bool musicNeedsStart = true;

    Mix_Chunk* tap_sound = Mix_LoadWAV("sounds/tapsound_2.wav");
    if(!tap_sound){
        printf("効果音読込失敗\n");
    }

    auto seekMusicIfNeeded = [&](int32_t chartTimeMs){
        double audioPosSec = std::max(0.0, (chartTimeMs + meta.offsetMs) / 1000.0);
        if(audioPosSec > 0.001){
            Mix_SetMusicPosition(audioPosSec);
        }
    };

    auto getCurrentBpm = [&](int32_t timeMs) -> double {
        double result = meta.bpm;
        int32_t bestTime = std::numeric_limits<int32_t>::min();
        for(const auto& b : bpmEvents){
            if(b.time <= timeMs && b.time > bestTime){
                bestTime = b.time;
                result = b.bpm;
            }
        }
        return (result > 0.0) ? result : meta.bpm;
    };

    auto togglePlayback = [&](){
        isPlaying = !isPlaying;

        if(isPlaying){
            if(!bgm && !meta.bgm.empty()){
                std::string fullPath = "sounds/" + meta.bgm;
                bgm = Mix_LoadMUS(fullPath.c_str());
                if(!bgm) printf("[譜面エディタ] BGM読込失敗 : %s\n", fullPath.c_str());
            }

            if(bgm){
                if(!musicNeedsStart && Mix_PausedMusic() == 1){
                    Mix_ResumeMusic();
                }
                else{
                    Mix_PlayMusic(bgm, 1);
                    musicNeedsStart = false;

                    if(editorAudioLatencyMs > 0) pendingAudioLatencyMs = editorAudioLatencyMs;
                    else if(editorAudioLatencyMs < 0){
                        scrollTimeMs += -editorAudioLatencyMs;
                        pendingAudioLatencyMs = 0;
                    }
                    else{
                        pendingAudioLatencyMs = 0;
                    }
                }
                seekMusicIfNeeded(scrollTimeMs);
            }
        }
        else{
            if(bgm) Mix_PauseMusic();
        }
    };

    double pixelsPerMs = 0.3;
    int judgeY = static_cast<int>(SCREEN_H * (3.0 / 4.0));
    int laneWidth = SCREEN_W / 16;
    int startX = SCREEN_W / 2 - (laneWidth * 3);
    int laneX[6];
    for(int i = 0; i < 6; i++) laneX[i] = startX + laneWidth * i;

    int gridDivisor = 4; //標準で4分

    //ツールバー設定
    int toolNotetype = 0;
    int toolWidth = 1;
    bool toolIsLong = false;
    int toolDurationMs = 500;
    bool toolHasCustomSpeed = false;
    double toolCustomSpeed = 1.8;

    int selectedNoteIndex = -1;

    enum class DragMode{ None, MoveNote, ResizeTail };
    DragMode dragMode = DragMode::None;
    int dragNoteIndex = -1;

    bool running = true;
    SDL_Event e;
    GameScene nextScene = GameScene::Select;

    const char* typenames[] = {"Normal(0)", "Drag(3)", "Lane(4)"};

    while(running){
        uint32_t nowTicks = SDL_GetTicks();
        uint32_t frameDeltaMs = nowTicks - lastFrameticks;
        lastFrameticks = nowTicks;

        if(isPlaying){
            if(pendingAudioLatencyMs > 0){
                int32_t consumed = std::min(pendingAudioLatencyMs, static_cast<int32_t>(frameDeltaMs));
                pendingAudioLatencyMs -= consumed;
                int32_t remaining = static_cast<int32_t>(frameDeltaMs) - consumed;
                scrollTimeMs += remaining;
            }
            else{
                scrollTimeMs += static_cast<int32_t>(frameDeltaMs);
            }
        }

        double currentBpmAtPlayhead = getCurrentBpm(scrollTimeMs);
        double effectivePixelsPerMs = pixelsPerMs * (currentBpmAtPlayhead / meta.bpm);

        //イベント処理
        while(SDL_PollEvent(&e) != 0){
            ImGui_ImplSDL2_ProcessEvent(&e);

            if(e.type == SDL_QUIT){
                running = false;
                nextScene = GameScene::Shutdown;
            }

            if(e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE){
                running = false;
                nextScene = GameScene::Select;
            }

            bool imguiWantsKeyBoard = io.WantCaptureKeyboard;

            if(!imguiWantsKeyBoard && e.type == SDL_KEYDOWN && e.key.repeat == 0 && e.key.keysym.sym == SDLK_SPACE){
                togglePlayback();
            }

            if(!imguiWantsKeyBoard && e.type == SDL_KEYDOWN && e.key.repeat == 0 && (e.key.keysym.sym == SDLK_w || e.key.keysym.sym == SDLK_s)){
                double beatDurationMs = 60000.0 / meta.bpm;
                double gridMs = (4.0 / gridDivisor) * beatDurationMs;
                int32_t step = static_cast<int32_t>(std::round(gridMs));

                if(e.key.keysym.sym == SDLK_w) scrollTimeMs += step;
                else scrollTimeMs -= step;

                if(scrollTimeMs < 0) scrollTimeMs = 0;

                if(bgm /*&& isPlaying*/){
                    seekMusicIfNeeded(scrollTimeMs);
                }
            }

            //ImGuiパネル操作中はエディタ本体のクリック処理を無効化
            bool imguiWantsMouse = io.WantCaptureMouse;

            if(!imguiWantsMouse && e.type == SDL_MOUSEWHEEL){
                SDL_Keymod mod = SDL_GetModState();
                bool ctrlHeld = (mod & KMOD_CTRL) != 0;

                if(ctrlHeld){
                    double zoomFactor = (e.wheel.y > 0) ? 1.15 : (1.0 / 1.15);
                    pixelsPerMs *= zoomFactor;
                    if(pixelsPerMs < 0.02) pixelsPerMs = 0.02;
                    if(pixelsPerMs > 3.0) pixelsPerMs = 3.0;
                }
                else{
                    double beatDuration = 60000.0 / meta.bpm;
                    double gridMs = (4.0 / gridDivisor) * beatDuration;
                    int32_t step = static_cast<int32_t>(std::round(gridMs));

                    //ホイールを上に回すと上にいく
                    if(e.wheel.y < 0) scrollTimeMs -= step;
                    else if(e.wheel.y > 0) scrollTimeMs += step;

                    if(scrollTimeMs < 0) scrollTimeMs = 0;

                    if(bgm) seekMusicIfNeeded(scrollTimeMs);
                }
            }

            if(!imguiWantsMouse && e.type == SDL_MOUSEBUTTONDOWN){
                int mx = e.button.x;
                int my = e.button.y;

                int clickedLane = -1;
                for(int l = 0; l < 6; l++){
                    if(mx >= laneX[l] && mx < laneX[l] + laneWidth){
                        clickedLane = l;
                        break;
                    }
                }

                if(clickedLane >= 0){
                    double rawTime = scrollTimeMs + (judgeY - my) / effectivePixelsPerMs;

                    double beatDurationMs = 60000.0 / meta.bpm;
                    double gridMs = (4.0 / gridDivisor) * beatDurationMs;
                    int32_t snappedTime = static_cast<int32_t>(std::round(rawTime / gridMs) * gridMs);

                    if(e.button.button == SDL_BUTTON_LEFT){
                        int tailHitIndex = -1;
                        for(size_t idx = 0; idx < notes.size(); idx++){
                            const auto& n = notes[idx];
                            if(!n.isLong()) continue;
                            if(clickedLane < n.lane || clickedLane > n.lane + n.width - 1) continue;

                            int32_t tailTime = n.time + n.duration;
                            if(std::abs(static_cast<double>(tailTime) - rawTime) < gridMs / 2){
                                tailHitIndex = static_cast<int>(idx);
                                break;
                            }
                        }

                        if(tailHitIndex >= 0){
                            selectedNoteIndex = tailHitIndex;
                            dragMode = DragMode::ResizeTail;
                            dragNoteIndex = tailHitIndex;
                        }
                        else{
                            int hitIndex = -1;
                            for(size_t idx = 0; idx < notes.size(); idx++){
                                const auto& n = notes[idx];
                                if(clickedLane >= n.lane && clickedLane <= n.lane + n.width - 1 && std::abs(n.time - snappedTime) < gridMs / 2){
                                    hitIndex = static_cast<int>(idx);
                                    break;
                                }
                            }

                            if(hitIndex >= 0){
                                selectedNoteIndex = hitIndex;
                                dragMode = DragMode::MoveNote;
                                dragNoteIndex = hitIndex;
                            }
                            else{
                                EditorNote newNote;
                                newNote.noteTypeInt = toolNotetype;
                                newNote.lane = clickedLane;
                                newNote.width = toolWidth;
                                newNote.time = snappedTime;
                                newNote.duration = toolIsLong ? toolDurationMs : 0;
                                newNote.hasCustomSpeed = toolHasCustomSpeed;
                                newNote.customSpeed = toolCustomSpeed;

                                notes.push_back(newNote);
                                selectedNoteIndex = static_cast<int>(notes.size()) - 1;
                            }
                        }
                    }
                    else if(e.button.button == SDL_BUTTON_RIGHT){
                        for(size_t idx = 0; idx < notes.size(); idx++){
                            const auto& n = notes[idx];
                            if(clickedLane >= n.lane && clickedLane <= n.lane + n.width - 1 && std::abs(n.time - snappedTime) < gridMs / 2){
                                notes.erase(notes.begin() + idx);
                                if(selectedNoteIndex == static_cast<int>(idx)) selectedNoteIndex = -1;
                                break;
                            }
                        }
                    }
                }
            }

            if(e.type == SDL_MOUSEMOTION && dragMode != DragMode::None && dragNoteIndex >= 0 && dragNoteIndex < static_cast<int>(notes.size())){

                int mx = e.motion.x;
                int my = e.motion.y;

                int hoverLane = -1;
                for(int l = 0; l < 6; l++){
                    if(mx >= laneX[l] && mx < laneX[l] + laneWidth){
                        hoverLane = l;
                        break;
                    }
                }

                double rawTime = scrollTimeMs + (judgeY - my) / effectivePixelsPerMs;
                double beatDurationMs = 60000.0 / meta.bpm;
                double gridMs = (4.0 / gridDivisor) * beatDurationMs;
                int32_t snappedTime = static_cast<int32_t>(std::round(rawTime / gridMs) * gridMs);
                if(snappedTime < 0) snappedTime = 0;

                EditorNote& n = notes[dragNoteIndex];

                if(dragMode == DragMode::MoveNote){
                    n.time = snappedTime;
                    if(hoverLane >= 0){
                        int maxLane = 6 - n.width;
                        n.lane = std::clamp(hoverLane, 0, maxLane);
                    }
                }
                else if(dragMode == DragMode::ResizeTail){
                    int32_t newDuration = snappedTime - n.time;
                    int32_t minDuration = static_cast<int32_t>(std::max(1.0, gridMs));
                    if(newDuration < minDuration) newDuration = minDuration;
                    n.duration = newDuration;
                }
            }

            if(e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT){
                dragMode = DragMode::None;
                dragNoteIndex = -1;
            }
        }

        if(selectedNoteIndex >= static_cast<int>(notes.size())) selectedNoteIndex = -1;

        //オートプレイ : プレビュー
        if(isPlaying){
            for(const auto& n : notes){
                if(scrollTimeMs >= n.time && scrollTimeMs - static_cast<int32_t>(frameDeltaMs) < n.time){
                    FlashEffect fx;
                    fx.lane = n.lane;
                    fx.spawnTime = nowTicks;
                    flashEffects.push_back(fx);
                    Mix_PlayChannel(-1, tap_sound, 0);
                }
            }
        }
        std::erase_if(flashEffects, [nowTicks](const FlashEffect& fx){
            return (nowTicks - fx.spawnTime) > 200;
        });

        //ImGuiフレーム
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        //ファイル・メタ情報
        ImGui::Begin("File / Meta");
        ImGui::InputText("BGM File Name", bgmBuf, sizeof(bgmBuf));
        meta.bgm = bgmBuf;

        ImGui::InputDouble("BPM", &meta.bpm, 1.0, 10.0, "%.3f");
        if(meta.bpm <= 0.0) meta.bpm = 1.0;

        ImGui::InputText("Composer", composerBuf, sizeof(composerBuf));
        meta.composer = composerBuf;

        ImGui::InputText("Level", levelBuf, sizeof(levelBuf));
        meta.level = levelBuf;

        ImGui::InputText("ChartCreator", creatorBuf, sizeof(creatorBuf));
        meta.chartCreator = creatorBuf;

        ImGui::InputDouble("Offset(ms)", &meta.offsetMs, 1.0, 10.0, "%.1f");
        ImGui::TextDisabled("+:late  -:fast");
        ImGui::Separator();
        ImGui::InputText("Save Path", savePathBuf, sizeof(savePathBuf));
        
        if(ImGui::Button("SAVE")){
            saveChart(savePathBuf, meta, notes, speedEvents, bpmEvents);
        }
        ImGui::SameLine();
        if(ImGui::Button("LOAD")){
            loadChartForEdit(savePathBuf, meta, notes, speedEvents, bpmEvents);
            snprintf(bgmBuf, sizeof(bgmBuf), "%s", meta.bgm.c_str());
            snprintf(composerBuf, sizeof(composerBuf), "%s", meta.composer.c_str());
            snprintf(levelBuf, sizeof(levelBuf), "%s", meta.level.c_str());
            snprintf(creatorBuf, sizeof(creatorBuf), "%s", meta.chartCreator.c_str());
            selectedNoteIndex = -1;
        }
        ImGui::End();

        //再生
        ImGui::Begin("Playback");
        if(ImGui::Button((isPlaying) ? "STOP" : "START")){
            togglePlayback();
        }

        ImGui::SameLine();
        if(ImGui::Button("RETURN BEGIN")){
            scrollTimeMs = 0;
            if(bgm) Mix_HaltMusic();
            isPlaying = false;
            musicNeedsStart = true;
        }

        ImGui::Text("NOW TIME: %d ms", scrollTimeMs);

        int32_t maxScrubMs = 60000;
        for(const auto& n : notes){
            int32_t noteEnd = n.time + n.duration;
            if(noteEnd + 5000 > maxScrubMs) maxScrubMs = noteEnd + 5000;
        }

        int scrubValue = scrollTimeMs;
        if(ImGui::SliderInt("SEEK", &scrubValue, 0, maxScrubMs)){
            scrollTimeMs = scrubValue;
            if(bgm){
                seekMusicIfNeeded(scrollTimeMs);
            }
        }

        ImGui::InputDouble("ZOOM", &pixelsPerMs, 0.01, 0.1, "%.3f");
        if(pixelsPerMs < 0.02) pixelsPerMs = 0.02;
        if(pixelsPerMs > 3.0) pixelsPerMs = 3.0;

        ImGui::InputInt("Cold Start Latency(ms)", &editorAudioLatencyMs, 1, 10);
        ImGui::TextDisabled("RETURN BEGIN SETTING");
        ImGui::End();


        //ツールバー
        ImGui::Begin("Tool");
        int typeComboIndex = (toolNotetype == 3) ? 1 : (toolNotetype == 4) ? 2 : 0;
        if(ImGui::Combo("SEPARATE TYPE", &typeComboIndex, typenames, 3)){
            toolNotetype = (typeComboIndex == 1) ? 3 : (typeComboIndex == 2) ? 4 : 0;
        }

        ImGui::SliderInt("WidthLanes", &toolWidth, 1, 6);

        ImGui::Checkbox("IsLong", &toolIsLong);
        if(toolIsLong){
            ImGui::SliderInt("Duration(ms)", &toolDurationMs, 100, 5000);
        }
        ImGui::Checkbox("IsCustomNoteSpeed", &toolHasCustomSpeed);
        if(toolHasCustomSpeed){
            ImGui::InputDouble("CustomSpeed", &toolCustomSpeed, 0.1, 1.0, "%.2f");
        }

        const char* gridNames[] = {"4", "6", "8", "12", "16","24", "32", "48", "64", "96", "128", "1920"};
        int gridValue[] = {4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 1920};
        int gridComboIndex = 0;
        for(int gi = 0; gi < 12; gi++) if(gridValue[gi] == gridDivisor) gridComboIndex = gi;
        if(ImGui::Combo("SNAP", &gridComboIndex, gridNames, 12)){
            gridDivisor = gridValue[gridComboIndex];
        }

        ImGui::TextWrapped("LEFT: CONFIGRATION/SELECT  RIGHT: DELETE  WHEEL: ZOOM");
        ImGui::End();

        //ノーツインスペクタ
        if(selectedNoteIndex >= 0){
            EditorNote& n = notes[selectedNoteIndex];
            ImGui::Begin("Note Inspector");

            int nTypeComboIndex = (n.noteTypeInt == 3) ? 1 : (n.noteTypeInt == 4) ? 2 : 0;
            if(ImGui::Combo("SEPARATE TYPE##inspector", &nTypeComboIndex, typenames, 3)){
                n.noteTypeInt = (nTypeComboIndex == 1) ? 3 : (nTypeComboIndex == 2) ? 4 : 0;
            }

            ImGui::SliderInt("レーン", &n.lane, 0, 5);
            ImGui::SliderInt("WIDTH##inspector", &n.width, 1, 6 - n.lane);

            ImGui::InputInt("TIME(ms)", &n.time);

            bool isLongFlag = n.isLong();
            if(ImGui::Checkbox("LONG NOTE##inspector", &isLongFlag)){
                n.duration = isLongFlag ? std::max(100, static_cast<int>(n.duration)) : 0;
            }
            if(isLongFlag){
                ImGui::InputInt("DURATION(ms)##inspector", &n.duration);
                if(n.duration < 1) n.duration = 1;
            }

            ImGui::Checkbox("SPEED##inspector", &n.hasCustomSpeed);
            if(n.hasCustomSpeed){
                ImGui::InputDouble("SPEED VALUE##inspector", &n.customSpeed, 0.1, 1.0, "%.2f");
            }

            if(ImGui::Button("DELETE THIS NOTE")){
                notes.erase(notes.begin() + selectedNoteIndex);
                selectedNoteIndex = -1;
            }

            ImGui::End();
        }

        //speedEventsエディタ
        ImGui::Begin("speed Events");
        if(ImGui::Button("ADD")){
            EditorSpeedEvent ev;
            ev.time = scrollTimeMs;
            speedEvents.push_back(ev);
        }

        int removeIndex = -1;
        for(size_t idx = 0; idx < speedEvents.size(); idx++){
            ImGui::PushID(static_cast<int>(idx));
            auto& ev = speedEvents[idx];

            ImGui::Text("#%zu", idx);
            ImGui::InputInt("TIME(ms)", &ev.time);
            ImGui::InputDouble("TARGET SPEED", &ev.target, 0.1, 1.0, "%.2f");

            const char* easeNames[] = {"Straight(1)", "easeOutCubic(2)", "easeInCubic(3)"};
            int easeIndex = ev.easing - 1;
            if(easeIndex < 0 || easeIndex > 2) easeIndex = 0; 
            if(ImGui::Combo("EASING", &easeIndex, easeNames, 3)){
                ev.easing = easeIndex + 1;
            }

            ImGui::InputInt("DURATION TIME(ms)", &ev.duration);

            if(ImGui::Button("DELETE")){
                removeIndex = static_cast<int>(idx);
            }

            ImGui::Separator();
            ImGui::PopID();
        }
        if(removeIndex >= 0){
            speedEvents.erase(speedEvents.begin() + removeIndex);
        }
        ImGui::End();

        ImGui::Begin("BPM Events");
        
        if(ImGui::Button("ADD##bpm")){
            EditorBpmEvent b;
            b.time = scrollTimeMs;
            b.bpm = meta.bpm;
            bpmEvents.push_back(b);
        }

        int removeBpmIndex = -1;
        for(size_t idx = 0; idx < bpmEvents.size(); idx++){
            ImGui::PushID(static_cast<int>(idx) + 100000);
            auto& b = bpmEvents[idx];

            ImGui::Text("#%zu", idx);
            ImGui::InputInt("TIME(ms)##bpm", &b.time);
            ImGui::InputDouble("BPM##bpm", &b.bpm, 1.0, 10.0, "%.3f");
            if(b.bpm <= 0.0) b.bpm = 1.0;
            if(ImGui::Button("DELETE##bpm")){
                removeBpmIndex = static_cast<int>(idx);
            }

            ImGui::Separator();
            ImGui::PopID();
        }
        if(removeBpmIndex >= 0){
            bpmEvents.erase(bpmEvents.begin() + removeBpmIndex);
        }
        ImGui::End();


        //SDL描画　タイムライン等など
        SDL_SetRenderDrawColor(renderer, 15, 15, 20, 255);
        SDL_RenderClear(renderer);

        for(int i = 0; i < 6; i++){
            SDL_Rect laneRect = {laneX[i], 0, laneWidth, SCREEN_H};
            if(i % 2 == 0) SDL_SetRenderDrawColor(renderer, 40, 40, 50, 255);
            else SDL_SetRenderDrawColor(renderer, 20, 20, 25, 255);
            SDL_RenderFillRect(renderer, &laneRect);
        }
        for(int i = 0; i <= 6; i++){
            SDL_SetRenderDrawColor(renderer, 80, 80, 90, 255);
            int lx = startX + laneWidth * i;
            SDL_RenderDrawLine(renderer, lx, 0, lx, SCREEN_H);
        }

        {
        double beatDurationMs = 60000.0 / meta.bpm;
        double gridMs = (4.0 / gridDivisor) * beatDurationMs;
        int stepsPerBeat = gridDivisor / 4; // 4,8,12,16,24,32はすべて4の倍数なので割り切れる

        int32_t viewStartTime = static_cast<int32_t>(scrollTimeMs - (SCREEN_H - judgeY) / effectivePixelsPerMs); // 画面下端(過去側)
        int32_t viewEndTime   = static_cast<int32_t>(scrollTimeMs + judgeY / effectivePixelsPerMs);              // 画面上端(未来側)

        int32_t firstGridIndex = static_cast<int32_t>(std::floor(viewStartTime / gridMs));
        for(int32_t gi = firstGridIndex; ; gi++){
            double t = gi * gridMs;
            if(t > viewEndTime) break;

            int gy = judgeY - static_cast<int>((t - scrollTimeMs) * effectivePixelsPerMs);

            bool isBeatLine = (stepsPerBeat > 0) && (std::abs(gi) % stepsPerBeat == 0);
            if(isBeatLine) SDL_SetRenderDrawColor(renderer, 120, 120, 140, 255);
            else SDL_SetRenderDrawColor(renderer, 50, 50, 60, 255);

            SDL_RenderDrawLine(renderer, startX, gy, startX + laneWidth * 6, gy);
        }    
    }

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDrawLine(renderer, 0, judgeY, SCREEN_W, judgeY);

    for(size_t idx = 0; idx < notes.size(); idx++){
        const auto& n = notes[idx];
        int noteY = judgeY - static_cast<int>((n.time - scrollTimeMs) * effectivePixelsPerMs);

        if(n.isLong()){
            int tailY = judgeY - static_cast<int>((n.time + n.duration - scrollTimeMs) * effectivePixelsPerMs);
            SDL_Rect bodyRect;
            bodyRect.x = laneX[n.lane];
            bodyRect.w = laneWidth * n.width;
            bodyRect.y = tailY;
            bodyRect.h = noteY - tailY;
            SDL_SetRenderDrawColor(renderer, 0, 150, 255, 120);
            SDL_RenderFillRect(renderer, &bodyRect);
        }

        SDL_Rect noteRect;
        noteRect.x = laneX[n.lane];
        noteRect.y = noteY - 15;
        noteRect.w = laneWidth * n.width;
        noteRect.h = 30;

        if(n.noteTypeInt == 3) SDL_SetRenderDrawColor(renderer, 250, 250, 150, 255);
        else if(n.noteTypeInt == 4) SDL_SetRenderDrawColor(renderer, 255, 50, 50, 255);
        else SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        SDL_RenderFillRect(renderer, &noteRect);

        if(static_cast<int>(idx) == selectedNoteIndex){
            SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
            SDL_Rect outline = {noteRect.x - 3, noteRect.y - 3, noteRect.w + 6, noteRect.h + 6};
            SDL_RenderDrawRect(renderer, &outline);
        }
    }

    {
    ImDrawList* fgDraw = ImGui::GetForegroundDrawList();

    // SpeedEvent: 左側にラベル、線は黄色系
    for(const auto& s : speedEvents){
        int ly = judgeY - static_cast<int>((s.time - scrollTimeMs) * effectivePixelsPerMs);
        if(ly < -20 || ly > SCREEN_H + 20) continue;

        SDL_SetRenderDrawColor(renderer, 255, 220, 80, 200);
        SDL_RenderDrawLine(renderer, 0, ly, SCREEN_W, ly);

        char buf[64];
        snprintf(buf, sizeof(buf), "SPD %.2fx", s.target);
        fgDraw->AddText(ImVec2(90.0f, static_cast<float>(ly - 8)), IM_COL32(255, 220, 80, 255), buf);
    }

    // BPM Event: 右側にラベル、線は水色系
    for(const auto& b : bpmEvents){
        int ly = judgeY - static_cast<int>((b.time - scrollTimeMs) * effectivePixelsPerMs);
        if(ly < -20 || ly > SCREEN_H + 20) continue;

            SDL_SetRenderDrawColor(renderer, 80, 220, 255, 200);
            SDL_RenderDrawLine(renderer, 0, ly, SCREEN_W, ly);

            char buf[64];
            snprintf(buf, sizeof(buf), "BPM %.1f", b.bpm);
            ImVec2 textSize = ImGui::CalcTextSize(buf);
            float rightX = static_cast<float>(SCREEN_W) - textSize.x - 90.0f;
            fgDraw->AddText(ImVec2(rightX, static_cast<float>(ly - 8)), IM_COL32(80, 220, 255, 255), buf);
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for(const auto& fx : flashEffects){
        double progress = (nowTicks - fx.spawnTime) / 200.0;
        int alpha = static_cast<int>((1.0 - progress) * 220);
        SDL_SetRenderDrawColor(renderer, 0, 255, 255, alpha);
        SDL_Rect fxRect = {laneX[fx.lane], judgeY - 20, laneWidth, 40};
        SDL_RenderFillRect(renderer, &fxRect);
    }

    //ImGui描画をSDL描画に合成
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);

    SDL_RenderPresent(renderer);
    SDL_Delay(16);
}

if(bgm){
    Mix_HaltMusic();
    Mix_FreeMusic(bgm);
}

ImGui_ImplSDLRenderer2_Shutdown();
ImGui_ImplSDL2_Shutdown();
ImGui::DestroyContext();
Mix_FreeChunk(tap_sound);

return nextScene;
}