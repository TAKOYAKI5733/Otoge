#pragma once

// =====================================================================
//  InputBinding.h — レーンに割り当てる「入力」
//
//  1つのレーンに、次のどれか1つを割り当てられる
//    ・キーボードのキー          （物理的な位置 = スキャンコードで覚える）
//    ・コントローラーのボタン     （ゲームパッド、アーケードコントローラー、専用コントローラーなど）
//    ・コントローラーの十字キー（ハット）
//    ・コントローラーのスティック・アナログ軸（片方向）
//
//  コントローラーは「何番のボタンか」だけで覚え、どの機械かは区別しない。
//  → 抜き差ししたり、別の USB ポートにさしたりしても、同じ設定のまま使える。
// =====================================================================

#include <SDL2/SDL.h>
#include <array>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

struct InputBinding{
    enum class Type{ None, Key, JoyButton, JoyHat, JoyAxis };

    Type type = Type::None;
    int  code = 0;    // Key: スキャンコード / JoyButton: ボタン番号 / JoyHat: ハット番号 / JoyAxis: 軸の番号
    int  value = 0;   // JoyHat: 向き（SDL_HAT_UP など） / JoyAxis: +1 か -1（どちら向きに倒すか）

    static InputBinding key(SDL_Scancode sc){ return {Type::Key, static_cast<int>(sc), 0}; }
    static InputBinding button(int b)       { return {Type::JoyButton, b, 0}; }
    static InputBinding hat(int h, int dir) { return {Type::JoyHat, h, dir}; }
    static InputBinding axis(int a, int sign){ return {Type::JoyAxis, a, sign}; }

    bool operator==(const InputBinding& o) const { return type == o.type && code == o.code && value == o.value; }
    bool operator!=(const InputBinding& o) const { return !(*this == o); }
};

constexpr int LANE_COUNT = 6;
using LaneBindings = std::array<InputBinding, LANE_COUNT>;

// 初期設定：W E R U I O
inline LaneBindings defaultLaneBindings(){
    return {
        InputBinding::key(SDL_SCANCODE_W), InputBinding::key(SDL_SCANCODE_E), InputBinding::key(SDL_SCANCODE_R),
        InputBinding::key(SDL_SCANCODE_U), InputBinding::key(SDL_SCANCODE_I), InputBinding::key(SDL_SCANCODE_O),
    };
}

// =====================================================================
//  つながっているコントローラーを開いておく
//    ・SDL は「開いた」コントローラーのボタンしか読めない
//    ・毎フレーム refresh() を呼べば、途中で抜き差ししても自動で開き直す（軽い処理）
//    ・main.cpp の SDL_Init に SDL_INIT_JOYSTICK を足しておくこと
// =====================================================================
class InputDevices{
public:
    static InputDevices& get(){
        static InputDevices inst;
        return inst;
    }

    void refresh(){
        if(SDL_WasInit(SDL_INIT_JOYSTICK) == 0) return;

        // 抜かれたものを閉じる
        for(size_t i = 0; i < joys_.size();){
            if(!SDL_JoystickGetAttached(joys_[i])){
                SDL_JoystickClose(joys_[i]);
                joys_.erase(joys_.begin() + static_cast<long>(i));
            }
            else{
                i++;
            }
        }
        // まだ開いていないものを開く
        const int n = SDL_NumJoysticks();
        for(int d = 0; d < n; d++){
            const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(d);
            bool opened = false;
            for(SDL_Joystick* j : joys_){
                if(SDL_JoystickInstanceID(j) == id){ opened = true; break; }
            }
            if(!opened){
                if(SDL_Joystick* j = SDL_JoystickOpen(d)) joys_.push_back(j);
            }
        }
    }

    const std::vector<SDL_Joystick*>& joysticks() const { return joys_; }

    void closeAll(){
        for(SDL_Joystick* j : joys_) SDL_JoystickClose(j);
        joys_.clear();
    }

private:
    std::vector<SDL_Joystick*> joys_;
};

// アナログ軸を「押した」とみなす倒し具合（最大 32767）
constexpr int AXIS_PRESS_THRESHOLD = 16000;

// 軸の「離した状態」の値。トリガーなどは離した状態で -32768 のことがあるので、つないだ時の値を基準にする
inline int axisRestValue(SDL_Joystick* j, int axis){
    Sint16 rest = 0;
    if(SDL_JoystickGetAxisInitialState(j, axis, &rest)) return rest;
    return 0;
}

// その割り当てが、今押されているか
//   keys：SDL_GetKeyboardState(NULL) の結果（1フレームに1回取って使い回す）
inline bool isBindingPressed(const InputBinding& b, const Uint8* keys){
    switch(b.type){
        case InputBinding::Type::Key:
            return keys && b.code >= 0 && b.code < SDL_NUM_SCANCODES && keys[b.code] != 0;

        case InputBinding::Type::JoyButton:
            for(SDL_Joystick* j : InputDevices::get().joysticks()){
                if(b.code < SDL_JoystickNumButtons(j) && SDL_JoystickGetButton(j, b.code)) return true;
            }
            return false;

        case InputBinding::Type::JoyHat:
            for(SDL_Joystick* j : InputDevices::get().joysticks()){
                if(b.code < SDL_JoystickNumHats(j) && (SDL_JoystickGetHat(j, b.code) & b.value)) return true;
            }
            return false;

        case InputBinding::Type::JoyAxis:
            for(SDL_Joystick* j : InputDevices::get().joysticks()){
                if(b.code >= SDL_JoystickNumAxes(j)) continue;
                const int d = SDL_JoystickGetAxis(j, b.code) - axisRestValue(j, b.code);
                if(b.value > 0 ? d > AXIS_PRESS_THRESHOLD : d < -AXIS_PRESS_THRESHOLD) return true;
            }
            return false;

        default:
            return false;
    }
}

// イベントが「新しく押された入力」なら、その割り当てを out に入れて true を返す（キー設定の入力待ちで使う）
inline bool captureBinding(const SDL_Event& e, InputBinding& out){
    switch(e.type){
        case SDL_KEYDOWN:
            if(e.key.repeat) return false;
            out = InputBinding::key(e.key.keysym.scancode);
            return true;

        case SDL_JOYBUTTONDOWN:
            out = InputBinding::button(e.jbutton.button);
            return true;

        case SDL_JOYHATMOTION:{
            // 斜め（UP|RIGHT など）は、縦の向きを優先して1方向にする
            const int v = e.jhat.value;
            int dir = 0;
            if(v & SDL_HAT_UP)         dir = SDL_HAT_UP;
            else if(v & SDL_HAT_DOWN)  dir = SDL_HAT_DOWN;
            else if(v & SDL_HAT_LEFT)  dir = SDL_HAT_LEFT;
            else if(v & SDL_HAT_RIGHT) dir = SDL_HAT_RIGHT;
            if(dir == 0) return false;   // 真ん中に戻っただけ
            out = InputBinding::hat(e.jhat.hat, dir);
            return true;
        }

        case SDL_JOYAXISMOTION:{
            SDL_Joystick* j = SDL_JoystickFromInstanceID(e.jaxis.which);
            const int rest = j ? axisRestValue(j, e.jaxis.axis) : 0;
            const int d = e.jaxis.value - rest;
            if(std::abs(d) < 24000) return false;   // 少し触れただけでは決めない（誤って決まらないように深めに）
            out = InputBinding::axis(e.jaxis.axis, d > 0 ? +1 : -1);
            return true;
        }

        default:
            return false;
    }
}

// 画面に出す名前（キー設定の画面・プレイ画面のレーンの下）
inline std::string bindingName(const InputBinding& b){
    char buf[64];
    switch(b.type){
        case InputBinding::Type::Key:{
            const SDL_Scancode sc = static_cast<SDL_Scancode>(b.code);
            const char* name = SDL_GetKeyName(SDL_GetKeyFromScancode(sc));
            if(!name || !*name) name = SDL_GetScancodeName(sc);
            if(!name || !*name){
                std::snprintf(buf, sizeof(buf), "KEY %d", b.code);
                return buf;
            }
            return name;
        }
        case InputBinding::Type::JoyButton:
            std::snprintf(buf, sizeof(buf), "BUTTON %d", b.code + 1);   // 人が数えやすいように 1 から
            return buf;
        case InputBinding::Type::JoyHat:{
            const char* d = (b.value == SDL_HAT_UP) ? "UP" : (b.value == SDL_HAT_DOWN) ? "DOWN"
                          : (b.value == SDL_HAT_LEFT) ? "LEFT" : "RIGHT";
            std::snprintf(buf, sizeof(buf), "HAT%d %s", b.code + 1, d);
            return buf;
        }
        case InputBinding::Type::JoyAxis:
            std::snprintf(buf, sizeof(buf), "AXIS %d %c", b.code + 1, b.value > 0 ? '+' : '-');
            return buf;
        default:
            return "---";
    }
}

// ----- 設定ファイル用の文字列（"key:26" / "btn:3" / "hat:0:1" / "axis:1:+1"） -----
inline std::string bindingToString(const InputBinding& b){
    char buf[48];
    switch(b.type){
        case InputBinding::Type::Key:       std::snprintf(buf, sizeof(buf), "key:%d", b.code); break;
        case InputBinding::Type::JoyButton: std::snprintf(buf, sizeof(buf), "btn:%d", b.code); break;
        case InputBinding::Type::JoyHat:    std::snprintf(buf, sizeof(buf), "hat:%d:%d", b.code, b.value); break;
        case InputBinding::Type::JoyAxis:   std::snprintf(buf, sizeof(buf), "axis:%d:%d", b.code, b.value); break;
        default:                            return "none";
    }
    return buf;
}

inline bool bindingFromString(const std::string& s, InputBinding& out){
    int a = 0, v = 0;
    if(std::sscanf(s.c_str(), "key:%d", &a) == 1 && a > 0 && a < SDL_NUM_SCANCODES){ out = InputBinding::key(static_cast<SDL_Scancode>(a)); return true; }
    if(std::sscanf(s.c_str(), "btn:%d", &a) == 1 && a >= 0){ out = InputBinding::button(a); return true; }
    if(std::sscanf(s.c_str(), "hat:%d:%d", &a, &v) == 2 && a >= 0){ out = InputBinding::hat(a, v); return true; }
    if(std::sscanf(s.c_str(), "axis:%d:%d", &a, &v) == 2 && a >= 0){ out = InputBinding::axis(a, v >= 0 ? +1 : -1); return true; }
    return false;
}