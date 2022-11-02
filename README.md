# Solanine Vulkan

*Due to inability to play the game [Solanine](https://github.com/TimoGuy/DemoEngine) on Steam Deck, as well as the frustration and shottiness of OpenGL, a rewrite and restructuring into Vulkan for the rendering became desirable. Thus, this repository exists.*

## Build Guide

> NOTE: this repository and guide only supports Visual Studio 2022

1. Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) version 1.3.224.1 to C: drive onto the default location
2. Clone this repository with `git clone`
3. Open the solution file `solanine_vulkan.sln` with Visual Studio 2022
4. Select a build configuration:
   - `Debug`
     - Unoptimized code, level editor
   - `Release`
     - ~~Removes the editor and just leaves the game as if for release~~ Similar to `Debug` but just whole program optimization is enabled
5. Click the green play button

## Screenshots

![Pipelines, importing gltf models, skinning gltf models, and skyboxes](etc/Solanine%20Prealpha%20-%20Vulkan%20-%20Build%20227%2010_15_2022%2013_14_52.png)
*Pipelines, importing gltf models, skinning gltf models, and skyboxes (Build #227)*

![Renderdoc showing that the irradiance and prefilter maps were generated and connected to the shader (if you scroll just a little further down the input textures you would've seen the brdf lut map too)](etc/RenderDoc%20v1.21%2010_16_2022%2001_05_22.png)
*Renderdoc showing that the irradiance and prefilter maps were generated and connected to the shader (if you scroll just a little further down the input textures you would've seen the brdf lut map too) (Build #242)*

![First PBR! It uses the textures packed in the gltf model (also, using a night space skybox probs wasn't the best for generating the irradiance and prefiltered maps. At least it's a little noticable)](etc/Solanine%20Prealpha%20-%20Vulkan%20-%20Build%20256%2010_17_2022%2002_43_46.png)
*First PBR! It uses the textures packed in the gltf model (also, using a night space skybox probs wasn't the best for generating the irradiance and prefiltered maps. At least it's a little noticable) (Build #256)*

![Implementation of hdr skyboxes, finally](etc/Solanine%20Prealpha%20-%20Vulkan%20-%20Build%20292%2010_17_2022%2020_15_50.png)
*Implementation of hdr skyboxes, finally (Build #292)*

![Object picking and transformation gizmo set up!](etc/Solanine%20Prealpha%20-%20Vulkan%20-%20Build%20385%2010_21_2022%2023_32_01.png)
*Object picking and transformation gizmo set up! (Build #385)*

![Various things, but feature is shadows here.](etc/Solanine%20Prealpha%20-%20Vulkan%20-%20Build%20750%2011_2_2022%2002_20_36.png)
*Various things, but feature is shadows here. Truthfully, there was bullet3 physics integration, the new Yosemite<sup>TM</sup> object (just a model with a cube collision... for now!), saving and loading, a nice object serialization system to make saving and loading easy maintenance, and then finally the CSM shadows. (Build #752)*
