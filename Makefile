# ====================================================================
#  Otoge Project - Advanced Makefile (Header Dependency Resolution)
# ====================================================================

# コンパイラと基本フラグ
CXX      := g++
# 🌟 -MMD -MP フラグを追加：コンパイル時にヘッダーの依存関係（.dファイル）を自動生成する
CXXFLAGS := -std=c++20 -Wall -Wextra -MMD -MP

# SDL2 などのライブラリ設定
LIBS     := -lSDL2 -lSDL2_ttf -lSDL2_mixer

# ディレクトリの定義
SRC_DIR  := src
OBJ_DIR  := obj
BIN_DIR  := bin

# 最終的な実行ファイルの出力パス
TARGET   := $(BIN_DIR)/otoge

# 🌟 src/ 内のすべての .cpp ファイルを自動検知 (LoadScene.cpp も自動で対象になります)
SRCS     := $(wildcard $(SRC_DIR)/*.cpp)
# オブジェクトファイルの配置を obj/ 以下に設定
OBJS     := $(SRCS:$(SRC_DIR)/%_dir=$(OBJ_DIR)/%) # 汎用パス変換
OBJS     := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

# 🌟 依存関係ファイル（.d）のリストを自動作成
DEPS     := $(OBJS:.o=.d)

# コピーしたい素材フォルダのリスト
ASSET_DIRS := sounds scores fonts

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
# 🌟 -I$(SRC_DIR) を指定しているため、コード内で #include "Scene.h" と直接書けます
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -c $< -o $@

# 4. 生成されたフォルダやファイルをすべて削除するコマンド
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# 🌟 依存関係ファイルをインクルードして、ヘッダーの変更を Makefile に教えてあげる
-include $(DEPS)

.PHONY: all clean copy_assets