#include "GameCommon.h"
#include <nlohmann/json.hpp>
#include <limits>
#include <cfloat>
#include <string>
#include <vector>
#include "Editortempo.h"
#include "bpmdetect.h"
#include "Play/drawFrame.h"      // NOTE_SIDE_MARGIN などノーツの形（プレイ画面と共通）
#include "Play/roundedRect.h"    // 角丸の描画（プレイ画面と共通）
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
    int width = 1;
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

// 保存できたら true（画面に「保存しました／失敗しました」を出すのに使う）
inline bool saveChart(const std::string& path, const ChartMeta& meta, const std::vector<EditorDifficulty>& difficulties){
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
        return true;
    }
    else{
        printf("[譜面エディタ] 保存失敗 : %s\n", path.c_str());
        return false;
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

// =====================================================================
//  エディタの見た目（選曲画面のウィンドウにそろえる）
// =====================================================================
namespace editorui {

// 固定パネルの位置 {x, y, w, h}。中央（x = 600〜1320）がレーン
constexpr SDL_Rect TOOL_RECT = {  20,  60, 400, 300};
constexpr SDL_Rect DIFF_RECT = {  20, 410, 400, 330};
constexpr SDL_Rect PLAY_RECT = {  20, 790, 400, 260};
constexpr SDL_Rect PALETTE_RECT = {440, 60, 140, 470};   // ノーツのパレット（細い縦長。ホイールで送る）
constexpr SDL_Rect NOTE_RECT = {1340,  60, 560, 440};
constexpr SDL_Rect DATA_RECT = {1340, 550, 560, 500};

constexpr ImU32 COL_ACCENT   = IM_COL32(  0, 200, 255, 255);   // 枠・タブ（選曲画面のシアン）
constexpr ImU32 COL_TAB_TEXT = IM_COL32( 15,  15,  25, 255);   // タブの文字（暗色）

inline ImVec4 rgba(int r, int g, int b, int a = 255){
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

// ImGui の部品の色・角・余白を選曲画面に合わせる
inline void applyStyle(){
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark(&st);

    st.WindowRounding    = 0.0f;   // 選曲画面のウィンドウは角なし
    st.FrameRounding     = 0.0f;
    st.GrabRounding      = 0.0f;
    st.PopupRounding     = 0.0f;
    st.ScrollbarRounding = 0.0f;
    st.TabRounding       = 0.0f;
    st.WindowBorderSize  = 0.0f;   // ウィンドウの枠は beginPanel でシアン3pxを自前で描く
    st.FrameBorderSize   = 1.0f;   // 入力欄・ボタンは細い枠（選曲画面の項目枠と同じ）
    st.WindowPadding     = ImVec2(18.0f, 16.0f);
    st.FramePadding      = ImVec2(10.0f, 6.0f);
    st.ItemSpacing       = ImVec2(10.0f, 8.0f);
    st.ScrollbarSize     = 12.0f;

    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg]             = rgba( 10,  10,  20, 215);
    c[ImGuiCol_ChildBg]              = rgba(  0,   0,   0,   0);
    c[ImGuiCol_PopupBg]              = rgba( 15,  15,  28, 245);
    c[ImGuiCol_Border]               = rgba(110, 110, 150, 255);
    c[ImGuiCol_Text]                 = rgba(235, 235, 245, 255);
    c[ImGuiCol_TextDisabled]         = rgba(150, 150, 170, 255);
    c[ImGuiCol_FrameBg]              = rgba( 30,  30,  50, 200);
    c[ImGuiCol_FrameBgHovered]       = rgba( 45,  45,  80, 220);
    c[ImGuiCol_FrameBgActive]        = rgba( 55,  55, 100, 240);
    c[ImGuiCol_Button]               = rgba( 30,  30,  50, 220);
    c[ImGuiCol_ButtonHovered]        = rgba( 45,  45,  80, 240);
    c[ImGuiCol_ButtonActive]         = rgba(  0, 150, 200, 255);
    c[ImGuiCol_Header]               = rgba( 45,  45,  80, 220);
    c[ImGuiCol_HeaderHovered]        = rgba( 55,  55, 100, 240);
    c[ImGuiCol_HeaderActive]         = rgba(  0, 150, 200, 255);
    c[ImGuiCol_CheckMark]            = rgba(  0, 255, 255, 255);
    c[ImGuiCol_SliderGrab]           = rgba(  0, 200, 255, 255);
    c[ImGuiCol_SliderGrabActive]     = rgba(  0, 255, 255, 255);
    c[ImGuiCol_Separator]            = rgba(110, 110, 150, 255);
    c[ImGuiCol_Tab]                  = rgba( 30,  30,  50, 255);
    c[ImGuiCol_TabHovered]           = rgba( 45,  45,  80, 255);
    c[ImGuiCol_ScrollbarBg]          = rgba( 10,  10,  20,   0);
    c[ImGuiCol_ScrollbarGrab]        = rgba( 70,  70, 100, 255);
    c[ImGuiCol_ScrollbarGrabHovered] = rgba( 90,  90, 130, 255);
    c[ImGuiCol_ScrollbarGrabActive]  = rgba(  0, 200, 255, 255);
    c[ImGuiCol_TextSelectedBg]       = rgba(  0, 200, 255,  90);
}

// 選曲画面と同じ見た目のウィンドウを始める（必ず ImGui::End() と対にする）
//   ・位置と大きさは固定（ドラッグで動かない）
//   ・ImGui のタイトルバーは出さず、上辺の左にシアンのタブを、周りにシアン3pxの枠を自前で描く
inline void beginPanel(const char* id, const char* title, const SDL_Rect& r){
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(r.x), static_cast<float>(r.y)), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(r.w), static_cast<float>(r.h)), ImGuiCond_Always);
    ImGui::Begin(id, nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

    // ウィンドウ自身の描画リストに、画面全体へはみ出してよい状態で描く
    // （ウィンドウの中身より奥・ポップアップより奥になるので、ドロップダウンを隠さない）
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRectFullScreen();

    const ImVec2 textSize = ImGui::CalcTextSize(title);
    const ImVec2 tab0(static_cast<float>(r.x), static_cast<float>(r.y) - textSize.y - 10.0f);
    const ImVec2 tab1(static_cast<float>(r.x) + textSize.x + 28.0f, static_cast<float>(r.y));
    dl->AddRectFilled(tab0, tab1, COL_ACCENT);
    dl->AddText(ImVec2(tab0.x + 14.0f, tab0.y + 5.0f), COL_TAB_TEXT, title);

    const ImVec2 p0(static_cast<float>(r.x) + 1.5f, static_cast<float>(r.y) + 1.5f);
    const ImVec2 p1(static_cast<float>(r.x + r.w) - 1.5f, static_cast<float>(r.y + r.h) - 1.5f);
    dl->AddRect(p0, p1, COL_ACCENT, 0.0f, 0, 3.0f);

    dl->PopClipRect();
}

// ノーツの色（renderGame.h と同じ）
inline SDL_Color noteColor(int type){
    if(type == 3) return SDL_Color{250, 250, 150, 255};   // Drag
    if(type == 4) return SDL_Color{255,  50,  50, 255};   // Lane
    if(type == 5) return SDL_Color{ 80, 220, 255, 255};   // Normal_c
    return SDL_Color{255, 255, 255, 255};                  // Normal
}

// ノーツの上に出すマーク（例：「DRAG」「LANE / x1.5」「PATH」）。何もなければ空
inline std::string noteMarker(const EditorNote& n){
    std::string s;
    auto add = [&](const std::string& part){
        if(!s.empty()) s += " / ";
        s += part;
    };
    if(n.noteTypeInt == 3) add("DRAG");
    if(n.noteTypeInt == 4) add("LANE");
    if(n.hasCustomSpeed){
        char buf[24];
        std::snprintf(buf, sizeof(buf), "x%.2g", n.customSpeed);
        add(buf);
    }
    if(!n.path.empty()) add("PATH");
    return s;
}

inline ImU32 toImU32(SDL_Color c, int alpha = -1){
    return IM_COL32(c.r, c.g, c.b, (alpha < 0) ? c.a : alpha);
}

// ---------------------------------------------------------------------
//  ノーツの絵（パレット・ノーツ編集の種類ボタンで使う）
//    p0〜p1 の四角の中に、タイムラインと同じ形・色でノーツを描く
//    ・通常：中央に横長のヘッド
//    ・ロング：上下いっぱいの半透明の帯＋暗い芯、下にヘッド、上に白いつまみ（終点）
//    ・DRAG / LANE はヘッドの上に小さく文字を書く
// ---------------------------------------------------------------------
inline void drawNoteIcon(ImDrawList* dl, ImVec2 p0, ImVec2 p1, int type, bool isLong){
    const SDL_Color col = noteColor(type);
    const float w = p1.x - p0.x;
    const float h = p1.y - p0.y;
    const float headW = w * 0.8f;
    const float headH = std::min(16.0f, h * 0.45f);
    const float headX = p0.x + (w - headW) * 0.5f;
    const float rounding = headH * 0.35f;

    float headY;   // ヘッドの上端
    if(isLong){
        headY = p1.y - headH;
        // 帯
        dl->AddRectFilled(ImVec2(headX, p0.y), ImVec2(headX + headW, p1.y), toImU32(col, 170), rounding);
        // 芯（帯の30%の幅、ノーツ色を暗くしたもの）
        const float coreW = headW * 0.3f;
        const float coreX = headX + (headW - coreW) * 0.5f;
        dl->AddRectFilled(ImVec2(coreX, p0.y + 4.0f), ImVec2(coreX + coreW, headY + headH * 0.5f),
                          IM_COL32(static_cast<int>(col.r * 0.6), static_cast<int>(col.g * 0.6),
                                   static_cast<int>(col.b * 0.6), 200), coreW * 0.5f);
        // 終点のつまみ
        dl->AddRectFilled(ImVec2(headX + headW * 0.25f, p0.y), ImVec2(headX + headW * 0.75f, p0.y + 5.0f),
                          IM_COL32(255, 255, 255, 220), 2.5f);
    }
    else{
        headY = p0.y + (h - headH) * 0.5f;
    }
    dl->AddRectFilled(ImVec2(headX, headY), ImVec2(headX + headW, headY + headH), toImU32(col), rounding);

    const char* mark = (type == 3) ? "DRAG" : (type == 4) ? "LANE" : nullptr;
    if(mark){
        const float fs = std::min(13.0f, headH * 0.95f);
        ImFont* f = ImGui::GetFont();
        ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, mark);
        ImU32 tc = (type == 4) ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 30, 255);
        dl->AddText(f, fs, ImVec2(headX + headW * 0.5f - ts.x * 0.5f, headY + headH * 0.5f - ts.y * 0.5f), tc, mark);
    }
}

// パレットに並べるもの（上から順）。キーボードの 1〜7 もこの順
struct PaletteItem{
    const char* name;
    int type;
    bool isLong;
};
constexpr PaletteItem PALETTE[] = {
    {"通常",   0, false},
    {"通常",   0, true },
    {"DRAG",   3, false},
    {"DRAG",   3, true },
    {"LANE",   4, false},
    {"通常C",  5, false},
    {"通常C",  5, true },
};
constexpr int PALETTE_COUNT = static_cast<int>(sizeof(PALETTE) / sizeof(PALETTE[0]));

// パレットの1マス。クリックされたら true
//   InvisibleButton で「押せる透明な四角」を置き、その上に背景・ノーツの絵・名前を自前で描く
inline bool paletteButton(int index, bool selected){
    const PaletteItem& it = PALETTE[index];
    ImGui::PushID(index);

    const float w = ImGui::GetContentRegionAvail().x;
    const float h = 96.0f;
    const bool clicked = ImGui::InvisibleButton("##pal", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 p0 = ImGui::GetItemRectMin();
    const ImVec2 p1 = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 背景：選択中はシアンを薄く敷いて枠を付ける（選曲画面の「選択中の項目」と同じ考え方）
    if(selected)      dl->AddRectFilled(p0, p1, IM_COL32(0, 200, 255, 45));
    else if(hovered)  dl->AddRectFilled(p0, p1, IM_COL32(45, 45, 80, 200));
    dl->AddRect(p0, p1, selected ? COL_ACCENT : IM_COL32(110, 110, 150, 255), 0.0f, 0, selected ? 3.0f : 1.0f);

    // ノーツの絵（上側 60px）
    drawNoteIcon(dl, ImVec2(p0.x + 6.0f, p0.y + 8.0f), ImVec2(p1.x - 6.0f, p0.y + 62.0f), it.type, it.isLong);

    // 名前（下側）。ロング版は「ロング」を添える
    char label[32];
    std::snprintf(label, sizeof(label), "%s%s", it.name, it.isLong ? " ロング" : "");
    const float fs = 15.0f;
    ImFont* f = ImGui::GetFont();
    ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
    dl->AddText(f, fs, ImVec2((p0.x + p1.x) * 0.5f - ts.x * 0.5f, p0.y + 70.0f),
                selected ? IM_COL32(0, 255, 255, 255) : IM_COL32(235, 235, 245, 255), label);

    // 番号（左上）：キーボードの 1〜7 に対応
    char num[4];
    std::snprintf(num, sizeof(num), "%d", index + 1);
    dl->AddText(f, 13.0f, ImVec2(p0.x + 4.0f, p0.y + 2.0f), IM_COL32(150, 150, 170, 255), num);

    ImGui::PopID();
    return clicked;
}

} // namespace editorui

//譜面エディター
GameScene chartCreateScene(SDL_Window* window, SDL_Renderer* renderer, std::string& scorePath, SDL_Texture* targetTex){
    if(targetTex != nullptr){
        return GameScene::ChartCreate;
    }

    //Dear ImGui 初期化
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // パネルは固定なので imgui.ini に位置を保存しない

    ImFontConfig font_cfg;
    font_cfg.OversampleH = 2;
    font_cfg.OversampleV = 2;

    io.Fonts->AddFontFromFileTTF(
        "fonts/prac.ttf",
        20.0f,
        &font_cfg,
        io.Fonts->GetGlyphRangesJapanese()
    );

    // キーボードでの ImGui 操作（Nav）は使わない。
    // これを有効にすると、ウィンドウを一度触っただけで「ImGui がキーボードを使う」状態になり、
    // Space で再生できなくなる／Space がフォーカス中のボタンを押してしまう。
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;

    editorui::applyStyle();
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
    if(!tap_sound || !tap_sound_c){
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
    const int lanesRight = startX + laneWidth * 6;   // レーン全体の右端

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
    int paletteIndex = 0;                 // パレットで選んでいるマス（editorui::PALETTE の番号）
    bool paletteScrollToSelected = false; // キーで選んだとき、そのマスが見える位置までパレットを送る

    // 保存したときの通知（画面上部に少しの間「保存しました」を出す）
    uint32_t saveToastTicks = 0;          // 保存した時刻（0 = まだ保存していない）
    bool saveToastOk = false;             // 保存できたか
    constexpr uint32_t SAVE_TOAST_MS = 1500;
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

            // 「文字を入力している最中」だけキーを ImGui に譲る。
            //   WantCaptureKeyboard は、ウィンドウを触っただけでも true になることがあるので使わない。
            //   WantTextInput は、入力欄（名前・BPM など）にカーソルがあるときだけ true になる。
            bool imguiWantsKeyBoard = io.WantTextInput;

            if(!imguiWantsKeyBoard && e.type == SDL_KEYDOWN && e.key.repeat == 0 && e.key.keysym.sym == SDLK_SPACE){
                togglePlayback();
            }

            // 1〜7 キー：パレットのマスを選ぶ
            if(!imguiWantsKeyBoard && e.type == SDL_KEYDOWN && e.key.repeat == 0 &&
               e.key.keysym.sym >= SDLK_1 && e.key.keysym.sym < SDLK_1 + editorui::PALETTE_COUNT){
                paletteIndex = e.key.keysym.sym - SDLK_1;
                paletteScrollToSelected = true;
            }

            // Ctrl + S：保存（入力欄で文字を打っている最中でも保存できるように、imguiWantsKeyBoard は見ない）
            //   入力欄の中身は毎フレーム meta / difficulties に書き戻しているので、打ちかけの値もそのまま保存される
            const bool ctrlDown = (SDL_GetModState() & KMOD_CTRL) != 0;
            if(e.type == SDL_KEYDOWN && e.key.repeat == 0 && ctrlDown && e.key.keysym.sym == SDLK_s){
                saveToastOk = saveChart(savePathBuf, meta, difficulties);
                saveToastTicks = SDL_GetTicks();
                continue;   // 下の「S = 1マス戻る」には流さない
            }

            if(!imguiWantsKeyBoard && !ctrlDown && e.type == SDL_KEYDOWN && e.key.repeat == 0 && (e.key.keysym.sym == SDLK_w || e.key.keysym.sym == SDLK_s)){
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
                                    // 置くノーツの種類・ロングかどうかはパレットで選んだマスで決まる
                                    toolNotetype = editorui::PALETTE[paletteIndex].type;
                                    toolIsLong = editorui::PALETTE[paletteIndex].isLong;

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
                    fx.width = n.width;
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

        // =================================================================
        //  左上：TOOL（新しく置くノーツの設定）
        // =================================================================
        // =================================================================
        //  レーンの左：NOTES（ノーツのパレット）
        //    中身が欄より縦に長いので、ImGui が自動でスクロールできる窓にしてくれる。
        //    マウスがこの欄の上にあるときは io.WantCaptureMouse が true になるので、
        //    ホイールはパレットを送るだけで、譜面の時間は動かない。
        // =================================================================
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 6.0f));
        editorui::beginPanel("##palette", "NOTES", editorui::PALETTE_RECT);
        {
            for(int i = 0; i < editorui::PALETTE_COUNT; i++){
                const bool selected = (i == paletteIndex);
                if(editorui::paletteButton(i, selected)){
                    paletteIndex = i;
                }
                // キーで選んだときだけ、そのマスが欄の真ん中に来るように送る
                if(selected && paletteScrollToSelected){
                    ImGui::SetScrollHereY(0.5f);
                    paletteScrollToSelected = false;
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);

        toolNotetype = editorui::PALETTE[paletteIndex].type;
        toolIsLong = editorui::PALETTE[paletteIndex].isLong;

        // =================================================================
        //  左上：TOOL（新しく置くノーツの設定）
        // =================================================================
        editorui::beginPanel("##tool", "TOOL", editorui::TOOL_RECT);
        {
            ImGui::SliderInt("幅（レーン数）##tool", &toolWidth, 1, 6);

            if(toolIsLong){
                ImGui::SliderInt("長さ(ms)##tool", &toolDurationMs, 100, 5000);
            }
            else{
                ImGui::TextDisabled("長さ: ロングを選ぶと設定できます");
            }
            ImGui::Checkbox("個別の速度##tool", &toolHasCustomSpeed);
            if(toolHasCustomSpeed){
                ImGui::InputDouble("速度##tool", &toolCustomSpeed, 0.1, 1.0, "%.2f");
            }

            const char* gridNames[] = {"4", "6", "8", "12", "16","24", "32", "48", "64", "96", "128", "1920"};
            int gridValue[] = {4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 1920};
            int gridComboIndex = 0;
            for(int gi = 0; gi < 12; gi++) if(gridValue[gi] == gridDivisor) gridComboIndex = gi;
            if(ImGui::Combo("スナップ（分）##tool", &gridComboIndex, gridNames, 12)){
                gridDivisor = gridValue[gridComboIndex];
            }

            ImGui::TextDisabled("左クリック: 置く/選ぶ  右: 消す");
            ImGui::TextDisabled("1〜7: ノーツ  Ctrl+ホイール: 拡大");
        }
        ImGui::End();

        // =================================================================
        //  左中：DIFFICULTY（難易度の切替と情報）
        // =================================================================
        editorui::beginPanel("##diff", "DIFFICULTY", editorui::DIFF_RECT);
        {
            if(ImGui::Button("追加##diff")){
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
                if(ImGui::Button("削除##diff")){
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

            ImGui::InputText("名前##diff", diffNameBuf, sizeof(diffNameBuf));
            currentDiff().name = diffNameBuf;

            ImGui::InputText("レベル##diff", diffLevelBuf, sizeof(diffLevelBuf));
            currentDiff().level = diffLevelBuf;

            ImGui::InputText("譜面製作者##diff", diffCreatorBuf, sizeof(diffCreatorBuf));
            currentDiff().chartCreator = diffCreatorBuf;

            ImGui::InputDouble("BPM##diff", &currentDiff().bpm, 1.0, 10.0, "%.3f");
            if(currentDiff().bpm <= 0.0) currentDiff().bpm = 1.0;
        }
        ImGui::End();

        // =================================================================
        //  左下：PLAYBACK（再生・位置・拡大率）
        // =================================================================
        editorui::beginPanel("##play", "PLAYBACK", editorui::PLAY_RECT);
        {
            if(ImGui::Button((isPlaying) ? "停止 (Space)" : "再生 (Space)")){
                togglePlayback();
            }

            ImGui::SameLine();
            if(ImGui::Button("先頭へ")){
                scrollTimeMs = 0;
                if(bgm) Mix_HaltMusic();
                isPlaying = false;
                musicNeedsStart = true;
            }

            ImGui::SameLine();
            ImGui::Text("%d ms", scrollTimeMs);

            int32_t maxScrubMs = 60000;
            for(const auto& n : currentDiff().notes){
                int32_t noteEnd = n.time + n.duration;
                if(noteEnd + 5000 > maxScrubMs) maxScrubMs = noteEnd + 5000;
            }

            int scrubValue = scrollTimeMs;
            if(ImGui::SliderInt("位置##play", &scrubValue, 0, maxScrubMs)){
                scrollTimeMs = scrubValue;
                if(bgm){
                    seekMusicIfNeeded(scrollTimeMs);
                }
            }

            ImGui::InputDouble("拡大率##play", &pixelsPerMs, 0.01, 0.1, "%.3f");
            if(pixelsPerMs < 0.02) pixelsPerMs = 0.02;
            if(pixelsPerMs > 3.0) pixelsPerMs = 3.0;

            ImGui::InputInt("再生開始の遅れ(ms)##play", &editorAudioLatencyMs, 1, 10);
            ImGui::TextDisabled("「先頭へ」から再生したときだけ使います");
        }
        ImGui::End();

        // =================================================================
        //  右上：NOTE（選択中のノーツ）
        // =================================================================
        editorui::beginPanel("##note", "NOTE", editorui::NOTE_RECT);
        if(selectedNoteIndex >= 0){
            EditorNote& n = currentDiff().notes[selectedNoteIndex];

            // 種類：ノーツの絵のボタンを4つ横に並べ、押したものに変える
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                for(int ti = 0; ti < kNoteTypeCount; ti++){
                    const int typeValue = kNoteTypeValues[ti];
                    ImGui::PushID(ti + 500000);
                    if(ti > 0) ImGui::SameLine();
                    if(ImGui::InvisibleButton("##type", ImVec2(110.0f, 44.0f))){
                        n.noteTypeInt = typeValue;
                    }
                    const bool hovered = ImGui::IsItemHovered();
                    const bool selected = (n.noteTypeInt == typeValue);
                    const ImVec2 p0 = ImGui::GetItemRectMin();
                    const ImVec2 p1 = ImGui::GetItemRectMax();
                    if(selected)      dl->AddRectFilled(p0, p1, IM_COL32(0, 200, 255, 45));
                    else if(hovered)  dl->AddRectFilled(p0, p1, IM_COL32(45, 45, 80, 200));
                    dl->AddRect(p0, p1, selected ? editorui::COL_ACCENT : IM_COL32(110, 110, 150, 255),
                                0.0f, 0, selected ? 3.0f : 1.0f);
                    editorui::drawNoteIcon(dl, ImVec2(p0.x + 6.0f, p0.y + 6.0f), ImVec2(p1.x - 6.0f, p1.y - 6.0f),
                                           typeValue, false);
                    if(hovered) ImGui::SetTooltip("%s", kNoteTypeNames[ti]);
                    ImGui::PopID();
                }
            }

            ImGui::SliderInt("レーン##inspector", &n.lane, 0, 5);
            ImGui::SliderInt("幅##inspector", &n.width, 1, 6 - n.lane);

            ImGui::InputInt("時刻(ms)##inspector", &n.time);

            bool isLongFlag = n.isLong();
            // LANE にはロングがないので、ロングにはできないようにする（すでにロングのものは外すことだけできる）
            const bool longLocked = (n.noteTypeInt == 4 && !isLongFlag);
            if(longLocked) ImGui::BeginDisabled();
            if(ImGui::Checkbox("ロングノーツ##inspector", &isLongFlag)){
                n.duration = isLongFlag ? std::max(100, static_cast<int>(n.duration)) : 0;
            }
            if(longLocked) ImGui::EndDisabled();
            if(isLongFlag){
                ImGui::InputInt("長さ(ms)##inspector", &n.duration);
                if(n.duration < 1) n.duration = 1;
            }

            ImGui::Checkbox("個別の速度##inspector", &n.hasCustomSpeed);
            if(n.hasCustomSpeed){
                ImGui::InputDouble("速度##inspector", &n.customSpeed, 0.1, 1.0, "%.2f");
            }

            ImGui::Separator();
            bool hasPath = !n.path.empty();
            if(ImGui::Checkbox("カスタムパス##inspector", &hasPath)){
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
                ImGui::TextDisabled("Y = 判定ラインからの距離(px)。速度・拡大率に関係なし");

                if(ImGui::Button("キーフレーム追加##path")){
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

                    ImGui::Text("#%zu", pi);
                    ImGui::InputInt("時刻(ms)##path", &k.time);
                    ImGui::InputDouble("Y(px)##path", &k.y, 10.0, 50.0, "%.0f");

                    const char* pathEaseNames[] = {"Linear(1)", "easeOut(2)", "easeIn(3)"};
                    int pathEaseIdx = k.easing - 1;
                    if(pathEaseIdx < 0 || pathEaseIdx > 2) pathEaseIdx = 0;
                    if(ImGui::Combo("イージング##path", &pathEaseIdx, pathEaseNames, 3)){
                        k.easing = pathEaseIdx + 1;
                    }

                    if(ImGui::Button("削除##path")){
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

            ImGui::Separator();
            if(ImGui::Button("このノーツを削除")){
                currentDiff().notes.erase(currentDiff().notes.begin() + selectedNoteIndex);
                selectedNoteIndex = -1;
            }
        }
        else{
            ImGui::TextDisabled("ノーツをクリックすると、ここで編集できます");
        }
        ImGui::End();

        // =================================================================
        //  右下：DATA（SPEED / BPM / FILE をタブで切替）
        // =================================================================
        editorui::beginPanel("##data", "DATA", editorui::DATA_RECT);
        if(ImGui::BeginTabBar("##datatabs")){

            // ----- SPEED：速度イベント -----
            if(ImGui::BeginTabItem("SPEED")){
                if(ImGui::Button("追加##speed")){
                    EditorSpeedEvent ev;
                    ev.time = scrollTimeMs;
                    currentDiff().speedEvents.push_back(ev);
                }

                int removeIndex = -1;
                for(size_t idx = 0; idx < currentDiff().speedEvents.size(); idx++){
                    ImGui::PushID(static_cast<int>(idx));
                    auto& ev = currentDiff().speedEvents[idx];

                    ImGui::Text("#%zu", idx);
                    ImGui::InputInt("時刻(ms)##speed", &ev.time);
                    ImGui::InputDouble("目標の速度##speed", &ev.target, 0.1, 1.0, "%.2f");

                    const char* easeNames[] = {"Straight(1)", "easeOutCubic(2)", "easeInCubic(3)"};
                    int easeIndex = ev.easing - 1;
                    if(easeIndex < 0 || easeIndex > 2) easeIndex = 0;
                    if(ImGui::Combo("イージング##speed", &easeIndex, easeNames, 3)){
                        ev.easing = easeIndex + 1;
                    }

                    ImGui::InputInt("変化にかける時間(ms)##speed", &ev.duration);

                    if(ImGui::Button("削除##speed")){
                        removeIndex = static_cast<int>(idx);
                    }

                    ImGui::Separator();
                    ImGui::PopID();
                }
                if(removeIndex >= 0){
                    currentDiff().speedEvents.erase(currentDiff().speedEvents.begin() + removeIndex);
                }
                ImGui::EndTabItem();
            }

            // ----- BPM：BPM 変化イベント -----
            if(ImGui::BeginTabItem("BPM")){
                ImGui::Checkbox("BPM を変えたらノーツも追従", &moveNotesOnBpmChange);
                ImGui::TextDisabled("入力を確定（Enter / 欄の外をクリック）したときに動きます");

                if(ImGui::Button("追加##bpm")){
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
                    ImGui::InputInt("時刻(ms)##bpm", &b.time);
                    ImGui::InputDouble("BPM##bpm", &b.bpm, 1.0, 10.0, "%.3f");
                    if(b.bpm <= 0.0) b.bpm = 1.0;
                    if(ImGui::Button("削除##bpm")){
                        removeBpmIndex = static_cast<int>(idx);
                    }

                    ImGui::Separator();
                    ImGui::PopID();
                }
                if(removeBpmIndex >= 0){
                    currentDiff().bpmEvents.erase(currentDiff().bpmEvents.begin() + removeBpmIndex);
                }
                ImGui::EndTabItem();
            }

            // ----- FILE：曲・作曲者・オフセット・保存／読込・BPM解析 -----
            if(ImGui::BeginTabItem("FILE")){
                ImGui::InputText("曲ファイル名##file", bgmBuf, sizeof(bgmBuf));
                meta.bgm = bgmBuf;

                ImGui::InputText("作曲者##file", composerBuf, sizeof(composerBuf));
                meta.composer = composerBuf;

                ImGui::InputDouble("オフセット(ms)##file", &meta.offsetMs, 1.0, 10.0, "%.1f");
                ImGui::TextDisabled("- : 遅く  + : 早く");

                ImGui::Separator();
                ImGui::InputText("保存先##file", savePathBuf, sizeof(savePathBuf));

                if(ImGui::Button("保存##file")){
                    saveToastOk = saveChart(savePathBuf, meta, difficulties);
                    saveToastTicks = SDL_GetTicks();
                }
                ImGui::SameLine();
                if(ImGui::Button("読込##file")){
                    loadChartForEdit(savePathBuf, meta, difficulties);
                    if(difficulties.empty()) difficulties.push_back(EditorDifficulty{});
                    currentDiffIndex = 0;
                    syncMetaBuffers();
                    syncDifficultyBuffers();
                    selectedNoteIndex = -1;
                    dragMode = DragMode::None;
                    dragNoteIndex = -1;
                }

                ImGui::Separator();
                if(ImGui::Button("BPM を解析")){
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
                ImGui::TextDisabled("（1秒ほど止まります）");

                if(hasBpmResult){
                    if(!bpmResult.ok){
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", bpmResult.error.c_str());
                    }
                    else{
                        ImGui::Text("推定: %.2f BPM（確からしさ %.0f%%）", bpmResult.bpm, bpmResult.confidence * 100.0);
                        ImGui::Text("拍の位置: 約 %.0f ms", bpmResult.beatPhaseMs);

                        char lbl[3][48];
                        snprintf(lbl[0], sizeof(lbl[0]), "%.2f", bpmResult.bpm);
                        snprintf(lbl[1], sizeof(lbl[1]), "x2 = %.2f", bpmResult.bpm * 2.0);
                        snprintf(lbl[2], sizeof(lbl[2]), "/2 = %.2f", bpmResult.bpm / 2.0);

                        if(ImGui::Button(lbl[0])) currentDiff().bpm = bpmResult.bpm;
                        ImGui::SameLine();
                        if(ImGui::Button(lbl[1])) currentDiff().bpm = bpmResult.bpm * 2.0;
                        ImGui::SameLine();
                        if(ImGui::Button(lbl[2])) currentDiff().bpm = bpmResult.bpm / 2.0;
                        ImGui::TextDisabled("編集中の難易度に反映されます");
                    }
                }

                ImGui::Separator();
                if(ImGui::Button("BPM の変化を解析")){
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
                ImGui::TextDisabled("（1秒ほど止まります）");

                if(hasTempoMap){
                    if(!tempoMap.ok){
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", tempoMap.error.c_str());
                    }
                    else{
                        ImGui::Text("%zu 区間を検出:", tempoMap.segments.size());
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
                        if(ImGui::SmallButton("すべて x2##tempo")){
                            for(auto& sg : tempoMap.segments) sg.bpm *= 2.0;
                        }
                        ImGui::SameLine();
                        if(ImGui::SmallButton("すべて /2##tempo")){
                            for(auto& sg : tempoMap.segments) sg.bpm /= 2.0;
                        }

                        ImGui::Checkbox("今の BPM イベントを置き換える", &replaceBpmEvents);
                        if(ImGui::Button("編集中の難易度に反映")){
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
                        ImGui::TextDisabled("元に戻せません。残したいときは「置き換える」を外してください");
                    }
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
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
                        // BPMイベントの「時刻」自体が変わったかどうかで方針を分ける
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

                        // SpeedEventもノーツと同じように拍位置を保って追従させる
                        for(auto& s : d.speedEvents){
                            s.time = remapTime(d.alignedMap, nowMap, s.time);
                        }

                        // BPMイベント自身の時刻は、「値だけが変わった」場合に限って追従させる。
                        // 時刻そのものが変わった(ドラッグ・直接入力)場合は、
                        // その新しい時刻がユーザーの意図した位置そのものなので触らない。
                        if(timesUnchanged){
                            for(auto& b : d.bpmEvents){
                                b.time = remapTime(d.alignedMap, nowMap, b.time);
                            }
                        }
                    }

                    // 実際に書き換えた後の現在のデータから作り直す。
                    // これを怠ると、次のフレームで「まだズレている」と誤検出され、意図せず再度動いてしまう。
                    d.alignedMap = currentTempoMap();
                }
            }
            else{
                d.alignedMap = nowMap;
            }
        }

        // =================================================================
        //  タイムライン（中央のレーン）の描画
        // =================================================================
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 15, 15, 25, 255);   // 選曲画面と同じ背景色
        SDL_RenderClear(renderer);

        for(int i = 0; i < 6; i++){
            SDL_Rect laneRect = {laneX[i], 0, laneWidth, SCREEN_H};
            if(i % 2 == 0) SDL_SetRenderDrawColor(renderer, 0, 0, 5, 255);       // プレイ画面と同じレーン色
            else SDL_SetRenderDrawColor(renderer, 25, 25, 35, 255);
            SDL_RenderFillRect(renderer, &laneRect);
        }
        for(int i = 0; i <= 6; i++){
            SDL_SetRenderDrawColor(renderer, 80, 80, 90, 255);
            int lx = startX + laneWidth * i;
            SDL_RenderDrawLine(renderer, lx, 0, lx, SCREEN_H);
        }

        // グリッド線（拍の線は明るく）
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

                    SDL_RenderDrawLine(renderer, startX, gy, lanesRight, gy);
                }
            }
        }

        // イベントの線と帯（レーンの範囲だけに描く。文字は後で ImGui の背景に描く）
        ImDrawList* bgDraw = ImGui::GetBackgroundDrawList();   // パネルより奥に描く文字用

        for(const auto& s : currentDiff().speedEvents){
            int ly = judgeY - static_cast<int>((s.time - scrollTimeMs) * effectivePixelsPerMs);
            int ey = judgeY - static_cast<int>((s.time + s.duration - scrollTimeMs) * effectivePixelsPerMs);

            // 適用時間を薄い黄色の帯で表現する(画面内に一部でも重なっていれば描画)
            if(!(ly < -20 && ey < -20) && !(ly > SCREEN_H + 20 && ey > SCREEN_H + 20)){
                int yTop = std::min(ly, ey);
                int yBottom = std::max(ly, ey);
                SDL_SetRenderDrawColor(renderer, 255, 240, 150, 40);
                SDL_Rect bandRect = {startX, yTop, lanesRight - startX, yBottom - yTop};
                SDL_RenderFillRect(renderer, &bandRect);

                // 終端にも薄い線を引き、Ctrl+ドラッグで掴める位置を視覚的に示す
                SDL_SetRenderDrawColor(renderer, 255, 220, 80, 100);
                SDL_RenderDrawLine(renderer, startX, ey, lanesRight, ey);
            }

            if(ly < -20 || ly > SCREEN_H + 20) continue;

            SDL_SetRenderDrawColor(renderer, 255, 220, 80, 200);
            SDL_Rect thick = {startX, ly - 1, lanesRight - startX, 3};
            SDL_RenderFillRect(renderer, &thick);

            char buf[64];
            snprintf(buf, sizeof(buf), "SPD %.2fx", s.target);
            bgDraw->AddText(ImVec2(static_cast<float>(startX + 6), static_cast<float>(ly - 24)), IM_COL32(255, 220, 80, 255), buf);
        }

        for(const auto& b : currentDiff().bpmEvents){
            int ly = judgeY - static_cast<int>((b.time - scrollTimeMs) * effectivePixelsPerMs);
            if(ly < -20 || ly > SCREEN_H + 20) continue;

            SDL_SetRenderDrawColor(renderer, 80, 220, 255, 200);
            SDL_Rect thick = {startX, ly - 1, lanesRight - startX, 3};
            SDL_RenderFillRect(renderer, &thick);

            char buf[64];
            snprintf(buf, sizeof(buf), "BPM %.1f", b.bpm);
            ImVec2 textSize = ImGui::CalcTextSize(buf);
            bgDraw->AddText(ImVec2(static_cast<float>(lanesRight - 6) - textSize.x, static_cast<float>(ly - 24)),
                            IM_COL32(80, 220, 255, 255), buf);
        }

        // ----- レーンノーツで消えている区間を暗くする -----
        //   プレイ中は、レーンノーツ（type 4・ロングでないもの）を叩く／見逃すたびに、
        //   そのノーツが乗っているレーンの「表示 ⇔ 非表示」が切り替わる（noteJudge.h の toggleLaneActive）。
        //   そこで、レーンごとに切り替わる時刻を並べ、非表示になっている時間の範囲だけを暗く塗る。
        //   ノーツはこの後に描くので、消えた区間の上でもノーツの色と種類マークはそのまま見える。
        {
            std::vector<int32_t> toggles[6];
            for(const auto& n : currentDiff().notes){
                if(n.noteTypeInt != 4 || n.isLong()) continue;
                for(int l = n.lane; l < n.lane + n.width; l++){
                    if(l >= 0 && l < 6) toggles[l].push_back(n.time);
                }
            }

            // 時刻 → 画面の y（範囲外は画面の少し外に丸める。int のあふれ防止）
            auto timeToY = [&](double t) -> int {
                double y = judgeY - (t - scrollTimeMs) * effectivePixelsPerMs;
                return static_cast<int>(std::clamp(y, -20.0, static_cast<double>(SCREEN_H + 20)));
            };

            // 閉じている区間の色：開いているレーン（0,0,5 / 25,25,35）より少し明るい灰色
            const SDL_Color CLOSED_LANE_COLOR = {55, 55, 62, 255};

            const int HATCH_STEP = 24;   // 斜線の間隔(px)
            // 斜線を譜面と一緒に流すためのずれ（スクロールしても斜線が止まって見えないように）
            const int hatchPhase = static_cast<int>(std::fmod(scrollTimeMs * effectivePixelsPerMs, static_cast<double>(HATCH_STEP)));

            // 曲の始まりの状態：W(0) と O(5) のレーンは最初から閉じている（PlayScene の laneActive の初期値と同じ）
            const bool INITIAL_ACTIVE[6] = {false, true, true, true, true, false};

            for(int l = 0; l < 6; l++){
                auto& t = toggles[l];
                std::sort(t.begin(), t.end());

                // 非表示の区間 [開始, 終了) を作る。
                //   最初から閉じているレーンは「曲の始まり〜最初のレーンノーツ」も非表示。
                //   その後はレーンノーツごとに表示⇔非表示を入れ替える。
                std::vector<std::pair<double, double>> hidden;   // (開始時刻, 終了時刻)
                const double INF = 1.0e12;
                bool active = INITIAL_ACTIVE[l];
                double hiddenFrom = -INF;                       // 閉じている区間の開始（始まりから閉じているなら -∞）
                for(size_t k = 0; k < t.size(); k++){
                    if(!active) hidden.push_back({hiddenFrom, static_cast<double>(t[k])});   // ここで開く
                    active = !active;
                    if(!active) hiddenFrom = t[k];                                          // ここで閉じる
                }
                if(!active) hidden.push_back({hiddenFrom, INF});   // 最後まで閉じたまま

                for(const auto& seg : hidden){
                    const bool startsFromBeginning = (seg.first <= -INF);
                    const int yBottom = startsFromBeginning ? SCREEN_H + 20 : timeToY(seg.first);
                    const int yTop = (seg.second >= INF) ? -20 : timeToY(seg.second);
                    if(yBottom <= yTop) continue;   // 画面外 or 長さ0

                    SDL_Rect r = {laneX[l], yTop, laneWidth, yBottom - yTop};

                    // 少しだけ灰色に塗って、開いているレーン（ほぼ黒・濃紺）と見分けられるようにする
                    SDL_SetRenderDrawColor(renderer, CLOSED_LANE_COLOR.r, CLOSED_LANE_COLOR.g, CLOSED_LANE_COLOR.b, CLOSED_LANE_COLOR.a);
                    SDL_RenderFillRect(renderer, &r);

                    // 斜線：はみ出さないように、この区間の四角で切り抜いて描く
                    SDL_RenderSetClipRect(renderer, &r);
                    SDL_SetRenderDrawColor(renderer, 90, 90, 110, 120);
                    const int h = r.h;
                    for(int x0 = -h - HATCH_STEP + hatchPhase; x0 < r.w; x0 += HATCH_STEP){
                        SDL_RenderDrawLine(renderer, r.x + x0, r.y + r.h, r.x + x0 + h, r.y);
                    }
                    SDL_RenderSetClipRect(renderer, nullptr);

                    // 消え始めの位置に線を引いて、どこから消えるか分かるようにする（始まりから閉じている区間は引かない）
                    if(!startsFromBeginning && yBottom < SCREEN_H + 20){
                        SDL_SetRenderDrawColor(renderer, 255, 50, 50, 200);   // レーンノーツと同じ赤
                        SDL_Rect edge = {laneX[l], yBottom - 1, laneWidth, 2};
                        SDL_RenderFillRect(renderer, &edge);
                    }
                }
            }
        }

        // ----- 判定ボックス（プレイ画面と同じ形）＋ 再生中に叩かれたときの塗りつぶし -----
        {
            const float jy = static_cast<float>(judgeY);
            const float noteY0 = jy - NOTE_HEAD_H * 0.5f;
            RoundedRectBatch boxBatch;
            for(int i = 0; i < 6; i++){
                const float bx = static_cast<float>(laneX[i]) + JUDGE_BOX_SIDE_MARGIN;
                const float bw = static_cast<float>(laneWidth) - JUDGE_BOX_SIDE_MARGIN * 2.0f;
                boxBatch.addOutline(bx, noteY0, bw, NOTE_HEAD_H, NOTE_RADIUS, JUDGE_BOX_BORDER, JUDGE_BOX_COLOR);
            }
            for(const auto& fx : flashEffects){
                double progress = (nowTicks - fx.spawnTime) / 200.0;
                SDL_Color c = JUDGE_FILL_COLOR;
                c.a = static_cast<Uint8>(std::clamp(1.0 - progress, 0.0, 1.0) * 200);
                const float bx = static_cast<float>(laneX[fx.lane]) + JUDGE_BOX_SIDE_MARGIN;
                const float bw = static_cast<float>(laneWidth * fx.width) - JUDGE_BOX_SIDE_MARGIN * 2.0f;
                boxBatch.add(bx, noteY0, bw, NOTE_HEAD_H, NOTE_RADIUS, c);
            }
            boxBatch.flush(renderer);
        }

        // ----- ノーツ（プレイ画面と同じ見た目） -----
        {
            RoundedRectBatch bodyBatch;     // ロングの帯
            RoundedRectBatch innerBatch;    // ロングの芯
            RoundedRectBatch handleBatch;   // ロングの終点のつまみ
            RoundedRectBatch noteBatch;     // ヘッド
            RoundedRectBatch selectBatch;   // 選択中の縁取り

            const auto& notes = currentDiff().notes;

            // ノーツのヘッドの y（パス付きはパスで計算）
            auto headYOf = [&](const EditorNote& n) -> int {
                if(!n.path.empty()) return judgeY - static_cast<int>(evaluateEditorPathY(n, scrollTimeMs));
                return judgeY - static_cast<int>((n.time - scrollTimeMs) * effectivePixelsPerMs);
            };

            // 同時押しの線：同じ時刻・別レーンのノーツを、パスなしのものだけ線でつなぐ
            {
                std::vector<std::pair<int32_t, size_t>> order;   // (時刻, 番号)
                order.reserve(notes.size());
                for(size_t i = 0; i < notes.size(); i++){
                    if(notes[i].path.empty()) order.push_back({notes[i].time, i});
                }
                std::sort(order.begin(), order.end());

                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 140);
                for(size_t a = 0; a < order.size();){
                    size_t b = a;
                    int minCx = 1 << 30, maxCx = -(1 << 30);
                    while(b < order.size() && order[b].first == order[a].first){
                        const EditorNote& n = notes[order[b].second];
                        int cx = laneX[n.lane] + laneWidth * n.width / 2;
                        minCx = std::min(minCx, cx);
                        maxCx = std::max(maxCx, cx);
                        b++;
                    }
                    if(b - a >= 2 && maxCx > minCx){
                        int y = headYOf(notes[order[a].second]);
                        if(y > -50 && y < SCREEN_H + 50){
                            SDL_Rect line = {minCx, y - 3, maxCx - minCx, 6};
                            SDL_RenderFillRect(renderer, &line);
                        }
                    }
                    a = b;
                }
            }

            for(size_t idx = 0; idx < notes.size(); idx++){
                const auto& n = notes[idx];
                const int noteY = headYOf(n);

                const SDL_Color col = editorui::noteColor(n.noteTypeInt);
                const float noteX = static_cast<float>(laneX[n.lane]) + NOTE_SIDE_MARGIN;
                const float noteW = static_cast<float>(laneWidth * n.width) - NOTE_SIDE_MARGIN * 2.0f;
                const float noteH = NOTE_HEAD_H;
                const float headTop = static_cast<float>(noteY) - noteH * 0.5f;
                const bool selected = (static_cast<int>(idx) == selectedNoteIndex);

                float bodyTop = headTop;   // 選択の縁取りの上端（ロングなら帯の上端）

                if(n.isLong()){
                    const int tailY = judgeY - static_cast<int>((n.time + n.duration - scrollTimeMs) * effectivePixelsPerMs);
                    const float top = static_cast<float>(tailY) - noteH * 0.5f;
                    const float bottom = static_cast<float>(noteY);
                    bodyTop = top;

                    if(bottom > -50.0f && top < SCREEN_H + 50.0f){
                        SDL_Color bodyCol = col;
                        bodyCol.a = 170;
                        bodyBatch.add(noteX, top, noteW, bottom - top, NOTE_RADIUS, bodyCol);

                        // 芯：帯の横幅の30%、ノーツ色を暗くした色（プレイ画面の「叩く前」と同じ）
                        const float innerW = noteW * 0.3f;
                        const float innerX = noteX + (noteW - innerW) * 0.5f;
                        SDL_Color innerCol = {static_cast<Uint8>(col.r * 0.6), static_cast<Uint8>(col.g * 0.6),
                                              static_cast<Uint8>(col.b * 0.6), 200};
                        innerBatch.add(innerX, top + 6.0f, innerW, bottom - (top + 6.0f),
                                       std::min(NOTE_RADIUS, innerW * 0.5f), innerCol);

                        // 終点のつまみ：ここをドラッグすると長さを変えられる
                        handleBatch.add(noteX + noteW * 0.25f, static_cast<float>(tailY) - 4.0f, noteW * 0.5f, 8.0f,
                                        4.0f, SDL_Color{255, 255, 255, 220});
                    }
                }

                if(noteY < -50 || noteY > SCREEN_H + 50) continue;

                noteBatch.add(noteX, headTop, noteW, noteH, NOTE_RADIUS, col);

                if(selected){
                    const float pad = 4.0f;
                    selectBatch.addOutline(noteX - pad, bodyTop - pad, noteW + pad * 2.0f,
                                           (headTop + noteH) - bodyTop + pad * 2.0f,
                                           NOTE_RADIUS + pad, 3.0f, SDL_Color{0, 255, 255, 255});
                }

                // 種類マーク：ヘッドの中央に小さく書く（ImGui の背景に描くのでパネルの奥に入る）
                const std::string marker = editorui::noteMarker(n);
                if(!marker.empty()){
                    const float fontSize = 15.0f;
                    ImFont* f = ImGui::GetFont();
                    ImVec2 ts = f->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, marker.c_str());
                    ImU32 textCol = (n.noteTypeInt == 4) ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 30, 255);
                    bgDraw->AddText(f, fontSize,
                                    ImVec2(noteX + noteW * 0.5f - ts.x * 0.5f, static_cast<float>(noteY) - ts.y * 0.5f),
                                    textCol, marker.c_str());
                }
            }

            bodyBatch.flush(renderer);
            innerBatch.flush(renderer);
            handleBatch.flush(renderer);
            noteBatch.flush(renderer);
            selectBatch.flush(renderer);
        }

        //ImGui描画をSDL描画に合成
        // 保存の通知：レーンの上部中央に出して、最後の 400ms で消えていく
        if(saveToastTicks != 0 && nowTicks - saveToastTicks < SAVE_TOAST_MS){
            const uint32_t elapsed = nowTicks - saveToastTicks;
            const float alpha = (elapsed < SAVE_TOAST_MS - 400) ? 1.0f : (SAVE_TOAST_MS - elapsed) / 400.0f;

            const char* msg = saveToastOk ? "保存しました" : "保存に失敗しました";
            ImFont* f = ImGui::GetFont();
            const float fs = 24.0f;
            const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, msg);
            const float cx = (startX + lanesRight) * 0.5f;
            const ImVec2 p0(cx - ts.x * 0.5f - 24.0f, 20.0f);
            const ImVec2 p1(cx + ts.x * 0.5f + 24.0f, 20.0f + ts.y + 16.0f);

            const ImU32 accent = saveToastOk ? IM_COL32(0, 200, 255, static_cast<int>(255 * alpha))
                                             : IM_COL32(255, 80, 80, static_cast<int>(255 * alpha));
            ImDrawList* fg = ImGui::GetForegroundDrawList();   // パネルより手前に出す
            fg->AddRectFilled(p0, p1, IM_COL32(10, 10, 20, static_cast<int>(230 * alpha)));
            fg->AddRect(p0, p1, accent, 0.0f, 0, 3.0f);
            fg->AddText(f, fs, ImVec2(cx - ts.x * 0.5f, p0.y + 8.0f), IM_COL32(235, 235, 245, static_cast<int>(255 * alpha)), msg);
        }

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
    if(tap_sound) Mix_FreeChunk(tap_sound);
    if(tap_sound_c) Mix_FreeChunk(tap_sound_c);

    return nextScene;
}