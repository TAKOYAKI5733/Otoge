#pragma once

#include <random>
#include "GameCommon.h"
#include "Play/GameContext.h"
#include "Play/type.h"
#include "Play/drawFrame.h"   // NOTE_HEAD_H（立方体が跳ね返る床の高さに使う）

inline constexpr int32_t LONG_RELEASE_GRACE_MS = 200;

// つながったロング：前のロングの終点と次のロングの始点がこれ以内なら、押しっぱなしのままで次の始点を取れる
inline constexpr int32_t LONG_CHAIN_GAP_MS = 120;

// レーン l を今押しているか
//   正確に見ているレーン（PreciseInput）は、押した瞬間と同じ別スレッドが見た「押している状態」を使う。
//   SDL のキーの状態（currentPressed）とも OR を取る：どちらか片方が押していると見ていれば押している
//   （2 つの見え方が食い違ったときに、ホールドが途切れる側には倒さない）
inline bool laneDown(const GameContext& ctx, int l){
    const bool precise = ctx.preciseLanes && ctx.preciseHeld && ctx.preciseLanes[l];
    return ctx.currentPressed[l] || (precise && ctx.preciseHeld[l]);
}

// ノーツの幅のどこかのレーンが今押されているか
inline bool isAreaHeld(const GameContext& ctx, const Note& note){
    for(int l = std::max(0, note.lane); l <= std::min(5, note.lane + note.widthLanes - 1); l++){
        if(laneDown(ctx, l)) return true;
    }
    return false;
}

// note（ロングの始点）の直前に、同じレーンで「押したまま終点まで行けた」ロングがあるか
inline bool hasChainedLongBefore(const GameContext& ctx, const Note& note){
    for(const Note& prev : ctx.notes){
        if(&prev == &note || prev.type != NoteType::Long || !prev.isHit || prev.isFailed || !prev.endHeld) continue;
        // レーンが重なっているか
        const int a0 = prev.lane, a1 = prev.lane + prev.widthLanes - 1;
        const int b0 = note.lane, b1 = note.lane + note.widthLanes - 1;
        if(a1 < b0 || b1 < a0) continue;
        const int32_t prevEnd = prev.targetTime + prev.durationMs;
        if(std::abs(note.targetTime - prevEnd) <= LONG_CHAIN_GAP_MS) return true;
    }
    return false;
}

// ロングノーツを失敗状態にする。cutTime より上（後ろ）の部分を暗い芯で流す
inline void markLongFailed(Note& note, int32_t cutTime){
    if(note.type != NoteType::Long) return;
    note.isFailed = true;
    note.failCutTime = std::max(cutTime, note.targetTime);
}

// 判定文字と一緒に出す「ズレ」を記録する
//   pressTime = 押した瞬間の曲の時刻（PreciseInput があればその時刻、なければフレームの時刻）
//   ほかのファイル（Autoplay.h など）から4つの引数で呼んだときは、今まで通りフレームの時刻を使う
inline void recordTiming(JudgeEffect& je, const GameContext& ctx, const Note& note, int judgeType, int32_t pressTime = NO_PRESS_TIME){
    if(pressTime == NO_PRESS_TIME) pressTime = ctx.musicTime;
    je.hasTiming = true;
    je.diffMs = pressTime - note.targetTime;   // - = 早い、+ = 遅い
    je.judgeType = judgeType;
    if(ctx.timingLog) ctx.timingLog->push_back({je.diffMs, judgeType});
}

inline void playHitSound(const GameContext& ctx, const Note& note){
    Mix_Chunk* se = ctx.tap_sound;
    if((note.type == NoteType::Normal_c || note.isCyan) && ctx.tap_sound_c){
        se = ctx.tap_sound_c;
    }
    if(se) Mix_PlayChannel(-1, se, 0);
}

inline void toggleLaneActive(GameContext& ctx, const Note& note){
    int startLane = note.lane;
    int endLane = note.lane + note.widthLanes - 1;

    for(int i = startLane; i <= endLane; i++){
        if(i < 0 || i >= 6) continue;
        ctx.laneActive[i] = !ctx.laneActive[i];
    }
}

// ノーツを取ったときの立方体を count 個出す（ノーツの幅の中の、ばらばらの位置から上へ飛び出す）
//   床 = 判定ボックスの下の端。立方体の下の面がそこに着いたら跳ね返る（中心の位置で考えるので、1 辺の半分だけ上）
//   オートプレイ中は出さない（Cube は自分で叩いたときだけ集まる）
inline void spawnHitCubes(GameContext& ctx, const Note& note, int count){
    if(!ctx.tickParticles || note.lane < 0 || ctx.isAutoplay) return;

    static std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> posDist(0.2f, 0.8f);         // ノーツの幅の中のどこから出るか
    std::uniform_real_distribution<float> vxDist(-0.10f, 0.10f);       // 横の速さ(px/ms)
    std::uniform_real_distribution<float> vyDist(0.55f, 0.80f);        // 上へ飛び出す速さ(px/ms)
    std::uniform_real_distribution<float> sizeDist(14.0f, 20.0f);      // 1 辺(px)
    std::uniform_real_distribution<float> rotDist(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> spinDist(-0.012f, 0.012f);   // 回る速さ(rad/ms)
    std::uniform_real_distribution<float> alphaDist(0.7f, 1.0f);
    std::uniform_real_distribution<float> tpDist(CUBE_TELEPORT_FROM, CUBE_TELEPORT_TO);   // 落ちる途中のどこで止まるか

    const float left  = static_cast<float>(ctx.startX + ctx.laneWidth * note.lane);
    const float width = static_cast<float>(ctx.laneWidth * std::max(1, note.widthLanes));

    for(int k = 0; k < count; k++){
        TickParticle p;
        p.x = left + width * posDist(rng);
        p.y = static_cast<float>(ctx.judgeY);
        p.vx = vxDist(rng);
        p.vy = -vyDist(rng);
        p.size = sizeDist(rng);
        p.floorDy = NOTE_HEAD_H * 0.5f - p.size * 0.5f;
        p.rotX = rotDist(rng); p.rotY = rotDist(rng); p.rotZ = rotDist(rng);
        p.spinX = spinDist(rng); p.spinY = spinDist(rng); p.spinZ = spinDist(rng);
        p.lane = std::clamp(static_cast<int>((p.x - ctx.startX) / ctx.laneWidth), 0, 5);   // 出た所のレーンの色
        p.spawnTime = ctx.musicTimeExact;
        p.alpha = alphaDist(rng);
        // 一番高い所に着く時刻 = 上へ飛ぶ速さ ÷ 重力。そこから床までの高さを落ちきる時刻 = √(2 × 高さ ÷ 重力)
        //   その間の CUBE_TELEPORT_FROM〜TO の割合の所で止める（＝落ちている途中）
        const double G = CUBE_GRAVITY;
        const double tApex = -p.vy / G;
        const double fallH = p.vy * p.vy / (2.0 * G) + p.floorDy;
        const double tLand = tApex + std::sqrt(2.0 * fallH / G);
        p.teleportMs = static_cast<float>(tApex + (tLand - tApex) * tpDist(rng));
        p.collected = false;
        ctx.tickParticles->push_back(p);
    }
}

// ノーツ 1 個ぶん：NOTE_CUBE_MIN〜NOTE_CUBE_MAX 個（ランダム）。出た数だけ、テレポート後に Cube が増える
inline void spawnNoteCubes(GameContext& ctx, const Note& note){
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> countDist(NOTE_CUBE_MIN, NOTE_CUBE_MAX);
    spawnHitCubes(ctx, note, countDist(rng));
}

// ロングの中継点
inline void spawnTickParticles(GameContext& ctx, const Note& ln){
    spawnHitCubes(ctx, ln, TICK_PARTICLE_COUNT);
}

// 同時押し：同じフレームで、幅の広いノーツに使ったレーンを別の指で押したとき、
//   そのノーツとこの時間以内に重なる「同時押しの相手」のノーツだけは取れるようにする
inline constexpr int32_t CHORD_MS = 40;

// ===== ロングの始点を「押している状態」でも取る（押した瞬間を取りこぼしても抜けないように） =====
//   ロングの始点は「押した瞬間（パルス）」で判定しているが、4 つ以上の同時押しなどで押した瞬間がどこかで
//   取りこぼされると、キーを押し続けていてもロングが取れず、抜けて MISS になっていた。
//   → レーンごとに「今の長押しが始まった時刻（holdStart）」を覚えておき、
//     ロングの始点がまだ取れていなくて、そのレーンを押し続けていて、長押しの始まりが判定の幅（±90ms）に入っていれば、
//     その始まりの時刻を「押した時刻」として判定する（PERFECT / GOOD / BAD の幅は叩いたときと同じ）
//   ・判定の幅より前から押しっぱなしにしていた長押しは使わない（ずっと押しておくだけで取れる、を防ぐ）
//   ・1 回の長押しで取れるのは 1 つだけ（used）。離して押し直すと、また使える
//   ・押した瞬間の処理で取れていれば何もしない（こちらは取りこぼしたときの予備）
struct LaneHoldTracker{
    bool    held[6]  = {};
    int32_t start[6] = {};          // 今の長押しが始まった曲の時刻
    bool    used[6]  = {};          // この長押しを、もうノーツに使ったか
    int32_t lastPulse[6] = {INT32_MIN / 2, INT32_MIN / 2, INT32_MIN / 2, INT32_MIN / 2, INT32_MIN / 2, INT32_MIN / 2};   // 最後に「押した瞬間」として使った時刻
    bool    lastFromSdl[6] = {};    // それがフレームごとの判定（予備）から来たものか
    const void* notesId = nullptr;  // どのプレイの記録か（プレイが変わったら作り直す）
    int32_t lastTime = 0;
};
inline LaneHoldTracker& laneHoldTracker(){
    static LaneHoldTracker t;
    return t;
}

// 毎フレーム最初に呼ぶ：レーンごとの長押しの始まりを更新する
inline void updateLaneHolds(const GameContext& ctx){
    LaneHoldTracker& h = laneHoldTracker();
    if(h.notesId != static_cast<const void*>(&ctx.notes) || ctx.musicTime < h.lastTime - 500){
        h = LaneHoldTracker{};          // 新しいプレイ（リトライ含む）
        h.notesId = &ctx.notes;
    }
    h.lastTime = ctx.musicTime;
    for(int i = 0; i < 6; i++){
        const bool precise = ctx.preciseLanes && ctx.pressTimes && ctx.preciseLanes[i];
        const bool pressNow = precise && ctx.pressTimes[i] != NO_PRESS_TIME;
        const bool down = laneDown(ctx, i) || pressNow;
        if(down && (!h.held[i] || pressNow)){
            // 押し始めた（または押し直した）：始まりの時刻は、正確な時刻があればそれ、無ければこのフレームの時刻
            h.start[i] = pressNow ? std::min(ctx.pressTimes[i], ctx.musicTime) : ctx.musicTime;
            h.used[i] = false;
        }
        h.held[i] = down;
    }
}

inline void NoteJudge(GameContext& ctx, Tex& tex, Sq& sq){
    // このフレームで「押したのをノーツに使った」レーンと、そのノーツの時刻
    //   前は、幅の広いノーツを取ると、そのノーツの全部のレーンの押下をこのフレームでは捨てていた。
    //   そのため、幅の広いノーツ（レーンノーツなど）と同じ時刻に、そのレーンの中にロングがある同時押しで、
    //   ロングの方の押下が捨てられて、ロングが抜けて MISS になっていた。
    //   → 捨てるのをやめて、「使ったノーツとほぼ同じ時刻（CHORD_MS 以内）のノーツ」だけは取れるようにする
    //     （2 本の指で幅の広いノーツを押したとき、2 本目で次のノーツを先取りしてしまうのは今まで通り防ぐ）
    bool    consumedThisFrame[6] = {false, false, false, false, false, false};
    int32_t consumedAt[6] = {0, 0, 0, 0, 0, 0};

    updateLaneHolds(ctx);
    LaneHoldTracker& holds = laneHoldTracker();

    for(int i = 0; i < 6; i++){
            // 押した瞬間と、その時刻
            //   ・PreciseInput で見ているレーン：別スレッドが約 1ms ごとに見つけた「押した瞬間」を使う
            //     （フレームの間に押して離した速い連打も取りこぼさない。時刻もフレームにそろわない）
            //   ・それ以外（コントローラーなど）：今まで通り、フレームの頭の状態の変化とフレームの時刻
            //   ・正確に見ているレーンでも、別スレッドが押した瞬間を取りこぼしたとき（同時押しが多いときなど）のために、
            //     フレームごとの「押した瞬間」を予備として使う。ただし、同じ押下を 2 回数えないよう、
            //     どちらかで押した瞬間を使ってから PULSE_DEDUP_MS 以内のもう片方は無視する
            constexpr int32_t PULSE_DEDUP_MS = 50;
            const bool precise = ctx.preciseLanes && ctx.pressTimes && ctx.preciseLanes[i];
            const bool sdlEdge = ctx.currentPressed[i] && !ctx.prevPressed[i];
            bool pressedNow = false;
            int32_t tPress = ctx.musicTime;
            if(precise && ctx.pressTimes[i] != NO_PRESS_TIME){
                tPress = std::min(ctx.pressTimes[i], ctx.musicTime);
                // 直前に予備（フレームごと）で同じ押下を使っていたら、二重に数えない
                pressedNow = !(holds.lastFromSdl[i] && tPress - holds.lastPulse[i] <= PULSE_DEDUP_MS);
                if(pressedNow){ holds.lastPulse[i] = tPress; holds.lastFromSdl[i] = false; }
            }
            else if(sdlEdge && (!precise || ctx.musicTime - holds.lastPulse[i] > PULSE_DEDUP_MS)){
                pressedNow = true;   // 正確に見ていないレーン、または別スレッドが取りこぼした押下
                holds.lastPulse[i] = ctx.musicTime;
                holds.lastFromSdl[i] = precise;
            }

            if(pressedNow){
                // 押した時刻に一番近いノーツを探す（前は「並び順で最初に見つかったノーツ」を使っていた）
                //   前のやり方だと、叩かずに通り過ぎた古いノーツ（まだ MISS になる前）が先に見つかると、
                //   押したのがそちらに使われて（判定の外なので何も起きず）、本当に叩きたい次のノーツが抜けて MISS になっていた
                //   同じ近さなら、幅の狭いノーツを優先する（同時押しで、レーンノーツなどの幅の広いノーツに
                //   ロングの押下を取られないように。幅の広いノーツは、ほかのレーンの押下でも取れる）
                Note* target = nullptr;
                int bestDiff = 301;
                for(auto& note : ctx.notes){
                    // ドラッグと、ドラッグロング（始点がドラッグ判定）は叩いて判定しない → 下の「ドラッグの自動判定」で扱う
                    if(note.lane < 0 || note.isHit || note.type == NoteType::Drag || note.isDragLong()) continue;
                    if(i < note.lane || i > note.lane + note.widthLanes - 1) continue;
                    // このレーンはこのフレームで別のノーツに使った → 同時押しの相手だけ
                    if(consumedThisFrame[i] && std::abs(note.targetTime - consumedAt[i]) > CHORD_MS) continue;
                    const int d = std::abs(static_cast<int>(tPress - note.targetTime));
                    if(d < bestDiff || (d == bestDiff && target && note.widthLanes < target->widthLanes)){
                        bestDiff = d;
                        target = &note;
                    }
                }

                for(auto& note : ctx.notes){
                    if(&note != target) continue;

                    int startLane = note.lane;
                    int endLane   = note.lane + note.widthLanes - 1;
                    bool isAreaPressed = (i >= startLane && i <= endLane);

                    if(isAreaPressed){
                        int timeDiff = std::abs(static_cast<int>(tPress - note.targetTime));   // 押した瞬間の時刻で比べる

                        if(timeDiff > 300) continue;

                        Effect newEffect;
                        newEffect.lane = note.lane;
                        newEffect.spawnTime = tPress;   // 破裂は押した瞬間から始める

                        if(timeDiff <= 60){
                            ctx.laneJudge[i] = {tex.perfect, sq.perfect, ctx.musicTime};
                            recordTiming(ctx.laneJudge[i], ctx, note, 1, tPress);
                            note.isHit = true;
                            newEffect.lane = note.lane;
                            newEffect.widthLanes = note.widthLanes;
                            newEffect.judgeType = 1;
                            ctx.effects.push_back(newEffect);
                            spawnNoteCubes(ctx, note);
                            ctx.scoreTracker.registerJudge("PERFECT");
                            ctx.scorePop.startTime = ctx.musicTime;
                            ctx.comboPop.startTime = ctx.musicTime;

                            playHitSound(ctx, note);

                            if(note.type == NoteType::Long){
                                note.isHolding = true;
                                note.lastTickTime = ctx.musicTime;
                            }

                            if(note.type == NoteType::Lane) toggleLaneActive(ctx, note);
                        }

                        else if(timeDiff <= 80){
                            ctx.laneJudge[i] = {tex.good, sq.good, ctx.musicTime};
                            recordTiming(ctx.laneJudge[i], ctx, note, 2, tPress);
                            note.isHit = true;
                            newEffect.judgeType = 2;
                            newEffect.lane = note.lane;
                            newEffect.widthLanes = note.widthLanes;
                            ctx.effects.push_back(newEffect);
                            spawnNoteCubes(ctx, note);

                            ctx.isAP = false;
                            ctx.scoreTracker.registerJudge("GOOD");
                            ctx.scorePop.startTime = ctx.musicTime;
                            ctx.comboPop.startTime = ctx.musicTime;

                            playHitSound(ctx, note);

                            if(note.type == NoteType::Long){
                                note.isHolding = true;
                                note.lastTickTime = ctx.musicTime;
                            }

                            if(note.type == NoteType::Lane) toggleLaneActive(ctx, note);
                        }

                        else if(timeDiff <= 90){
                            ctx.laneJudge[i] = {tex.bad, sq.bad, ctx.musicTime};
                            recordTiming(ctx.laneJudge[i], ctx, note, 3, tPress);
                            note.isHit = true;
                            markLongFailed(note, note.targetTime);
                            newEffect.judgeType = 3;
                            newEffect.lane = note.lane;
                            newEffect.widthLanes = note.widthLanes;

                            ctx.isAP = ctx.isFC = false;
                            ctx.effects.push_back(newEffect);
                            // BAD のときは Cube を出さない
                            ctx.scoreTracker.registerJudge("BAD");
                            if(note.type == NoteType::Lane) toggleLaneActive(ctx, note);
                        }

                        // 判定が出た時刻を記録（描画で「表示の時刻」と比べて、叩いた見た目にする）
                        //   上の continue で isHit のノーツは飛ばしているので、ここで isHit なら今判定したもの
                        //   押した瞬間の時刻にする（表示の時刻がそこを過ぎたら、叩いた見た目になる）
                        if(note.isHit) note.hitTime = tPress;
                        holds.used[i] = true;   // この押下（長押し）はもう使った

                        for(int l = std::max(0, startLane); l <= std::min(5, endLane); l++){
                            consumedThisFrame[l] = true;
                            consumedAt[l] = note.targetTime;
                        }

                        break;
                    }
                }
            }
            ctx.prevPressed[i] = ctx.currentPressed[i];
        }

        //トレースノーツ(ドラッグ)の自動判定 (毎フレーム監視)
        //   ドラッグロングの始点もここで判定する（押しっぱなしで通過すれば PERFECT → そのままホールドへ）
        for(auto& note : ctx.notes){
            if(note.lane < 0 || note.isHit || !(note.type == NoteType::Drag || note.isDragLong())) continue;

            int i = note.lane;
            int32_t timeDiff = ctx.musicTime - note.targetTime;

            // ドラッグの判定（DRAG_WINDOW_MS = ±20ms）
            //   ・判定ラインの ±20ms の間にキーを押していれば取れる（それより前から押しっぱなしでもよい）
            //   ・判定ラインより前から押していた（押し始めが targetTime 以前）→ ちょうど targetTime に破裂する
            //   ・判定ラインを過ぎてから +20ms 以内に押した → 押した瞬間の時刻に破裂する
            //   ・判定はいつも targetTime になってから行う（打鍵音が早く鳴らないように）。
            //     ±20ms の間に一度押していれば（dragArmed）、判定ラインに来る前に離していても取れる
            //   ・押し始めの時刻は、レーンごとの「今の長押しが始まった時刻」（LaneHoldTracker）を使う
            //     → フレームの間隔に関係なく、押した瞬間の時刻で決まる
            constexpr int32_t DRAG_WINDOW_MS = 60;
            bool isAreaPresssed = false;
            int32_t pressStart = INT32_MAX;   // 押しているレーンのうち、一番早い押し始め
            for(int l = std::max(0, note.lane); l <= std::min(5, note.lane + note.widthLanes - 1); l++){
                if(laneDown(ctx, l)){
                    isAreaPresssed = true;
                    if(holds.held[l]) pressStart = std::min(pressStart, holds.start[l]);
                }
            }
            if(pressStart == INT32_MAX) pressStart = ctx.musicTime;
            if(isAreaPresssed && timeDiff >= -DRAG_WINDOW_MS && pressStart <= note.targetTime + DRAG_WINDOW_MS){
                note.dragArmed = true;
                // 判定ラインを過ぎてから押したときは、押した瞬間を覚えておく（破裂の時刻）
                if(pressStart > note.targetTime) note.hitTime = std::max(note.hitTime, pressStart);
            }
            //   フレームとフレームの間に押して離した短い押下も、押した瞬間の記録（PreciseInput）が ±20ms の中なら取れる
            if(ctx.pressTimes && ctx.preciseLanes){
                for(int l = std::max(0, note.lane); l <= std::min(5, note.lane + note.widthLanes - 1); l++){
                    if(!ctx.preciseLanes[l] || ctx.pressTimes[l] == NO_PRESS_TIME) continue;
                    const int32_t pt = std::min(ctx.pressTimes[l], ctx.musicTime);
                    if(pt < note.targetTime - DRAG_WINDOW_MS || pt > note.targetTime + DRAG_WINDOW_MS) continue;
                    note.dragArmed = true;
                    if(pt > note.targetTime) note.hitTime = std::max(note.hitTime, pt);
                }
            }

            // 取れる状態になっていて、判定ラインに来たら PERFECT
            if(note.dragArmed){
                if(timeDiff >= 0){
                    // 破裂の時刻：判定ラインより前から押していた → targetTime ちょうど、後から押した → 押した瞬間
                    const int32_t burstAt = std::clamp(std::max(note.targetTime, note.hitTime), note.targetTime, ctx.musicTime);
                    ctx.laneJudge[i] = {tex.perfect, sq.perfect, ctx.musicTime};
                    note.isHit = true;
                    note.hitTime = burstAt;
                    ctx.scoreTracker.registerJudge("PERFECT");
                    ctx.scorePop.startTime = ctx.musicTime;
                    ctx.comboPop.startTime = ctx.musicTime;

                    Effect dragEffect;
                    dragEffect.lane = i;
                    dragEffect.spawnTime = burstAt;   // 破裂はこの時刻から（表示の時刻がここを過ぎたら始まる）
                    dragEffect.widthLanes = note.widthLanes;
                    dragEffect.judgeType = 1;
                    ctx.effects.push_back(dragEffect);
                    spawnNoteCubes(ctx, note);

                    Mix_PlayChannel(-1, ctx.tap_sound, 0);

                    // ドラッグロング：始点を取れたら、ここからは普通のロングと同じホールド判定（下の 4.）に渡す
                    if(note.isDragLong()){
                        note.isHolding = true;
                        note.lastTickTime = ctx.musicTime;
                    }
                }
            }
        }

        // --- 2.4 押した瞬間を取りこぼしたロングの始点を、押している状態で取る（上の LaneHoldTracker の説明を参照） ---
        for(auto& note : ctx.notes){
            if(note.lane < 0 || note.isHit || note.isHolding || note.type != NoteType::Long || note.isDragLong()) continue;
            // 幅の中で、押し続けていて、まだ使っていない長押しのうち、始まりが始点に一番近いもの
            int lane = -1, bestDiff = 91;
            for(int l = std::max(0, note.lane); l <= std::min(5, note.lane + note.widthLanes - 1); l++){
                if(!holds.held[l] || holds.used[l] || !laneDown(ctx, l)) continue;
                const int d = std::abs(static_cast<int>(holds.start[l] - note.targetTime));
                if(d < bestDiff){ bestDiff = d; lane = l; }
            }
            if(lane < 0) continue;   // 判定の幅（±90ms）の中で押し始めた長押しが無い

            const int32_t tPress = holds.start[lane];
            holds.used[lane] = true;
            Effect e;
            e.lane = note.lane;
            e.widthLanes = note.widthLanes;
            e.spawnTime = tPress;
            note.isHit = true;
            note.hitTime = tPress;
            if(bestDiff <= 60){
                ctx.laneJudge[lane] = {tex.perfect, sq.perfect, ctx.musicTime};
                recordTiming(ctx.laneJudge[lane], ctx, note, 1, tPress);
                e.judgeType = 1;
                ctx.scoreTracker.registerJudge("PERFECT");
                note.isHolding = true;
                note.lastTickTime = ctx.musicTime;
            }
            else if(bestDiff <= 80){
                ctx.laneJudge[lane] = {tex.good, sq.good, ctx.musicTime};
                recordTiming(ctx.laneJudge[lane], ctx, note, 2, tPress);
                e.judgeType = 2;
                ctx.isAP = false;
                ctx.scoreTracker.registerJudge("GOOD");
                note.isHolding = true;
                note.lastTickTime = ctx.musicTime;
            }
            else{
                ctx.laneJudge[lane] = {tex.bad, sq.bad, ctx.musicTime};
                recordTiming(ctx.laneJudge[lane], ctx, note, 3, tPress);
                e.judgeType = 3;
                markLongFailed(note, note.targetTime);
                ctx.isAP = ctx.isFC = false;
                ctx.scoreTracker.registerJudge("BAD");
            }
            ctx.scorePop.startTime = ctx.musicTime;
            ctx.comboPop.startTime = ctx.musicTime;
            ctx.effects.push_back(e);
            if(e.judgeType != 3) spawnNoteCubes(ctx, note);   // BAD のときは Cube を出さない
            playHitSound(ctx, note);
        }

        // --- 2.5 つながったロングの始点 ---
        //   ロングの終点とすぐ次のロングの始点が同じレーンでつながっているとき、プレイヤーは押しっぱなしにする。
        //   始点の判定は「押した瞬間（パルス）」でしか取っていなかったので、押しっぱなしだと始点が取れず、
        //   そのまま 200ms 過ぎて MISS（抜けてミス）になっていた。
        //   → 前のロングを押したまま終点まで行けていて（endHeld）、今もそのレーンを押していれば、
        //     始点が判定ラインに来た時点で PERFECT にして、そのままホールドを始める
        for(auto& note : ctx.notes){
            if(note.lane < 0 || note.isHit || note.isHolding || note.type != NoteType::Long || note.isDragLong()) continue;
            const int32_t diff = ctx.musicTime - note.targetTime;
            if(diff < 0 || diff > 60) continue;                          // 判定ラインに来てから PERFECT の幅の間だけ
            if(!isAreaHeld(ctx, note) || !hasChainedLongBefore(ctx, note)) continue;

            const int i = note.lane;
            ctx.laneJudge[i] = {tex.perfect, sq.perfect, ctx.musicTime};   // 押した瞬間が無いので、ズレ（FAST/LATE・ms）は記録しない
            note.isHit = true;
            note.hitTime = note.targetTime;
            note.isHolding = true;
            note.lastTickTime = ctx.musicTime;
            ctx.scoreTracker.registerJudge("PERFECT");
            ctx.scorePop.startTime = ctx.musicTime;
            ctx.comboPop.startTime = ctx.musicTime;

            Effect e;
            e.lane = note.lane;
            e.widthLanes = note.widthLanes;
            e.spawnTime = ctx.musicTime;
            e.judgeType = 1;
            ctx.effects.push_back(e);
            spawnNoteCubes(ctx, note);
            Mix_PlayChannel(-1, ctx.tap_sound, 0);
        }

        // --- 3. MISS判定 (スルーしたノーツの処理) ---
        for(auto& note: ctx.notes){
            if(note.lane < 0) continue;

            if(note.isHit || note.isHolding) continue;

            if(ctx.musicTime > note.targetTime && (ctx.musicTime - note.targetTime > 200)){
                ctx.laneJudge[note.lane]  = {tex.miss, sq.miss, ctx.musicTime};
                note.isHit = true;
                note.hitTime = ctx.musicTime;
                markLongFailed(note, note.targetTime);
                ctx.scoreTracker.registerJudge("MISS");

                ctx.isAP = ctx.isFC = false;
                if(note.type == NoteType::Lane) toggleLaneActive(ctx, note);
            }
        }


    // --- 4. ロングノーツの継続ホールド・終点判定 ---
    for(auto& ln : ctx.notes){
        if(ln.type == NoteType::Long && ln.isHolding){
            int i = ln.lane;
            int32_t endTime = ln.targetTime + ln.durationMs;

            bool isHoldingArea = false;
            if(ln.widthLanes > 1){
                int startLane = ln.lane;
                int endLane = ln.lane + ln.widthLanes - 1;
                for(int l = startLane; l <= endLane; l++){
                    if(laneDown(ctx, l)){
                        isHoldingArea = true;
                        break;
                    }
                }
            }
            else{
                isHoldingArea = laneDown(ctx, ln.lane);
            }

            // 押し直した：離していた時刻を消して、そのままホールドを続ける
            if(isHoldingArea) ln.releasedAtMs = INT32_MIN;

            // 途中で指を離した場合
            //   ・終点の直前（LONG_RELEASE_GRACE_MS 以内）：失敗にせず、終点まで流す（今まで通り）
            //   ・それより前：すぐには途切れない。離してから ln.releaseToleranceMs（BPM の 2 拍分）たっても
            //     押し直さなかったときだけ MISS にする。それまでに押し直せば、何もなかったように続く
            //     （離している間も、中継点のコンボは下でそのまま入る）
            if(!isHoldingArea && !ln.releaseGrace){
                if(endTime - ctx.musicTime <= LONG_RELEASE_GRACE_MS){
                    // 終点直前：失敗にせず、ホールドしている扱いのまま終点まで流す
                    // 判定と音は下の「終点に無事到達した場合」で、終点の時刻に出す
                    ln.releaseGrace = true;
                }
                else{
                    if(ln.releasedAtMs == INT32_MIN){
                        // 離し始めた時刻：別スレッドが離した瞬間を記録していれば、その時刻（フレームの時刻より正確）
                        int32_t rel = ctx.musicTime;
                        if(ctx.releaseTimes){
                            for(int l = std::max(0, ln.lane); l <= std::min(5, ln.lane + ln.widthLanes - 1); l++){
                                if(ctx.releaseTimes[l] != NO_PRESS_TIME) rel = std::min(rel, ctx.releaseTimes[l]);
                            }
                        }
                        ln.releasedAtMs = rel;
                    }

                    if(ctx.musicTime - ln.releasedAtMs > ln.releaseToleranceMs){
                        // 長く離しすぎた：MISS（残りの中継点はもらえない）
                        //   帯は「離し始めた時刻」の所から暗くする（どこで離したか分かるように）
                        ln.isHolding = false;
                        ln.isHit = true;
                        ln.hitTime = ctx.musicTime;
                        ctx.laneJudge[i] = {tex.miss, sq.miss, ctx.musicTime};
                        // ※ MISS のときは音を鳴らさない（前はノーツ音が鳴っていて、成功したように聞こえた）
                        ctx.scoreTracker.registerJudge("MISS");
                        markLongFailed(ln, ln.releasedAtMs);

                        ctx.isAP = ctx.isFC = false;
                        continue;
                    }
                }
            }

            // 中継点：決まった時刻を過ぎたら、押し続けている限りコンボを 1 ずつ足す
            //   ・時刻は譜面を読んだときに決めてあるので、押し始めが遅くても、
            //     その時刻までに過ぎた中継点はこのフレームでまとめて足される → 合計の数は変わらない
            //   ・表示は PERFECT を出すが、数えるのはコンボだけ（スコア・PERFECT の数は増えない）
            while(ln.nextTick < ln.tickTimes.size() && ctx.musicTime >= ln.tickTimes[ln.nextTick]){
                ln.nextTick++;
                ctx.scoreTracker.registerTick();
                ctx.comboPop.startTime = ctx.musicTime;
                ctx.laneJudge[i] = {tex.perfect, sq.perfect, ctx.musicTime};   // hasTiming = false → FAST/LATE は出ない
                spawnTickParticles(ctx, ln);
            }

            // 終点に無事到達した場合
            if(ctx.musicTime >= endTime){
                ln.isHolding = false;
                ln.isHit = true;
                ln.endHeld = isHoldingArea;   // 押したまま終点に来た → すぐ後につながったロングがあれば、押しっぱなしで取れる
                ln.hitTime = ctx.musicTime;   // 終点の判定の時刻：表示がここに来るまでホールドの見た目を続ける
                ctx.scoreTracker.registerJudge("PERFECT");
                ctx.scorePop.startTime = ctx.musicTime;
                ctx.comboPop.startTime = ctx.musicTime;

                ctx.laneJudge[i] = {tex.perfect, sq.perfect, ctx.musicTime};
                ctx.comboCount++;
                Mix_PlayChannel(-1, ctx.tap_sound, 0);
                spawnNoteCubes(ctx, ln);   // 終点
            }
        }
    }
}