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
    - [x] Figure out way to load it into the gondola system object.
        > Prefab loading system, which is the same as the scene loading system except it returns the entity pointers.
    - [x] Gondola accelerates along ~~bezier curve~~ B-spline towards a max speed.
        > Not bezier curve, do a uniform B-spline (See https://www.youtube.com/watch?v=jvPPXbo87ds)
        > This is the characteristic matrix:
                | 1  4  1  0 |
            1/6 |-3  0  3  0 |
                | 3 -6  3  0 |
                |-1  3 -3  1 |
        > Thus, this is the polynomial coefficients version of the b-spline:
            P =  (   P0 + 4*P1 +   P2) +
                t(-3*P0 +        3*P2) +
              t^2( 3*P0 - 6*P1 + 3*P2) +
              t^3(-1*P0 + 3*P1 - 3*P2 + P3) +
            > Ahhhh, except I forgot to include the 1/6.
    - [x] Add station button. Uses 4 nodes, where they indicate the beginning and end of the straight track.
        - [x] Put middle 2 nodes at the ends of the station, then 2 more off the ends (this guarantees that if all 4 are in line, the middle 2 in the rendered B-spline will be perfectly in line).
            > Or could have it be 5 nodes, so that the middle one is for sure the middle one.
    - [x] Update the ~~4~~ 5 control points whenever the station itself moves.
        - [x] This is for (a possible game element) manipulating the positions of the stations' planets.
    - [ ] At the last station, gondola turns around and goes different direction.
        - [ ] ~~Change driving to come from middle instead of from front.~~
        - [x] Keep track of rearmost `t` and then assign it to the now front `t` and then toggle train reverse mode
        - [x] Take the `t` of the station node, and then add `LENGTH_CART_LOCAL_NETWORK * NUM_CARTS_LOCAL_NETWORK / LENGTH_STATION_LOCAL_NETWORK` as the `t` offset (in whatever direction you're going).
        - [x] Slow down to a stop.
            - [x] BUG: it's not working once it gets out the first station.
        - [ ] Process turning around to go into reverse mode.
            - CAN'T FIGURE THIS OUT!!!
        - [x] Wait 5 seconds before starting to accelerate out of the station.
        - [x] Keep track of how long the whole simulation time is.
            - 1 for every `t`
            - 5 for every station.
            - idk how much for every slow down into the station.
            - [x] Figure out a way to take a global timer, offset it with the offset factor of each train, and then calculate the `t` value that that results in.
                - [x] Would at least need to know what the whole simulation cycle time is so that that can be moduloed
    - [ ] BUG: when adding a new station, it flips around the orientation of the forward and backwards control points, forcing me to have to turn it around 180 degrees every time.
    - [ ] BUG: fix the bugs where control points get added and then simulation's control points gets off.
        - [ ] Go thru on every "Add control point" and "Remove control point"
        - [ ] Watch out for if a simulation didn't have an aux position, and now it does with adding another control point.
            > I think the best way to accomplish all this is to only update anchor points, then for every simulation, re-get the secondary and auxiliary positions.
    - [ ] Make train line double lined.
        - [ ] Take neighboring two nodes and get their deltaposition and that should be the tangent. Cross it with vector.up and you should get the offset direction of the control points.
        - [ ] Make track changer.
            - [ ] Make trains change their tracks to run on the left side of the rail.
            > I think a way you could do it is by creating two versions of the path, and every other simulation uses the different versions. One version starts and lands in the west side of the end stations, and one version starts and lands in the east side of the end stations.
    - [ ] Copy design of station from Unity Concept Prototype (have station that merely displays how many meters away the next gondola is)
    - [ ] Every gondola always stops at every station.

- [x] EMERGENCY DETOUR
    - [x] Get voxel fields to create their own meshes instead of using each voxel as their own render object.
        > Did kinda a compromise. Using the generated physics colliders, creates a render obj to represent it.