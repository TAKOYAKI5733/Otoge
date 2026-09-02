#include "GameCommon.h"
#include <nlohmann/json.hpp>
#include "Play/PlaySceneManager.h"

#define SCREEN_W 1920
#define SCREEN_H 1080

struct SongInfo{
    std::string title;
    std::string scorePath;
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
};

enum class SelectMode{
    SelectGenre,
    SelectSong,
    SelectDifficulty
};

//プロトタイプ宣言
std::vector<GenreInfo> scanScoreFolder(const std::string& baseDir);
void draw_GenreList(SDL_Renderer* renderer, TTF_Font* font, std::vector<GenreInfo>& categories,size_t genreCursor, bool isSongMode, SDL_Rect* outSelectedGenreRect);
void draw_SongList(SDL_Renderer* renderer, TTF_Font* font, std::vector<SongInfo>& songList, size_t songCursor, SelectMode currentMode, const SDL_Rect* priorityRect);
void draw_songDetail(SDL_Renderer* renderer, TTF_Font* font, const SongInfo& song, double animation);
void draw_DifficultyList(SDL_Renderer* renderer, TTF_Font* font, std::vector<DifficultyInfo>& difficulties, size_t difficultyCursor, SelectMode currentMode);
SongInfo parseScoreFile(const std::filesystem::path& filePath);


//メイン関数
GameScene selectSongScene(SDL_Window* window, SDL_Renderer* renderer, std::string& outSelectedScorePath, int& outSelectedDifficulty, PlayerSettings& playerSettings, SDL_Texture* targetTex){
    std::vector<GenreInfo> categories = scanScoreFolder("scores");
    if(categories.empty()){
        std::cout << "[エラー]曲がねぇ\n";
        return GameScene::Shutdown;
    }

    SelectMode currentMode = SelectMode::SelectGenre;
    int genreCursor = 0;
    int songCursor = 0;
    int difficultyCursor = 0;

    TTF_Font* font = TTF_OpenFont("fonts/prac.ttf", 40);
    TTF_Font* sub_font = TTF_OpenFont("fonts/prac.ttf", 35);
    if(!font || !sub_font){
        std::cout << "選曲画面のフォント読込失敗\n";
        return GameScene::Shutdown;
    }

    Mix_Music* bgm = nullptr;
    std::string currentPlayPath = "";
    std::string dafaultBgmPath = "sounds/十番街、雨【シティ探索BGM】.mp3";
    Mix_VolumeMusic(70);

    double detailAnimation = 0.0;

    bool running = true;
    SDL_Event e;
    GameScene nextScene = GameScene::Shutdown;

    if(targetTex != nullptr){
        SDL_SetRenderTarget(renderer, targetTex);

        SDL_SetRenderDrawColor(renderer, 15, 15, 25, 255);
        SDL_RenderClear(renderer);

        bool genreCollapsed = (currentMode != SelectMode::SelectGenre);
        SDL_Rect selectedGenreRect = {0, 0, 0, 0};

        draw_GenreList(renderer, font, categories, genreCursor, genreCollapsed, &selectedGenreRect);
        draw_SongList(renderer, font, categories[genreCursor].songList, songCursor, currentMode, &selectedGenreRect);
        draw_DifficultyList(renderer, font, categories[genreCursor].songList[songCursor].difficulties, difficultyCursor, currentMode);
        
        SDL_SetRenderTarget(renderer, NULL);

        TTF_CloseFont(font);
        TTF_CloseFont(sub_font);
        return GameScene::Select;
    }

    while(running){
        std::string targetMusicPath = dafaultBgmPath;

        if(currentMode == SelectMode::SelectSong || currentMode == SelectMode::SelectDifficulty){
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
                        if(currentMode == SelectMode::SelectDifficulty){
                            currentMode = SelectMode::SelectSong;
                        }
                        else if(currentMode == SelectMode::SelectSong){
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
                        else{
                            int diffCount = static_cast<int>(categories[genreCursor].songList[songCursor].difficulties.size());
                            if(diffCount > 0) difficultyCursor = (difficultyCursor + 1 + diffCount) % diffCount;
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
                        else{
                            int diffCount = static_cast<int>(categories[genreCursor].songList[songCursor].difficulties.size());
                            if(diffCount > 0) difficultyCursor = (difficultyCursor - 1 + diffCount) % diffCount;
                        }
                        break;
                    }

                    case SDLK_RETURN:{
                        if(currentMode == SelectMode::SelectGenre){
                            currentMode = SelectMode::SelectSong;
                            songCursor = 0;
                            cursorMoved = true;
                        }
                        else if(currentMode == SelectMode::SelectSong){
                            outSelectedScorePath = categories[genreCursor].songList[songCursor].scorePath;
                            difficultyCursor = 0;

                            currentMode = SelectMode::SelectDifficulty;

                            if(categories[genreCursor].songList[songCursor].difficulties.empty()){
                                outSelectedScorePath = categories[genreCursor].songList[songCursor].scorePath;
                                outSelectedDifficulty = 0;
                                nextScene = GameScene::Load;
                                running = false;
                            }
                        }
                        else{
                            outSelectedScorePath = categories[genreCursor].songList[songCursor].scorePath;
                            outSelectedDifficulty = difficultyCursor;
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

        bool genreCollapsed = (currentMode != SelectMode::SelectGenre);
        SDL_Rect selectedGenreRect = {0, 0, 0, 0};

        draw_GenreList(renderer, font, categories, genreCursor, genreCollapsed, &selectedGenreRect);
        draw_SongList(renderer, font, categories[genreCursor].songList, songCursor, currentMode, genreCollapsed ? &selectedGenreRect : nullptr);

        if(detailAnimation > 0.001){
            draw_songDetail(renderer, font, categories[genreCursor].songList[songCursor], detailAnimation);
        }

        draw_DifficultyList(renderer, font, categories[genreCursor].songList[songCursor].difficulties, difficultyCursor, currentMode);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    if(bgm){
        Mix_HaltMusic();
        Mix_FreeMusic(bgm);
    }

    TTF_CloseFont(font);
    TTF_CloseFont(sub_font);
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
        
        const double UNINITIALIZED_THRESHOLD = -100000.0;

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

        SDL_RenderCopy(renderer, tex, NULL, &destRect);

        if(isSongMode && i == genreCursor && outSelectedGenreRect != nullptr){
            *outSelectedGenreRect = destRect;
        }

        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }
}

void draw_SongList(SDL_Renderer* renderer, TTF_Font* font, std::vector<SongInfo>& songList, size_t songCursor, SelectMode currentMode, const SDL_Rect* priorityRect){
    int lineGap = 80;
    int centerY = SCREEN_H / 2;

    const double selectedSongTargetX = 100.0;
    const double selectedSongTargetY = 240.0;
    const double offscreenSongTargetX = -800.0;

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
        else if(currentMode == SelectMode::SelectSong){
            targetScale = isCursor ? 1.3 : 1.0;
            targetX = 200.0;
            targetY = centerY + (static_cast<double>(i) - static_cast<double>(songCursor)) * lineGap;
            colorActive = true;
        }
        else{
            if(isCursor){
                targetScale = 1.0;
                targetX = selectedSongTargetX;
                targetY = selectedSongTargetY;
            }
            else{
                targetScale = 1.0;
                targetX = offscreenSongTargetX;
                targetY = centerY + (static_cast<double>(i) - static_cast<double>(songCursor)) * lineGap;
            }
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
            if(currentMode == SelectMode::SelectDifficulty && isCursor){
                textColor = SDL_Color{255, 215, 0, 255};
            }
            else{
                textColor = isCursor ? SDL_Color{0, 255, 255, 255} : SDL_Color{255, 255, 255, 255};
            }
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

        if(priorityRect != nullptr && SDL_HasIntersection(&destRect, priorityRect)){
            SDL_FreeSurface(surf);
            continue;
        }

        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_RenderCopy(renderer, tex, NULL, &destRect);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }
}

void draw_songDetail(SDL_Renderer* renderer, TTF_Font* font, const SongInfo& song, double animation){
    int alpha = static_cast<int>(animation * 255);
    int offsetX = static_cast<int>((1.0 - animation) * 100);

    int baseX = 1350 + offsetX;
    int baseY = 750;
    int lineGap = 50;

    std::vector<std::pair<std::string, SDL_Color>> infoLines = {
        {"COMPOSER: " + song.composer, {255, 255, 255, 255}}
    };

    for(size_t i = 0; i < infoLines.size(); i++){
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, infoLines[i].first.c_str(), infoLines[i].second);
        if(!surf) continue;

        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);

        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(tex, alpha);

        SDL_Rect destrect = {baseX, baseY + static_cast<int>(i * lineGap), surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, NULL, &destrect);

        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }
    return;
}

SongInfo parseScoreFile(const std::filesystem::path& filePath){
    SongInfo song;
    song.scorePath = filePath.string();
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

    song.difficulties = listDifficulties(song.scorePath);

    return song;
}

void draw_DifficultyList(SDL_Renderer* renderer, TTF_Font* font, std::vector<DifficultyInfo>& difficulties, size_t difficultyCursor, SelectMode currentMode){
    int lineGap = 80;
    int centerY = SCREEN_H / 2;

    const double offscreenTargetX = -800.0;
    const double waitingTargetX = 900;

    for(size_t i = 0; i < difficulties.size(); i++){
        bool isCursor = (i == difficultyCursor);

        double targetScale;
        double targetX;
        double targetY;
        bool colorActive;

        if(currentMode == SelectMode::SelectGenre){
            targetScale = 1.0;
            targetX = offscreenTargetX;
            targetY = centerY + (static_cast<double>(i) - static_cast<double>(difficultyCursor)) * lineGap;
            colorActive = false;
        }
        else if(currentMode == SelectMode::SelectSong){
            targetScale = 1.0;
            targetX = waitingTargetX;
            targetY = centerY + (static_cast<double>(i) - static_cast<double>(difficultyCursor)) * lineGap;
            colorActive = false;
        }
        else{
            targetScale = isCursor ? 1.3 : 1.0;
            targetX = 200.0;
            targetY = centerY + (static_cast<double>(i) - static_cast<double>(difficultyCursor)) * lineGap;
            colorActive = true;
        }

        difficulties[i].currentScale += (targetScale - difficulties[i].currentScale) * 0.15;

        if(!difficulties[i].positionInitialized){
            difficulties[i].currentX = targetX;
            difficulties[i].currentY = targetY;
            difficulties[i].positionInitialized = true;
        }
        else{
            difficulties[i].currentX += (targetX - difficulties[i].currentX) * 0.15;
            difficulties[i].currentY += (targetY - difficulties[i].currentY) * 0.15;
        }

        SDL_Color textColor = {150, 150, 150, 255};
        if(colorActive){
            textColor = isCursor ? SDL_Color{0, 255, 255, 255} : SDL_Color{255, 255, 255, 255};
        }

        std::string displayName = difficulties[i].name + " (Lv." + difficulties[i].level + ")";
        if(isCursor && colorActive) displayName = ">> " + displayName;

        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, displayName.c_str(), textColor);
        if(!surf) continue;

        SDL_Rect destRect;
        destRect.w = static_cast<int>(surf->w * difficulties[i].currentScale);
        destRect.h = static_cast<int>(surf->h * difficulties[i].currentScale);
        destRect.x = static_cast<int>(difficulties[i].currentX);

        int baseY = static_cast<int>(difficulties[i].currentY);
        destRect.y = baseY - (destRect.h - surf->h) / 2 - destRect.h / 2;

        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_RenderCopy(renderer, tex, NULL, &destRect);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }
}