# Parent of all TODOs, Solanine TODO.

> These are all of the epics. Upon finishing each one, take time to look over the overall
> game design and ask whether the design needs to be revised due to what was created or what.
> Then redesign what needs to be redesigned, then continue to the next section!


## MVP Development.

- [ ] Movement, Combat, Materialization.
    - [x] `improved_sim_char_todo.md`
    - [ ] `weapon_gameplay_todo.md`
        - [ ] `transparency_renderpass_todo.md` (Just particles)
    - [ ] `harvesting_and_weapon_materialization_todo.md`
- [ ] Time, Seasons, Scarcity & Abundance.
    - [ ] `time_mechanic_todo.md`
    - [ ] `seasons,_looks,_and_atmosphere_todo.md`
    - [ ] `seasons_resource_spawning_todo.md`
- [ ] Puzzly dungeons, Floating island physics.
    - [ ] `villages_and_dungeon_quest_todo.md`
    - [ ] `dungeon_and_island_manipulation_todo.md`

## Art Direction and Engine Dev.

> Finish `MVP Development` first.

> THOUGHT: upon completing this section, MARKETING REALLY NEEDS TO BE CONSIDERED AND A STEAM PAGE UP.

> @NOTE: @RENDER_ENGINE_DESIGN: For things like the chirp sphere (see `weapon_gameplay_todo.md`), using a transparency object was definitely the way to go, since a custom cutout shader would've added unnecessary complexity to the z-prepass subpass. The z-prepass is meant to be an uber-shader at this moment, so it can do sdf-like cutouts and hopefully will support some kind of per-mesh dither fade-out. Or something. Probably would have to be attached to the instance data instead of the material param.
    > The main part that will be challenging is how to get volumetric objects to play nice with transparent objects. Up close, transparent objects should be drawn in front of the volumetric object, since likely the volumetrics doesn't go that far.
        > Transparent objects are just going to be 
        > Fog could get stored in a sliced 3d texture, allowing transparent objects to insert themselves somewhere between a couple layers.
        > Clouds could get given a depth buffer that shows the depth of all the transparent objects (kinda like a z-prepass for transparent objects) and then it renders in kind of a 2-layer depth peel.
        > Or, you could just not have transparent objects. Or make sure that transparent objects never see clouds.
            > But, particles need to have transparency support. Cutouts aren't really enough bc there's likely not going to be a z-prepass for particles since there are so many.


## Random World-gen.

> Finish `Art Direction and Engine Dev` first.


## Tools Development and Push to Data Based Design.

> Finish `Random World-gen` first, or do at the same time.

> THOUGHT: consider Lua.


## Game Engine Optimization and Content Production.

> Finish `Tools Development and Push to Data Based Design` first.

> THOUGHT: at this point, the scope of the game should be set, the marketing done little by little, and the vision locked in.
