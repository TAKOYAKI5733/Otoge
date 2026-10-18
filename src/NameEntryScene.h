#pragma once
// NameEntryScene.h
// リザルトの後に出す「名前入力 → 送信 → ランキング表示」画面。
// 他の画面（resultScene など）と同じく、関数の中に自分のループを持つ。
//
// ■ 画面の流れ
//   名前入力 : 日本語入力（IME）対応、10文字まで。Enter で決定、Esc で送信せずに選曲へ
//   送信中   : ScoreSync に記録を渡し、返事を待つ（通信は別スレッドなので画面は止まらない）
//   ランキング: 自分の順位と上位50件（↑↓でスクロール）。Enter で選曲へ

#include "GameCommon.h"
#include "ScoreSync.h"

// プレイ結果から送信用の記録を作る。
//   songId     : 「フォルダ名/譜面ファイル名」（例: "INCHO/High Hopes"）
//   difficulty : 譜面 JSON の難易度名（例: "HARD"）
ScoreRecord makeScoreRecord(const std::string& scorePath, int difficultyIndex, const ResultData& result);

GameScene nameEntryScene(SDL_Window* window, SDL_Renderer* renderer, const ScoreRecord& record);