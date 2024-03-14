#include "pch.h"

#include "CombatInteractionManager.h"

#include "StringHelper.h"


namespace comim
{
    double_t current;
    int32_t currentCombatBeat;
    int32_t combatBeatTempo;

    void init()
    {
        currentCombatBeat = 0;
        combatBeatTempo   = 24;  // Number of simulation ticks for one beat (40 ticks per second, 24 tempo: 100bpm)
    }

    void cleanup()
    {

    }

    void simulationTick()
    {
        // @TODO: move the combat interaction pieces into this file once the prototype is finished.



        // End tick.
        currentCombatBeat = (currentCombatBeat + 1) % combatBeatTempo;
    }

    int32_t getCurrentBeat()
    {
        return currentCombatBeat;
    }
    
    void hurtRequest(const std::string& guid, void (*onSuccess)(), void (*onGotParried)())
    {
        // @TODO: write a real hurt request approval system!!!
        onSuccess();
        onGotParried();  // For now just test all the input functions by calling all of them!
    }
}