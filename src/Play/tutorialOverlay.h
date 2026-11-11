#pragma once

#include "GameCommon.h"
#include <nlohmann/json.hpp>
#include "Play/roundedRect.h"
#include "Play/drawFrame.h"      // ノーツの形（NOTE_HEAD_H など）・隠れているノーツ・ロングの芯の値を、プレイ画面と共通で使う
#include "Play/tutorialAnim.h"

#include <vector>
#include <string>
#include <map>
#include <set>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <cmath>
#include <algorithm>

class TutorialOverlay{
public:
    TutorialOverlay() = default;
    ~TutorialOverlay(){ release(); }
    TutorialOverlay(const TutorialOverlay&) = delete;
    TutorialOverlay& operator=(const TutorialOverlay&) = delete;

    // 台本を読み込み、文字を画像にしておく(以降の描画では文字を作り直さない)
    bool load(SDL_Renderer* renderer, const std::string& scriptPath){
        using json = nlohmann::json;
        release();

        // 日本語のパスでも開けるように fs::path 経由で開く。
        // ※ std::ifstream file(std::filesystem::path(x)); と1行で書くと、変数ではなく
        //    関数の宣言と解釈されてビルドが通らない。必ず変数に分ける(または { } で初期化する)
        const std::filesystem::path scriptFsPath(scriptPath);
        std::ifstream file(scriptFsPath);
        if(!file.is_open()){
            std::printf("[チュートリアル] 台本が開けない: %s\n", scriptPath.c_str());
            return false;
        }

        json j;
        try{
            file >> j;
        }
        catch(const json::exception& e){
            std::printf("[チュートリアル] 台本のJSONが読めない: %s\n", e.what());
            return false;
        }
        if(!j.is_object()){
            std::printf("[チュートリアル] 台本の一番外側が { } になっていない\n");
            return false;
        }

        warnUnknownKeys(j, {"chart", "difficulty", "font", "laneWidth", "items"}, "台本の一番外側");

        try{
            if(!j.contains("chart")){
                std::printf("[チュートリアル] 台本に \"chart\" がない\n");
                return false;
            }
            chartPath_ = j.at("chart").get<std::string>();
            chartDifficulty_ = j.value("difficulty", 0);
            laneWidth_ = std::max(1, j.value("laneWidth", 120));
        }
        catch(const json::exception& e){
            std::printf("[チュートリアル] 台本の基本項目が不正: %s\n", e.what());
            return false;
        }

        if(!std::filesystem::exists(std::filesystem::path(chartPath_))){
            std::printf("[チュートリアル] 譜面が見つからない: %s\n", chartPath_.c_str());
            return false;
        }

        const std::string fontPath = j.value("font", std::string("fonts/prac.ttf"));

        // 大きさごとにフォントを開く(読込の間だけ使い、終わったら閉じる)
        std::map<int, TTF_Font*> fonts;
        auto getFont = [&](int size) -> TTF_Font* {
            auto found = fonts.find(size);
            if(found != fonts.end()) return found->second;
            TTF_Font* font = TTF_OpenFont(fontPath.c_str(), size);
            if(!font){
                std::printf("[チュートリアル] フォントが開けない(%s, %dpt): %s\n", fontPath.c_str(), size, TTF_GetError());
            }
#if SDL_TTF_VERSION_ATLEAST(2, 20, 0)
            else{
                TTF_SetFontWrappedAlign(font, TTF_WRAPPED_ALIGN_CENTER);   // 複数行を中央そろえに
            }
#endif
            fonts[size] = font;   // 失敗(nullptr)も覚えて、同じ失敗を繰り返さない
            return font;
        };

        if(!j.contains("items") || !j.at("items").is_array()){
            std::printf("[チュートリアル] 台本に \"items\" の配列がない\n");
            closeFonts(fonts);
            return false;
        }

        int textCount = 0, noteCount = 0, skipped = 0, index = -1;
        for(const auto& item : j.at("items")){
            index++;
            try{
                Item it;
                if(parseItem(renderer, item, index, getFont, it)){
                    (it.kind == Kind::Text ? textCount : noteCount)++;
                    items_.push_back(it);
                }
                else{
                    skipped++;
                }
            }
            catch(const std::exception& e){
                std::printf("[チュートリアル] items[%d] を読めない(飛ばします): %s\n", index, e.what());
                skipped++;
            }
        }

        closeFonts(fonts);
        std::printf("[チュートリアル] 読込完了: 文字%d個 / ノーツ図%d個 / 飛ばした項目%d個\n", textCount, noteCount, skipped);
        return true;
    }

    // 作った画像をすべて解放する(renderer を壊す前に呼ぶ。デストラクタでも呼ばれる)
    void release(){
        for(Item& it : items_){
            if(it.text.t){
                SDL_DestroyTexture(it.text.t);
                it.text.t = nullptr;
            }
        }
        items_.clear();
    }

    const std::string& chartPath() const { return chartPath_; }
    int chartDifficulty() const { return chartDifficulty_; }
    size_t itemCount() const { return items_.size(); }

    // 毎フレーム、renderGamePlayScreen の SDL_RenderPresent の直前に呼ぶ
    void draw(SDL_Renderer* renderer, int32_t musicTimeMs){
        if(items_.empty()) return;

        SDL_BlendMode prevBlend = SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(renderer, &prevBlend);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        const double t = static_cast<double>(musicTimeMs);
        for(const Item& it : items_){
            const TutAnimState s = computeTutAnim(it.anim, t, it.start, it.end, it.inMs, it.outMs);
            if(!s.visible || s.alpha <= 0.004f) continue;

            if(it.kind == Kind::Text) drawText(renderer, it, s);
            else                      drawNote(renderer, it, s);
        }

        SDL_SetRenderDrawBlendMode(renderer, prevBlend);
    }

private:
    enum class Kind{ Text, Note };
    enum class NoteKind{ Normal, Drag, Lane, Long, DragLong };

    struct Tex{
        SDL_Texture* t = nullptr;
        int w = 0, h = 0;
    };

    struct Item{
        Kind kind = Kind::Text;
        double start = 0.0, end = 0.0, inMs = 400.0, outMs = 400.0;
        TutAnim anim = TutAnim::Fade;
        float x = 960.0f, y = 540.0f;

        Tex text;                 // Text: 本文 / Note: ラベル(なければ空)
        bool panel = true;        // Text: 文字の後ろの半透明の板

        NoteKind noteKind = NoteKind::Normal;
        int widthLanes = 1;
        float length = 220.0f;    // long / dragLong の長さ(px)：ヘッドの中心から終点の中心まで
        bool hidden = false;      // true = レーンが隠れているときの見た目（半透明＋縁取り）
    };

    std::vector<Item> items_;
    std::string chartPath_;
    int chartDifficulty_ = 0;
    int laneWidth_ = 120;

    static void closeFonts(std::map<int, TTF_Font*>& fonts){
        for(auto& kv : fonts){
            if(kv.second) TTF_CloseFont(kv.second);
        }
        fonts.clear();
    }

    // 知らないキー名を警告する(書き間違いが黙って無視されるのを防ぐ)
    static void warnUnknownKeys(const nlohmann::json& obj, const std::set<std::string>& allowed, const std::string& where){
        for(const auto& kv : obj.items()){
            if(allowed.find(kv.key()) == allowed.end()){
                std::printf("[チュートリアル] 警告: %s に知らないキー \"%s\" がある(書き間違い?)\n", where.c_str(), kv.key().c_str());
            }
        }
    }

    static SDL_Color parseColor(const nlohmann::json& item, SDL_Color fallback){
        if(!item.contains("color")) return fallback;
        const auto& arr = item.at("color");
        if(!arr.is_array() || arr.size() < 3){
            std::printf("[チュートリアル] 警告: \"color\" は [r,g,b] の形で書く\n");
            return fallback;
        }
        auto ch = [&](size_t i, int def) -> Uint8 {
            if(i >= arr.size()) return static_cast<Uint8>(def);
            const int v = arr[i].get<int>();
            return static_cast<Uint8>(std::max(0, std::min(255, v)));
        };
        return SDL_Color{ ch(0, 255), ch(1, 255), ch(2, 255), ch(3, 255) };
    }

    // 文字を画像にする。wrap(px)を超えると折り返し、"\n" でも改行する
    static Tex renderText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, SDL_Color color, int wrap){
        Tex out;
        if(!font || text.empty()) return out;

        SDL_Surface* surf = TTF_RenderUTF8_Blended_Wrapped(font, text.c_str(), color, static_cast<Uint32>(std::max(0, wrap)));
        if(!surf) return out;

        out.t = SDL_CreateTextureFromSurface(renderer, surf);
        out.w = surf->w;
        out.h = surf->h;
        SDL_FreeSurface(surf);

        if(out.t) SDL_SetTextureBlendMode(out.t, SDL_BLENDMODE_BLEND);
        else      out = Tex{};
        return out;
    }

    // items の1項目を読む。使えない項目なら false(警告を出して飛ばす)
    template<class FontGetter>
    bool parseItem(SDL_Renderer* renderer, const nlohmann::json& item, int index, FontGetter& getFont, Item& it){
        const std::string where = "items[" + std::to_string(index) + "]";

        if(!item.is_object()){
            std::printf("[チュートリアル] %s が { } になっていない(飛ばします)\n", where.c_str());
            return false;
        }

        const std::string type = item.value("type", std::string(""));
        if(type == "text")      it.kind = Kind::Text;
        else if(type == "note") it.kind = Kind::Note;
        else{
            std::printf("[チュートリアル] %s の type \"%s\" が不明(text か note)。飛ばします\n", where.c_str(), type.c_str());
            return false;
        }

        if(it.kind == Kind::Text){
            warnUnknownKeys(item, {"type", "start", "end", "text", "x", "y", "size", "color", "wrap", "anim", "in", "out", "panel"}, where);
        }
        else{
            warnUnknownKeys(item, {"type", "start", "end", "noteType", "x", "y", "width", "length", "hidden", "label", "size", "color", "wrap", "anim", "in", "out"}, where);
        }

        if(!item.contains("start") || !item.contains("end")){
            std::printf("[チュートリアル] %s に start / end がない(飛ばします)\n", where.c_str());
            return false;
        }
        it.start = item.at("start").get<double>();
        it.end   = item.at("end").get<double>();
        if(!(it.end > it.start)){
            std::printf("[チュートリアル] %s は end が start より後になっていない(飛ばします)\n", where.c_str());
            return false;
        }

        it.inMs  = std::max(0.0, item.value("in", 400.0));
        it.outMs = std::max(0.0, item.value("out", 400.0));
        it.x = static_cast<float>(item.value("x", 960.0));
        it.y = static_cast<float>(item.value("y", 540.0));

        const std::string animName = item.value("anim", std::string("fade"));
        if(!parseTutAnim(animName, it.anim)){
            std::printf("[チュートリアル] 警告: %s の anim \"%s\" が不明。fade にします(none/fade/pop/slideUp/slideDown/wipe)\n", where.c_str(), animName.c_str());
            it.anim = TutAnim::Fade;
        }

        const SDL_Color white{255, 255, 255, 255};

        if(it.kind == Kind::Text){
            const std::string text = item.value("text", std::string(""));
            if(text.empty()){
                std::printf("[チュートリアル] %s の text が空(飛ばします)\n", where.c_str());
                return false;
            }
            it.panel = item.value("panel", true);
            const int size = std::max(8, std::min(200, item.value("size", 36)));
            const int wrap = item.value("wrap", 1000);
            it.text = renderText(renderer, getFont(size), text, parseColor(item, white), wrap);
            if(!it.text.t){
                std::printf("[チュートリアル] %s の文字を画像にできなかった(飛ばします)\n", where.c_str());
                return false;
            }
        }
        else{
            const std::string nk = item.value("noteType", std::string("normal"));
            if(nk == "normal")      it.noteKind = NoteKind::Normal;
            else if(nk == "drag")   it.noteKind = NoteKind::Drag;
            else if(nk == "lane")   it.noteKind = NoteKind::Lane;
            else if(nk == "long")   it.noteKind = NoteKind::Long;
            else if(nk == "dragLong") it.noteKind = NoteKind::DragLong;
            else{
                std::printf("[チュートリアル] 警告: %s の noteType \"%s\" が不明。normal にします(normal/drag/lane/long/dragLong)\n", where.c_str(), nk.c_str());
                it.noteKind = NoteKind::Normal;
            }
            it.hidden = item.value("hidden", false);
            if(it.hidden && it.noteKind == NoteKind::Lane){
                // ゲームでも LANE ノーツは隠れている見た目にならない（レーンを開け閉めするノーツなので、いつもはっきり見える）
                std::printf("[チュートリアル] 警告: %s の lane ノーツに hidden は使えません（ふつうの見た目で出します）\n", where.c_str());
                it.hidden = false;
            }
            it.widthLanes = std::max(1, std::min(6, item.value("width", 1)));
            it.length = static_cast<float>(std::max(1.0, item.value("length", 220.0)));

            const std::string label = item.value("label", std::string(""));
            if(!label.empty()){
                const int size = std::max(8, std::min(200, item.value("size", 30)));
                const int wrap = item.value("wrap", 600);
                it.text = renderText(renderer, getFont(size), label, parseColor(item, white), wrap);
            }
        }
        return true;
    }

    // 画像を、中心(cx,cy)・拡大率・透明度・見える割合を指定して貼る
    static void blit(SDL_Renderer* renderer, const Tex& tex, float cx, float cy, float scale, float alpha, float reveal){
        if(!tex.t) return;
        const float sw = tex.w * scale, sh = tex.h * scale;

        SDL_Rect src = { 0, 0, static_cast<int>(tex.w * reveal), tex.h };
        if(src.w <= 0) return;
        SDL_Rect dst = {
            static_cast<int>(std::lround(cx - sw * 0.5f)),
            static_cast<int>(std::lround(cy - sh * 0.5f)),
            static_cast<int>(std::lround(sw * reveal)),
            static_cast<int>(std::lround(sh))
        };
        SDL_SetTextureAlphaMod(tex.t, static_cast<Uint8>(std::lround(255.0f * alpha)));
        SDL_RenderCopy(renderer, tex.t, &src, &dst);
    }

    void drawText(SDL_Renderer* renderer, const Item& it, const TutAnimState& s) const {
        const float cx = it.x + s.dx, cy = it.y + s.dy;

        if(it.panel){
            const float sw = it.text.w * s.scale, sh = it.text.h * s.scale;
            const float padX = 28.0f * s.scale, padY = 18.0f * s.scale;
            RoundedRectBatch panel;
            panel.add(cx - sw * 0.5f - padX, cy - sh * 0.5f - padY, sw + padX * 2.0f, sh + padY * 2.0f,
                      16.0f * s.scale, SDL_Color{0, 0, 0, static_cast<Uint8>(std::lround(160.0f * s.alpha))});
            panel.flush(renderer);
        }
        blit(renderer, it.text, cx, cy, s.scale, s.alpha, s.reveal);
    }

    // ノーツの図：プレイ画面（renderGame.h）と同じ形・色で描く
    //   ・幅      ：レーン幅 × width から、左右 NOTE_SIDE_MARGIN ずつ空ける
    //   ・ヘッド  ：高さ NOTE_HEAD_H、角の丸み NOTE_RADIUS。色は 通常=白 / ドラッグ=黄 / レーン=赤
    //   ・ロング  ：ヘッドと同じ色の帯 ＋ 真ん中の芯（ノーツ色を 0.6 倍に暗くした色）。
    //               帯はヘッドの中心から length だけ上の「終点」まで。芯は帯の端より内側で止める（LONG_CORE_*）
    //   ・hidden  ：レーンが隠れているときの見た目。中身を半透明にして、同じ色の縁取りを付ける（HIDDEN_NOTE_*）
    //   (x, y) はヘッドの中心。図全体を s.scale 倍・s.alpha の濃さで描く
    void drawNote(SDL_Renderer* renderer, const Item& it, const TutAnimState& s) const {
        const float k  = s.scale;
        const float cx = it.x + s.dx, cy = it.y + s.dy;
        const float laneSpan = static_cast<float>(laneWidth_ * it.widthLanes) * k;
        const float w  = std::max(4.0f * k, laneSpan - NOTE_SIDE_MARGIN * 2.0f * k);
        const float h  = NOTE_HEAD_H * k;
        const float r  = NOTE_RADIUS * k;
        const float left = cx - w * 0.5f;
        const float headTop = cy - h * 0.5f;

        // 色に、アニメーションの濃さ（s.alpha）を掛ける
        auto fade = [&](SDL_Color c) -> SDL_Color {
            c.a = static_cast<Uint8>(std::lround(c.a * s.alpha));
            return c;
        };

        const bool isLong = (it.noteKind == NoteKind::Long || it.noteKind == NoteKind::DragLong);

        // ① 色：ドラッグロングはドラッグと同じ黄色、ふつうのロングは通常ノーツと同じ白
        SDL_Color col{255, 255, 255, 255};
        if(it.noteKind == NoteKind::Drag || it.noteKind == NoteKind::DragLong) col = SDL_Color{250, 250, 150, 255};
        else if(it.noteKind == NoteKind::Lane)                                  col = SDL_Color{255, 50, 50, 255};

        const SDL_Color outlineCol{col.r, col.g, col.b, HIDDEN_NOTE_OUTLINE_ALPHA};
        if(it.hidden) col.a = HIDDEN_NOTE_ALPHA;

        RoundedRectBatch batch;   // 足した順に重なる：帯 → 芯 → ヘッド → 縁取り

        // ② ロング：帯と芯（ヘッドより奥）
        if(isLong){
            const float len = it.length * k;
            const float top = cy - len - h * 0.5f;   // 終点の丸い端も、ヘッドと同じ太さ
            const float bottom = cy;                 // 帯はヘッドの中心まで（下半分はヘッドで隠れる）

            batch.add(left, top, w, bottom - top, r, fade(col));
            if(it.hidden){
                // 帯の縁取りはヘッドの上の端までにする（ヘッドの真ん中を線が横切らないように）
                const float outlineH = std::max(r * 2.0f, headTop - top);
                batch.addOutline(left, top, w, outlineH, r, HIDDEN_NOTE_OUTLINE_W * k, fade(outlineCol));
            }

            // 芯：帯の横幅の LONG_CORE_WIDTH_RATIO、左右中央。上は帯の端から内側、下はヘッドから離して止める
            const float innerW = w * LONG_CORE_WIDTH_RATIO;
            const float innerX = cx - innerW * 0.5f;
            const float innerTop = top + LONG_CORE_TAIL_MARGIN * k;
            const float innerBottom = headTop - LONG_CORE_HEAD_GAP * k;
            if(innerBottom - innerTop > 1.0f){
                const SDL_Color innerCol = it.hidden
                    ? SDL_Color{col.r, col.g, col.b, HIDDEN_NOTE_INNER_ALPHA}            // 隠れている：帯より薄く
                    : SDL_Color{static_cast<Uint8>(col.r * 0.6), static_cast<Uint8>(col.g * 0.6),
                                static_cast<Uint8>(col.b * 0.6), 200};                 // 叩く前の芯
                batch.add(innerX, innerTop, innerW, innerBottom - innerTop, std::min(r, innerW * 0.5f), fade(innerCol));
            }
        }

        // ③ ヘッド（＋隠れているときの縁取り）
        batch.add(left, headTop, w, h, r, fade(col));
        if(it.hidden){
            batch.addOutline(left, headTop, w, h, r, HIDDEN_NOTE_OUTLINE_W * k, fade(outlineCol));
        }
        batch.flush(renderer);

        if(it.text.t){                                         // ラベルはノーツの下に中央そろえ
            const float ly = cy + h * 0.5f + 14.0f * k + it.text.h * k * 0.5f;
            blit(renderer, it.text, cx, ly, k, s.alpha, 1.0f);
        }
    }
};