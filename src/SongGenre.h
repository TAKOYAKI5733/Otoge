#pragma once

// =====================================================================
//  SongGenre.h — 楽曲のジャンル（属性）  src/SongGenre.h
//
//  ■ 譜面ファイル（.json）の一番外側に、ジャンルの名前の並びとして入れる
//      "genres": ["Pops", "Vocaloid"]
//    1 曲に何個でも付けられる（付けなくてもよい）。順番は下の SONG_GENRES の順にそろえて保存する
//
//  ■ ジャンルを増やすとき
//    SONG_GENRES に 1 行足すだけ（エディタの選ぶ箱も、そのぶん増える）。
//    名前（name）は譜面ファイルに書く文字なので、一度使い始めたら変えない
//    （変えると、前に付けた譜面のジャンルが読めなくなる）
//
//  ■ これから
//    選曲画面でジャンルごとに並べる・絞るときは、この一覧と、譜面の "genres" を使う
// =====================================================================

#include <SDL2/SDL.h>
#include <algorithm>
#include <string>
#include <vector>

struct SongGenreInfo{
    const char* name;     // 譜面ファイルに書く名前
    SDL_Color   color;    // エディタ（とこれからの選曲画面）で使う色
};

inline constexpr SongGenreInfo SONG_GENRES[] = {
    {"Pops",      {255, 120, 180, 255}},
    {"Vocaloid",  { 60, 220, 200, 255}},
    {"Inst.",     {255, 180,  70, 255}},
    {"GameMusic", {120, 220, 100, 255}},
    {"Variety",   {170, 130, 255, 255}},
};
inline constexpr int SONG_GENRE_COUNT = static_cast<int>(sizeof(SONG_GENRES) / sizeof(SONG_GENRES[0]));

// 名前 → 一覧の番号（知らない名前なら -1）
inline int songGenreIndex(const std::string& name){
    for(int i = 0; i < SONG_GENRE_COUNT; i++){
        if(name == SONG_GENRES[i].name) return i;
    }
    return -1;
}

// 並びを、一覧の順にそろえて、重なりを消す（知らない名前は後ろにそのまま残す：消さない）
inline std::vector<std::string> normalizeSongGenres(const std::vector<std::string>& in){
    std::vector<std::string> known, unknown;
    for(int i = 0; i < SONG_GENRE_COUNT; i++){
        if(std::find(in.begin(), in.end(), SONG_GENRES[i].name) != in.end()) known.push_back(SONG_GENRES[i].name);
    }
    for(const auto& g : in){
        if(songGenreIndex(g) < 0 && std::find(unknown.begin(), unknown.end(), g) == unknown.end()) unknown.push_back(g);
    }
    known.insert(known.end(), unknown.begin(), unknown.end());
    return known;
}