// ScoreSync.cpp
// 理屈は ScoreSync.h の先頭コメントを参照。

#include "ScoreSync.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr auto kRetryInterval = std::chrono::seconds(30);

// 1プレイを識別する 128bit の乱数を 32桁hex にする
std::string makePlayId() {
    static std::mt19937_64 rng{std::random_device{}() ^
                               (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count()};
    static std::mutex m;
    std::lock_guard<std::mutex> lk(m);
    char buf[33];
    std::snprintf(buf, sizeof(buf), "%016llx%016llx",
                  (unsigned long long)rng(), (unsigned long long)rng());
    return buf;
}

// URL のクエリに日本語（曲フォルダ名）を入れるためのパーセントエンコード
std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

// 通信中に 0 以外を返すと libcurl が転送を中断する → stop() 時に待たされない
int progressCallback(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<std::atomic<bool>*>(userdata)->load() ? 1 : 0;
}

// サーバ応答の entries 等を SyncResult に詰める。
// .value() を使うので、キーが欠けていても例外にならず既定値になる
void parseRanking(const json& j, SyncResult& out) {
    out.myRank = j.value("myRank", 0);
    out.total  = j.value("total", 0);
    out.entries.clear();
    auto it = j.find("entries");
    if (it == j.end() || !it->is_array()) return;
    for (const auto& e : *it) {
        if (!e.is_object()) continue;
        RankEntry r;
        r.rank     = e.value("rank", 0);
        r.name     = e.value("name", std::string());
        r.score    = e.value("score", 0);
        r.maxCombo = e.value("maxCombo", 0);
        r.isMine   = e.value("mine", false);
        out.entries.push_back(std::move(r));
    }
}

json recordToJson(const ScoreRecord& r) {
    return json{{"playId", r.playId}, {"name", r.name}, {"songId", r.songId},
                {"difficulty", r.difficulty}, {"score", r.score}, {"maxCombo", r.maxCombo}};
}

}  // namespace

// ---------------------------------------------------------------------
// 起動・停止
// ---------------------------------------------------------------------

ScoreSync& ScoreSync::instance() {
    static ScoreSync s;
    return s;
}

ScoreSync::~ScoreSync() { stop(); }

bool ScoreSync::start(const std::string& configPath, const std::string& pendingPath) {
    if (running_) return true;

    std::ifstream ifs(configPath);
    if (!ifs) {
        std::fprintf(stderr, "[ScoreSync] %s が無いのでオンライン機能は無効\n", configPath.c_str());
        return false;
    }
    json cfg = json::parse(ifs, nullptr, false);
    if (cfg.is_discarded() || !cfg.is_object()) {
        std::fprintf(stderr, "[ScoreSync] %s の JSON が不正\n", configPath.c_str());
        return false;
    }
    url_       = cfg.value("url", std::string());
    apiKey_    = cfg.value("apiKey", std::string());
    machineId_ = cfg.value("machineId", std::string("PC"));
    if (url_.rfind("https://", 0) != 0 || apiKey_.empty()) {
        std::fprintf(stderr, "[ScoreSync] url または apiKey が未設定\n");
        return false;
    }

    pendingPath_ = pendingPath;
    curl_global_init(CURL_GLOBAL_DEFAULT);  // スレッドを作る前に1回だけ呼ぶ決まり
    loadPending();

    stopping_ = false;
    running_ = true;
    worker_ = std::thread(&ScoreSync::workerLoop, this);
    return true;
}

void ScoreSync::stop() {
    if (!running_) return;
    stopping_ = true;
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    curl_global_cleanup();
    running_ = false;
}

// ---------------------------------------------------------------------
// メインスレッドから呼ぶ API
// ---------------------------------------------------------------------

void ScoreSync::submit(ScoreRecord rec) {
    if (rec.playId.empty()) rec.playId = makePlayId();

    if (!running_) {
        SyncResult r;
        r.kind = SyncResult::Kind::Submit;
        r.songId = rec.songId;
        r.difficulty = rec.difficulty;
        r.error = "オンライン機能が無効です (config/online.json を確認)";
        pushResult(std::move(r));
        return;
    }
    Job job;
    job.kind = Job::Kind::Submit;
    job.rec = std::move(rec);
    {
        std::lock_guard<std::mutex> lk(jobMutex_);
        jobs_.push_back(std::move(job));
    }
    cv_.notify_one();
}

void ScoreSync::requestRanking(const std::string& songId, const std::string& difficulty) {
    if (!running_) {
        SyncResult r;
        r.kind = SyncResult::Kind::Ranking;
        r.songId = songId;
        r.difficulty = difficulty;
        r.error = "オンライン機能が無効です";
        pushResult(std::move(r));
        return;
    }
    Job job;
    job.kind = Job::Kind::Ranking;
    job.songId = songId;
    job.difficulty = difficulty;
    {
        std::lock_guard<std::mutex> lk(jobMutex_);
        jobs_.push_back(std::move(job));
    }
    cv_.notify_one();
}

bool ScoreSync::poll(SyncResult& out) {
    std::lock_guard<std::mutex> lk(resultMutex_);
    if (results_.empty()) return false;
    out = std::move(results_.front());
    results_.pop_front();
    return true;
}

size_t ScoreSync::pendingCount() {
    std::lock_guard<std::mutex> lk(pendingMutex_);
    return pending_.size();
}

void ScoreSync::pushResult(SyncResult r) {
    std::lock_guard<std::mutex> lk(resultMutex_);
    results_.push_back(std::move(r));
}

// ---------------------------------------------------------------------
// 通信スレッド
// ---------------------------------------------------------------------

void ScoreSync::workerLoop() {
    retryPending();  // 前回送れなかった分を起動直後に送る
    auto nextRetry = std::chrono::steady_clock::now() + kRetryInterval;

    while (true) {
        Job job;
        bool haveJob = false;
        {
            std::unique_lock<std::mutex> lk(jobMutex_);
            // 仕事が来る / 停止要求 / 再送時刻 のどれかまで眠る（CPU を使わない）
            cv_.wait_until(lk, nextRetry, [&] { return stopping_.load() || !jobs_.empty(); });
            if (stopping_) break;
            if (!jobs_.empty()) {
                job = std::move(jobs_.front());
                jobs_.pop_front();
                haveJob = true;
            }
        }

        if (haveJob) {
            SyncResult r;
            if (job.kind == Job::Kind::Submit) {
                r.kind = SyncResult::Kind::Submit;
                r.songId = job.rec.songId;
                r.difficulty = job.rec.difficulty;
                if (sendRecord(job.rec, r) == Outcome::Retry) {
                    addPending(job.rec);
                    r.queuedOffline = true;
                }
            } else {
                fetchRanking(job.songId, job.difficulty, r);
            }
            pushResult(std::move(r));
        } else if (std::chrono::steady_clock::now() >= nextRetry) {
            retryPending();
            nextRetry = std::chrono::steady_clock::now() + kRetryInterval;
        }
    }
}

void ScoreSync::retryPending() {
    std::vector<ScoreRecord> snapshot;
    {
        std::lock_guard<std::mutex> lk(pendingMutex_);
        snapshot = pending_;
    }
    for (const auto& rec : snapshot) {
        if (stopping_) return;
        SyncResult dummy;
        Outcome oc = sendRecord(rec, dummy);
        if (oc == Outcome::Retry) break;   // まだ繋がらない → 残りも次回に回す
        removePending(rec.playId);         // 成功 or 送っても無駄 → キューから消す
    }
}

ScoreSync::Outcome ScoreSync::sendRecord(const ScoreRecord& rec, SyncResult& out) {
    json body = recordToJson(rec);
    body["action"] = "submit";
    body["key"] = apiKey_;
    body["machineId"] = machineId_;
    // 不正な UTF-8 が混ざっていても例外を投げず置換文字にする
    std::string payload = body.dump(-1, ' ', false, json::error_handler_t::replace);

    std::string resp, err;
    if (!httpRequest(url_, &payload, resp, err)) {
        out.error = "通信エラー: " + err;
        return Outcome::Retry;
    }
    json j = json::parse(resp, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        out.error = "サーバの応答がJSONではありません (GASのデプロイ設定を確認)";
        return Outcome::Retry;
    }
    if (!j.value("ok", false)) {
        std::string e = j.value("error", std::string("unknown"));
        out.error = "サーバが拒否: " + e;
        // busy=混雑, unauthorized=合言葉の設定ミス → 直せば通るので保存して再送
        // それ以外（名前やスコアが不正）は何度送っても通らないので捨てる
        return (e == "busy" || e == "unauthorized") ? Outcome::Retry : Outcome::Reject;
    }
    parseRanking(j, out);
    out.ok = true;
    return Outcome::Ok;
}

void ScoreSync::fetchRanking(const std::string& songId, const std::string& difficulty,
                             SyncResult& out) {
    out.kind = SyncResult::Kind::Ranking;
    out.songId = songId;
    out.difficulty = difficulty;

    std::string url = url_ + "?mode=ranking&key=" + urlEncode(apiKey_) +
                      "&songId=" + urlEncode(songId) + "&difficulty=" + urlEncode(difficulty);
    std::string resp, err;
    if (!httpRequest(url, nullptr, resp, err)) {
        out.error = "通信エラー: " + err;
        return;
    }
    json j = json::parse(resp, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.value("ok", false)) {
        out.error = "ランキングを取得できませんでした";
        return;
    }
    parseRanking(j, out);
    out.ok = true;
}

// GET（postBody == nullptr）または POST を行い、本文を response に入れる
bool ScoreSync::httpRequest(const std::string& url, const std::string* postBody,
                            std::string& response, std::string& error) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init 失敗";
        return false;
    }
    curl_slist* headers = nullptr;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    // GAS は応答を別URLへの 302 リダイレクトで返す。追いかけないと本文が取れない。
    // POST に対する 302 では libcurl は自動で GET に切り替えて追う → GAS が期待する動きそのもの。
    // ※ CURLOPT_CUSTOMREQUEST "POST" を使うとリダイレクト先にも POST してしまい失敗するので使わない
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // マルチスレッドでのタイムアウトにシグナルを使わせない
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Simple.notes/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stopping_);
#ifdef _WIN32
    // 配布先の Windows に MSYS2 の証明書ファイルは無いので、Windows 標準の証明書ストアを使う
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
#endif

    if (postBody) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postBody->c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)postBody->size());
    }

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        error = curl_easy_strerror(rc);
        return false;
    }
    if (status != 200) {
        error = "HTTP " + std::to_string(status);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------
// 未送信キュー（ファイルに保存するので、ゲームを閉じても消えない）
// ---------------------------------------------------------------------

void ScoreSync::addPending(const ScoreRecord& rec) {
    std::lock_guard<std::mutex> lk(pendingMutex_);
    for (const auto& p : pending_)
        if (p.playId == rec.playId) return;
    pending_.push_back(rec);
    savePendingLocked();
}

void ScoreSync::removePending(const std::string& playId) {
    std::lock_guard<std::mutex> lk(pendingMutex_);
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it->playId == playId) {
            pending_.erase(it);
            savePendingLocked();
            return;
        }
    }
}

void ScoreSync::loadPending() {
    std::lock_guard<std::mutex> lk(pendingMutex_);
    pending_.clear();
    std::ifstream ifs(pendingPath_);
    if (!ifs) return;
    json arr = json::parse(ifs, nullptr, false);
    if (arr.is_discarded() || !arr.is_array()) return;
    for (const auto& e : arr) {
        if (!e.is_object()) continue;
        ScoreRecord r;
        r.playId     = e.value("playId", std::string());
        r.name       = e.value("name", std::string());
        r.songId     = e.value("songId", std::string());
        r.difficulty = e.value("difficulty", std::string());
        r.score      = e.value("score", 0);
        r.maxCombo   = e.value("maxCombo", 0);
        if (!r.playId.empty()) pending_.push_back(std::move(r));
    }
}

void ScoreSync::savePendingLocked() {
    std::error_code ec;
    fs::path p(pendingPath_);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);

    json arr = json::array();
    for (const auto& r : pending_) arr.push_back(recordToJson(r));
    std::ofstream ofs(pendingPath_, std::ios::trunc);
    if (ofs) ofs << arr.dump(2, ' ', false, json::error_handler_t::replace);
}