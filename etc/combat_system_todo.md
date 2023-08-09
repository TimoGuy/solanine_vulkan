- [ ] Integrate the aura system into the combat.
    - [x] Decide how weapon materialization will work.
        - You can turn your materials into weapons, but once they're created, you can't put them back into the materials since there's durability and stuff.
        - Maybe put the created blades into your inventory? They wouldn't be able to stack if that's the case.
        - Left click to materialize, and right click to break off and place in inventory (drop in front of you if inventory is "full")
        - If you try to use the item again, you can't materialize another one, and you just pull out the already created one
        - You can go into your inventory to drop the created blade if you don't like a dulled/used blade or something like that.
    - [x] Add unsheath & materialize anim to asm
    - [ ] Add break off and sheath anim to asm

- [ ] Create simple tool to set the hitscan lines along with the animations of the hwac.

- [ ] Modify beanbag enemy to be affected by the slices

- [ ] Move to exiting leap forward (WazaAir2nd_LeapForwardRaimei) when hitting the ground.
    - [ ] Change the exit animation to be a single forward tumble that ends with skidding to a halt with feet.
- [ ] Change (WazaAir2nd_TwistingUpdraft) to not do landing animation until hitting the ground. If the landing animation never triggers, then you can't go to the next waza (WazaAir3rd_ShoulderLift)
- [ ] When doing weight shift slice, just do simple flip vertical slice if there is nothing to hit (air) or the thing being sliced is super easy to slice. It's only when the thing being sliced is super strong that the blade gets "stuck", which requires the weight shift. The weight shift adds double "poise" to the attack, and if even then that's not enough, then have the blade snap off (scratch idea of doing a button mash) and stay on the enemy.

### Bugs
- [ ] Landing on the ground while doing first waza and then not doing next one will trigger land animation after return-from-waza anim
- [ ] Doing first waza while on ground and holding movement direction, then after return-from-waza anim if still holding the movement direction then only idle anim will play while still running.