#include "GameCommon.h"
#include <nlohmann/json.hpp>
#include <limits>
#include "Editortempo.h"
#include "bpmdetect.h"
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

static const char* kNoteTypeNames[]  = {"Normal(0)", "Drag(3)", "Lane(4)", "Normal_c(5)"};
static const int   kNoteTypeValues[] = {0, 3, 4, 5};
static constexpr int kNoteTypeCount  = 4;

inline int noteTypeToComboIndex(int typeValue){
    for(int i = 0; i < kNoteTypeCount; i++){
        if(kNoteTypeValues[i] == typeValue) return i;
    }
    return 0;
}
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
    std::string name = "EASY";
    std::string level = "0";
    std::string chartCreator = "Unknown";
    double bpm = 120.0;

    std::vector<EditorNote> notes;
    std::vector<EditorSpeedEvent> speedEvents;
    std::vector<EditorBpmEvent> bpmEvents;

    TempoMap alignedMap;
    bool tempoAligned = false;
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

    std::ofstream out(toUtf8Path(path));
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
    d.name = src.value("name", std::string("EASY"));
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
    std::ifstream file(toUtf8Path(path));
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
    uint32_t lastDriftCheckTicks = 0;

    int editorAudioLatencyMs = 40;
    int32_t pendingAudioLatencyMs = 0;
    bool musicNeedsStart = true;

    Mix_Chunk* tap_sound = Mix_LoadWAV("sounds/tapsound_2.wav");
    Mix_Chunk* tap_sound_c = Mix_LoadWAV("sounds/tapsound_2.wav");
    if(!tap_sound && !tap_sound_c){
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

    int gridDivisor = 16; //標準で16分

    auto currentTempoMap = [&]() -> TempoMap {
        std::vector<TempoPoint> pts;
        for(const auto& b : currentDiff().bpmEvents) pts.push_back({static_cast<double>(b.time), b.bpm});
        return makeTempoMap(currentDiff().bpm, pts);
    };
    // 最も近いグリッド線へ吸着
    auto snapToGrid = [&](double rawTimeMs) -> int32_t {
        return static_cast<int32_t>(std::lround(tempoSnap(currentTempoMap(), rawTimeMs, 4.0 / gridDivisor)));
    };
    // 次(+1)/前(-1)のグリッド線へ
    auto stepGrid = [&](int32_t fromMs, int dir) -> int32_t {
        return tempoStepMs(currentTempoMap(), fromMs, 4.0 / gridDivisor, dir);
    };
    // その時刻付近のグリッド1マスの長さ(ms)。当たり判定の許容幅に使う
    auto localGridMs = [&](double atMs) -> double {
        return 60000.0 / tempoBpmAt(currentTempoMap(), atMs) * (4.0 / gridDivisor);
    };

    //ツールバー設定
    int toolNotetype = 0;
    int toolWidth = 1;
    bool toolIsLong = false;
    int toolDurationMs = 500;
    bool toolHasCustomSpeed = false;
    double toolCustomSpeed = 1.8;

    int selectedNoteIndex = -1;
    bool moveNotesOnBpmChange = true;

    enum class DragMode{ None, MoveNote, ResizeTail, MoveSpeedEvent, MoveBpmEvent, ResizeSpeedDuration };
    DragMode dragMode = DragMode::None;
    int dragNoteIndex = -1;
    int dragEventIndex = -1;
    BpmAnalysisResult bpmResult;
    bool hasBpmResult = false;
    TempoMapResult tempoMap;
    bool hasTempoMap = false;
    bool replaceBpmEvents = true;

    bool running = true;
    SDL_Event e;
    GameScene nextScene = GameScene::Select;

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

        if(isPlaying && bgm && pendingAudioLatencyMs == 0 &&
        Mix_PlayingMusic() && Mix_PausedMusic() == 0 &&
        (nowTicks - lastDriftCheckTicks) >= 200){
            lastDriftCheckTicks = nowTicks;

            double actualAudioMs = Mix_GetMusicPosition(bgm) * 1000.0;
            if(actualAudioMs >= 0.0){
                // seekMusicIfNeededの「音声位置 = chartTime - offset」という関係式の逆算
                double predictedAudioMs = static_cast<double>(scrollTimeMs) + meta.offsetMs;
                double drift = actualAudioMs - predictedAudioMs;

                if(std::abs(drift) > 5.0){
                    scrollTimeMs += static_cast<int32_t>(drift * 0.2);
                }
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
                scrollTimeMs = stepGrid(scrollTimeMs, (e.key.keysym.sym == SDLK_w) ? +1 : -1);

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
                    if(e.wheel.y < 0) scrollTimeMs = stepGrid(scrollTimeMs, -1);
                    else if(e.wheel.y > 0) scrollTimeMs = stepGrid(scrollTimeMs, +1);

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

                    SDL_Keymod mod = SDL_GetModState();
                    bool ctrlHeld = (mod & KMOD_CTRL) != 0;

                    auto& speedEvents = currentDiff().speedEvents;

                    if(ctrlHeld){
                        for(size_t si = 0; si < speedEvents.size(); si++){
                            int ey = judgeY - static_cast<int>((speedEvents[si].time + speedEvents[si].duration - scrollTimeMs) * effectivePixelsPerMs);
                            if(std::abs(my - ey) <= LINE_HIT_PX){
                                dragMode = DragMode::ResizeSpeedDuration;
                                dragEventIndex = static_cast<int>(si);
                                grabbedEventLine = true;
                                break;
                            }
                        }
                    }

                    if(!grabbedEventLine){
                        for(size_t si = 0; si < speedEvents.size(); si++){
                            int ly = judgeY - static_cast<int>((speedEvents[si].time - scrollTimeMs) * effectivePixelsPerMs);
                            if(std::abs(my - ly) <= LINE_HIT_PX){
                                dragMode = DragMode::MoveSpeedEvent;
                                dragEventIndex = static_cast<int>(si);
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

                        double gridMs = localGridMs(rawTime);
                        int32_t snappedTime = snapToGrid(rawTime);
                        if(snappedTime < 0) snappedTime = 0;

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
                double gridMs = localGridMs(rawTime);
                int32_t snappedTime = snapToGrid(rawTime);
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
                else if(dragMode == DragMode::ResizeSpeedDuration){
                    auto& speedEvents = currentDiff().speedEvents;
                    if(dragEventIndex >= 0 && dragEventIndex < static_cast<int>(speedEvents.size())){
                        auto& ev = speedEvents[dragEventIndex];
                        int32_t newDuration = snappedTime - ev.time;
                        int32_t minDuration = static_cast<int32_t>(std::max(1.0, gridMs));   // 最低1グリッド分は確保
                        ev.duration = std::max(minDuration, newDuration);
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
                    Mix_Chunk* se = (n.noteTypeInt == 5 && !n.isLong() && tap_sound_c) ? tap_sound_c : tap_sound;
                    Mix_PlayChannel(-1, se, 0);
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

        if(ImGui::Button("ANALYZE BPM")){
            if(meta.bgm.empty()){
                bpmResult = BpmAnalysisResult{};
                bpmResult.error = "BGM File Name is empty";
            }
            else{
                bpmResult = detectBpmFromFile("sounds/" + meta.bgm);
            }
            hasBpmResult = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(UI freezes for ~1s)");

        if(hasBpmResult){
            if(!bpmResult.ok){
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", bpmResult.error.c_str());
            }
            else{
                ImGui::Text("Estimated: %.2f BPM  (clarity %.0f%%)", bpmResult.bpm, bpmResult.confidence * 100.0);
                ImGui::Text("Beat phase: about %.0f ms", bpmResult.beatPhaseMs);

                char lbl[3][48];
                snprintf(lbl[0], sizeof(lbl[0]), "USE %.2f", bpmResult.bpm);
                snprintf(lbl[1], sizeof(lbl[1]), "USE x2 = %.2f", bpmResult.bpm * 2.0);
                snprintf(lbl[2], sizeof(lbl[2]), "USE /2 = %.2f", bpmResult.bpm / 2.0);

                if(ImGui::Button(lbl[0])) currentDiff().bpm = bpmResult.bpm;
                ImGui::SameLine();
                if(ImGui::Button(lbl[1])) currentDiff().bpm = bpmResult.bpm * 2.0;
                ImGui::SameLine();
                if(ImGui::Button(lbl[2])) currentDiff().bpm = bpmResult.bpm / 2.0;
                ImGui::TextDisabled("Applies to the difficulty being edited");
            }
        }

        ImGui::Separator();
        if(ImGui::Button("ANALYZE TEMPO CHANGES")){
            if(meta.bgm.empty()){
                tempoMap = TempoMapResult{};
                tempoMap.error = "BGM File Name is empty";
            }
            else{
                tempoMap = detectTempoMapFromFile("sounds/" + meta.bgm);
            }
            hasTempoMap = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(UI freezes for ~1s)");

        if(hasTempoMap){
            if(!tempoMap.ok){
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", tempoMap.error.c_str());
            }
            else{
                ImGui::Text("Detected %zu section(s):", tempoMap.segments.size());
                for(size_t i = 0; i < tempoMap.segments.size(); i++){
                    ImGui::PushID(static_cast<int>(i) + 400000);
                    auto& sg = tempoMap.segments[i];
                    ImGui::Text("%7.0f ms : %.2f BPM", sg.startMs, sg.bpm);
                    ImGui::SameLine();
                    if(ImGui::SmallButton("x2")) sg.bpm *= 2.0;
                    ImGui::SameLine();
                    if(ImGui::SmallButton("/2")) sg.bpm /= 2.0;
                    ImGui::PopID();
                }
                if(ImGui::SmallButton("ALL x2##tempo")){
                    for(auto& sg : tempoMap.segments) sg.bpm *= 2.0;
                }
                ImGui::SameLine();
                if(ImGui::SmallButton("ALL /2##tempo")){
                    for(auto& sg : tempoMap.segments) sg.bpm /= 2.0;
                }

                ImGui::Checkbox("Replace existing BPM events", &replaceBpmEvents);
                if(ImGui::Button("APPLY TO CURRENT DIFFICULTY")){
                    EditorDifficulty& d = currentDiff();
                    if(replaceBpmEvents) d.bpmEvents.clear();

                    d.bpm = tempoMap.segments[0].bpm;
                    for(size_t i = 1; i < tempoMap.segments.size(); i++){
                        EditorBpmEvent b;
                        // 音源上の位置 → 譜面上の時刻(音声位置 = 譜面時刻 + Offset の関係に合わせる)
                        b.time = std::max(0, static_cast<int>(std::lround(tempoMap.segments[i].startMs - meta.offsetMs)));
                        b.bpm = tempoMap.segments[i].bpm;
                        d.bpmEvents.push_back(b);
                    }
                    std::sort(d.bpmEvents.begin(), d.bpmEvents.end(),
                        [](const EditorBpmEvent& a, const EditorBpmEvent& b){ return a.time < b.time; });
                }
                ImGui::TextDisabled("No undo. Uncheck Replace to keep existing events.");
            }
        }

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
        int typeComboIndex = noteTypeToComboIndex(toolNotetype);
        if(ImGui::Combo("SEPARATE TYPE", &typeComboIndex, kNoteTypeNames, kNoteTypeCount)){
            toolNotetype = kNoteTypeValues[typeComboIndex];
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

            int nTypeComboIndex = noteTypeToComboIndex(n.noteTypeInt);
            if(ImGui::Combo("SEPARATE TYPE##inspector", &nTypeComboIndex, kNoteTypeNames, kNoteTypeCount)){
                n.noteTypeInt = kNoteTypeValues[nTypeComboIndex];
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

        ImGui::Checkbox("Move notes on BPM change", &moveNotesOnBpmChange);
        ImGui::TextDisabled("Notes follow after editing ends (Enter / click away)");
        
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

        {
            EditorDifficulty& d = currentDiff();
            const TempoMap nowMap = currentTempoMap();

            if(!d.tempoAligned){
                d.alignedMap = nowMap;
                d.tempoAligned = true;
            }
            else if(!tempoEqual(d.alignedMap, nowMap)){
                const bool stillEditing = ImGui::IsAnyItemActive() || dragMode != DragMode::None;
                if(!stillEditing){
                    if(moveNotesOnBpmChange){
                        // 🌟 追加: BPMイベントの「時刻」自体が変わったかどうかで方針を分ける
                        const bool timesUnchanged = tempoBreakpointsSameTimes(d.alignedMap, nowMap);

                        for(auto& n : d.notes){
                            const int32_t newStart = remapTime(d.alignedMap, nowMap, n.time);
                            if(n.duration > 0){
                                const int32_t newEnd = remapTime(d.alignedMap, nowMap, n.time + n.duration);
                                n.duration = std::max<int32_t>(1, newEnd - newStart);
                            }
                            for(auto& k : n.path) k.time = remapTime(d.alignedMap, nowMap, k.time);
                            n.time = newStart;
                        }

                        // 🌟 追加: SpeedEventもノーツと同じように拍位置を保って追従させる
                        for(auto& s : d.speedEvents){
                            s.time = remapTime(d.alignedMap, nowMap, s.time);
                        }

                        // 🌟 追加: BPMイベント自身の時刻は、「値だけが変わった」場合に限って追従させる。
                        //          時刻そのものが変わった(ドラッグ・直接入力)場合は、
                        //          その新しい時刻がユーザーの意図した位置そのものなので触らない。
                        if(timesUnchanged){
                            for(auto& b : d.bpmEvents){
                                b.time = remapTime(d.alignedMap, nowMap, b.time);
                            }
                        }
                    }

                    // 🌟 修正: nowMap(変更前のBPMイベント配置を元にした参照用の表)ではなく、
                    //          実際に書き換えた後の現在のデータから作り直す。
                    //          これを怠ると、次のフレームで「まだズレている」と誤検出され、
                    //          意図せず再度動いてしまう。
                    d.alignedMap = currentTempoMap();
                }
            }
            else{
                d.alignedMap = nowMap;
            }
        }

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
            const TempoMap tm = currentTempoMap();
            const double gb = 4.0 / gridDivisor;   // グリッド1マスの拍数

            const double viewStartTime = scrollTimeMs - (SCREEN_H - judgeY) / effectivePixelsPerMs;
            const double viewEndTime   = scrollTimeMs + judgeY / effectivePixelsPerMs;

            const long kFirst = static_cast<long>(std::floor(tempoBeatAt(tm, viewStartTime) / gb));
            const long kLast  = static_cast<long>(std::ceil(tempoBeatAt(tm, viewEndTime) / gb));
            if(kLast - kFirst < 20000){
                for(long k = kFirst; k <= kLast; k++){
                    const double beat = k * gb;
                    const double t = tempoTimeAt(tm, beat);
                    const int gy = judgeY - static_cast<int>((t - scrollTimeMs) * effectivePixelsPerMs);

                    const bool isBeatLine = std::abs(beat - std::round(beat)) < 1.0e-6;
                    if(isBeatLine) SDL_SetRenderDrawColor(renderer, 120, 120, 140, 255);
                    else SDL_SetRenderDrawColor(renderer, 50, 50, 60, 255);

                    SDL_RenderDrawLine(renderer, startX, gy, startX + laneWidth * 6, gy);
                }
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
        else if(n.noteTypeInt == 5) SDL_SetRenderDrawColor(renderer, 80, 220, 255, 255);
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
        int ey = judgeY - static_cast<int>((s.time + s.duration - scrollTimeMs) * effectivePixelsPerMs);

        // 🌟 追加: 適用時間を薄い黄色の帯で表現する(画面内に一部でも重なっていれば描画)
        if(!(ly < -20 && ey < -20) && !(ly > SCREEN_H + 20 && ey > SCREEN_H + 20)){
            int yTop = std::min(ly, ey);
            int yBottom = std::max(ly, ey);
            SDL_SetRenderDrawColor(renderer, 255, 240, 150, 50);
            SDL_Rect bandRect = {0, yTop, SCREEN_W, yBottom - yTop};
            SDL_RenderFillRect(renderer, &bandRect);

            // 🌟 追加: 終端にも薄い線を引き、Ctrl+ドラッグで掴める位置を視覚的に示す
            SDL_SetRenderDrawColor(renderer, 255, 220, 80, 100);
            SDL_RenderDrawLine(renderer, 0, ey, SCREEN_W, ey);
        }

        if(ly < -20 || ly > SCREEN_H + 20) continue;   // 従来通り: 開始線自体が画面外ならラベル等はスキップ

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
}

if(bgm){
    Mix_HaltMusic();
    Mix_FreeMusic(bgm);
}

ImGui_ImplSDLRenderer2_Shutdown();
ImGui_ImplSDL2_Shutdown();
ImGui::DestroyContext();
Mix_FreeChunk(tap_sound);
Mix_FreeChunk(tap_sound_c);

return nextScene;
}