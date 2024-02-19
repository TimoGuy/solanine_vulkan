# Weapon attack art.

- [ ] Rhythm ticking while sword is out.
    > This rhythm is used to help with timing attacks for yourself. It shows up as a pulsing decal on the floor. Most attacks will require holding the attack button for one beat in the metronome (just have default be 120bpm for now). During this one beat, the entity places themselves into a stance to attack, and they don't change the direction of their move. Right when the next pulse clicks they release the stance, unleashing an attack (params: whether the attack was released too fast or too slow | the direction they're pushing the input joystick, since that will change what move they do next... the move ends in the direction they were pushing though).
    > @NOTE: the same weapon types will generally have the same range of their bpm, but the bpm of the attacking rhythm will change depending on a stat in the weapon. The enemies will definitely vary slightly too. Be sure to pay attention to their attack rhythm too and maybe you could awaseru yours with theirs! If you do that, then swords will clang for sure.
    > @NOTE2: the battle rhythm is definitely going on at all times. There isn't a way to change it. When an entity draws their sword, the draw is slow or fast in order to land on the next beat to initiate an attack. If the entity decides to start running off-rhythm, they will stumble for a sec. If they start their stance off-beat, then they will get a weak stance. 
    > @NOTE3: some special hidden moves will require starting on an offbeat or doing something for an offset, or doing something over a triplet. This will definitely require good rhythm sense.
    > @NOTE4: QUESTION: so what happens when an enemy and a player are fighting and their weapon types are different, and their rhythms are different? There will not be clanging in those cases???
        > @NOTE5: REPLY: It seems like there can be some fudging with something like a 0.2s window to allow for multiple attacks to be clanged off. There are times where the attacks don't line up, and in those cases, having to take the hit or one of the parties realizing that they need to dodge and dodging.
        > @NOTE6: Enemy also have weapon durability, which means you can break their weapon, and you as the player can't take their weapon bc of your contract with your ancient weapon. It has to be copied then materialized to be able to be used by player.

    - [ ] 

- [ ] 
