#pragma once

#include "GameCommon.h"

class ScoreTracker{
private:
    int maxCombo = 0;
    int currentCombo = 0;
    int displayCombo = 0;
    double currentScore = 1000000.0;
    
    const double LOSS_GOOD = 0.3;
    const double LOSS_MISS = 1.0;

public:
    void init(int totalMaxCombo){
        maxCombo = totalMaxCombo;
        currentCombo = 0;
        displayCombo = 0;
        currentScore = 0.0;
    }

    void registerJudge(const std::string& judge){
        if(maxCombo <= 0) return;

        double scorePerNote = 1000000.0 / static_cast<double>(maxCombo);

        if(judge == "PERFECT"){
            currentCombo++;
            currentScore += scorePerNote;
        }
        else if(judge == "GOOD"){
            currentCombo++;
            currentScore += (scorePerNote * (1.0 - LOSS_GOOD));
        }
        else if(judge == "BAD" || judge == "MISS"){
            currentCombo = 0;
            currentScore += (scorePerNote * (1.0 - LOSS_MISS));
        }

        if(currentCombo > displayCombo){
            displayCombo = currentCombo;
        }
        currentScore = std::min(1000000.0, currentScore);
    }

    int getTrueScore() const {
        return static_cast<int>(std::round(currentScore));
    }

    int getVisualTargetScore() const {
        double currentLoss = 1000000.0 - currentScore;
        return static_cast<int>(std::round(1000000.0 - currentLoss));
    }

    int getScore() const {
        return static_cast<int>(std::round(currentScore));
    }

    int getCurrentCombo() const {
        return currentCombo;
    }

    int getMaxComboAchieved() const {
        return displayCombo;
    }

    void saveHighScore(const std::string& scorePath, int score){
        std::ofstream outFile(scorePath, std::ios::binary);
        if(outFile.is_open()){
            outFile.write(reinterpret_cast<char*>(&score), sizeof(score));
            outFile.close();
        }
    }

    int loadHighScore(const std::string& scorePath){
        int loadedScore = 0;
        std::ifstream inFile(scorePath, std::ios::binary);
        if(inFile.is_open()){
            inFile.read(reinterpret_cast<char*>(&loadedScore), sizeof(loadedScore));
            inFile.close();
        }
        return loadedScore;
    }
};