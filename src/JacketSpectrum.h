#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <vector>
#include <complex>
#include <mutex>
#include <cmath>
#include <algorithm>

// 再生中の音声を周波数ごとの強さ(0.0〜1.0)のバーに変換するクラス
class JacketSpectrum{
public:
    static constexpr int FFT_SIZE = 1024;

    explicit JacketSpectrum(int barCount)               // ← 変更
        : ring_(FFT_SIZE, 0.0f), bars_(barCount, 0.0f){}

        ~JacketSpectrum(){ stop(); }  

    // 音声の受け取りを開始する
    void start(){
        if(running_) return;
        Mix_QuerySpec(&freq_, &format_, &channels_);   // 実際の再生フォーマットを取得
        Mix_SetPostMix(&JacketSpectrum::postMixCallback, this);   // ← 変更
        running_ = true;
    }

    // 音声の受け取りを止める（このオブジェクトが消える前に必ず呼ぶ）
    void stop(){
        if(!running_) return;
        Mix_SetPostMix(nullptr, nullptr);
        running_ = false;
    }

    // 毎フレーム1回呼ぶ：ためた波形を解析してバーの高さを更新する
    void update(){
        // 1. 音声スレッドと取り合わないよう、ロックしてから古い順に並べ替えてコピー
        std::vector<std::complex<float>> data(FFT_SIZE);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for(int i = 0; i < FFT_SIZE; i++){
                data[i] = ring_[(writePos_ + i) % FFT_SIZE];
            }
        }

        // 2. ハン窓：両端を0に近づけて、切り取りによるノイズを減らす
        for(int i = 0; i < FFT_SIZE; i++){
            float w = 0.5f * (1.0f - std::cos(2.0f * PI * i / (FFT_SIZE - 1)));
            data[i] *= w;
        }

        fft(data);

        // 3. 対数間隔で周波数帯を分け、各帯の一番強い値をバーの高さにする
        const int barCount = static_cast<int>(bars_.size());
        const float fMin = 40.0f;
        const float fMax = std::min(16000.0f, freq_ / 2.0f);
        const float binHz = static_cast<float>(freq_) / FFT_SIZE;   // 1要素あたりの周波数幅

        for(int b = 0; b < barCount; b++){
            float f0 = fMin * std::pow(fMax / fMin, static_cast<float>(b) / barCount);
            float f1 = fMin * std::pow(fMax / fMin, static_cast<float>(b + 1) / barCount);
            int k0 = std::max(1, static_cast<int>(f0 / binHz));
            int k1 = std::max(k0 + 1, static_cast<int>(std::ceil(f1 / binHz)));
            k1 = std::min(k1, FFT_SIZE / 2);

            float peak = 0.0f;
            for(int k = k0; k < k1; k++){
                peak = std::max(peak, std::abs(data[k]));
            }

            // 4. デシベルに変換して 0.0〜1.0 に収める（-50dB 以下は0扱い）
            float db = 20.0f * std::log10(peak / (FFT_SIZE / 4.0f) + 1e-9f);
            float value = std::clamp((db + RANGE_DB) / RANGE_DB, 0.0f, 1.0f);

            // 5. 上がるときは即座に、下がるときはゆっくり（バーがちらつかない）
            bars_[b] = std::max(value, bars_[b] - FALL_SPEED);
        }
    }

    const std::vector<float>& bars() const { return bars_; }

private:
    static constexpr float PI = 3.14159265358979f;
    static constexpr float RANGE_DB = 50.0f;     // 表示する音量の幅（大きくすると小さい音も伸びる）
    static constexpr float FALL_SPEED = 0.03f;   // 1フレームで下がる量

    // SDL_mixer から音声スレッドで呼ばれる
    static void postMixCallback(void* udata, Uint8* stream, int len){
                static_cast<JacketSpectrum*>(udata)->pushSamples(stream, len);   // ← 変更
    }

    // 受け取った音声をモノラルにしてリングバッファへ書き込む
    void pushSamples(const Uint8* stream, int len){
        if(format_ != AUDIO_S16SYS || channels_ <= 0) return;   // 想定外の形式は無視

        const Sint16* samples = reinterpret_cast<const Sint16*>(stream);
        int frames = len / static_cast<int>(sizeof(Sint16) * channels_);

        std::lock_guard<std::mutex> lock(mutex_);
        for(int f = 0; f < frames; f++){
            float sum = 0.0f;
            for(int c = 0; c < channels_; c++){
                sum += samples[f * channels_ + c] / 32768.0f;
            }
            ring_[writePos_] = sum / channels_;
            writePos_ = (writePos_ + 1) % FFT_SIZE;
        }
    }

    // 反復型の FFT（要素数は2の累乗）
    static void fft(std::vector<std::complex<float>>& a){
        const size_t n = a.size();
        for(size_t i = 1, j = 0; i < n; i++){       // ビット反転の並べ替え
            size_t bit = n >> 1;
            for(; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if(i < j) std::swap(a[i], a[j]);
        }
        for(size_t len = 2; len <= n; len <<= 1){
            float ang = -2.0f * PI / static_cast<float>(len);
            std::complex<float> wl(std::cos(ang), std::sin(ang));
            for(size_t i = 0; i < n; i += len){
                std::complex<float> w(1.0f, 0.0f);
                for(size_t k = 0; k < len / 2; k++){
                    std::complex<float> u = a[i + k];
                    std::complex<float> v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }

    std::mutex mutex_;
    std::vector<float> ring_;     // 直近 FFT_SIZE サンプル分の波形（音声スレッドが書く）
    int writePos_ = 0;
    std::vector<float> bars_;     // バーの高さ（メインスレッドだけが触る）

    int freq_ = 44100;
    Uint16 format_ = AUDIO_S16SYS;
    int channels_ = 2;
    bool running_ = false;
};