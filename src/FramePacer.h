#pragma once
#include <SDL2/SDL.h>
#include <cstdio>
#include <cmath>
#include <cstdlib>

// =====================================================================
//  FPS 自動で選ぶ候補（設定画面の FPS 上限の選択肢から「無制限」を除いたもの）
//  Settingspanel.h の FPS_OPTIONS と同じ値にそろえておく
// =====================================================================
inline constexpr int FPS_AUTO_CHOICES[] = {60, 120, 144, 165, 240, 360};
inline constexpr int FPS_AUTO_CHOICE_COUNT = static_cast<int>(sizeof(FPS_AUTO_CHOICES) / sizeof(FPS_AUTO_CHOICES[0]));

// モニターの Hz に一番近い候補を返す（例：59 → 60、75 → 60、143 → 144、170 → 165）
// 差が同じなら小さい方を選ぶ（上限をモニターより高くしすぎないため）
inline int nearestFpsChoice(int hz){
    int best = FPS_AUTO_CHOICES[0];
    for(int i = 1; i < FPS_AUTO_CHOICE_COUNT; i++){
        if(std::abs(FPS_AUTO_CHOICES[i] - hz) < std::abs(best - hz)) best = FPS_AUTO_CHOICES[i];
    }
    return best;
}

class FramePacer{
public:
    // 手動の FPS 上限。fps <= 0 で上限なし（VSync任せ）
    //   自動が ON の間は覚えておくだけで、OFF に戻したときにこの値を使う
    void setCap(int fps){
        manualFrameSec = (fps > 0) ? 1.0 / fps : 0.0;
        if(!autoCap) targetFrameSec = manualFrameSec;
    }

    // FPS 自動：ON の間は、ウィンドウがいるモニターの Hz に一番近い候補を上限にする
    //   window を渡すと、その場で1回計算する（渡さなければ次の1秒ごとの確認で計算）
    void setAutoCap(bool on, SDL_Window* window = nullptr){
        autoCap = on;
        detectedHz_ = -1;   // 次の確認で必ず計算し直す
        autoFps_ = 0;
        if(on){
            if(window) updateAutoCap(window);
        }
        else{
            targetFrameSec = manualFrameSec;   // 手動の値に戻す
        }
    }

    void setShowFps(bool show){ showFps = show; }

    // SDL_RenderPresent の直後に毎フレーム呼ぶ
    void endFrame(SDL_Window* window = nullptr){
        const double freq = static_cast<double>(SDL_GetPerformanceFrequency());

        if(targetFrameSec > 0.0){
            const Uint64 frameTicks = static_cast<Uint64>(targetFrameSec * freq);
            const Uint64 target = lastFrame + frameTicks;

            while(true){
                Uint64 now = SDL_GetPerformanceCounter();
                if(now >= target) break;
                double remainMs = static_cast<double>(target - now) * 1000.0 / freq;
                if(remainMs > 2.0){
                    SDL_Delay(static_cast<Uint32>(remainMs - 1.5)); // 大まかに寝る
                }
                // 残り2ms未満は空回し（SDL_Delayの誤差を避ける）
            }

            // 目標時刻を基準に進めることで誤差が積み重ならない
            lastFrame = target;
            Uint64 now = SDL_GetPerformanceCounter();
            if(now - lastFrame > frameTicks){
                lastFrame = now; // 大きく遅れた（ロード等）ら基準をリセット
            }
        }
        else{
            lastFrame = SDL_GetPerformanceCounter();
        }

        // 前フレームからの経過時間（アニメーション補正用）
        Uint64 now = SDL_GetPerformanceCounter();
        if(prevEnd != 0){
            dt = static_cast<double>(now - prevEnd) / freq;
            if(dt > 0.1) dt = 0.1; // ロード明けの巨大なdtで動きが飛ばないように
        }
        prevEnd = now;

        // FPS計測（1秒ごとにタイトルへ表示）
        frames++;
        if(statStart == 0) statStart = now;
        double elapsed = static_cast<double>(now - statStart) / freq;
        if(elapsed >= 1.0){
            measuredFps = frames / elapsed;
            frames = 0;
            statStart = now;
            if(window && showFps){
                char buf[64];
                snprintf(buf, sizeof(buf), "Otoge - %.1f fps", measuredFps);
                SDL_SetWindowTitle(window, buf);
            }

            // FPS 自動：1秒に1回だけモニターの Hz を確かめる
            //   別のモニターへ移したとき・表示モードを変えたときも、ここで追いかける
            if(autoCap && window) updateAutoCap(window);
        }
    }

    double deltaSec() const { return dt; }
    double fps() const { return measuredFps; }

    bool isAutoCap() const { return autoCap; }
    int  autoCapFps() const { return autoFps_; }      // 自動で決めた上限（0 = まだ／取得できない）
    int  detectedHz() const { return detectedHz_; }   // 取得できたモニターの Hz（-1 = まだ、0 = 取得できない）

private:
    // ウィンドウがいるモニターの「今の」Hz を調べて、変わっていれば上限を決め直す
    void updateAutoCap(SDL_Window* window){
        int display = SDL_GetWindowDisplayIndex(window);
        if(display < 0) display = 0;

        // SDL_GetWindowDisplayMode はウィンドウ用に登録したモードを返すので、
        // モニターが実際に動いているモードを返す SDL_GetCurrentDisplayMode を使う
        SDL_DisplayMode dm;
        int hz = 0;
        if(SDL_GetCurrentDisplayMode(display, &dm) == 0) hz = dm.refresh_rate;

        if(hz == detectedHz_) return;   // 変わっていなければ何もしない
        detectedHz_ = hz;

        if(hz <= 0){
            // Hz が分からない環境（一部の Linux 環境など）では、手動の値を使う
            autoFps_ = 0;
            targetFrameSec = manualFrameSec;
            printf("[FramePacer] モニターの Hz が取得できないため、手動の FPS 上限を使います\n");
            return;
        }

        autoFps_ = nearestFpsChoice(hz);
        targetFrameSec = 1.0 / autoFps_;
        printf("[FramePacer] モニター %d Hz → FPS 上限 %d\n", hz, autoFps_);
    }

    double targetFrameSec = 0.0;   // 実際に使う上限（自動なら自動の値、そうでなければ手動の値）
    double manualFrameSec = 0.0;   // 手動で設定された上限
    bool autoCap = false;
    int  autoFps_ = 0;
    int  detectedHz_ = -1;

    Uint64 lastFrame = 0;
    Uint64 prevEnd = 0;
    double dt = 1.0 / 60.0;
    Uint64 statStart = 0;
    int frames = 0;
    double measuredFps = 0.0;
    bool showFps = true;
};

inline FramePacer g_framePacer;

// 「60fpsで毎フレーム rate ずつ近づく」動きを、任意のfpsで同じ速さにする係数
// 例: x += (target - x) * 0.15  →  x += (target - x) * lerpFactor(0.15)
inline double lerpFactor(double ratePer60fps){
    return 1.0 - std::pow(1.0 - ratePer60fps, g_framePacer.deltaSec() * 60.0);
}