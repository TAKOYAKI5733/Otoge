#include "GameCommon.h"
#include <nlohmann/json.hpp>
#include "Play/PlaySceneManager.h"
#include "JacketSpectrum.h"
#include "Settingspanel.h"
#include "TapSound.h"
#include "SongPreview.h"     // 試聴：開始位置から10秒、フェードで繰り返し
#include "SelectBackground.h"   // 背景：選んでいる曲のジャケットのぼかし（切り替えは中割り）＋ 低音で背景だけズーム
#include "Play/CubeWallet.h"    // 持っている Cube の総数（cubes.json）
#include "CubeShowcase.h"       // 画面の真ん中の回る台座と立方体（メニュー画面と共通）
#include "QuarryReveal.h"        // ウィンドウを Cube から作り出す出方（光の線 → 枠 → 上から生成）
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
    double previewStartMs = 0.0;   // 試聴の開始位置（曲ファイル上の ms）
    std::filesystem::file_time_type fileTime = std::filesystem::file_time_type::max();   // 譜面ファイルの更新日時（ジャンル選択中の背景に使う。分からなければ一番新しい扱い）

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
    bool isAll = false;
};

enum class SelectMode{
    SelectGenre,
    SelectSong
};

// ALL の並べ替えの基準
enum class SortKey{
    Title,   // 曲名
    Level    // 表示中の難易度のレベル
};
struct JacketManager{
    std::unordered_map<std::string, SDL_Texture*> cache;  // 譜面パス → テクスチャ（読込失敗は nullptr）
    SDL_Texture* fallback = nullptr;   // 読めなかったとき用の専用ジャケット

    SDL_Texture* shown = nullptr;      // 今表示しているテクスチャ
    std::string shownKey = "";         // 今表示している曲の譜面パス（空 = まだ何も表示していない）
    double flip = 1.0;                 // （今は使っていない。前の「めくり」の進み具合）
    SDL_Texture* shownPrev = nullptr;  // 切り替え中：前の曲のジャケット（下に敷き、上に今の曲を濃くしていく）
    double appear = 0.0;               // 出現アニメの進み具合 (0.0〜1.0)
    SDL_Rect lastPanel = {0, 0, 0, 0};

    // 帯の左右を埋める「ぼかした絵」（SelectBackground が持っている絵を借りる。消さないこと）
    SDL_Texture* blurShown = nullptr;  // 今の曲のぼかし
    SDL_Texture* blurPrev  = nullptr;  // 切り替え中：前の曲のぼかし（下に不透明のまま敷き、上に今の曲を濃くしていく）
    std::string  blurKey   = "";       // blurShown がどの曲のものか
    double       blurT     = 1.0;      // ぼかしの切り替えの進み具合（0〜1、1 = 切り替え終わり）

    // 帯の左の辺をドラッグして右へ動かす「ロード」（draw_Jacket・マウスのイベントで使う）
    bool   dragging   = false;
    float  dragStartX = 0.0f;          // つかんだときのマウスの x
    double load       = 0.0;           // 今のロードの割合（0〜LOAD_MAX）
    double loadShown  = 0.0;           // 表示しているロードの割合（離したら 0 へなめらかに戻る）
    float  bandL = 0, bandT = 0, bandB = 0;   // 今の帯の位置（左の辺：下が (bandL, bandB)、上が (bandL + BAND_SKEW, bandT)）
    bool   bandReady = false;          // 帯が出きっていて、つかめるか
};

struct SelectMemory{
    bool hasLast = false;
    std::string genreName;
    std::string scorePath;
    int difficultyCursor = 0;
};
static SelectMemory g_selectMemory;

// ===== マウスで押せる所（毎フレーム、描くときに位置を覚えておく。w = 0 なら押せない） =====
//   クリックされたら、同じキーを押したことにして（SDL_PushEvent）キーボードと同じ処理を通す
static SDL_Rect g_hitGenre = {0, 0, 0, 0};   // 選んでいるジャンルのラベル（ジャンル選択中）→ ENTER
static SDL_Rect g_hitSong  = {0, 0, 0, 0};   // 選んでいる曲のラベル（曲選択中）→ ENTER
static SDL_Rect g_hitDiffL = {0, 0, 0, 0};   // 難易度の「<」→ ←
static SDL_Rect g_hitDiffR = {0, 0, 0, 0};   // 難易度の「>」→ →

// ジャンルのウィンドウの出方（メニュー画面とのつなぎ）：1 = いつも通り、0〜1 = 出てくる途中／消える途中、0 = 出さない
//   出てくるとき（g_genreAppearing = true）は Cube から作り出す（QuarryReveal.h）、消えるときはグリッチ
static double g_genreAppear = 1.0;
static bool   g_genreAppearing = true;
static SDL_Texture* g_genreLayer = nullptr;

// 難易度・情報のウィンドウを Cube から作り出すときに使う透明な絵（draw_DetailWindows）
static SDL_Texture* g_badgeLayer = nullptr;
static SDL_Texture* g_infoLayer  = nullptr;
static SDL_Rect     g_infoRect   = {0, 0, 0, 0};   // 情報のウィンドウの枠（draw_songDetail が描くたびに覚える）
static bool         g_detailLastSongMode = false;   // 前のフレームで曲選択だったか（曲選択になった瞬間を見つける）

static bool pointIn(const SDL_Rect& r, int x, int y){
    return r.w > 0 && r.h > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// キーを押したことにする（このフレームのイベントの続きで、キーボードと同じ処理を通る）
static void pushKey(SDL_Keycode key){
    SDL_Event ev{};
    ev.type = SDL_KEYDOWN;
    ev.key.state = SDL_PRESSED;
    ev.key.repeat = 0;
    ev.key.keysym.sym = key;
    ev.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    ev.key.keysym.mod = static_cast<Uint16>(SDL_GetModState());   // Ctrl を押しながらならオートプレイ（ENTER と同じ）
    SDL_PushEvent(&ev);
}

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
class SelectBackground;
void draw_Jacket(SDL_Renderer* renderer, TTF_Font* labelFont, JacketManager& jm, const SongInfo& song, SelectMode currentMode,
                 SelectBackground& blurSource);
void destroySongLayer();
void draw_CubeWindow(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* labelFont, float cubeX, float cubeY);
void destroyCubeWindow();
static bool hitBandLeftEdge(const JacketManager& jm, int mx, int my);
static double bandLoadTravel();
static double loadMax();

void destroyJackets(JacketManager& jm);
void draw_DifficultyBadge(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* labelFont, const std::vector<DifficultyInfo>& diffs, int index, double animation, double slide);
void draw_DetailWindows(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* labelFont, const SongInfo& song, int diffIndex,
                        const DifficultyInfo* diff, double animation, double slide, double chartAnim, bool songMode);
void draw_JacketVisualizer(SDL_Renderer* renderer, const JacketManager& jm, const JacketSpectrum& viz);
void sortAllSongs(GenreInfo& all, SortKey key, bool descending, int difficultyCursor, int& songCursor);

//メイン関数
// ジャンルの中で一番時期が早い曲（譜面ファイルの更新日時が一番古い曲）の番号。曲が無ければ -1
//   ジャンルを選んでいる間の背景に使う（並べ替えても同じ曲になる）
static int earliestSongIndex(const GenreInfo& genre){
    int best = -1;
    for(int i = 0; i < static_cast<int>(genre.songList.size()); i++){
        if(best < 0 || genre.songList[i].fileTime < genre.songList[best].fileTime) best = i;
    }
    return best;
}

GameScene selectSongScene(SDL_Window* window, SDL_Renderer* renderer, std::string& outSelectedScorePath, int& outSelectedDifficulty, PlayerSettings& playerSettings, bool& outAutoplay, SDL_Texture* targetTex){
    std::vector<GenreInfo> categories = scanScoreFolder("scores");
    if(categories.empty()){
        std::cout << "[エラー]曲がねぇ\n";
        return GameScene::Shutdown;
    }

    {
        GenreInfo all;
        all.genreName = "ALL";
        all.isAll = true;
        for(const auto& genre : categories){
            all.songList.insert(all.songList.end(), genre.songList.begin(), genre.songList.end());
        }
        int unusedCursor = 0;
        sortAllSongs(all, SortKey::Title, false, 0, unusedCursor);
        categories.insert(categories.begin(), all);
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

    if(g_selectMemory.hasLast){
        for(size_t gi = 0; gi < categories.size(); gi++){
            if(categories[gi].isTutorial) continue;
            if(categories[gi].genreName != g_selectMemory.genreName) continue;

            for(size_t si = 0; si < categories[gi].songList.size(); si++){
                if(categories[gi].songList[si].scorePath == g_selectMemory.scorePath){
                    genreCursor = static_cast<int>(gi);
                    songCursor = static_cast<int>(si);
                    difficultyCursor = g_selectMemory.difficultyCursor;
                    currentMode = SelectMode::SelectSong;
                    break;
                }
            }
        }
    }

    SortKey sortKey = SortKey::Title;   // ALL の並べ替えの基準
    bool sortDescending = false;        // true で降順

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
    double currentPreviewMs = -1.0;   // 今鳴らしている試聴の開始位置（-1 = 試聴ではない）
    SongPreview preview;
    std::string dafaultBgmPath = "sounds/十番街、雨【シティ探索BGM】.mp3";
        Mix_VolumeMusic(static_cast<int>(playerSettings.bgmVolume * MIX_MAX_VOLUME / 100.0));

    double detailAnimation = 0.0;
    double chartAnimation = 0.0;   // 譜面製作者の行の表示量 (0.0〜1.0)
    double diffSlide = 0.0;        // 難易度切替時のスライド量 (-1.0〜1.0、0で停止)

    auto moveCursor = [&](int dir){
        if(currentMode == SelectMode::SelectGenre){
            int n = static_cast<int>(categories.size());
            genreCursor = (genreCursor + dir + n) % n;
        }
        else if(currentMode == SelectMode::SelectSong){
            int n = static_cast<int>(categories[genreCursor].songList.size());
            songCursor = (songCursor + dir + n) % n;
            detailAnimation = 0.2;   // 曲が変わったときの詳細パネルのアニメ
        }
    };

    bool running = true;
    SDL_Event e;
    GameScene nextScene = GameScene::Shutdown;

    if(targetTex != nullptr){
        SDL_SetRenderTarget(renderer, targetTex);

        SDL_SetRenderDrawColor(renderer, 15, 15, 25, 255);
        SDL_RenderClear(renderer);

        g_hitGenre = g_hitSong = g_hitDiffL = g_hitDiffR = SDL_Rect{0, 0, 0, 0};   // このフレームで描いた物だけ押せる
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

    SelectBackground selectBg;
    selectBg.init(renderer);
    SettingsPanel settings;
    settings.init(playerSettings);

    // ===== メニュー画面とのつなぎ =====
    //   ・メニューから来たとき（g_sceneHandoff）：ジャンルのウィンドウが Cube から作り出されて（QuarryReveal.h）、
    //     背景が土台の色（メニューの背景）から、選んでいる曲のぼかしへなめらかに変わる
    //   ・ジャンル選択中の ESC：ジャンルのウィンドウがグリッチで消え、背景も土台の色へ戻ってから、メニューへ
    //     （メニューでは、そのあとメニューのウィンドウが下から上がってくる）
    //   ・真ん中の台座と立方体は、どちらの画面でも同じ位置・同じ回り方なので、そのまま続いて見える
    constexpr double GENRE_IN_MS  = QuarryReveal::TOTAL_MS;   // メニューから来たとき：ジャンルのウィンドウを Cube から作り出す長さ
    constexpr double BG_IN_MS     = 600.0;   //                         背景が変わる長さ
    constexpr double TO_MENU_MS   = 260.0;   // メニューへ戻るとき：ジャンルのウィンドウが消える・背景が戻る長さ
    const bool fromMenu = (g_sceneHandoff == SceneHandoff::MenuToSelect);
    g_sceneHandoff = SceneHandoff::None;
    const Uint32 sceneEnterTick = SDL_GetTicks();
    bool toMenu = false;                     // メニューへ戻る途中か
    Uint32 toMenuTick = 0;

    // 🌟 追加: ランキング表示

    while(running){
        // メニューへ戻る動きが終わったら、メニューへ
        if(toMenu && SDL_GetTicks() - toMenuTick >= TO_MENU_MS){
            g_sceneHandoff = SceneHandoff::SelectToMenu;   // メニューで、ウィンドウを下から上げる
            nextScene = GameScene::Menu;
            running = false;
            break;
        }
        {
            // ジャンルのウィンドウの出方
            const double inT  = fromMenu ? std::min(1.0, (SDL_GetTicks() - sceneEnterTick) / GENRE_IN_MS) : 1.0;
            const double outT = toMenu ? std::min(1.0, (SDL_GetTicks() - toMenuTick) / TO_MENU_MS) : 0.0;
            g_genreAppear = toMenu ? (1.0 - outT) : inT;
            g_genreAppearing = !toMenu;   // 出てくる途中は Cube から作り出す、消える途中はグリッチ
        }

        std::string targetMusicPath = dafaultBgmPath;
        double targetPreviewMs = -1.0;   // -1 = 試聴しない（デフォルト BGM を普通にループ）

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

            if(targetMusicPath != dafaultBgmPath){
                targetPreviewMs = currentSong.previewStartMs;   // 曲が見つかったときだけ試聴
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

        // 曲が変わった、または同じ音源でも試聴位置が違う譜面に移ったら鳴らし直す
        if(targetMusicPath != currentPlayPath || targetPreviewMs != currentPreviewMs){
            preview.stop();   // 解放する曲を試聴が触らないように、先に止める
            if(bgm){
                Mix_HaltMusic();
                Mix_FreeMusic(bgm);
                bgm = nullptr;
            }

            bgm = Mix_LoadMUS(targetMusicPath.c_str());
            if(bgm){
                if(targetPreviewMs >= 0.0){
                    preview.start(bgm, targetPreviewMs);   // 開始位置からフェードイン
                }
                else{
                    Mix_PlayMusic(bgm, -1);                // デフォルト BGM は今まで通り
                }
                currentPlayPath = targetMusicPath;
                currentPreviewMs = targetPreviewMs;
            }
            else{
                std::cout << "[警告]音楽の読込失敗: " << targetMusicPath << "\n";
            }
        }
        preview.update();   // 10秒たったらフェードアウト → 止まったら開始位置から再開

        while(SDL_PollEvent(&e) != 0){
            if(e.type == SDL_QUIT){
                running = false;
                nextScene = GameScene::Shutdown;
            }
            if(toMenu) continue;   // メニューへ戻る途中は操作を受け付けない

            // 真ん中の立方体をクリック：台座と立方体が勢いよく回り、Cube の数のウィンドウを開く／閉じる
            if(e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && !settings.isOpen()){
                const int mx = e.button.x, my = e.button.y;
                const bool songMode = (currentMode == SelectMode::SelectSong);
                if(hitShowcaseCube(mx, my)){
                    countShowcaseClick();   // 0.3 秒以内の連打を数える（20 回で十二面体）
                    g_showcase.boost = std::min(SPIN_BOOST_MAX, g_showcase.boost + SPIN_BOOST_ADD);
                    g_showcase.windowOpen = !g_showcase.windowOpen;
                }
                else if(songMode && hitBandLeftEdge(jackets, mx, my)){
                    jackets.dragging = true;          // 帯の左の辺をつかんだ → ロード開始
                    jackets.dragStartX = static_cast<float>(mx);
                    jackets.load = 0.0;
                }
                else if(!songMode && pointIn(g_hitGenre, mx, my)) pushKey(SDLK_RETURN);   // ジャンルを決定
                else if(songMode && pointIn(g_hitSong, mx, my))   pushKey(SDLK_RETURN);   // 曲を決定（プレイへ）
                else if(songMode && pointIn(g_hitDiffL, mx, my))  pushKey(SDLK_LEFT);
                else if(songMode && pointIn(g_hitDiffR, mx, my))  pushKey(SDLK_RIGHT);
            }
            // ロード：つかんでいる間は、右へ動かした分だけ進む（最大 LOAD_MAX = 99.9%）
            if(e.type == SDL_MOUSEMOTION && jackets.dragging){
                const double travel = bandLoadTravel();   // 左の辺が右の辺に着くまでの距離
                jackets.load = std::clamp((e.motion.x - jackets.dragStartX) / travel, 0.0, loadMax());
            }
            // 離したとき：99.9% まで来ていれば ENTER と同じ（ロード画面へ）。足りなければ 0 に戻る
            if(e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && jackets.dragging){
                jackets.dragging = false;
                if(jackets.load >= loadMax() - 1e-9 && currentMode == SelectMode::SelectSong) pushKey(SDLK_RETURN);
                jackets.load = 0.0;
            }

            if(e.type == SDL_MOUSEWHEEL){
                int wheelY = e.wheel.y;
                if(e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) wheelY = -wheelY;   // ナチュラルスクロール対策

                if(wheelY > 0)      moveCursor(-1);   // 奥に回す = 上へ
                else if(wheelY < 0) moveCursor(+1);   // 手前に回す = 下へ
            }

            if(e.type == SDL_KEYDOWN){
                if(settings.isOpen()){
                    settings.handleKey(e.key, window, renderer, playerSettings);
                    if(settings.consumeCalibrationRequest()){   // 「判定調整」で ENTER
                        nextScene = GameScene::Calibration;
                        running = false;
                    }
                    if(settings.consumeKeyConfigRequest()){      // 「キー設定」で ENTER
                        nextScene = GameScene::KeyConfig;
                        running = false;
                    }
                    if(settings.consumeNoteSettingRequest()){    // 「ノーツの詳細設定」で ENTER
                        nextScene = GameScene::NoteSetting;
                        running = false;
                    }
                    continue;
                }

                bool cursorMoved = false;

                switch(e.key.keysym.sym){
                    case SDLK_ESCAPE:{
                        if(currentMode == SelectMode::SelectSong){
                            currentMode = SelectMode::SelectGenre;
                            songCursor = 0;
                        }
                        else if(!toMenu){
                            // ジャンル選択中の ESC：ジャンルのウィンドウがグリッチで消えてから、メニュー画面へ戻る
                            toMenu = true;
                            toMenuTick = SDL_GetTicks();
                        }
                        break;
                    }
                    
                    case SDLK_DOWN:{
                        moveCursor(+1);
                        break;
                    }

                    case SDLK_UP:{
                        moveCursor(-1);
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
                        if(sortKey == SortKey::Level && categories[genreCursor].isAll){
                            sortAllSongs(categories[genreCursor], sortKey, sortDescending, difficultyCursor, songCursor);
                        }
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
                            g_selectMemory.hasLast = true;
                            g_selectMemory.genreName = categories[genreCursor].genreName;
                            g_selectMemory.scorePath = song.scorePath;
                            g_selectMemory.difficultyCursor = difficultyCursor;
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
                        if(!e.key.repeat) settings.open(playerSettings);   // 別画面に行かずに開く
                        break;
                    }

                    case SDLK_F3:
                    case SDLK_F4:{
                        // ALL の曲選択中だけ有効
                        if(currentMode != SelectMode::SelectSong || !categories[genreCursor].isAll) break;

                        if(e.key.keysym.sym == SDLK_F3){
                            sortKey = (sortKey == SortKey::Title) ? SortKey::Level : SortKey::Title;
                        }
                        else{
                            sortDescending = !sortDescending;
                        }
                        sortAllSongs(categories[genreCursor], sortKey, sortDescending, difficultyCursor, songCursor);
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

        // 音の解析を先にする（背景のズームとジャケットのビジュアライザーの両方で使う）
        visualizer.update();

        // 背景：曲を選んでいるときは、その曲のジャケットのぼかし。ジャンルを選んでいるときは土台の色だけ
        //   ジャケットの探し方は loadJacketFor と同じ順番
        {
            //   ジャンルを選んでいるときは、そのジャンルの中で一番時期が早い曲（譜面ファイルが一番古い曲）の背景にする
            std::string bgKey;
            std::vector<std::string> bgCandidates;
            const SongInfo* bgSong = nullptr;
            if(currentMode == SelectMode::SelectSong){
                bgSong = &categories[genreCursor].songList[songCursor];
            }
            else if(!categories[genreCursor].isTutorial){
                const int idx = earliestSongIndex(categories[genreCursor]);
                if(idx >= 0) bgSong = &categories[genreCursor].songList[idx];
            }
            if(bgSong){
                bgKey = bgSong->scorePath;
                if(!bgSong->jacket.empty()) bgCandidates.push_back("jackets/" + bgSong->jacket);
                bgCandidates.push_back("jackets/" + bgSong->title + ".png");
                bgCandidates.push_back("jackets/" + bgSong->title + ".jpg");
            }
            selectBg.setSong(renderer, bgKey, bgCandidates);

            // 低音（一番下の LOW_BARS 本の平均）で背景だけズーム。設定の「低音ズーム」が OFF なら大きくしない
            const std::vector<float>& bars = visualizer.bars();
            float low = 0.0f;
            const int n = std::min<int>(SelectBackground::LOW_BARS, static_cast<int>(bars.size()));
            for(int b = 0; b < n; b++) low += bars[b];
            if(n > 0) low /= n;
            selectBg.updateZoom(low, playerSettings.bassZoom);
            selectBg.draw(renderer);

            // メニュー画面とのつなぎ：土台の色（メニューの背景）を上に重ねて、なめらかに切り替える
            //   来たとき：濃さ 1 → 0（だんだん曲のぼかしが見えてくる）、戻るとき：0 → 1
            double cover = 0.0;
            if(toMenu)        cover = std::min(1.0, (SDL_GetTicks() - toMenuTick) / TO_MENU_MS);
            else if(fromMenu) cover = 1.0 - std::min(1.0, (SDL_GetTicks() - sceneEnterTick) / BG_IN_MS);
            if(cover > 0.0){
                cover = cover * cover * (3.0 - 2.0 * cover);   // smoothstep：始めと終わりをゆっくり
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, SelectBackground::BASE_COLOR.r, SelectBackground::BASE_COLOR.g,
                                       SelectBackground::BASE_COLOR.b, static_cast<Uint8>(255.0 * cover));
                SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
                SDL_RenderFillRect(renderer, &full);
            }
        }

        // 画面の真ん中の奥：回る台座と立方体（一覧などより先に描くので、一番奥になる）
        draw_CubeShowcase(renderer);

        if(currentMode == SelectMode::SelectSong && categories[genreCursor].isAll){
            songPanel.title = std::string("MUSIC / ")
                            + (sortKey == SortKey::Title ? "TITLE" : "LEVEL")
                            + (sortDescending ? " DESC" : " ASC")
                            + "  [F3/F4]";
        }
        else{
            songPanel.title = "MUSIC";
        }

        g_hitGenre = g_hitSong = g_hitDiffL = g_hitDiffR = SDL_Rect{0, 0, 0, 0};   // このフレームで描いた物だけ押せる
        draw_ListsInPanels(renderer, font, label_font, categories, genreCursor, songCursor,
                           currentMode, genrePanel, songPanel);
        draw_Jacket(renderer, label_font, jackets, categories[genreCursor].songList[songCursor], currentMode, selectBg);

        draw_JacketVisualizer(renderer, jackets, visualizer);

        if(detailAnimation > 0.001){
            const SongInfo& song = categories[genreCursor].songList[songCursor];
            int diffIndex = clampDifficulty(difficultyCursor, song.difficulties.size());
            const DifficultyInfo* diff = song.difficulties.empty() ? nullptr : &song.difficulties[diffIndex];

            // 難易度・情報のウィンドウ：曲選択になった瞬間は Cube から作り出す（それ以外はいつも通りのスライド）
            draw_DetailWindows(renderer, font, label_font, song, diffIndex, diff, detailAnimation, diffSlide, chartAnimation,
                               currentMode == SelectMode::SelectSong);
        }

        // 右下の操作説明（今のモードで使えるキーだけを出す）
        //   右端をそろえて2行で出す。設定ウィンドウを開いているときは、その下に隠れる
        {
            const bool songMode = (currentMode == SelectMode::SelectSong);
            const bool sortable = songMode && categories[genreCursor].isAll;
            const std::string line1 = songMode
                ? "[↑↓]:曲を選ぶ   [←→]:難易度   [ENTER]:プレイ   [Ctrl+ENTER]:オートプレイ"
                : "[↑↓]:ジャンルを選ぶ   [ENTER]:決定";
            //   [F1]:譜面作成 は曲を選んでいるときだけ出す（選んでいる曲の譜面を開くため）
            const std::string line2 = std::string(sortable ? "[F3/F4]:並べ替え   " : "")
                + (songMode ? "[F1]:譜面作成   " : "") + "[F2]:設定   " + (songMode ? "[ESC]:戻る" : "[ESC]:メニュー");

            auto drawRight = [&](const std::string& text, int rightX, int centerY){
                SDL_Surface* surf = TTF_RenderUTF8_Blended(label_font, text.c_str(), SDL_Color{150, 150, 170, 255});
                if(!surf) return;
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
                if(tex){
                    SDL_Rect dst = {rightX - surf->w, centerY - surf->h / 2, surf->w, surf->h};
                    SDL_RenderCopy(renderer, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                }
                SDL_FreeSurface(surf);
            };
            drawRight(line1, SCREEN_W - 30, SCREEN_H - 72);
            drawRight(line2, SCREEN_W - 30, SCREEN_H - 36);
        }

        settings.update();
        settings.draw(renderer, font, label_font, playerSettings);

        // Cube の数のウィンドウ（立方体の右斜め上）：最前面。一覧・ジャケット・操作説明・設定ウィンドウよりも後に描く
        {
            float cubeX, cubeY;
            showcaseCubeCenter(cubeX, cubeY);
            draw_CubeWindow(renderer, font, label_font, cubeX, cubeY);
        }


        SDL_RenderPresent(renderer);

        SDL_Delay(8);
    }

    if(settings.isOpen()) settings.close(window, playerSettings);
    settings.destroy();

    selectBg.destroy();
    visualizer.stop();   // BGM を止める前に音声の受け取りを止める
    preview.stop();      // 曲を解放する前に試聴を止める
    if(bgm){
        Mix_HaltMusic();
        Mix_FreeMusic(bgm);
    }

    destroyJackets(jackets);
    destroySongLayer();
    destroyCubeWindow();
    g_showcase.windowOpen = false;   // 次に選曲画面に来たときは閉じた状態から
    g_showcase.easter = 0;           // 十二面体のまま画面を離れても、次は立方体から
    g_showcase.combo = 0;
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
                    std::error_code ec;
                    const auto t = fs::last_write_time(subEntry.path(), ec);
                    if(!ec) song.fileTime = t;
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
static const int GENRE_FRAME_MIN_W = 540;   // ジャンル枠の最小幅
static const int SONG_FRAME_MIN_W  = 640;   // 楽曲枠の最小幅（一覧のウィンドウの幅に合わせる）

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
static const SDL_Rect GENRE_PANEL_LIST   = {150, 140,  620, 790};  // ジャンル選択中：一覧全体（下を 10px 詰めた 800 → 790）
static const SDL_Rect GENRE_PANEL_HEADER = { 70,  88,  720, 108};  // 曲選択中：選んだジャンルだけ（左の列の一番上）
//   曲選択中：曲の一覧。画面の真ん中の台座と立方体（draw_CubeShowcase：回っても x = 797〜1123）の右から、画面の右端まで。
//   下は右下の操作説明（SCREEN_H − 72 あたりから下）に重ならない所まで
//   ※ ジャンルの一覧（x = 150〜770）・左の列（x = 70〜790）も、台座と立方体には重ならない
static const SDL_Rect SONG_PANEL_LIST    = {1150, 70,  740, 900};

// 選んでいる項目の出っ張り（ジャンルは右へ、曲は左へ）。カーソルが動くと、項目の位置がなめらかに動く（スライド）
static const double GENRE_ITEM_X     = 190.0;   // ジャンルの項目の文字の左端
static const double GENRE_SELECT_JUT = 30.0;    // 選んでいるジャンルを右へ出す量(px)
static const double SONG_ITEM_X      = 1210.0;  // 曲の項目の文字の左端
static const double SONG_SELECT_JUT  = 30.0;    // 選んでいる曲を左へ出す量(px)（ウィンドウの左の枠に届かない量）

// 曲の一覧のウィンドウが出る・消えるとき（出るときは Cube から作り出す：QuarryReveal.h、消えるときはグリッチ）
static const double SONG_GLITCH_IN_MS  = QuarryReveal::TOTAL_MS;   // 出てくる長さ(ms)
static const double SONG_GLITCH_OUT_MS = 180.0;   // 消える長さ(ms)
static const int    SONG_GLITCH_BANDS  = 18;      // 横の帯の数
static const float  SONG_GLITCH_SHIFT  = 36.0f;   // 帯を左右にずらす最大(px)
static const float  SONG_GLITCH_RGB    = 6.0f;    // 色ずれの幅(px)

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

// =====================================================================
//  曲の一覧のウィンドウの出し方
//
//  ・曲選択になったら、一覧のウィンドウ（枠・タイトル・曲の項目）を、まず画面と同じ大きさの透明な絵 g_songLayer に描く
//  ・出てくるときは、その絵を QuarryReveal（QuarryReveal.h）で Cube から作り出す：
//      Cube から光の線 → 枠が縦 → 横に広がる（中に薄い斜線）→ 上から下へ中身ができる（先頭はグリッチ）
//  ・消えるときは、絵を横の帯（SONG_GLITCH_BANDS 本）に切って画面に貼る（drawGlitchLayer）。進み具合 p（1 → 0）に合わせて
//      見えている帯の割合 = p（ばらばらの帯から現れて、最後に全部そろう）
//      ずれ・色ずれの強さ = (1 − p)²（はじめ強く、すぐ落ち着く）
//    帯の形は 30ms ごとに変わる（holoHash01：同じ数を入れると同じ答えになる乱数）
//  ・p = 1 になったら、絵を通さず画面に直接描く（いつもの重さ）
//  ・ジャンル選択に戻るときは、このグリッチを逆向きに速く（SONG_GLITCH_OUT_MS）して消す
//  ・透明な絵に半透明の物を描くと、色が「透明度を掛けた値」で入る（premultiplied）ので、貼るときもその前提の混ぜ方にする
// =====================================================================
static SDL_Texture* g_songLayer = nullptr;
static double g_songAppear = 0.0;      // 0 = 出ていない、1 = 出きった
static Uint32 g_songLastTick = 0;

void destroySongLayer(){
    if(g_songLayer){ SDL_DestroyTexture(g_songLayer); g_songLayer = nullptr; }
    if(g_genreLayer){ SDL_DestroyTexture(g_genreLayer); g_genreLayer = nullptr; }
    if(g_badgeLayer){ SDL_DestroyTexture(g_badgeLayer); g_badgeLayer = nullptr; }
    if(g_infoLayer) { SDL_DestroyTexture(g_infoLayer);  g_infoLayer  = nullptr; }
    g_songAppear = 0.0;
    g_songLastTick = 0;
    g_genreAppear = 1.0;
    g_genreAppearing = true;
    g_detailLastSongMode = false;
}

static SDL_Texture* songLayer(SDL_Renderer* renderer){
    if(!g_songLayer){
        g_songLayer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);
    }
    return g_songLayer;
}

static const SDL_BlendMode BLEND_PREMULTIPLIED = SDL_ComposeCustomBlendMode(
    SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
    SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);

// tex（premultiplied の絵）の area の部分を、グリッチさせて貼る。p = 進み具合（0〜1）
static void drawGlitchLayer(SDL_Renderer* renderer, SDL_Texture* tex, const SDL_Rect& area, double p){
    const float strength = static_cast<float>((1.0 - p) * (1.0 - p));
    const uint32_t seed = SDL_GetTicks() / 30u;
    const float bandH = static_cast<float>(area.h) / SONG_GLITCH_BANDS;
    const Uint8 mainA = static_cast<Uint8>(255.0 * std::clamp(0.5 + 0.5 * p, 0.0, 1.0));

    for(int b = 0; b < SONG_GLITCH_BANDS; b++){
        const uint32_t k = seed * 977u + static_cast<uint32_t>(b) * 17u;
        if(holoHash01(k) >= p * 1.15) continue;   // まだ現れていない帯（最後の 1 割ほどで全部そろう）
        float shift = 0.0f;
        if(holoHash01(k * 3u + 5u) < 0.55){
            shift = static_cast<float>(holoHash01(k * 7u + 11u) * 2.0 - 1.0) * SONG_GLITCH_SHIFT * strength;
        }
        const int y0 = area.y + static_cast<int>(bandH * b);
        const int y1 = area.y + static_cast<int>(bandH * (b + 1));
        const SDL_Rect src = {area.x, y0, area.w, y1 - y0};

        // 本体（premultiplied なので、薄くするときは色も同じだけ下げる）
        SDL_SetTextureBlendMode(tex, BLEND_PREMULTIPLIED);
        SDL_SetTextureColorMod(tex, mainA, mainA, mainA);
        SDL_SetTextureAlphaMod(tex, mainA);
        const SDL_FRect dst = {area.x + shift, static_cast<float>(y0), static_cast<float>(area.w), static_cast<float>(y1 - y0)};
        SDL_RenderCopyF(renderer, tex, &src, &dst);

        // 色ずれ（赤を左、青を右に、足し算で薄く）
        if(strength > 0.03f){
            const Uint8 ga = static_cast<Uint8>(150 * strength);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
            SDL_SetTextureAlphaMod(tex, ga);
            SDL_SetTextureColorMod(tex, 255, 40, 60);
            SDL_FRect d1 = dst; d1.x -= SONG_GLITCH_RGB;
            SDL_RenderCopyF(renderer, tex, &src, &d1);
            SDL_SetTextureColorMod(tex, 40, 200, 255);
            SDL_FRect d2 = dst; d2.x += SONG_GLITCH_RGB;
            SDL_RenderCopyF(renderer, tex, &src, &d2);
        }
    }
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);

    // 走査線：ウィンドウの中に、白っぽい水色の細い横線を 2 本
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for(int k = 0; k < 2; k++){
        const int y = area.y + static_cast<int>(holoHash01(seed * 31u + k * 101u) * area.h);
        SDL_SetRenderDrawColor(renderer, HOLO_FRONT_COLOR.r, HOLO_FRONT_COLOR.g, HOLO_FRONT_COLOR.b,
                               static_cast<Uint8>(160 * strength));
        SDL_Rect line = {area.x, y, area.w, 2};
        SDL_RenderFillRect(renderer, &line);
    }
}

void draw_ListsInPanels(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* labelFont,
                        std::vector<GenreInfo>& categories, size_t genreCursor, size_t songCursor,
                        SelectMode currentMode, PanelWindow& genrePanel, PanelWindow& songPanel){
    const bool songMode = (currentMode == SelectMode::SelectSong);

    // 1. ジャンルのウィンドウ：ジャンル選択中は一覧、曲選択中は選んだジャンルだけ（左上）に縮む
    updatePanel(genrePanel, songMode ? GENRE_PANEL_HEADER : GENRE_PANEL_LIST, songMode ? 0.0 : 1.0);
    auto drawGenreWindow = [&](){
        drawPanelBack(renderer, labelFont, genrePanel, !songMode);
        setPanelClip(renderer, genrePanel);
        draw_GenreList(renderer, font, categories, genreCursor, songMode, nullptr);
        SDL_RenderSetClipRect(renderer, nullptr);
    };
    //   メニュー画面から来た直後・メニュー画面へ戻る途中は、曲の一覧と同じグリッチで出す／消す（g_genreAppear）
    if(g_genreAppear >= 1.0){
        drawGenreWindow();
    }
    else if(g_genreAppear > 0.0){
        if(!g_genreLayer){
            g_genreLayer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);
        }
        if(!g_genreLayer){
            drawGenreWindow();
        }
        else{
            SDL_Texture* prevTarget = SDL_GetRenderTarget(renderer);
            SDL_SetRenderTarget(renderer, g_genreLayer);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            SDL_RenderClear(renderer);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            drawGenreWindow();
            SDL_SetRenderTarget(renderer, prevTarget);

            const SDL_Rect r = panelRect(genrePanel);
            const SDL_Rect area = {r.x - 8, r.y - 50, r.w + 16, r.h + 58};   // 上のタイトルのタブも入れる
            if(g_genreAppearing){
                // 出てくる：Cube から光の線 → 枠（縦 → 横・薄い斜線）→ 上から生成
                float cubeX, cubeY;
                showcaseCubeCenter(cubeX, cubeY);
                QuarryReveal::draw(renderer, g_genreLayer, r, area, cubeX, cubeY, g_genreAppear * QuarryReveal::TOTAL_MS);
            }
            else{
                // 消える：今まで通りのグリッチ
                SDL_RenderSetClipRect(renderer, &area);
                drawGlitchLayer(renderer, g_genreLayer, area, g_genreAppear);
                SDL_RenderSetClipRect(renderer, nullptr);
            }
        }
    }

    // 2. 曲の一覧のウィンドウ：ジャンル選択中は出さない。曲選択中はグリッチで出てくる
    const Uint32 now = SDL_GetTicks();
    const double dt = (g_songLastTick == 0) ? 0.0 : std::min<double>(now - g_songLastTick, 100.0);
    g_songLastTick = now;
    const double before = g_songAppear;
    if(songMode) g_songAppear = std::min(1.0, g_songAppear + dt / SONG_GLITCH_IN_MS);
    else         g_songAppear = std::max(0.0, g_songAppear - dt / SONG_GLITCH_OUT_MS);
    if(g_songAppear <= 0.0) return;

    // 出始めた瞬間：曲の項目を、動きながら来るのではなく、いきなり今の位置に置く
    if(before <= 0.0){
        for(auto& song : categories[genreCursor].songList) song.positionInittailized = false;
    }

    // ウィンドウは動かさない（その場でグリッチして出る）
    songPanel.x = SONG_PANEL_LIST.x;  songPanel.y = SONG_PANEL_LIST.y;
    songPanel.w = SONG_PANEL_LIST.w;  songPanel.h = SONG_PANEL_LIST.h;
    songPanel.titleAlpha = 1.0;
    songPanel.initialized = true;

    auto drawSongWindow = [&](){
        drawPanelBack(renderer, labelFont, songPanel, songMode);
        setPanelClip(renderer, songPanel);
        draw_SongList(renderer, font, categories[genreCursor].songList, songCursor, SelectMode::SelectSong, nullptr);
        SDL_RenderSetClipRect(renderer, nullptr);
    };

    SDL_Texture* layer = (g_songAppear < 1.0) ? songLayer(renderer) : nullptr;
    if(!layer){
        drawSongWindow();   // 出きった（または絵が作れない環境）：そのまま描く
        return;
    }

    // 透明な絵に描いてから、グリッチさせて貼る（タイトルのタブはウィンドウの上にはみ出すので、その分も含める）
    SDL_Texture* prevTarget = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, layer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    drawSongWindow();
    SDL_SetRenderTarget(renderer, prevTarget);

    const SDL_Rect area = {SONG_PANEL_LIST.x - 8, SONG_PANEL_LIST.y - 50, SONG_PANEL_LIST.w + 16, SONG_PANEL_LIST.h + 58};
    if(songMode){
        // 出てくる：Cube から光の線 → 枠（縦 → 横・薄い斜線）→ 上から生成（QuarryReveal.h）
        float cubeX, cubeY;
        showcaseCubeCenter(cubeX, cubeY);
        QuarryReveal::draw(renderer, layer, SONG_PANEL_LIST, area, cubeX, cubeY, g_songAppear * QuarryReveal::TOTAL_MS);
        return;
    }
    // 消える：グリッチ。ずらした帯・色ずれが、ジャケットの帯や真ん中の立方体にかからないよう、
    //   ウィンドウ（＋上のタイトルのタブ）の範囲の外には描かない
    SDL_RenderSetClipRect(renderer, &area);
    drawGlitchLayer(renderer, layer, area, g_songAppear);
    SDL_RenderSetClipRect(renderer, nullptr);
}

// =====================================================================
//  Cube の数のウィンドウ（真ん中の立方体をクリックすると開く／閉じる）
//
//  ・立方体の右斜め上に置き、立方体の中心とウィンドウの左下の角を、ウィンドウの枠と同じ色の直線で結ぶ
//  ・中身は持っている Cube の総数（cubes.json）。開いた瞬間に読み直すので、プレイで増えた分も入る
//  ・出るときは Cube から作り出す（QuarryReveal.h）：立方体から左下の角へ光の線が伸び（その線がそのまま結ぶ線になる）、
//    枠が縦 → 横に広がって、上から中身ができる
//  ・消えるときは、曲の一覧と同じグリッチ（drawGlitchLayer）。線も一緒にグリッチする
// =====================================================================
static const float  CUBE_WIN_DX    = 110.0f;   // 立方体の中心から、ウィンドウの左下の角までの横のずれ(px)
static const float  CUBE_WIN_DY    = -120.0f;  //   縦のずれ(px)（マイナス = 上）
static const int    CUBE_WIN_W     = 320;
static const int    CUBE_WIN_H     = 130;
static const float  CUBE_WIN_ICON_RATIO = 0.6f;   // 数の右の立方体の大きさ（数のフォントの高さの何倍か。0.6 ≒ 数字の高さ）
static const SDL_Color CUBE_WIN_COLOR = {0, 200, 255, 255};   // 枠と線の色（操作中のウィンドウの枠と同じ水色）
static const double CUBE_WIN_IN_MS  = QuarryReveal::TOTAL_MS;
static const double CUBE_WIN_OUT_MS = 160.0;

static SDL_Texture* g_cubeLayer = nullptr;
static double    g_cubeAppear = 0.0;
static Uint32    g_cubeLastTick = 0;
static long long g_cubeTotal = 0;

void destroyCubeWindow(){
    if(g_cubeLayer){ SDL_DestroyTexture(g_cubeLayer); g_cubeLayer = nullptr; }
    g_cubeAppear = 0.0;
    g_cubeLastTick = 0;
}

void draw_CubeWindow(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* labelFont, float cubeX, float cubeY){
    const Uint32 now = SDL_GetTicks();
    const double dt = (g_cubeLastTick == 0) ? 0.0 : std::min<double>(now - g_cubeLastTick, 100.0);
    g_cubeLastTick = now;
    const double before = g_cubeAppear;
    if(g_showcase.windowOpen) g_cubeAppear = std::min(1.0, g_cubeAppear + dt / CUBE_WIN_IN_MS);
    else                      g_cubeAppear = std::max(0.0, g_cubeAppear - dt / CUBE_WIN_OUT_MS);
    if(g_cubeAppear <= 0.0) return;
    if(before <= 0.0) g_cubeTotal = loadCubeWallet();   // 開き始めた瞬間に読み直す

    // 数の文字の大きさを先に測る（数の右に置く立方体の大きさと、ウィンドウの幅を決めるため）
    const std::string numText = std::to_string(g_cubeTotal);
    int numW = 0, numH = 0;
    if(font) TTF_SizeUTF8(font, numText.c_str(), &numW, &numH);
    //   立方体の大きさ：数字の高さ（だいたいフォントの高さの 0.6 倍）に合わせる。回っても数字と同じくらいに見える
    const float iconSize = std::max(12.0f, numH * CUBE_WIN_ICON_RATIO);
    const int   iconGap  = 18;                                       // 数と立方体のすき間
    const int   contentW = 24 + numW + iconGap + static_cast<int>(iconSize * 1.5f) + 24;   // 回ると 1 辺の約 1.5 倍の幅になる

    const int winW = std::max(CUBE_WIN_W, contentW);                  // 数が大きくなったらウィンドウも広げる
    const int winX = static_cast<int>(cubeX + CUBE_WIN_DX);
    const int winY = static_cast<int>(cubeY + CUBE_WIN_DY) - CUBE_WIN_H;   // 左下の角が (cubeX + DX, cubeY + DY)
    const SDL_Rect win = {winX, winY, winW, CUBE_WIN_H};

    auto drawWindow = [&](bool withLine){
        // 線：立方体の中心 → ウィンドウの左下の角（作り出している途中は、光の線が代わりになる）
        if(withLine) drawThickLine(renderer, cubeX, cubeY, static_cast<float>(win.x), static_cast<float>(win.y + win.h), 2.5f, CUBE_WIN_COLOR);
        // ウィンドウ
        drawItemFrame(renderer, win, PANEL_FILL, CUBE_WIN_COLOR, PANEL_BORDER_W);
        // 文字：上に小さく「TOTAL CUBE」、その下に大きく数
        auto text = [&](TTF_Font* f, const std::string& str, SDL_Color c, int x, int cy){
            if(!f) return;
            SDL_Surface* surf = TTF_RenderUTF8_Blended(f, str.c_str(), c);
            if(!surf) return;
            if(SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, surf)){
                SDL_Rect dst = {x, cy - surf->h / 2, surf->w, surf->h};
                SDL_RenderCopy(renderer, t, nullptr, &dst);
                SDL_DestroyTexture(t);
            }
            SDL_FreeSurface(surf);
        };
        text(labelFont, "TOTAL CUBE", SDL_Color{150, 220, 255, 255}, win.x + 24, win.y + 30);
        const int numY = win.y + 84;
        text(font, numText, SDL_Color{255, 255, 255, 255}, win.x + 24, numY);
        // 数の右で回る立方体（真ん中の立方体と同じ回り方。クリックで速くなるのも同じ）
        const float t = static_cast<float>(std::fmod(g_showcase.cubeT, 2000.0 * 3.14159265358979));
        const float iconX = win.x + 24 + numW + iconGap + iconSize * 0.75f;
        drawCube(renderer, iconX, static_cast<float>(numY), iconSize, 0.5f + t * 0.4f, t, 0.3f,
                 SDL_Color{60, 70, 95, 255}, 1.0f, 1.0f, 1.0f, HOLO_FRONT_COLOR, 2.0f);
    };

    // 範囲：線の始まり（立方体の中心）からウィンドウの右上まで
    const SDL_Rect area = {static_cast<int>(cubeX) - 4, win.y - 4,
                           win.x + win.w + 4 - (static_cast<int>(cubeX) - 4), static_cast<int>(cubeY) + 4 - (win.y - 4)};

    if(g_cubeAppear >= 1.0){
        drawWindow(true);
        return;
    }
    if(!g_cubeLayer){
        g_cubeLayer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);
    }
    if(!g_cubeLayer){
        drawWindow(true);
        return;
    }
    const bool appearing = g_showcase.windowOpen;   // 出てくる途中は Cube から作り出す、消える途中はグリッチ
    SDL_Texture* prevTarget = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, g_cubeLayer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    drawWindow(!appearing);
    SDL_SetRenderTarget(renderer, prevTarget);

    if(appearing){
        // 光の線は消さずに残し、できあがった後の結ぶ線（同じ色・太さ）にそのままつなぐ
        const SDL_Rect winArea = {win.x - 8, win.y - 8, win.w + 16, win.h + 16};
        QuarryReveal::draw(renderer, g_cubeLayer, win, winArea, cubeX, cubeY, g_cubeAppear * QuarryReveal::TOTAL_MS,
                           true, CUBE_WIN_COLOR, 2.5f);
        return;
    }

    SDL_RenderSetClipRect(renderer, &area);
    drawGlitchLayer(renderer, g_cubeLayer, area, g_cubeAppear);
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
            // 選んでいるジャンルは少し右へ出っ張る（カーソルが動くと、なめらかに出入りする）
            targetX = GENRE_ITEM_X + ((i == genreCursor) ? GENRE_SELECT_JUT : 0.0);
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
        if(!isSongMode && isCursor) g_hitGenre = frameRect;   // ジャンル選択中：選んでいるラベルをクリック → ENTER

        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }
}

void draw_SongList(SDL_Renderer* renderer, TTF_Font* font, std::vector<SongInfo>& songList, size_t songCursor, SelectMode currentMode, const SDL_Rect* priorityRect){
    int lineGap = 80;
    int centerY = SONG_PANEL_LIST.y + SONG_PANEL_LIST.h / 2;   // 一覧のウィンドウの縦の真ん中

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
            // 選んでいる曲は少し左へ出っ張る（カーソルが動くと、なめらかに出入りする）
            targetX = SONG_ITEM_X - (isCursor ? SONG_SELECT_JUT : 0.0);
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
        if(highlight) g_hitSong = frameRect;   // 曲選択中：選んでいる曲のラベルをクリック → ENTER

        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_RenderCopy(renderer, tex, NULL, &destRect);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }
}

static const int INFO_PANEL_MIN_W = 720;   // 情報ウィンドウの幅（左の列の幅にそろえる）
static const int INFO_X = 100;             // 情報の文字の左端（ウィンドウは INFO_PANEL_PAD_X だけ左から。左の列 x = 70 にそろう）
static const int INFO_Y = 800;             // 情報の文字の上（難易度のウィンドウのすぐ下）
static const int INFO_PANEL_PAD_X = 30;    // 文字とウィンドウの左右の余白
static const int INFO_PANEL_PAD_Y = 20;    // 文字とウィンドウの上下の余白

void draw_songDetail(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* labelFont, const SongInfo& song, double animation,
                     const DifficultyInfo* diff, double chartAnim){
    int alpha = static_cast<int>(animation * 255);
    int offsetX = static_cast<int>((1.0 - animation) * 100);

    int baseX = INFO_X + offsetX;
    int baseY = INFO_Y;   // 左の列：ジャケットの帯 → 難易度 → ここ
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
    info.w = INFO_PANEL_MIN_W;   // 左の列の幅に固定（長い文字はウィンドウの中で切れる）
    (void)maxW;
    info.h = firstH + static_cast<int>(extraH) + INFO_PANEL_PAD_Y * 2;
    info.titleAlpha = animation;
    drawPanelBack(renderer, labelFont, info, false, animation);
    g_infoRect = panelRect(info);   // Cube から作り出すときの枠（draw_DetailWindows）

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
    song.previewStartMs = j.value("previewStart", 0.0);   // 試聴の開始位置（無ければ曲の頭）

    song.difficulties = listDifficulties(song.scorePath);

    // EASY → NORMAL → HARD → MASTER → 特殊 の順に並べ替える（同じ種類どうしは JSON の順を保つ）
    std::stable_sort(song.difficulties.begin(), song.difficulties.end(),
        [](const DifficultyInfo& a, const DifficultyInfo& b){
            return difficultyRank(a.name) < difficultyRank(b.name);
        });

    return song;
}

// ===== ジャケット =====
//   曲選択中、左の列（ジャンルの下）に横長の平行四辺形の帯を出す。
//   ・帯の真ん中に 1:1 のジャケット、その左右は同じ曲のぼかした絵（背景と同じもの）で埋める
//   ・出てくるとき：帯が右から少しスライドしながら、左の斜めの辺が右から左へ広がっていく
//     （左の方はまだ斜めの辺の外なので隠れていて、右から現れてくる）
//   ・曲を変えたとき：真ん中のジャケットも左右のぼかしも、中割り（前の絵 × (1−t) ＋ 新しい絵 × t）でなめらかに切り替える。
//     前の絵は不透明のまま下に敷き、その上に新しい絵を t の濃さで重ねる（両方を半透明にすると真ん中で暗く点滅するため）
static const char*  JACKET_FALLBACK_PATH = "jackets/default.png";  // 専用のジャケット絵
static const int    BAND_X            = 70;      // 帯（平行四辺形）を囲む四角の左上
static const int    BAND_Y            = 236;
static const int    BAND_W            = 720;
static const int    BAND_H            = 380;
static const int    BAND_SKEW         = 70;      // 斜めの辺の傾き（上の辺が下の辺より右へずれる量 px）
static const int    BAND_PAD          = 18;      // ジャケットと帯の上下のすき間
static const int    JACKET_SIZE       = BAND_H - BAND_PAD * 2;   // 真ん中のジャケットの大きさ（正方形）
static const double BAND_SLIDE_PX     = 160.0;   // 出てくるときに右からスライドする量
static const float  BAND_BLUR_U0 = 0.30f, BAND_BLUR_U1 = 0.70f;   // ぼかした絵の、帯に使う所（真ん中だけ。ふちは透明なので使わない）
static const float  BAND_BLUR_V0 = 0.40f, BAND_BLUR_V1 = 0.60f;   //   帯の縦横比（2:1）に合わせて縦を狭く
static const SDL_Color BAND_BORDER = {0, 200, 255, 255};           // 帯の枠の色
// ロード（帯の左の辺を右へドラッグ）
static const double LOAD_MAX        = 0.999;     // ここで止まる（99.9%）。この状態で離すとロード画面へ
static const SDL_Color LOAD_COLOR   = {150, 215, 255, 120};   // 塗り：Cube の枠（HOLO_FRONT_COLOR {200,250,255}）より少し青みを強く、半透明
static const SDL_Color LOAD_EDGE    = {150, 215, 255, 230};   // 先頭の線
static const float  LOAD_GRAB_PX    = 40.0f;     // 左の辺から、左右にこの範囲をつかめる(px)
static const float  LOAD_STRIPE_W   = 26.0f;     // 流れる斜めのしまの幅(px)
static const double JACKET_FLIP_SPEED = 0.08;    // 切り替えの速さ（1フレームの進み量。0.08 なら約 13 フレームで終わる）

static double bandLoadTravel(){ return static_cast<double>(BAND_W - BAND_SKEW); }
static double loadMax(){ return LOAD_MAX; }

// (mx, my) が帯の左の斜めの辺のそば（左右 LOAD_GRAB_PX 以内）か
static bool hitBandLeftEdge(const JacketManager& jm, int mx, int my){
    if(!jm.bandReady || my < jm.bandT || my > jm.bandB) return false;
    const float edgeX = jm.bandL + BAND_SKEW * (jm.bandB - my) / (jm.bandB - jm.bandT);   // その高さでの左の辺の x
    return std::fabs(mx - edgeX) <= LOAD_GRAB_PX;
}

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

// ----- 平行四辺形の帯を描くための道具 -----
struct BandPt{ float x, y; };

// 凸の多角形 subj を、凸の多角形 clip（時計回り・画面の座標）の内側だけに切る（Sutherland–Hodgman）
static std::vector<BandPt> clipConvex(const std::vector<BandPt>& subj, const std::vector<BandPt>& clip){
    std::vector<BandPt> out = subj;
    const size_t n = clip.size();
    for(size_t e = 0; e < n && !out.empty(); e++){
        const BandPt a = clip[e], b = clip[(e + 1) % n];
        auto inside = [&](const BandPt& p){ return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x) >= 0.0f; };
        auto cross = [&](const BandPt& p, const BandPt& q){
            const float d1 = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
            const float d2 = (b.x - a.x) * (q.y - a.y) - (b.y - a.y) * (q.x - a.x);
            const float t = d1 / (d1 - d2);
            return BandPt{p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t};
        };
        std::vector<BandPt> in = out;
        out.clear();
        for(size_t i = 0; i < in.size(); i++){
            const BandPt cur = in[i], prev = in[(i + in.size() - 1) % in.size()];
            const bool ci = inside(cur), pi = inside(prev);
            if(ci){
                if(!pi) out.push_back(cross(prev, cur));
                out.push_back(cur);
            }
            else if(pi){
                out.push_back(cross(prev, cur));
            }
        }
    }
    return out;
}

// 多角形を、画像（または色だけ）で塗る。画像は四角 rect に、uv（u0,v0〜u1,v1）の部分を引き伸ばして貼った位置で切り出す
static void fillPolygon(SDL_Renderer* r, const std::vector<BandPt>& poly, SDL_Texture* tex, const SDL_FRect& rect,
                        float u0, float v0, float u1, float v1, SDL_Color c){
    if(poly.size() < 3) return;
    std::vector<SDL_Vertex> v(poly.size());
    for(size_t i = 0; i < poly.size(); i++){
        const float u = u0 + (poly[i].x - rect.x) / rect.w * (u1 - u0);
        const float w = v0 + (poly[i].y - rect.y) / rect.h * (v1 - v0);
        v[i] = {SDL_FPoint{poly[i].x, poly[i].y}, c, SDL_FPoint{u, w}};
    }
    std::vector<int> idx;
    for(size_t i = 1; i + 1 < poly.size(); i++){ idx.push_back(0); idx.push_back(static_cast<int>(i)); idx.push_back(static_cast<int>(i + 1)); }
    SDL_RenderGeometry(r, tex, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
}

void draw_Jacket(SDL_Renderer* renderer, TTF_Font* labelFont, JacketManager& jm, const SongInfo& song, SelectMode currentMode,
                 SelectBackground& blurSource){
    // 1. 出現アニメ：曲選択中だけ表示
    bool visible = (currentMode != SelectMode::SelectGenre);
    jm.appear += ((visible ? 1.0 : 0.0) - jm.appear) * 0.15;

    if(jm.appear < 0.001){
        // 完全に隠れたら状態をリセット（次に出るときはめくらずにそのまま表示）
        jm.shownKey.clear();
        jm.shown = nullptr;
        jm.blurShown = jm.blurPrev = nullptr;
        jm.shownPrev = nullptr;
        jm.blurKey.clear();
        jm.dragging = false;          // 隠れたらロードもやめる
        jm.load = jm.loadShown = 0.0;
        jm.bandReady = false;
        jm.blurT = 1.0;
        jm.flip = 1.0;
        return;
    }

    // 2. 切り替え（中割り）：曲が変わったら、ジャケットとぼかしを一緒に切り替え始める
    if(visible){
        SDL_Texture* target = getJacket(jm, renderer, song);
        std::vector<std::string> candidates;
        if(!song.jacket.empty()) candidates.push_back("jackets/" + song.jacket);
        candidates.push_back("jackets/" + song.title + ".png");
        candidates.push_back("jackets/" + song.title + ".jpg");
        SDL_Texture* targetBlur = blurSource.blurredFor(renderer, song.scorePath, candidates);

        if(jm.shownKey.empty()){
            // 最初の1枚はそのまま表示
            jm.shown = target;
            jm.blurShown = targetBlur;
            jm.shownKey = song.scorePath;
            jm.shownPrev = nullptr;
            jm.blurPrev = nullptr;
            jm.blurT = 1.0;
        }
        else if(song.scorePath != jm.shownKey){
            // 切り替えの途中でさらに曲を変えたときは、今どちらが多く見えているかで「前の絵」を決める（見た目が大きく飛ばないように）
            const bool newerWins = (jm.blurT >= 0.5) || !jm.shownPrev;
            jm.shownPrev = newerWins ? jm.shown : jm.shownPrev;
            jm.blurPrev  = (jm.blurT >= 0.5 || !jm.blurPrev) ? jm.blurShown : jm.blurPrev;
            jm.shown = target;
            jm.blurShown = targetBlur;
            jm.shownKey = song.scorePath;
            jm.blurT = 0.0;
        }
        if(jm.blurT < 1.0){
            jm.blurT = std::min(1.0, jm.blurT + JACKET_FLIP_SPEED);
            if(jm.blurT >= 1.0){
                jm.blurPrev = nullptr;
                jm.shownPrev = nullptr;
            }
        }
    }

    // 3. 帯の形：右からスライド ＋ 左の斜めの辺が右から左へ広がる
    const double a  = jm.appear;
    const float  dx = static_cast<float>((1.0 - a) * BAND_SLIDE_PX);
    const float  reveal = static_cast<float>((1.0 - a) * (BAND_W - BAND_SKEW - 2));   // 左の辺をどれだけ右に寄せるか
    const float  L = BAND_X + dx, T = static_cast<float>(BAND_Y), R = BAND_X + BAND_W + dx, B = static_cast<float>(BAND_Y + BAND_H);
    // 時計回り：左上 → 右上 → 右下 → 左下（上の辺が BAND_SKEW だけ右にずれた平行四辺形）
    const std::vector<BandPt> band = {
        {L + BAND_SKEW + reveal, T}, {R, T}, {R - BAND_SKEW, B}, {L + reveal, B}};
    const SDL_FRect bandRect = {L, T, static_cast<float>(BAND_W), static_cast<float>(BAND_H)};
    const Uint8 alpha = static_cast<Uint8>(a * 255);
    jm.lastPanel = {static_cast<int>(L) + BAND_SKEW, BAND_Y, BAND_W - BAND_SKEW * 2, BAND_H};   // ビジュアライザーの場所

    // 4. 帯の中：土台の暗い色 → 左右のぼかし（前の曲 → 今の曲）
    fillPolygon(renderer, band, nullptr, bandRect, 0, 0, 1, 1, SDL_Color{10, 10, 20, static_cast<Uint8>(220 * a)});
    //   前の曲のぼかしは「不透明のまま」下に敷き、その上に今の曲を t の濃さで重ねる
    //   （両方を半透明にして重ねると、真ん中で下の暗い色が透けて、一瞬暗く点滅して見えてしまうため）
    double blurT = jm.blurT;   // ジャケットとぼかしで同じ進み具合を使う
    blurT = blurT * blurT * (3.0 - 2.0 * blurT);   // smoothstep：始めと終わりをゆっくり
    auto drawBlur = [&](SDL_Texture* t, double k){
        if(!t || k <= 0.0) return;
        Uint8 oldA = 255;
        SDL_GetTextureAlphaMod(t, &oldA);
        SDL_SetTextureAlphaMod(t, 255);   // 背景用の薄さ（JACKET_ALPHA）は使わず、帯ではしっかり出す
        fillPolygon(renderer, band, t, bandRect, BAND_BLUR_U0, BAND_BLUR_V0, BAND_BLUR_U1, BAND_BLUR_V1,
                    SDL_Color{255, 255, 255, static_cast<Uint8>(255 * a * k)});
        SDL_SetTextureAlphaMod(t, oldA);  // 背景のために元に戻す
    };
    //   新しい曲にぼかしが無い（ジャケットが見つからない）ときは、前の曲のぼかしを薄くしていって消す
    if(jm.blurPrev && blurT < 1.0) drawBlur(jm.blurPrev, jm.blurShown ? 1.0 : 1.0 - blurT);
    drawBlur(jm.blurShown, blurT);

    // 5. 真ん中の 1:1 のジャケット：縦横比を保って正方形に収める。帯の外は切る
    //   切り替え中は、前の曲のジャケットを下に（形が同じなら不透明のまま、違えば薄くしていきながら）、
    //   その上に今の曲のジャケットを t の濃さで重ねる
    const float jcx = L + BAND_W * 0.5f, jcy = T + BAND_H * 0.5f;
    auto jacketRect = [&](SDL_Texture* t, SDL_FRect& out){
        int texW = 0, texH = 0;
        if(!t || SDL_QueryTexture(t, NULL, NULL, &texW, &texH) != 0 || texW <= 0 || texH <= 0) return false;
        const double fit = std::min(static_cast<double>(JACKET_SIZE) / texW, static_cast<double>(JACKET_SIZE) / texH);
        const float w = static_cast<float>(texW * fit), h = static_cast<float>(texH * fit);
        out = {jcx - w * 0.5f, jcy - h * 0.5f, w, h};
        return true;
    };
    auto drawJacketTex = [&](SDL_Texture* t, const SDL_FRect& jr, double k){
        if(k <= 0.0) return;
        const Uint8 a8 = static_cast<Uint8>(alpha * k);
        const std::vector<BandPt> sq = {{jr.x, jr.y}, {jr.x + jr.w, jr.y}, {jr.x + jr.w, jr.y + jr.h}, {jr.x, jr.y + jr.h}};
        const std::vector<BandPt> clipped = clipConvex(sq, band);
        // 帯の斜めの辺にかかっていない（四角がそのまま残った）ときは、ふつうの貼り方で描く
        //   （三角形で貼る SDL_RenderGeometry は、描画のしかたによっては 1 フレームだけ描かれないことがあったため）
        bool whole = (clipped.size() == 4);
        for(size_t i = 0; whole && i < 4; i++){
            whole = std::fabs(clipped[i].x - sq[i].x) < 0.01f && std::fabs(clipped[i].y - sq[i].y) < 0.01f;
        }
        if(whole){
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(t, a8);
            SDL_RenderCopyF(renderer, t, nullptr, &jr);
            SDL_SetTextureAlphaMod(t, 255);
            return;
        }
        fillPolygon(renderer, clipped, t, jr, 0, 0, 1, 1, SDL_Color{255, 255, 255, a8});
    };
    SDL_FRect curRect{}, prevRect{};
    const bool hasCur  = jacketRect(jm.shown, curRect);
    const bool hasPrev = (jm.shownPrev && blurT < 1.0) && jacketRect(jm.shownPrev, prevRect);
    if(hasPrev){
        const bool sameShape = hasCur && std::fabs(prevRect.w - curRect.w) < 0.5f && std::fabs(prevRect.h - curRect.h) < 0.5f;
        drawJacketTex(jm.shownPrev, prevRect, sameShape ? 1.0 : 1.0 - blurT);
    }
    if(hasCur) drawJacketTex(jm.shown, curRect, hasPrev ? blurT : 1.0);

    // 6. ロード：帯の左の辺から、右へドラッグした所までを塗る（左の辺と平行な斜めの線で区切る）
    //   ・塗りは Cube の枠より少し青みの強い半透明。中を斜めのしまが右へ流れ、先頭に明るい線、右下に「LOADING xx.x%」
    //   ・離したら 0 へなめらかに戻る（99.9% で離したときは、その前にロード画面へ移る）
    jm.bandL = L + reveal;  jm.bandT = T;  jm.bandB = B;
    jm.bandReady = (a > 0.95);
    if(jm.dragging) jm.loadShown = jm.load;
    else            jm.loadShown += (0.0 - jm.loadShown) * 0.2;
    if(jm.loadShown > 0.001){
        const float w = static_cast<float>(jm.loadShown * (BAND_W - BAND_SKEW));
        const std::vector<BandPt> fillArea = {
            {L + reveal + BAND_SKEW, T}, {L + reveal + BAND_SKEW + w, T}, {L + reveal + w, B}, {L + reveal, B}};
        const std::vector<BandPt> filled = clipConvex(band, fillArea);
        fillPolygon(renderer, filled, nullptr, bandRect, 0, 0, 1, 1,
                    SDL_Color{LOAD_COLOR.r, LOAD_COLOR.g, LOAD_COLOR.b, static_cast<Uint8>(LOAD_COLOR.a * a)});

        // 流れる斜めのしま（ロード中らしさ）：同じ傾きの細い平行四辺形を、時間とともに右へずらして並べる
        const float scroll = std::fmod(SDL_GetTicks() * 0.06f, LOAD_STRIPE_W * 2.0f);
        for(float sx = -LOAD_STRIPE_W * 2.0f + scroll; sx < w; sx += LOAD_STRIPE_W * 2.0f){
            const float x0 = std::max(0.0f, sx), x1 = std::min(w, sx + LOAD_STRIPE_W);
            if(x1 <= x0) continue;
            const std::vector<BandPt> stripe = {
                {L + reveal + BAND_SKEW + x0, T}, {L + reveal + BAND_SKEW + x1, T}, {L + reveal + x1, B}, {L + reveal + x0, B}};
            fillPolygon(renderer, clipConvex(band, stripe), nullptr, bandRect, 0, 0, 1, 1,
                        SDL_Color{LOAD_COLOR.r, LOAD_COLOR.g, LOAD_COLOR.b, static_cast<Uint8>(45 * a)});
        }
        // 先頭の線
        drawThickLine(renderer, L + reveal + BAND_SKEW + w, T, L + reveal + w, B, 3.0f,
                      SDL_Color{LOAD_EDGE.r, LOAD_EDGE.g, LOAD_EDGE.b, static_cast<Uint8>(LOAD_EDGE.a * a)});

        // 文字：LOADING xx.x%（99.9% のときは「離してスタート」も）
        if(labelFont){
            char buf[64];
            const bool full = jm.dragging && jm.load >= LOAD_MAX - 1e-9;
            std::snprintf(buf, sizeof(buf), full ? "LOADING %.1f%%  RELEASE TO START" : "LOADING %.1f%%", jm.loadShown * 100.0);
            if(SDL_Surface* ts = TTF_RenderUTF8_Blended(labelFont, buf, SDL_Color{230, 245, 255, 255})){
                if(SDL_Texture* tt = SDL_CreateTextureFromSurface(renderer, ts)){
                    SDL_SetTextureAlphaMod(tt, alpha);
                    SDL_Rect dst = {static_cast<int>(R - BAND_SKEW) - ts->w - 16, static_cast<int>(B) - ts->h - 10, ts->w, ts->h};
                    SDL_RenderCopy(renderer, tt, nullptr, &dst);
                    SDL_DestroyTexture(tt);
                }
                SDL_FreeSurface(ts);
            }
        }
    }

    // 7. 枠：平行四辺形の 4 辺
    const SDL_Color bc = {BAND_BORDER.r, BAND_BORDER.g, BAND_BORDER.b, alpha};
    for(size_t i = 0; i < band.size(); i++){
        const BandPt p = band[i], q = band[(i + 1) % band.size()];
        drawThickLine(renderer, p.x, p.y, q.x, q.y, 3.0f, bc);
    }
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
static const int BADGE_X = 70;     // 左の列（ジャンル・ジャケットの帯・情報と左端をそろえる）
static const int BADGE_Y = 650;    // ジャケットの帯のすぐ下
static const int BADGE_W = 720;
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
    // 矢印をクリック → ← / →（出きっていて、その向きに進めるときだけ）
    if(animation > 0.9){
        if(canLeft)  g_hitDiffL = SDL_Rect{left, BADGE_Y, 70, BADGE_H};
        if(canRight) g_hitDiffR = SDL_Rect{left + BADGE_W - 70, BADGE_Y, 70, BADGE_H};
    }

    // 3. 難易度名とレベル：切替直後は押した方向からスライドしながらフェードイン
    int textOffset = static_cast<int>(slide * 60);
    Uint8 textAlpha = static_cast<Uint8>(alpha * (1.0 - std::fabs(slide)));
    std::string label = d.name + "   Lv." + d.level;

    setPanelClip(renderer, panel);   // スライド中に矢印の外へはみ出さないように
    drawTextCentered(renderer, font, label, dc, left + BADGE_W / 2 + textOffset, cy, textAlpha, true);
    SDL_RenderSetClipRect(renderer, nullptr);
}

// =====================================================================
//  難易度・情報のウィンドウ（曲選択中だけ出る）
//
//  ・曲選択になった瞬間：2 つのウィンドウを Cube から作り出す（QuarryReveal.h）
//      それぞれ透明な絵（g_badgeLayer / g_infoLayer）に出きった形（animation = 1）で描いてから、
//      Cube から光の線 → 枠（縦 → 横・薄い斜線）→ 上から生成。情報のウィンドウは DETAIL_INFO_DELAY_MS 遅れて始める
//  ・作り出し終わった後・曲を変えたとき・ジャンル選択へ戻るときは、今まで通り（右からのスライド ＋ 濃さ）
// =====================================================================
static const double DETAIL_INFO_DELAY_MS = 90.0;   // 難易度のウィンドウから、情報のウィンドウが遅れて始まる時間

void draw_DetailWindows(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* labelFont, const SongInfo& song, int diffIndex,
                        const DifficultyInfo* diff, double animation, double slide, double chartAnim, bool songMode){
    static Uint32 quarryStart  = 0;
    static bool   quarryActive = false;
    const Uint32 now = SDL_GetTicks();
    if(songMode && !g_detailLastSongMode){   // 曲選択になった瞬間
        quarryStart  = now;
        quarryActive = true;
    }
    g_detailLastSongMode = songMode;
    if(!songMode) quarryActive = false;      // ジャンル選択へ戻る：いつも通り消える
    const double ms = static_cast<double>(now - quarryStart);
    if(quarryActive && QuarryReveal::done(ms - DETAIL_INFO_DELAY_MS)) quarryActive = false;

    if(quarryActive){
        if(!g_badgeLayer) g_badgeLayer = quarryMakeLayer(renderer);
        if(!g_infoLayer)  g_infoLayer  = quarryMakeLayer(renderer);
        if(!g_badgeLayer || !g_infoLayer) quarryActive = false;   // 透明な絵が作れない環境：いつも通り描く
    }
    if(!quarryActive){
        draw_DifficultyBadge(renderer, font, labelFont, song.difficulties, diffIndex, animation, slide);
        draw_songDetail(renderer, font, labelFont, song, animation, diff, chartAnim);
        return;
    }

    // 出きった形で、それぞれの透明な絵に描く
    SDL_Texture* prev = quarryBeginLayer(renderer, g_badgeLayer);
    draw_DifficultyBadge(renderer, font, labelFont, song.difficulties, diffIndex, 1.0, slide);
    SDL_SetRenderTarget(renderer, prev);
    prev = quarryBeginLayer(renderer, g_infoLayer);
    draw_songDetail(renderer, font, labelFont, song, 1.0, diff, chartAnim);
    SDL_SetRenderTarget(renderer, prev);

    float cubeX, cubeY;
    showcaseCubeCenter(cubeX, cubeY);
    if(!song.difficulties.empty()){
        const SDL_Rect badge = {BADGE_X, BADGE_Y, BADGE_W, BADGE_H};
        const SDL_Rect badgeArea = {badge.x - 8, badge.y - 50, badge.w + 16, badge.h + 58};   // 上のタブも入れる
        QuarryReveal::draw(renderer, g_badgeLayer, badge, badgeArea, cubeX, cubeY, ms);
    }
    const SDL_Rect infoArea = {g_infoRect.x - 8, g_infoRect.y - 50, g_infoRect.w + 16, g_infoRect.h + 58};
    QuarryReveal::draw(renderer, g_infoLayer, g_infoRect, infoArea, cubeX, cubeY, ms - DETAIL_INFO_DELAY_MS);
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

// ===== ALL の並べ替え =====

// UTF-8 の文字列を1文字ずつ Unicode の番号（コードポイント）に分解する
static std::vector<char32_t> decodeUtf8(const std::string& s){
    std::vector<char32_t> out;
    size_t i = 0;
    while(i < s.size()){
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp;
        int len;
        if(c < 0x80)             { cp = c;        len = 1; }   // 1バイト（ASCII）
        else if((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }   // 2バイト
        else if((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }   // 3バイト（かな・漢字）
        else if((c >> 3) == 0x1E){ cp = c & 0x07; len = 4; }   // 4バイト（絵文字など）
        else { i++; continue; }                                 // 壊れたバイトは飛ばす

        if(i + len > s.size()) break;
        for(int k = 1; k < len; k++){
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

// 1文字を「グループ（0:英数字 1:かな 2:その他）」と「比較用の番号」に変換する
struct CollateChar{
    int group;
    char32_t key;
};

static CollateChar collateKey(char32_t c){
    if(c >= 0xFF01 && c <= 0xFF5E) c -= 0xFEE0;            // 全角英数記号 → 半角
    if(c >= 'a' && c <= 'z') c -= 32;                      // 小文字 → 大文字
    if(c < 0x80) return {0, c};                            // 英数字・記号

    if(c >= 0x30A1 && c <= 0x30F6) c -= 0x60;              // カタカナ → ひらがな
    if((c >= 0x3041 && c <= 0x3096) || c == 0x30FC){       // ひらがな・長音「ー」
        return {1, c};
    }
    return {2, c};                                         // 漢字など
}

// 曲名の比較：a が b より前なら true
static bool titleLess(const std::string& a, const std::string& b){
    std::vector<char32_t> ca = decodeUtf8(a);
    std::vector<char32_t> cb = decodeUtf8(b);
    size_t n = std::min(ca.size(), cb.size());

    for(size_t i = 0; i < n; i++){
        CollateChar ka = collateKey(ca[i]);
        CollateChar kb = collateKey(cb[i]);
        if(ka.group != kb.group) return ka.group < kb.group;
        if(ka.key != kb.key)     return ka.key < kb.key;
    }
    return ca.size() < cb.size();   // 途中まで同じなら短いほうが前
}

// レベルの文字列を数値にする（"12" → 12、"12+" → 12.5、数字で始まらない → -1）
static double parseLevel(const std::string& s){
    size_t i = 0;
    int value = 0;
    bool hasDigit = false;
    while(i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))){
        value = value * 10 + (s[i] - '0');
        hasDigit = true;
        i++;
    }
    if(!hasDigit) return -1.0;
    return (s.find('+') != std::string::npos) ? value + 0.5 : static_cast<double>(value);
}

void sortAllSongs(GenreInfo& all, SortKey key, bool descending, int difficultyCursor, int& songCursor){
    if(all.songList.empty()) return;

    // 並べ替え後もカーソルを同じ曲に付けるため、今の曲の譜面パスを覚えておく
    int cur = std::clamp(songCursor, 0, static_cast<int>(all.songList.size()) - 1);
    std::string currentPath = all.songList[cur].scorePath;

    // 表示中の難易度のレベル（←→ で選んでいる順番を、その曲の範囲に収めて使う）
    auto levelOf = [difficultyCursor](const SongInfo& s){
        if(s.difficulties.empty()) return parseLevel(s.level);
        int idx = clampDifficulty(difficultyCursor, s.difficulties.size());
        return parseLevel(s.difficulties[idx].level);
    };

    std::stable_sort(all.songList.begin(), all.songList.end(),
        [&](const SongInfo& a, const SongInfo& b){
            if(key == SortKey::Level){
                double la = levelOf(a);
                double lb = levelOf(b);
                bool unknownA = (la < 0);
                bool unknownB = (lb < 0);
                if(unknownA != unknownB) return unknownB;               // レベル不明は常に最後
                if(la != lb) return descending ? (la > lb) : (la < lb);
                return titleLess(a.title, b.title);                     // 同じレベルは曲名の昇順
            }
            return descending ? titleLess(b.title, a.title) : titleLess(a.title, b.title);
        });

    // 覚えておいた曲を探して、カーソルをそこへ移す
    for(size_t i = 0; i < all.songList.size(); i++){
        if(all.songList[i].scorePath == currentPath){
            songCursor = static_cast<int>(i);
            break;
        }
    }
}