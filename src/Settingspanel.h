#pragma once

#include "GameCommon.h"
#include "Displaymode.h"
#include "TapSound.h"
#include "HoloWindow.h"   // ウィンドウの見た目（Cube の色のホログラム・走査線・たまにグリッチ）
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cmath>

// =====================================================================
//  選曲画面の上に左からスライドインする設定ウィンドウ
//  使い方：
//    init(settings)    … 選曲シーンの開始時に1回
//    open(settings)    … F2 で開く
//    handleKey(...)    … 開いている間のキー入力を全部渡す
//    update()          … 毎フレーム1回（スライドのアニメーション）
//    draw(...)         … 毎フレーム、選曲画面を描いた「後」に呼ぶ（一番手前に出す）
//    destroy()         … 選曲シーンの終了時に1回
//
//  操作（2段階）
//    ・選ぶとき   ：←→ / Tab（Shift+Tab で逆）で分類を切り替え、↑↓ で項目を選ぶ
//                   ENTER で「変更中」に入る（判定調整・キー設定・ノーツの詳細設定は、ENTER でその画面を開く）
//    ・変更中     ：←→ で値を変える（Shift で大きく）。ENTER / ESC で「選ぶとき」に戻る
//    ・F2         ：いつでも閉じて保存。ESC は「選ぶとき」なら閉じて保存
//  ノーツ速度・レーンの傾き・レーンカバー・キービームの長さは、ノーツの詳細設定（NoteSettingScene.cpp）だけで変える
// =====================================================================
class SettingsPanel{
public:
    void init(const PlayerSettings& s){
        tapSounds_  = listTapSounds();              // sounds/tap の中を調べて選択肢を作る
        previewTap_ = loadTapSound(s.tapSound);     // 試し聴き用に、今選んでいる音を読み込む
    }

    void destroy(){
        if(previewTap_){
            Mix_FreeChunk(previewTap_);
            previewTap_ = nullptr;
        }
    }

    bool isOpen() const { return open_; }

    // 「判定調整」で ENTER が押されたか（1回読むと false に戻る）
    bool consumeCalibrationRequest(){
        bool requested = requestCalibration_;
        requestCalibration_ = false;
        return requested;
    }

    // 「ノーツの詳細設定」で ENTER が押されたか（1回読むと false に戻る）
    bool consumeNoteSettingRequest(){
        bool requested = requestNoteSetting_;
        requestNoteSetting_ = false;
        return requested;
    }

    // 「キー設定」で ENTER が押されたか（1回読むと false に戻る）
    bool consumeKeyConfigRequest(){
        bool requested = requestKeyConfig_;
        requestKeyConfig_ = false;
        return requested;
    }

    // 開く：表示モードは閉じたときに反映するので、開いた時点の値を覚えておく
    void open(const PlayerSettings& s){
        open_ = true;
        cursor_ = 0;
        editing_ = false;
        openedWindowMode_ = s.windowMode;
        openedStreamMode_ = s.streamMode;
    }

    // 閉じる：表示モードが変わっていれば反映し、ファイルに保存する
    void close(SDL_Window* window, PlayerSettings& s){
        if(!open_) return;
        open_ = false;
        editing_ = false;
        // 表示モードか配信モードが変わっていれば、表示モードを付け直す
        //   （配信モードを切り替えると、フルスクリーンが排他 ↔ ボーダーレスに変わるため）
        if(s.windowMode != openedWindowMode_ || s.streamMode != openedStreamMode_){
            g_streamMode = s.streamMode;
            applyWindowMode(window, s.windowMode);
            // 表示モードを変えると Hz が変わることがある（フルスクリーンは一番高い Hz を選ぶ）ので、
            // FPS 自動が ON ならその場で選び直す
            if(s.fpsAuto) g_framePacer.setAutoCap(true, window);
        }
        savePlayerSettings(s);
    }

    // 開いている間のキー入力を処理する
    void handleKey(const SDL_KeyboardEvent& key, SDL_Window* window, SDL_Renderer* renderer, PlayerSettings& s){
        SDL_Keycode sym = key.keysym.sym;
        bool isArrow = (sym == SDLK_UP || sym == SDLK_DOWN || sym == SDLK_LEFT || sym == SDLK_RIGHT);
        if(key.repeat && !isArrow) return;   // 押しっぱなしの連続入力は矢印だけ受け付ける

        bool big = (key.keysym.mod & KMOD_SHIFT) != 0;

        // F2 はどちらの状態でも、閉じて保存
        if(sym == SDLK_F2){
            close(window, s);
            return;
        }

        // ===== 変更中：←→ で値を変える。ENTER / ESC で選ぶ状態に戻る =====
        if(editing_){
            switch(sym){
                case SDLK_LEFT:   changeValue(-1, big, renderer, s); break;
                case SDLK_RIGHT:  changeValue(+1, big, renderer, s); break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_ESCAPE: editing_ = false; break;
                default: break;   // ↑↓・Tab などは、変更中は使わない（うっかり別の項目に移らないように）
            }
            return;
        }

        // ===== 選ぶとき：←→ / Tab で分類、↑↓ で項目、ENTER で変更中へ =====
        const int rowCount = categoryRowCount(category_);
        switch(sym){
            case SDLK_ESCAPE:
                close(window, s);
                break;
            case SDLK_TAB:
                // Shift+Tab で逆順
                changeCategory(big ? -1 : +1);
                break;
            case SDLK_LEFT:
                changeCategory(-1);
                break;
            case SDLK_RIGHT:
                changeCategory(+1);
                break;
            case SDLK_UP:
                cursor_ = (cursor_ + rowCount - 1) % rowCount;
                break;
            case SDLK_DOWN:
                cursor_ = (cursor_ + 1) % rowCount;
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                if(currentRow() == ROW_CALIBRATE){
                    // 判定調整：設定を保存して閉じ、選曲画面に「調整画面へ行く」ことを知らせる
                    requestCalibration_ = true;
                    close(window, s);
                }
                else if(currentRow() == ROW_NOTE_DETAIL){
                    // ノーツの詳細設定：判定調整と同じく、保存して閉じてから選曲画面に知らせる
                    requestNoteSetting_ = true;
                    close(window, s);
                }
                else if(currentRow() == ROW_KEYCONFIG){
                    // キー設定：判定調整と同じく、保存して閉じてから選曲画面に知らせる
                    requestKeyConfig_ = true;
                    close(window, s);
                }
                else if(!isLocked(currentRow(), s)){
                    editing_ = true;   // ここから ←→ で値を変えられる
                }
                break;
            default:
                break;
        }
    }

    // 毎フレーム：開閉に向かってアニメーション値を近づける
    void update(){
        double target = open_ ? 1.0 : 0.0;
        anim_ += (target - anim_) * 0.18;
    }

    void draw(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* smallFont, const PlayerSettings& s) const{
        if(anim_ < 0.001) return;

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        Uint8 a = static_cast<Uint8>(255 * anim_);

        // 1. 後ろの選曲画面を暗くする
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<Uint8>(150 * anim_));
        SDL_Rect full = {0, 0, SW, SH};
        SDL_RenderFillRect(renderer, &full);

        // 2. ウィンドウ本体：画面左の外から PANEL_X までスライド
        int x = static_cast<int>(-PANEL_W - 20 + (PANEL_X + PANEL_W + 20) * anim_);
        SDL_Rect panel = {x, PANEL_Y, PANEL_W, PANEL_H};

        drawTab(renderer, smallFont, "SETTINGS", panel, a);
        drawHoloWindow(renderer, panel, anim_, true, 3, 0x5E77u);

        // 3. 分類のタブ（選んでいる分類だけシアンで塗る）
        {
            const int tabY = PANEL_Y + 24;
            const int tabW = (PANEL_W - 60 - TAB_GAP * (CATEGORY_COUNT - 1)) / CATEGORY_COUNT;
            for(int c = 0; c < CATEGORY_COUNT; c++){
                bool active = (c == category_);
                SDL_Rect tab = {x + 30 + c * (tabW + TAB_GAP), tabY, tabW, TAB_H};
                drawFrame(renderer, tab,
                          active ? SDL_Color{HOLO_WINDOW_COLOR.r, HOLO_WINDOW_COLOR.g, HOLO_WINDOW_COLOR.b, static_cast<Uint8>(230 * anim_)}
                                 : SDL_Color{30, 30, 50, static_cast<Uint8>(170 * anim_)},
                          active ? SDL_Color{HOLO_WINDOW_COLOR.r, HOLO_WINDOW_COLOR.g, HOLO_WINDOW_COLOR.b, a}
                                 : SDL_Color{110, 110, 150, a},
                          active ? 2 : 1);
                drawText(renderer, smallFont, CATEGORY_NAMES[c],
                         active ? SDL_Color{15, 15, 25, 255} : SDL_Color{200, 200, 220, 255},
                         tab.x + tab.w / 2, tab.y + tab.h / 2, a, Align::Center, tab.w - TEXT_PAD * 2);
            }
        }

        // 4. 選んでいる分類の項目
        const int rowCount = categoryRowCount(category_);
        for(int k = 0; k < rowCount; k++){
            const int i = CATEGORY_ROWS[category_][k];   // 項目の種類（ROW_xxx）
            bool selected = (k == cursor_);
            const bool editing = selected && editing_;   // この項目を変更中か
            SDL_Rect row = {x + 30, ROWS_TOP + k * ROW_H, PANEL_W - 60, ROW_H - 12};

            // 今は変えられない項目（FPS 自動が ON の間の FPS 上限）は、灰色で描く
            const bool locked = isLocked(i, s);

            // 変更中の項目は、塗りを明るくして枠を橙にする（選んでいるだけの項目と見分けられるように）
            drawFrame(renderer, row,
                      editing  ? SDL_Color{70, 55, 40, static_cast<Uint8>(230 * anim_)}
                    : selected ? SDL_Color{45, 45, 80, static_cast<Uint8>(210 * anim_)}
                               : SDL_Color{30, 30, 50, static_cast<Uint8>(170 * anim_)},
                      editing  ? SDL_Color{255, 190, 60, a}
                    : selected ? SDL_Color{0, 255, 255, a}
                               : SDL_Color{110, 110, 150, a},
                      selected ? 3 : 1);

            int cy = row.y + row.h / 2;
            SDL_Color labelColor = selected ? SDL_Color{0, 255, 255, 255} : SDL_Color{255, 255, 255, 255};
            if(locked) labelColor = SDL_Color{130, 130, 150, 255};
            // 項目名と値の幅：枠の内側（左右 30px の余白を除いた幅）を2つで分ける
            //   ・項目名は内側の LABEL_MAX_RATIO まで（はみ出す分だけ縮める）
            //   ・値は、項目名が実際に使った幅と、間のすき間（LABEL_VALUE_GAP）を除いた残り全部
            const int innerW = row.w - 60;
            const int labelW = drawText(renderer, font, ROW_LABELS[i], labelColor, row.x + 30, cy, a, Align::Left,
                                        static_cast<int>(innerW * LABEL_MAX_RATIO));

            // 値：変更中だけ「< 値 >」の形にして、左右で変えられることを示す
            std::string value = valueText(i, s);
            if(editing) value = "<  " + value + "  >";
            SDL_Color valueColor = locked ? SDL_Color{130, 130, 150, 255} : SDL_Color{255, 255, 255, 255};
            // 音声オフセットが ±20ms 以上でノーツ音を鳴らしているときは、音声オフセットとノーツ音の値を橙色にする
            if(!locked && (i == ROW_OFFSET || i == ROW_TAPSOUND) && shouldWarnTapSoundOffset(s)) valueColor = WARN_COLOR;
            drawText(renderer, font, value, valueColor, row.x + row.w - 30, cy, a, Align::Right,
                     std::max(80, innerW - labelW - LABEL_VALUE_GAP));
        }

        // 5. 選択中の項目の説明と、操作方法
        int descY = ROWS_TOP + MAX_ROWS * ROW_H + 10;
        const char* desc = isLocked(currentRow(), s) ? "FPS 自動が ON の間は、モニターに合わせて自動で決まります"
                                                     : ROW_DESCRIPTIONS[currentRow()];
        const int textMaxW = PANEL_W - 80;   // 説明・警告・操作方法の最大の幅（左右 40px の余白の内側）
        drawText(renderer, smallFont, desc, SDL_Color{180, 180, 200, 255},
                 x + 40, descY, a, Align::Left, textMaxW);

        // 6. 警告：音声オフセットが 0 から 20ms 以上ずれているのに、ノーツ音を鳴らしているとき
        //    どの項目を選んでいても、説明の下にもう1行出す（オフセットの分類でも、ノーツの分類でも気づけるように）
        if(shouldWarnTapSoundOffset(s)){
            drawText(renderer, smallFont, TAP_SOUND_OFFSET_WARN_LINE1, WARN_COLOR, x + 40, descY + 34, a, Align::Left, textMaxW);
            drawText(renderer, smallFont, TAP_SOUND_OFFSET_WARN_LINE2, WARN_COLOR, x + 40, descY + 64, a, Align::Left, textMaxW);
        }
        // 操作方法：今の状態で使えるキーだけ出す
        const char* help;
        if(editing_)                       help = "左右:値を変更（Shift で大きく）   ENTER/ESC:変更をやめる   F2:閉じて保存";
        else if(isActionRow(currentRow())) help = "左右/Tab:分類   上下:選択   ENTER:開く   F2/ESC:閉じて保存";
        else                               help = "左右/Tab:分類   上下:選択   ENTER:変更する   F2/ESC:閉じて保存";
        drawText(renderer, smallFont, help, SDL_Color{130, 130, 150, 255}, x + 40, PANEL_Y + PANEL_H - 36, a, Align::Left, textMaxW);
    }

private:
    // ----- 画面サイズとレイアウト -----
    static constexpr int SW = 1920;
    static constexpr int SH = 1080;
    static constexpr int PANEL_X = 60;
    static constexpr int PANEL_Y = 120;   // 8項目入るように 140 → 120 に上げた
    static constexpr int PANEL_W = 960;   // 文字が収まるように 760 → 960 に広げた
    static constexpr int PANEL_H = 936;   // 項目が一番多い分類（8項目）に合わせた高さ。下端は 120 + 936 = 1056（画面は 1080）
    static constexpr int ROW_H   = 80;    // 8項目入るように 88 → 80 に詰めた
    static constexpr int TAB_H   = 50;    // 分類タブの高さ
    static constexpr int TAB_GAP = 10;    // 分類タブどうしの隙間
    static constexpr int ROWS_TOP = PANEL_Y + 24 + TAB_H + 24;   // 項目の並びの上端
    static constexpr int MAX_ROWS = 8;    // 1つの分類に入る項目の最大数（レイアウト用）
    static constexpr SDL_Color WARN_COLOR = {255, 170, 60, 255};   // 警告の色（橙）

    // ----- 文字がはみ出さないようにする幅 -----
    //   決めた幅に収まる文字はそのままの大きさ、はみ出す文字だけ、その幅にぴったり収まるまで縮めて描く（drawText の maxW）
    static constexpr double LABEL_MAX_RATIO = 0.45;   // 項目名に使ってよい幅（枠の内側の幅に対する割合）
    static constexpr int    LABEL_VALUE_GAP = 40;     // 項目名と値の間に、最低限空けるすき間(px)
    static constexpr int    TEXT_PAD        = 8;      // 分類タブの中の左右の余白(px)

    // ----- 項目 -----
    enum Row{
        ROW_CALIBRATE,
        ROW_OFFSET,
        ROW_VISUAL_OFFSET,
        ROW_BGM,
        ROW_SE,
        ROW_TAPSOUND,
        ROW_FASTLATE,
        ROW_TIMING_MS,
        ROW_VSYNC,
        ROW_FPS,
        ROW_WINDOW,
        ROW_KEYCONFIG,
        ROW_FPS_AUTO,
        ROW_NOTE_DETAIL,
        ROW_STREAM_MODE,
        ROW_BASS_ZOOM,
        ROW_LANE_ROLL,
        ROW_COUNT
    };

    static constexpr const char* ROW_LABELS[ROW_COUNT] = {
        "判定調整",
        "音声オフセット",
        "表示オフセット",
        "BGM 音量",
        "SE 音量",
        "ノーツ音",
        "FAST/LATE 表示",
        "±ms 表示",
        "垂直同期",
        "FPS 上限",
        "表示モード",
        "キー設定",
        "FPS 自動",
        "ノーツの詳細設定",
        "配信モード",
        "低音ズーム",
        "レーンの左右の傾き"
    };

    static constexpr const char* ROW_DESCRIPTIONS[ROW_COUNT] = {
        "ENTER で 音声調整→表示調整→確認 の順に調整します",
        "+ で判定が遅くなります",
        "+ でノーツが早めに表示されます",
        "BGMの割合を変更します",
        "SEの割合を変更します",
        "切り替えると試し聴きできます",
        "GOOD・BAD のとき、早ければ FAST、遅ければ LATE を出します",
        "叩いたズレを ms で出します（- = 早い、+ = 遅い）",
        "画面のちらつきを防ぎます（OFF にすると遅延が少し減ります）",
        "垂直同期が OFF のときに使われる上限設定です",
        "ウィンドウの設定です",
        "ENTER で キー設定の画面を開きます（キーボード・コントローラー）",
        "モニターの Hz に一番近い FPS 上限を自動で選びます",
        "ENTER で ノーツ速度・レーンの傾き・レーンカバー・キービームを、画面を見ながら変えられます",
        "OBS で録画・配信するとき ON（描き方の切り替えは、ゲームを起動し直すと反映）",
        "曲の低音（バスドラム・ベース）に合わせて、プレイ画面全体が少し大きくなります",
        "押しているロングが左右どちらに多いかで、レーンが左右に少し傾きます"
    };

    // ----- 分類 -----
    //   項目を増やすときは、ROW_xxx を作って、ここの好きな分類の行に足す（-1 は空き）
    static constexpr int CATEGORY_COUNT = 4;
    static constexpr const char* CATEGORY_NAMES[CATEGORY_COUNT] = {"オフセット", "ノーツ", "グラフィックス", "演出"};
    static constexpr int CATEGORY_ROWS[CATEGORY_COUNT][MAX_ROWS] = {
        //   FAST/LATE 表示・±ms 表示は、ノーツの分類に入りきらなくなったので「画面に出す物」としてグラフィックスへ移した
        {ROW_CALIBRATE,  ROW_OFFSET,    ROW_VISUAL_OFFSET, -1,            -1,           -1,           -1,      -1},   // オフセット関連
        //   ノーツ速度・レーンの傾き・レーンカバーは、ノーツの詳細設定（画面を見ながら変える）だけにした
        {ROW_NOTE_DETAIL, ROW_KEYCONFIG, ROW_TAPSOUND,     ROW_SE,        ROW_BGM,      -1,           -1,      -1},   // ノーツ関連
        {ROW_VSYNC,      ROW_FPS_AUTO,  ROW_FPS,           ROW_WINDOW,    ROW_STREAM_MODE, ROW_FASTLATE, ROW_TIMING_MS, -1},   // グラフィックス関連
        {ROW_BASS_ZOOM,  ROW_LANE_ROLL, -1,                -1,            -1,           -1,           -1,      -1},   // 演出（画面の動き）
    };

    // 分類 c に入っている項目の数（-1 の手前まで）
    static int categoryRowCount(int c){
        int n = 0;
        while(n < MAX_ROWS && CATEGORY_ROWS[c][n] >= 0) n++;
        return n;
    }

    static constexpr int FPS_OPTIONS[] ={0, 60, 120, 144, 165, 240, 360};   // 0 = 無制限（FramePacer.h の FPS_AUTO_CHOICES とそろえる）
    static constexpr int FPS_COUNT = static_cast<int>(sizeof(FPS_OPTIONS) / sizeof(FPS_OPTIONS[0]));

    static constexpr const char* WINDOW_MODE_NAMES[3] = {"ウィンドウ", "ボーダーレス", "フルスクリーン"};

    // ----- 状態 -----
    bool open_ = false;
    bool requestCalibration_ = false;
    bool requestKeyConfig_ = false;
    bool requestNoteSetting_ = false;
    int category_ = 0;   // 選んでいる分類（閉じても覚えておく）
    int cursor_ = 0;     // 分類の中で何番目の項目か

    // カーソルが指している項目の種類（ROW_xxx）
    int currentRow() const { return CATEGORY_ROWS[category_][cursor_]; }
    bool editing_ = false;   // true = 変更中（←→ で値を変える）、false = 選ぶとき（←→ で分類を変える）

    // 分類を切り替える（端まで行ったら反対側に戻る）。カーソルは分類の先頭に戻す
    void changeCategory(int dir){
        category_ = (category_ + dir + CATEGORY_COUNT) % CATEGORY_COUNT;
        cursor_ = 0;
    }

    // ENTER で別の画面を開く項目か（値を持たない）
    static bool isActionRow(int row){
        return row == ROW_CALIBRATE || row == ROW_KEYCONFIG || row == ROW_NOTE_DETAIL;
    }
    int openedWindowMode_ = 0;
    bool openedStreamMode_ = false;   // 開いたときの配信モード（閉じたときに変わっていれば表示モードを付け直す）
    double anim_ = 0.0;              // 0.0 = 隠れている、1.0 = 出きっている
    Mix_Chunk* previewTap_ = nullptr;
    std::vector<std::string> tapSounds_;   // 選べるノーツ音（先頭は標準 = 空文字）

    // その項目を今は変えられないか（FPS 自動が ON の間の FPS 上限）
    static bool isLocked(int row, const PlayerSettings& s){
        if(row == ROW_FPS) return s.fpsAuto;
        return false;
    }

    // 今の設定のノーツ音が、選択肢の何番目か（見つからなければ標準 = 0）
    int tapSoundIndex(const std::string& name) const{
        for(size_t i = 0; i < tapSounds_.size(); i++){
            if(tapSounds_[i] == name) return static_cast<int>(i);
        }
        return 0;
    }

    // 試し聴き：今の SE 音量で鳴らす
    void playPreview(const PlayerSettings& s){
        if(!previewTap_) return;
        Mix_VolumeChunk(previewTap_, volumeToMix(s.seVolume));
        Mix_PlayChannel(-1, previewTap_, 0);
    }

    static int volumeToMix(int percent){
        return static_cast<int>(percent * MIX_MAX_VOLUME / 100.0);
    }

    // 今の FPS 上限に一番近い選択肢の番号（設定ファイルに選択肢外の値が入っていても動くように）
    static int nearestFpsIndex(int fps){
        if(fps <= 0) return 0;
        int best = 1;
        for(int i = 1; i < FPS_COUNT; i++){
            if(std::abs(FPS_OPTIONS[i] - fps) < std::abs(FPS_OPTIONS[best] - fps)) best = i;
        }
        return best;
    }

    // 値を変更して、その場で反映できるものは反映する
    void changeValue(int dir, bool big, SDL_Renderer* renderer, PlayerSettings& s){
        switch(currentRow()){
            case ROW_OFFSET:
                s.offsetMs = std::clamp(s.offsetMs + dir * (big ? 10.0 : 1.0), -500.0, 500.0);
                break;

            case ROW_VISUAL_OFFSET:
                s.visualOffsetMs = std::clamp(s.visualOffsetMs + dir * (big ? 10.0 : 1.0), -300.0, 300.0);
                break;

            case ROW_BGM:
                s.bgmVolume = std::clamp(s.bgmVolume + dir * (big ? 10 : 1), 0, 100);
                Mix_VolumeMusic(volumeToMix(s.bgmVolume));
                break;

            case ROW_SE:
                s.seVolume = std::clamp(s.seVolume + dir * (big ? 10 : 1), 0, 100);
                playPreview(s);
                break;

            case ROW_TAPSOUND:{
                const int n = static_cast<int>(tapSounds_.size());
                if(n == 0) break;
                // 端まで行ったら反対側に戻る（音の数が多くても一周できる）
                int idx = (tapSoundIndex(s.tapSound) + dir + n) % n;
                s.tapSound = tapSounds_[idx];

                // 試し聴き用の音を差し替えて鳴らす
                if(previewTap_) Mix_FreeChunk(previewTap_);
                previewTap_ = loadTapSound(s.tapSound);
                playPreview(s);
                break;
            }

            case ROW_FASTLATE:
                s.showFastLate = !s.showFastLate;
                break;

            case ROW_TIMING_MS:
                s.showTimingMs = !s.showTimingMs;
                break;

            case ROW_BASS_ZOOM:
                s.bassZoom = !s.bassZoom;
                break;

            case ROW_LANE_ROLL:
                s.laneRoll = !s.laneRoll;
                break;

            case ROW_VSYNC:
                s.vsync = !s.vsync;
                SDL_RenderSetVSync(renderer, s.vsync ? 1 : 0);
                break;

            case ROW_FPS_AUTO:
                s.fpsAuto = !s.fpsAuto;
                // ON → その場でモニターの Hz を調べて上限を決める／OFF → 手動の FPS 上限に戻る
                //   changeValue には window が来ないので、renderer から取り出す
                g_framePacer.setAutoCap(s.fpsAuto, SDL_RenderGetWindow(renderer));
                break;

            case ROW_FPS:{
                if(s.fpsAuto) break;   // 自動の間は変えられない
                int idx = std::clamp(nearestFpsIndex(s.fpsCap) + dir, 0, FPS_COUNT - 1);
                s.fpsCap = FPS_OPTIONS[idx];
                g_framePacer.setCap(s.fpsCap);
                break;
            }

            case ROW_WINDOW:
                s.windowMode = std::clamp(s.windowMode + dir, 0, 2);   // 反映は close() で
                break;

            case ROW_STREAM_MODE:
                s.streamMode = !s.streamMode;   // 反映は close() で（表示モードと一緒に付け直す）
                break;

            default:
                break;
        }
    }

    // 各項目の値を表示用の文字列にする
    static std::string valueText(int row, const PlayerSettings& s){
        char buf[64];
        switch(row){
            case ROW_CALIBRATE: return "ENTER で開始";
            case ROW_KEYCONFIG: return "ENTER で開く";
            case ROW_NOTE_DETAIL: return "ENTER で開く";
            case ROW_OFFSET: std::snprintf(buf, sizeof(buf), "%+.0f ms", s.offsetMs); return buf;
            case ROW_VISUAL_OFFSET: std::snprintf(buf, sizeof(buf), "%+.0f ms", s.visualOffsetMs); return buf;
            case ROW_BGM:    std::snprintf(buf, sizeof(buf), "%d %%", s.bgmVolume);   return buf;
            case ROW_SE:     std::snprintf(buf, sizeof(buf), "%d %%", s.seVolume);    return buf;
            case ROW_TAPSOUND: return tapSoundDisplayName(s.tapSound);
            case ROW_FASTLATE:  return s.showFastLate ? "ON" : "OFF";
            case ROW_TIMING_MS: return s.showTimingMs ? "ON" : "OFF";
            case ROW_BASS_ZOOM: return s.bassZoom ? "ON" : "OFF";
            case ROW_LANE_ROLL: return s.laneRoll ? "ON" : "OFF";
            case ROW_VSYNC:  return s.vsync ? "ON" : "OFF";
            case ROW_FPS_AUTO:{
                if(!s.fpsAuto) return "OFF";
                // ON のときは、取得できたモニターの Hz も一緒に出す
                const int hz = g_framePacer.detectedHz();
                if(hz > 0) std::snprintf(buf, sizeof(buf), "ON（%d Hz）", hz);
                else if(hz == 0) std::snprintf(buf, sizeof(buf), "ON（Hz 取得不可）");
                else std::snprintf(buf, sizeof(buf), "ON");
                return buf;
            }

            case ROW_FPS:
                // 自動で決まった値があればそれを出す（Hz が取れないときは手動の値が使われる）
                if(s.fpsAuto && g_framePacer.autoCapFps() > 0){
                    std::snprintf(buf, sizeof(buf), "%d fps", g_framePacer.autoCapFps());
                    return buf;
                }
                if(s.fpsCap <= 0) return "無制限";
                std::snprintf(buf, sizeof(buf), "%d fps", s.fpsCap);
                return buf;
            case ROW_WINDOW:
                // 配信モードでは、フルスクリーンはボーダーレスで表示されるので、そのことも出す
                if(s.streamMode && s.windowMode == WINDOW_MODE_EXCLUSIVE) return "フルスクリーン（配信用）";
                return WINDOW_MODE_NAMES[std::clamp(s.windowMode, 0, 2)];
            case ROW_STREAM_MODE: return s.streamMode ? "ON" : "OFF";
            default:         return "";
        }
    }

    // ----- 描画の小道具 -----
    enum class Align{ Left, Center, Right };

    // 半透明の塗り＋枠線（選曲画面の枠と同じ見た目）
    static void drawFrame(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color fill, SDL_Color border, int thickness){
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        for(int t = 0; t < thickness; t++){
            SDL_Rect r = {rect.x + t, rect.y + t, rect.w - t * 2, rect.h - t * 2};
            SDL_RenderDrawRect(renderer, &r);
        }
    }

    // ウィンドウ上辺に乗せるタイトルタブ（HoloWindow.h のホログラムのタブ）
    static void drawTab(SDL_Renderer* renderer, TTF_Font* font, const char* title, const SDL_Rect& panel, Uint8 alpha){
        drawHoloWindowTab(renderer, font, title, panel, alpha);
    }

    // 文字を描く。y は文字の縦の中心、x は Left なら左端・Right なら右端・Center なら中心
    //   maxW > 0 のとき：文字の幅が maxW を超える場合だけ、縦横の比を保ったまま maxW に収まるまで縮めて描く
    //                    （収まる文字は元の大きさのまま。縮めたときは、なめらかに見えるよう線形補間で縮小する）
    //   戻り値：実際に描いた幅(px)。項目名の幅を測って、値に使える幅を決めるのに使う
    static int drawText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text,
                        SDL_Color color, int x, int y, Uint8 alpha, Align align, int maxW = 0){
        if(!font || text.empty()) return 0;
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
        if(!surf) return 0;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        if(!tex){
            SDL_FreeSurface(surf);
            return 0;
        }
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(tex, alpha);

        int w = surf->w;
        int h = surf->h;
        if(maxW > 0 && w > maxW){
            const double scale = static_cast<double>(maxW) / w;
            w = maxW;
            h = std::max(1, static_cast<int>(std::lround(surf->h * scale)));
            SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);   // 縮めた文字がガタガタにならないように
        }

        int left = x;
        if(align == Align::Right)  left = x - w;
        if(align == Align::Center) left = x - w / 2;
        SDL_Rect dst = {left, y - h / 2, w, h};   // 縦の中心は y のまま（縮めても行の真ん中にそろう）
        SDL_RenderCopy(renderer, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
        return w;
    }
};