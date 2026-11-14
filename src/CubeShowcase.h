#pragma once

// =====================================================================
//  CubeShowcase.h — 画面の真ん中の「回る台座 ＋ 立方体」（選曲画面・メニュー画面で共通）
//    SelectScene.cpp から取り出した。中身の説明は下のそれぞれのコメントを参照
// =====================================================================

#include "GameCommon.h"
#include "Play/PlaySceneManager.h"   // drawCube・drawThickLine・BURST_SQ_*・HOLO_* など（renderGame.h / drawFrame.h）
#include <algorithm>
#include <cmath>
#include <vector>

// =====================================================================
//  画面の真ん中の奥：回り続ける四角の台座 ＋ その上で回る立方体
//
//  ・台座：プレイ画面の破裂エフェクトの「重なった四角」（BURST_SQ_SIZE・ALPHA・THICK を同じ値で使う）
//    床に置いた板に見えるよう、縦を PEDESTAL_FLAT 倍につぶして描く（斜め上から見た形）。
//    大きい四角を一番下に、小さい四角ほど少し上に重ね、段のある台座にする。
//    ずっと同じ速さで回り続け、小さい四角ほど少しずつ速く回る（向きは破裂と同じく交互）
//      回る速さ = PEDESTAL_SPIN × (1 + PEDESTAL_SPIN_STEP × 段の番号)
//  ・立方体：プレイ画面の左上の Cube のアイコン（renderGame.h の drawCube）と同じ回り方で、大きく描く
//  ・一覧・ジャケット・文字より先に描くので、いつも一番奥（背景のすぐ手前）になる
// =====================================================================
inline constexpr float PEDESTAL_SIZE      = 230.0f;   // 一番大きい四角の 1 辺(px)。回ると横幅は最大 √2 倍（約 325px）になるので、
                                                      //   左の列（x ≦ 790）と曲の一覧（x ≧ 1150）の間に収まる大きさにする
inline constexpr float PEDESTAL_FLAT      = 0.38f;    // 縦をつぶす割合（小さいほど寝て見える）
inline constexpr float PEDESTAL_STEP_PX   = 14.0f;    // 段ごとに上へずらす高さ(px)
inline constexpr float PEDESTAL_SPIN      = 0.45f;    // 一番大きい四角の回る速さ(ラジアン/秒)
inline constexpr float PEDESTAL_SPIN_STEP = 0.35f;    // 1 段小さくなるごとに速くする割合
inline constexpr SDL_Color PEDESTAL_COLOR = kPerfectColor;   // 破裂（PERFECT）と同じ淡い黄色
inline constexpr float PEDESTAL_Y         = SCREEN_H * 0.5f + 150.0f;   // 台座の中心の y
inline constexpr float SHOWCASE_CUBE_SIZE = 120.0f;   // 立方体の大きさ(px)
inline constexpr float SHOWCASE_CUBE_GAP  = 18.0f;    // 一番上の段から立方体の下までのすき間(px)

// 縦をつぶした「回った四角の枠」
inline void drawFlatSquareFrame(SDL_Renderer* r, float cx, float cy, float size, float angle, float flat,
                                float thick, SDL_Color c){
    const float h = size * 0.5f;
    const float cs = std::cos(angle), sn = std::sin(angle);
    const float lx[4] = {-h, h, h, -h};
    const float ly[4] = {-h, -h, h, h};
    float px[4], py[4];
    for(int k = 0; k < 4; k++){
        px[k] = cx + lx[k] * cs - ly[k] * sn;
        py[k] = cy + (lx[k] * sn + ly[k] * cs) * flat;
    }
    for(int k = 0; k < 4; k++){
        const int n = (k + 1) % 4;
        drawThickLine(r, px[k], py[k], px[n], py[n], thick, c);
    }
}

// ----- 画面どうしのつなぎ（メニュー画面 ⇔ 選曲画面）-----
//   出ていく画面がここに書き、次の画面が最初に読んで None に戻す
enum class SceneHandoff{
    None,           // ふつうに来た（タイトルから・ほかの画面から）
    MenuToSelect,   // メニュー → 選曲：選曲画面でジャンルのウィンドウをグリッチで出し、背景をなめらかに変える
    SelectToMenu    // 選曲 → メニュー：メニュー画面でウィンドウを下から上げる（暗転・立方体のグリッチはしない）
};
inline SceneHandoff g_sceneHandoff = SceneHandoff::None;

// ----- 回る速さ：クリックで速くなり、慣性でゆっくり元に戻る -----
//   回った角度を毎フレーム「速さ × 経過時間」で足していく（時刻から直接計算すると、速さを変えた瞬間に角度が飛ぶため）
//   速さ = ふだんの速さ × (1 + boost)。クリックで boost を SPIN_BOOST_ADD 増やし（最大 SPIN_BOOST_MAX）、
//   boost は e^(−経過時間 / SPIN_DECAY_MS) で 0 に近づく → 勢いよく回ってから、だんだんゆっくりになる
inline constexpr double SPIN_BOOST_ADD = 6.0;      // 1 回のクリックで足す勢い（6 = ふだんの 7 倍の速さ）
inline constexpr double SPIN_BOOST_MAX = 15.0;     // 勢いの上限（連打しても速くなりすぎない）
inline constexpr double SPIN_DECAY_MS  = 900.0;    // 勢いが弱まる速さ（大きいほど長く回り続ける）

struct ShowcaseSpin{
    double pedestal[BURST_SQ_COUNT] = {};   // 台座の四角ごとの回った角度(ラジアン)
    double cubeT = 0.0;                     // 立方体の回り具合（Cube のアイコンと同じ式に入れる値）
    double boost = 0.0;                     // 今の勢い
    Uint32 last  = 0;
    bool   windowOpen = false;              // Cube の数のウィンドウを開いているか

    // 隠し要素：0.3 秒以内の間隔で 20 回続けてクリックすると、正十二面体に変わる
    int    combo = 0;                       // 続けてクリックした回数
    Uint32 lastClick = 0;                   // 前にクリックした時刻
    int    easter = 0;                      // 0 = 立方体、1 = 十二面体へ変わる途中、2 = 十二面体、3 = 立方体へ戻る途中
    Uint32 easterStart = 0;                 // 今の段階が始まった時刻
};
inline ShowcaseSpin g_showcase;   // 選曲画面とメニュー画面で同じものを使う（画面を移っても回り方が続く）

inline constexpr int    EASTER_CLICKS     = 20;      // 必要な連打の回数
inline constexpr Uint32 EASTER_GAP_MS     = 300;     // この間隔以内のクリックだけ「続けて」と数える
inline constexpr double EASTER_GLITCH_MS  = 450.0;   // 変わるときのグリッチの長さ
inline constexpr double EASTER_HOLD_MS    = 4000.0;  // 十二面体でいる時間

// 立方体をクリックしたときに呼ぶ：連打を数えて、20 回目で十二面体へ
inline void countShowcaseClick(){
    const Uint32 now = SDL_GetTicks();
    g_showcase.combo = (g_showcase.lastClick != 0 && now - g_showcase.lastClick <= EASTER_GAP_MS) ? g_showcase.combo + 1 : 1;
    g_showcase.lastClick = now;
    if(g_showcase.easter == 0 && g_showcase.combo >= EASTER_CLICKS){
        g_showcase.easter = 1;
        g_showcase.easterStart = now;
        g_showcase.combo = 0;
    }
}

// ----- 正十二面体 -----
//   頂点 20 個：(±1, ±1, ±1)、(0, ±1/φ, ±φ)、(±1/φ, ±φ, 0)、(±φ, 0, ±1/φ)（φ = 黄金比）。外接球の半径は √3
//   面 12 個（正五角形）：面の向き（法線）は (0, ±φ, ±1) などの 12 方向。その向きに一番出ている 5 つの頂点が 1 つの面
//   → 最初に 1 回だけ計算して、面ごとに頂点を一周の順に並べておく
struct Dodecahedron{
    float v[20][3];
    int   face[12][5];
    float normal[12][3];
    Dodecahedron(){
        const float p = 1.6180340f, q = 1.0f / p;
        int n = 0;
        for(int i = 0; i < 8; i++){ v[n][0] = (i & 1) ? 1.f : -1.f; v[n][1] = (i & 2) ? 1.f : -1.f; v[n][2] = (i & 4) ? 1.f : -1.f; n++; }
        for(int i = 0; i < 4; i++){
            const float a = (i & 1) ? q : -q, b = (i & 2) ? p : -p;
            v[n][0] = 0; v[n][1] = a; v[n][2] = b; n++;
            v[n][0] = a; v[n][1] = b; v[n][2] = 0; n++;
            v[n][0] = b; v[n][1] = 0; v[n][2] = a; n++;
        }
        int f = 0;
        for(int i = 0; i < 4; i++){
            const float a = (i & 1) ? 1.f : -1.f, b = (i & 2) ? p : -p;
            const float ns[3][3] = {{0, b, a}, {b, a, 0}, {a, 0, b}};   // 面の向き：(0, ±φ, ±1) とその並べ替え
            for(const auto& nm : ns){
                // その向きに一番出ている 5 頂点を集める
                float best = -1e9f;
                for(int k = 0; k < 20; k++) best = std::max(best, v[k][0] * nm[0] + v[k][1] * nm[1] + v[k][2] * nm[2]);
                int ids[5] = {0, 0, 0, 0, 0}, c = 0;
                for(int k = 0; k < 20 && c < 5; k++){
                    if(v[k][0] * nm[0] + v[k][1] * nm[1] + v[k][2] * nm[2] > best - 1e-3f) ids[c++] = k;
                }
                // 面の中心から見た角度の順に並べる（一周の順）
                const float len = std::sqrt(nm[0] * nm[0] + nm[1] * nm[1] + nm[2] * nm[2]);
                const float N[3] = {nm[0] / len, nm[1] / len, nm[2] / len};
                float cxyz[3] = {0, 0, 0};
                for(int k = 0; k < 5; k++) for(int d = 0; d < 3; d++) cxyz[d] += v[ids[k]][d] / 5.f;
                float U[3] = {v[ids[0]][0] - cxyz[0], v[ids[0]][1] - cxyz[1], v[ids[0]][2] - cxyz[2]};
                const float W[3] = {N[1] * U[2] - N[2] * U[1], N[2] * U[0] - N[0] * U[2], N[0] * U[1] - N[1] * U[0]};
                float ang[5];
                for(int k = 0; k < 5; k++){
                    const float d[3] = {v[ids[k]][0] - cxyz[0], v[ids[k]][1] - cxyz[1], v[ids[k]][2] - cxyz[2]};
                    ang[k] = std::atan2(d[0] * W[0] + d[1] * W[1] + d[2] * W[2], d[0] * U[0] + d[1] * U[1] + d[2] * U[2]);
                }
                for(int x = 0; x < 5; x++) for(int y = x + 1; y < 5; y++) if(ang[y] < ang[x]){ std::swap(ang[x], ang[y]); std::swap(ids[x], ids[y]); }
                for(int k = 0; k < 5; k++) face[f][k] = ids[k];
                for(int d = 0; d < 3; d++) normal[f][d] = N[d];
                f++;
            }
        }
    }
};

// 正十二面体を描く（drawCube と同じ回し方・光の当て方・色。radius = 外接球の半径(px)）
inline void drawDodecahedron(SDL_Renderer* r, float cx, float cy, float radius, float ax, float ay, float az,
                             SDL_Color base, float alpha, SDL_Color edge, float faceMul){
    static const Dodecahedron D;
    const float s = radius / std::sqrt(3.0f);
    const float cxs = std::cos(ax), sxs = std::sin(ax), cys = std::cos(ay), sys = std::sin(ay), czs = std::cos(az), szs = std::sin(az);
    auto rot = [&](float x, float y, float z, float& ox, float& oy, float& oz){
        float y1 = y * cxs - z * sxs, z1 = y * sxs + z * cxs;
        float x2 = x * cys + z1 * sys, z2 = -x * sys + z1 * cys;
        ox = x2 * czs - y1 * szs; oy = x2 * szs + y1 * czs; oz = z2;
    };
    float px[20], py[20];
    for(int i = 0; i < 20; i++){
        float X, Y, Z;
        rot(D.v[i][0], D.v[i][1], D.v[i][2], X, Y, Z);
        px[i] = cx + X * s; py[i] = cy + Y * s;
    }
    const float L[3] = {-0.45f, -0.70f, -0.55f};
    const float Ln = 1.0f / std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
    bool visible[12] = {};
    std::vector<SDL_Vertex> verts;
    std::vector<int> idx;
    for(int f = 0; f < 12; f++){
        float n[3];
        rot(D.normal[f][0], D.normal[f][1], D.normal[f][2], n[0], n[1], n[2]);
        if(n[2] >= -0.02f) continue;   // 奥を向いている面は見えない
        visible[f] = true;
        const float lit = std::max(0.0f, (n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) * Ln);
        const float shade = 0.25f + 0.75f * lit;
        const SDL_Color c = {
            static_cast<Uint8>(std::min(255.0f, base.r + 70 * shade)),
            static_cast<Uint8>(std::min(255.0f, base.g + 85 * shade)),
            static_cast<Uint8>(std::min(255.0f, base.b + 110 * shade)),
            static_cast<Uint8>(std::clamp(55.0f * alpha * faceMul, 0.0f, 255.0f))};
        const int b0 = static_cast<int>(verts.size());
        for(int k = 0; k < 5; k++) verts.push_back({SDL_FPoint{px[D.face[f][k]], py[D.face[f][k]]}, c, SDL_FPoint{0, 0}});
        for(int k = 1; k + 1 < 5; k++){ idx.push_back(b0); idx.push_back(b0 + k); idx.push_back(b0 + k + 1); }
    }
    if(faceMul > 0.0f && !verts.empty()){
        SDL_RenderGeometry(r, nullptr, verts.data(), static_cast<int>(verts.size()), idx.data(), static_cast<int>(idx.size()));
    }
    // 辺：見えている面の辺を描く（2 つの面で共有する辺は、番号の小さい方の面から 1 回だけ）
    SDL_Color ec = edge;
    ec.a = static_cast<Uint8>(std::clamp(edge.a / 255.0f * 190.0f * alpha, 0.0f, 255.0f));
    for(int f = 0; f < 12; f++){
        for(int k = 0; k < 5; k++){
            const int a = D.face[f][k], b = D.face[f][(k + 1) % 5];
            // この辺を持つもう 1 つの面
            int other = -1;
            for(int g = 0; g < 12 && other < 0; g++){
                if(g == f) continue;
                for(int m = 0; m < 5; m++){
                    const int c0 = D.face[g][m], c1 = D.face[g][(m + 1) % 5];
                    if((c0 == a && c1 == b) || (c0 == b && c1 == a)){ other = g; break; }
                }
            }
            // この面が見えていて、もう 1 つの面が見えていないか番号が大きいときだけ描く（同じ辺を 2 回描かない）
            if(!visible[f]) continue;
            if(other >= 0 && visible[other] && other < f) continue;
            drawThickLine(r, px[a], py[a], px[b], py[b], 1.5f, ec);
        }
    }
}

// 真ん中の形を描く（立方体 or 十二面体）。グリッチの色ずれ用に、辺の色と面の濃さを変えられる
inline void drawShowcaseShape(SDL_Renderer* r, bool dodeca, float x, float y, float t, SDL_Color edge, float faceMul, float alpha){
    if(dodeca){
        drawDodecahedron(r, x, y, SHOWCASE_CUBE_SIZE * 0.87f, 0.5f + t * 0.4f, t, 0.3f,
                         SDL_Color{60, 70, 95, 255}, alpha, edge, faceMul * 2.0f);
    }
    else{
        drawCube(r, x, y, SHOWCASE_CUBE_SIZE, 0.5f + t * 0.4f, t, 0.3f,
                 SDL_Color{60, 70, 95, 255}, alpha, 1.0f, 1.0f, edge, faceMul * 2.0f);
    }
}

// 立方体の中心（クリックの当たり判定・ウィンドウの線に使う）
inline void showcaseCubeCenter(float& x, float& y){
    const float topY = PEDESTAL_Y - PEDESTAL_STEP_PX * (BURST_SQ_COUNT - 1);
    x = SCREEN_W * 0.5f;
    y = topY - SHOWCASE_CUBE_GAP - SHOWCASE_CUBE_SIZE * 0.87f;   // 回っても台座に重ならない高さ（1 辺の √3/2 倍）
}

// (mx, my) が立方体の上か（立方体の中心から 1 辺の 0.75 倍の円の中）
inline bool hitShowcaseCube(int mx, int my){
    float cx, cy;
    showcaseCubeCenter(cx, cy);
    const float dx = mx - cx, dy = my - cy, r = SHOWCASE_CUBE_SIZE * 0.75f;
    return dx * dx + dy * dy <= r * r;
}

inline void draw_CubeShowcase(SDL_Renderer* renderer){
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const float cx = SCREEN_W * 0.5f;

    // 経過時間 → 角度を進める・勢いを弱める
    const Uint32 now = SDL_GetTicks();
    const double dt = (g_showcase.last == 0) ? 0.0 : std::min<double>(now - g_showcase.last, 100.0);
    g_showcase.last = now;
    const double mul = 1.0 + g_showcase.boost;
    for(int k = 0; k < BURST_SQ_COUNT; k++){
        const double dir   = (BURST_SQ_SPIN[k] >= 0.0f) ? 1.0 : -1.0;
        const double speed = PEDESTAL_SPIN * (1.0 + PEDESTAL_SPIN_STEP * k);   // 小さい四角ほど少し速い
        g_showcase.pedestal[k] = std::fmod(g_showcase.pedestal[k] + dir * speed * mul * dt / 1000.0, 2.0 * 3.14159265358979);
    }
    g_showcase.cubeT += 0.0012 * mul * dt;
    g_showcase.boost *= std::exp(-dt / SPIN_DECAY_MS);

    // 台座：大きい四角（下の段）から順に
    for(int k = 0; k < BURST_SQ_COUNT; k++){
        const float size  = PEDESTAL_SIZE * BURST_SQ_SIZE[k];
        const float angle = 0.785398f + static_cast<float>(g_showcase.pedestal[k]);   // 45°（ひし形）から回る
        const float y     = PEDESTAL_Y - PEDESTAL_STEP_PX * k;
        SDL_Color c = HOLO_FRONT_COLOR;
        c.a = static_cast<Uint8>(255.0f * BURST_SQ_ALPHA[k]);
        drawFlatSquareFrame(renderer, cx, y, size, angle, PEDESTAL_FLAT, BURST_SQ_THICK[k], c);
    }

    // 立方体：一番上の段の少し上に置く（左上の Cube のアイコンと同じ回り方）
    float cubeX, cubeY;
    showcaseCubeCenter(cubeX, cubeY);
    const float t = static_cast<float>(std::fmod(g_showcase.cubeT, 2000.0 * 3.14159265358979));

    // 隠し要素の段階を進める：変わる途中（グリッチ）→ 十二面体 → 戻る途中（グリッチ）→ 立方体
    double phaseP = 1.0;   // 今の段階の進み具合（0〜1）
    if(g_showcase.easter != 0){
        const double el = static_cast<double>(now - g_showcase.easterStart);
        const double len = (g_showcase.easter == 2) ? EASTER_HOLD_MS : EASTER_GLITCH_MS;
        phaseP = std::clamp(el / len, 0.0, 1.0);
        if(phaseP >= 1.0){
            g_showcase.easter = (g_showcase.easter + 1) % 4;   // 1 → 2 → 3 → 0
            g_showcase.easterStart = now;
            phaseP = 0.0;
        }
    }

    if(g_showcase.easter == 0 || g_showcase.easter == 2){
        drawShowcaseShape(renderer, g_showcase.easter == 2, cubeX, cubeY, t, HOLO_FRONT_COLOR, 1.0f, 1.0f);
        return;
    }

    // グリッチで変わる途中：30ms ごとに「どちらの形を出すか」を、進み具合の確率で決める
    //   変わる途中（1）は十二面体の出る確率が 0 → 1、戻る途中（3）は 1 → 0。
    //   真ん中ほど強く（sin）ぶれて、赤と青の色ずれの形と、横の走査線を重ねる
    const uint32_t seed = now / 30u;
    const double dodecaChance = (g_showcase.easter == 1) ? phaseP : 1.0 - phaseP;
    const bool showDodeca = holoHash01(seed * 2654435761u + 7u) < dodecaChance;
    const float strength = static_cast<float>(std::sin(3.14159265358979 * phaseP)) * 0.7f + 0.3f;
    const float jitter = static_cast<float>(holoHash01(seed * 31u + 3u) * 2.0 - 1.0) * 12.0f * strength;
    const float split  = 6.0f * strength;
    drawShowcaseShape(renderer, !showDodeca, cubeX - split + jitter, cubeY, t, SDL_Color{255, 40, 60, 150}, 0.0f, strength);
    drawShowcaseShape(renderer, showDodeca, cubeX + split + jitter, cubeY, t, SDL_Color{40, 200, 255, 150}, 0.0f, strength);
    drawShowcaseShape(renderer, showDodeca, cubeX + jitter, cubeY, t, HOLO_FRONT_COLOR, 1.0f, 1.0f);
    for(int k = 0; k < 3; k++){
        const float ly = cubeY + static_cast<float>(holoHash01(seed * 97u + k * 13u) * 2.0 - 1.0) * SHOWCASE_CUBE_SIZE;
        const float lw = SHOWCASE_CUBE_SIZE * (1.2f + static_cast<float>(holoHash01(seed * 61u + k)) * 1.2f);
        const float lx = cubeX - lw * 0.5f + static_cast<float>(holoHash01(seed * 17u + k * 5u) * 2.0 - 1.0) * 30.0f;
        SDL_SetRenderDrawColor(renderer, HOLO_FRONT_COLOR.r, HOLO_FRONT_COLOR.g, HOLO_FRONT_COLOR.b, static_cast<Uint8>(140 * strength));
        SDL_FRect line = {lx, ly, lw, 2.0f};
        SDL_RenderFillRectF(renderer, &line);
    }
}