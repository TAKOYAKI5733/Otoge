# ====================================================================
#  Otoge Project - Advanced Makefile (Header Dependency Resolution)
#  Linux (Ubuntu) 用
# ====================================================================

# コンパイラと基本フラグ
CXX      := g++
# -MMD -MP フラグ：コンパイル時にヘッダーの依存関係(.dファイル)を自動生成する
# 🌟 追加: $(shell sdl2-config --cflags) で /usr/include/SDL2 を検索場所に入れる
#          （ImGui の imgui_impl_sdl2.cpp は <SDL.h> と書いているため、これが無いと見つからない）
CXXFLAGS := -std=c++20 -Wall -Wextra -MMD -MP $(shell sdl2-config --cflags)

# SDL2 などのライブラリ設定
# 🌟 修正: -lSDL2_image を追加(.png画像の読込に必要)
# 🌟 追加: -lcurl（ScoreSync.cpp がランキングサーバ(GAS)と通信するのに使う）
#          事前に sudo apt install libcurl4-openssl-dev が必要
LIBS     := -lSDL2 -lSDL2_ttf -lSDL2_mixer -lSDL2_image -lcurl

# ディレクトリの定義
SRC_DIR  := src
OBJ_DIR  := obj
BIN_DIR  := bin

# 最終的な実行ファイルの出力パス
TARGET   := $(BIN_DIR)/otoge

# src/ 内のすべての .cpp ファイルと、src/imgui/ 内の .cpp ファイルを自動検知
# （ScoreSync.cpp / NameEntryScene.cpp も src/ に置けば自動で入る）
SRCS     := $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(SRC_DIR)/imgui/*.cpp)
OBJS     := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

# 依存関係ファイル(.d)のリストを自動作成
DEPS     := $(OBJS:.o=.d)

# コピーしたい素材フォルダのリスト
# 🌟 修正: images を追加(ノーツの.png画像フォルダ)
# 🌟 追加: config（config/online.json = ランキングサーバの URL・合言葉）
ASSET_DIRS := sounds scores fonts images config

# ---------------------------------------------------
# メインタスク
# ---------------------------------------------------
all: $(TARGET) copy_assets

# 1. 最終的な実行ファイルのリンク処理
$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBS)

# 2. 素材フォルダを実行ファイルと同じ場所に自動コピーする処理
copy_assets:
	@mkdir -p $(BIN_DIR)
	@for dir in $(ASSET_DIRS); do \
	    if [ -d $$dir ]; then \
	        echo "Copying $$dir to $(BIN_DIR)/..."; \
	        cp -r $$dir $(BIN_DIR)/; \
	    fi; \
	done

# 3. 各 .cpp ファイルを .o ファイルにコンパイルする処理
# -I$(SRC_DIR) / -I$(SRC_DIR)/imgui で自作ヘッダー・ImGuiヘッダーを解決
# $(dir $@) で、obj/imgui/ のようなサブディレクトリも自動作成する
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -I$(SRC_DIR)/imgui -c $< -o $@

# 4. 生成されたフォルダやファイルをすべて削除するコマンド
# ⚠ bin/save/pending_scores.json（未送信のスコア）も消えるので、文化祭の当日は使わないこと
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# 依存関係ファイルをインクルードして、ヘッダーの変更をMakefileに教える
-include $(DEPS)

.PHONY: all clean copy_assets