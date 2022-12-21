#include "MinecartSystem.h"

#include "VulkanEngine.h"
#include "RenderObject.h"
#include "VkglTFModel.h"
#include "PhysicsEngine.h"
#include "DataSerialization.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"


MinecartSystem::MinecartSystem(VulkanEngine* engine, EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _engine(engine), _rom(rom)
{
    if (ds)
        load(*ds);

    _minecartModel                         = _rom->getModel("Minecart", this, [](){});
    _builder_bezierControlPointHandleModel = _rom->getModel("BuilderObj_BezierHandle", this, [](){});

    _paths.push_back({});
    _paths[0].controlPoints.push_back(glm::vec3(0, 0, 0));
    _paths[0].controlPoints.push_back(glm::vec3(0, 0, 5));
    _paths[0].pathScale = 1.0f;
    _paths[0].pathScale = 0.0f;

    reconstructBezierCurves();
    _isDirty = false;
}

MinecartSystem::~MinecartSystem()
{
    for (auto& ro : _builder_bezierControlPointRenderObjs)
        _rom->unregisterRenderObject(ro);
    for (auto& ro : _renderObjs)
        _rom->unregisterRenderObject(ro);
    _rom->removeModelCallbacks(this);
}

void MinecartSystem::physicsUpdate(const float_t& physicsDeltaTime)
{
    const size_t numSlices = 10;
    for (size_t i = 0; i <= numSlices; i++)
    {
        float_t t = (float_t)i / (float_t)numSlices;
        // @TODO: figure out how you want to do splits/forks and bezier curves.
        //        It seemed possible to do just a cubic bezier curve but there may be some things to consider before pulling the trigger on that.
        //        Also, what control points are considered when doing a split fork is an important thing to consider as well. I don't know what to do about that.
    }
}

void MinecartSystem::lateUpdate(const float_t& deltaTime)
{}

void MinecartSystem::dump(DataSerializer& ds)
{
    Entity::dump(ds);
}

void MinecartSystem::load(DataSerialized& ds)
{
    Entity::load(ds);
}

void MinecartSystem::loadModelWithName(const std::string& modelName)
{}

void MinecartSystem::createCollisionMeshFromModel()
{}

void MinecartSystem::reportMoved(void* matrixMoved)
{
    //
    // Look up the path index and subindex of the selected control matrix
    // and then update the internal bezier curve information based off this
    //
    size_t selectedPathIndex,
           selectedControlPointIndex;
    bool found = getControlPointPathAndSubIndices(selectedPathIndex, selectedControlPointIndex);
    if (!found)
        return;

    glm::mat4 asMat4 = *(glm::mat4*)matrixMoved;
    glm::vec3 cpPos  = physutil::getPosition(asMat4);
    _paths[selectedPathIndex].controlPoints[selectedControlPointIndex] = cpPos;
}

void MinecartSystem::renderImGui()
{
    //
    // Find the total number of control points and the selected control point path index and index.
    //
    size_t totalControlPointCount = 0;
    for (auto& path : _paths)
        totalControlPointCount += path.controlPoints.size();
    ImGui::Text(("Num control points: " + std::to_string(totalControlPointCount)).c_str());

    size_t selectedPathIndex,
           selectedControlPointIndex;
    bool showSelectedIndex = getControlPointPathAndSubIndices(selectedPathIndex, selectedControlPointIndex);
    if (showSelectedIndex)
    {
        ImGui::Text(("Selected path: " + std::to_string(selectedPathIndex)).c_str());
        ImGui::Text(("Selected index: " + std::to_string(selectedControlPointIndex)).c_str());
    }

    //
    // Manipulations
    //
    if (showSelectedIndex &&
        ImGui::Button("Add control point after selected control point"))
    {
        auto      iti     = _paths[selectedPathIndex].controlPoints.begin() + (size_t)selectedControlPointIndex;
        glm::vec3 prevPos = _paths[selectedPathIndex].controlPoints[(size_t)selectedControlPointIndex];
        _paths[selectedPathIndex].controlPoints.insert(iti, prevPos + glm::vec3(0, 0, 1));

        reconstructBezierCurves();  // Automatically just rebake when adding new points
    }

    if (_isDirty)
    {
        if (ImGui::Button("Rebake System"))
        {
            // @TODO: create the baking system (partially done I guess)
            reconstructBezierCurves();

            _isDirty = false;
        }
    }
}

bool MinecartSystem::getControlPointPathAndSubIndices(size_t& outPathIndex, size_t& outSubIndex)
{
    bool found = false;
    int32_t selectedControlPointIndex = -1;
    for (size_t i = 0; i < _builder_bezierControlPointRenderObjs.size(); i++)
    {
        auto& cp = _builder_bezierControlPointRenderObjs[i];
        if (&cp->transformMatrix == _engine->getMatrixToMove())
        {
            selectedControlPointIndex = (int32_t)i;
            found = true;
            break;
        }
    }
    size_t selectedPathIndex = 0;
    while (selectedControlPointIndex >= 0)
    {
        auto& path = _paths[selectedPathIndex];
        if (path.controlPoints.size() <= selectedControlPointIndex)
        {
            selectedControlPointIndex -= (int32_t)path.controlPoints.size();
            selectedPathIndex++;
        }
        else break;
    }

    if (found)
    {
        outPathIndex = selectedPathIndex;
        outSubIndex = (size_t)selectedControlPointIndex;
    }

    return found;
}

void MinecartSystem::reconstructBezierCurves()
{
    for (auto& ro : _builder_bezierControlPointRenderObjs)
        _rom->unregisterRenderObject(ro);

    _builder_bezierControlPointRenderObjs.clear();

    for (auto& path : _paths)
        for (auto& controlPoint : path.controlPoints)
        {
            _builder_bezierControlPointRenderObjs.push_back(
                _rom->registerRenderObject({
                    .model = _builder_bezierControlPointHandleModel,
                    .transformMatrix = glm::translate(glm::mat4(1.0f), controlPoint),
                    .renderLayer = RenderLayer::BUILDER,
                    .attachedEntityGuid = getGUID(),
                })
            );
        }
}
