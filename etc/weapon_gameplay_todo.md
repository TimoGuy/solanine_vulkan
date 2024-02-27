# Weapon attack art.


- [ ] Prototype/research gameplay.
    - [x] Does inputting button combos feel good in a set rhythm?
        > Yes. There needs to be a lot of wiggle room for input latency, and just bad sense of rhythm overall, but just going along with the "heartbeat" and doing the inputs feels really good.
    - [ ] Does doing button combos in sync w/ rhythm feel good when fighting another enemy?
        > @PRE_THOUGHTS: Having successful hits getting landed (either by you or by enemy) would feel strong. Also, getting both enemy and player to perform an attack on the same "beat" needs to feel good (I feel like this has the possibility of feeling off of the beat and not very good).
            > I feel like if only the timing were more tight it would be a lot less noticable.
            > @POSITIVE_THOUGHT!: I do feel like it would feel just as tight as the player inputting the attacks bc the game already knows what the enemy is going to do on X beat, so as soon as the game knows what the player is gonna do (input on the beat), then the "clang" of both player and enemy performing an attack together will be on a good beat and will probs feel good. If the player does the attack input and the game knows the enemy isn't going to attack, then it's immediate there too. If the enemy is attacking and the player doesn't input an attack, then it will have to be on the last tick of the `acceptableRange`, however, if the player isn't even charging for an attack (i.e. they don't have the attack button already held), then it's impossible to execute an attack and the game won't have to wait until the last tick of `acceptableRange`, and can simply submit the player getting hurt on the beat. That case will surely feel on-beat and strong.
                > @NOTE: all of this goes out the window if it becomes multiplayer pvp.
        - [ ] Have enemies do their sound effects (when not relying upon seeing if player is gonna give out an attack if it's possible to even do during the beat) on the downbeat where `tempo == 0`. That would move the tightening to be so much better.
            - [ ] MAYBE: could even just have all sonds, including the attack animation play out.
            - [ ] Ummmmm, I don't know what I wrote one above, but I was fighting sleep. So, have all enemies do their action and sound on the downbeat, but if you as the player can unleash an attack before the last 10 ticks of timing window, then the attack from the enemy is blocked by your sword!



- [ ] Rhythm ticking while sword is out.
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



- [ ] Attacking with rhythm.
    - [ ] .

- [ ] Weapon clashing with enemy.
    - [ ] Add attack hitboxes.
