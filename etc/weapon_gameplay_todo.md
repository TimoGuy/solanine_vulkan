# Weapon attack art.


- [x] Prototype/research gameplay.
    - [x] Does inputting button combos feel good in a set rhythm?
        > Yes. There needs to be a lot of wiggle room for input latency, and just bad sense of rhythm overall, but just going along with the "heartbeat" and doing the inputs feels really good.
    - [x] Does doing button combos in sync w/ rhythm feel good when fighting another enemy?
        > @PRE_THOUGHTS: Having successful hits getting landed (either by you or by enemy) would feel strong. Also, getting both enemy and player to perform an attack on the same "beat" needs to feel good (I feel like this has the possibility of feeling off of the beat and not very good).
            > I feel like if only the timing were more tight it would be a lot less noticable.
            > @POSITIVE_THOUGHT!: I do feel like it would feel just as tight as the player inputting the attacks bc the game already knows what the enemy is going to do on X beat, so as soon as the game knows what the player is gonna do (input on the beat), then the "clang" of both player and enemy performing an attack together will be on a good beat and will probs feel good. If the player does the attack input and the game knows the enemy isn't going to attack, then it's immediate there too. If the enemy is attacking and the player doesn't input an attack, then it will have to be on the last tick of the `acceptableRange`, however, if the player isn't even charging for an attack (i.e. they don't have the attack button already held), then it's impossible to execute an attack and the game won't have to wait until the last tick of `acceptableRange`, and can simply submit the player getting hurt on the beat. That case will surely feel on-beat and strong.
                > @NOTE: all of this goes out the window if it becomes multiplayer pvp.
        - [x] Have enemies do their sound effects (when not relying upon seeing if player is gonna give out an attack if it's possible to even do during the beat) on the downbeat where `tempo == 0`. That would move the tightening to be so much better.
            - [ ] ~~MAYBE: could even just have all sonds, including the attack animation play out.~~
            - [x] Ummmmm, I don't know what I wrote one above, but I was fighting sleep. So, have all enemies do their action and sound on the downbeat, but if you as the player can unleash an attack before the last 10 ticks of timing window, then the attack from the enemy is blocked by your sword!
                > This feels a lot lot better!
        - [x] Try allowing player to start charging their weapon if they're already holding the attack button on the first frame of the input window.
            > It felt like my input was getting eaten.
        
        > CONCLUSION: I feel like they do feel good, however, I don't think the player should be required to follow the rhythm to attack.
        >     Instead of being forced to follow the rhythm, have the "breathing technique" attacks be contingent upon good sense of rhythm with charging the attacks and releasing them. It increases poise, attack, defense, and it looks flashy too.
        >     With being able to start an attack charge and release it at any time, this would feel very responsive. Then, if an attack is coming, the player can at any time block the incoming attack if they release attack at the right time.
        >     With all this, I still want the player to be required to press and release the input in a certain rhythm for doing a "breathing technique" attack. This will be a learned thing from discovering learning rooms, instructors, etc.


- [ ] Plan the new approach and what combat will now look like!
    > It could be based off of Sekiro, though I haven't played the game. 
    > I would like some more variety like if the enemy is blocking with their sword horizontal, doing a vertical slice snaps their sword in half; their sword vertical, doing a horizontal slice knocks the sword out of their hands and they run after it.
    > ~~There is no blocking, just attacking at the same time as an enemy to "parry".~~
    > Enemies should be able to follow a visual rhythm of attack the player can dance with with their different moves.


- [ ] New combat approach from prototype.
    > I haven't played Sekiro, but the combat reference (https://www.ncbi.nlm.nih.gov/pmc/articles/PMC9648270/) is very informative and kinda completes the puzzle for how I want to design the combat system.
    > Other inspo: https://www.youtube.com/watch?v=7A0hj5IdF4A https://www.youtube.com/watch?v=7W4li9yfY3o

    > The basic idea of the system of Sekiro's combat:
        - Posture is spent while receiving attacks and it is dangerous to lose.
        - Losing all posture puts one into a critical state where they cannot stand and will likely receive a critical blow from their opponent.
        - Guarding against an attack spends posture.
        - Getting hurt from an attack spends posture.
        - Parrying does not spend posture.
        - Posture slowly regains if it has been awhile since one lost some posture.
        - There is an aggressive/offensive playstyle and defensive/passive playstyle that cause enemies to react in different ways.

    > Things I want to add/revise:
        - If both parties in duel clang swords together (both attack at same time), they knock back apart and both lose posture.
        - If one party is guarding, the other can do a heavy attack (two beat hold) perpendicular to the direction the sword is facing to snap the sword in half.
        - With all enemies and NPCs, there should be a clear, global metronomic rhythm (@120bpm) such that all these entities are synced up with attack rhythm at all times.
            - Player will use this to their advantage in attacking and parrying.
            - Not all enemies will always attack on the downbeat either. Some will attack in various rhythmic patterns, or use offbeats.

    - [ ] Player combat.
        - [x] Tuning:
            - [x] Make attack always fall on a certain point in the beat. Usu. the downbeat.
                > THOUGHTS: This actually feels really good. You can start the weapon charge anytime, but the release is always going to land on the downbeat (or a little bit after to compensate for input delay).

        - [x] Attack.
            > There's a weapon-charging state when holding the attack button (LMB), then the moment the attack button is let go of the attack is unleashed.
            > If attack an enemy at the same time they attack, both player and enemy will get knocked back by a large clang, and posture gets lost by both people.
                > Higher level enemies will know to parry the player's attack, however.

        - [ ] Guard.
            > Holding the block button (RMB) brings the player into a block state. Player will double their defense in this position for the majority of attack types. Receiving an attack while guarding loses some posture.
            > If player started an attack, at the beginning of the attack they can switch to doing a block, else no way to cancel the attack animation.

        - [x] Parry.
            > Tapping the block button (RMB) at the same time as an enemy is attacking allows player to protect themselves from an attack without losing any posture, moreso causing the enemy to lose a bit of posture.

    - [x] Enemy combat.
        - [x] Rhythm based timer.

    - [x] Combat interaction.
        - [x] Parry tuning.
            - [x] Would wanna have good fudging so that player doesn't have to try to overcompensate and parry earlier than wanted.



- NEW APPROACH/REFACTORING: interaction figured out.
    - ~~Create capsule-triangle overlap algorithm (for sword path and opponent collision).~~
        > ~~Well, it seems like doing a bunch of OBB to capsule collisions is what will be the ticket. The issue will just be figuring out how to represent the collision with the OBB.~~

    - Create a bunch of capsule overlap actions.
        - Have the traversal of the slice happen over the 3 ticks @40hz (0.075ms), so about 2 24fps frames (0.0833ms).
            > 3 ticks isn't nearly enough to have good hitbox coverage, so precompute all the necessary hitbox capsules, then just test all of them to see if they connect with any enemies.

    - ANALYIS OF SEKIRO.
        - Stealth sneak into battle. Get rid of first enemies that don't spot you.
        - Enemies see you performing stealth kill, battle music starts and no more can get stealth killed.
        - Whittle at the enemies' posture meter and kill them.
            - Slice, parry, and use any shinobi tactics to accomplish this.
        
        - THOUGHTS: it's a dance during the meat of the battle. In the beginning, you're always watching and listening for where enemies are/could be and reacting to that. During battle too, you're always reacting to enemies and aggressively defending and offending until the posture meter is filled.

        - LIKEN TO MY GAME: the dance with the enemies is essential and with it the parry/guard/attack aggressive play style I really want. I want to include breathing techniques. This would replace the shinobi-like stealth & posture break kill actions. Instead, the player would (due to it requiring time and precise timing) stand afar and jump into action with a breath technique, then upon breaking the posture of the enemy, can do another breathing technique. Ultimately the player can do a breathing technique at any time, however, the risk is higher vulnerability (i.e. will take more damage and more posture if hit), with the tradeoff of higher offensive power. With mastery of the breathing techniques, the player could just use them instead of the normal offensive attacks.
            > One breathing technique is usually a short set pattern of inputs and timings of R1 that equate to the attack pattern sequence. You have to have the right weapon type to do the actual attack sequence, and if you're the right transformation, you get KNY-like elemental art with the breathing technique. So you have to plan what blades you'll have, and what terrain/transformation you'll be during the battle. If not, performing the attack sequence without the right transformation does not give advantage (i.e., with the right blade, transformation, and breathing technique, an attack can just wipe out a whole health bar of an opponent (mini-bosses and bosses will have 2+, but regular enemies will have 1)).
            > And what if you run off as the player, to do a breath technique? If the enemies see you running off, they'll have some kind of projectile weapon (shuriken or something) that they can throw at you which you can parry away or jump away from, but likely it'll hit you and it will cause a lot of damage, esp. if hit in the back.

    - 2024/03/10 UPDATE --------------------------------------------------------
    
    - Only special breathing technique moves will use the heartbeat.
        - It feels really "non-reaction timely" when every attack has to land on a certain part of the beat. Something slow like a heavy two-handed sword should be good to do heavy attacks, but something light like a one-handed sword should be able to have immediate attacks.
            - @NOTE: okay after doing some research, there are techniques to repeately swing a two-handed heavy sword, but the first charge up is the longest/heaviest.
        
        - Breathing techniques are in-line with the global timer (that enemies and npcs are based off of, and the heartbeat sfx are based off of), so holding the attack button for 1-2 beats (depending on where the beats fall once player starts hearing heartbeats).


- [ ] TASKS (@NOTE: all timings are with 40 ticks/sec simulation pace)
    - [x] Separate the enemy into separate sim char.
        - [x] Has model (just use same slimegirl model).

        - [x] Has idle, weapon chargeup, and release animations.

        - [x] Some enemy behavior in a certain pattern (@NOTE: no actual enemy attacks until combat manager and hit-/hurt-capsules are created) as a state machine w/ animations.

    - [ ] Create hit-/hurt-capsules for entities in combat (incl. npc's).  @@@@@TODO: DO THIS ONE NEXT!!!!!!
        - [x] Better capsule debug rendering. (i.e. capsules that can be any arbitrary direction)
        - [ ] Hurt-capsules for sword.
        - [ ] Hit-capsules for player character.
            - @NOTE: for first enemy, just reuse player character model and attack sequences.
            - @REF: https://www.youtube.com/watch?v=8zdbqTHtnr4
            - [x] Have each hit-capsule be assigned a bone and some offset of the bone.
            - [ ] Improve the look of them, bc they look off. Also, figure out how you're gonna do this bc it looks like mutating the shape is pretty heavy for the physics engine.
                - Try seeing if batching the mutate step into `mutateshapes()` instead of `mutateshape()` would be better.
                - @NOTE: okay, so the reason why it was looking off is mainly bc the bone offset was 0, but there was height on the capsules, so the capsules were centered around the joints, instead of sprawling the joints. Also, I think the other reason why it doesn't look right is bc the model is scaled by 0.3 by the sim char, but that scaling isn't applied to the hit capsule shapes.
                    - [ ] Scale slime girl model down by 0.3 (or the amt that the game is scaling the model down).
                - [x] Align the body transform to the character.
                - [ ] Align body to character using simTransformId.

    - [ ] Combat manager class.
        - [ ] Each entity wanting to engage in combat (player, enemies, npcs) will register themselves into the combat manager class.

        - [ ] For each collision with a hurt-capsule and a hit-capsule, submit a "hurt request"
            - @NOTE: after some research (i.e. copying Lies of P), there will be a 1 frame response time for a parry or another attack to come in and match up with it (pretty unlikely, but still possible. @THOUGHT: fudge it so that if enemy attack will happen sometime soon after player attack, make it so both attacks happened the same time and the big damage to both player and enemy weapon durability happens)

            - @TODO: figure out how "posture" goes into these different interactions.

            - [ ] If there is a parry that occurs within the time frame (12 frames of parry effect, can happen 1 frame after the "hurt request" is submitted), then do "parry interrupt".

            - [ ] If an enemy attack is about to happen (XXX frames after player-to-enemy "hurt request" was submitted) (not a parry), then do "weapon durability interrupt".

            - [ ] If "hurt request" is approved, then send callback to party2 (uke) for defense and hurt calculations, then send callback to party1 (seme) for weapon durability. If party2 (uke) is in "guard mode" (i.e. still holding guard after parry window is over), then the damage that would've been applied to the party's HP goes to the party's weapon durability instead.

            - [ ] If "hurt request" comes out with "weapon durability interrupt", then send callback to both parties to subtract weapon durability from opposing party's attack power (i.e. clashing with a heavy attack is better, since you inflict more durability damage to enemies)

            - [ ] If "hurt request" comes out with "parry interrupt", then send callback to seme for getting parried (i.e. bigger posture penalty and weapon durability penalty), and callback to uke for parrying (i.e. smaller posture penalty).

            - @NOTE: unless if party has a certain level of poise (only >=mini-bosses would, and even then only the big, burly ones), then all approved "hurt requests" onto the uke will interrupt them and do a short stagger.

    - [ ] NOW YOU ARE FREE TO LOOK AT THE PROGRESS. I HONESTLY FEEL LIKE YOU (timo) LOOK INTO DETAILS AND RANDOM FACTS THAT ANSWER QUESTIONS OF SUPER LOW-LEVEL DETAILS YOU DON'T NEED TO KNOW THE ANSWER TO AT THE TIME, IF EVER. SEEK TO MAKE SOMETHING MAGICAL, BEAUTIFUL, FUN, AND ENTERTAINING.



- [d] ~~Rhythm ticking while sword is out.~~ @NOTE: since the rhythm is going to be internalized into the player itself, the player will decide the rhythm that they attack in. If it's the right tempo for "breathing techniques", then a flair will show up along with the attacks, signifying better attack, defense, poise, etc.
    > This rhythm is used to help with timing attacks for yourself. It shows up as a pulsing decal on the floor. Most attacks will require holding the attack button for one beat in the metronome (just have default be 120bpm for now). During this one beat, the entity places themselves into a stance to attack, and they don't change the direction of their move. Right when the next pulse clicks they release the stance, unleashing an attack (params: whether the attack was released too fast or too slow | the direction they're pushing the input joystick, since that will change what move they do next... the move ends in the direction they were pushing though).
    > @NOTE: the same weapon types will generally have the same range of their bpm, but the bpm of the attacking rhythm will change depending on a stat in the weapon. The enemies will definitely vary slightly too. Be sure to pay attention to their attack rhythm too and maybe you could awaseru yours with theirs! If you do that, then swords will clang for sure.
    > @NOTE2: the battle rhythm is definitely going on at all times. There isn't a way to change it. When an entity draws their sword, the draw is slow or fast in order to land on the next beat to initiate an attack. If the entity decides to start running off-rhythm, they will stumble for a sec. If they start their stance off-beat, then they will get a weak stance. 
    > @NOTE3: some special hidden moves will require starting on an offbeat or doing something for an offset, or doing something over a triplet. This will definitely require good rhythm sense.
    > @NOTE4: QUESTION: so what happens when an enemy and a player are fighting and their weapon types are different, and their rhythms are different? There will not be clanging in those cases???
        > @NOTE5: REPLY: It seems like there can be some fudging with something like a 0.2s window to allow for multiple attacks to be clanged off. There are times where the attacks don't line up, and in those cases, having to take the hit or one of the parties realizing that they need to dodge and dodging.
        > @NOTE6: Enemy also have weapon durability, which means you can break their weapon, and you as the player can't take their weapon bc of your contract with your ancient weapon. It has to be copied then materialized to be able to be used by player.

    - [ ] Create pulse with visual and audio.
        - [x] FOR NOW: "Chirp" can be created with just a simple animated transparent sphere.
        - [x] Create the shader.
        - [ ] ~~Fix events not running @0.0 when looping animation.~~
            > I think this is actually supposed to be the case?
        - [x] Add chirp sfx.
        - [ ] ~~Add cutout shader support (add some ZPrepass shader override).~~
            > The current ZPrepass implementation will likely be the only zprepass implementation for a shader. The best thing you can do is to add a transparency renderpass so that particle effects etc. can be implemented.
        - [ ] Add transparency renderpass.
            - [ ] ~~Do ZPrepass for that too.~~
                > The primary reason why I want a transparency mode is so that I can get water, ice, and particles rendered. Water and ice would definitely be included in some zprepass, but not particles.
            - [ ] `transparency_renderpass_todo.md` (Just particles)

