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
- [ ] Material maker
    - [ ] Main Engine Changes
        - [f] Change `VulkanEngine` to `VulkanRenderer`.
            - [ ] Set up main engine. It manages the window, selects the renderer/starts the render engine, starts the simulation engine, then polls for input and keeps the input state up to date. Upon shutting down it takes care of shutting down the render engine and simulation engine.
        - [ ] Separate things out into editor modes
            - [x] Level Editor (everything created up to this point with a loaded in scene)
            - [x] Texture Editor
            - [ ] Material Editor (what we're gonna create. Just no objects except for the `MaterialViewer` object type to render the textures and a bunch of imgui stuff)
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
    - [ ] New material system.
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
        - [ ] Migrate to material system.
            - [x] CREATE MATERIAL VIEWER.
            - [x] Add different materials (group by humba) in `compactRenderObjectsIntoDraws`
            - [x] Load in correct material IDs instead of using material ids from vkgltf
            - [f] Connect material to render object's palette. When loading in the model, it will also return a palette, and the programmer can insert that palette or insert a loaded palette swap file into the render object.
                - [f] Load palette swap file.
            - [ ] Try to come up with a better sorting for compacting render meshes
                > RESULTS: 
            - [ ] Don't load textures with vkgltf. (bandaid... see sub task for real.)
                - [ ] Re-export all gltf models with "Geometry->Materials->Export" and "Geometry->Images->None" set.
                    - [f] @NOTE: "Geometry->Materials->Placeholder" doesn't export the material name and we're matching on name. However, in the future it might be good just to match on primitive id, which would just be using "Placeholder".
            - [ ] Turn off the "pbrmaterial" material.
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

- [ ] "Reflection" imgui panel.
    - [ ] Install https://github.com/Csabix/imgui/tree/master/auto
    - [ ] Setup the struct, then run `ImGui::auto(...)` on the struct!
    - [ ] Check to see if cglm types are supported
    - [ ] Add support to them if not!

- [ ] Rename "Character" to something else
    - [ ] Then, import all of the jolt physics stuff into pch! That way "JPH::Character" doesn't need a differentiation!

- [ ] UI Editor

    - [ ] Small animations of how UI elements will come in and move out.

    - [ ] Entity that loads and displays a UI file.

- [ ] 
