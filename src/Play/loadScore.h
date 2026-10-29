#pragma once

#include "GameCommon.h"
#include "type.h"
#include "Editortempo.h"   // 拍 ⇔ 時刻の変換（エディタのグリッド線と同じ計算を使う）
#include <nlohmann/json.hpp>
#include <utility>
#include <cmath>

using json = nlohmann::json;

struct DifficultyInfo{
    std::string name;
    std::string level;
    std::string chartCreator;
    int fileIndex = 0;

    double currentScale = 1.0;
    double currentX = 0.0;
    double currentY = 0.0;
    bool positionInitialized = false;
};

inline std::vector<DifficultyInfo> listDifficulties(const std::string& filename){
    std::vector<DifficultyInfo> result;

    std::ifstream file(toUtf8Path(filename));
    if(!file.is_open()) return result;

    json j;
    try{
        file >> j;
    }
    catch(const json::parse_error&){
        return result;
    }

    if(j.contains("difficulties") && j.at("difficulties").is_array()){
        for(const auto& d : j.at("difficulties")){
            DifficultyInfo info;
            info.name = d.value("name", std::string("DEFAULT"));
            info.level = d.value("level", std::string("0"));
            info.chartCreator = d.value("chartCreator", std::string(""));
            info.fileIndex = static_cast<int>(result.size());
            result.push_back(info);
        }
    }
    else{
        DifficultyInfo info;
        info.name = "DEFAULT";
        info.level = j.value("level", std::string("0"));
        result.push_back(info);
    }
    return result;
}

// =====================================================================
//  ロングの中継点
//    ・曲全体で共通の 16分グリッド（時刻 0ms = 拍 0 から、BPM で刻んだ線）の上に置く
//      → 同時に押しているロングは、同じ瞬間に中継点を取る
//      → エディタの 16分のグリッド線と同じ位置になる（同じ Editortempo.h の計算を使う）
//    ・BPM が途中で変わる譜面でも、拍で数えるので線の位置がずれない
//    ・時刻は「譜面の時刻」で決めるので、いつ押し始めても数は変わらない
//    ・始点・終点から半マス以内の線は使わない
//      （始点の判定・終点の判定と、ほぼ同時に重ならないように）
// =====================================================================
inline constexpr double LONG_TICKS_PER_BEAT = 4.0;   // 1拍に何回（4 = 16分音符 = BPM の4倍）

inline void buildLongTicks(Note& n, const TempoMap& tempo){
    n.tickTimes.clear();
    n.nextTick = 0;
    if(n.type != NoteType::Long || n.durationMs <= 0) return;

    const double grid = 1.0 / LONG_TICKS_PER_BEAT;   // グリッド1マスの長さ（拍）

    // 始点と終点を「何マス目か」に直す（小数になることもある：3連符の位置など）
    const double startK = tempoBeatAt(tempo, static_cast<double>(n.targetTime)) / grid;
    const double endK   = tempoBeatAt(tempo, static_cast<double>(n.targetTime) + n.durationMs) / grid;

    // 使う線の番号 k は「始点より半マス以上あと」かつ「終点より半マス以上まえ」
    //   ・始点がちょうど線の上（startK が整数）なら、次の線から
    //   ・時刻は整数 ms なので startK が 7.9999 のようになっても、半マスの余裕で同じ結果になる
    long k = static_cast<long>(std::floor(startK + 0.5)) + 1;
    for(int guard = 0; guard < 100000 && k < endK - 0.5; guard++, k++){
        const double t = tempoTimeAt(tempo, k * grid);
        n.tickTimes.push_back(static_cast<int32_t>(std::lround(t)));
    }
}

inline bool loadScore(const std::string& filename, std::string& bgmName, std::vector<Note>& notes, std::vector<SpeedEvent>& speedEvents, double bpm, double& offsetMs, int difficultyIndex){
    std::ifstream file(toUtf8Path(filename));
    if(!file.is_open()){
        printf("譜面ファイル開かん!!\n");
        return false;
    }

    notes.clear();
    speedEvents.clear();

    //try catchエラー処理
    json j;
    try{
        file >> j;
    }
    catch(const json::parse_error& e){
        printf("JSONパース失敗\n");
        return false;
    }

    try{
        bgmName = j.at("bgm").get<std::string>();
    }
    catch(const json::exception& e){
        printf("JSON: / bgm / error : %s\n", e.what());
        return false;
    }

    (void)j.value("bpm", bpm);
    offsetMs = j.value("offset", 0.0);

    const json* chartRoot = &j;
    if(j.contains("difficulties") && j.at("difficulties").is_array()){
        const auto& diffs = j.at("difficulties");
        if(difficultyIndex < 0 || static_cast<size_t>(difficultyIndex) >= diffs.size()){
            printf("JSON: 難易度が範囲外(%d)\n", difficultyIndex);
            return false;
        }
        chartRoot = &diffs[difficultyIndex];
    }

    (void)chartRoot->value("bpm", bpm);

    // 中継点の計算用に、この難易度の BPM と BPM の変化を読む
    double chartBpm = j.value("bpm", 120.0);
    chartBpm = chartRoot->value("bpm", chartBpm);
    std::vector<TempoPoint> bpmPoints;
    if(chartRoot->contains("bpmEvents") && chartRoot->at("bpmEvents").is_array()){
        for(const auto& b : chartRoot->at("bpmEvents")){
            bpmPoints.push_back({b.value("time", 0.0), b.value("bpm", chartBpm)});
        }
    }
    // 並べ替えと、BPM が 0 以下のイベントの除外は makeTempoMap がやってくれる
    const TempoMap tempo = makeTempoMap(chartBpm, bpmPoints);

    if(!chartRoot->contains("notes") || !chartRoot->at("notes").is_array()){
        printf("JSON / notes error\n");
        return false;
    }

    for(const auto& item : chartRoot->at("notes")){
        try{
            int noteType = item.at("type").get<int>();
            int32_t time = item.at("time").get<int32_t>();
            int lane = item.at("lane").get<int>();

            Note newNote;
            newNote.lane = lane;
            newNote.targetTime = time;
            newNote.widthLanes = item.value("width", 1);

            if(noteType == 3)   newNote.type = NoteType::Drag;
            else if(noteType == 4)  newNote.type = NoteType::Lane;
            else if(noteType == 5)  newNote.type = NoteType::Normal_c;
            else if(noteType == 6) newNote.isCyan = true;
            else newNote.type = NoteType::Normal;

            if(item.contains("duration")){
                newNote.headType = newNote.type;
                newNote.type = NoteType::Long;
                newNote.durationMs = item.at("duration").get<int32_t>();
            }

            if(item.contains("speed")){
                newNote.hasCustomSpeed = true;
                newNote.customSpeed = item.at("speed").get<double>();
            }

            if(item.contains("path")){
                for(const auto& kf : item.at("path")){
                    PathKeyframe k;
                    k.time = kf.at("time").get<int32_t>();
                    k.y = kf.value("y", 1);
                    k.easing = kf.value("easing", 1);
                    newNote.path.push_back(k);
                }

                std::sort(newNote.path.begin(), newNote.path.end(), [](const PathKeyframe& a, const PathKeyframe& b){
                    return a.time < b.time;
                });
            }

            buildLongTicks(newNote, tempo);   // ロングなら中継点を置く

            notes.push_back(newNote);
        }
        catch(const json::exception& e){
            printf("JSON: / notes内の項目不正 : %s\n", e.what());
            return false;
        }
    }

    std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b){
        return a.targetTime < b.targetTime;
    });

    //speedEventの読込
    if(chartRoot->contains("speedEvents")){
        if(!chartRoot->at("speedEvents").is_array()){
            printf("JSON / speedEvent / error\n");
            return false;
        }

        for(const auto& item : chartRoot->at("speedEvents")){
            try{
                SpeedEvent ev;
                ev.triggerTime = item.at("time").get<int32_t>();
                ev.targetspeed = item.at("target").get<double>();
                ev.easing = item.at("easing").get<int>();
                ev.duration = item.value("duration", 500);

                speedEvents.push_back(ev);
            }
            catch(const json::exception& e){
                printf("JSON / speedEvents / error : %s\n", e.what());
                return false;
            }
        }

        std::sort(speedEvents.begin(), speedEvents.end(), [](const SpeedEvent& a, const SpeedEvent& b){
                return a.triggerTime < b.triggerTime;
            });
    }
    return true;
}