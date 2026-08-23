#pragma once

#include "GameCommon.h"
#include "Play/type.h"
#include "Play/GameContext.h"

inline void updateNoteSpeed(
    int32_t musicTime, 
    double noteSpeed, 
    double& currentNoteSpeed, 
    std::vector<SpeedEvent>& speedEvents
){
    double targetSpeedActive = noteSpeed;
    bool speedControlled = false;

    for(auto& ev : speedEvents){
        if(musicTime >= ev.triggerTime){
            if(!ev.isTriggered){
                ev.startSpeed = currentNoteSpeed;
                ev.isTriggered = true;
            }

            int32_t elapsedTime = musicTime - ev.triggerTime;

            if(elapsedTime >= ev.duration){
                targetSpeedActive = ev.targetspeed;
            }
            else{
                double t = static_cast<double>(elapsedTime) / ev.duration;
                if(ev.easing == 1){
                    targetSpeedActive = ev.startSpeed + (ev.targetspeed - ev.startSpeed) * t;
                }
                else if(ev.easing == 2){
                    targetSpeedActive = ev.startSpeed + (ev.targetspeed - ev.startSpeed) * easeOutCubic(t);
                }
                else if(ev.easing == 3){
                    targetSpeedActive = ev.startSpeed + (ev.targetspeed - ev.startSpeed) *easeInCubic(t);
                }
                else{
                    targetSpeedActive = ev.targetspeed;
                }
            }
            speedControlled = true;
        }
    }

    if(speedControlled){
        currentNoteSpeed = targetSpeedActive;
    }
    else{
        currentNoteSpeed = noteSpeed;
    }
}