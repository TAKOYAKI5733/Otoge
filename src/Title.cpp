// =====================================================================
//  Title.cpp — タイトル画面
//
//  ・ロゴは画像を使わず、SDL の図形（SDL_RenderGeometry）だけで描く
//  ・ロゴはもともと「45°傾けた黒い正方形」を、2000×2000 の正方形で切り取った形をしている。
//    そこで、ロゴの部品は「45°回した座標 (u, v)」で持っておき、描くときに 45°戻す。
//    回した座標では、黒い板・白い溝・NOTES の文字が全部「ただの長方形」になるので、数字で持ちやすい。
//  ・ロゴは 2 倍の大きさのテクスチャに描いてから縮小して貼る → 斜めの線のギザギザが目立たない
//    （テクスチャの外は自動で切り取られるので、元画像の四角い切り取りもそのまま再現できる）
// =====================================================================
#include "GameCommon.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include "Play/visualizer.h"   // プレイ画面と同じビジュアライザー（画面の左右の端で横線が伸び縮みする）

namespace titlelogo {

// ----- 元画像の大きさ（座標はすべてこの単位） -----
constexpr float CANVAS = 2000.0f;          // 元画像の1辺
constexpr float CENTER = CANVAS * 0.5f;    // 元画像の中心

// ----- 45°回した座標 (u, v) での部品。原点は元画像の中心 -----
constexpr float SQ = 849.0f;               // 黒い正方形の半分の大きさ

// 黒い板を左右に分ける細い白線（少しだけ傾いている）
constexpr float SEAM_U_TOP    = 6.6f;      // v = -SQ での線の中心
constexpr float SEAM_U_BOTTOM = 17.4f;     // v = +SQ での線の中心
constexpr float SEAM_W        = 4.5f;      // 線の太さ

struct Box{ float u, v, w, h; };

// 白い溝（ノーツの形）。外側の端は黒い正方形の縁にそろえる
constexpr Box   BAR_RIGHT  = { 45.5f, -407.5f, SQ - 45.5f, 244.0f};   // 右上の溝（左端が丸い）
constexpr Box   BAR_LEFT   = {-SQ,     170.5f, SQ - 25.5f, 244.0f};   // 左下の溝（右端が丸い）
constexpr float BAR_RADIUS = 24.0f;

// 「NOTES」のドット文字（白い長方形の集まり）
constexpr Box TEXT[] = {
    {-414, 488, 61, 93}, {-324, 488, 61, 33}, {-214, 488, 88, 33}, {-64, 488, 154, 33},
    {104, 488, 181, 33}, {332, 488, 91, 33},  {-296, 522, 33, 150}, {-248, 522, 33, 209},
    {-126, 522, 33, 209}, {-64, 522, 32, 60}, {-4, 522, 34, 209},  {58, 522, 32, 60},
    {134, 522, 34, 88},  {254, 522, 31, 30},  {300, 522, 32, 60},  {390, 522, 32, 30},
    {422, 552, 32, 30},  {-414, 582, 33, 149}, {-352, 582, 29, 90}, {222, 582, 32, 28},
    {332, 582, 32, 28},  {134, 610, 121, 33}, {364, 610, 59, 33},  {134, 642, 34, 88},
    {222, 642, 32, 29},  {422, 642, 32, 88},  {-324, 672, 61, 92}, {300, 672, 32, 29},
    {254, 700, 31, 30},  {332, 700, 32, 30},  {-414, 730, 61, 33}, {-214, 730, 88, 33},
    {-32, 730, 90, 33},  {104, 730, 181, 33}, {332, 730, 91, 33},
};
constexpr int TEXT_COUNT = static_cast<int>(sizeof(TEXT) / sizeof(TEXT[0]));

// 文字ごとの区切り（u の位置）。N | O | T | E | S
constexpr float LETTER_SPLIT[4] = {-220.0f, -80.0f, 97.0f, 292.0f};
inline int letterOf(const Box& b){
    const float cu = b.u + b.w * 0.5f;
    int k = 0;
    while(k < 4 && cu > LETTER_SPLIT[k]) k++;
    return k;   // 0〜4
}

// 右下の「.」。これだけは回っていない（元画像の座標そのまま）
constexpr float DOT_X = 1694.0f, DOT_Y = 1693.0f, DOT_SIZE = 213.0f, DOT_RADIUS = 14.0f;

// ----- 色（ロゴは元のまま：黒と白） -----
constexpr SDL_Color BLACK = {0, 0, 0, 255};
constexpr SDL_Color WHITE = {255, 255, 255, 255};

// ---------------------------------------------------------------------
//  図形をためて、最後にまとめて1回で描く
// ---------------------------------------------------------------------
class Painter{
public:
    explicit Painter(float pxPerUnit) : scale_(pxPerUnit) {}

    // 回した座標 (u, v) → テクスチャ上の位置
    SDL_FPoint fromUV(float u, float v) const {
        constexpr float k = 0.70710678f;   // cos45° = sin45°
        const float x = CENTER + (u * k - v * k);
        const float y = CENTER + (u * k + v * k);
        return SDL_FPoint{x * scale_, y * scale_};
    }
    // 元画像の座標 → テクスチャ上の位置
    SDL_FPoint fromCanvas(float x, float y) const {
        return SDL_FPoint{x * scale_, y * scale_};
    }

    // 凸多角形を塗る（1点目から扇形に三角形を作る）
    void polygon(const std::vector<SDL_FPoint>& pts, SDL_Color c){
        if(pts.size() < 3 || c.a == 0) return;
        const int base = static_cast<int>(verts_.size());
        for(const auto& p : pts) verts_.push_back(SDL_Vertex{p, c, SDL_FPoint{0.0f, 0.0f}});
        for(int i = 1; i + 1 < static_cast<int>(pts.size()); i++){
            idx_.push_back(base);
            idx_.push_back(base + i);
            idx_.push_back(base + i + 1);
        }
    }

    // 回した座標での角丸の長方形。角ごとに丸みを変えられる（左上・右上・右下・左下）
    void roundedBoxUV(float u0, float v0, float u1, float v1, float rTL, float rTR, float rBR, float rBL, SDL_Color c){
        if(u1 - u0 <= 0.5f || v1 - v0 <= 0.5f) return;
        const float maxR = std::min(u1 - u0, v1 - v0) * 0.5f;
        rTL = std::min(rTL, maxR); rTR = std::min(rTR, maxR);
        rBR = std::min(rBR, maxR); rBL = std::min(rBL, maxR);

        std::vector<SDL_FPoint> pts;
        auto arc = [&](float cu, float cv, float r, float a0){
            if(r < 0.5f){ pts.push_back(fromUV(cu, cv)); return; }
            const int SEG = 6;
            for(int s = 0; s <= SEG; s++){
                const float a = a0 + 1.5707963f * static_cast<float>(s) / SEG;
                pts.push_back(fromUV(cu + std::cos(a) * r, cv + std::sin(a) * r));
            }
        };
        const float PI = 3.14159265f;
        arc(u0 + rTL, v0 + rTL, rTL, PI);          // 左上
        arc(u1 - rTR, v0 + rTR, rTR, PI * 1.5f);   // 右上
        arc(u1 - rBR, v1 - rBR, rBR, 0.0f);        // 右下
        arc(u0 + rBL, v1 - rBL, rBL, PI * 0.5f);   // 左下
        polygon(pts, c);
    }

    // 元画像の座標での角丸の正方形（「.」用）。中心と大きさで指定
    void roundedSquareCanvas(float cx, float cy, float size, float r, SDL_Color c){
        if(size <= 0.5f) return;
        r = std::min(r, size * 0.5f);
        const float h = size * 0.5f;
        std::vector<SDL_FPoint> pts;
        const float PI = 3.14159265f;
        const float cxs[4] = {cx + h - r, cx + h - r, cx - h + r, cx - h + r};
        const float cys[4] = {cy - h + r, cy + h - r, cy + h - r, cy - h + r};
        const float a0[4]  = {-PI * 0.5f, 0.0f, PI * 0.5f, PI};
        for(int k = 0; k < 4; k++){
            for(int s = 0; s <= 6; s++){
                const float a = a0[k] + 1.5707963f * s / 6.0f;
                pts.push_back(fromCanvas(cxs[k] + std::cos(a) * r, cys[k] + std::sin(a) * r));
            }
        }
        polygon(pts, c);
    }

    void flush(SDL_Renderer* renderer){
        if(!idx_.empty()){
            SDL_RenderGeometry(renderer, nullptr, verts_.data(), static_cast<int>(verts_.size()),
                               idx_.data(), static_cast<int>(idx_.size()));
        }
        verts_.clear();
        idx_.clear();
    }

private:
    float scale_;
    std::vector<SDL_Vertex> verts_;
    std::vector<int> idx_;
};

} // namespace titlelogo

namespace {

// ----- 画面の配置 -----
//   アイコン（S のマーク）と、あとで入れる「simple.notes」の文字ロゴを横に並べる。
//   ┌────────┐
//   │  アイコン  │   simple.notes（WORDMARK_RECT）
//   └────────┘
//          PRESS ENTER
//   アイコンは最初は画面の真ん中で組み上がり、文字ロゴが出るタイミングで左へ寄る。
constexpr int LOGO_SIZE     = 560;                    // アイコンの大きさ(px)
constexpr int LOGO_TEX_SIZE = LOGO_SIZE * 2;          // アイコンを描くテクスチャ（2倍で描いて縮小 → なめらか）

constexpr int LOCKUP_GAP    = 70;                     // アイコンと文字ロゴの間
constexpr int WORDMARK_W    = 760;                    // 文字ロゴの場所の幅
constexpr int WORDMARK_H    = 220;                    // 文字ロゴの場所の高さ
constexpr int LOCKUP_W      = LOGO_SIZE + LOCKUP_GAP + WORDMARK_W;   // 並べたときの全体の幅
constexpr int LOGO_CENTER_Y = 470;                    // アイコンと文字ロゴの縦の中心

// アイコンの位置：組み上がるとき（真ん中）と、文字ロゴと並んだとき（左寄り）
constexpr int LOGO_X_CENTER = (SCREEN_W - LOGO_SIZE) / 2;
constexpr int LOGO_X_LOCKUP = (SCREEN_W - LOCKUP_W) / 2;
constexpr int LOGO_Y        = LOGO_CENTER_Y - LOGO_SIZE / 2;

// 「simple.notes」を置く場所（あとで drawWordmark() の中身を作る）
constexpr SDL_Rect WORDMARK_RECT = {LOGO_X_LOCKUP + LOGO_SIZE + LOCKUP_GAP, LOGO_CENTER_Y - WORDMARK_H / 2, WORDMARK_W, WORDMARK_H};

// 作業用：文字ロゴの場所を点線の枠で見せる（完成したら false にする）
constexpr bool SHOW_LAYOUT_GUIDE = false;

// ----- 出てくる順番と時間(ms) -----
constexpr double T_PIECE_L  = 100.0;    // 左の黒い板
constexpr double T_PIECE_R  = 260.0;    // 右の黒い板
constexpr double D_PIECE    = 700.0;
constexpr double T_SEAM     = 900.0;    // 板の間の白線
constexpr double D_SEAM     = 250.0;
constexpr double T_BAR_R    = 950.0;    // 右上の溝
constexpr double T_BAR_L    = 1100.0;   // 左下の溝
constexpr double D_BAR      = 500.0;
constexpr double T_TEXT     = 1500.0;   // NOTES（1文字ずつ）
constexpr double STEP_TEXT  = 90.0;
constexpr double D_TEXT     = 320.0;
constexpr double T_DOT      = 2050.0;   // 「.」
constexpr double D_DOT      = 420.0;
constexpr double D_RING     = 650.0;    // 「.」の周りに広がる枠
constexpr double T_SHIFT    = 2450.0;   // アイコンが左へ寄る（同時に文字ロゴが出る）
constexpr double D_SHIFT    = 650.0;
constexpr double T_WORDMARK = 2850.0;   // 文字ロゴが出始める（アイコンが寄り終わる頃。重ならないように）
constexpr double D_WORDMARK = 600.0;    // （作業用の点線の枠が出る時間）
constexpr double STEP_WORD  = 60.0;     // 文字ロゴ：次の文字が出始めるまでの間隔
constexpr double D_WORD_DOT = 260.0;    // 文字ロゴ：ドット1つが浮き出るのにかかる時間
constexpr double T_INTRO_END = 3800.0;  // ここから「PRESS ENTER」
constexpr double D_EXIT     = 550.0;    // Enter を押してから次の画面へ

constexpr double DOT_PULSE_BPM = 100.0;                    // 待機中、「.」が脈打つ速さ（BPM）
constexpr double DOT_PULSE_MS  = 60000.0 / DOT_PULSE_BPM;  // 1拍の長さ（100BPM なら 600ms）

// ----- タイトルの曲 -----
//   ロゴが完成したとき（T_INTRO_END）にフェードインで流し始める
constexpr const char* TITLE_BGM_PATH = "sounds/Cyber_Society_2.mp3";   // タイトルの曲
constexpr int    BGM_FADE_IN_MS  = 2000;   // フェードインにかける時間

// ----- ビジュアライザー（プレイ画面と同じもの） -----
//   背景が明るい灰色なので、プレイ画面の白ではなく、ロゴと同じ黒っぽい色で薄く出す
constexpr SDL_Color VIS_COLOR = {20, 20, 28, 70};

inline double clamp01(double x){ return std::clamp(x, 0.0, 1.0); }
inline double progress(double t, double start, double dur){ return clamp01((t - start) / dur); }
inline double easeOutCubicT(double t){ return 1.0 - std::pow(1.0 - t, 3.0); }
inline double easeInCubicT(double t){ return t * t * t; }
inline double easeInOutCubicT(double t){ return (t < 0.5) ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) * 0.5; }
inline double easeOutBackT(double t){
    const double c1 = 1.70158, c3 = c1 + 1.0;
    return 1.0 + c3 * std::pow(t - 1.0, 3.0) + c1 * std::pow(t - 1.0, 2.0);
}

// 背景：左上が明るく右下が暗い灰色のグラデーション＋ゆっくり流れる斜めの帯
void drawBackground(SDL_Renderer* renderer, double timeMs){
    const SDL_Color tl = {214, 214, 218, 255};
    const SDL_Color tr = {172, 172, 180, 255};
    const SDL_Color bl = {160, 160, 168, 255};
    const SDL_Color br = {112, 112, 122, 255};
    const float W = static_cast<float>(SCREEN_W), H = static_cast<float>(SCREEN_H);

    // 4隅に色を付けた四角形を1枚描くと、間の色は GPU がなめらかにつないでくれる
    SDL_Vertex v[4] = {
        {{0.0f, 0.0f}, tl, {0, 0}},
        {{W,    0.0f}, tr, {0, 0}},
        {{W,    H   }, br, {0, 0}},
        {{0.0f, H   }, bl, {0, 0}},
    };
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, nullptr, v, 4, idx, 6);

    // 斜めの帯（ロゴと同じ 45°）。少し明るい白を薄く重ねる
    const float spacing = 180.0f, bandW = 70.0f;
    const float shift = static_cast<float>(std::fmod(timeMs * 0.02, spacing));   // 1秒に 20px 流れる
    const SDL_Color c = {255, 255, 255, 16};
    std::vector<SDL_Vertex> vs;
    std::vector<int> is;
    for(float x = -H - spacing + shift; x < W + spacing; x += spacing){
        const int b = static_cast<int>(vs.size());
        vs.push_back({{x,             0.0f}, c, {0, 0}});
        vs.push_back({{x + bandW,     0.0f}, c, {0, 0}});
        vs.push_back({{x + bandW + H, H   }, c, {0, 0}});
        vs.push_back({{x + H,         H   }, c, {0, 0}});
        is.insert(is.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    }
    SDL_RenderGeometry(renderer, nullptr, vs.data(), static_cast<int>(vs.size()), is.data(), static_cast<int>(is.size()));
}

// =====================================================================
//  「simple.notes」の文字ロゴ（ドット文字）
//
//  ・1文字を「11行 × 幅w列」のマス目で持つ。'#' がドット、'.' が空き
//      行 0〜2 : 上に伸びる部分（l, t, i の点）
//      行 3〜8 : 小文字の本体（行 8 が文字の底）
//      行 9〜10: 下に伸びる部分（p）
//  ・ドットは少しすき間を空けた正方形で描くので、「ドットで書いた文字」に見える
// =====================================================================
struct Glyph{
    char ch;
    int w;                    // 列の数
    const char* rows[11];     // 上から順に 11 行
};

constexpr Glyph WORD_GLYPHS[] = {
    {'s', 5, {".....", ".....", ".....", ".####", "#....", "#....", ".###.", "....#", "####.", ".....", "....."}},
    {'i', 3, {".#.",   "...",   "...",   "##.",   ".#.",   ".#.",   ".#.",   ".#.",   "###",   "...",   "..."}},
    {'m', 5, {".....", ".....", ".....", "##.#.", "#.#.#", "#.#.#", "#.#.#", "#.#.#", "#.#.#", ".....", "....."}},
    {'p', 5, {".....", ".....", ".....", "####.", "#...#", "#...#", "#...#", "#...#", "####.", "#....", "#...."}},
    {'l', 3, {"##.",   ".#.",   ".#.",   ".#.",   ".#.",   ".#.",   ".#.",   ".#.",   "###",   "...",   "..."}},
    {'e', 5, {".....", ".....", ".....", ".###.", "#...#", "#####", "#....", "#....", ".####", ".....", "....."}},
    {'.', 2, {"..",    "..",    "..",    "..",    "..",    "..",    "..",    "##",    "##",    "..",    ".."}},
    {'n', 5, {".....", ".....", ".....", "####.", "#...#", "#...#", "#...#", "#...#", "#...#", ".....", "....."}},
    {'o', 5, {".....", ".....", ".....", ".###.", "#...#", "#...#", "#...#", "#...#", ".###.", ".....", "....."}},
    {'t', 4, {"....",  ".#..",  ".#..",  "####",  ".#..",  ".#..",  ".#..",  ".#..",  "..##",  "....",  "...."}},
};

inline const Glyph* findGlyph(char c){
    for(const Glyph& g : WORD_GLYPHS) if(g.ch == c) return &g;
    return nullptr;
}

constexpr const char* WORD_TEXT  = "simple.notes";
constexpr int         WORD_ROWS  = 11;
constexpr int         WORD_SPACE = 1;          // 文字と文字の間（マス）
constexpr float       WORD_DOT_GAP = 2.0f;     // ドットとドットのすき間(px)
constexpr float       WORD_RISE    = 16.0f;    // 浮き出るときに下から上がってくる距離(px)
constexpr SDL_Color   WORD_COLOR   = {0, 0, 0, 255};   // ロゴと同じ黒

// 文字列全体の幅（マスの数）
inline int wordCells(){
    int cells = 0;
    int n = 0;
    for(const char* c = WORD_TEXT; *c; c++){
        const Glyph* g = findGlyph(*c);
        if(!g) continue;
        if(n++ > 0) cells += WORD_SPACE;
        cells += g->w;
    }
    return cells;
}

// rect  : 置く場所（WORDMARK_RECT）。この中に収まる一番大きいマスの大きさで描く
// t     : タイトル画面が始まってからの時間(ms)
// exitP : 次の画面へ移るときの進み具合 0〜1
// pulse : 待機中の拍（0〜1）。「.」だけ、アイコンの「.」と一緒に脈打つ
void drawWordmark(SDL_Renderer* renderer, const SDL_Rect& rect, double t, double exitP, double pulse){
    // マスの大きさ：横・縦どちらにも収まるように小さいほうに合わせる
    const int cellsW = wordCells();
    const float cell = std::floor(std::min(static_cast<float>(rect.w) / cellsW, static_cast<float>(rect.h) / WORD_ROWS));
    const float totalW = cell * cellsW;
    const float totalH = cell * WORD_ROWS;
    const float x0 = rect.x + (rect.w - totalW) * 0.5f;
    const float y0 = rect.y + (rect.h - totalH) * 0.5f;
    const float dot = cell - WORD_DOT_GAP;

    // 影 → 本体の順に 2 回描く（影はアイコンと同じく右下にずらした薄い黒）
    for(int pass = 0; pass < 2; pass++){
        const bool shadow = (pass == 0);
        const float sx = shadow ? 4.0f : 0.0f;
        const float sy = shadow ? 5.0f : 0.0f;

        float cx = x0;
        int k = 0;   // 何文字目か（出てくる順番）
        for(const char* c = WORD_TEXT; *c; c++){
            const Glyph* g = findGlyph(*c);
            if(!g) continue;
            const double charStart = T_WORDMARK + STEP_WORD * k;
            const bool isDot = (g->ch == '.');

            for(int r = 0; r < WORD_ROWS; r++){
                for(int col = 0; col < g->w; col++){
                    if(g->rows[r][col] != '#') continue;

                    // 同じ文字の中でも、ドットごとに少しずつ出る時刻をずらす（ぱらぱらと浮き出る）
                    const int scatter = (r * 7 + col * 13 + k * 5) % 5;
                    const double dp = progress(t, charStart + scatter * 25.0, D_WORD_DOT);
                    if(dp <= 0.0) continue;
                    const double e = easeOutCubicT(dp);

                    // 「.」は待機中に拍に合わせて少し大きくなる
                    float size = dot;
                    if(isDot) size *= static_cast<float>(1.0 + 0.25 * pulse);

                    const float px = cx + col * cell + (dot - size) * 0.5f + sx;
                    const float py = y0 + r * cell + (dot - size) * 0.5f + sy
                                   + static_cast<float>(WORD_RISE * (1.0 - e))     // 下から浮き上がる
                                   - static_cast<float>(WORD_RISE * exitP);        // 終わるときは上へ抜ける

                    double a = e * (1.0 - exitP);
                    if(shadow) a *= 60.0 / 255.0;
                    SDL_SetRenderDrawColor(renderer, WORD_COLOR.r, WORD_COLOR.g, WORD_COLOR.b,
                                           static_cast<Uint8>(255.0 * clamp01(a)));
                    SDL_FRect d = {px, py, size, size};
                    SDL_RenderFillRectF(renderer, &d);
                }
            }
            cx += (g->w + WORD_SPACE) * cell;
            k++;
        }
    }
}

// 点線の枠（作業用のガイド）
void drawDashedRect(SDL_Renderer* renderer, const SDL_Rect& r, SDL_Color c){
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    const int dash = 12, gap = 8;
    for(int x = r.x; x < r.x + r.w; x += dash + gap){
        const int w = std::min(dash, r.x + r.w - x);
        SDL_Rect top = {x, r.y, w, 2}, bottom = {x, r.y + r.h - 2, w, 2};
        SDL_RenderFillRect(renderer, &top);
        SDL_RenderFillRect(renderer, &bottom);
    }
    for(int y = r.y; y < r.y + r.h; y += dash + gap){
        const int h = std::min(dash, r.y + r.h - y);
        SDL_Rect left = {r.x, y, 2, h}, right = {r.x + r.w - 2, y, 2, h};
        SDL_RenderFillRect(renderer, &left);
        SDL_RenderFillRect(renderer, &right);
    }
}

// 太さのある四角い枠（画面の座標）
void drawFrameRect(SDL_Renderer* renderer, float x, float y, float w, float h, float t, SDL_Color c){
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_FRect r[4] = {
        {x, y, w, t}, {x, y + h - t, w, t},
        {x, y + t, t, h - t * 2.0f}, {x + w - t, y + t, t, h - t * 2.0f},
    };
    SDL_RenderFillRectsF(renderer, r, 4);
}

// ロゴを1枚描く
//   t       : 出始めてからの時間(ms)
//   exitP   : 次の画面へ移るときの進み具合（0 = まだ, 1 = 終わり）
//   pulse   : 待機中の「.」の脈（0〜1）
void drawLogo(SDL_Renderer* renderer, double t, double exitP, double pulse){
    using namespace titlelogo;
    Painter p(static_cast<float>(LOGO_TEX_SIZE) / CANVAS);

    const double exitE = easeInCubicT(exitP);

    // ----- 黒い板（左右2枚）：斜めの外側から滑り込む。終わるときは逆に開いていく -----
    const float pieceL = static_cast<float>(-1800.0 * (1.0 - easeOutCubicT(progress(t, T_PIECE_L, D_PIECE))) - 1800.0 * exitE);
    const float pieceR = static_cast<float>( 1800.0 * (1.0 - easeOutCubicT(progress(t, T_PIECE_R, D_PIECE))) + 1800.0 * exitE);

    // 線の位置（v によって少し動く）
    auto seamU = [](float v){ return SEAM_U_TOP + (SEAM_U_BOTTOM - SEAM_U_TOP) * (v + SQ) / (2.0f * SQ); };
    const float hw = SEAM_W * 0.5f;

    // 左の板：u = -SQ 〜 線の左側
    p.polygon({p.fromUV(-SQ, -SQ + pieceL), p.fromUV(seamU(-SQ) - hw, -SQ + pieceL),
               p.fromUV(seamU(SQ) - hw, SQ + pieceL), p.fromUV(-SQ, SQ + pieceL)}, BLACK);
    // 右の板：線の右側 〜 u = +SQ
    p.polygon({p.fromUV(seamU(-SQ) + hw, -SQ + pieceR), p.fromUV(SQ, -SQ + pieceR),
               p.fromUV(SQ, SQ + pieceR), p.fromUV(seamU(SQ) + hw, SQ + pieceR)}, BLACK);

    // ----- 板の間の白線：板が合わさった後に浮かび上がる -----
    {
        const double a = easeOutCubicT(progress(t, T_SEAM, D_SEAM)) * (1.0 - exitP);
        SDL_Color c = WHITE;
        c.a = static_cast<Uint8>(255 * a);
        p.polygon({p.fromUV(seamU(-SQ) - hw, -SQ), p.fromUV(seamU(-SQ) + hw, -SQ),
                   p.fromUV(seamU(SQ) + hw, SQ),   p.fromUV(seamU(SQ) - hw, SQ)}, c);
    }

    // ----- 白い溝：ノーツのように、正方形の縁から押し込まれる -----
    //   ずらした後で u を正方形の内側（-SQ〜SQ）に切り詰めるので、外にははみ出さない
    {
        const float offR = static_cast<float>(900.0 * (1.0 - easeOutBackT(progress(t, T_BAR_R, D_BAR))));
        const float u0 = std::max(-SQ, BAR_RIGHT.u + offR);
        const float u1 = std::min( SQ, BAR_RIGHT.u + BAR_RIGHT.w + offR);
        const float v  = BAR_RIGHT.v + pieceR;   // 右の板と一緒に動く
        if(progress(t, T_BAR_R, D_BAR) > 0.0)
            p.roundedBoxUV(u0, v, u1, v + BAR_RIGHT.h, BAR_RADIUS, 0.0f, 0.0f, BAR_RADIUS, WHITE);
    }
    {
        const float offL = static_cast<float>(-900.0 * (1.0 - easeOutBackT(progress(t, T_BAR_L, D_BAR))));
        const float u0 = std::max(-SQ, BAR_LEFT.u + offL);
        const float u1 = std::min( SQ, BAR_LEFT.u + BAR_LEFT.w + offL);
        const float v  = BAR_LEFT.v + pieceL;    // 左の板と一緒に動く
        if(progress(t, T_BAR_L, D_BAR) > 0.0)
            p.roundedBoxUV(u0, v, u1, v + BAR_LEFT.h, 0.0f, BAR_RADIUS, BAR_RADIUS, 0.0f, WHITE);
    }

    // ----- NOTES：1文字ずつ、下から浮かび上がる -----
    for(int i = 0; i < TEXT_COUNT; i++){
        const Box& b = TEXT[i];
        const int k = letterOf(b);
        const double lp = progress(t, T_TEXT + STEP_TEXT * k, D_TEXT);
        if(lp <= 0.0) continue;

        const float rise = static_cast<float>(60.0 * (1.0 - easeOutCubicT(lp)));
        SDL_Color c = WHITE;
        c.a = static_cast<Uint8>(255 * easeOutCubicT(lp) * (1.0 - exitP));
        p.roundedBoxUV(b.u, b.v + rise, b.u + b.w, b.v + b.h + rise, 0, 0, 0, 0, c);
    }

    // ----- 「.」：ポンと出る。待機中は拍に合わせて少しふくらむ -----
    {
        const double dp = progress(t, T_DOT, D_DOT);
        if(dp > 0.0){
            double s = easeOutBackT(dp) * (1.0 + 0.07 * pulse) * (1.0 - exitE);
            p.roundedSquareCanvas(DOT_X + DOT_SIZE * 0.5f, DOT_Y + DOT_SIZE * 0.5f,
                                  static_cast<float>(DOT_SIZE * s), static_cast<float>(DOT_RADIUS * s), BLACK);
        }
    }

    p.flush(renderer);
}

} // namespace

// =====================================================================
//  タイトル画面
//    Enter / Space：次へ（ロゴが出ている途中なら、最後まで一気に出す）
//    Esc：ゲームを終了
// =====================================================================
GameScene titleScene(SDL_Window* window, SDL_Renderer* renderer){
    // ロゴを描くテクスチャ（透明で始める）
    SDL_Texture* logoTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                             LOGO_TEX_SIZE, LOGO_TEX_SIZE);
    if(logoTex){
        SDL_SetTextureBlendMode(logoTex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(logoTex, SDL_ScaleModeLinear);   // 縮小するときになめらかにする
    }

    // 「PRESS ENTER」などの文字
    TTF_Font* font  = TTF_OpenFont("fonts/prac.ttf", 40);
    TTF_Font* small = TTF_OpenFont("fonts/prac.ttf", 22);
    auto makeText = [&](TTF_Font* f, const char* s, SDL_Color c, SDL_Rect& outRect) -> SDL_Texture* {
        if(!f) return nullptr;
        SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s, c);
        if(!surf) return nullptr;
        SDL_Texture* tx = SDL_CreateTextureFromSurface(renderer, surf);
        outRect = {0, 0, surf->w, surf->h};
        SDL_FreeSurface(surf);
        return tx;
    };
    SDL_Rect pressRect{}, hintRect{};
    SDL_Texture* pressTex = makeText(font,  "PRESS ENTER", {20, 20, 26, 255}, pressRect);
    SDL_Texture* hintTex  = makeText(small, "ESC : 終了",  {55, 55, 65, 255}, hintRect);
    pressRect.x = SCREEN_W / 2 - pressRect.w / 2;
    pressRect.y = LOGO_Y + LOGO_SIZE + 90;
    hintRect.x  = SCREEN_W - hintRect.w - 40;
    hintRect.y  = SCREEN_H - hintRect.h - 30;

    Mix_Chunk* enterSe = Mix_LoadWAV("sounds/tapsound_2.wav");

    // ----- タイトルの曲 -----
    Mix_Music* bgm = Mix_LoadMUS(TITLE_BGM_PATH);
    if(!bgm) printf("[タイトル] 曲の読込失敗: %s\n", TITLE_BGM_PATH);
    bool bgmStarted = false;
    double bgmStartReal = 0.0;   // 曲を流し始めた時刻（ビジュアライザーを曲と一緒にフェードインさせる）

    // ビジュアライザー：SDL_mixer が混ぜ終わった音を受け取って、周波数ごとの強さを出す
    SpectrumVisualizer visualizer;
    visualizer.attach();

    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    const Uint64 start = SDL_GetPerformanceCounter();
    auto nowMs = [&](){ return static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 / freq; };

    double skipMs = 0.0;        // 途中で Enter を押したとき、時間をどれだけ進めたか
    double exitStartMs = -1.0;  // 次の画面へ移り始めた時刻（-1 = まだ）
    bool running = true;
    GameScene nextScene = GameScene::Select;
    SDL_Event e;

    while(running){
        const double real = nowMs();
        const double t = real + skipMs;   // 演出用の時間

        while(SDL_PollEvent(&e) != 0){
            if(e.type == SDL_QUIT){
                running = false;
                nextScene = GameScene::Shutdown;
            }
            else if(e.type == SDL_KEYDOWN && e.key.repeat == 0){
                const SDL_Keycode k = e.key.keysym.sym;
                if(k == SDLK_ESCAPE){
                    running = false;
                    nextScene = GameScene::Shutdown;
                }
                else if(k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE){
                    if(t < T_INTRO_END){
                        skipMs += T_INTRO_END - t;          // 演出の途中 → 最後まで飛ばす
                    }
                    else if(exitStartMs < 0.0){
                        exitStartMs = real;                 // 次の画面へ
                        if(enterSe) Mix_PlayChannel(-1, enterSe, 0);
                        if(bgmStarted) Mix_FadeOutMusic(static_cast<int>(D_EXIT));   // 画面が暗くなるのと一緒に曲も消す
                    }
                }
            }
        }

        const double exitP = (exitStartMs < 0.0) ? 0.0 : clamp01((real - exitStartMs) / D_EXIT);
        if(exitP >= 1.0) running = false;

        // ロゴが完成したら、曲をフェードインで流し始める（途中で Enter を押して飛ばしたときも、その瞬間から）
        if(bgm && !bgmStarted && t >= T_INTRO_END && exitStartMs < 0.0){
            Mix_FadeInMusic(bgm, -1, BGM_FADE_IN_MS);   // -1 = くり返し
            bgmStarted = true;
            bgmStartReal = real;
        }

        // 待機中の「.」の脈：ロゴが完成してから、一定の速さ（DOT_PULSE_BPM）で
        //   拍の頭で 1 → すぐ 0 に戻る値を「.」の大きさに使う
        double pulse = 0.0;
        if(t >= T_INTRO_END){
            const double phase = std::fmod(t - T_INTRO_END, DOT_PULSE_MS) / DOT_PULSE_MS;   // 拍の中の位置 0〜1
            pulse = std::exp(-phase * 6.0);
        }

        // ---------- 1. ロゴをテクスチャに描く ----------
        if(logoTex){
            SDL_SetRenderTarget(renderer, logoTex);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);   // 透明で消す
            SDL_RenderClear(renderer);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            drawLogo(renderer, t, exitP, pulse);
            SDL_SetRenderTarget(renderer, nullptr);
        }

        // アイコンの今の位置：真ん中 → 左寄り（文字ロゴの場所を空ける）
        const double shiftP = easeInOutCubicT(progress(t, T_SHIFT, D_SHIFT));
        const SDL_Rect LOGO_RECT = {
            static_cast<int>(LOGO_X_CENTER + (LOGO_X_LOCKUP - LOGO_X_CENTER) * shiftP),
            LOGO_Y, LOGO_SIZE, LOGO_SIZE
        };

        // ---------- 2. 画面 ----------
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        drawBackground(renderer, real);

        // ビジュアライザー：背景のすぐ上（ロゴより奥）に描く。曲のフェードインに合わせて濃くなる
        visualizer.update();
        if(bgmStarted){
            const double fade = clamp01((real - bgmStartReal) / BGM_FADE_IN_MS) * (1.0 - exitP);
            visualizer.color = VIS_COLOR;
            visualizer.color.a = static_cast<Uint8>(VIS_COLOR.a * fade);
            visualizer.render(renderer);
        }

        if(logoTex){
            // 影：同じロゴを黒く・薄く・少しずらして先に貼る
            SDL_Rect shadow = {LOGO_RECT.x + 14, LOGO_RECT.y + 16, LOGO_RECT.w, LOGO_RECT.h};
            SDL_SetTextureColorMod(logoTex, 0, 0, 0);
            SDL_SetTextureAlphaMod(logoTex, 60);
            SDL_RenderCopy(renderer, logoTex, nullptr, &shadow);

            SDL_SetTextureColorMod(logoTex, 255, 255, 255);
            SDL_SetTextureAlphaMod(logoTex, 255);
            SDL_RenderCopy(renderer, logoTex, nullptr, &LOGO_RECT);
        }

        // 「.」が出た瞬間、周りに枠が広がって消える（判定の破裂と同じ考え方）
        {
            const double rp = progress(t, T_DOT, D_RING);
            if(rp > 0.0 && rp < 1.0){
                const float s = static_cast<float>(LOGO_SIZE) / titlelogo::CANVAS;
                const float cx = LOGO_RECT.x + (titlelogo::DOT_X + titlelogo::DOT_SIZE * 0.5f) * s;
                const float cy = LOGO_RECT.y + (titlelogo::DOT_Y + titlelogo::DOT_SIZE * 0.5f) * s;
                const float size = titlelogo::DOT_SIZE * s + static_cast<float>(easeOutCubicT(rp) * 90.0);
                drawFrameRect(renderer, cx - size * 0.5f, cy - size * 0.5f, size, size, 4.0f,
                              SDL_Color{0, 0, 0, static_cast<Uint8>(200 * (1.0 - rp))});
            }
        }

        // 「simple.notes」の文字ロゴ
        if(SHOW_LAYOUT_GUIDE){
            const Uint8 ga = static_cast<Uint8>(140 * progress(t, T_WORDMARK, D_WORDMARK) * (1.0 - exitP));
            drawDashedRect(renderer, WORDMARK_RECT, SDL_Color{30, 30, 40, ga});
        }
        drawWordmark(renderer, WORDMARK_RECT, t, exitP, pulse);

        // 「PRESS ENTER」：演出が終わったらフェードインして、ゆっくり点滅
        if(pressTex){
            const double in = progress(t, T_INTRO_END, 400.0);
            const double blink = 0.55 + 0.45 * std::cos((t - T_INTRO_END) / 1000.0 * 3.14159265 * 1.2);
            const double a = in * blink * (1.0 - exitP);
            SDL_SetTextureAlphaMod(pressTex, static_cast<Uint8>(255 * clamp01(a)));
            SDL_RenderCopy(renderer, pressTex, nullptr, &pressRect);
        }
        if(hintTex){
            SDL_SetTextureAlphaMod(hintTex, static_cast<Uint8>(255 * progress(t, T_INTRO_END, 400.0) * (1.0 - exitP)));
            SDL_RenderCopy(renderer, hintTex, nullptr, &hintRect);
        }

        // 次の画面へ移るとき：全体を暗くしていく（選曲画面の暗い背景につながる）
        if(exitP > 0.0){
            SDL_SetRenderDrawColor(renderer, 15, 15, 25, static_cast<Uint8>(255 * easeInCubicT(exitP)));
            SDL_RenderFillRect(renderer, nullptr);
        }

        SDL_RenderPresent(renderer);
        g_framePacer.endFrame(window);
    }

    visualizer.detach();   // 曲を止める前に音のフックを外す（次の画面が自分のビジュアライザーを付けられるように）
    if(bgm){
        Mix_HaltMusic();
        Mix_FreeMusic(bgm);
    }
    if(enterSe) Mix_FreeChunk(enterSe);
    if(pressTex) SDL_DestroyTexture(pressTex);
    if(hintTex) SDL_DestroyTexture(hintTex);
    if(font) TTF_CloseFont(font);
    if(small) TTF_CloseFont(small);
    if(logoTex) SDL_DestroyTexture(logoTex);

    return nextScene;
}