#pragma once
#include <SDL2/SDL.h>
#include <cstdio>
#include <cmath>

class FramePacer{
public:
    // fps <= 0 で上限なし（VSync任せ）
    void setCap(int fps){
        targetFrameSec = (fps > 0) ? 1.0 / fps : 0.0;
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
        }
    }

    double deltaSec() const { return dt; }
    double fps() const { return measuredFps; }

private:
    double targetFrameSec = 0.0;
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