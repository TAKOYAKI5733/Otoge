#include "GameCommon.h"

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
};

struct GenreInfo{
    std::string genreName;
    std::vector<SongInfo> songList;
    
    double currentScale = 1.0;
};

enum class SelectMode{
    SelectGenre,
    SelectSong
};

//プロトタイプ宣言
SongInfo parseScoreFile(const std::filesystem::path& filePath);
std::vector<GenreInfo> scanScoreFolder(const std::string& baseDir);
void draw_GenreList(SDL_Renderer* renderer, TTF_Font* font, std::vector<GenreInfo>& categories,size_t genreCursor);
void draw_SongList(SDL_Renderer* renderer, TTF_Font* font, std::vector<SongInfo>& songList, size_t songCursor, bool isActive);
void draw_songDetail(SDL_Renderer* renderer, TTF_Font* font, const SongInfo& song, double animation);


//メイン関数
GameScene selectSongScene(SDL_Window* window, SDL_Renderer* renderer, std::string& outSelectedScorePath, SDL_Texture* targetTex){
    std::vector<GenreInfo> categories = scanScoreFolder("scores");
    if(categories.empty()){
        std::cout << "[エラー]曲がねぇ\n";
        return GameScene::Shutdown;
    }

    SelectMode currentMode = SelectMode::SelectGenre;
    int genreCursor = 0;
    int songCursor = 0;

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

        draw_GenreList(renderer, font, categories, genreCursor);
        bool isSongActive = (currentMode == SelectMode::SelectSong);
        draw_SongList(renderer, font, categories[genreCursor].songList, songCursor, isSongActive);
        
        SDL_SetRenderTarget(renderer, NULL);

        TTF_CloseFont(font);
        TTF_CloseFont(sub_font);
        return GameScene::Select;
    }

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
                        else{
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
                        else{
                            int songCount = categories[genreCursor].songList.size();
                            songCursor = (songCursor - 1 + songCount) % songCount;
                            cursorMoved = true;
                        }
                        break;
                    }

                    case SDLK_RETURN:{
                        if(currentMode == SelectMode::SelectGenre){
                            currentMode = SelectMode::SelectSong;
                            songCursor = 0;
                            cursorMoved = true;
                        }
                        else{
                            outSelectedScorePath = categories[genreCursor].songList[songCursor].scorePath;
                            nextScene = GameScene::Load;
                            running = false;
                        }
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

        draw_GenreList(renderer, font, categories, genreCursor);

        bool isSongActive = (currentMode == SelectMode::SelectSong);
        draw_SongList(renderer, font, categories[genreCursor].songList, songCursor, isSongActive);

        if(detailAnimation > 0.001){
            draw_songDetail(renderer, font, categories[genreCursor].songList[songCursor], detailAnimation);
        }

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
SongInfo parseScoreFile(const std::filesystem::path& filePath){
    SongInfo song;
    song.scorePath = filePath.string();
    song.title = filePath.stem().string();

    std::ifstream file(filePath);
    if(file.is_open()){
        std::string line;
        while(std::getline(file, line)){
            if(line.empty() || line.rfind("//", 0) == 0) continue;

            if(line.rfind("BGM:", 0) == 0){
                song.audioExtName = line.substr(4);
            }
            else if(line.rfind("BPM:", 0) == 0){
                std::string input = line.substr(4);
                if(input.empty()){
                    continue;
                }
                song.bpm = std::stod(input);
            }
            else if(line.rfind("COMP:", 0) == 0){
                song.composer = line.substr(5);
            }
            else if(line.rfind("LV:", 0) == 0){
                song.level = line.substr(3);
            }
            else if(line.rfind("CC:", 0) == 0){
                song.ChartCreator = line.substr(3);
            }

            if(line == "#START") break;
        }
    }
    return song;
}

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
                if(subEntry.is_regular_file() && subEntry.path().extension() == ".txt"){
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

void draw_GenreList(SDL_Renderer* renderer, TTF_Font* font, std::vector<GenreInfo>& categories,size_t genreCursor){
    int startY = 300;
    int lineGap = 80;

    for(size_t i = 0; i < categories.size(); i++){
        double targetScale = (i == genreCursor) ? 1.4 : 1.0;

        categories[i].currentScale += (targetScale - categories[i].currentScale) * 0.15;

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

        destRect.x = 200;

        int baseY = startY + (i *  lineGap);
        destRect.y =baseY - (destRect.h - surf->h) / 2;

        SDL_RenderCopy(renderer, tex, NULL, &destRect);

        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }
}

void draw_SongList(SDL_Renderer* renderer, TTF_Font* font, std::vector<SongInfo>& songList, size_t songCursor, bool isActive){
    int startY = 250;
    int lineGap = 80;

    for(size_t i = 0; i < songList.size(); i++){
        double targetScale = (i == songCursor && isActive) ? 1.3 : 1.0;
        songList[i].currentScale += (targetScale - songList[i].currentScale) * 0.15;

        SDL_Color textColor = {150, 150, 150, 255};
        if(isActive){
            textColor = (i == songCursor) ? SDL_Color{0, 255, 255, 255} : SDL_Color{255, 255, 255, 255};
        }

        std::string displayName = songList[i].title;
        if(i == songCursor && isActive){
            displayName = ">> " + displayName; 
        }

        displayName += " (BPM: " + std::to_string(static_cast<int>(songList[i].bpm)) + ")";

        SDL_Surface* surf  = TTF_RenderUTF8_Blended(font, displayName.c_str(), textColor);
        if(!surf) continue;

        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_Rect destRect;
        destRect.w = static_cast<int>(surf->w * songList[i].currentScale);
        destRect.h = static_cast<int>(surf->h * songList[i].currentScale);
        destRect.x = 900;

        int baseY = startY + (i * lineGap);
        destRect.y = baseY - (destRect.h - surf->h) / 2;

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
        {"COMPOSER: " + song.composer, {255, 255, 255, 255}},
        {"CHART CREATOR: " + song.ChartCreator, {255, 255, 255, 255}},
        {"LV: " + song.level, {255, 69, 0, 255}}
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