# Solanine Vulkan

## Build Guide

> NOTE: this repository and guide is currently only compatible with Windows and Visual Studio 2022

1. Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) version 1.3.224.1 to C: drive onto the default location
   - NOTE: this will make the SDK appear in `C:\VulkanSDK\1.3.224.1` ... hopefully
2. Clone this repository with `git clone`
3. Open the solution file `solanine_vulkan.sln` with Visual Studio 2022
4. Select a build configuration:
   - `Debug`
     - Unoptimized code, level editor
   - `Checked`
     - Identical to `Debug`, however, the libraries being used are `Release` or `Checked` level (i.e. Nvidia PhysX has the `Checked` build configuration)
   - `Release`
     - Removes the editor and just leaves the game as if for release
5. Click the green play button
