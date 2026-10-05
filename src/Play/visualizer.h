#pragma once

#include "GameCommon.h"
#include <array>
#include <complex>
#include <mutex>

// =====================================================================
//  SpectrumVisualizer
//  再生中の音を周波数帯ごとに分解し、帯ごとの強さを
//  「中央から左右に伸びる横線」を縦に積み重ねて描画する。
//  下 = 低音 / 上 = 高音
// =====================================================================
struct SpectrumVisualizer{

    // ---------- 調整用パラメータ ----------
    static constexpr int FFT_SIZE  = 2048; // 解析に使うサンプル数（必ず2の累乗）
    static constexpr int BAR_COUNT = 64;   // 横線の本数（=周波数帯の数）

    float minFreq = 40.0f;      // 一番下の線が担当する周波数[Hz]
    float maxFreq = 16000.0f;   // 一番上の線が担当する周波数[Hz]
    float minDb   = -60.0f;     // この音量以下は長さ0
    float maxDb   = -6.0f;      // この音量以上は最大長
    float tiltDbPerOct = 3.0f;  // 高音ほど上乗せする補正（1オクターブごと）
    float attackMs  = 20.0f;    // 線が伸びる速さ（小さいほど速い）
    float releaseMs = 150.0f;   // 線が縮む速さ（大きいほどゆっくり）

    int lineThickness = 3;      // 線の太さ[px]
    int minHalfWidth = 2;       // 無音時の半分の長さ（上の方の「点」になる）
    int maxHalfWidth = 200;     // 最大時の半分の長さ
    std::vector<int> centerXs = {0, SCREEN_W}; // 描画する列の中心X（左右の余白）
    SDL_Color color = {255, 255, 255, 120};

    // ---------- オーディオスレッドと共有するデータ（mtxで保護） ----------
    std::mutex mtx;
    std::array<float, FFT_SIZE> ring{}; // 直近FFT_SIZE個のモノラルサンプル（リングバッファ）
    int writePos = 0;                   // 次に書き込む位置 = 一番古いサンプルの位置

    // ---------- メインスレッド専用のデータ ----------
    int sampleRate = 44100;
    Uint16 format = AUDIO_S16SYS;
    int channels = 2;
    bool attached = false;
    Uint32 lastTick = 0;

    std::array<float, FFT_SIZE> window{};                  // ハン窓
    std::array<std::complex<float>, FFT_SIZE> fftBuf{};    // FFT作業領域
    std::array<int, BAR_COUNT + 1> bandEdgeBin{};          // 各帯の境界ビン番号
    std::array<float, BAR_COUNT> bandTiltDb{};             // 各帯の高域補正量
    std::array<float, BAR_COUNT> levels{};                 // 表示中の長さ(0〜1)

    SpectrumVisualizer() = default;
    SpectrumVisualizer(const SpectrumVisualizer&) = delete;
    SpectrumVisualizer& operator=(const SpectrumVisualizer&) = delete;
    ~SpectrumVisualizer(){ detach(); }

    // ---------------------------------------------------------------
    // 音声フックの登録と、窓関数・帯域の事前計算
    // ---------------------------------------------------------------
    bool attach(){
        int freq = 0, ch = 0;
        Uint16 fmt = 0;
        if(Mix_QuerySpec(&freq, &fmt, &ch) == 0){
            printf("Visualizer: オーディオ未初期化\n");
            return false;
        }
        if(fmt != AUDIO_S16SYS && fmt != AUDIO_F32SYS){
            printf("Visualizer: 未対応の音声フォーマット 0x%x\n", fmt);
            return false;
        }
        sampleRate = freq;
        format = fmt;
        channels = ch;

        // ハン窓: 切り出した波形の両端を0に近づけ、周波数の「にじみ」を減らす
        constexpr float PI = 3.14159265358979f;
        for(int n = 0; n < FFT_SIZE; n++){
            window[n] = 0.5f * (1.0f - std::cos(2.0f * PI * n / (FFT_SIZE - 1)));
        }

        // 帯の境界を対数間隔で決める（耳は周波数を「比」で感じるため）
        const int maxBin = FFT_SIZE / 2 - 1;
        for(int b = 0; b <= BAR_COUNT; b++){
            float f = minFreq * std::pow(maxFreq / minFreq, static_cast<float>(b) / BAR_COUNT);
            int bin = static_cast<int>(std::round(f * FFT_SIZE / sampleRate));
            bin = std::clamp(bin, 1, maxBin);
            // 低音側は1帯が1ビンより狭くなるので、最低1ビンは確保する
            if(b > 0 && bin <= bandEdgeBin[b - 1]) bin = std::min(bandEdgeBin[b - 1] + 1, maxBin + 1);
            bandEdgeBin[b] = bin;
        }

        // 音楽は高音ほどエネルギーが小さいので、オクターブごとに少し持ち上げる
        for(int b = 0; b < BAR_COUNT; b++){
            float fLo = bandEdgeBin[b]     * static_cast<float>(sampleRate) / FFT_SIZE;
            float fHi = bandEdgeBin[b + 1] * static_cast<float>(sampleRate) / FFT_SIZE;
            float fc  = std::sqrt(fLo * fHi);
            bandTiltDb[b] = tiltDbPerOct * std::log2(fc / 1000.0f);
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            ring.fill(0.0f);
            writePos = 0;
        }
        levels.fill(0.0f);
        lastTick = SDL_GetTicks();

        // SDL_mixerが全ての音を混ぜ終わった直後のデータを受け取る
        Mix_SetPostMix(&SpectrumVisualizer::postMixCallback, this);
        attached = true;
        return true;
    }

    // ---------------------------------------------------------------
    // 音声フックの解除（このオブジェクトが消える前に必ず呼ぶ）
    // ---------------------------------------------------------------
    void detach(){
        if(!attached) return;
        Mix_SetPostMix(nullptr, nullptr);
        attached = false;
    }

    // ---------------------------------------------------------------
    // オーディオスレッドから呼ばれる（描画処理は絶対に書かない）
    // ---------------------------------------------------------------
    static void SDLCALL postMixCallback(void* udata, Uint8* stream, int len){
        static_cast<SpectrumVisualizer*>(udata)->pushSamples(stream, len);
    }

    void pushSamples(const Uint8* stream, int len){
        std::lock_guard<std::mutex> lock(mtx);

        if(format == AUDIO_S16SYS){
            const Sint16* s = reinterpret_cast<const Sint16*>(stream);
            int frames = len / static_cast<int>(sizeof(Sint16) * channels);
            for(int f = 0; f < frames; f++){
                float sum = 0.0f;
                for(int c = 0; c < channels; c++) sum += s[f * channels + c] / 32768.0f;
                ring[writePos] = sum / channels; // L/Rを平均してモノラル化
                writePos = (writePos + 1) % FFT_SIZE;
            }
        }
        else{ // AUDIO_F32SYS
            const float* s = reinterpret_cast<const float*>(stream);
            int frames = len / static_cast<int>(sizeof(float) * channels);
            for(int f = 0; f < frames; f++){
                float sum = 0.0f;
                for(int c = 0; c < channels; c++) sum += s[f * channels + c];
                ring[writePos] = sum / channels;
                writePos = (writePos + 1) % FFT_SIZE;
            }
        }
    }

    // ---------------------------------------------------------------
    // 毎フレーム: 直近の波形 → FFT → 帯ごとの強さ → なめらかに追従
    // ---------------------------------------------------------------
    void update(){
        if(!attached) return;

        Uint32 now = SDL_GetTicks();
        float dtMs = static_cast<float>(now - lastTick);
        lastTick = now;
        if(dtMs > 100.0f) dtMs = 100.0f;

        // ロックしている時間を最短にするため、コピーだけしてすぐ離す
        {
            std::lock_guard<std::mutex> lock(mtx);
            for(int n = 0; n < FFT_SIZE; n++){
                int idx = (writePos + n) % FFT_SIZE; // 古い順に並べ直す
                fftBuf[n] = std::complex<float>(ring[idx] * window[n], 0.0f);
            }
        }

        fft(fftBuf);

        // 振幅1.0の正弦波がちょうど1.0になるように補正（ハン窓の平均0.5 × 片側スペクトル）
        const float norm = 4.0f / FFT_SIZE;

        for(int b = 0; b < BAR_COUNT; b++){
            float peak = 0.0f;
            for(int k = bandEdgeBin[b]; k < bandEdgeBin[b + 1]; k++){
                peak = std::max(peak, std::abs(fftBuf[k]) * norm);
            }

            float db = 20.0f * std::log10(peak + 1e-9f) + bandTiltDb[b];
            float target = std::clamp((db - minDb) / (maxDb - minDb), 0.0f, 1.0f);

            // 伸びるときは速く、縮むときはゆっくり（フレームレートに依存しない指数補間）
            float tau = (target > levels[b]) ? attackMs : releaseMs;
            float a = 1.0f - std::exp(-dtMs / tau);
            levels[b] += (target - levels[b]) * a;
        }
    }

    // ---------------------------------------------------------------
    // 描画: 帯0(低音)を一番下に、中央から左右対称に伸ばす
    // ---------------------------------------------------------------
    void render(SDL_Renderer* renderer) const{
        if(!attached) return;

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

        // 1本目の上端 = 0、最後の線の下端 = SCREEN_H になる間隔
        const float step = static_cast<float>(SCREEN_H - lineThickness) / (BAR_COUNT - 1);
        std::array<SDL_Rect, BAR_COUNT> rects;

        for(int cx : centerXs){
            for(int b = 0; b < BAR_COUNT; b++){
                // b=0(低音)が一番下。上端の座標を四捨五入で決める
                int yTop = static_cast<int>(std::round((BAR_COUNT - 1 - b) * step));
                int half = minHalfWidth + static_cast<int>(levels[b] * (maxHalfWidth - minHalfWidth));
                rects[b] = {cx - half, yTop, half * 2, lineThickness};
            }
            SDL_RenderFillRects(renderer, rects.data(), BAR_COUNT);
        }
    }

private:
    // 反復型 Cooley-Tukey FFT（基数2）
    static void fft(std::array<std::complex<float>, FFT_SIZE>& a){
        constexpr float PI = 3.14159265358979f;
        const int n = FFT_SIZE;

        // ビット反転並べ替え
        for(int i = 1, j = 0; i < n; i++){
            int bit = n >> 1;
            for(; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if(i < j) std::swap(a[i], a[j]);
        }

        // バタフライ演算
        for(int len = 2; len <= n; len <<= 1){
            float ang = -2.0f * PI / len;
            std::complex<float> wlen(std::cos(ang), std::sin(ang));
            for(int i = 0; i < n; i += len){
                std::complex<float> w(1.0f, 0.0f);
                for(int j = 0; j < len / 2; j++){
                    std::complex<float> u = a[i + j];
                    std::complex<float> v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
    }
};