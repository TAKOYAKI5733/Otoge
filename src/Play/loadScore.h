#pragma once

#include "GameCommon.h"
#include "type.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct DifficultyInfo{
    std::string name;
    std::string level;
};

inline std::vector<DifficultyInfo> listDifficulties(const std::string& filename){
    std::vector<DifficultyInfo> result;

    std::ifstream file(filename);
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

inline bool loadScore(const std::string& filename, std::string& bgmName, std::vector<Note>& notes, std::vector<SpeedEvent>& speedEvents, double bpm, double& offsetMs, int difficultyIndex){
    std::ifstream file(filename);
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
    if(j.contains("difficulties") && j.at("difficulities").is_array()){
        const auto& diffs = j.at("difficulties");
        if(difficultyIndex < 0 || static_cast<size_t>(difficultyIndex) >= diffs.size()){
            printf("JSON: 難易度が範囲外(%d)\n", difficultyIndex);
            return false;
        }
        chartRoot = &diffs[difficultyIndex];
    }

    (void)chartRoot->value("bpm", bpm);

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
            else newNote.type = NoteType::Normal;

            if(item.contains("duration")){
                newNote.type = NoteType::Long;
                newNote.durationMs = item.at("duration").get<int32_t>();
            }

            if(item.contains("speed")){
                newNote.hasCustomSpeed = true;
                newNote.customSpeed = item.at("speed").get<double>();
            }

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
    if(j.contains("speedEvents")){
        if(!j.at("speedEvents").is_array()){
            printf("JSON / speedEvent / error\n");
            return false;
        }

        for(const auto& item : j.at("speedEvents")){
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