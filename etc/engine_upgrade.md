- [x] Editor inputs
    - F1    Toggle play/edit mode (level-level if in level editor | world-level if in world editor | game-level if otherwise)
    - F2    Toggle editor UI
    - F3    Toggle rendering mode (shaded | simple | wireframe | unlit)
    - F4    
    - F5
    - F6
    - F7
    - F8
    - F9
    - F10
    - F11   Toggle fullscreen
- [x] Input system that's action based not render thread based.
    - [x] Add runtime check for whether the correct inputs are being used on the correct threads.
- [x] RenderObjects can either have their own simulation position that gets set or they get tied to a physics object with some offset.
    - [x] Tie a simulation transform id (one vec3 one quat) to the render object, and this gets returned from creating something like a character or voxelfield.
    - [f] For physics objects that have multiple ties with an offset, don't let multiple objects tie themselves to the one physics object, rather put the subsequent objects as siblings so that multiple lerps and nlerps don't have to happen for the one physics object.
- [x] When all the physics objects' new transforms are calculated, insert them into a "next" queue.
- [x] At the end of the delay, at the beginning of the physics loop, lock the physics transform data and swap pointers between the "prev", "current", and "next" so that only pointer swaps have to happen instead of actual data copying inside of the lock.
- [x] At the beginning of every frame right before uploading the positions of the renderobjects' transforms an object interpolation step is put.
    - [x] This, as well as where the simulation position pointers are getting swapped are where multithreaded locks are placed.
    > This results in a massive speedup since the render thread can run almost lock free! (250 -> 600 fps (saving 2.333333ms))
- [x] Material maker
    - [x] Main Engine Changes
        - [f] Change `VulkanEngine` to `VulkanRenderer`.
            - [ ] Set up main engine. It manages the window, selects the renderer/starts the render engine, starts the simulation engine, then polls for input and keeps the input state up to date. Upon shutting down it takes care of shutting down the render engine and simulation engine.
        - [x] Separate things out into editor modes
            - [x] Level Editor (everything created up to this point with a loaded in scene)
            - [f] Texture Editor
            - [x] Material Editor (what we're gonna create. Just no objects except for the `MaterialViewer` object type to render the textures and a bunch of imgui stuff)
    - [x] Texture cooker
        - [x] Textures to mid gen textures.
        - [x] Run all mid gen then run texture cooking.
            - [x] Create simple hotswap dependency tree.
            - [x] Collect jobs when checking hot swappiness.
            - [x] Order all jobs with a few dependency relationship entries.
                > Seomthing like these:
                    - `".halfstep" -> ".hrecipe"`
                    - `".hrecipe" -> ".hderriere"`
                    - `".vert" -> ".humba"`
                    - `".frag" -> ".humba"`
                    - `".humba" -> "rebuildPipelines"`
                    - `".hderriere" -> "materialPropagation"`    # NOTE: notice the "rebuildPipelines" and "materialPropagation" don't have a "." at the beginning. Those point to a specific function to run rather than the file extension.
                > Maybe there could be something like each file type gathering other resources to reload bc one resource reloaded. Idk.
                > But hey, instead of saving a resource list and checking just those resources, maybe something like just recursively checking all the files in the "res" folder would be good, then processing on the new ones and saving their previous modified time. Then any that are missing, create a delete resource job for them.
        - [x] Textures from pool and recipe into ktx format.
            > I think that the texture viewer needs to happen after the new material system gets implemented. We can cook some textures into ktx and write the texture cooker, but the viewer feels EXTREMELY full of friction. I believe that once the material system gets written, the viewer should be really easy to implement. Just assign a material to a render object and just keep changing "texture test material"'s albedo with the texture the creator is looking at.
        - [f] Texture viewer
            - [ ] Select filenames from the `/Textures` folder and subfolders. Import textures as a type of internal texture.
            - [ ] Can delete imported textures.
            - [ ] Texture viewer once it's imported
                - 2D
                    - [ ] Quad
                    - [x] Sphere
                - 3D/2Darray
                    - [ ] Quad-slices
                        - [ ] Slice spread attribute (so that can see each individual slice. Use z axis for 3d textures for slices)
                - Cubemap
                    - [ ] Sphere-cubemap
                    - [ ] Skybox
    - [x] Upgrade hotswapper
        - [x] Allow dependency trees. Allow pngs to be gen'd with the halfway cooker, then trigger another check routine to see if any pngs got updated that are reliant upon the hrecipe's.
            > THIS WAS A FREAKTON OF WORK THAT I WILL NOT BE LISTING HERE!
    - [x] New material system.
        - [x] Connect the hotswap reload to hotswapresources.h/.cpp
        - [x] Load in .hderriere and .humba files into a data structure.
        - [x] Load in pipelines from .humba files.
            - [x] Use SPIRV-Reflect to generate descriptor set layouts and pipeline layouts.
            - [x] Connect globaldescriptor, objectdescriptor, etc. to the parts where the name matches from reflection.
                - [x] CHANGE: actually just assume every shader file that will be a material has a certain order to the bindings.
        - [x] Load in materials from .hderriere files.
            - [x] Create list of which unique textures to use and insert into descriptor set.
            - [x] Insert all material params into material collection.
            - [x] Insert all textures into material collection texture maps.
                - [x] Create KTX texture loader.
        - [x] Get new material data to run.
            - [x] Implement bindless textures for the giant texturemap
                - [x] Turns out I'm looking for descriptor indexing.
                - [x] Enable extensions.
                - [x] Mark descriptor binding as using a variable count.
        - [x] Fix that pesky error when exiting the program.
        - [x] Migrate to material system.
            - [x] CREATE MATERIAL VIEWER.
            - [x] Add different materials (group by humba) in `compactRenderObjectsIntoDraws`
            - [x] Load in correct material IDs instead of using material ids from vkgltf
            - [f] Connect material to render object's palette. When loading in the model, it will also return a palette, and the programmer can insert that palette or insert a loaded palette swap file into the render object.
                - [f] Load palette swap file.
            - [x] Try to come up with a better sorting for compacting render meshes
                > RESULTS: Ummmmmm, I couldn't really find anything very useful, but just creating a cache that gets managed by renderobjectmanager was my solution.
            - [f] ~~Don't load textures with vkgltf. (bandaid... see sub task for real.)~~ I'll just mark this as future, bc material palettes should do this solution.
                - [f] Re-export all gltf models with "Geometry->Materials->Export" and "Geometry->Images->None" set.
                    - [f] @NOTE: "Geometry->Materials->Placeholder" doesn't export the material name and we're matching on name. However, in the future it might be good just to match on primitive id, which would just be using "Placeholder".
            - [f] ~~Turn off the "pbrmaterial" material.~~ Use material palettes that compare on the primitive id.
    - [x] Material viewer
        - [x] Orbit camera view
        - [x] Sphere material inspector.
            - [x] Draw sphere with as the subject for the orbit view.
        - [x] Directly change the EDITORTextureViewer model's assigned dmps and its renderobj's umb.
        - [x] Have properties you can change 
        - [x] Save properties to file.
            - [x] Mark file as dirty when edited
            - [x] Show "save" button when file is dirty.
            - [x] Write saving procedure.
            - [x] ~~Show "would you like to save your changes?" when making a material copy and when opening a different material to edit.~~
            - [x] Prevent changing or copying materials if the material params are dirty.

- [f] "Reflection" imgui panel.
    - [ ] Install https://github.com/Csabix/imgui/tree/master/auto
    - [ ] Setup the struct, then run `ImGui::auto(...)` on the struct!
    - [x] Check to see if cglm types are supported
        - [ ] It looks like it's easy to implement, if you just copy the implementation of glm.

- [x] Rename "Character" to something else
    - [x] Then, import all of the jolt physics stuff into pch! That way "JPH::Character" doesn't need a differentiation!

- [x] I want to be able to load up the game near instantly!
    - [x] Slimegirl.glb's animations take 1.8sec to parse (much better than the former 8sec but seriously sucky).
        > Specifically, reading in the file into json takes 1892.37ms with the animations vs 24.37ms without the animations.
        - [x] Create animation cooker, that takes a .glb file and prints out an animation file that contains all the actions and removes all the animations it had formerly.
            - [x] Read file from tiny_gltf and print out animation data into file.
            - [x] Rewrite data from tiny_gltf load (after doing `.clear()` to animations) into another .glb file in `models_cooked`
        - [x] Rewrite the glb file without its animations and put into `models_cooked` with the just-animation file.
        - [x] Okay, found out that a lot of this information is stored in the bufferview, so new strategy.
            - [x] Store the binary data of the vkgltf animations which copy from the bufferviews and accessors.
            - [x] Delete the bufferview and accessor entries that were used during this process.
            - [x] Bc it's too much work, leave the actual buffer alone. (This loads fairly quickly too, bc it's just one big chunk of bytes.)
            > After these things, the animations get loaded in with the model separately over the course of 232.779ms (slimegirl model) where before it was almost 2000ms! Since we didn't actually chop down the buffer, after cooking the model down, the info in the buffer that contained the animated information was left still. Because of this, it takes 47.638ms to load in the buffer as opposed to the eye-watering 24.37ms
            > Anywho, huge improvement! I think I can sleep well now!
    > Now, the game loads up in about 5000ms. Not instant, but it feels like it now that the SlimeGirl model loads up super quickly.

- [x] Compute based skinning.
    - [x] Setup vkdevice context to allow for compute shaders.
        > Afaik using the graphics queue should support compute shaders too. Using another compute shader queue is only necessary if async compute is desired.
    - [x] Write compute shader.
    - [x] @NOTE: happens after compute based culling pass.
    - [x] Compact and sort skinned meshes into buffer entries and instance id offsets (separated by just the umb idx).
        - [x] The offsets should be configured such that when the indirect draw command is compiled/run normally, it should all just line up to the correct instance data.
    - [x] Create buffer that matches the size of all the skinned meshes, with the below attributes:
        - Position
        - Normal
        - UV0*
        - UV1*
        - Color0*
        - animatorNodeID  @TODO: take it out of the InstancePointer struct!
        - BaseInstanceID*
            * For passing thru.
    - [x] Make sure to multiply the skeletal animation node matrix with the pos/norm
    - [x] Output to buffer that has these attributes:
        - Position
        - Normal
        - UV0
        - UV1
        - Color0
        - InstanceIDOffset
            > This is set to 0 for unskinned meshes, but it's so that skinned meshes can be drawn with one model bind and set `gl_BaseInstance` to 0 and have the actual instance ids tied to their indices.
    - [x] It appears that the data is off somehow. The input mesh for the skinning is off for sure.
    - [x] The instance ptr is off. @NOTE: turns out the vertex attribute wasn't assigned for instance ID offset and that's why.
    - [x] It looks like some faces are missing from the 2nd slime girl legs.
    - [x] It looks like where the models switch are maligned.
        > The issue was thinking the instance id and the indirect draw command id are the same. They were the same until the giant mesh that combined together 36 of the draws into 1.
    - [x] It looks like the material for slimegirl is incorrect (shouldn't be gold right???)
        > It should be gold for the belt buckle and bag buckle (material #5)... but for some reason all of the instances (0-36) for the first group of the skinned meshes are pointing to material #5
    - [x] Double check: it looks like the shoes for slimegirl aren't correct.
    - [x] It looks like the model matrix for skinned meshes is messed up. Probably inverse transposed matrix operations aren't associative.
        > Normals aren't getting inputted into the vertex shader correctly (ones that got compute skinned). Setting the normal to 1,0,0 causes it to be 0,1,0 when ingested. Likely it's vec4 padding issues.
        > TRY: put instance id offset between inPos and inNormal in the buffer to resolve the vec3/vec4 padding issue.
    - [x] Get wireframe picking to work with skinned meshes.

- [f] Pipeline layout from reflection of the shaders.
    - [f] Can fix the weird dependency hack going on with the skinning compute shader.
        > Bc of the binding flags system with the descriptor indexing.... this feels like too much to bite and chew off. CHANGE TO FUTURE PLAN.

- [x] Fix hacky compute skinning pipline creation system. (line 1443 of VulkanEngine.cpp)
    - [x] Be able to create descriptor set layouts at will.
    - [f] Detect for descriptor indexing using `SpvOpTypeRuntimeArray`.
        > This would be for the case of pipeline layout reflection. However, this may be something to look into for the future... if it's not gonna hide a bunch of validation errors.
- [x] Fix deallocation error.
    > Just had to destroy the buffers for compute skinning.

- [x] New mesh organization.
    - [x] Using buckets instead of sorting.
    - [x] Do it lol.
- [x] Compute culling.
    - [x] RESEARCH: how to use `vkCmdDrawIndirectIndexedCount` to compact draw commands.
        > You don't have to rewrite instance data if you use indexed count.
        > Also, with the command buffer, instead of having commands compacted to the mesh level (index offset and counts), you can compact commands to the model level.
    - [x] Fix skinned meshes and how they render (start at the @TODO: start here!!!!!!)
    - [x] Use a compute shader to iterate thru all the instances, and if one is visible (for this time just have the `isvisible()` func just return true), atomic add the count of the count buffer, and take the stored offset value + the new count in the count buffer - 1 to get the index in the indirect command buffer to write to. If it's the same index that the shader is working on, then skip writing the command, but if not, copy the whole indirect command into the offset+count-1 position.
        - [x] Figure out how the compute shader will figure out which count buffer offset slot to write to. @THOUGHT: maybe just adding that field into the indirect offsets buffer?
            - [x] ANSWER: copy the draw command into the next atomic add reserved spot.
        - [x] Create `isVisible()`
            - [x] Frustum culling.
                - [x] Fill in a placeholder bounding sphere.
                - [x] MOOOOOREEEE!!!!
                - [x] Bug fixes m8
            - [f] Occlusion culling. (FUTURE!! This shouldn't be too hard to implement though)
        - [x] Create another indirect draw command buffer for shadows.
            - [x] Only do frustum culling tho.
            - [x] Has its own count buffer too.

- [x] Update missing material.
    > Now it's an amalgamation of missing material in japanese and english in wordart. (https://www.makewordart.com/)

- [x] BUGFIX: fix the material viewer black ball.

- [x] BUGFIX: materials being off (both in material viewer and in level editor).
    > Turns out that sorting by humba was incorrect (MaterialOrganizer.cpp: line 542), bc it was a.dmpsFname vs b.humbaFname, instead of both humbaFname.
- [x] BUGFIX: entering level editor twice causes instance corruption.
    > This is fortunately not instance corruption. Rather, this is related to the physics engine. It could be a stale transform that was set in the pool. Idk.
    > Okay, so this was not from the physics engine/stale transforms. This was due to bad allocation for the first element. Since the first element's index didn't get set correctly (when `numVFsCreated == 0`), subsequent voxel fields created get fragmentation.
    - [x] Also, didn't realize there was a memory leak with `vfpd->voxelData` in the `destroyVoxelField()` and destructor. Bonus fix!
        > But... it also makes we wonder who is supposed to be responsible for destroying the voxeldata. I think the destruction is in the right place, so how do we create the voxeldata? That's the real question I think.
- [x] FIX: turn off dof when going back into free cam mode.

- [ ] Better level editor.
    - [x] Update collision box texture for voxel fields.
    - [x] No physics simulations when the level is in editing mode.
        - [x] Turn off physics.
        - [x] Fix character position going up when moving them.
        - [x] Fix voxelfield not wanting to rotate.
            > It now holds off on propagating the new physics transform until 2 simulation ticks later so that no more lerping happens. A timer keeps track of the sim ticks. For compound objects, the lag is very obvious. Maybe adding a thing where you propagate the transforms of the other objects would be good?
            - [x] Do the above
        - [x] Toggle physics with the same key as the camera type.
        - [x] Add strong labels whether in play mode or not.
            - [x] Add play mode pause (~~ctrl~~ shift F1). Keeps in play mode, pauses simulation, changes camera mode.
                > So it's WIP, but F1 will toggle play mode, ~~Ctrl~~ Shift+F1 will pause and unpause the physics sim (only when in play mode), and F2 cycles thru the camera styles.
                > There must be something wrong with Windows, bc I think it's blocking Ctrl+F1 inputs. Or my Keychron K8 keyboard.
            - [x] Add imgui play mode stats.
                > It's super clear with colored text replacing the save, open Scene Properties window.
            - [x] Add changing camera modes during play mode too.
                > It's bound to F2.
            - [x] Add "Start Play Mode" "Stop Play Mode" debug messages.
    - [x] Make the simulation values be run by an atomic size_t instead of switching pointers and with a mutex lock.
    - [x] Play mode uses a copy of the editing level.
        - [x] Save the state of the level as soon as play mode is entered. (@NOTE: no need to load the level again, just use the state of the level you were at before).
        - [x] Then, add a player object at one of the spawn points.
        - [x] Saving is disabled in play mode.
            > ImGui window that does saving is replaced with play mode stats.
        - [x] As soon as play mode is exited, the previously saved state of the level is reloaded.
        - [x] BUGFIX: some objects don't appear in the right spots after the level is reloaded?
            > It seems to be the player object not getting deleted after doing a level reload upon stopping play mode.
            > Fix: It's the same issue as the voxel field creation index issue when there are 0 instances created. The index never gets set in the list of capsule/voxelfield indices after deleting all the way to zero and then rebuilding (only is noticable after the [0] gets set to something other than 0, which is what happens during a full delete). Well, now it's both fixed in capsules and voxelfields... so should've definitely done it for the capsules too after doing it for the voxelfields.
    - [ ] Toggle wireframe mode. (Hook into material system)
        - [ ] Trigger a recook of the material system but now with the pipelines remade to have wireframe set.
        - [ ] Include zprepass in wireframe render, but not shadow map!
    - [x] Don't include player object in saved entity assortment of identities files.
        > Removed from `hello_hello_world`
        > @FUTURE: Player object location will be handled by the global state.
        - [ ] Crash application if found trying to save the player object.
    - [ ] Lay out simulation objects that are just level testing spawn points and select which one to start playing at when doing level testing
        - EDITORTestLevelSpawnPoint.h/.cpp This name would be good eh!
        - [x] Create the 3d model.
        - [x] They get saved, but their render object is in the builder layer.
        - [ ] Select which one to spawn at to start playing.
    - [ ] When pressing F1 (or whatever key will be for starting/stopping play mode), pop up a menu that has a list of the currently available test spawn points. Click on one and the player will be created and spawned at that position.
        - [ ] Press a certain key to reset the player to the position.
    - [x] Disable player being able to be created in palette.

- [ ] Add Tracy profiler
    - [ ] Install the hpp and cpp files.
    - [ ] Put it in every function.
    - [ ] Figure out multithreading with the physics engine.
    - [ ] Add GPU support with vulkan.

- [ ] Font cooker
    - [ ] STUFF

- [ ] Mid level AO: XeGTAO (port to vulkan).
    - [ ] See if the hlsl will just compile from the compiler.
    - [ ] Get the velocity buffer working.
    - [ ] Input in all the values needed for the XeGTAO to work.
    - [ ] Magic and it works???!??!?!?!?

- [ ] GRAPHICS EXPERIMENT: Real time sky ambient occlusion
    > Get the whole area around the camera captured in a low frequency shadow texture in multiple different directions (26?).
    > Maybe instead of having to sample 26 shadow maps (each direction's render is amortized, so the main cost is the sampling direction), maybe having render probes capture the occlusion, that could be good.
        > @NOTE: it only has to look good at 30 and 60 fps!!! For these kinds of effects, the higher the better, so if they look good at those marks, they'll look good anywhere.
        > Also, the technique has to be stable, but also be able to react quickly to changes.
    > Perhaps a good reference technique? https://www.youtube.com/watch?v=TzS0Zspn2Ig (16 shadowmaps per frame, sampled with monte-carlo and TAA)

- [ ] UI Editor

    - [ ] Small animations of how UI elements will come in and move out.

    - [ ] Entity that loads and displays a UI file.

- [ ] 
