#pragma once

#include "GameCommon.h"

// =====================================================================
//  表示モードの切り替え
//  PlayerSettings::windowMode に入る値
//    0 = ウィンドウ
//    1 = ボーダーレス（デスクトップと同じ解像度・Hzのまま画面いっぱい）
//    2 = 排他フルスクリーン（ゲームがモニターの表示モードを直接選ぶ）
// =====================================================================
enum WindowModeId : int{
    WINDOW_MODE_WINDOWED   = 0,
    WINDOW_MODE_BORDERLESS = 1,
    WINDOW_MODE_EXCLUSIVE  = 2
};

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