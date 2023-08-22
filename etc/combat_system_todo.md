- [ ] Improve the combat animation and feel of it.
    - [x] Decide how weapon materialization will work.
        - You can turn your materials into weapons, but once they're created, you can't put them back into the materials since there's durability and stuff.
        - Maybe put the created blades into your inventory? They wouldn't be able to stack if that's the case.
        - Left click to materialize, and right click to break off and place in inventory (drop in front of you if inventory is "full")
        - If you try to use the item again, you can't materialize another one, and you just pull out the already created one
        - You can go into your inventory to drop the created blade if you don't like a dulled/used blade or something like that.
    - [x] Add unsheath & materialize anim to asm
    - [x] Add break off and sheath anim to asm
    - [ ] Modify beanbag enemy to be affected by the slices
        - [x] Create entity that holds a model, loads the animations, and writes hitscans by selecting a bone and setting the length of the start and end of the blade, and you can select from which point in time physics wise to do the attack and it will write hitscan vec3s in a textbox for you. And it will draw the hitscan lines for you so you can easily see them.
            - [x] This could just piggyback off of the player for the time being.
            > The player entity could just turn into an actor and every enemy and npc could be based off of it too I guess. That would help a lot with a lot of code duplication. This may not even need to be its own separate entity ever!
            > So if we're just gonna piggyback off the player, have a checkbox that's like "hwac testing mode".
            - [x] Be able to scrub thru animation.
            - [x] Draw line of "Hand Attachment" with the weapon's blade start/end stats.
            - [x] Write out all hitscans in textarea for copying into hwac.
            - [x] Draw lines of all hitscan lines, with a different color of the current tick's hitscan.
            > NOTE: I wrote down doing the velocity tracking, but it could step on top of the hitscan line stuff, and felt like it wasn't the main focus of this (for now) small tool, so I removed it.  -Timo 2023/08/10
        - [x] Fix bug where short circuit searching for entity to hit if hit self with hitscans.
        - [x] Fix bug where hitscan lines aren't following velocity.
            - [x] Change hitscan lines to save the previous line in world space as the previous flow node positions.
        - [x] Add velocity param to hwac file to set what velocity enemy would go (non-normalized) if it touched the slice/hitscan.
            - [x] Write a velocity and move capsule system..... but maybe might wanna consolidate it with the player... and have them be the same thing.
            - [x] Fix velocity reaction.
                - Jump only does one frame.
                - Some weird behavior with XZ movement too.
            - [x] Improve velocity with a knockback recovery timer.
                - When the knockback timer depletes, then the character will "wake" up.
                    - This is of course if the character has the ability to wake up. Parametrize this in the future (idea: depending on the amount of health the player has)
                - When the character has woken up, the primary objective is to get to a stop. Grounded ukemi friction is used here.
            - [x] Allow picking bone where waza hitscans are baked from.
            - [x] Allow immediate usage of .hwac file by hotloading.
            - [x] MAYBE: have the character that's caught in the waza hitscan to have their position be set relative to the attacker's position and facingdirection.
                - The problem I'm trying to solve is the issue with the twisting updraft waza not placing the attacked character in the right spot to do the next attack sometimes.
                    - It seems like it's influenced by just where in the attack did the attacked character make contact with the hitscan.
                    - Though, now just writing it out rn, I think the theory above is incorrect, since the attacked character just gets a velocity applied to it.
                    - I think, it's a timing issue! Different times in the animation/waza that the attacked character gets attacked will change where both characters will land.
                        - Hmmm, well, it's probably a mix of both. Try fixing the positioning issue first and see if that resolves it!
			- REPLY: the position getting set upon getting hit tightens it up so much that the animation where the character gets hit doesn't seem to make any difference. The problem is solved!(TM)
        - [x] Add a "suck in nearby entities @ point x,x,x" param to hwac file for WazaAir4th_VacuumCockBack.
        - [ ] Add "push entities in this cylinder this direction" param to hwac file for spinny spinny move.

- [x] Attempt to reduce memory usage on gpu by reducing overdraw
    > Since both steam deck and laptop have a hard time running with the textures close up with memory switch hitches, it's likely shader memory usage.
    - [x] Test that the hypothesis is correct by setting all pbr materials to white without reading texture.
        - What I discovered was causing the high gpu usage was not the pbr materials (only really used 10ms or so), but rather that the skybox (raymarched atmospheric scattering) was eating up 100ms. Also, the shadow passes could be toned down, though they didn't eat up as much as I had thought. Changing the shadows from 4k to 1024 made the biggest difference.
    - [x] Change skybox to use the skybox texture instead of recomputing raymarched atmospheric scattering every frame.
        - I think in the future reducing the size to 512x512 for realtime atmospheric scattering every frame would be good but not now.
	- I think I discovered that most of the slowness was bc of the shadows, however, this fix did shave off around 10ms on the igpu.
    - [x] Test hypothesis of overdraw from pbr materials causing slowdown. I'm not sure how to test other than just acually implementing the z prepass test.
    - [x] Do a z prepass for opaque pbr materials
    - [x] Set opaque pbr material pipeline to zequal depth test. NOTE: this should be fine to be on the same renderpass and framebuffer.
        > NOTE: this reduced frame times from 35-40ms to 23-27ms (igpu laptop). THOUGH: the zprepass needs to test for discard alpha pixels like pbr_khr.frag does.
    - [x] Add discard alpha pixels to frag z prepass.
    - [x] Check that the albedo texture is cutout rendered with the alpha before sampling texture.
    	> NOTE: this improved performance by 2-3ms

- [ ] Reiterate the "idea" of combat.
    > The biggest goal is feeling like what you're inputting as actions is actually what you feel like you're doing.
    - [ ] Divide up the individual wazas such that you can do all of them one at a time. (THOUGHT: I think that being able to do them all individually is more important than the chain FOR NOW... bc doing them all individually all feels like a different action that you have control over doing)
    - [ ] Require holding shift to do the aura-based wazas.
        - [ ] Change the long wazas to have -1 duration so that it stays active until letting go of shift (or with something like WazaAir2nd_LeapForwardRaimei, it will continue until letting go of shift or landing on the ground)

- [ ] Create simple tool to set the hitscan lines along with the animations of the hwac.
    - [ ] Checkbox for "allow movement" with the certain waza.
        - [ ] Preview for different lower body animations with the waza if movement is allowed.

- [ ] Move to exiting leap forward (WazaAir2nd_LeapForwardRaimei) when hitting the ground.
    - [ ] Change the exit animation to be a single forward tumble that ends with skidding to a halt with feet.
- [ ] Change (WazaAir2nd_TwistingUpdraft) to not do landing animation until hitting the ground. If the landing animation never triggers, then you can't go to the next waza (WazaAir3rd_ShoulderLift)
- [ ] When doing weight shift slice, just do simple flip vertical slice if there is nothing to hit (air) or the thing being sliced is super easy to slice. It's only when the thing being sliced is super strong that the blade gets "stuck", which requires the weight shift. The weight shift adds double "poise" to the attack, and if even then that's not enough, then have the blade snap off (scratch idea of doing a button mash) and stay on the enemy.

### Bugs
- [ ] Landing on the ground while doing first waza and then not doing next one will trigger land animation after return-from-waza anim
- [ ] Doing first waza while on ground and holding movement direction, then after return-from-waza anim if still holding the movement direction then only idle anim will play while still running.
- [ ] Random bug where I got "ERROR: physics engine is running too slowly", and then it just segfaults. I wasn't running with the debugger at the time so idk what the error is.
