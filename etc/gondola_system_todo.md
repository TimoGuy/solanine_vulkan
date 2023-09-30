> CONTEXT: the gondolas should be the primary "fast-travel" in the game. They're trains where the track materializes right underneath them, and then disappears after the train like atoms disintegrating. This way there is no collision for the rails and it just makes the gondola just be essentially a floating platform that can be as fast as possible. When an entity moves into a track that is done, the particles will float away.

- [ ] Change physics to Jolt Physics
    - [ ] Wowee, really, really think about what you're doing first. Is this really what you want????? Sign below if you really want it, with the date.
        - x____________
    - [ ] Basic API implementation
        - [ ] Create shape initializers. (sphere, box, capsule, mesh)
        - [ ] Create body initializers that ingest the shape. (note: the createBody system is thread-safe already)
        - [ ] Create add body into world system. (how: load a level and allow all the constructors, which should include rigidbody create to run, then have all of them batch into a list where at the end of the step it will add in all the rigidbodies)
        - [x] Create voxel field to compound collider (using multiple boxes and creating stair set pieces and scale and stuff).
            - [x] For now just do the boxes voxels (and make it greedy grouping!!!!)
        - [ ] Create Rigidbody Character Controller
            - [x] Get basic movement down
            - [ ] Get bug: XZ doesn't slow down when holding a movement direction and then going into a waza.
            - [x] Get bug: XZ friction doesn't match up anywhere near to what is going on.
            - [x] Make all bottom collision flat (maybe... just prevent from sliding down steep slopes).
                > Walls feel tacky (not sticky), but 0.5 fricion smoothed over pretty much everything. Having an animation for "pushing against something but against the wall midair" could be good.
        - [x] BUG: rigidbodies are not deleted upon entity delete.
        - [ ] Create renderer. (or not...)
        - [ ] Create recording system (refer to samples).
    - [ ] Get movement down with the ~~rigidbody~~ virtual character controller.
        > After trying out the samples, this type of character controller is what I am looking for. It handles slopes and stairs well and moving platforms, while also being fairly good at being an interactor within the simulation world.
    - [ ] Get moving platform movement down with the ~~rigidbody~~ virtual char controller.
- [ ] Create path system for gondola to show up at a station.
    - [ ] Gondola accelerates along bezier curve towards a max speed.
    - [ ] Copy design of station from Unity Concept Prototype (have station that merely displays how many meters away the next gondola is)
    - [ ] Next in line gondola decelerates with `smoothDamp` function.