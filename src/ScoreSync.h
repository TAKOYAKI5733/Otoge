#pragma once
// ScoreSync.h
// GAS ランキングサーバとの通信を担当するクラス。
//
// ■ 動作の理屈
//  ・通信（1回 1〜2秒かかる）をメインループで行うとゲームが止まるので、専用スレッドで行う。
//  ・メインスレッド → 通信スレッド : jobs_ キューに「仕事」を積む（submit / requestRanking）
//  ・通信スレッド → メインスレッド : results_ キューに「結果」を積み、メイン側が毎フレーム poll() で受け取る
//  ・送れなかった記録は pending_ に入れてファイルにも保存し、30秒ごとに自動で再送する。
//    （playId をサーバが見て二重登録を防ぐので、何度再送しても安全）

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ScoreRecord {
    std::string playId;      // 空なら submit() 時に自動生成（1プレイを識別する32桁hex）
    std::string name;        // UTF-8、10文字以内
    std::string songId;      // 曲フォルダ名
    std::string difficulty;  // "EASY" / "NORMAL" / "HARD" / "MASTER" / 特殊 など
    int score = 0;
    int maxCombo = 0;
};

struct RankEntry {
    int rank = 0;
    std::string name;
    int score = 0;
    int maxCombo = 0;
    bool isMine = false;     // 今送った記録なら true（強調表示用）
};

struct SyncResult {
    enum class Kind { Submit, Ranking };
    Kind kind = Kind::Submit;
    bool ok = false;
    bool queuedOffline = false;  // 送れなかったので保存して後で再送する
    std::string error;
    std::string songId, difficulty;
    int myRank = 0;              // 自分の順位（Submit のみ。50位圏外でも入る）
    int total = 0;               // その曲×難易度の総記録数
    std::vector<RankEntry> entries;  // 上位50件
};

class ScoreSync {
public:
    static ScoreSync& instance();

    // configPath（例: config/online.json）を読んで通信スレッドを起動する。
    // 設定が無い／不正なら false を返し、オンライン機能なしでゲームは普通に動く。
    bool start(const std::string& configPath, const std::string& pendingPath);
    void stop();                         // 終了時に必ず呼ぶ（通信中でもすぐ止まる）
    bool enabled() const { return running_.load(); }

    void submit(ScoreRecord rec);        // 記録を送る（すぐ戻る）
    void requestRanking(const std::string& songId, const std::string& difficulty);
    bool poll(SyncResult& out);          // 結果があれば true（メインスレッドで毎フレーム呼ぶ）
    size_t pendingCount();               // 未送信の記録数

    ScoreSync(const ScoreSync&) = delete;
    ScoreSync& operator=(const ScoreSync&) = delete;

private:
    ScoreSync() = default;
    ~ScoreSync();

    struct Job {
        enum class Kind { Submit, Ranking } kind = Kind::Submit;
        ScoreRecord rec;
        std::string songId, difficulty;
    };
    enum class Outcome { Ok, Retry, Reject };  // Retry=後で再送 / Reject=何度送っても無駄

    void workerLoop();
    void retryPending();
    Outcome sendRecord(const ScoreRecord& rec, SyncResult& out);
    void fetchRanking(const std::string& songId, const std::string& difficulty, SyncResult& out);
    bool httpRequest(const std::string& url, const std::string* postBody,
                     std::string& response, std::string& error);

    void addPending(const ScoreRecord& rec);
    void removePending(const std::string& playId);
    void loadPending();
    void savePendingLocked();            // pendingMutex_ を持った状態で呼ぶ
    void pushResult(SyncResult r);

    std::string url_, apiKey_, machineId_, pendingPath_;

    std::thread worker_;
    std::mutex jobMutex_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;

    std::mutex resultMutex_;
    std::deque<SyncResult> results_;

    std::mutex pendingMutex_;
    std::vector<ScoreRecord> pending_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
};