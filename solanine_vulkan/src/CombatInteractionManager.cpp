#include "pch.h"

#include "CombatInteractionManager.h"

#include "StringHelper.h"


namespace comim
{
    int32_t currentCombatBeat;
    int32_t combatBeatTempo;

    // How many ticks a parry can be late from the attack unleash and still be
    // considered valid.
    // 5 feels way too late.
    // 2 feels really nice.
    // 0 feels tight but feels like it eats my inputs.
    // 1 is a tradeoff, but it really honestly just feels like Lies of P, so it's fine I suppose.
    //   It's really interesting how it changes when the ticks in the simulation system get changed
    //   to 50hz, though. It feels so much less forgiving than working in 40hz, so at least for combat
    //   inputs, it really should be kept at 40hz. I wonder if physics would need to speed up for that
    //   with something like combat hitbox sensing. I wonder if doing async for everything and have it
    //   running at different rates would work okay for it all if that were the case.
    //     -Timo 2024/03/13
    int32_t parryFudgeTicks;

    void init()
    {
        currentCombatBeat = 0;
        combatBeatTempo   = 24;  // Number of simulation ticks for one beat (40 ticks per second, 24 tempo: 100bpm)
        parryFudgeTicks   = 1;
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
    
    void hurtRequest(const std::string& guid, JPH::SubShapeID subShapeId, void (*onSuccess)(), void (*onGotParried)())
    {
        // @TODO: write a real hurt request approval system!!!
        onSuccess();
        onGotParried();  // For now just test all the input functions by calling all of them!
    }
}