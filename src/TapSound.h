#pragma once

#include "GameCommon.h"
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cmath>

// =====================================================================
//  ノーツ音（打鍵音）の選択肢
//    設定には「sounds/tap/ の中のファイル名」を保存する。空文字 = 標準の音、TAP_SOUND_OFF = 鳴らさない
// =====================================================================

inline constexpr const char* TAP_SOUND_DEFAULT_PATH = "sounds/tapsound_2.wav";   // 標準の音
inline constexpr const char* TAP_SOUND_DIR          = "sounds/tap";              // 追加の音を置くフォルダ
inline constexpr const char* TAP_SOUND_OFF          = "*off*";                   // 「なし」を表す名前（ファイル名には使えない * を含めて、本物のファイルと重ならないようにする）

// 音声オフセットがこの値(ms)以上 0 からずれていて、ノーツ音を鳴らしているときは「ノーツ音を消した方がよい」と警告する
//   ノーツ音はキーを押した瞬間に鳴るので、オフセットで判定をずらしている分、曲のリズムとずれて聞こえるため
inline constexpr double TAP_SOUND_OFFSET_WARN_MS = 20.0;

inline bool isTapSoundOff(const std::string& name){ return name == TAP_SOUND_OFF; }

// 警告を出すべきか：音声オフセットが ±TAP_SOUND_OFFSET_WARN_MS 以上 かつ ノーツ音を鳴らしている
inline bool shouldWarnTapSoundOffset(const PlayerSettings& s){
    return std::fabs(s.offsetMs) >= TAP_SOUND_OFFSET_WARN_MS && !isTapSoundOff(s.tapSound);
}

// 警告の文（設定ウィンドウ・ノーツの詳細設定で同じ文を出す）。ウィンドウの幅に収まるよう2行に分けてある
inline constexpr const char* TAP_SOUND_OFFSET_WARN_LINE1 = "【注意】音声オフセットが ±20ms 以上です";
inline constexpr const char* TAP_SOUND_OFFSET_WARN_LINE2 = "ノーツ音がずれて聞こえるので「なし」がおすすめ";

// 設定に保存した名前 → 実際のファイルパス
inline std::string tapSoundPath(const std::string& name){
    if(name.empty()) return TAP_SOUND_DEFAULT_PATH;
    return std::string(TAP_SOUND_DIR) + "/" + name;
}

// 選べるノーツ音の一覧（先頭は標準 = 空文字、2番目は「なし」、残りはファイル名順）
inline std::vector<std::string> listTapSounds(){
    std::vector<std::string> names = {"", TAP_SOUND_OFF};

    namespace fs = std::filesystem;
    std::error_code ec;
    if(!fs::is_directory(TAP_SOUND_DIR, ec)) return names;   // フォルダが無ければ標準と「なし」だけ

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

// 表示用の名前（拡張子を外す。標準は「標準」、鳴らさないは「なし」）
inline std::string tapSoundDisplayName(const std::string& name){
    if(name.empty()) return "標準";
    if(isTapSoundOff(name)) return "なし";
    size_t dot = name.find_last_of('.');
    return (dot == std::string::npos) ? name : name.substr(0, dot);
}

// 音を読み込む。読めなければ標準の音に戻す（それも無理なら nullptr）
//   「なし」のときは nullptr を返す。鳴らす側（noteJudge.h の playHitSound など）は nullptr なら鳴らさない
inline Mix_Chunk* loadTapSound(const std::string& name){
    if(isTapSoundOff(name)) return nullptr;
    Mix_Chunk* chunk = Mix_LoadWAV(tapSoundPath(name).c_str());
    if(!chunk && !name.empty()){
        std::cout << "[警告]ノーツ音の読込失敗、標準に戻します: " << name << "\n";
        chunk = Mix_LoadWAV(TAP_SOUND_DEFAULT_PATH);
    }
    return chunk;
}