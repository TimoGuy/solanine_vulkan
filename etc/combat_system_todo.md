- [x] Improve the combat animation and feel of it.
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
        - [x] Add "push entities in this cylinder this direction" param to hwac file for spinny spinny move.

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

- [x] Improve the combat (ending areas)
    - [x] Weight shift double slice: move velocity properly
    - [x] BUGFIX: fix character getting pushed into the ground with the first weight shift slice.
        - [x] Add param to hitscan rel position to "keep character in same spot, and use the `position` param as moving it relative from the hit character's position, not the hitting character", maybe just by adding a keyword "hit_char_origin".
	- [x] That worked, but then there was a separate issue that arose where it was impossible to tune what was the correct relative amount, so the variable was changed to an `ignore_y` tag that would assign all of the positions except the y axis (this would solve the challenge of keeping the hit char in the same 'position' without shoving it into the ground).
    - [x] Add "being pressed" animation
        - [x] Create the animation
        - [x] Create the state and the transitions to the animation
    > Ideally, adding something like "cancel waza when shift is released or when character touches the ground"-type params would be great for making sure that the waza won't get put out when the character is sitting on the ground on their head or something like that.

- [x] Reiterate the "idea" of combat.
    > This is an important "fix" I want to the combat:
    - [x] When facing an opponent and using the waza breathing pattern, the camera should be focusing on the nearest facing character that the player could be facing.
        - [x] That is, holding <shift> would cause the camera to show both the player and the focused character in frame with a slight angle, and try to keep both in the shot.
            - [x] Pressing and holding shift adds the nearest capsule as a camera targeting object.
            - [x] Move the camera at an angle, lerping the focus position based off the look direction and deltaposition (between character and opponent) dot product.
            - [x] As deltaposition (char and oppo) angle changes, move the orbitangle Y in the same way.
            - [x] Change the distance the camera zooms in/out depending on the distance of the deltaposition (char and oppo).
                - [x] Tune the params.
            - [x] Move the X orbitangle to face the opponent.
        - [x] Reigai: RESEARCH: figure out what is needed to make doing the waza play nice with this camera system.
            - [x] Try limiting changing the Y axis orbit angle when the X axis orbit angle is relatively flat (+-60deg)
                > This wasn't the best. It made the camera act weirdly, and after doing the waza, the camera wouldn't return back to what the user's original camera angle was.
            - [x] Try implementing the smoothdampangle function
                - [x] Make X and Y axes orbit angles use smooth damp.
                    > This is a really good change, honestly. It really makes the camera make sense of where it's tracking.
                - [x] Implement smoothdamp on XZ and Y focus positions.
                - [x] Continuously check for which side of the character is the best side when moving the camera around.
                    - [x] Have mouse input (x axis) change the Y orbit angle side offset.
                    > This really smooths out the camera movement and makes everything a lot more readable, especially when the char and the oppo change positions and the camera wants to "flip" to the other side. The mirrored alternate angle stops the camera from flipping things around and keeps the readability good by being on the same "side".
                - [x] Try making the speed of the smoothdampangle change depending on the distance away from the opponent (closer = slower change of Y orbit angle)
                    > This ended up being a pretty good thing. Didn't really affect much for me though.
                > All in all, the best thing to do was to not change the X orbit angle and rather keep it at 0deg, flat. Then, as the Y distance gets larger, zoom out the camera so that both subjects can still be seen easily. It takes away from the "shoot down" feel of the waza where the player character jumps off their weapon and rockets downward, but the speed of the jumpdown is accentuated very well with the zoomed out camera.

- [ ] Reiterate the "idea" of combat. PART II
    > The biggest goal is feeling like what you're inputting as actions is actually what you feel like you're doing.
    - [x] Divide up the individual wazas such that you can do all of them one at a time. (THOUGHT: I think that being able to do them all individually is more important than the chain FOR NOW... bc doing them all individually all feels like a different action that you have control over doing)
        - Here are the wazas (in the order of the "complete waza") and what I'm thinking to be the new button inputs (see `etc/waza/air_waza_plan.txt` for more info):
            1. Gust Wall
                > Grounded. Hold LMB, then release to do the swing down.
                > Hold, take out stamina over time. Swing, take a small amount of stamina (I think this one should be the least amount of stamina than the over moves).
            2. Updraft
                > Grounded. Hold LMB and spacebar at the same time, and player will put weapon at side and hunch down, ready to launch off. Release spacebar, launching player forward. While launched forward, release LMB where enemy is to trigger player to do a strong bottom to top slice that launches the opponent up into the air. Player automatically lands in a squat from the strong bodily twist, ready for Giant Leap if spacebar is pressed and released.
                > Hold (either spacebar or LMB) take out stamina over time (more when both). Jump, also swing, take a good amount of stamina (not as much as later moves).
            3. Giant leap (jump up really high)
                > Grounded. Double tap spacebar, and the second tap, hold spacebar to get into a squat, and release after half a second (or no time if landed in a squat from Updraft) to trigger player to do a giant leap upwards, Jump King style (but will have the ability to move while midair).
                > Hold, take out stamina over time. Jump, take out good amount of stamina.
            4. Eye of the Storm (Vacuum and vertical swing)
                > Midair. Double click LMB to get into the vacuum, then when the enemy is in front of the player (after like 1/3 second), click LMB to unleash the cocked back swing and "vacuum".
                > Stamina gets taken down for each click, but after click #2 is the vacuum, and stamina will tick down over time too.
            5. Flip over to upside down
                > Midair. Press spacebar.
                > No stamina.
            6. Downward dive
                > Upside-down. Double tap spacebar, and the second tap, hold spacebar for a qtr second (game will hold you in the same spot midair as you're holding spacebar), then release, triggering the super strong dive.
                - __NOTE__: there should be a "terminal velocity", where if player use this technique player actually surpasses the maximum negative y velocity for a bit, before it slowly goes back to "terminal velocity". Thus, if wanting to go down faster, would have to do another second Downward Dive (stamina doesn't refill until you're on the ground).
                > Hold, take stamina over time. On jump, take out a good amount of stamina.
            7. Spinny helicopter (Compression)
                > Upside-down. Hold LMB, then release to do the Spinny Helicopter.
                > During the holding period slowly tick down stamina over time. Upon release, take out a good amount of stamina.
            8. Flip from upside down to right side up
                > Upside down. Press spacebar.
                > No stamina.
            9. Derriere cut
                > Midair. Hold LMB for 1/3 second and then release, releasing the first cut. If it makes it thru, then that's it. If not, hold LMB again for 1/3 second to do weight shift and ready the second cut, and release to do the second swing thru. The blade will either snap off or go thru the enemy. Either way the animation is the same. The only difference is whether the blade comes with or not.
                > Hold, take out stamina over time. On first cut take out a good amount of stamina. Hold, take out stamina over time (more than previous). On second cut take out twice as much as the first cut's stamina.

        - THOUGHT: since diving off the weapon takes up resources, maybe making the player able to with the air waza use air itself as the kickboard to jump off downward in the dive move.
            - THOUGHT: also, I feel like a way to do the spinny helicopter move (since it's essentially a double jump) without having to do the downward dive jump would be useful.
                Maybe, first having the option where while midair the player could flip over upside down, and then in that state could either do helicopter OR downward dive OR go back to being right side up (essentially doing a front flip). You could do a couple different moves with this:
                - Flip over, helicopter.
                - Flip over, downward dive, helicopter.
                - Flip over, flip back (frontflip!!)
                - Flip over, downward dive, flip back (essentially superman landing where he is accelerating downward and then lands on his feet, crumbling the ground beneath him).
                - Flip over, hit the ground (NOTE: will always get hurt if you land on the ground upside down!!!)
                - Flip over, downward dive, hit the ground very hard (even more "fall damage"... more like stupid damage (OR!!!!!! Make this a way to defeat a certain enemy: flip them over so they land on their heads))
                - Flip over, helicopter, helicopter, helicopter, etc.
            - THOUGHT: I think that the helicopter move shouldn't automatically move the player to right side up (I think that's already the case perhaps).
        
    - [F] Implement the new air waza design.
        - [x] Rewrite the .hwac file
        - [x] Implement the `grounded` attacks
            - [x] GustWall(S)
            - [x] Animations:
                - [x] LeapForward(S)  (@NOTE: this will be Updraft but without holding X... so essentially if starting to hold X while starting this, then can switch to Updraft)
                    - [x] Hold
                    - [x] S_01
                    - [x] 01
                - [x] HighJump
                    - [x] Hold
                    - [x] 01
                - [x] Updraft(S)
                    - [x] Hold
                    - [x] S_01
                    - [x] 01
                    - [x] 02
            - [x] Put the animations into the hwac and set velocities and whatnot.
            - [x] Fix the hitscan baker.
        - [ ] Make aura be something that happens while you're performing wazas after a hold waza, so doing a hold waza starts a combo, then keeping up wazas will keep the aura going.
            - [ ] After around 2 seconds of idling, the aura wears off.
        > FROM HERE ON, DO IN THE FUTURE!!!!!
        - [ ] Let user slowly move during midair times.
        - [ ] Implement flipping over.
            - [ ] Make character take damage when hitting the ground while flipped over.
        - [ ] Create `barrier` that doesn't step forward time in the waza past the indicated tick number (inclusive).
            - [ ] Modify `chain` to be able to select where in a state to start other than 0 (use square brackets).

- [ ] Create simple tool to set the hitscan lines along with the animations of the hwac.
    - [ ] Checkbox for "allow movement" with the certain waza.
        - [ ] Preview for different lower body animations with the waza if movement is allowed.

- [ ] Move to exiting leap forward (WazaAir2nd_LeapForwardRaimei) when hitting the ground.
    - [ ] Change the exit animation to be a single forward tumble that ends with skidding to a halt with feet.
- [ ] Change (WazaAir2nd_TwistingUpdraft) to not do landing animation until hitting the ground. If the landing animation never triggers, then you can't go to the next waza (WazaAir3rd_ShoulderLift)
- [ ] When doing weight shift slice, just do simple flip vertical slice if there is nothing to hit (air) or the thing being sliced is super easy to slice. It's only when the thing being sliced is super strong that the blade gets "stuck", which requires the weight shift. The weight shift adds double "poise" to the attack, and if even then that's not enough, then have the blade snap off (scratch idea of doing a button mash) and stay on the enemy.
    > @NOTE: if poise isn't enough for any attack, then the attack won't go thru, and the character will poink and get knocked back from the lack of poise.

### Bugs
- [x] HIGH PRIORITY: Nvidia gpu doesn't do the z prepass correctly. Add a renderpass (with colorattachment of 0) to keep multiple writes on the z buffer.
    - [x] HIGH PRIORITY: turns out this fix didn't fix the issue. I realize that the issue is that the z buffer getting written and the next subpass occurring will sometimes get an animator update before this, causing the bone transformations to be different slightly and then the animated meshes can't render. DOD: get the animators to insert the data into a `currentFrame`-based data structure.
    > NOTE: this provided a big speed increase (400fps to 460fps)... probably bc the gpu is being scheduled properly now.
- [ ] Landing on the ground while doing first waza and then not doing next one will trigger land animation after return-from-waza anim
- [ ] Doing first waza while on ground and holding movement direction, then after return-from-waza anim if still holding the movement direction then only idle anim will play while still running.
- [ ] Random bug where I got "ERROR: physics engine is running too slowly", and then it just segfaults. I wasn't running with the debugger at the time so idk what the error is.
    > I think this is a duplicate of the segfault when defeating a character. I was hammering the npc character for a long time when it just segfaulted.
- [ ] Bug where when falling at high speed in very slow motion, then returning to 1.0 timescale will cause character to tunnel thru floor.
- [ ] Upon defeating a character, the destroy command breaks the program.
- [ ] Hotswap resource checker crashes when a file gets deleted (i.e. if you started the game with a .swp file from having the file loaded in vim and then you do `:wq`)
