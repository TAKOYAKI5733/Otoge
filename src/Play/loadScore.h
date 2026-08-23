#pragma once

#include "GameCommon.h"
#include "type.h"

inline bool loadScore(const std::string& filename, std::string& bgmName, std::vector<Note>& notes, std::vector<SpeedEvent>& speedEvents, double bpm){
    std::ifstream file(filename);
    if(!file.is_open()){
        printf("譜面ファイルが開かん!\n");
        return false;
    }

    speedEvents.clear();
    notes.clear();

    std::string line;
    double beatDurationMs = 500;
    double accumulatedTimeMs = 1000;    //仮設定
    bool isDataSection = false;

    while(std::getline(file, line)){
        if(line.empty() || line.rfind("//", 0) == 0) continue;

        if(!isDataSection){
            if(line.rfind("BGM:", 0) == 0){
                bgmName = line.substr(4);
            }
            else if(line.rfind("BPM:", 0) == 0){
                bpm = std::stod(line.substr(4));
                beatDurationMs = 60000.0 / bpm;
            }
            else if(line.rfind("ACC:", 0) == 0){
                accumulatedTimeMs = std::stod(line.substr(4));
            }
            else if(line == "#START"){
                isDataSection = true;
            }
        }
        else{
            if(line == "#END") break;

            if(line.rfind("#BPM:", 0) == 0){
                double newBpm = std::stod(line.substr(5));
                beatDurationMs = 60000.0 / newBpm;
                continue;
            }

            if(line.rfind("#SPEED:", 0) == 0){
                std::string data = line.substr(7);
                std::stringstream parseSs(data);
                std::string speedSs, easeStr, durationStr;

                if(std::getline(parseSs, speedSs, ',') && std::getline(parseSs, easeStr, ',')){
                    SpeedEvent ev;
                    ev.triggerTime = static_cast<int32_t>(accumulatedTimeMs);
                    ev.targetspeed = std::stod(speedSs);
                    ev.easing = std::stoi(easeStr);

                    if(std::getline(parseSs, durationStr)){
                        int32_t input_duration = std::stoi(durationStr);
                        ev.duration = input_duration * beatDurationMs;
                    }
                    else{
                        ev.duration = 500;
                    }

                    speedEvents.push_back(ev);
                }
                continue;
            }

            std::stringstream ss(line);
            std::string noteTypeStr, spacingStr, laneStr, lengthStr;

            // 1. ノーツの種類、2. ノーツの間隔、3. レーン を順番にカンマ区切りで取得
            if(std::getline(ss, noteTypeStr, ',') && 
               std::getline(ss, spacingStr, ',') && 
               std::getline(ss, laneStr, ',')){
                
                int noteType = std::stoi(noteTypeStr);
                int spacingType = std::stoi(spacingStr); // 四分音符なら「1」や「4」など
                int lane = std::stoi(laneStr);

                // (例: 1=4分音符分の時間, 2=8分音符分の時間 としたい場合は分母分子を調整してください)
                // ここでは従来の計算式をベースに spacingType を分母にしています
                double noteLengthMs = (4.0 / spacingType) * beatDurationMs;

                Note newNote;
                newNote.lane = lane;
                if(noteType == 3){
                    newNote.type = NoteType::Drag;
                }
                else if(noteType == 4){
                    newNote.type = NoteType::Lane;
                }
                newNote.targetTime = static_cast<int32_t>(accumulatedTimeMs);

                // 4. もし4つ目の値（ロングノーツの拍）があれば取得
                if(std::getline(ss, lengthStr, ',')){
                    double beats = std::stod(lengthStr);
                    newNote.type = NoteType::Long;
                    newNote.durationMs = static_cast<int32_t>(beats * beatDurationMs);
                }

                notes.push_back(newNote);

                // 次のノーツの基準時間を進める
                accumulatedTimeMs += noteLengthMs;
            }
        }
    }
    return true;
}