> CONTEXT: the gondolas should be the primary "fast-travel" in the game. They're trains where the track materializes right underneath them, and then disappears after the train like atoms disintegrating. This way there is no collision for the rails and it just makes the gondola just be essentially a floating platform that can be as fast as possible. When an entity moves into a track that is done, the particles will float away.

- [ ] Change physics to Jolt Physics
    - [ ] Wowee, really, really think about what you're doing first. Is this really what you want????? Sign below if you really want it, with the date.
        - x____________
    - [x] Basic API implementation
        - [x] Create shape initializers. (sphere, box, capsule, mesh)
        - [x] Create voxel field to compound collider (using multiple boxes and creating stair set pieces and scale and stuff).
            - [x] For now just do the boxes voxels (and make it greedy grouping!!!!)
        - [x] Create Rigidbody Character Controller
            - [x] Get basic movement down
            - [x] Get bug: XZ friction doesn't match up anywhere near to what is going on.
            - [x] Make all bottom collision flat (maybe... just prevent from sliding down steep slopes).
                > Walls feel tacky (not sticky), but 0.5 fricion smoothed over pretty much everything. Having an animation for "pushing against something but against the wall midair" could be good.
            - [u] Get bug: XZ Friction isn't even in the two axes.
                > This is Jolt Physics' fault, unfortunately. The friction system is not as sophisticated as wanted. Will have to just deal with it I think.  -Timo 2023/10/01
                > @REPLY: Maybe it might be worth looking into why the friction works this way, but for now, this will be marked `u` for "unfixable"
            - [x] Get bug: XZ doesn't slow down when holding a movement direction and then going into a waza.
                > From setting a friction, this now seems to be resolved, however, See `u` todo above: XZ Friction isn't even.
                    > In the samples, friction was calculated artificially by having XZ velocity decay in different rates depending on whether the character is midair or grounded. This system could work, and it works thus far for the normal movement.
                    > @UPDATE: so after slowing down the update for the samples Character controller, it appears that the same issue is present. This likely isn't something that's easy to fix.
                        > The idea I have is to simply add `0.5f * deltaTime * sign(axis)` to each axis, but this feels EXTREMELY HACKY  -Timo 2023/10/01
        - [x] BUG: rigidbodies are not deleted upon entity delete.
        > FUTURE FROM HERE. -----
        - [ ] Change `wazaVelocityDecay` to be 0.25 in midair always, and 0.5 grounded always, unless if there is an override.
        - [ ] Create add body into world system. (how: load a level and allow all the constructors, which should include rigidbody create to run, then have all of them batch into a list where at the end of the step it will add in all the rigidbodies)
        - [ ] Create renderer. (or not...)
        - [ ] Create recording system (refer to samples).
    - [x] Get movement down with the ~~rigidbody~~ ~~virtual~~ character controller.
        > ~~After trying out the samples, this type of character controller is what I am looking for. It handles slopes and stairs well and moving platforms, while also being fairly good at being an interactor within the simulation world.~~
        > After finding out that two virtual character controllers don't interact with each other, it felt like using the rigidbody (normal character) controller was the best thing to do. It has a tacky (aka somewhere between smooth and sticky) feel when brushing up against walls, and it can push stuff and interact with other rigidbody characters.
    - [x] Get moving platform movement down with the ~~rigidbody~~ ~~virtual~~ char controller.
- [ ] Create path system for gondola to show up at a station.
    - [x] Create Gondola collision info as a voxel field.
    - [ ] Figure out way to load it into the gondola system object.
    - [ ] Gondola accelerates along bezier curve towards a max speed.
    - [ ] Copy design of station from Unity Concept Prototype (have station that merely displays how many meters away the next gondola is)
    - [ ] Next in line gondola decelerates with `smoothDamp` function.