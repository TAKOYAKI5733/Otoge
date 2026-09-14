#include "GameCommon.h"
#include <nlohmann/json.hpp>
#include <limits>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

using json = nlohmann::json;

#define SCREEN_W 1920
#define SCREEN_H 1080

struct EditorPathKeyfrane{
    int32_t time = 0;
    double y = 0.0;
    int easing = 1;
};
struct EditorNote{
    int noteTypeInt = 0;
    int lane;
    int width = 1;
    int32_t time = 0;
    int32_t duration = 0; //0のときはノーマルノーツ処理
    bool hasCustomSpeed = false;
    double customSpeed = 1.0;

    std::vector<EditorPathKeyfrane> path;

    bool isLong() const {return duration > 0;}
};

struct EditorSpeedEvent{
    int32_t time = 0;
    double target = 1.0;
    int easing = 1;
    int32_t duration = 500;
};
struct FlashEffect{
    int lane;
    uint32_t spawnTime;
};

struct EditorBpmEvent{
    int32_t time = 0;
    double bpm = 120.0;
};

struct EditorDifficulty{
    std::string name = "EAZY";
    std::string level = "0";
    std::string chartCreator = "Unknown";
    double bpm = 120.0;

    std::vector<EditorNote> notes;
    std::vector<EditorSpeedEvent> speedEvents;
    std::vector<EditorBpmEvent> bpmEvents;
};

struct ChartMeta{
    std::string bgm;
    std::string composer = "Unknown";
    double offsetMs = 0.0;
};

//JSON保存・読込

inline void saveChart(const std::string& path, const ChartMeta& meta, const std::vector<EditorDifficulty>& difficulties){
    json j;
    j["bgm"] = meta.bgm;
    j["composer"] = meta.composer;
    j["offset"] = meta.offsetMs;

    json diffArr = json::array();
    for(const auto& d : difficulties){
        json dj;
        dj["name"] = d.name;
        dj["level"] = d.level;
        dj["chartCreator"] = d.chartCreator;
        dj["bpm"] = d.bpm;

        json notesArr = json::array();
        for(const auto& n : d.notes){
            json item;
            item["type"] = n.noteTypeInt;
            item["time"] = n.time;
            item["lane"] = n.lane;
            if(n.width != 1) item["width"] = n.width;
            if(n.duration > 0) item["duration"] = n.duration;
            if(n.hasCustomSpeed) item["speed"] = n.customSpeed;

            if(!n.path.empty()){
                json pathArr = json::array();
                for(const auto& k : n.path){
                    json kj;
                    kj["time"] = k.time;
                    kj["y"] = k.y;
                    kj["easing"] = k.easing;
                    pathArr.push_back(kj);
                }
                item["path"] = pathArr;
            }

            notesArr.push_back(item);
        }
        dj["notes"] = notesArr;

        json speedArr = json::array();
        for(const auto& s : d.speedEvents){
            json item;
            item["time"] = s.time;
            item["target"] = s.target;
            item["easing"] = s.easing;
            item["duration"] = s.duration;
            speedArr.push_back(item);
        }
        dj["speedEvents"] = speedArr;

        json bpmArr = json::array();
        for(const auto& b : d.bpmEvents){
            json item;
            item["time"] = b.time;
            item["bpm"] = b.bpm;
            bpmArr.push_back(item);
        }
        dj["bpmEvents"] = bpmArr;

        diffArr.push_back(dj);
    }
    j["difficulties"] = diffArr;

    std::ofstream out(path);
    if(out.is_open()){
        out << j.dump(2);
        printf("[譜面エディタ] 保存完了 : %s\n", path.c_str());
    }
    else{
        printf("[譜面エディタ] 保存失敗 : %s\n", path.c_str());
    }
}

inline EditorDifficulty parseOneDifficulty(const json& src){
    EditorDifficulty d;
    d.name = src.value("name", std::string("EAZY"));
    d.level = src.value("level", std::string("0"));
    d.chartCreator = src.value("chartCreator", std::string("Unknown"));
    d.bpm = src.value("bpm", 120.0);

    if(src.contains("notes")){
        for(const auto& item : src.at("notes")){
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

            if(item.contains("path")){
                for(const auto& kf : item.at("path")){
                    EditorPathKeyfrane k;
                    k.time = kf.value("time", 0);
                    k.y = kf.value("y", 0.0);
                    k.easing = kf.value("easing", 1);
                    n.path.push_back(k);
                } 
                std::sort(n.path.begin(), n.path.end(), [](const EditorPathKeyfrane& a, const EditorPathKeyfrane& b){
                    return a.time < b.time;
                });
            }

            d.notes.push_back(n);
        }
    }

    if(src.contains("speedEvents")){
        for(const auto& item : src.at("speedEvents")){
            EditorSpeedEvent s;
            s.time = item.value("time", 0);
            s.target = item.value("target", 1.0);
            s.easing = item.value("easing", 1);
            s.duration = item.value("duration", 500);
            d.speedEvents.push_back(s);
        }
    }

    if(src.contains("bpmEvents")){
        for(const auto& item : src.at("bpmEvents")){
            EditorBpmEvent b;
            b.time = item.value("time", 0);
            b.bpm = item.value("bpm", 120.0);
            d.bpmEvents.push_back(b);
        }
    }

    return d;
}

inline bool loadChartForEdit(const std::string & path, ChartMeta& meta, std::vector<EditorDifficulty>& difficulties){
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
    meta.composer = j.value("composer", std::string("Unknown"));
    meta.offsetMs = j.value("offset", 0.0);

    difficulties.clear();
    if(j.contains("difficulties") && j.at("difficulties").is_array()){
        for(const auto& src : j.at("difficulties")){
            difficulties.push_back(parseOneDifficulty(src));
        }
    }
    else{
        difficulties.push_back(parseOneDifficulty(j));
    }

    if(difficulties.empty()){
        difficulties.push_back(EditorDifficulty{});
    }

    return true;
}

inline double evaluateEditorPathY(const EditorNote& n, int32_t musicTime){
    const auto& path = n.path;
    if(path.empty()) return 0.0;

    if(musicTime == n.time) return 0.0;

    if(musicTime <= path.front().time) return path.front().y;
    if(musicTime >= path.back().time) return path.back().y;

    for(size_t i = 1; i < path.size(); i++){
        if(musicTime <= path[i].time){
            const auto& k0 = path[i - 1];
            const auto& k1 = path[i];
            double t = static_cast<double>(musicTime - k0.time) / static_cast<double>(k1.time - k0.time);

            if(k1.easing == 2) t = easeOutCubic(t);
            else if(k1.easing == 3) t = easeInCubic(t);

            return k0.y + (k1.y - k0.y) * t;
        }
    }
    return path.back().y;
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

    //譜面データ
    ChartMeta meta;
    std::vector<EditorDifficulty> difficulties;
    int currentDiffIndex = 0;
    std::vector<FlashEffect> flashEffects;

    if(!scorePath.empty()){
        loadChartForEdit(scorePath, meta, difficulties);
    }

    if(difficulties.empty()){
        difficulties.push_back(EditorDifficulty{});
    }

    auto currentDiff = [&]() -> EditorDifficulty& { return difficulties[currentDiffIndex]; };

    char savePathBuf[256];
    snprintf(savePathBuf, sizeof(savePathBuf), "%s", scorePath.empty() ? "scores/new_chart.json" : scorePath.c_str());
    char bgmBuf[256];
    char composerBuf[128];
    char diffNameBuf[128];
    char diffLevelBuf[32];
    char diffCreatorBuf[128];

    auto syncMetaBuffers = [&](){
        snprintf(bgmBuf, sizeof(bgmBuf), "%s", meta.bgm.c_str());
        snprintf(composerBuf, sizeof(composerBuf), "%s", meta.composer.c_str());
    };

    auto syncDifficultyBuffers = [&](){
        snprintf(diffNameBuf, sizeof(diffNameBuf), "%s", currentDiff().name.c_str());
        snprintf(diffLevelBuf, sizeof(diffLevelBuf), "%s", currentDiff().level.c_str());
        snprintf(diffCreatorBuf, sizeof(diffCreatorBuf), "%s", currentDiff().chartCreator.c_str());
    };
    syncMetaBuffers();
    syncDifficultyBuffers();

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
        double result = currentDiff().bpm;
        int32_t bestTime = std::numeric_limits<int32_t>::min();
        for(const auto& b : currentDiff().bpmEvents){
            if(b.time <= timeMs && b.time > bestTime){
                bestTime = b.time;
                result = b.bpm;
            }
        }
        return (result > 0.0) ? result : currentDiff().bpm;
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

    enum class DragMode{ None, MoveNote, ResizeTail, MoveSpeedEvent, MoveBpmEvent };
    DragMode dragMode = DragMode::None;
    int dragNoteIndex = -1;
    int dragEventIndex = -1;

    bool running = true;
    SDL_Event e;
    GameScene nextScene = GameScene::Select;

    const char* typenames[] = {"Normal(0)", "Drag(3)", "Lane(4)"};

    while(running){
        uint32_t nowTicks = SDL_GetTicks();
        uint32_t frameDeltaMs = nowTicks - lastFrameticks;
        lastFrameticks = nowTicks;

        if(currentDiffIndex < 0) currentDiffIndex = 0;
        if(currentDiffIndex >= static_cast<int>(difficulties.size())) currentDiffIndex = static_cast<int>(difficulties.size()) - 1;

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
        double effectivePixelsPerMs = pixelsPerMs * (currentBpmAtPlayhead / currentDiff().bpm);

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
                double beatDurationMs = 60000.0 / currentDiff().bpm;
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
                    double beatDuration = 60000.0 / currentDiff().bpm;
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

                bool grabbedEventLine = false;
                if(e.button.button == SDL_BUTTON_LEFT){
                    const int LINE_HIT_PX = 10;

                    auto& speedEvents = currentDiff().speedEvents;
                    for(size_t si = 0; si < speedEvents.size(); si++){
                        int ly = judgeY - static_cast<int>((speedEvents[si].time - scrollTimeMs) * effectivePixelsPerMs);
                        if(std::abs(my - ly) <= LINE_HIT_PX){
                            dragMode = DragMode::MoveSpeedEvent;
                            dragEventIndex = static_cast<int>(si);
                            grabbedEventLine = true;
                            break;
                        }
                    }

                    if(!grabbedEventLine){
                        auto& bpmEvents = currentDiff().bpmEvents;
                        for(size_t bi = 0; bi < bpmEvents.size(); bi++){
                            int ly = judgeY - static_cast<int>((bpmEvents[bi].time - scrollTimeMs) * effectivePixelsPerMs);
                            if(std::abs(my - ly) <= LINE_HIT_PX){
                                dragMode = DragMode::MoveBpmEvent;
                                dragEventIndex = static_cast<int>(bi);
                                grabbedEventLine = true;
                                break;
                            }
                        }
                    }
                }

                if(!grabbedEventLine){
                    int clickedLane = -1;
                    for(int l = 0; l < 6; l++){
                        if(mx >= laneX[l] && mx < laneX[l] + laneWidth){
                            clickedLane = l;
                            break;
                        }
                    }

                    if(clickedLane >= 0){
                        double rawTime = scrollTimeMs + (judgeY - my) / effectivePixelsPerMs;

                        double beatDurationMs = 60000.0 / currentDiff().bpm;
                        double gridMs = (4.0 / gridDivisor) * beatDurationMs;
                        int32_t snappedTime = static_cast<int32_t>(std::round(rawTime / gridMs) * gridMs);

                        if(e.button.button == SDL_BUTTON_LEFT){
                            int tailHitIndex = -1;
                            for(size_t idx = 0; idx < currentDiff().notes.size(); idx++){
                                const auto& n = currentDiff().notes[idx];
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
                                for(size_t idx = 0; idx < currentDiff().notes.size(); idx++){
                                    const auto& n = currentDiff().notes[idx];
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

                                    currentDiff().notes.push_back(newNote);
                                    selectedNoteIndex = static_cast<int>(currentDiff().notes.size()) - 1;
                                }
                            }
                        }
                        else if(e.button.button == SDL_BUTTON_RIGHT){
                            for(size_t idx = 0; idx < currentDiff().notes.size(); idx++){
                                const auto& n = currentDiff().notes[idx];
                                if(clickedLane >= n.lane && clickedLane <= n.lane + n.width - 1 && std::abs(n.time - snappedTime) < gridMs / 2){
                                    currentDiff().notes.erase(currentDiff().notes.begin() + idx);
                                    if(selectedNoteIndex == static_cast<int>(idx)) selectedNoteIndex = -1;
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            if(e.type == SDL_MOUSEMOTION && dragMode != DragMode::None){

                int my = e.motion.y;
                double rawTime = scrollTimeMs + (judgeY - my) / effectivePixelsPerMs;
                double beatDurationMs = 60000.0 / currentDiff().bpm;
                double gridMs = (4.0 / gridDivisor) * beatDurationMs;
                int32_t snappedTime = static_cast<int32_t>(std::round(rawTime / gridMs) * gridMs);
                if(snappedTime < 0) snappedTime = 0;

                if(dragMode == DragMode::MoveNote || dragMode == DragMode::ResizeTail){
                    if(dragNoteIndex >= 0 && dragNoteIndex < static_cast<int>(currentDiff().notes.size())){
                        int mx = e.motion.x;
                        int hoverLane = -1;
                        for(int l = 0; l < 6; l++){
                            if(mx >= laneX[l] && mx < laneX[l] + laneWidth){
                                hoverLane = l;
                                break;
                            }
                        }

                        EditorNote& n = currentDiff().notes[dragNoteIndex];

                        if(dragMode == DragMode::MoveNote){
                            if(!n.path.empty()){
                                int32_t delta = snappedTime - n.time;
                                for(auto& k : n.path){
                                    k.time += delta;
                                }
                            }

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
                }
                else if(dragMode == DragMode::MoveSpeedEvent){
                    auto& speedEvents = currentDiff().speedEvents;
                    if(dragEventIndex >= 0 && dragEventIndex < static_cast<int>(speedEvents.size())){
                        speedEvents[dragEventIndex].time = snappedTime;
                    }
                }
                else if(dragMode == DragMode::MoveBpmEvent){
                    auto& bpmEvents = currentDiff().bpmEvents;
                    if(dragEventIndex >= 0 && dragEventIndex < static_cast<int>(bpmEvents.size())){
                        bpmEvents[dragEventIndex].time = snappedTime;
                    }
                }
            }

            if(e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT){
                if(dragMode == DragMode::MoveSpeedEvent){
                    std::sort(currentDiff().speedEvents.begin(), currentDiff().speedEvents.end(),
                        [](const EditorSpeedEvent& a, const EditorSpeedEvent& b){ return a.time < b.time; });
                }
                else if(dragMode == DragMode::MoveBpmEvent){
                    std::sort(currentDiff().bpmEvents.begin(), currentDiff().bpmEvents.end(),
                        [](const EditorBpmEvent& a, const EditorBpmEvent& b){ return a.time < b.time; });
                }

                dragMode = DragMode::None;
                dragNoteIndex = -1;
                dragEventIndex = -1;
            }
        }
        if(selectedNoteIndex >= static_cast<int>(currentDiff().notes.size())) selectedNoteIndex = -1;

        //オートプレイ : プレビュー
        if(isPlaying){
            for(const auto& n : currentDiff().notes){
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
        ImGui::Begin("Difficulties");
        
        if(ImGui::Button("ADD##diff")){
            EditorDifficulty newDiff;
            newDiff.name = "NEW";
            newDiff.level = "0";
            newDiff.chartCreator = currentDiff().chartCreator;
            newDiff.bpm = currentDiff().bpm;
            difficulties.push_back(newDiff);

            currentDiffIndex = static_cast<int>(difficulties.size()) - 1;
            selectedNoteIndex = -1;
            dragMode = DragMode::None;
            dragNoteIndex = -1;
            syncDifficultyBuffers();
        }
        ImGui::SameLine();

        bool canDelete = difficulties.size() > 1;
        if(!canDelete) ImGui::BeginDisabled();
            if(ImGui::Button("DELETE##diff")){
                difficulties.erase(difficulties.begin() + currentDiffIndex);
                if(currentDiffIndex >= static_cast<int>(difficulties.size())){
                    currentDiffIndex = static_cast<int>(difficulties.size()) - 1;
                }
                selectedNoteIndex = -1;
                dragMode = DragMode::None;
                dragNoteIndex = -1;
                syncDifficultyBuffers();
            }
        if(!canDelete) ImGui::EndDisabled();

        ImGui::Separator();

        for(size_t i = 0; i < difficulties.size(); i++){
            ImGui::PushID(static_cast<int>(i) + 200000);
            bool isSelected = (static_cast<int>(i) == currentDiffIndex);
            std::string label = difficulties[i].name + " (Lv." + difficulties[i].level + ")";
            if(ImGui::Selectable(label.c_str(), isSelected)){
                if(currentDiffIndex != static_cast<int>(i)){
                    currentDiffIndex = static_cast<int>(i);
                    selectedNoteIndex = -1;
                    dragMode = DragMode::None;
                    dragNoteIndex = -1;
                    syncDifficultyBuffers();
                }
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        ImGui::Text("Editing: %s", currentDiff().name.c_str());

        ImGui::InputText("Name##diff", diffNameBuf, sizeof(diffNameBuf));
        currentDiff().name = diffNameBuf;

        ImGui::InputText("Level##diff", diffLevelBuf, sizeof(diffLevelBuf));
        currentDiff().level = diffLevelBuf;

        ImGui::InputText("Chart Creator##diff", diffCreatorBuf, sizeof(diffCreatorBuf));
        currentDiff().chartCreator = diffCreatorBuf;

        ImGui::InputDouble("BPM##diff", &currentDiff().bpm, 1.0, 10.0, "%.3f");
        if(currentDiff().bpm <= 0.0) currentDiff().bpm = 1.0;

        ImGui::End();


        ImGui::Begin("File / Meta");
        ImGui::InputText("BGM File Name", bgmBuf, sizeof(bgmBuf));
        meta.bgm = bgmBuf;

        ImGui::InputText("Composer", composerBuf, sizeof(composerBuf));
        meta.composer = composerBuf;

        ImGui::InputDouble("Offset(ms)", &meta.offsetMs, 1.0, 10.0, "%.1f");
        ImGui::TextDisabled("-:late  +:fast");
        ImGui::Separator();
        ImGui::InputText("Save Path", savePathBuf, sizeof(savePathBuf));
        
        if(ImGui::Button("SAVE")){
            saveChart(savePathBuf, meta, difficulties);
        }
        ImGui::SameLine();
        if(ImGui::Button("LOAD")){
            loadChartForEdit(savePathBuf, meta, difficulties);
            if(difficulties.empty()) difficulties.push_back(EditorDifficulty{});
            currentDiffIndex = 0;
            syncMetaBuffers();
            syncDifficultyBuffers();
            selectedNoteIndex = -1;
            dragMode = DragMode::None;
            dragNoteIndex = -1;
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
        for(const auto& n : currentDiff().notes){
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
            EditorNote& n = currentDiff().notes[selectedNoteIndex];
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

            ImGui::Separator();
            bool hasPath = !n.path.empty();
            if(ImGui::Checkbox("CUSTOM PATH##inspector", &hasPath)){
                if(hasPath && n.path.empty()){
                    EditorPathKeyfrane start;
                    start.time = n.time - 1000;
                    start.y = 900.0;
                    start.easing = 1;

                    EditorPathKeyfrane  end;
                    end.time = n.time;
                    end.y = 0.0;
                    end.easing = 1;

                    n.path = { start, end };
                }
                else if(!hasPath){
                    n.path.clear();
                }
            }

            if(hasPath){
                ImGui::TextDisabled("Y = judge line distance(px). Independent of speed/zoom.");

                if(ImGui::Button("ADD KEYFRAME##path")){
                    EditorPathKeyfrane k;
                    k.time = scrollTimeMs;
                    k.y = static_cast<double>(judgeY - (judgeY - static_cast<int>((n.time - scrollTimeMs) * effectivePixelsPerMs)));
                    n.path.push_back(k);
                    std::sort(n.path.begin(), n.path.end(), [](const EditorPathKeyfrane& a, const EditorPathKeyfrane& b){
                        return a.time < b.time;
                    });
                }

                int removePathIdx = -1;
                for(size_t pi = 0; pi < n.path.size(); pi++){
                    ImGui::PushID(static_cast<int>(pi) + 300000);
                    auto& k = n.path[pi];

                    ImGui::Text("%zu", pi);
                    ImGui::InputInt("Time(ms)##path", &k.time);
                    ImGui::InputDouble("Y(px)##path", &k.y, 10.0, 50.0, "%.0f");

                    const char* pathEaseNames[] = {"Linear(1)", "easeOut(2)", "easeIn(3)"};
                    int pathEaseIdx = k.easing - 1;
                    if(pathEaseIdx < 0 || pathEaseIdx > 2) pathEaseIdx = 0;
                    if(ImGui::Combo("Easing##path", &pathEaseIdx, pathEaseNames, 3)){
                        k.easing = pathEaseIdx + 1;
                    }

                    if(ImGui::Button("DELETE##path")){
                        removePathIdx = static_cast<int>(pi);
                    }

                    ImGui::Separator();
                    ImGui::PopID();
                }
                if(removePathIdx >= 0){
                    n.path.erase(n.path.begin() + removePathIdx);
                }

                std::sort(n.path.begin(), n.path.end(), [](const EditorPathKeyfrane& a, const EditorPathKeyfrane& b){
                    return a.time < b.time;
                });
            }

            if(ImGui::Button("DELETE THIS NOTE")){
                currentDiff().notes.erase(currentDiff().notes.begin() + selectedNoteIndex);
                selectedNoteIndex = -1;
            }

            ImGui::End();
        }

        //speedEventsエディタ
        ImGui::Begin("speed Events");
        if(ImGui::Button("ADD")){
            EditorSpeedEvent ev;
            ev.time = scrollTimeMs;
            currentDiff().speedEvents.push_back(ev);
        }

        int removeIndex = -1;
        for(size_t idx = 0; idx < currentDiff().speedEvents.size(); idx++){
            ImGui::PushID(static_cast<int>(idx));
            auto& ev = currentDiff().speedEvents[idx];

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
            currentDiff().speedEvents.erase(currentDiff().speedEvents.begin() + removeIndex);
        }
        ImGui::End();

        ImGui::Begin("BPM Events");
        
        if(ImGui::Button("ADD##bpm")){
            EditorBpmEvent b;
            b.time = scrollTimeMs;
            b.bpm = currentDiff().bpm;
            currentDiff().bpmEvents.push_back(b);
        }

        int removeBpmIndex = -1;
        for(size_t idx = 0; idx < currentDiff().bpmEvents.size(); idx++){
            ImGui::PushID(static_cast<int>(idx) + 100000);
            auto& b = currentDiff().bpmEvents[idx];

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
            currentDiff().bpmEvents.erase(currentDiff().bpmEvents.begin() + removeBpmIndex);
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
        double beatDurationMs = 60000.0 / currentDiff().bpm;
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

    for(size_t idx = 0; idx < currentDiff().notes.size(); idx++){
        const auto& n = currentDiff().notes[idx];
        int noteY;
        if(!n.path.empty()){
            noteY = judgeY - static_cast<int>(evaluateEditorPathY(n, scrollTimeMs));
        }
        else{
            noteY = judgeY - static_cast<int>((n.time - scrollTimeMs) * effectivePixelsPerMs);
        }

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
    for(const auto& s : currentDiff().speedEvents){
        int ly = judgeY - static_cast<int>((s.time - scrollTimeMs) * effectivePixelsPerMs);
        if(ly < -20 || ly > SCREEN_H + 20) continue;

        SDL_SetRenderDrawColor(renderer, 255, 220, 80, 200);
        SDL_RenderDrawLine(renderer, 0, ly, SCREEN_W, ly);

        char buf[64];
        snprintf(buf, sizeof(buf), "SPD %.2fx", s.target);
        fgDraw->AddText(ImVec2(90.0f, static_cast<float>(ly - 8)), IM_COL32(255, 220, 80, 255), buf);
    }

    // BPM Event: 右側にラベル、線は水色系
    for(const auto& b : currentDiff().bpmEvents){
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