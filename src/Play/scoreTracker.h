#pragma once

#include "GameCommon.h"

// =====================================================================
//  スコアの計算  src/Play/scoreTracker.h
//
//  ■ 2 つのスコア
//    ① 1,000,000 点のスコア（今まで通り）
//        1 ノーツの点 = 1,000,000 ÷ ノーツの数
//        S-PERFECT・PERFECT：1 ノーツの点そのまま / GOOD：7 割 / BAD：4 分の 1 / MISS：0
//    ② パーセントのスコア（新しく追加）
//        101% から始まり、判定ごとに減っていく（取りこぼした分だけ減る「減点式」）。
//        1 ノーツの値（S-PERFECT を 101 とした割合）を決めておき、
//            パーセント = 101 − Σ（101 − そのノーツの値）÷ ノーツの数
//        S-PERFECT：101（減らない） / PERFECT：100 / GOOD：70 / BAD：25 / MISS：0
//        → 全部 S-PERFECT なら 101%、全部 PERFECT なら 100%、全部 GOOD なら 70%
//        （GOOD・BAD の値は、①の 1,000,000 点のスコアの割合にそろえてある）
//
//  ■ 判定の名前（registerJudge に渡す文字）
//    "S_PERFECT"（PERFECT の中でも ±40ms 以内）, "PERFECT", "GOOD", "BAD", "MISS"
//    ・BAD ではコンボが切れない（GOOD と同じくコンボが 1 増える）
//    ・getPerfectCount() は S-PERFECT も含めた PERFECT の数（リザルトの PERFECT の数は今まで通り）。
//      S-PERFECT だけの数は getSPerfectCount()
// =====================================================================
class ScoreTracker{
private:
    int maxCombo = 0;
    int currentCombo = 0;
    int displayCombo = 0;
    double currentScore = 1000000.0;

    const double LOSS_GOOD = 0.3;
    const double LOSS_BAD  = 0.75;   // BAD は PERFECT の 4 分の 1 の点
    const double LOSS_MISS = 1.0;

    // パーセントのスコア：1 ノーツの値（S-PERFECT = 101）
    static constexpr double PCT_MAX       = 101.0;
    static constexpr double PCT_S_PERFECT = 101.0;
    static constexpr double PCT_PERFECT   = 100.0;
    static constexpr double PCT_GOOD      = 70.0;
    static constexpr double PCT_BAD       = 25.0;
    static constexpr double PCT_MISS      = 0.0;
    double percentLoss = 0.0;   // これまでに減った分の合計（ノーツの数で割る前）

    int sPerfectCount = 0;
    int perfectCount = 0;   // S-PERFECT も含む
    int goodCount = 0;
    int badCount = 0;
    int missCount = 0;

    int tickCount = 0;        // ロングの中継点で増えたコンボの数
    int totalCombo = 0;       // 全部つなげたときのコンボ数（ノーツ ＋ 中継点）

    void addCombo(){
        currentCombo++;
        if(currentCombo > displayCombo) displayCombo = currentCombo;
    }

public:
    // totalMaxCombo：スコアの計算に使う「ノーツの数」（中継点は入れない）
    void init(int totalMaxCombo){
        maxCombo = totalMaxCombo;
        currentCombo = 0;
        displayCombo = 0;
        currentScore = 0.0;
        percentLoss = 0.0;

        sPerfectCount = 0;
        perfectCount = 0;
        goodCount = 0;
        badCount = 0;
        missCount = 0;

        tickCount = 0;
        totalCombo = totalMaxCombo;
    }

    // 全部つなげたときのコンボ数を覚えておく（リザルトなどで「最大コンボ / 理論値」を出すとき用）
    void setTotalCombo(int n){ totalCombo = n; }
    int getTotalCombo() const { return totalCombo; }

    void registerJudge(const std::string& judge){
        if(maxCombo <= 0) return;

        const double scorePerNote = 1000000.0 / static_cast<double>(maxCombo);

        if(judge == "S_PERFECT"){
            addCombo();
            currentScore += scorePerNote;
            sPerfectCount++;
            perfectCount++;
            percentLoss += PCT_MAX - PCT_S_PERFECT;
        }
        else if(judge == "PERFECT"){
            addCombo();
            currentScore += scorePerNote;
            perfectCount++;
            percentLoss += PCT_MAX - PCT_PERFECT;
        }
        else if(judge == "GOOD"){
            addCombo();
            currentScore += scorePerNote * (1.0 - LOSS_GOOD);
            goodCount++;
            percentLoss += PCT_MAX - PCT_GOOD;
        }
        else if(judge == "BAD"){
            addCombo();   // BAD ではコンボを切らない
            currentScore += scorePerNote * (1.0 - LOSS_BAD);
            badCount++;
            percentLoss += PCT_MAX - PCT_BAD;
        }
        else if(judge == "MISS"){
            currentCombo = 0;
            currentScore += scorePerNote * (1.0 - LOSS_MISS);
            missCount++;
            percentLoss += PCT_MAX - PCT_MISS;
        }

        currentScore = std::min(1000000.0, currentScore);
    }

    // ロングの中継点：コンボだけ 1 増やす
    //   スコアと PERFECT の数は増やさない → フルコンボでも「ノーツの数」と「コンボの数」が違うものになる
    void registerTick(){
        tickCount++;
        addCombo();
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

    // パーセントのスコア（101.0 から減っていく。0 未満にはならない）
    double getPercent() const {
        if(maxCombo <= 0) return PCT_MAX;
        return std::max(0.0, PCT_MAX - percentLoss / static_cast<double>(maxCombo));
    }

    int getCurrentCombo() const {
        return currentCombo;
    }

    int getMaxComboAchieved() const {
        return displayCombo;
    }

    int getSPerfectCount() const { return sPerfectCount; }
    int getPerfectCount() const { return perfectCount; }
    int getGoodCount() const { return goodCount; }
    int getBadCount() const { return badCount; }
    int getMissCount() const { return missCount; }
    int getTickCount() const { return tickCount; }

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