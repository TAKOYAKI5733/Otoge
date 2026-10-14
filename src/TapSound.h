#pragma once

#include "GameCommon.h"
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cctype>

// =====================================================================
//  ノーツ音（打鍵音）の選択肢
//    設定には「sounds/tap/ の中のファイル名」を保存する。空文字 = 標準の音
// =====================================================================

inline constexpr const char* TAP_SOUND_DEFAULT_PATH = "sounds/tapsound_2.wav";   // 標準の音
inline constexpr const char* TAP_SOUND_DIR          = "sounds/tap";              // 追加の音を置くフォルダ

// 設定に保存した名前 → 実際のファイルパス
inline std::string tapSoundPath(const std::string& name){
    if(name.empty()) return TAP_SOUND_DEFAULT_PATH;
    return std::string(TAP_SOUND_DIR) + "/" + name;
}

// 選べるノーツ音の一覧（先頭は必ず標準 = 空文字、残りはファイル名順）
inline std::vector<std::string> listTapSounds(){
    std::vector<std::string> names = {""};

    namespace fs = std::filesystem;
    std::error_code ec;
    if(!fs::is_directory(TAP_SOUND_DIR, ec)) return names;   // フォルダが無ければ標準だけ

    std::vector<std::string> found;
    for(const auto& entry : fs::directory_iterator(TAP_SOUND_DIR, ec)){
        if(!entry.is_regular_file(ec)) continue;

        std::string ext = entry.path().extension().string();
        for(char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if(ext != ".wav" && ext != ".ogg" && ext != ".mp3") continue;

        // 日本語のファイル名でも壊れないよう UTF-8 で取り出す
        std::u8string u8 = entry.path().filename().u8string();
        found.emplace_back(reinterpret_cast<const char*>(u8.data()), u8.size());
    }
    std::sort(found.begin(), found.end());
    names.insert(names.end(), found.begin(), found.end());
    return names;
}

// 表示用の名前（拡張子を外す。標準は「標準」）
inline std::string tapSoundDisplayName(const std::string& name){
    if(name.empty()) return "標準";
    size_t dot = name.find_last_of('.');
    return (dot == std::string::npos) ? name : name.substr(0, dot);
}

// 音を読み込む。読めなければ標準の音に戻す（それも無理なら nullptr）
inline Mix_Chunk* loadTapSound(const std::string& name){
    Mix_Chunk* chunk = Mix_LoadWAV(tapSoundPath(name).c_str());
    if(!chunk && !name.empty()){
        std::cout << "[警告]ノーツ音の読込失敗、標準に戻します: " << name << "\n";
        chunk = Mix_LoadWAV(TAP_SOUND_DEFAULT_PATH);
    }
    return chunk;
}