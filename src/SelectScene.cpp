#include "GameCommon.h"
#include <nlohmann/json.hpp>
#include "Play/PlaySceneManager.h"
#include "JacketSpectrum.h"
#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <cctype>

#define SCREEN_W 1920
#define SCREEN_H 1080

struct PanelWindow{
    std::string title;
    double x = 0, y = 0, w = 0, h = 0;
    double titleAlpha = 0.0;     // タイトルタブの透明度 (0.0〜1.0)
    bool initialized = false;
};
struct SongInfo{
    std::string title;
    std::string scorePath;
    std::string jacket = "";
    double bpm  = 120.0;    //初期化するための一時的な定義
    std::string composer = "Unknown";
    std::string level = "0";
    std::string audioExtName = "";
    std::string ChartCreator = "Unknown";

    double currentScale = 1.0;
    double currentY = -1.0;
    double currentX = -1.0;
    bool positionInittailized = false;

    std::vector<DifficultyInfo> difficulties;
};

struct GenreInfo{
    std::string genreName;
    std::vector<SongInfo> songList;
    
    double currentScale = 1.0;
    double currentY = -1.0;
    double currentX = -1.0;
    bool positionInittailized = false;
    bool isTutorial = false;
};

enum class SelectMode{
    SelectGenre,
    SelectSong
};
struct JacketManager{
    std::unordered_map<std::string, SDL_Texture*> cache;  // 譜面パス → テクスチャ（読込失敗は nullptr）
    SDL_Texture* fallback = nullptr;   // 読めなかったとき用の専用ジャケット

    SDL_Texture* shown = nullptr;      // 今表示しているテクスチャ
    std::string shownKey = "";         // 今表示している曲の譜面パス（空 = まだ何も表示していない）
    double flip = 1.0;                 // めくりアニメの進み具合 (0.0〜1.0、1.0 = 停止中)
    double appear = 0.0;               // 出現アニメの進み具合 (0.0〜1.0)
    SDL_Rect lastPanel = {0, 0, 0, 0};
};

// 「プレイヤーが選んだ順番」を、その曲に存在する範囲に収める
inline int clampDifficulty(int preferred, size_t count){
    if(count == 0) return 0;
    return std::min(preferred, static_cast<int>(count) - 1);
}

//プロトタイプ宣言
std::vector<GenreInfo> scanScoreFolder(const std::string& baseDir);
void draw_GenreList(SDL_Renderer* renderer, TTF_Font* font, std::vector<GenreInfo>& categories,size_t genreCursor, bool isSongMode, SDL_Rect* outSelectedGenreRect);
void draw_SongList(SDL_Renderer* renderer, TTF_Font* font, std::vector<SongInfo>& songList, size_t songCursor, SelectMode currentMode, const SDL_Rect* priorityRect);
void draw_songDetail(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* labelFont, const SongInfo& song, double animation, const DifficultyInfo* diff, double chartAnim);
SongInfo parseScoreFile(const std::filesystem::path& filePath);
void draw_ListsInPanels(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* labelFont,
                        std::vector<GenreInfo>& categories, size_t genreCursor, size_t songCursor,
                        SelectMode currentMode, PanelWindow& genrePanel, PanelWindow& songPanel);
SDL_Texture* createFallbackJacket(SDL_Renderer* renderer, TTF_Font* font);
void draw_Jacket(SDL_Renderer* renderer, TTF_Font* labelFont, JacketManager& jm, const SongInfo& song, SelectMode currentMode);
void destroyJackets(JacketManager& jm);
void draw_DifficultyBadge(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* labelFont, const std::vector<DifficultyInfo>& diffs, int index, double animation, double slide);
void draw_JacketVisualizer(SDL_Renderer* renderer, const JacketManager& jm, const JacketSpectrum& viz);

//メイン関数
GameScene selectSongScene(SDL_Window* window, SDL_Renderer* renderer, std::string& outSelectedScorePath, int& outSelectedDifficulty, PlayerSettings& playerSettings, bool& outAutoplay, SDL_Texture* targetTex){
    std::vector<GenreInfo> categories = scanScoreFolder("scores");
    if(categories.empty()){
        std::cout << "[エラー]曲がねぇ\n";
        return GameScene::Shutdown;
    }

    {
        GenreInfo tutorial;
        tutorial.genreName = "チュートリアル";
        tutorial.isTutorial = true;

        SongInfo hint;                                  // 曲リストの描画が曲を1つ必要とするため
        hint.title = "ENTER で開始";
        hint.scorePath = "tutorial/tutorial.json";      // 台本のパス
        tutorial.songList.push_back(hint);

        categories.push_back(tutorial);                 // 最後に追加。先頭にするなら begin() へ insert
    }

    SelectMode currentMode = SelectMode::SelectGenre;
    int genreCursor = 0;
    int songCursor = 0;
    int difficultyCursor = 0;   // プレイヤーが選んだ難易度の「順番」（曲を移っても維持する）

    TTF_Font* font = TTF_OpenFont("fonts/prac.ttf", 40);
    TTF_Font* sub_font = TTF_OpenFont("fonts/prac.ttf", 35);
    TTF_Font* label_font = TTF_OpenFont("fonts/prac.ttf", 24);
    if(!font || !sub_font || !label_font){
        std::cout << "選曲画面のフォント読込失敗\n";
        return GameScene::Shutdown;
    }

    PanelWindow genrePanel;
    genrePanel.title = "GENRE";
    PanelWindow songPanel;
    songPanel.title = "MUSIC";

    Mix_Music* bgm = nullptr;
    std::string currentPlayPath = "";
    std::string dafaultBgmPath = "sounds/十番街、雨【シティ探索BGM】.mp3";
    Mix_VolumeMusic(70);

    double detailAnimation = 0.0;
    double chartAnimation = 0.0;   // 譜面製作者の行の表示量 (0.0〜1.0)
    double diffSlide = 0.0;        // 難易度切替時のスライド量 (-1.0〜1.0、0で停止)

    bool running = true;
    SDL_Event e;
    GameScene nextScene = GameScene::Shutdown;

    if(targetTex != nullptr){
        SDL_SetRenderTarget(renderer, targetTex);

        SDL_SetRenderDrawColor(renderer, 15, 15, 25, 255);
        SDL_RenderClear(renderer);

        draw_ListsInPanels(renderer, font, label_font, categories, genreCursor, songCursor,
                           currentMode, genrePanel, songPanel);
        
        SDL_SetRenderTarget(renderer, NULL);

        TTF_CloseFont(font);
        TTF_CloseFont(sub_font);
        TTF_CloseFont(label_font);
        return GameScene::Select;
    }

    JacketManager jackets;
    jackets.fallback = createFallbackJacket(renderer, font);

    JacketSpectrum visualizer(12);   // バーの本数（少ないほど1本が太くなる）
    visualizer.start();

    while(running){
        std::string targetMusicPath = dafaultBgmPath;

        if(currentMode == SelectMode::SelectSong){
            const SongInfo& currentSong = categories[genreCursor].songList[songCursor];

            if(!currentSong.audioExtName.empty() && std::filesystem::exists("sounds/" + currentSong.audioExtName)){
                targetMusicPath = "sounds/" + currentSong.audioExtName;
            }
            else{
                std::string songTitle = categories[genreCursor].songList[songCursor].title;
                std::string musicPath = "sounds/" + songTitle;
                if(std::filesystem::exists((musicPath + ".mp3"))){
                    targetMusicPath = musicPath + ".mp3";
                }
                else if(std::filesystem::exists((musicPath + ".wav"))){
                    targetMusicPath = musicPath + ".wav";
                }
            }

            detailAnimation += (1.0 - detailAnimation) * 0.12;
        }
        else{
            detailAnimation += (0.0 - detailAnimation) * 0.12;
        }

        // 曲選択中は譜面製作者の行も出す
        double chartTarget = (currentMode == SelectMode::SelectSong) ? 1.0 : 0.0;
        chartAnimation += (chartTarget - chartAnimation) * 0.15;
        diffSlide += (0.0 - diffSlide) * 0.2;

        if(targetMusicPath != currentPlayPath){
            if(bgm){
                Mix_HaltMusic();
                Mix_FreeMusic(bgm);
                bgm = nullptr;
            }

            bgm = Mix_LoadMUS(targetMusicPath.c_str());
            if(bgm){
                Mix_PlayMusic(bgm, -1);
                currentPlayPath = targetMusicPath;
            }
            else{
                std::cout << "[警告]音楽の読込失敗: " << targetMusicPath << "\n";
            }
        }

        while(SDL_PollEvent(&e) != 0){
            if(e.type == SDL_QUIT){
                running = false;
                nextScene = GameScene::Shutdown;
            }

            if(e.type == SDL_KEYDOWN){
                bool cursorMoved = false;

                switch(e.key.keysym.sym){
                    case SDLK_ESCAPE:{
                        if(currentMode == SelectMode::SelectSong){
                            currentMode = SelectMode::SelectGenre;
                            songCursor = 0;
                        }
                        else{
                            running = false;
                            nextScene = GameScene::Shutdown;
                        }
                        break;
                    }
                    
                    case SDLK_DOWN:{
                        if(currentMode == SelectMode::SelectGenre){
                            genreCursor = (genreCursor + 1 + categories.size()) % categories.size();
                        }
                        else if(currentMode == SelectMode::SelectSong){
                            int songCount = categories[genreCursor].songList.size();
                            songCursor = (songCursor + 1 + songCount) % songCount;
                            cursorMoved = true;
                        }
                        break;
                    }

                    case SDLK_UP:{
                        if(currentMode == SelectMode::SelectGenre){
                            genreCursor = (genreCursor - 1 + categories.size()) % categories.size();
                        }
                        else if(currentMode == SelectMode::SelectSong){
                            int songCount = categories[genreCursor].songList.size();
                            songCursor = (songCursor - 1 + songCount) % songCount;
                            cursorMoved = true;
                        }
                        break;
                    }

                    case SDLK_LEFT:
                    case SDLK_RIGHT:{
                        if(currentMode == SelectMode::SelectGenre) break;

                        const auto& diffs = categories[genreCursor].songList[songCursor].difficulties;
                        if(diffs.empty()) break;

                        bool toRight = (e.key.keysym.sym == SDLK_RIGHT);
                        int current = clampDifficulty(difficultyCursor, diffs.size());
                        int next = current + (toRight ? 1 : -1);
                        if(next < 0 || next >= static_cast<int>(diffs.size())) break;   // 端で止める

                        difficultyCursor = next;
                        diffSlide = toRight ? 1.0 : -1.0;   // 押した方向から次の難易度が入ってくる
                        break;
                    }

                    case SDLK_RETURN:{
                        if(currentMode == SelectMode::SelectGenre){
                            if(categories[genreCursor].isTutorial){
                                outSelectedScorePath = categories[genreCursor].songList[0].scorePath;
                                nextScene = GameScene::Tutorial;
                                running = false;
                                break;
                            }

                            currentMode = SelectMode::SelectSong;
                            songCursor = 0;
                            cursorMoved = true;
                        }
                        else if(currentMode == SelectMode::SelectSong){
                            const SongInfo& song = categories[genreCursor].songList[songCursor];
                            outSelectedScorePath = song.scorePath;

                            if(song.difficulties.empty()){
                                outSelectedDifficulty = 0;
                            }
                            else{
                                // ←→ で表示している難易度の、JSON 上の元の番号を渡す
                                int idx = clampDifficulty(difficultyCursor, song.difficulties.size());
                                outSelectedDifficulty = song.difficulties[idx].fileIndex;
                            }

                            SDL_Keymod mod = SDL_GetModState();
                            outAutoplay = (mod & KMOD_CTRL) != 0;   // Ctrl+ENTER でオートプレイ
                            nextScene = GameScene::Load;
                            running = false;
                        }
                        break;
                    }

                    case SDLK_F1:{
                        if(currentMode == SelectMode::SelectSong){
                            outSelectedScorePath = categories[genreCursor].songList[songCursor].scorePath;
                        }
                        else{
                            outSelectedScorePath = "";
                        }
                        nextScene = GameScene::ChartCreate;
                        running = false;
                        break;
                    }

                    case SDLK_F2:{
                        nextScene = GameScene::Setting;
                        running = false;
                        break;
                    }
                }
                
                if(cursorMoved && currentMode == SelectMode::SelectSong){
                    detailAnimation = 0.2;
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 15, 15, 25, 255);
        SDL_RenderClear(renderer);

        draw_ListsInPanels(renderer, font, label_font, categories, genreCursor, songCursor,
                           currentMode, genrePanel, songPanel);
        draw_Jacket(renderer, label_font, jackets, categories[genreCursor].songList[songCursor], currentMode);

        visualizer.update();
        draw_JacketVisualizer(renderer, jackets, visualizer);

        if(detailAnimation > 0.001){
            const SongInfo& song = categories[genreCursor].songList[songCursor];
            int diffIndex = clampDifficulty(difficultyCursor, song.difficulties.size());
            const DifficultyInfo* diff = song.difficulties.empty() ? nullptr : &song.difficulties[diffIndex];

            draw_DifficultyBadge(renderer, font, label_font, song.difficulties, diffIndex, detailAnimation, diffSlide);
            draw_songDetail(renderer, font, label_font, song, detailAnimation, diff, chartAnimation);
        }

        SDL_RenderPresent(renderer);

        SDL_Delay(8);
    }

    visualizer.stop();   // BGM を止める前に音声の受け取りを止める
    if(bgm){
        Mix_HaltMusic();
        Mix_FreeMusic(bgm);
    }

    destroyJackets(jackets);
    TTF_CloseFont(font);
    TTF_CloseFont(sub_font);
    TTF_CloseFont(label_font);
    return nextScene;
}


//関数群
std::vector<GenreInfo> scanScoreFolder(const std::string& baseDir){
    std::vector<GenreInfo> categories;
    namespace fs = std::filesystem;

    if(!fs::exists(baseDir) || !fs::is_directory(baseDir)){
        std::cout << "[エラー]フォルダが見つからない:" << baseDir << "\n";
        return categories;
    }

    for(const auto& entry : fs::directory_iterator(baseDir)){
        if(entry.is_directory()){
            GenreInfo genre;
            genre.genreName = entry.path().filename().string();

            for(const auto& subEntry : fs::directory_iterator(entry.path())){
                if(subEntry.is_regular_file() && subEntry.path().extension() == ".json"){
                    SongInfo song = parseScoreFile(subEntry.path());
                    genre.songList.push_back(song);
                }
            }

            if(!genre.songList.empty()){
                categories.push_back(genre);
            }
        }
    }

    return categories;
}

static const SDL_Color FRAME_FILL_NORMAL     = { 30,  30,  50, 170};  // 通常項目の塗り
static const SDL_Color FRAME_FILL_ACTIVE     = { 45,  45,  80, 210};  // 選択中の塗り
static const SDL_Color FRAME_FILL_INACTIVE   = { 25,  25,  35, 110};  // プレビュー（灰色表示）時の塗り
static const SDL_Color FRAME_BORDER_NORMAL   = {110, 110, 150, 255};  // 通常項目の枠線
static const SDL_Color FRAME_BORDER_INACTIVE = { 70,  70,  85, 255};  // プレビュー時の枠線

static const int FRAME_PAD_X = 20;          // 文字と枠の左右の余白
static const int FRAME_PAD_Y = 6;           // 文字と枠の上下の余白
static const int GENRE_FRAME_MIN_W = 560;   // ジャンル枠の最小幅
static const int SONG_FRAME_MIN_W  = 900;   // 楽曲枠の最小幅

// 文字の描画矩形から、余白を足した枠の矩形を作る
static SDL_Rect makeFrameRect(const SDL_Rect& textRect, int minWidth){
    int w = std::max(minWidth, textRect.w + FRAME_PAD_X * 2);
    return SDL_Rect{textRect.x - FRAME_PAD_X, textRect.y - FRAME_PAD_Y, w, textRect.h + FRAME_PAD_Y * 2};
}

// 半透明の塗り＋枠線を描く。thickness で枠線の太さ(px)を指定
static void drawItemFrame(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color fill, SDL_Color border, int thickness){
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);   // アルファを有効にする

    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &rect);

    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    for(int t = 0; t < thickness; t++){
        SDL_Rect r = {rect.x + t, rect.y + t, rect.w - t * 2, rect.h - t * 2};  // 内側に1pxずつずらして重ね描き
        SDL_RenderDrawRect(renderer, &r);
    }
}

// ===== 一覧ウィンドウまわり =====
static const SDL_Color PANEL_FILL          = { 10,  10,  20, 190};
static const SDL_Color PANEL_BORDER_FOCUS  = {  0, 200, 255, 255};  // 操作中のウィンドウ
static const SDL_Color PANEL_BORDER_NORMAL = { 90,  90, 120, 255};  // それ以外
static const int PANEL_BORDER_W = 3;

// 各モードでのウィンドウの目標矩形 {x, y, w, h}
static const SDL_Rect GENRE_PANEL_LIST   = {150, 140,  620, 800};  // ジャンル選択中：一覧全体
static const SDL_Rect GENRE_PANEL_HEADER = { 70,  88,  720, 108};  // 曲選択中：選んだジャンルだけ
static const SDL_Rect SONG_PANEL_PREVIEW = {860, 140,  960, 800};  // ジャンル選択中：右側プレビュー
static const SDL_Rect SONG_PANEL_LIST    = {150, 236,  960, 764};  // 曲選択中：一覧全体

static SDL_Rect panelRect(const PanelWindow& p){
    return SDL_Rect{static_cast<int>(p.x), static_cast<int>(p.y), static_cast<int>(p.w), static_cast<int>(p.h)};
}

// 目標に向かって位置・サイズ・タイトル透明度を近づける（項目と同じ方式のイージング）
static void updatePanel(PanelWindow& p, const SDL_Rect& target, double targetTitleAlpha){
    if(!p.initialized){     // 初回は目標位置にいきなり置く
        p.x = target.x;  p.y = target.y;  p.w = target.w;  p.h = target.h;
        p.titleAlpha = targetTitleAlpha;
        p.initialized = true;
        return;
    }
    const double k = 0.15;
    p.x += (target.x - p.x) * k;
    p.y += (target.y - p.y) * k;
    p.w += (target.w - p.w) * k;
    p.h += (target.h - p.h) * k;
    p.titleAlpha += (targetTitleAlpha - p.titleAlpha) * k;
}

// ウィンドウ本体とタイトルタブを描く
static void drawPanelBack(SDL_Renderer* renderer, TTF_Font* labelFont, const PanelWindow& p, bool focused, double alphaMul = 1.0){
    SDL_Rect r = panelRect(p);
    SDL_Color border = focused ? PANEL_BORDER_FOCUS : PANEL_BORDER_NORMAL;
    SDL_Color fill = PANEL_FILL;
    border.a = static_cast<Uint8>(border.a * alphaMul);
    fill.a   = static_cast<Uint8>(fill.a * alphaMul);

    // タイトルタブ：ウィンドウ上辺の左端に乗せる
    if(p.titleAlpha > 0.01 && labelFont){
        SDL_Surface* surf = TTF_RenderUTF8_Blended(labelFont, p.title.c_str(), SDL_Color{15, 15, 25, 255});
        if(surf){
            Uint8 a = static_cast<Uint8>(p.titleAlpha * 255);
            SDL_Rect tab = {r.x, r.y - surf->h - 6, surf->w + 28, surf->h + 6};

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, a);
            SDL_RenderFillRect(renderer, &tab);

            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(tex, a);
            SDL_Rect textRect = {tab.x + 14, tab.y + 3, surf->w, surf->h};
            SDL_RenderCopy(renderer, tex, NULL, &textRect);

            SDL_DestroyTexture(tex);
            SDL_FreeSurface(surf);
        }
    }

    drawItemFrame(renderer, r, fill, border, PANEL_BORDER_W);
}

// ウィンドウの内側（枠線の内側）だけを描画可能にする
static void setPanelClip(SDL_Renderer* renderer, const PanelWindow& p){
    SDL_Rect r = panelRect(p);
    SDL_Rect inner = {r.x + PANEL_BORDER_W, r.y + PANEL_BORDER_W,
                      r.w - PANEL_BORDER_W * 2, r.h - PANEL_BORDER_W * 2};
    SDL_RenderSetClipRect(renderer, &inner);
}

void draw_ListsInPanels(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* labelFont,
                        std::vector<GenreInfo>& categories, size_t genreCursor, size_t songCursor,
                        SelectMode currentMode, PanelWindow& genrePanel, PanelWindow& songPanel){
    // 1. モードごとに目標を決めて、ウィンドウを動かす
    SDL_Rect genreTarget, songTarget;
    double genreTitleA, songTitleA;
    if(currentMode == SelectMode::SelectGenre){
        genreTarget = GENRE_PANEL_LIST;    genreTitleA = 1.0;
        songTarget  = SONG_PANEL_PREVIEW;  songTitleA  = 1.0;
    }
    else{
        genreTarget = GENRE_PANEL_HEADER;  genreTitleA = 0.0;
        songTarget  = SONG_PANEL_LIST;     songTitleA  = 1.0;
    }
    updatePanel(genrePanel, genreTarget, genreTitleA);
    updatePanel(songPanel,  songTarget,  songTitleA);

    bool genreCollapsed = (currentMode != SelectMode::SelectGenre);
    SDL_Rect selectedGenreRect = {0, 0, 0, 0};

    // 2. ジャンル
    drawPanelBack(renderer, labelFont, genrePanel, currentMode == SelectMode::SelectGenre);
    setPanelClip(renderer, genrePanel);
    draw_GenreList(renderer, font, categories, genreCursor, genreCollapsed, &selectedGenreRect);
    SDL_RenderSetClipRect(renderer, nullptr);

    // 3. 楽曲
    drawPanelBack(renderer, labelFont, songPanel, currentMode == SelectMode::SelectSong);
    setPanelClip(renderer, songPanel);
    draw_SongList(renderer, font, categories[genreCursor].songList, songCursor, currentMode,
                  genreCollapsed ? &selectedGenreRect : nullptr);
    SDL_RenderSetClipRect(renderer, nullptr);
}

void draw_GenreList(SDL_Renderer* renderer, TTF_Font* font, std::vector<GenreInfo>& categories,size_t genreCursor, bool isSongMode, SDL_Rect* outSelectedGenreRect){
    int lineGap = 80;
    int centerY = SCREEN_H / 2;

    const double selectedGenreTargetX = 100.0;
    const double selectedGenreTargetY = 120.0;
    const double offscreenGenreTargetX = -800.0;

    for(size_t i = 0; i < categories.size(); i++){
        double targetScale = (i == genreCursor) ? 1.4 : 1.0;

        categories[i].currentScale += (targetScale - categories[i].currentScale) * 0.15;

        double targetX;
        double targetY;

        if(!isSongMode){
            targetX = 200.0;
            targetY = centerY + (static_cast<double>(i) - static_cast<double>(genreCursor)) * lineGap;
        }
        else{
            if(i == genreCursor){
                targetX = selectedGenreTargetX;
                targetY = selectedGenreTargetY;
            }
            else{
                targetX = offscreenGenreTargetX;
                targetY = centerY + (static_cast<double>(i) - static_cast<double>(genreCursor)) * lineGap;
            }
        }

        if(!categories[i].positionInittailized){
            categories[i].currentX = targetX;
            categories[i].currentY = targetY;
            categories[i].positionInittailized = true;
        }
        else{
            categories[i].currentX += (targetX - categories[i].currentX) * 0.15;
            categories[i].currentY += (targetY - categories[i].currentY) * 0.15;
        }

        SDL_Color textColor = (i == genreCursor) ? SDL_Color{255, 215, 0, 255} : SDL_Color{255, 255, 255, 255};

        std::string displayName = categories[i].genreName;
        if(i == genreCursor){
            displayName = ">> " + displayName;
        }

        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, displayName.c_str(), textColor);
        if(!surf) continue;

        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_Rect destRect;
        destRect.w = static_cast<int>(surf->w * categories[i].currentScale);
        destRect.h = static_cast<int>(surf->h * categories[i].currentScale);

        destRect.x = static_cast<int>(categories[i].currentX);

        int baseY = static_cast<int>(categories[i].currentY);
        destRect.y =baseY - (destRect.h - surf->h) / 2;

        bool isCursor = (i == genreCursor);
        SDL_Rect frameRect = makeFrameRect(destRect, GENRE_FRAME_MIN_W);
        drawItemFrame(renderer, frameRect,
                      isCursor ? FRAME_FILL_ACTIVE : FRAME_FILL_NORMAL,
                      isCursor ? textColor : FRAME_BORDER_NORMAL,
                      isCursor ? 3 : 1);

        SDL_RenderCopy(renderer, tex, NULL, &destRect);

        if(isSongMode && isCursor && outSelectedGenreRect != nullptr){
            *outSelectedGenreRect = frameRect;
        }

        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }
}

void draw_SongList(SDL_Renderer* renderer, TTF_Font* font, std::vector<SongInfo>& songList, size_t songCursor, SelectMode currentMode, const SDL_Rect* priorityRect){
    int lineGap = 80;
    int centerY = SCREEN_H / 2;

    for(size_t i = 0; i < songList.size(); i++){
        bool isCursor = (i == songCursor);

        double targetScale;
        double targetX;
        double targetY;
        bool colorActive;

        if(currentMode == SelectMode::SelectGenre){
            targetScale = 1.0;
            targetX = 900.0;
            targetY = centerY + (static_cast<double>(i) - static_cast<double>(songCursor)) * lineGap;
            colorActive = false;
        }
        else{
            targetScale = isCursor ? 1.3 : 1.0;
            targetX = 200.0;
            targetY = centerY + (static_cast<double>(i) - static_cast<double>(songCursor)) * lineGap;
            colorActive = true;
        }

        songList[i].currentScale += (targetScale - songList[i].currentScale) * 0.15;

        if(!songList[i].positionInittailized){
            songList[i].currentX = targetX;
            songList[i].currentY = targetY;
            songList[i].positionInittailized = true;
        }
        else{
            songList[i].currentX += (targetX - songList[i].currentX) * 0.15;
            songList[i].currentY += (targetY - songList[i].currentY) * 0.15;
        }

        SDL_Color textColor = {150, 150, 150, 255};
        if(colorActive){
            textColor = isCursor ? SDL_Color{0, 255, 255, 255} : SDL_Color{255, 255, 255, 255};
        }

        std::string displayName = songList[i].title;
        if(i == songCursor && colorActive){
            displayName = ">> " + displayName; 
        }

        SDL_Surface* surf  = TTF_RenderUTF8_Blended(font, displayName.c_str(), textColor);
        if(!surf) continue;

        SDL_Rect destRect;
        destRect.w = static_cast<int>(surf->w * songList[i].currentScale);
        destRect.h = static_cast<int>(surf->h * songList[i].currentScale);
        destRect.x = static_cast<int>(songList[i].currentX);

        int baseY = static_cast<int>(songList[i].currentY);
        destRect.y = baseY - (destRect.h - surf->h) / 2 - destRect.h / 2;

        SDL_Rect frameRect = makeFrameRect(destRect, SONG_FRAME_MIN_W);

        // 上に退避したジャンル枠と重なる項目は描かない
        if(priorityRect != nullptr && SDL_HasIntersection(&frameRect, priorityRect)){
            SDL_FreeSurface(surf);
            continue;
        }

        // 枠の色を状態で切り替え
        bool highlight = isCursor && colorActive;
        SDL_Color fill, border;
        if(!colorActive){            // ジャンル選択中のプレビュー（灰色）
            fill = FRAME_FILL_INACTIVE;  border = FRAME_BORDER_INACTIVE;
        }
        else if(highlight){          // カーソル位置
            fill = FRAME_FILL_ACTIVE;    border = textColor;
        }
        else{
            fill = FRAME_FILL_NORMAL;    border = FRAME_BORDER_NORMAL;
        }
        drawItemFrame(renderer, frameRect, fill, border, highlight ? 3 : 1);

        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_RenderCopy(renderer, tex, NULL, &destRect);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }
}

static const int INFO_PANEL_MIN_W = 520;   // 情報ウィンドウの最小幅
static const int INFO_PANEL_PAD_X = 30;    // 文字とウィンドウの左右の余白
static const int INFO_PANEL_PAD_Y = 20;    // 文字とウィンドウの上下の余白

void draw_songDetail(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* labelFont, const SongInfo& song, double animation,
                     const DifficultyInfo* diff, double chartAnim){
    int alpha = static_cast<int>(animation * 255);
    int offsetX = static_cast<int>((1.0 - animation) * 100);

    int baseX = 1350 + offsetX;
    int baseY = 896;
    int lineGap = 50;

    // 各行の「文字・色・表示量」。表示量 0.0〜1.0 で行の高さと透明度が変わる
    struct InfoLine{
        std::string text;
        SDL_Color color;
        double anim;
    };
    std::vector<InfoLine> infoLines = {
        {"COMPOSER: " + song.composer, {255, 255, 255, 255}, 1.0}
    };

    // 譜面製作者の行を追加（難易度ごとの指定 → 無ければ曲全体の指定）
    if(diff != nullptr && chartAnim > 0.001){
        std::string creator = diff->chartCreator.empty() ? song.ChartCreator : diff->chartCreator;
        infoLines.push_back({"CHART: " + creator, {0, 255, 255, 255}, chartAnim});
    }

    // 1. 全行をサーフェスにして、幅と高さを測る
    std::vector<SDL_Surface*> surfs;
    int maxW = 0;
    int firstH = 0;
    for(size_t i = 0; i < infoLines.size(); i++){
        SDL_Surface* s = TTF_RenderUTF8_Blended(font, infoLines[i].text.c_str(), infoLines[i].color);
        surfs.push_back(s);
        if(s){
            maxW = std::max(maxW, s->w);
            if(i == 0) firstH = s->h;
        }
    }

    // ウィンドウの高さ：1行目 ＋ 2行目以降は「行間 × 表示量」だけ伸ばす
    double extraH = 0.0;
    for(size_t i = 1; i < infoLines.size(); i++){
        extraH += lineGap * infoLines[i].anim;
    }

    // 2. ウィンドウ
    PanelWindow info;
    info.title = "INFO";
    info.x = baseX - INFO_PANEL_PAD_X;
    info.y = baseY - INFO_PANEL_PAD_Y;
    info.w = std::max(INFO_PANEL_MIN_W, maxW + INFO_PANEL_PAD_X * 2);
    info.h = firstH + static_cast<int>(extraH) + INFO_PANEL_PAD_Y * 2;
    info.titleAlpha = animation;
    drawPanelBack(renderer, labelFont, info, false, animation);

    // 3. 文字：伸びている途中のウィンドウからはみ出さないようにクリップ
    setPanelClip(renderer, info);
    for(size_t i = 0; i < surfs.size(); i++){
        SDL_Surface* surf = surfs[i];
        if(!surf) continue;

        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(alpha * infoLines[i].anim));

        SDL_Rect destrect = {baseX, baseY + static_cast<int>(i * lineGap), surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, NULL, &destrect);

        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }
    SDL_RenderSetClipRect(renderer, nullptr);
}

// ===== 難易度の種類（色と並び順をまとめて管理） =====
struct DifficultyRule{
    const char* keyword;
    SDL_Color color;
    int rank;        // 小さいほど前に並ぶ
};

// 上から順に調べ、最初に含まれていたキーワードを採用する
// （"HARD MASTER" のように2つ含む場合は上の MASTER が優先）
static const DifficultyRule DIFFICULTY_RULES[] = {
    {"MASTER", {200, 110, 255, 255}, 3},
    {"HARD",   {255, 170,  60, 255}, 2},
    {"NORMAL", { 80, 170, 255, 255}, 1},
    {"EASY",   { 80, 220, 120, 255}, 0},
};
static const int       SPECIAL_RANK  = 4;                       // どれも含まない = 特殊
static const SDL_Color SPECIAL_COLOR = {255, 255, 255, 255};

// 名前に含まれるキーワードの規則を探す。見つからなければ nullptr（特殊）
static const DifficultyRule* findDifficultyRule(const std::string& name){
    std::string up = name;
    for(char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    for(const auto& r : DIFFICULTY_RULES){
        if(up.find(r.keyword) != std::string::npos) return &r;
    }
    return nullptr;
}

static SDL_Color difficultyColor(const std::string& name){
    const DifficultyRule* r = findDifficultyRule(name);
    return r ? r->color : SPECIAL_COLOR;
}

static int difficultyRank(const std::string& name){
    const DifficultyRule* r = findDifficultyRule(name);
    return r ? r->rank : SPECIAL_RANK;
}

SongInfo parseScoreFile(const std::filesystem::path& filePath){
    SongInfo song;
    std::u8string u8 = filePath.u8string();
    song.scorePath.assign(reinterpret_cast<const char*>(u8.data()), u8.size());
    song.title = filePath.stem().string();

    std::ifstream file(filePath);
    if(!file.is_open()){
        return song;
    }

    nlohmann::json j;
    try{
        file >> j;
    }
    catch(const nlohmann::json::parse_error& e){
        printf("譜面データ読み込み失敗 : %s\n", e.what());
        return song;
    }

    song.audioExtName = j.value("bgm", std::string(""));
    song.bpm = j.value("bpm", 120.0);
    song.composer = j.value("composer", std::string("Unknown"));
    song.level = j.value("level", std::string("0"));
    song.ChartCreator = j.value("chartCreator", std::string("Unknown"));
    song.jacket = j.value("jacket", std::string(""));

    song.difficulties = listDifficulties(song.scorePath);

    // EASY → NORMAL → HARD → MASTER → 特殊 の順に並べ替える（同じ種類どうしは JSON の順を保つ）
    std::stable_sort(song.difficulties.begin(), song.difficulties.end(),
        [](const DifficultyInfo& a, const DifficultyInfo& b){
            return difficultyRank(a.name) < difficultyRank(b.name);
        });

    return song;
}

// ===== ジャケット =====
static const char*  JACKET_FALLBACK_PATH = "jackets/default.png";  // 専用のジャケット絵
static const int    JACKET_SIZE       = 480;     // ジャケットの表示サイズ（正方形の枠）
static const int    JACKET_X          = 1340;    // ジャケット左上（INFO ウィンドウと左端を揃える）
static const int    JACKET_Y          = 180;
static const int    JACKET_PANEL_PAD  = 20;      // ジャケットとウィンドウの余白
static const double JACKET_FLIP_SPEED = 0.08;    // めくりの速さ（1フレームの進み量）
static const double JACKET_PI         = 3.14159265358979;

// 読めなかったとき用のジャケットを用意する
SDL_Texture* createFallbackJacket(SDL_Renderer* renderer, TTF_Font* font){
    // 1. 専用画像があればそれを使う
    SDL_Texture* tex = IMG_LoadTexture(renderer, JACKET_FALLBACK_PATH);
    if(tex) return tex;

    // 2. 無ければサーフェス上に「NO IMAGE」の絵を描いて作る
    const int S = 512;
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, S, S, 32, SDL_PIXELFORMAT_RGBA32);
    if(!surf) return nullptr;

    // 縦グラデーションの背景
    for(int y = 0; y < S; y += 4){
        double t = static_cast<double>(y) / S;
        Uint32 c = SDL_MapRGBA(surf->format,
                               static_cast<Uint8>(20 + 20 * t),
                               static_cast<Uint8>(20 + 10 * t),
                               static_cast<Uint8>(45 + 50 * t), 255);
        SDL_Rect row = {0, y, S, 4};
        SDL_FillRect(surf, &row, c);
    }

    // 枠線（上下左右に太さ6pxの帯）
    Uint32 bc = SDL_MapRGBA(surf->format, 0, 200, 255, 255);
    const int B = 6;
    SDL_Rect edges[4] = {{0, 0, S, B}, {0, S - B, S, B}, {0, 0, B, S}, {S - B, 0, B, S}};
    for(auto& r : edges) SDL_FillRect(surf, &r, bc);

    // 中央に文字
    SDL_Surface* text = TTF_RenderUTF8_Blended(font, "NO IMAGE", SDL_Color{200, 200, 220, 255});
    if(text){
        SDL_Rect dst = {(S - text->w) / 2, (S - text->h) / 2, text->w, text->h};
        SDL_BlitSurface(text, NULL, surf, &dst);
        SDL_FreeSurface(text);
    }

    tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    return tex;
}

// 曲のジャケットを候補パスから順に読み込む。見つからなければ nullptr
static SDL_Texture* loadJacketFor(SDL_Renderer* renderer, const SongInfo& song){
    std::vector<std::string> candidates;
    if(!song.jacket.empty()) candidates.push_back("jackets/" + song.jacket);
    candidates.push_back("jackets/" + song.title + ".png");
    candidates.push_back("jackets/" + song.title + ".jpg");

    for(const auto& path : candidates){
        SDL_Texture* t = IMG_LoadTexture(renderer, path.c_str());
        if(t) return t;
    }
    std::cout << "[情報]ジャケットが見つからない: " << song.title << "\n";
    return nullptr;
}

// キャッシュから取り出す。初めての曲だけ読み込む。読めなかった曲は専用ジャケットを返す
static SDL_Texture* getJacket(JacketManager& jm, SDL_Renderer* renderer, const SongInfo& song){
    auto it = jm.cache.find(song.scorePath);
    if(it == jm.cache.end()){
        it = jm.cache.emplace(song.scorePath, loadJacketFor(renderer, song)).first;
    }
    return it->second ? it->second : jm.fallback;
}

void draw_Jacket(SDL_Renderer* renderer, TTF_Font* labelFont, JacketManager& jm, const SongInfo& song, SelectMode currentMode){
    // 1. 出現アニメ：曲選択中だけ表示
    bool visible = (currentMode != SelectMode::SelectGenre);
    jm.appear += ((visible ? 1.0 : 0.0) - jm.appear) * 0.15;

    if(jm.appear < 0.001){
        // 完全に隠れたら状態をリセット（次に出るときはめくらずにそのまま表示）
        jm.shownKey.clear();
        jm.shown = nullptr;
        jm.flip = 1.0;
        return;
    }

    // 2. めくりアニメ：表示中の曲と違う曲になったら開始
    if(visible){
        SDL_Texture* target = getJacket(jm, renderer, song);

        if(jm.shownKey.empty()){                 // 最初の1枚はそのまま表示
            jm.shown = target;
            jm.shownKey = song.scorePath;
        }
        else if(song.scorePath != jm.shownKey && jm.flip >= 1.0){
            jm.flip = 0.0;
        }

        if(jm.flip < 1.0){
            jm.flip += JACKET_FLIP_SPEED;
            if(jm.flip >= 0.5 && jm.shownKey != song.scorePath){   // 真横を向いた瞬間に絵を差し替え
                jm.shown = target;
                jm.shownKey = song.scorePath;
            }
            if(jm.flip > 1.0) jm.flip = 1.0;
        }
    }

    // 3. 位置：右からスライドイン ＋ ふわふわ上下
    int offsetX = static_cast<int>((1.0 - jm.appear) * 150);
    int floatY  = static_cast<int>(std::sin(SDL_GetTicks() * 0.002) * 6.0);
    int left = JACKET_X + offsetX;
    int top  = JACKET_Y + floatY;

    // 4. ウィンドウ
    PanelWindow panel;
    panel.title = "JACKET";
    panel.x = left - JACKET_PANEL_PAD;
    panel.y = top - JACKET_PANEL_PAD;
    panel.w = JACKET_SIZE + JACKET_PANEL_PAD * 2;
    panel.h = JACKET_SIZE + JACKET_PANEL_PAD * 2;
    panel.titleAlpha = jm.appear;
    drawPanelBack(renderer, labelFont, panel, false, jm.appear);
    jm.lastPanel = panelRect(panel);

    if(!jm.shown) return;

    // 5. ジャケット：縦横比を保って枠に収め、めくり中は横幅を cos で縮める
    int texW = 0, texH = 0;
    SDL_QueryTexture(jm.shown, NULL, NULL, &texW, &texH);
    if(texW <= 0 || texH <= 0) return;

    double fit = std::min(static_cast<double>(JACKET_SIZE) / texW, static_cast<double>(JACKET_SIZE) / texH);
    int drawW = static_cast<int>(texW * fit);
    int drawH = static_cast<int>(texH * fit);

    double widthScale = (jm.flip < 1.0) ? std::fabs(std::cos(JACKET_PI * jm.flip)) : 1.0;
    int flipW = static_cast<int>(drawW * widthScale);

    int cx = left + JACKET_SIZE / 2;
    int cy = top + JACKET_SIZE / 2;
    SDL_Rect dst = {cx - flipW / 2, cy - drawH / 2, flipW, drawH};

    SDL_SetTextureBlendMode(jm.shown, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(jm.shown, static_cast<Uint8>(jm.appear * 255));
    SDL_RenderCopy(renderer, jm.shown, NULL, &dst);
}

// キャッシュしたテクスチャをすべて解放する
void destroyJackets(JacketManager& jm){
    for(auto& kv : jm.cache){
        if(kv.second) SDL_DestroyTexture(kv.second);
    }
    jm.cache.clear();
    if(jm.fallback){
        SDL_DestroyTexture(jm.fallback);
        jm.fallback = nullptr;
    }
    jm.shown = nullptr;
}

// ===== 難易度（1つだけ表示） =====
static const int BADGE_X = 1320;   // INFO / JACKET ウィンドウと左端を揃える
static const int BADGE_Y = 730;
static const int BADGE_W = 520;
static const int BADGE_H = 90;

// 文字を描く。centerX が true なら x を中心として描く。縦は y を中心にする
static void drawTextCentered(SDL_Renderer* renderer, TTF_Font* font, const std::string& text,
                             SDL_Color color, int x, int y, Uint8 alpha, bool centerX){
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if(!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(tex, alpha);

    SDL_Rect dst = {centerX ? x - surf->w / 2 : x, y - surf->h / 2, surf->w, surf->h};
    SDL_RenderCopy(renderer, tex, NULL, &dst);

    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

void draw_DifficultyBadge(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* labelFont,
                          const std::vector<DifficultyInfo>& diffs, int index, double animation, double slide){
    if(diffs.empty()) return;
    const DifficultyInfo& d = diffs[index];

    Uint8 alpha = static_cast<Uint8>(animation * 255);
    int offsetX = static_cast<int>((1.0 - animation) * 100);   // INFO と同じスライドイン
    int left = BADGE_X + offsetX;
    int cy = BADGE_Y + BADGE_H / 2;

    // 1. ウィンドウ ＋ 難易度色の枠線を上書き
    PanelWindow panel;
    panel.title = "DIFFICULTY";
    panel.x = left;
    panel.y = BADGE_Y;
    panel.w = BADGE_W;
    panel.h = BADGE_H;
    panel.titleAlpha = animation;
    drawPanelBack(renderer, labelFont, panel, false, animation);

    SDL_Color dc = difficultyColor(d.name);
    SDL_Color border = dc;
    border.a = alpha;
    drawItemFrame(renderer, panelRect(panel), SDL_Color{0, 0, 0, 0}, border, PANEL_BORDER_W);

    // 2. 左右の矢印：これ以上進めない側は薄くする
    bool canLeft  = (index > 0);
    bool canRight = (index < static_cast<int>(diffs.size()) - 1);
    SDL_Color arrowColor = {255, 255, 255, 255};
    drawTextCentered(renderer, font, "<", arrowColor, left + 35, cy,
                     canLeft ? alpha : static_cast<Uint8>(alpha * 0.2), true);
    drawTextCentered(renderer, font, ">", arrowColor, left + BADGE_W - 35, cy,
                     canRight ? alpha : static_cast<Uint8>(alpha * 0.2), true);

    // 3. 難易度名とレベル：切替直後は押した方向からスライドしながらフェードイン
    int textOffset = static_cast<int>(slide * 60);
    Uint8 textAlpha = static_cast<Uint8>(alpha * (1.0 - std::fabs(slide)));
    std::string label = d.name + "   Lv." + d.level;

    setPanelClip(renderer, panel);   // スライド中に矢印の外へはみ出さないように
    drawTextCentered(renderer, font, label, dc, left + BADGE_W / 2 + textOffset, cy, textAlpha, true);
    SDL_RenderSetClipRect(renderer, nullptr);
}

// ===== ジャケットのビジュアライザー =====
static const int       VIS_MARGIN   = 24;                   // ウィンドウの左右・下の余白
static const int       VIS_BAR_GAP  = 6;                    // バー同士の隙間
static const int       VIS_HALF_GAP = 10;                   // ウィンドウ縦半分の位置からの最低距離
static const SDL_Color VIS_COLOR    = {0, 200, 255, 50};    // バーの色（ある程度の半透明）
static const SDL_Color VIS_CAP      = {180, 240, 255, 100}; // バー先端の線の色

void draw_JacketVisualizer(SDL_Renderer* renderer, const JacketManager& jm, const JacketSpectrum& viz){
    if(jm.appear < 0.001) return;
    const auto& bars = viz.bars();
    if(bars.empty()) return;

    SDL_Rect p = jm.lastPanel;
    int areaX  = p.x + VIS_MARGIN;
    int areaW  = p.w - VIS_MARGIN * 2;
    int bottom = p.y + p.h - VIS_MARGIN;

    // 最大の高さ：下端から「ウィンドウの縦半分 + 10px」の位置まで
    int halfY = p.y + p.h / 2;
    int maxH  = bottom - (halfY + VIS_HALF_GAP);
    if(maxH <= 0) return;

    int n = static_cast<int>(bars.size());
    int barW = (areaW - VIS_BAR_GAP * (n - 1)) / n;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    Uint8 bodyA = static_cast<Uint8>(VIS_COLOR.a * jm.appear);
    Uint8 capA  = static_cast<Uint8>(VIS_CAP.a * jm.appear);

    for(int i = 0; i < n; i++){
        int h = std::max(2, static_cast<int>(bars[i] * maxH));   // 無音でも2pxは見せる
        SDL_Rect body = {areaX + i * (barW + VIS_BAR_GAP), bottom - h, barW, h};

        SDL_SetRenderDrawColor(renderer, VIS_COLOR.r, VIS_COLOR.g, VIS_COLOR.b, bodyA);
        SDL_RenderFillRect(renderer, &body);

        SDL_Rect cap = {body.x, body.y, barW, 3};
        SDL_SetRenderDrawColor(renderer, VIS_CAP.r, VIS_CAP.g, VIS_CAP.b, capA);
        SDL_RenderFillRect(renderer, &cap);
    }
}