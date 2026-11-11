#pragma once

#include "GameCommon.h"

// =====================================================================
//  表示モードの切り替え
//  PlayerSettings::windowMode に入る値
//    0 = ウィンドウ
//    1 = ボーダーレス（デスクトップと同じ解像度・Hzのまま画面いっぱい）
//    2 = 排他フルスクリーン（ゲームがモニターの表示モードを直接選ぶ）
//
//  ■ 配信モード（PlayerSettings::streamMode、g_streamMode）
//    OBS などで録画・配信するときのためのモード。ON にすると:
//      ① 「フルスクリーン」を選んでいても、排他フルスクリーンではなくボーダーレスで画面いっぱいにする
//         排他フルスクリーンは、ゲームが画面を独り占めするので、OBS の「ウィンドウキャプチャ」では
//         真っ黒になったり、OBS を操作した瞬間にゲームが最小化されたりする。
//         ボーダーレスは「画面と同じ大きさのふつうのウィンドウ」なので、どのキャプチャ方法でも映る
//      ② ほかのウィンドウ（OBS など）をクリックしても、ゲームを最小化しない
//      ③ プレイ中、ほかのウィンドウをクリックしても自動でポーズしない（PlayScene.cpp）
//      ④ 描き方（レンダラー）を OpenGL から Direct3D 11 に変える（main.cpp。起動し直したときに反映）
//         画面いっぱいの OpenGL のウィンドウは、Windows（とグラフィックドライバ）が「フルスクリーンのゲーム」と見なして、
//         デスクトップの合成（DWM）を通さずに直接モニターへ出すことがある。そうなると、OBS の「画面キャプチャ」には
//         ゲームの画面が届かず、最初の1枚のまま止まって映る。Direct3D 11 は必ずデスクトップの合成を通るので、
//         画面キャプチャ・ウィンドウキャプチャ・ゲームキャプチャのどれでも、実際の画面どおりに映る
//    見た目はどちらも画面いっぱいなので、プレイする人から見た違いはほとんど無い
// =====================================================================
enum WindowModeId : int{
    WINDOW_MODE_WINDOWED   = 0,
    WINDOW_MODE_BORDERLESS = 1,
    WINDOW_MODE_EXCLUSIVE  = 2
};

// 配信モードが ON か（loadPlayerSettings と設定ウィンドウが書き換える。applyWindowMode が見る）
//   applyWindowMode を呼んでいる所（main.cpp など）を書き換えなくても、配信モードが反映されるようにするため
inline bool g_streamMode = false;

// ---------------------------------------------------------------
// 排他フルスクリーンで使う表示モードを探す
// 「デスクトップと同じ解像度」の中で「一番高いリフレッシュレート」を選ぶ
// ---------------------------------------------------------------
inline bool findBestExclusiveMode(SDL_Window* window, SDL_DisplayMode& out){
    int display = SDL_GetWindowDisplayIndex(window);   // ウィンドウが今いるモニター
    if(display < 0) display = 0;

    SDL_DisplayMode desktop;
    if(SDL_GetDesktopDisplayMode(display, &desktop) != 0){
        printf("[Display] デスクトップ情報の取得失敗: %s\n", SDL_GetError());
        return false;
    }

    bool found = false;
    int count = SDL_GetNumDisplayModes(display);
    for(int i = 0; i < count; i++){
        SDL_DisplayMode m;
        if(SDL_GetDisplayMode(display, i, &m) != 0) continue;

        // 解像度を変えるとモニター側の拡大でぼやけるので、ネイティブ解像度に限定する
        if(m.w != desktop.w || m.h != desktop.h) continue;

        if(!found || m.refresh_rate > out.refresh_rate){
            out = m;
            found = true;
        }
    }

    // 一覧が取れない環境では、デスクトップと同じモードで妥協する
    if(!found) out = desktop;
    return true;
}

// ---------------------------------------------------------------
// 表示モードを適用する（起動時と、設定画面でSAVEしたときに呼ぶ）
// ---------------------------------------------------------------
inline bool applyWindowMode(SDL_Window* window, int mode){
    // ② 配信モード：ほかのウィンドウをクリックしても最小化しない（"0"）
    //    配信モードでないときは SDL の標準（"auto" = 排他フルスクリーンのときだけ最小化）に戻す
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, g_streamMode ? "0" : "auto");

    // ① 配信モード：排他フルスクリーンの代わりにボーダーレスにする
    if(g_streamMode && mode == WINDOW_MODE_EXCLUSIVE){
        printf("[Display] 配信モードなので、フルスクリーンをボーダーレスで表示します\n");
        mode = WINDOW_MODE_BORDERLESS;
    }

    if(mode == WINDOW_MODE_EXCLUSIVE){
        SDL_DisplayMode target;
        if(!findBestExclusiveMode(window, target)) return false;

        // 先に「どのモードで全画面にするか」を登録してから、全画面にする
        if(SDL_SetWindowDisplayMode(window, &target) != 0){
            printf("[Display] 表示モード設定失敗: %s\n", SDL_GetError());
            return false;
        }
        if(SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN) != 0){
            printf("[Display] 排他フルスクリーン失敗: %s\n", SDL_GetError());
            return false;
        }

        // 実際に採用されたモードを確認（ドライバが近いモードに置き換えることがある）
        SDL_DisplayMode actual;
        if(SDL_GetWindowDisplayMode(window, &actual) == 0){
            printf("[Display] 排他フルスクリーン: %dx%d @ %dHz\n", actual.w, actual.h, actual.refresh_rate);
        }
        return true;
    }

    if(mode == WINDOW_MODE_BORDERLESS){
        if(SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0){
            printf("[Display] ボーダーレス失敗: %s\n", SDL_GetError());
            return false;
        }
        return true;
    }

    // ウィンドウ: 全画面を解除すると、SDLが全画面前のサイズ・位置に戻してくれる
    if(SDL_SetWindowFullscreen(window, 0) != 0){
        printf("[Display] ウィンドウ化失敗: %s\n", SDL_GetError());
        return false;
    }
    return true;
}