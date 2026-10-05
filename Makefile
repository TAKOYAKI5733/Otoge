# ====================================================================
#  Otoge Project - Advanced Makefile (Header Dependency Resolution)
# ====================================================================

# コンパイラと基本フラグ
CXX      := g++
# -MMD -MP フラグ：コンパイル時にヘッダーの依存関係(.dファイル)を自動生成する
CXXFLAGS := -std=c++20 -Wall -Wextra -MMD -MP

# SDL2 などのライブラリ設定
# 🌟 修正: -lSDL2_image を追加(.png画像の読込に必要)
LIBS     := -lSDL2 -lSDL2_ttf -lSDL2_mixer -lSDL2_image

# ディレクトリの定義
SRC_DIR  := src
OBJ_DIR  := obj
BIN_DIR  := bin

# 最終的な実行ファイルの出力パス
TARGET   := $(BIN_DIR)/otoge

# src/ 内のすべての .cpp ファイルと、src/imgui/ 内の .cpp ファイルを自動検知
SRCS     := $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(SRC_DIR)/imgui/*.cpp)
OBJS     := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

# 依存関係ファイル(.d)のリストを自動作成
DEPS     := $(OBJS:.o=.d)

# コピーしたい素材フォルダのリスト
# 🌟 修正: images を追加(ノーツの.png画像フォルダ)
ASSET_DIRS := sounds scores fonts images

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
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# 依存関係ファイルをインクルードして、ヘッダーの変更をMakefileに教える
-include $(DEPS)

.PHONY: all clean copy_assets