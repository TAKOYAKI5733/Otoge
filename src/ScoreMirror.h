#pragma once

// =====================================================================
//  ScoreMirror.h — 譜面を保存したとき、ほかの scores フォルダにも同じ内容を書く
//
//  ■ なぜ必要か
//    ゲームは bin_w_static/（や bin_w/・bin/）の中で起動するので、譜面エディタの保存先 "scores/..." は
//    bin_w_static/scores/... になる。ところが make のたびに、プロジェクトの scores/ が bin_w_static/ へ
//    コピーし直される（Makefile.windows の copy_assets）ので、エディタで作った・直した譜面が古いもので上書きされたり、
//    ビルド用の scores/ に入っていなかったりする。
//
//  ■ どうするか
//    保存した場所が「…/scores/<ジャンル>/<名前>.json」のように scores フォルダの中なら、
//    scores から下の部分（<ジャンル>/<名前>.json）を、次の scores フォルダにも同じ内容で書く：
//      ・exe の 1 つ上のフォルダの scores/（＝プロジェクトの scores/。bin_w_static/ で起動したとき）
//      ・exe と同じフォルダの scores/（＝bin_w_static/scores/。プロジェクトのフォルダで起動したとき）
//      ・今いるフォルダの 1 つ上の scores/
//    ・その scores フォルダがあるときだけ書く（無ければ何もしない。勝手に scores フォルダは作らない）
//    ・保存したのと同じフォルダ（同じ scores）には書かない
//    ・ジャンルのフォルダが無ければ作る（新しいジャンルで保存したとき）
//    ・scores の外（テストプレイ用の editor_testplay.json など）は対象外
// =====================================================================

#include "GameCommon.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// 書いた先（保存した場所は含まない）を返す
inline std::vector<std::filesystem::path> mirrorScoreToSource(const std::filesystem::path& savedPath, const std::string& content){
    namespace fs = std::filesystem;
    std::vector<fs::path> written;
    std::error_code ec;

    // 1. 保存した場所を、scores フォルダとその下の部分に分ける
    const fs::path abs = fs::absolute(savedPath, ec).lexically_normal();
    if(ec) return written;
    std::vector<fs::path> parts(abs.begin(), abs.end());
    int scoresIdx = -1;
    for(int i = static_cast<int>(parts.size()) - 2; i >= 0; i--){   // 一番深い "scores"（ファイル名そのものは除く）
        if(parts[i] == "scores"){ scoresIdx = i; break; }
    }
    if(scoresIdx < 0) return written;   // scores フォルダの外 → 対象外
    fs::path scoresRoot, rel;
    for(int i = 0; i <= scoresIdx; i++) scoresRoot /= parts[i];
    for(size_t i = scoresIdx + 1; i < parts.size(); i++) rel /= parts[i];

    // 2. 書く候補の scores フォルダ
    std::vector<fs::path> roots;
    if(char* base = SDL_GetBasePath()){
        const fs::path exeDir = toUtf8Path(base).lexically_normal();
        SDL_free(base);
        fs::path dir = exeDir;
        if(!dir.empty() && !dir.has_filename()) dir = dir.parent_path();   // 末尾の区切り文字を取る
        roots.push_back(dir.parent_path() / "scores");   // exe の 1 つ上（プロジェクト）
        roots.push_back(dir / "scores");                 // exe と同じフォルダ
    }
    roots.push_back(fs::current_path(ec).parent_path() / "scores");   // 今いるフォルダの 1 つ上

    // 3. あるフォルダにだけ、同じ内容を書く（同じフォルダ・重複は飛ばす）
    std::vector<fs::path> done;
    for(const fs::path& root : roots){
        if(!fs::is_directory(root, ec)) continue;                      // その scores フォルダが無い
        if(fs::equivalent(root, scoresRoot, ec)) continue;             // 保存したのと同じ scores
        bool dup = false;
        for(const fs::path& d : done) if(fs::equivalent(root, d, ec)) dup = true;
        if(dup) continue;
        done.push_back(root);

        const fs::path target = root / rel;
        fs::create_directories(target.parent_path(), ec);             // ジャンルのフォルダが無ければ作る
        std::ofstream out(target, std::ios::binary);
        if(out.is_open()){
            out << content;
            written.push_back(target);
            printf("[譜面エディタ] ビルド用の scores にも保存 : %s\n", reinterpret_cast<const char*>(target.u8string().c_str()));
        }
        else{
            printf("[譜面エディタ] ビルド用の scores に保存できませんでした : %s\n", reinterpret_cast<const char*>(target.u8string().c_str()));
        }
    }
    return written;
}