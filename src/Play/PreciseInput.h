#pragma once

// =====================================================================
//  PreciseInput.h — キーを押した瞬間の時刻を、フレームに関係なく正確に記録する
//
//  ■ なぜ必要か
//    今までは、フレームの最初にキーの状態を1回だけ見て、そのフレームの時刻で判定していた。
//    すると、フレームとフレームの間のいつ押しても「次のフレームの時刻」に押したことになる
//    （60fps なら最大 16.7ms、平均 8ms 遅く判定される）。
//    SDL2 のキーのイベントの時刻も「イベントを取り出した瞬間（＝フレームの最初）」に付くので、同じことになる。
//
//  ■ どうするか（Windows）
//    ゲームの絵を描く処理とは別のスレッドで、約 1ms ごとにキーボードの状態を直接見る（GetAsyncKeyState）。
//    押された瞬間を見つけたら、その時刻を高精度タイマー（SDL_GetPerformanceCounter = 描画側の時計と同じもの）で記録する。
//    描画側は毎フレーム takePresses() で受け取り、曲の時刻に直して判定に使う（noteJudge.h）。
//    → fps に関係なく、押した時刻のずれが 1〜2ms になる
//
//  ■ 使えないもの
//    ・コントローラーのボタン・十字キー・軸：SDL のコントローラーは描画側のスレッドでしか読めないので、今まで通りフレームごと
//    ・Linux などの Windows 以外：今まで通りフレームごと
//    lanePrecise(i) が false のレーンは、今まで通りの判定になる
// =====================================================================

#include "GameCommon.h"
#include "InputBinding.h"
#include <atomic>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

class PreciseInput{
public:
    PreciseInput() = default;
    ~PreciseInput(){ stop(); }
    PreciseInput(const PreciseInput&) = delete;
    PreciseInput& operator=(const PreciseInput&) = delete;

    // レーンのキー設定を受け取って、見張りのスレッドを始める（キーボードのキーが1つも無ければ始めない）
    void start(const LaneBindings& bindings){
        stop();
        bool any = false;
        for(int i = 0; i < LANE_COUNT; i++){
            vk_[i] = toVirtualKey(bindings[i]);
            pending_[i] = 0;
            if(vk_[i] != 0) any = true;
        }
#if defined(_WIN32)
        if(any){
            running_ = true;
            thread_ = std::thread(&PreciseInput::run, this);
        }
#else
        (void)any;
#endif
    }

    void stop(){
        running_ = false;
        if(thread_.joinable()) thread_.join();
    }

    // レーン i を、このスレッドで正確に見ているか（false なら今まで通りフレームごとの判定）
    bool lanePrecise(int i) const{
#if defined(_WIN32)
        return thread_.joinable() && vk_[i] != 0;
#else
        (void)i;
        return false;
#endif
    }

    // 前に呼んでから今までに押された瞬間（レーンごとに一番最初の1回）を受け取る。
    //   out[i] = 押した瞬間の SDL_GetPerformanceCounter() の値。押されていなければ 0
    void takePresses(Uint64 out[LANE_COUNT]){
        std::lock_guard<std::mutex> lock(mutex_);
        for(int i = 0; i < LANE_COUNT; i++){
            out[i] = pending_[i];
            pending_[i] = 0;
        }
    }

private:
    int vk_[LANE_COUNT] = {};            // レーンごとの Windows の仮想キーコード（0 = このスレッドでは見ない）
    Uint64 pending_[LANE_COUNT] = {};    // まだ描画側が受け取っていない「押した瞬間」（0 = なし）
    std::mutex mutex_;
    std::atomic<bool> running_{false};
    std::thread thread_;

#if defined(_WIN32)
    // 見張り：約 1ms ごとに、レーンのキーが「離れている → 押されている」に変わったかを見る
    void run(){
        // 描画より優先して動くようにする（判定の時刻がぶれないように）
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

        bool prev[LANE_COUNT] = {};
        for(int i = 0; i < LANE_COUNT; i++){
            if(vk_[i] != 0) prev[i] = (GetAsyncKeyState(vk_[i]) & 0x8000) != 0;   // 始めた時点で押しっぱなしのキーは数えない
        }

        while(running_){
            for(int i = 0; i < LANE_COUNT; i++){
                if(vk_[i] == 0) continue;
                const bool down = (GetAsyncKeyState(vk_[i]) & 0x8000) != 0;
                if(down && !prev[i]){
                    const Uint64 t = SDL_GetPerformanceCounter();   // 押した瞬間の時刻
                    std::lock_guard<std::mutex> lock(mutex_);
                    if(pending_[i] == 0) pending_[i] = t;          // 1フレームの間に何度も押されたら、最初の1回を使う
                }
                prev[i] = down;
            }
            // main.cpp の timeBeginPeriod(1) により、Sleep(1) は約 1ms で戻ってくる
            Sleep(1);
        }
    }
#endif

    // キー設定（物理的な位置 = SDL のスキャンコード）→ Windows の仮想キーコード
    //   キーボードの配列（日本語・英語など）によって、同じ位置でも仮想キーコードが変わるので、
    //   「位置のコード（スキャンコード セット1）」から MapVirtualKey で今の配列の仮想キーコードに直す
    static int toVirtualKey(const InputBinding& b){
#if defined(_WIN32)
        if(b.type != InputBinding::Type::Key) return 0;   // コントローラーはこのスレッドでは見ない
        const SDL_Scancode sc = static_cast<SDL_Scancode>(b.code);

        // 位置で決まっていて、配列に関係ないキー
        switch(sc){
            case SDL_SCANCODE_UP:       return VK_UP;
            case SDL_SCANCODE_DOWN:     return VK_DOWN;
            case SDL_SCANCODE_LEFT:     return VK_LEFT;
            case SDL_SCANCODE_RIGHT:    return VK_RIGHT;
            case SDL_SCANCODE_INSERT:   return VK_INSERT;
            case SDL_SCANCODE_DELETE:   return VK_DELETE;
            case SDL_SCANCODE_HOME:     return VK_HOME;
            case SDL_SCANCODE_END:      return VK_END;
            case SDL_SCANCODE_PAGEUP:   return VK_PRIOR;
            case SDL_SCANCODE_PAGEDOWN: return VK_NEXT;
            case SDL_SCANCODE_LSHIFT:   return VK_LSHIFT;
            case SDL_SCANCODE_RSHIFT:   return VK_RSHIFT;
            case SDL_SCANCODE_LCTRL:    return VK_LCONTROL;
            case SDL_SCANCODE_RCTRL:    return VK_RCONTROL;
            case SDL_SCANCODE_LALT:     return VK_LMENU;
            case SDL_SCANCODE_RALT:     return VK_RMENU;
            case SDL_SCANCODE_SPACE:    return VK_SPACE;
            case SDL_SCANCODE_RETURN:   return VK_RETURN;
            case SDL_SCANCODE_KP_ENTER: return VK_RETURN;
            case SDL_SCANCODE_TAB:      return VK_TAB;
            case SDL_SCANCODE_BACKSPACE:return VK_BACK;
            case SDL_SCANCODE_KP_0:     return VK_NUMPAD0;
            case SDL_SCANCODE_KP_1:     return VK_NUMPAD1;
            case SDL_SCANCODE_KP_2:     return VK_NUMPAD2;
            case SDL_SCANCODE_KP_3:     return VK_NUMPAD3;
            case SDL_SCANCODE_KP_4:     return VK_NUMPAD4;
            case SDL_SCANCODE_KP_5:     return VK_NUMPAD5;
            case SDL_SCANCODE_KP_6:     return VK_NUMPAD6;
            case SDL_SCANCODE_KP_7:     return VK_NUMPAD7;
            case SDL_SCANCODE_KP_8:     return VK_NUMPAD8;
            case SDL_SCANCODE_KP_9:     return VK_NUMPAD9;
            case SDL_SCANCODE_KP_PERIOD:   return VK_DECIMAL;
            case SDL_SCANCODE_KP_PLUS:     return VK_ADD;
            case SDL_SCANCODE_KP_MINUS:    return VK_SUBTRACT;
            case SDL_SCANCODE_KP_MULTIPLY: return VK_MULTIPLY;
            case SDL_SCANCODE_KP_DIVIDE:   return VK_DIVIDE;
            default: break;
        }

        // 文字・数字・記号のキー：位置のコード（セット1）を、今の配列の仮想キーコードに直す
        const unsigned set1 = set1ScanCode(sc);
        if(set1 == 0) return 0;   // 知らないキー → このレーンは今まで通り
        return static_cast<int>(MapVirtualKeyW(set1, MAPVK_VSC_TO_VK_EX));
#else
        (void)b;
        return 0;
#endif
    }

    // SDL のスキャンコード（USB の並び）→ PC キーボードの位置のコード（スキャンコード セット1）
    static unsigned set1ScanCode(SDL_Scancode sc){
        // 数字の列
        if(sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return 0x02 + (sc - SDL_SCANCODE_1);
        if(sc == SDL_SCANCODE_0) return 0x0B;
        switch(sc){
            // Q の列
            case SDL_SCANCODE_Q: return 0x10; case SDL_SCANCODE_W: return 0x11; case SDL_SCANCODE_E: return 0x12;
            case SDL_SCANCODE_R: return 0x13; case SDL_SCANCODE_T: return 0x14; case SDL_SCANCODE_Y: return 0x15;
            case SDL_SCANCODE_U: return 0x16; case SDL_SCANCODE_I: return 0x17; case SDL_SCANCODE_O: return 0x18;
            case SDL_SCANCODE_P: return 0x19;
            case SDL_SCANCODE_LEFTBRACKET:  return 0x1A;   // 日本語配列の「@」
            case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;   // 「[」
            // A の列
            case SDL_SCANCODE_A: return 0x1E; case SDL_SCANCODE_S: return 0x1F; case SDL_SCANCODE_D: return 0x20;
            case SDL_SCANCODE_F: return 0x21; case SDL_SCANCODE_G: return 0x22; case SDL_SCANCODE_H: return 0x23;
            case SDL_SCANCODE_J: return 0x24; case SDL_SCANCODE_K: return 0x25; case SDL_SCANCODE_L: return 0x26;
            case SDL_SCANCODE_SEMICOLON:  return 0x27;
            case SDL_SCANCODE_APOSTROPHE: return 0x28;     // 日本語配列の「:」
            case SDL_SCANCODE_BACKSLASH:  return 0x2B;     // 日本語配列の「]」
            // Z の列
            case SDL_SCANCODE_Z: return 0x2C; case SDL_SCANCODE_X: return 0x2D; case SDL_SCANCODE_C: return 0x2E;
            case SDL_SCANCODE_V: return 0x2F; case SDL_SCANCODE_B: return 0x30; case SDL_SCANCODE_N: return 0x31;
            case SDL_SCANCODE_M: return 0x32;
            case SDL_SCANCODE_COMMA:  return 0x33;
            case SDL_SCANCODE_PERIOD: return 0x34;
            case SDL_SCANCODE_SLASH:  return 0x35;
            // 数字の列の右
            case SDL_SCANCODE_MINUS:  return 0x0C;
            case SDL_SCANCODE_EQUALS: return 0x0D;         // 日本語配列の「^」
            // 日本語配列だけのキー
            case SDL_SCANCODE_INTERNATIONAL1: return 0x73;  // 「\」（ろ）
            case SDL_SCANCODE_INTERNATIONAL3: return 0x7D;  // 「¥」
            case SDL_SCANCODE_INTERNATIONAL4: return 0x79;  // 変換
            case SDL_SCANCODE_INTERNATIONAL5: return 0x7B;  // 無変換
            case SDL_SCANCODE_NONUSBACKSLASH: return 0x56;
            default: return 0;
        }
    }
};