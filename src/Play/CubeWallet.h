#pragma once

// =====================================================================
//  CubeWallet.h — ゲーム内の通貨「Cube」の保存と読み込み
//
//  ・cubes.json（settings.json と同じフォルダ）に {"cubes": 数} で保存する
//  ・プレイでノーツを取ると立方体が出て、テレポートし終わるたびに 1 増える（renderGame.h の erase_some で数える）
//  ・プレイが終わったとき（リザルト・リトライ・リタイア・ゲームの終了のどれでも）に、集めた分を足して保存する
//    オートプレイ・譜面エディタのテストプレイでは保存しない（表示は増えるが、持ち物には入らない）
// =====================================================================

#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>
#include <algorithm>

inline constexpr const char* CUBE_WALLET_PATH = "cubes.json";

// 持っている Cube の数（ファイルが無い・読めないときは 0）
inline long long loadCubeWallet(){
    try{
        std::ifstream f(CUBE_WALLET_PATH);
        if(!f.is_open()) return 0;
        nlohmann::json j;
        f >> j;
        return std::max<long long>(0, j.value("cubes", 0LL));
    }
    catch(const std::exception& e){
        std::printf("cubes.json が読めません: %s\n", e.what());
        return 0;
    }
}

inline void saveCubeWallet(long long cubes){
    std::ofstream f(CUBE_WALLET_PATH);
    if(!f.is_open()){
        std::printf("cubes.json に保存できません\n");
        return;
    }
    nlohmann::json j;
    j["cubes"] = cubes;
    f << j.dump(2);
}