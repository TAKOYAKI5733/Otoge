#include "GameCommon.h"
#include <nlohmann/json.hpp>
#include <limits>
#include <cfloat>
#include <string>
#include <vector>
#include "Editortempo.h"
#include "bpmdetect.h"
#include "musicAnalyze.h"   // Music Settings：オフセット・BPM・BPM の変化を自動で決める
#include "InputBinding.h"   // キー設定（レーンのキー）を読む：キーボード配置モードで使う
#include "SongPreview.h"    // 試聴の長さ（選曲画面と共通）
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

    bool selected = false;   // 範囲選択で選ばれているか（エディタ専用。JSONには保存しない）

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
    int sigNum = 4;   // 拍子の分子（1 小節に何拍）
    int sigDen = 4;   // 拍子の分母（何分音符を 1 拍とするか）
};

struct EditorDifficulty{
    std::string name = "EASY";
    std::string level = "0";
    std::string chartCreator = "Unknown";
    double bpm = 120.0;
    int sigNum = 4;   // 曲の始まり（最初の BPM イベントより前）の拍子
    int sigDen = 4;

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
    double previewStartMs = 0.0;   // 選曲画面の試聴を始める位置（曲ファイル上の ms。オフセットの影響を受けない）
};

//JSON保存・読込

// 保存できたら true（画面に「保存しました／失敗しました」を出すのに使う）
inline bool saveChart(const std::string& path, const ChartMeta& meta, const std::vector<EditorDifficulty>& difficulties){
    json j;
    j["bgm"] = meta.bgm;
    j["composer"] = meta.composer;
    j["offset"] = meta.offsetMs;
    j["previewStart"] = meta.previewStartMs;

    json diffArr = json::array();
    for(const auto& d : difficulties){
        json dj;
        dj["name"] = d.name;
        dj["level"] = d.level;
        dj["chartCreator"] = d.chartCreator;
        dj["bpm"] = d.bpm;
        dj["sigNum"] = d.sigNum;
        dj["sigDen"] = d.sigDen;

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
            item["sigNum"] = b.sigNum;
            item["sigDen"] = b.sigDen;
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
    d.sigNum = std::max(1, src.value("sigNum", 4));   // 拍子がない古い譜面は 4/4
    d.sigDen = std::max(1, src.value("sigDen", 4));

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
            b.sigNum = std::max(1, item.value("sigNum", 4));
            b.sigDen = std::max(1, item.value("sigDen", 4));
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
    meta.previewStartMs = j.value("previewStart", 0.0);

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
//  BPM を変えたあと、BPM イベント自身の時刻を「拍の位置」を保つように直す
//    oldMap  : 変える前のテンポ表（alignedMap）
//    baseBpm : 今の難易度の BPM（変えたあとの値）
//    events  : 今の BPM イベント。時刻は「変える前のテンポ表」で置いた位置、BPM の値は変えたあとの値
//  やること：
//    1. どのイベントも、今の時刻を「変える前のテンポ表」で拍に直す
//       （右クリックで追加したイベントも、変える前のグリッドに吸着して置いたものなので、これで正しい拍になる）
//    2. 拍の順に並べ、新しい BPM で 0ms から時刻を積み上げ直す
//       → 前の区間の BPM が変われば、後ろのイベントもその分だけ前後に動く
//  戻り値：直したあとのテンポ表（ノーツ・SPEED イベントの移動先の計算に使う）
// =====================================================================
inline TempoMap realignBpmEvents(const TempoMap& oldMap, double baseBpm, std::vector<EditorBpmEvent>& events){
    const double base = (baseBpm > 0.0) ? baseBpm : 1.0;

    // 1. 各イベントが「何拍目」にあるか（変える前のテンポ表で）
    std::vector<std::pair<double, size_t>> order;   // (拍, 番号)
    order.reserve(events.size());
    for(size_t i = 0; i < events.size(); i++){
        order.push_back({tempoBeatAt(oldMap, static_cast<double>(events[i].time)), i});
    }
    std::sort(order.begin(), order.end());

    // 2. 拍の順に、新しい BPM で時刻を積み上げ直す
    double prevBeat = 0.0;    // 1つ前の区切りの拍
    double prevTime = 0.0;    // 1つ前の区切りの時刻(ms)
    double prevBpm  = base;   // 1つ前の区切りから使われる BPM
    for(const auto& [beat, idx] : order){
        EditorBpmEvent& ev = events[idx];
        if(beat < 0.0){
            // 0ms より前に置かれたイベント（ふつうはない）：最初の BPM で戻すだけで、後ろには影響させない
            ev.time = static_cast<int32_t>(std::lround(beat * 60000.0 / base));
            continue;
        }
        const double t = prevTime + (beat - prevBeat) * 60000.0 / prevBpm;
        ev.time = static_cast<int32_t>(std::lround(t));
        prevBeat = beat;
        prevTime = t;
        if(ev.bpm > 0.0) prevBpm = ev.bpm;
    }

    // BPM タブの並びも時刻順にそろえる
    std::stable_sort(events.begin(), events.end(),
        [](const EditorBpmEvent& a, const EditorBpmEvent& b){ return a.time < b.time; });

    // 3. 直したあとの時刻（整数 ms に丸めたもの）でテンポ表を作る
    //    → 保存される値と、ノーツの移動先の計算がぴったり一致する
    std::vector<TempoPoint> pts;
    pts.reserve(events.size());
    for(const auto& ev : events) pts.push_back({static_cast<double>(ev.time), ev.bpm});
    return makeTempoMap(base, pts);
}

// =====================================================================
//  Music Settings の結果を譜面に入れる（すべての難易度）
//    ・オフセット、曲頭の BPM、BPM イベントを、解析結果で置き換える
//    ・拍子は変えない：曲頭の拍子はそのまま。新しい BPM イベントには、その位置で今まで使われていた拍子を入れる
//      （テンポは曲そのものの性質なので、難易度ごとに変えず全部そろえる）
//    ・ノーツと SPEED イベントは「何拍目か」を保って動かす（BPM を変えたときと同じ考え方）
// =====================================================================
inline void applyMusicAnalysis(const MusicAnalysis& r, ChartMeta& meta, std::vector<EditorDifficulty>& difficulties){
    // オフセットの入力欄は 0.1ms 単位なので、それに合わせて丸めてから、イベントの譜面上の時刻を決める
    meta.offsetMs = std::round(r.offsetMs * 10.0) / 10.0;

    std::vector<EditorBpmEvent> events;
    std::vector<TempoPoint> newPts;
    for(const auto& c : r.changes){
        EditorBpmEvent b;
        b.time = static_cast<int32_t>(std::lround(c.audioMs - meta.offsetMs));   // 音源上の位置 → 譜面上の時刻
        b.bpm = c.bpm;
        events.push_back(b);
        newPts.push_back({static_cast<double>(b.time), b.bpm});
    }
    const TempoMap newMap = makeTempoMap(r.bpm, newPts);

    for(auto& d : difficulties){
        std::vector<TempoPoint> oldPts;
        for(const auto& b : d.bpmEvents) oldPts.push_back({static_cast<double>(b.time), b.bpm});
        const TempoMap oldMap = makeTempoMap(d.bpm, oldPts);

        for(auto& n : d.notes){
            const int32_t newStart = remapTime(oldMap, newMap, n.time);
            if(n.duration > 0){
                const int32_t newEnd = remapTime(oldMap, newMap, n.time + n.duration);
                n.duration = std::max<int32_t>(1, newEnd - newStart);
            }
            for(auto& k : n.path) k.time = remapTime(oldMap, newMap, k.time);
            n.time = newStart;
        }
        for(auto& sev : d.speedEvents) sev.time = remapTime(oldMap, newMap, sev.time);

        // 拍子は勝手に変えない：新しいイベントの位置を「今までのテンポ表で何拍目か」に直し、
        // そこで今まで使われていた拍子（曲頭の拍子か、それより前の一番近い BPM イベントの拍子）を入れる
        std::vector<EditorBpmEvent> evs = events;
        for(auto& b : evs){
            const int32_t oldT = remapTime(newMap, oldMap, b.time);
            b.sigNum = d.sigNum;
            b.sigDen = d.sigDen;
            int32_t best = std::numeric_limits<int32_t>::min();
            for(const auto& ob : d.bpmEvents){
                if(ob.time <= oldT && ob.time >= best){
                    best = ob.time;
                    b.sigNum = ob.sigNum;
                    b.sigDen = ob.sigDen;
                }
            }
        }

        d.bpm = r.bpm;
        d.bpmEvents = evs;   // 曲頭の拍子（d.sigNum / d.sigDen）はそのまま

        // 「今のテンポ表 = 合わせ済み」にしておく。
        //   これをしないと、毎フレームの「BPM が変わったらノーツを動かす」処理が、もう一度ノーツを動かしてしまう
        d.alignedMap = newMap;
        d.tempoAligned = true;
    }
}

// BPM をすべて倍・半分にする（倍テン・半テンの直し用。すべての難易度）
//   拍の数え方が変わるだけで、音の位置は変わらないので、ノーツ・イベントの時刻（ms）はそのまま
inline void scaleAllBpm(double factor, std::vector<EditorDifficulty>& difficulties){
    for(auto& d : difficulties){
        d.bpm *= factor;
        std::vector<TempoPoint> pts;
        for(auto& b : d.bpmEvents){
            b.bpm *= factor;
            pts.push_back({static_cast<double>(b.time), b.bpm});
        }
        d.alignedMap = makeTempoMap(d.bpm, pts);   // ノーツは動かさない
        d.tempoAligned = true;
    }
}

// =====================================================================
//  拍子
//    ・BPM は拍子に関係なく「1 分間の 4 分音符の数」のまま（ノーツの時刻・判定は変わらない）
//    ・拍子で変わるのは小節線の位置だけ。1 小節 = 分子 × (4 ÷ 分母) 拍（4 分音符いくつ分か）
//      例：4/4 → 4 拍、3/4 → 3 拍、6/8 → 3 拍、7/8 → 3.5 拍
//    ・BPM イベントのところで、新しい小節が始まる
// =====================================================================
inline constexpr int kSigMax = 64;   // 拍子の分子・分母の上限（入力欄で打てる範囲）

// 拍子の入力欄：「分子 / 分母」を横に並べる。+/- ボタンは付けず、キーボードで打つ
//   変更されたら true
inline bool timeSigInput(const char* id, int& num, int& den){
    bool changed = false;
    ImGui::PushID(id);
    ImGui::PushItemWidth(60.0f);
    changed |= ImGui::InputInt("##num", &num, 0, 0);
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextUnformatted("/");
    ImGui::SameLine(0.0f, 6.0f);
    changed |= ImGui::InputInt("##den", &den, 0, 0);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::TextUnformatted("拍子");
    ImGui::PopID();
    num = std::clamp(num, 1, kSigMax);
    den = std::clamp(den, 1, kSigMax);
    return changed;
}

// 拍子の区切り：曲の始まり（拍 0）と各 BPM イベント。区切りごとに新しい小節が始まる
struct SigSeg{ double beat; int num, den; };
inline std::vector<SigSeg> buildSigSegs(const EditorDifficulty& d, const TempoMap& tm){
    std::vector<SigSeg> segs;
    segs.push_back({0.0, d.sigNum, d.sigDen});
    std::vector<EditorBpmEvent> evs = d.bpmEvents;   // 入力中で並びが崩れていても大丈夫なように、写して並べる
    std::stable_sort(evs.begin(), evs.end(), [](const EditorBpmEvent& a, const EditorBpmEvent& b){ return a.time < b.time; });
    for(const auto& b : evs){
        const double bb = std::max(0.0, tempoBeatAt(tm, static_cast<double>(b.time)));
        if(bb <= segs.back().beat + 1.0e-9) segs.back() = {bb, b.sigNum, b.sigDen};   // 同じ位置なら後のものを使う
        else segs.push_back({bb, b.sigNum, b.sigDen});
    }
    return segs;
}

// =====================================================================
//  区切りごとのグリッド
//    グリッドの原点は「拍 0」ではなく、各区切り（曲の始まり・BPM イベント）の先頭。
//    こうしないと 3/8 などの半端な小節のあとで、4 分の線が小節線からずれる。
//    区切りの境目そのものも、必ずグリッドの点になる。
// =====================================================================

// beat が入っている区切りの番号（eps だけ手前でも「その区切り」とみなす）
inline size_t sigSegIndexAt(const std::vector<SigSeg>& segs, double beat, double eps = 1.0e-9){
    size_t i = 0;
    while(i + 1 < segs.size() && segs[i + 1].beat <= beat + eps) i++;
    return i;
}

// 区切り i の終わりの拍（最後の区切りは終わりなし）
inline double sigSegEnd(const std::vector<SigSeg>& segs, size_t i){
    return (i + 1 < segs.size()) ? segs[i + 1].beat : std::numeric_limits<double>::infinity();
}

// 一番近いグリッドの拍（gb = 1マスの拍数）
inline double gridSnapBeat(const std::vector<SigSeg>& segs, double beat, double gb){
    const size_t i = sigSegIndexAt(segs, beat);
    const double s = segs[i].beat;
    const double e = sigSegEnd(segs, i);
    double c = s + std::round((beat - s) / gb) * gb;
    if(c > e) c = e;   // 区切りを越えたら、境目（次の区切りの先頭）のほうが近い
    return c;
}

// 次(+1)/前(-1)のグリッドの拍。eps 以内の誤差は「線の上にいる」とみなす
inline double gridStepBeat(const std::vector<SigSeg>& segs, double beat, double gb, int dir, double eps){
    const size_t i = sigSegIndexAt(segs, beat, eps);
    const double s = segs[i].beat;
    if(dir > 0){
        const long k = static_cast<long>(std::floor((beat - s + eps) / gb)) + 1;
        return std::min(s + k * gb, sigSegEnd(segs, i));   // 区切りの終わりで一度止まる
    }
    const long k = static_cast<long>(std::ceil((beat - s - eps) / gb)) - 1;
    const double c = s + k * gb;
    if(c >= s - 1.0e-9 || i == 0) return c;
    // この区切りの先頭より前 → 1つ前の区切りの、最後のグリッド線
    const double ps = segs[i - 1].beat;
    const long pk = static_cast<long>(std::ceil((s - ps - 1.0e-9) / gb)) - 1;
    return ps + pk * gb;
}

// =====================================================================
//  メトロノームの音（コードで作るので、音のファイルは要らない）
//    ・拍子の分母で音を変える：4 分（分母 4 以下）= 1000Hz、8 分 = 1500Hz、16 分以上 = 2000Hz
//    ・大きさ：4 分を基準に、8 分は 65%、16 分は 45%。小節の頭以外はさらに 75%
//      （8 分の小節の頭でも、4 分のふつうの拍より小さい）
// =====================================================================
struct MetronomeSounds{
    enum Kind{ QUARTER, EIGHTH, SIXTEENTH, KIND_COUNT };

    bool create(){
        int freq = 0;
        Uint16 fmt = 0;
        int ch = 0;
        if(Mix_QuerySpec(&freq, &fmt, &ch) == 0 || fmt != AUDIO_S16SYS || ch < 1) return false;
        const double hz[KIND_COUNT] = {1000.0, 1500.0, 2000.0};
        for(int k = 0; k < KIND_COUNT; k++) makeClick(k, hz[k], freq, ch);
        return true;
    }

    void destroy(){
        for(int k = 0; k < KIND_COUNT; k++){
            if(chunk_[k]) Mix_FreeChunk(chunk_[k]);   // QuickLoad で作ったものは、中の波形（pcm_）を解放しない
            chunk_[k] = nullptr;
            pcm_[k].clear();
        }
    }

    // 拍子の分母 → 音の種類
    static Kind kindFor(int den){
        if(den >= 16) return SIXTEENTH;
        if(den >= 8)  return EIGHTH;
        return QUARTER;
    }

    // 鳴らす。volume = メトロノーム全体の音量（0〜100）
    void play(Kind k, bool barHead, int volume){
        if(!chunk_[k]) return;
        const double base[KIND_COUNT] = {1.0, 0.65, 0.45};
        const double level = base[k] * (barHead ? 1.0 : 0.75) * std::clamp(volume, 0, 100) / 100.0;
        // チャンネルの音量（Mix_Volume）を変えると、同じチャンネルで鳴るノーツ音まで小さくなるので、音そのものの音量を変える
        Mix_VolumeChunk(chunk_[k], static_cast<int>(std::lround(MIX_MAX_VOLUME * level)));
        Mix_PlayChannel(-1, chunk_[k], 0);
    }

private:
    std::vector<int16_t> pcm_[KIND_COUNT];   // Mix_QuickLoad_RAW はコピーしないので、ここに持ち続ける
    Mix_Chunk* chunk_[KIND_COUNT] = {nullptr, nullptr, nullptr};

    // 60ms の「ピッ」：1ms で立ち上がり、すぐに減っていくサイン波
    void makeClick(int k, double hz, int freq, int ch){
        const double kPi = 3.14159265358979323846;
        const int n = static_cast<int>(freq * 0.06);
        pcm_[k].assign(static_cast<size_t>(n) * static_cast<size_t>(ch), 0);
        for(int i = 0; i < n; i++){
            const double t = static_cast<double>(i) / freq;
            const double env = (t < 0.001) ? t / 0.001 : std::exp(-(t - 0.001) / 0.012);
            const int16_t v = static_cast<int16_t>(0.6 * env * std::sin(2.0 * kPi * hz * t) * 32767.0);
            for(int c = 0; c < ch; c++) pcm_[k][static_cast<size_t>(i) * ch + c] = v;
        }
        chunk_[k] = Mix_QuickLoad_RAW(reinterpret_cast<Uint8*>(pcm_[k].data()),
                                      static_cast<Uint32>(pcm_[k].size() * sizeof(int16_t)));
    }
};

// 時刻 t で使われている拍子（t ちょうどの BPM イベントも含む）
inline void timeSigAt(const EditorDifficulty& d, int32_t t, int& num, int& den){
    num = d.sigNum;
    den = d.sigDen;
    int32_t best = std::numeric_limits<int32_t>::min();
    for(const auto& b : d.bpmEvents){
        if(b.time <= t && b.time >= best){
            best = b.time;
            num = b.sigNum;
            den = b.sigDen;
        }
    }
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

    // メトロノーム（再生中に、拍子に合わせて鳴らす）
    MetronomeSounds metronome;
    if(!metronome.create()) printf("[譜面エディタ] メトロノームの音を作れませんでした\n");
    bool metronomeOn = false;
    int metronomeVolume = 70;     // 0〜100
    double metronomePrevMs = 0.0; // 前のフレームの再生位置（ここから今の位置までに過ぎた拍で鳴らす）

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
            // 再生を始めた位置ちょうどに拍があれば、その拍のメトロノームも鳴らす（「前の位置」を少しだけ手前にしておく）
            metronomePrevMs = static_cast<double>(scrollTimeMs) - 0.5;

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

    // スナップの「自由入力」
    //   一覧（4〜1920）にない分割数を、プレイヤーが数字で打って使えるようにする
    //   gridDivisor = 「全音符をいくつに分けるか」なので、20 と打てば 20 分音符（1 拍を 5 等分）になる
    constexpr int GRID_DIVISOR_MAX = 1920;   // 打てる上限（一覧の一番細かい値と同じ）
    bool gridCustom = false;                 // 自由入力を選んでいるか
    int  customGridDivisor = 20;             // 自由入力の値

    auto currentTempoMap = [&]() -> TempoMap {
        std::vector<TempoPoint> pts;
        for(const auto& b : currentDiff().bpmEvents) pts.push_back({static_cast<double>(b.time), b.bpm});
        return makeTempoMap(currentDiff().bpm, pts);
    };
    // 最も近いグリッド線へ吸着（区切りごとに原点を置き直したグリッド）
    auto snapToGrid = [&](double rawTimeMs) -> int32_t {
        const TempoMap tm = currentTempoMap();
        const std::vector<SigSeg> segs = buildSigSegs(currentDiff(), tm);
        const double b = gridSnapBeat(segs, tempoBeatAt(tm, rawTimeMs), 4.0 / gridDivisor);
        return static_cast<int32_t>(std::lround(tempoTimeAt(tm, b)));
    };
    // 次(+1)/前(-1)のグリッド線へ
    auto stepGrid = [&](int32_t fromMs, int dir) -> int32_t {
        const TempoMap tm = currentTempoMap();
        const std::vector<SigSeg> segs = buildSigSegs(currentDiff(), tm);
        const double gb = 4.0 / gridDivisor;
        // 時刻は整数 ms に丸められているので、0.6ms 以内のずれは「線の上」とみなす（細かいグリッドではマスの 1/4 まで）
        const double eps = std::min(tempoBpmAt(tm, fromMs) / 60000.0 * 0.6, gb * 0.25);

        double b = tempoBeatAt(tm, static_cast<double>(fromMs));
        int32_t result = fromMs;
        for(int guard = 0; guard < 4 && result == fromMs; guard++){   // 丸めで同じ ms に戻ったら、もう1マス進む
            b = gridStepBeat(segs, b, gb, dir, eps);
            result = static_cast<int32_t>(std::lround(tempoTimeAt(tm, b)));
        }
        return result;
    };
    // その時刻付近のグリッド1マスの長さ(ms)。当たり判定の許容幅に使う
    auto localGridMs = [&](double atMs) -> double {
        return 60000.0 / tempoBpmAt(currentTempoMap(), atMs) * (4.0 / gridDivisor);
    };

    //ツールバー設定
    int paletteIndex = 0;                 // パレットで選んでいるマス（editorui::PALETTE の番号）
    bool paletteScrollToSelected = false; // キーで選んだとき、そのマスが見える位置までパレットを送る

    // 右クリックメニュー（SPEED / BPM イベントの追加）
    bool    eventMenuRequest = false;       // 次の ImGui フレームで開く
    bool    eventMenuOpen = false;          // 今開いているか（Esc の扱いに使う）
    bool    eventMenuCloseRequest = false;  // Esc で閉じる
    int32_t eventMenuTime = 0;              // 追加する時刻（右クリックした位置に一番近いグリッド線）
    ImVec2  eventMenuPos(0.0f, 0.0f);       // メニューを出す位置（マウスの位置）
    double  eventMenuSpeed = 1.0;           // SPEED の目標値
    int     eventMenuSpeedEasing = 1;       // SPEED のイージング
    int     eventMenuSpeedDuration = 500;   // SPEED の変化にかける時間
    double  eventMenuBpm = 120.0;           // BPM の値
    int     eventMenuSigNum = 4;            // 拍子の分子
    int     eventMenuSigDen = 4;            // 拍子の分母

    // テストプレイ（右クリックメニューから）
    int     testPlayFrom = 0;               // 0 = 右クリックした位置から、1 = 最初から
    bool    testPlayAutoplay = false;       // オートプレイで確認する
    bool    testPlayRequest = false;        // このフレームの描画が終わったら開始する
    int32_t testPlayStartMs = 0;            // 開始する譜面上の時刻

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

    enum class DragMode{ None, MoveNote, ResizeTail, MoveSpeedEvent, MoveBpmEvent, ResizeSpeedDuration, MoveSelection };
    DragMode dragMode = DragMode::None;
    int dragNoteIndex = -1;
    int dragEventIndex = -1;

    // =================================================================
    //  範囲選択（Shift + ドラッグ）
    // =================================================================
    bool    boxSelecting = false;      // 四角を描いている最中か
    double  boxStartTime = 0.0;        // 始点の譜面上の時刻（スクロールしてもずれないように時刻で持つ）
    int     boxStartX = 0;             // 始点の x（レーンは横に動かないので画面座標でよい）
    int     boxNowX = 0, boxNowY = 0;  // 今のマウス位置

    // まとめて移動：掴んだ瞬間の状態
    struct MoveOrigin{
        size_t index;
        int32_t time;
        int lane;
        std::vector<int32_t> pathTimes;
    };
    std::vector<MoveOrigin> moveOrigins;
    int32_t moveAnchorTime = 0;   // 掴んだ位置の時刻
    int     moveAnchorLane = 0;   // 掴んだ位置のレーン

    std::vector<EditorNote> clipboard;   // コピーしたノーツ（先頭を 0ms とした相対時刻）

    // 範囲選択のメニュー（右クリックメニューと同じく、マウスの位置にポップアップで出す）
    bool    selectMenuRequest = false;      // 次の ImGui フレームで開く
    bool    selectMenuOpen = false;         // 今開いているか（Esc の扱いに使う）
    bool    selectMenuCloseRequest = false; // Esc で閉じる
    ImVec2  selectMenuPos(0.0f, 0.0f);      // メニューを出す位置（マウスの位置）

    // 画面の x → レーン番号（レーンの外は端のレーンに丸める）
    auto laneAtX = [&](int x) -> int {
        int l = static_cast<int>(std::floor(static_cast<double>(x - startX) / laneWidth));
        return std::clamp(l, 0, 5);
    };

    auto clearSelection = [&](){
        for(auto& d : difficulties){
            for(auto& n : d.notes) n.selected = false;
        }
    };

    auto countSelected = [&]() -> int {
        int c = 0;
        for(const auto& n : currentDiff().notes){
            if(n.selected) c++;
        }
        return c;
    };

    // 範囲選択がないときは、クリックで選んでいる1個を対象にする
    // （1個だけでも Ctrl+C / Ctrl+R / Delete が効くように）
    auto promoteSingleSelection = [&](){
        if(countSelected() == 0 && selectedNoteIndex >= 0 &&
           selectedNoteIndex < static_cast<int>(currentDiff().notes.size())){
            currentDiff().notes[selectedNoteIndex].selected = true;
        }
    };

    // 四角に入っているノーツに旗を立てる（時間とレーンの両方で判定。時間はヘッドの位置で見る）
    auto updateBoxSelection = [&](double ppm){
        const double t0 = boxStartTime;
        const double t1 = scrollTimeMs + (judgeY - boxNowY) / ppm;
        const double tMin = std::min(t0, t1);
        const double tMax = std::max(t0, t1);
        const int l0 = laneAtX(boxStartX);
        const int l1 = laneAtX(boxNowX);
        const int lMin = std::min(l0, l1);
        const int lMax = std::max(l0, l1);

        for(auto& n : currentDiff().notes){
            const bool laneHit = (n.lane <= lMax) && (n.lane + n.width - 1 >= lMin);   // 幅のあるノーツは一部でも重なればOK
            const bool timeHit = (n.time >= tMin) && (n.time <= tMax);
            n.selected = laneHit && timeHit;
        }
    };

    auto copySelection = [&](){
        promoteSingleSelection();
        int32_t base = std::numeric_limits<int32_t>::max();
        for(const auto& n : currentDiff().notes){
            if(n.selected) base = std::min(base, n.time);
        }
        if(base == std::numeric_limits<int32_t>::max()) return;   // 何も選ばれていない

        clipboard.clear();
        for(const auto& n : currentDiff().notes){
            if(!n.selected) continue;
            EditorNote c = n;
            c.time -= base;
            for(auto& k : c.path) k.time -= base;   // パスもノーツと一緒にずらす
            c.selected = false;
            clipboard.push_back(c);
        }
    };

    // 判定ライン（今の再生位置）に一番近いグリッドへ貼り付ける。貼ったノーツが選ばれた状態になる
    auto pasteClipboard = [&](){
        if(clipboard.empty()) return;
        clearSelection();
        const int32_t base = std::max<int32_t>(0, snapToGrid(scrollTimeMs));
        for(const auto& c : clipboard){
            EditorNote n = c;
            n.time += base;
            for(auto& k : n.path) k.time += base;
            n.selected = true;
            currentDiff().notes.push_back(n);
        }
        selectedNoteIndex = -1;
    };

    // 左右反転：レーン 0⇔5, 1⇔4, 2⇔3。幅のあるノーツは右端が左端になるように 6 - lane - width
    auto mirrorSelection = [&](){
        promoteSingleSelection();
        for(auto& n : currentDiff().notes){
            if(n.selected) n.lane = 6 - n.lane - n.width;
        }
    };

    auto deleteSelection = [&](){
        promoteSingleSelection();
        std::erase_if(currentDiff().notes, [](const EditorNote& n){ return n.selected; });
        selectedNoteIndex = -1;   // 番号がずれるので、クリック選択は解除する
        dragMode = DragMode::None;
        dragNoteIndex = -1;
    };

    // =================================================================
    //  キーボード配置モード（Ctrl+K で ON/OFF）
    //    ・プレイと同じキー設定（PlayerSettings::laneBindings）のキーを押すと、
    //      判定ライン（今の再生位置）に一番近いグリッド線へ、パレットで選んだノーツを置く
    //    ・ロングは「押した位置が始点」。押している間、終点が判定ラインに一番近いグリッド線まで伸び、
    //      離したところで決まる（再生中なら曲に合わせて伸びる／止めているならホイールで伸ばせる）
    //    ・キーの「押した瞬間」は、SDL のイベントではなく毎フレームの状態の変化で見る
    //      （プレイ画面と同じ isBindingPressed を使うので、コントローラーのボタンでも置ける）
    // =================================================================
    PlayerSettings editorSettings;
    loadPlayerSettings(editorSettings);   // キー設定を読むためだけに使う（エディタからは保存しない）

    std::string kbKeyNames[6];            // レーンの下に出すキーの名前
    for(int i = 0; i < 6; i++) kbKeyNames[i] = bindingName(editorSettings.laneBindings[i]);

    bool     keyboardMode = false;
    bool     kbPrev[6] = {};              // 前のフレームで押していたか（「押した瞬間」を見つける用）
    bool     kbHeld[6] = {};              // 今押しているか（キービームの描画用）
    uint32_t kbReleaseTicks[6] = {};      // 離した時刻（キービームを少しずつ消す用。0 = まだ離していない）
    constexpr uint32_t KEY_BEAM_FADE_MS = 150;

    // 押している間に伸ばしているロングノーツ（レーンのキーごと）
    //   ノーツは vector の番号で覚えるが、削除・移動などで番号がずれることがあるので、
    //   使う前に「置いたときの時刻・レーン・難易度」と一致するかを必ず確かめる
    struct KeyHold{
        bool    active = false;
        int     diff = 0;       // どの難易度に置いたか
        size_t  index = 0;      // currentDiff().notes の番号
        int32_t start = 0;      // 始点の時刻
        int     lane = 0;       // 左端のレーン
    };
    KeyHold keyHolds[6];

    // その入力（スキャンコード）がレーンのキーか
    auto isLaneKey = [&](SDL_Scancode sc) -> bool {
        const InputBinding b = InputBinding::key(sc);
        for(int i = 0; i < 6; i++){
            if(editorSettings.laneBindings[i] == b) return true;
        }
        return false;
    };

    auto setKeyboardMode = [&](bool on){
        keyboardMode = on;
        // 切り替えた瞬間に押しっぱなしのキー（Ctrl+K の K など）を「押した瞬間」と数えないように、今の状態で埋めておく
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        for(int i = 0; i < 6; i++){
            kbPrev[i] = on && isBindingPressed(editorSettings.laneBindings[i], ks);
            kbHeld[i] = false;
            kbReleaseTicks[i] = 0;
            keyHolds[i].active = false;
        }
    };

    // 重なってはいけないノーツか：DRAG(3) 以外（通常・通常C・それらのロング・LANE）
    //   DRAG はどのノーツとも重ねてよい（単発+DRAG、DRAG+DRAG など）
    auto isSolidType = [](int type) -> bool { return type != 3; };

    // 2つのノーツのレーンが重なっているか（幅のあるノーツは一部でも重なれば重なり）
    auto lanesOverlap = [](int laneA, int widthA, int laneB, int widthB) -> bool {
        return laneA <= laneB + widthB - 1 && laneB <= laneA + widthA - 1;
    };

    // 時刻 t・レーン lane〜lane+width-1 に type のノーツを置くと、ほかのノーツと重なるか
    //   既存のロングは [始点, 終点] の全体を占めているとみなす
    auto keyPlaceBlocked = [&](int lane, int width, int type, int32_t t) -> bool {
        if(!isSolidType(type)) return false;
        for(const auto& n : currentDiff().notes){
            if(!isSolidType(n.noteTypeInt)) continue;
            if(!lanesOverlap(lane, width, n.lane, n.width)) continue;
            if(t >= n.time && t <= n.time + n.duration) return true;
        }
        return false;
    };

    // ロングを伸ばすとき、ぶつかる相手：start より後にある、同じレーンの重なってはいけないノーツの始点のうち一番近いもの
    //   なければ int32_t の最大値
    auto nextBlockingHead = [&](int lane, int width, int type, int32_t start, size_t selfIndex) -> int32_t {
        int32_t best = std::numeric_limits<int32_t>::max();
        if(!isSolidType(type)) return best;
        const auto& notes = currentDiff().notes;
        for(size_t j = 0; j < notes.size(); j++){
            if(j == selfIndex) continue;
            const auto& n = notes[j];
            if(!isSolidType(n.noteTypeInt)) continue;
            if(!lanesOverlap(lane, width, n.lane, n.width)) continue;
            if(n.time > start) best = std::min(best, n.time);
        }
        return best;
    };

    // keyHolds[k] が指しているノーツ。もう同じノーツでなければ nullptr
    auto holdNote = [&](const KeyHold& h) -> EditorNote* {
        if(!h.active || h.diff != currentDiffIndex) return nullptr;
        auto& notes = currentDiff().notes;
        if(h.index >= notes.size()) return nullptr;
        EditorNote& n = notes[h.index];
        if(n.time != h.start || n.lane != h.lane || !n.isLong()) return nullptr;
        return &n;
    };

    // レーンのキー keyLane が押された：ノーツを置く
    auto keyPlaceNote = [&](int keyLane, uint32_t nowTicks){
        const editorui::PaletteItem& pal = editorui::PALETTE[paletteIndex];
        const int width = std::clamp(toolWidth, 1, 6);
        const int lane = std::min(keyLane, 6 - width);   // 右にはみ出すときは左にずらす

        const int32_t t = std::max<int32_t>(0, snapToGrid(scrollTimeMs));
        if(keyPlaceBlocked(lane, width, pal.type, t)) return;   // 重なるなら置かない

        EditorNote n;
        n.noteTypeInt = pal.type;
        n.lane = lane;
        n.width = width;
        n.time = t;
        n.duration = 0;
        n.hasCustomSpeed = toolHasCustomSpeed;
        n.customSpeed = toolCustomSpeed;

        if(pal.isLong){
            // まずは 1 マス分の長さで置き、押している間に伸ばす
            n.duration = std::max<int32_t>(1, stepGrid(t, +1) - t);
        }

        currentDiff().notes.push_back(n);
        const size_t idx = currentDiff().notes.size() - 1;
        selectedNoteIndex = static_cast<int>(idx);

        if(pal.isLong){
            KeyHold& h = keyHolds[keyLane];
            h.active = true;
            h.diff = currentDiffIndex;
            h.index = idx;
            h.start = t;
            h.lane = lane;
        }

        // 手応え：置いた音と判定ボックスの光
        //   再生中で、置いた位置がまだ判定ラインより先（未来）なら、そこを通るときにプレビューが鳴らすので鳴らさない
        if(!(isPlaying && t > scrollTimeMs)){
            FlashEffect fx;
            fx.lane = lane;
            fx.width = width;
            fx.spawnTime = nowTicks;
            flashEffects.push_back(fx);
            Mix_Chunk* se = (pal.type == 5 && !pal.isLong && tap_sound_c) ? tap_sound_c : tap_sound;
            if(se) Mix_PlayChannel(-1, se, 0);
        }
    };

    // 押している間のロングを、判定ラインに一番近いグリッド線まで伸ばす（毎フレーム）
    auto updateKeyHolds = [&](){
        for(int k = 0; k < 6; k++){
            KeyHold& h = keyHolds[k];
            if(!h.active) continue;
            EditorNote* n = holdNote(h);
            if(!n){ h.active = false; continue; }   // 消された・動かされた・難易度を変えた

            const int32_t minEnd = std::max<int32_t>(h.start + 1, stepGrid(h.start, +1));   // 最低 1 マス
            int32_t end = std::max(minEnd, snapToGrid(scrollTimeMs));

            // 前にある重なってはいけないノーツの手前（1 マス前）で止める
            const int32_t limit = nextBlockingHead(n->lane, n->width, n->noteTypeInt, h.start, h.index);
            if(limit != std::numeric_limits<int32_t>::max()){
                const int32_t cap = stepGrid(limit, -1);
                end = (cap >= minEnd) ? std::min(end, cap) : minEnd;
            }
            n->duration = end - h.start;
        }
    };

    MusicAnalysis musicResult;          // Music Settings の結果（画面に出す用）
    bool hasMusicResult = false;
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
                if(eventMenuOpen){
                    eventMenuCloseRequest = true;   // メニューが開いているときは、メニューを閉じるだけ
                }
                else if(selectMenuOpen){
                    selectMenuCloseRequest = true;  // 範囲選択のメニューも同じく、閉じるだけ
                }
                else{
                    running = false;
                    nextScene = GameScene::Select;
                }
            }

            // ----- キーボード配置モード -----
            //   Ctrl+K：モードの ON/OFF（入力欄で文字を打っている最中でも切り替えられる）
            //   モード中は、レーンのキーを最優先にする。
            //   レーンのキーに W・S・M・1〜7・Space などが入っていても、下のショートカットには流さない
            //   （Ctrl 付きの操作と Esc はそのまま使える。ノーツを置く処理はイベントループの後で行う）
            if(e.type == SDL_KEYDOWN){
                const bool ctrlNow = (SDL_GetModState() & KMOD_CTRL) != 0;
                if(ctrlNow && e.key.repeat == 0 && e.key.keysym.sym == SDLK_k){
                    setKeyboardMode(!keyboardMode);
                    continue;
                }
                if(keyboardMode && !ctrlNow && !io.WantTextInput &&
                   e.key.keysym.sym != SDLK_ESCAPE && isLaneKey(e.key.keysym.scancode)){
                    continue;
                }
            }

            // 「文字を入力している最中」だけキーを ImGui に譲る。
            //   WantCaptureKeyboard は、ウィンドウを触っただけでも true になることがあるので使わない。
            //   WantTextInput は、入力欄（名前・BPM など）にカーソルがあるときだけ true になる。
            bool imguiWantsKeyBoard = io.WantTextInput;

            if(!imguiWantsKeyBoard && e.type == SDL_KEYDOWN && e.key.repeat == 0 && e.key.keysym.sym == SDLK_SPACE){
                togglePlayback();
            }

            // M キー：メトロノームの ON/OFF
            if(!imguiWantsKeyBoard && e.type == SDL_KEYDOWN && e.key.repeat == 0 && e.key.keysym.sym == SDLK_m &&
               (SDL_GetModState() & KMOD_CTRL) == 0){
                metronomeOn = !metronomeOn;
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

            // 範囲選択の操作
            //   入力欄で文字を打っている最中は、ImGui 側の Ctrl+C / V（文字のコピペ）を優先する
            //   ドラッグ中に押されるとノーツの番号がずれるので、ドラッグしていないときだけ
            if(!imguiWantsKeyBoard && e.type == SDL_KEYDOWN && e.key.repeat == 0 &&
               dragMode == DragMode::None && !boxSelecting){
                const SDL_Keycode key = e.key.keysym.sym;
                if(ctrlDown && key == SDLK_c){ copySelection();   continue; }
                if(ctrlDown && key == SDLK_v){ pasteClipboard();  continue; }
                if(ctrlDown && key == SDLK_r){ mirrorSelection(); continue; }
                if(key == SDLK_DELETE){        deleteSelection(); continue; }
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

                // Shift + 左ドラッグ：範囲選択を始める（イベント線を掴む・ノーツを置くより優先）
                const bool shiftHeld = (SDL_GetModState() & KMOD_SHIFT) != 0;
                if(shiftHeld && e.button.button == SDL_BUTTON_LEFT){
                    boxSelecting = true;
                    boxStartTime = scrollTimeMs + (judgeY - my) / effectivePixelsPerMs;
                    boxStartX = mx;
                    boxNowX = mx;
                    boxNowY = my;
                    selectedNoteIndex = -1;
                    continue;   // このクリックではノーツを置かない
                }

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
                                    const auto& notesRef = currentDiff().notes;
                                    if(notesRef[hitIndex].selected && countSelected() >= 2){
                                        // 範囲選択に入っているノーツを掴んだ → 選んだもの全部を動かす
                                        dragMode = DragMode::MoveSelection;
                                        moveOrigins.clear();
                                        for(size_t i = 0; i < notesRef.size(); i++){
                                            if(!notesRef[i].selected) continue;
                                            MoveOrigin o;
                                            o.index = i;
                                            o.time = notesRef[i].time;
                                            o.lane = notesRef[i].lane;
                                            for(const auto& k : notesRef[i].path) o.pathTimes.push_back(k.time);
                                            moveOrigins.push_back(o);
                                        }
                                        moveAnchorTime = snappedTime;
                                        moveAnchorLane = clickedLane;
                                        selectedNoteIndex = -1;
                                    }
                                    else{
                                        // 選ばれていないノーツを普通にクリック → 範囲選択は解除して、今までどおり1個だけ動かす
                                        clearSelection();
                                        selectedNoteIndex = hitIndex;
                                        dragMode = DragMode::MoveNote;
                                        dragNoteIndex = hitIndex;
                                    }
                                }
                                else{
                                    clearSelection();   // 空いている場所をクリック → 範囲選択は外す

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
                            bool erasedNote = false;   // ノーツの上を右クリックした（＝イベントメニューは出さない）
                            for(size_t idx = 0; idx < currentDiff().notes.size(); idx++){
                                const auto& n = currentDiff().notes[idx];
                                if(clickedLane >= n.lane && clickedLane <= n.lane + n.width - 1 && std::abs(n.time - snappedTime) < gridMs / 2){
                                    // 範囲選択に入っているノーツを右クリック → 消さずに、範囲選択のメニューを開き直す
                                    if(n.selected){
                                        selectMenuRequest = true;
                                        selectMenuPos = ImVec2(static_cast<float>(mx), static_cast<float>(my));
                                        erasedNote = true;
                                        break;
                                    }
                                    currentDiff().notes.erase(currentDiff().notes.begin() + idx);
                                    if(selectedNoteIndex == static_cast<int>(idx)) selectedNoteIndex = -1;
                                    else if(selectedNoteIndex > static_cast<int>(idx)) selectedNoteIndex--;   // 前が消えたら番号を詰める
                                    erasedNote = true;
                                    break;
                                }
                            }

                            // ノーツがない場所の右クリック → 一番近いグリッド線にイベントを追加するメニューを開く
                            //   ここ（SDL のイベント処理）は ImGui のフレームの外なので、開くのは「予約」だけにして、
                            //   実際の OpenPopup は ImGui のフレームの中で行う
                            if(!erasedNote){
                                eventMenuRequest = true;
                                eventMenuTime = snappedTime;
                                eventMenuPos = ImVec2(static_cast<float>(mx), static_cast<float>(my));
                                eventMenuBpm = getCurrentBpm(snappedTime);   // 入力欄の初期値：その位置の今の BPM
                                timeSigAt(currentDiff(), snappedTime, eventMenuSigNum, eventMenuSigDen);   // 拍子も今のもの
                            }
                        }
                    }
                }
            }

            // 範囲選択中：今のマウス位置を覚えておく（判定は毎フレーム updateBoxSelection で行う）
            if(e.type == SDL_MOUSEMOTION && boxSelecting){
                boxNowX = e.motion.x;
                boxNowY = e.motion.y;
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
                else if(dragMode == DragMode::MoveSelection){
                    auto& notes = currentDiff().notes;
                    int32_t dt = snappedTime - moveAnchorTime;
                    int dl = laneAtX(e.motion.x) - moveAnchorLane;

                    // 1個でもはみ出すなら、ずれ量を減らす → 全体が形を保ったまま端で止まる
                    for(const auto& o : moveOrigins){
                        if(o.index >= notes.size()) continue;
                        const int w = notes[o.index].width;
                        dl = std::clamp(dl, -o.lane, (6 - w) - o.lane);
                        if(o.time + dt < 0) dt = -o.time;
                    }

                    // 「掴んだ瞬間の位置 + ずれ量」で毎回計算し直す（足し続けないので誤差がたまらない）
                    for(const auto& o : moveOrigins){
                        if(o.index >= notes.size()) continue;
                        EditorNote& n = notes[o.index];
                        n.time = o.time + dt;
                        n.lane = o.lane + dl;
                        for(size_t k = 0; k < n.path.size() && k < o.pathTimes.size(); k++){
                            n.path[k].time = o.pathTimes[k] + dt;
                        }
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

                // 範囲選択を終えた → 何か選べていたら、離した位置にメニューを出す
                //   ここは ImGui のフレームの外なので「予約」だけして、開くのは ImGui のフレームの中で行う
                if(boxSelecting){
                    boxNowX = e.button.x;
                    boxNowY = e.button.y;
                    updateBoxSelection(effectivePixelsPerMs);   // 離した瞬間の位置で選び直す（最後の1フレーム分のずれを防ぐ）
                    if(countSelected() > 0){
                        selectMenuRequest = true;
                        selectMenuPos = ImVec2(static_cast<float>(e.button.x), static_cast<float>(e.button.y));
                    }
                }

                boxSelecting = false;
                moveOrigins.clear();
                dragMode = DragMode::None;
                dragNoteIndex = -1;
                dragEventIndex = -1;
            }
        }
        if(selectedNoteIndex >= static_cast<int>(currentDiff().notes.size())) selectedNoteIndex = -1;

        // 範囲選択中は毎フレーム判定し直す（マウスを止めたままホイールでスクロールしても追いかける）
        if(boxSelecting) updateBoxSelection(effectivePixelsPerMs);

        // =================================================================
        //  キーボード配置モード：レーンのキーの状態を見て、置く・伸ばす・離す
        //    ・入力欄で文字を打っている最中、Ctrl / Alt を押している間、メニューが開いている間は置かない
        //      （Ctrl+K の K がレーンのキーでも、ノーツが置かれないように）
        //    ・状態（kbPrev）は置かないときも毎フレーム更新する。こうしないと、Ctrl を離した瞬間に
        //      押しっぱなしのキーが「今押された」と判定されてしまう
        // =================================================================
        if(keyboardMode){
            InputDevices::get().refresh();   // コントローラーの抜き差しに合わせる（プレイ画面と同じ）
            const Uint8* ks = SDL_GetKeyboardState(nullptr);
            const bool modBlocked = (SDL_GetModState() & (KMOD_CTRL | KMOD_ALT)) != 0;
            const bool canPlace = !io.WantTextInput && !modBlocked && !eventMenuOpen && !selectMenuOpen;

            for(int i = 0; i < 6; i++){
                const bool pressed = !io.WantTextInput && isBindingPressed(editorSettings.laneBindings[i], ks);

                if(pressed && !kbPrev[i] && canPlace){
                    keyPlaceNote(i, nowTicks);               // 押した瞬間：置く
                }
                if(!pressed && kbPrev[i]){
                    kbReleaseTicks[i] = nowTicks;            // 離した瞬間：ビームを消し始める
                    keyHolds[i].active = false;              // ロングはここで長さが決まる
                }
                kbPrev[i] = pressed;
                kbHeld[i] = pressed;
            }

            updateKeyHolds();   // 押している間のロングを伸ばす
        }

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
        // ----- メトロノーム：前のフレームから今のフレームまでに過ぎた拍で鳴らす -----
        //   拍 = 拍子の分母の音符（4/4 なら 4 分、7/8 なら 8 分）。区切り（曲の始まり・BPM イベント）ごとに小節を数え直す
        if(isPlaying && metronomeOn){
            const double t0 = metronomePrevMs;
            const double t1 = static_cast<double>(scrollTimeMs);
            // 進んでいない、または大きく飛んだ（再生中にホイールで動かした）ときは鳴らさない
            if(t1 > t0 && t1 - t0 < 250.0){
                const TempoMap tm = currentTempoMap();
                const std::vector<SigSeg> segs = buildSigSegs(currentDiff(), tm);
                const double b0 = tempoBeatAt(tm, t0);
                const double b1 = tempoBeatAt(tm, t1);
                for(size_t si = 0; si < segs.size(); si++){
                    const SigSeg& sg = segs[si];
                    const double segEnd = (si + 1 < segs.size()) ? segs[si + 1].beat : std::numeric_limits<double>::infinity();
                    if(segEnd <= b0 || sg.beat > b1) continue;

                    const double unit = 4.0 / sg.den;   // 1 拍が 4 分音符いくつ分か
                    // (b0, b1] に入る拍だけ
                    long j = std::max(0L, static_cast<long>(std::floor((b0 - sg.beat) / unit + 1.0e-9)) + 1);
                    if(b0 < sg.beat) j = 0;
                    for(; ; j++){
                        const double beat = sg.beat + j * unit;
                        if(beat > b1 + 1.0e-9 || beat >= segEnd - 1.0e-9) break;
                        if(beat <= b0 + 1.0e-9) continue;
                        const bool barHead = (j % sg.num) == 0;
                        metronome.play(MetronomeSounds::kindFor(sg.den), barHead, metronomeVolume);
                    }
                }
            }
        }
        metronomePrevMs = static_cast<double>(scrollTimeMs);

        std::erase_if(flashEffects, [nowTicks](const FlashEffect& fx){
            return (nowTicks - fx.spawnTime) > 200;
        });

        //ImGuiフレーム
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // =================================================================
        //  右クリックメニュー：SPEED / BPM イベントを追加する
        // =================================================================
        if(eventMenuRequest){
            ImGui::OpenPopup("##eventmenu");
            ImGui::SetNextWindowPos(eventMenuPos, ImGuiCond_Always);   // 右クリックした場所に出す
            eventMenuRequest = false;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
        eventMenuOpen = ImGui::BeginPopup("##eventmenu");
        ImGui::PopStyleVar();
        if(eventMenuOpen){
            // 選曲画面と同じシアンの枠
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 wp = ImGui::GetWindowPos();
                const ImVec2 ws = ImGui::GetWindowSize();
                dl->PushClipRectFullScreen();
                dl->AddRect(ImVec2(wp.x + 1.5f, wp.y + 1.5f), ImVec2(wp.x + ws.x - 1.5f, wp.y + ws.y - 1.5f),
                            editorui::COL_ACCENT, 0.0f, 0, 3.0f);
                dl->PopClipRect();
            }

            if(eventMenuCloseRequest){
                ImGui::CloseCurrentPopup();
            }

            ImGui::Text("%d ms に追加", eventMenuTime);
            ImGui::Separator();
            ImGui::PushItemWidth(200.0f);

            // ----- SPEED -----
            ImGui::TextColored(ImVec4(1.0f, 0.86f, 0.31f, 1.0f), "SPEED");
            ImGui::InputDouble("目標の速度##ctx", &eventMenuSpeed, 0.1, 1.0, "%.2f");
            const char* easeNames[] = {"Straight(1)", "easeOutCubic(2)", "easeInCubic(3)"};
            int easeIdx = std::clamp(eventMenuSpeedEasing - 1, 0, 2);
            if(ImGui::Combo("イージング##ctx", &easeIdx, easeNames, 3)) eventMenuSpeedEasing = easeIdx + 1;
            ImGui::InputInt("変化にかける時間(ms)##ctx", &eventMenuSpeedDuration, 10, 100);
            if(eventMenuSpeedDuration < 0) eventMenuSpeedDuration = 0;

            if(ImGui::Button("SPEED を追加", ImVec2(200.0f, 0.0f))){
                auto& evs = currentDiff().speedEvents;
                // 同じ時刻にもうあれば、新しく作らずに上書きする（重なって見えなくなるのを防ぐ）
                auto it = std::find_if(evs.begin(), evs.end(), [&](const EditorSpeedEvent& s){ return s.time == eventMenuTime; });
                EditorSpeedEvent ev;
                ev.time = eventMenuTime;
                ev.target = eventMenuSpeed;
                ev.easing = eventMenuSpeedEasing;
                ev.duration = eventMenuSpeedDuration;
                if(it != evs.end()) *it = ev;
                else evs.push_back(ev);
                std::sort(evs.begin(), evs.end(), [](const EditorSpeedEvent& a, const EditorSpeedEvent& b){ return a.time < b.time; });
                ImGui::CloseCurrentPopup();
            }

            ImGui::Separator();

            // ----- BPM -----
            ImGui::TextColored(ImVec4(0.31f, 0.86f, 1.0f, 1.0f), "BPM");
            ImGui::InputDouble("BPM##ctx", &eventMenuBpm, 1.0, 10.0, "%.3f");
            if(eventMenuBpm <= 0.0) eventMenuBpm = 1.0;
            timeSigInput("sig##ctx", eventMenuSigNum, eventMenuSigDen);

            if(ImGui::Button("BPM を追加", ImVec2(200.0f, 0.0f))){
                auto& evs = currentDiff().bpmEvents;
                auto it = std::find_if(evs.begin(), evs.end(), [&](const EditorBpmEvent& b){ return b.time == eventMenuTime; });
                if(it != evs.end()){
                    it->bpm = eventMenuBpm;
                    it->sigNum = eventMenuSigNum;
                    it->sigDen = eventMenuSigDen;
                }
                else{
                    EditorBpmEvent b;
                    b.time = eventMenuTime;
                    b.bpm = eventMenuBpm;
                    b.sigNum = eventMenuSigNum;
                    b.sigDen = eventMenuSigDen;
                    evs.push_back(b);
                }
                std::sort(evs.begin(), evs.end(), [](const EditorBpmEvent& a, const EditorBpmEvent& b){ return a.time < b.time; });
                ImGui::CloseCurrentPopup();
            }

            ImGui::Separator();

            // ----- TEST PLAY -----
            ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.55f, 1.0f), "TEST PLAY");
            ImGui::RadioButton("ここから##test", &testPlayFrom, 0);
            ImGui::SameLine();
            ImGui::RadioButton("最初から##test", &testPlayFrom, 1);
            ImGui::Checkbox("オートプレイ##test", &testPlayAutoplay);

            if(ImGui::Button("テストプレイ (編集中の難易度)", ImVec2(-1.0f, 0.0f))){
                testPlayRequest = true;
                testPlayStartMs = (testPlayFrom == 0) ? eventMenuTime : 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextDisabled("Esc → リタイアでエディタに戻ります");

            ImGui::PopItemWidth();
            ImGui::EndPopup();
        }
        eventMenuCloseRequest = false;

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
        //  範囲選択のメニュー（右クリックメニューと同じ出方）
        //    ・Shift+ドラッグを離した位置／選んだノーツを右クリックした位置にポップアップで出る
        //    ・メニューの外をクリックする、Esc を押す、ボタンを押すと閉じる
        //    ※ ImGui のウィンドウなので、必ず NewFrame 〜 Render の間（ここ）で作る
        // =================================================================
        if(selectMenuRequest){
            ImGui::OpenPopup("##selectmenu");
            ImGui::SetNextWindowPos(selectMenuPos, ImGuiCond_Always);   // マウスの位置に出す
            selectMenuRequest = false;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
        selectMenuOpen = ImGui::BeginPopup("##selectmenu");
        ImGui::PopStyleVar();
        if(selectMenuOpen){
            // 右クリックメニューと同じシアンの枠
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 wp = ImGui::GetWindowPos();
                const ImVec2 ws = ImGui::GetWindowSize();
                dl->PushClipRectFullScreen();
                dl->AddRect(ImVec2(wp.x + 1.5f, wp.y + 1.5f), ImVec2(wp.x + ws.x - 1.5f, wp.y + ws.y - 1.5f),
                            editorui::COL_ACCENT, 0.0f, 0, 3.0f);
                dl->PopClipRect();
            }

            const int selCount = countSelected();

            // Esc が押された／キー操作（Delete など）で選択がなくなった → 閉じる
            if(selectMenuCloseRequest || selCount == 0){
                ImGui::CloseCurrentPopup();
            }

            ImGui::Text("%d 個を選択中", selCount);
            ImGui::Separator();

            ImGui::TextColored(ImVec4(0.0f, 0.86f, 1.0f, 1.0f), "SELECT");

            // ボタン＋右にキーの説明。押せないときは灰色にする。押したらメニューを閉じる
            auto opButton = [&](const char* label, const char* keyHint, bool enabled) -> bool {
                if(!enabled) ImGui::BeginDisabled();
                const bool pressed = ImGui::Button(label, ImVec2(200.0f, 0.0f));
                if(!enabled) ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("%s", keyHint);
                if(pressed) ImGui::CloseCurrentPopup();
                return pressed;
            };

            if(opButton("コピー",   "Ctrl+C", true))               copySelection();
            if(opButton("貼り付け", "Ctrl+V", !clipboard.empty())) pasteClipboard();
            if(opButton("左右反転", "Ctrl+R", true))               mirrorSelection();
            if(opButton("削除",     "Delete", true))               deleteSelection();

            ImGui::Separator();
            if(ImGui::Button("選択解除", ImVec2(200.0f, 0.0f))){
                clearSelection();
                ImGui::CloseCurrentPopup();
            }

            ImGui::TextDisabled("貼り付けは判定ラインの位置に入ります");
            ImGui::TextDisabled("選んだノーツはドラッグでまとめて移動");
            ImGui::TextDisabled("選んだノーツを右クリックで再表示");

            ImGui::EndPopup();
        }
        selectMenuCloseRequest = false;

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

            // ----- スナップ：一覧から選ぶ ＋ 一番下の「自由入力」で好きな分割数を打てる -----
            //   コンボの一番下（1920 の下）が「自由入力」。選ぶと右に数字の入力欄が出る
            //   入力欄は +/- ボタンなし（パネルの横幅に収めるため）。キーボードで打つ
            {
                static const char* gridNames[] = {"4", "6", "8", "12", "16", "24", "32", "48", "64", "96", "128", "1920", "自由入力"};
                static const int   gridValue[] = {4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 1920};
                constexpr int GRID_PRESET_COUNT = static_cast<int>(sizeof(gridValue) / sizeof(gridValue[0]));
                constexpr int GRID_CUSTOM_INDEX = GRID_PRESET_COUNT;   // 「自由入力」の番号（一覧の一番下）

                // 今のコンボの位置：自由入力中ならいちばん下。そうでなければ一覧から同じ値を探す
                int gridComboIndex = GRID_CUSTOM_INDEX;
                if(!gridCustom){
                    for(int gi = 0; gi < GRID_PRESET_COUNT; gi++){
                        if(gridValue[gi] == gridDivisor){
                            gridComboIndex = gi;
                            break;
                        }
                    }
                }

                ImGui::SetNextItemWidth(gridCustom ? 120.0f : 160.0f);
                if(ImGui::Combo("##snapcombo", &gridComboIndex, gridNames, GRID_PRESET_COUNT + 1)){
                    if(gridComboIndex == GRID_CUSTOM_INDEX){
                        gridCustom = true;
                        gridDivisor = customGridDivisor;   // 前に打った値から始める
                    }
                    else{
                        gridCustom = false;
                        gridDivisor = gridValue[gridComboIndex];
                    }
                }

                if(gridCustom){
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80.0f);
                    ImGui::InputInt("##snapcustom", &customGridDivisor, 0, 0);
                    customGridDivisor = std::clamp(customGridDivisor, 1, GRID_DIVISOR_MAX);
                    gridDivisor = customGridDivisor;   // 打った値をそのままスナップに使う
                }

                ImGui::SameLine();
                ImGui::TextUnformatted("分スナップ");
            }

            // キーボード配置モード（Ctrl+K と同じ）
            bool kbModeCheck = keyboardMode;
            if(ImGui::Checkbox("キーボード配置 (Ctrl+K)##tool", &kbModeCheck)){
                setKeyboardMode(kbModeCheck);
            }

            ImGui::TextDisabled("左:置く/選ぶ 右:消す 1〜7:ノーツ");
            ImGui::TextDisabled("Shift+ドラッグ:範囲選択 Ctrl+ホイール:拡大");
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

                // テンポは親（今編集している難易度）と同じにする
                //   ・曲頭の BPM と拍子
                //   ・BPM イベント（時刻・BPM・拍子をそのまま写す → 同じ位置に小節線・グリッドが来る）
                //   ノーツと SPEED イベントは写さない（子の譜面は空から作る）
                newDiff.bpm = currentDiff().bpm;
                newDiff.sigNum = currentDiff().sigNum;
                newDiff.sigDen = currentDiff().sigDen;
                newDiff.bpmEvents = currentDiff().bpmEvents;
                std::stable_sort(newDiff.bpmEvents.begin(), newDiff.bpmEvents.end(),
                    [](const EditorBpmEvent& a, const EditorBpmEvent& b){ return a.time < b.time; });

                // 「今のテンポ表 = 合わせ済み」にしておく（写した直後にノーツ追従の処理が走らないように）
                {
                    std::vector<TempoPoint> pts;
                    for(const auto& b : newDiff.bpmEvents) pts.push_back({static_cast<double>(b.time), b.bpm});
                    newDiff.alignedMap = makeTempoMap(newDiff.bpm, pts);
                    newDiff.tempoAligned = true;
                }

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
            timeSigInput("sig##diff", currentDiff().sigNum, currentDiff().sigDen);
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

            ImGui::Checkbox("メトロノーム (M)##play", &metronomeOn);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140.0f);
            ImGui::SliderInt("音量##metronome", &metronomeVolume, 0, 100);

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
                    b.bpm = getCurrentBpm(scrollTimeMs);                        // 今の位置の BPM と拍子を初期値に
                    timeSigAt(currentDiff(), scrollTimeMs, b.sigNum, b.sigDen);
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
                    timeSigInput("sig##bpm", b.sigNum, b.sigDen);
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

                // ----- 選曲画面の試聴 -----
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.31f, 1.0f, 0.55f, 1.0f), "PREVIEW（選曲画面の試聴）");
                ImGui::InputDouble("試聴の開始(ms)##file", &meta.previewStartMs, 100.0, 1000.0, "%.0f");
                if(meta.previewStartMs < 0.0) meta.previewStartMs = 0.0;

                if(ImGui::Button("判定ラインの位置を試聴の開始にする##file")){
                    // 譜面上の時刻 → 曲ファイル上の位置（seekMusicIfNeeded と同じ「音声位置 = 譜面時刻 + オフセット」）
                    meta.previewStartMs = std::max(0.0, std::round(scrollTimeMs + meta.offsetMs));
                }
                if(ImGui::Button("試聴の開始へ移動##file")){
                    scrollTimeMs = std::max<int32_t>(0, static_cast<int32_t>(std::lround(meta.previewStartMs - meta.offsetMs)));
                    if(bgm) seekMusicIfNeeded(scrollTimeMs);
                }
                ImGui::TextDisabled("ここから %d 秒、フェードしながら繰り返し流れます", songpreview::LENGTH_MS / 1000);

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

                // ----- Music Settings：オフセット → 曲全体の BPM → 途中の BPM 変化 を自動で設定（拍子は変えない） -----
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.0f, 0.86f, 1.0f, 1.0f), "MUSIC SETTINGS");
                if(ImGui::Button("Music Settings（オフセットと BPM を自動設定）", ImVec2(-1.0f, 0.0f))){
                    if(meta.bgm.empty()){
                        musicResult = MusicAnalysis{};
                        musicResult.error = "曲ファイル名が空です";
                    }
                    else{
                        musicResult = analyzeMusic("sounds/" + meta.bgm);
                        if(musicResult.ok){
                            applyMusicAnalysis(musicResult, meta, difficulties);
                            selectedNoteIndex = -1;
                            dragMode = DragMode::None;
                            dragNoteIndex = -1;
                        }
                    }
                    hasMusicResult = true;
                }
                ImGui::TextDisabled("曲全体を解析します（数秒止まります）");
                ImGui::TextDisabled("すべての難易度に反映され、元に戻せません");

                if(hasMusicResult){
                    if(!musicResult.ok){
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", musicResult.error.c_str());
                    }
                    else{
                        ImGui::Text("オフセット: %.1f ms", meta.offsetMs);
                        ImGui::Text("BPM: %.3f（拍のはっきりさ %.0f%%）", currentDiff().bpm, musicResult.clarity * 100.0);
                        ImGui::TextDisabled("拍子は変えていません（今: %d/%d）", currentDiff().sigNum, currentDiff().sigDen);
                        ImGui::TextDisabled("拍の間の音の型: %s", musicResult.pattern.c_str());
                        if(currentDiff().bpmEvents.empty()){
                            ImGui::Text("BPM の変化: なし");
                        }
                        else{
                            ImGui::Text("BPM の変化: %zu か所", currentDiff().bpmEvents.size());
                            for(const auto& b : currentDiff().bpmEvents){
                                ImGui::Text("  %7d ms : %.3f BPM  %d/%d", b.time, b.bpm, b.sigNum, b.sigDen);
                            }
                        }

                        // 倍テン・半テンの直し：ノーツの位置（ms）は変えずに、BPM の数え方だけ変える
                        if(ImGui::SmallButton("BPM をすべて x2##music")) scaleAllBpm(2.0, difficulties);
                        ImGui::SameLine();
                        if(ImGui::SmallButton("BPM をすべて /2##music")) scaleAllBpm(0.5, difficulties);
                    }
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
                        // 1. BPM イベント自身を、拍の位置を保ったまま動かし、変えたあとのテンポ表を作る
                        //    （値を変えた・追加した・時刻を直接変えた、のどの場合も同じ計算でよい）
                        const TempoMap newMap = realignBpmEvents(d.alignedMap, d.bpm, d.bpmEvents);

                        // 2. ノーツ：変える前の拍の位置 → 変えたあとのテンポ表での時刻
                        for(auto& n : d.notes){
                            const int32_t newStart = remapTime(d.alignedMap, newMap, n.time);
                            if(n.duration > 0){
                                const int32_t newEnd = remapTime(d.alignedMap, newMap, n.time + n.duration);
                                n.duration = std::max<int32_t>(1, newEnd - newStart);
                            }
                            for(auto& k : n.path) k.time = remapTime(d.alignedMap, newMap, k.time);
                            n.time = newStart;
                        }

                        // 3. SPEED イベントも同じように拍の位置を保って動かす
                        for(auto& sev : d.speedEvents){
                            sev.time = remapTime(d.alignedMap, newMap, sev.time);
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

        // グリッド線・小節線
        //   どちらも区切り（曲の始まり・各 BPM イベント）ごとに、その先頭から数え直す
        {
            const TempoMap tm = currentTempoMap();
            const std::vector<SigSeg> segs = buildSigSegs(currentDiff(), tm);
            const double gb = 4.0 / gridDivisor;   // グリッド1マスの拍数

            const double viewStartTime = scrollTimeMs - (SCREEN_H - judgeY) / effectivePixelsPerMs;
            const double viewEndTime   = scrollTimeMs + judgeY / effectivePixelsPerMs;
            const double beat0 = tempoBeatAt(tm, viewStartTime);
            const double beat1 = tempoBeatAt(tm, viewEndTime);

            // ----- グリッド線（拍の線は明るく） -----
            long drawn = 0;
            for(size_t si = 0; si < segs.size(); si++){
                const SigSeg& sg = segs[si];
                const double segEnd = sigSegEnd(segs, si);
                if(segEnd <= beat0) continue;             // この区切りは画面より下（過去）
                if(si > 0 && sg.beat > beat1) break;      // ここから先は画面より上（未来）

                const double unit = 4.0 / sg.den;         // 拍子の 1 拍（4/4 なら 4 分、3/8 なら 8 分）
                long kFirst = static_cast<long>(std::floor((beat0 - sg.beat) / gb));
                if(si > 0) kFirst = std::max(0L, kFirst); // 曲の始まりより前（負の拍）は最初の区切りだけで描く
                const long kLast = static_cast<long>(std::ceil((std::min(beat1, segEnd) - sg.beat) / gb));

                for(long k = kFirst; k <= kLast; k++){
                    if(++drawn > 20000) break;            // 拡大しすぎたときの保険
                    const double beat = sg.beat + k * gb;
                    if(beat >= segEnd - 1.0e-9) break;    // 境目の線は次の区切りが引く

                    const double t = tempoTimeAt(tm, beat);
                    const int gy = judgeY - static_cast<int>((t - scrollTimeMs) * effectivePixelsPerMs);

                    const double r = (beat - sg.beat) / unit;   // 区切りの先頭から何拍目か
                    const bool isBeatLine = std::abs(r - std::round(r)) < 1.0e-6;
                    if(isBeatLine) SDL_SetRenderDrawColor(renderer, 120, 120, 140, 255);
                    else SDL_SetRenderDrawColor(renderer, 50, 50, 60, 255);

                    SDL_RenderDrawLine(renderer, startX, gy, lanesRight, gy);
                }
            }

            // ----- 小節線（拍の線より明るく太く、左に小節番号） -----
            {
                ImFont* f = ImGui::GetFont();
                long measureNo = 1;   // その区切りの最初の小節の番号
                for(size_t si = 0; si < segs.size(); si++){
                    const SigSeg& sg = segs[si];
                    const double len = sg.num * 4.0 / sg.den;                    // 1 小節が 4 分音符いくつ分か
                    const bool last = (si + 1 == segs.size());
                    const double segEnd = last ? std::numeric_limits<double>::infinity() : segs[si + 1].beat;

                    // この区切りに入る小節の数（最後の区切りは終わりなし）
                    long count = last ? std::numeric_limits<long>::max()
                                      : static_cast<long>(std::ceil((segEnd - sg.beat) / len - 1.0e-6));
                    if(count < 1) count = 1;

                    const long kMin = std::max(0L, static_cast<long>(std::floor((beat0 - sg.beat) / len)));
                    const long kMax = std::min(count - 1, static_cast<long>(std::ceil((beat1 - sg.beat) / len)));
                    if(kMax - kMin <= 5000){
                        for(long k = kMin; k <= kMax; k++){
                            const double beat = sg.beat + k * len;
                            if(beat >= segEnd - 1.0e-9) break;
                            const double t = tempoTimeAt(tm, beat);
                            const int gy = judgeY - static_cast<int>((t - scrollTimeMs) * effectivePixelsPerMs);
                            if(gy < -30 || gy > SCREEN_H + 30) continue;

                            SDL_SetRenderDrawColor(renderer, 200, 200, 235, 255);
                            SDL_Rect bar = {startX, gy - 1, lanesRight - startX, 2};
                            SDL_RenderFillRect(renderer, &bar);

                            // 小節番号。区切りの最初の小節には拍子も書く
                            char label[32];
                            if(k == 0) std::snprintf(label, sizeof(label), "%ld  %d/%d", measureNo + k, sg.num, sg.den);
                            else       std::snprintf(label, sizeof(label), "%ld", measureNo + k);
                            ImGui::GetBackgroundDrawList()->AddText(f, 15.0f,
                                ImVec2(static_cast<float>(startX + 4), static_cast<float>(gy - 18)),
                                IM_COL32(170, 170, 205, 255), label);
                        }
                    }
                    if(!last) measureNo += count;
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
            snprintf(buf, sizeof(buf), "BPM %.1f  %d/%d", b.bpm, b.sigNum, b.sigDen);
            ImVec2 textSize = ImGui::CalcTextSize(buf);
            bgDraw->AddText(ImVec2(static_cast<float>(lanesRight - 6) - textSize.x, static_cast<float>(ly - 24)),
                            IM_COL32(80, 220, 255, 255), buf);
        }

        // ----- 選曲画面の試聴範囲：レーンの左に緑の縦帯、開始位置に線 -----
        {
            const double pStart = meta.previewStartMs - meta.offsetMs;   // 曲ファイル上の位置 → 譜面上の時刻
            const double pEnd   = pStart + songpreview::LENGTH_MS;
            const double yS = judgeY - (pStart - scrollTimeMs) * effectivePixelsPerMs;   // 開始（下側）
            const double yE = judgeY - (pEnd   - scrollTimeMs) * effectivePixelsPerMs;   // 終了（上側）

            if(yS > -20.0 && yE < SCREEN_H + 20.0){   // 一部でも画面に入っていれば描く
                const int top    = static_cast<int>(std::clamp(yE, -20.0, static_cast<double>(SCREEN_H + 20)));
                const int bottom = static_cast<int>(std::clamp(yS, -20.0, static_cast<double>(SCREEN_H + 20)));
                SDL_SetRenderDrawColor(renderer, 80, 255, 140, 160);
                SDL_Rect band = {startX - 12, top, 6, bottom - top};
                SDL_RenderFillRect(renderer, &band);

                if(yS <= SCREEN_H + 20.0){
                    SDL_SetRenderDrawColor(renderer, 80, 255, 140, 200);
                    SDL_Rect line = {startX, bottom - 1, lanesRight - startX, 2};
                    SDL_RenderFillRect(renderer, &line);
                    bgDraw->AddText(ImVec2(static_cast<float>(startX + 6), static_cast<float>(bottom + 4)),
                                    IM_COL32(80, 255, 140, 255), "PREVIEW");
                }
            }
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

        // ----- キーボード配置モード：キービーム -----
        //   押している間、そのレーンの判定ラインから上へ、下が濃く上に行くほど透明になる光の柱を出す
        //   離したら KEY_BEAM_FADE_MS かけて消える
        //   色を頂点ごとに変えて GPU に混ぜてもらうため、SDL_RenderGeometry（4頂点＝2つの三角形）で描く
        if(keyboardMode){
            const float beamH = 420.0f;
            for(int l = 0; l < 6; l++){
                float a = 0.0f;
                if(kbHeld[l]) a = 1.0f;
                else if(kbReleaseTicks[l] != 0 && nowTicks - kbReleaseTicks[l] < KEY_BEAM_FADE_MS){
                    a = 1.0f - static_cast<float>(nowTicks - kbReleaseTicks[l]) / KEY_BEAM_FADE_MS;
                }
                if(a <= 0.0f) continue;

                const float x0 = static_cast<float>(laneX[l]) + NOTE_SIDE_MARGIN;
                const float x1 = static_cast<float>(laneX[l] + laneWidth) - NOTE_SIDE_MARGIN;
                const float yBottom = static_cast<float>(judgeY);
                const float yTop = yBottom - beamH;
                const Uint8 bottomAlpha = static_cast<Uint8>(90.0f * a);   // 「薄い」ビーム：一番濃いところでも 90/255

                const SDL_Color top = {150, 230, 255, 0};
                const SDL_Color bottom = {150, 230, 255, bottomAlpha};
                SDL_Vertex v[4] = {
                    {SDL_FPoint{x0, yTop},    top,    SDL_FPoint{0.0f, 0.0f}},
                    {SDL_FPoint{x1, yTop},    top,    SDL_FPoint{0.0f, 0.0f}},
                    {SDL_FPoint{x0, yBottom}, bottom, SDL_FPoint{0.0f, 0.0f}},
                    {SDL_FPoint{x1, yBottom}, bottom, SDL_FPoint{0.0f, 0.0f}},
                };
                const int idx[6] = {0, 1, 2, 2, 1, 3};
                SDL_RenderGeometry(renderer, nullptr, v, 4, idx, 6);
            }
        }

        // ----- 右クリックメニューが開いている間、追加先の線を光らせる -----
        if(eventMenuOpen){
            const int my = judgeY - static_cast<int>((eventMenuTime - scrollTimeMs) * effectivePixelsPerMs);
            if(my > -10 && my < SCREEN_H + 10){
                SDL_SetRenderDrawColor(renderer, 0, 255, 255, 230);
                SDL_Rect mark = {startX, my - 2, lanesRight - startX, 4};
                SDL_RenderFillRect(renderer, &mark);
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
                const bool selected = (static_cast<int>(idx) == selectedNoteIndex) || n.selected;

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

        // ----- キーボード配置モード：レーンの下にキーの名前 -----
        //   判定ラインの少し下に、レーンごとの小さな枠を並べてキーの名前を書く
        //   押している間は枠をシアンで塗り、文字を暗くする（キー設定画面の「色反転」と同じ考え方）
        //   文字は ImGui の背景に描く（日本語フォントがそのまま使える。パネルより奥になる）
        if(keyboardMode){
            const int boxY = judgeY + 60;
            const int boxH = 48;
            ImFont* f = ImGui::GetFont();
            for(int l = 0; l < 6; l++){
                const SDL_Rect r = {laneX[l] + 6, boxY, laneWidth - 12, boxH};
                if(kbHeld[l]){
                    SDL_SetRenderDrawColor(renderer, 0, 200, 255, 200);
                    SDL_RenderFillRect(renderer, &r);
                    SDL_SetRenderDrawColor(renderer, 0, 255, 255, 255);
                }
                else{
                    SDL_SetRenderDrawColor(renderer, 30, 30, 50, 200);
                    SDL_RenderFillRect(renderer, &r);
                    SDL_SetRenderDrawColor(renderer, 110, 110, 150, 255);
                }
                SDL_RenderDrawRect(renderer, &r);

                // 長い名前（Left Shift など）は枠の幅に収まるまで文字を小さくする
                const char* name = kbKeyNames[l].c_str();
                float fs = 26.0f;
                ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, name);
                const float maxW = static_cast<float>(r.w - 8);
                if(ts.x > maxW && ts.x > 0.0f){
                    fs *= maxW / ts.x;
                    ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, name);
                }
                bgDraw->AddText(f, fs,
                                ImVec2(r.x + r.w * 0.5f - ts.x * 0.5f, r.y + r.h * 0.5f - ts.y * 0.5f),
                                kbHeld[l] ? IM_COL32(15, 15, 25, 255) : IM_COL32(235, 235, 245, 255), name);
            }

            // モード中であることの表示
            const char* modeText = "キーボード配置モード   Ctrl+K で終了";
            const ImVec2 mts = f->CalcTextSizeA(18.0f, FLT_MAX, 0.0f, modeText);
            bgDraw->AddText(f, 18.0f,
                            ImVec2((startX + lanesRight) * 0.5f - mts.x * 0.5f, static_cast<float>(boxY + boxH + 14)),
                            IM_COL32(0, 220, 255, 255), modeText);
        }

        // ----- 範囲選択の四角 -----
        if(boxSelecting){
            // 始点は時刻で覚えているので、今のスクロール位置で y に直す（int があふれないよう丸める）
            const double y0d = judgeY - (boxStartTime - scrollTimeMs) * effectivePixelsPerMs;
            const int y0 = static_cast<int>(std::clamp(y0d, -10000.0, 10000.0));
            SDL_Rect r = {
                std::min(boxStartX, boxNowX), std::min(y0, boxNowY),
                std::abs(boxNowX - boxStartX), std::abs(boxNowY - y0)
            };
            SDL_SetRenderDrawColor(renderer, 0, 200, 255, 40);    // 中を薄いシアンで塗る
            SDL_RenderFillRect(renderer, &r);
            SDL_SetRenderDrawColor(renderer, 0, 255, 255, 220);   // 枠
            SDL_RenderDrawRect(renderer, &r);
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

        // =================================================================
        //  テストプレイ
        //    ImGui のフレームを描き終えた「後」で始める。
        //    playGame は自分で描画ループを回すので、ImGui のフレームの途中で呼ぶと描画が混ざってしまう。
        // =================================================================
        if(testPlayRequest && running){
            testPlayRequest = false;

            // 1. エディタ側の再生を止める
            isPlaying = false;
            if(bgm) Mix_HaltMusic();
            musicNeedsStart = true;
            flashEffects.clear();

            // 2. 今の譜面を一時ファイルに保存する（本来の保存先は書き換えない）
            //    playGame はファイルから譜面を読むので、編集中の内容をいったんファイルにする
            const std::string testPath = "editor_testplay.json";   // scores/ に置くと選曲画面に出てしまうので外に置く
            if(saveChart(testPath, meta, difficulties)){
                PlayerSettings testSettings;
                loadPlayerSettings(testSettings);   // オフセット・音量・タップ音はプレイと同じ設定を使う

                PlayTestOptions opt;
                opt.startMs = testPlayStartMs;

                // 3. プレイ。ポーズメニューの「リトライ」は Play が返ってくるので、同じ条件でやり直す
                GameScene r;
                do{
                    ResultData testResult;
                    r = playGame(window, renderer, testPath, currentDiffIndex, testResult, testSettings,
                                 testPlayAutoplay, nullptr, nullptr, &opt);
                }while(r == GameScene::Play);

                if(r == GameScene::Shutdown){
                    running = false;
                    nextScene = GameScene::Shutdown;
                }
            }
            else{
                saveToastOk = false;            // 一時ファイルが作れなかった → 失敗の通知を出す
                saveToastTicks = SDL_GetTicks();
            }

            // 4. エディタに戻る準備
            //    プレイ中の時間が「1フレームの経過時間」として足されないように、時計を今に合わせる
            lastFrameticks = SDL_GetTicks();
            SDL_ShowCursor(SDL_ENABLE);

            // テストプレイ中に押していたキーを「エディタに戻った瞬間に押された」と数えないように、今の状態に合わせ直す
            if(keyboardMode) setKeyboardMode(true);
        }
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
    Mix_HaltChannel(-1);   // 鳴っている途中のメトロノームの音を止めてから解放する
    metronome.destroy();

    return nextScene;
}