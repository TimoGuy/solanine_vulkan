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

    if (_paths.empty())
    {
        // Initialize a path if empty (i.e. if no data was loaded)
        _paths.push_back({});
        _paths[0].controlPoints.push_back(glm::vec3(0, 0,  0));
        _paths[0].controlPoints.push_back(glm::vec3(0, 0,  5));
        _paths[0].controlPoints.push_back(glm::vec3(0, 0, 10));
        _paths[0].controlPoints.push_back(glm::vec3(0, 0, 15));
        _paths[0].pathScale = 1.0f;
        _paths[0].pathScale = 0.0f;
    }

    reconstructBezierCurves();
    _isDirty = false;
    
    _enablePhysicsUpdate = true;
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
    //
    // Draw the debug lines for the minecart
    //
    const size_t    numSlices     = 10;
    const glm::vec3 bezierColor   = glm::vec3(43, 217, 133) / 255.0f;
    const glm::vec3 controlColor1 = glm::vec3( 0,   0,   0) / 255.0f;
    const glm::vec3 controlColor2 = glm::vec3(31, 219, 182) / 255.0f;
    for (auto& path : _paths)
    for (size_t i = 0; i < path.controlPoints.size() - 1; i += 3)  // @NOTE: there will be one final control point that won't have 3 more following it with this traversal, so that's why it's size-1
    {
        glm::vec3 prevEvalBezierPoint;
        for (size_t j = 0; j <= numSlices; j++)
        {
            float_t t = (float_t)j / (float_t)numSlices;
            glm::vec3 t3(t);

            //
            // HOW THE MINECART SYSTEM WORKS
            //
            // These are quadratic bezier curves, so if a new section is added, then 3 more control points are added, the last one created being C0 for the new bezier curve.
            // This prevents any kind of weirdnesses when adding more 'dimensions' to the bezier curve, however, it does limit the types of tracks that can be created.
            // I don't wanna say that putting this kind of limitation is "okay bc this is poc", but I don't think this would be a bad limitation in the full game by any means.
            // The thing that might be hard is having the bezier curves' mid control points not mirror the next sections' control points. (i.e. C2 not mirroring C1 of the next group).
            // But that is a limitation that is considered and I personally think that's okay.
            //     -Timo 2022/12/21
            //
            glm::vec3 controlPointsCooked[] = {
                glm::vec3(),
                glm::vec3(),
                glm::vec3(),
                glm::vec3(),
            };
            if (path.parentPathId < 0)
            {
                controlPointsCooked[0] = path.controlPoints[i + 0];
                controlPointsCooked[1] = path.controlPoints[i + 1];
                controlPointsCooked[2] = path.controlPoints[i + 2];
                controlPointsCooked[3] = path.controlPoints[i + 3];
            }
            else
            {
                if (i == 0)
                    controlPointsCooked[0] = _paths[(size_t)path.parentPathId].controlPoints[(size_t)path.parentPathCPId];
                else
                    controlPointsCooked[0] = path.controlPoints[i - 1];
                controlPointsCooked[1] = path.controlPoints[i + 0];
                controlPointsCooked[2] = path.controlPoints[i + 1];
                controlPointsCooked[3] = path.controlPoints[i + 2];
            }

            glm::vec3 evalCtrlPtsLayer1[] = {
                physutil::lerp(
                    controlPointsCooked[0],
                    controlPointsCooked[1],
                    t3
                ),
                physutil::lerp(
                    controlPointsCooked[1],
                    controlPointsCooked[2],
                    t3
                ),
                physutil::lerp(
                    controlPointsCooked[2],
                    controlPointsCooked[3],
                    t3
                ),
            };
            glm::vec3 evalCtrlPtsLayer2[] = {
                physutil::lerp(
                    evalCtrlPtsLayer1[0],
                    evalCtrlPtsLayer1[1],
                    t3
                ),
                physutil::lerp(
                    evalCtrlPtsLayer1[1],
                    evalCtrlPtsLayer1[2],
                    t3
                ),
            };
            glm::vec3 evalBezierPoint = physutil::lerp(evalCtrlPtsLayer2[0], evalCtrlPtsLayer2[1], t3);

            if (j > 0)
                PhysicsEngine::getInstance().debugDrawLineOneFrame(
                    prevEvalBezierPoint,
                    evalBezierPoint,
                    bezierColor
                );
            prevEvalBezierPoint = evalBezierPoint;
        }

        for (size_t ic = i; ic < i + 3; ic++)  // @NOTE: only 3 lines connect the 4 control points together
        {
            float_t t = (float_t)ic / (float_t)path.controlPoints.size();
            t = t * t;  // Square the t to approximate gamma correction

            glm::vec3 pt1, pt2;
            if (path.parentPathId < 0)
            {
                pt1 = path.controlPoints[ic + 0];
                pt2 = path.controlPoints[ic + 1];
            }
            else
            {
                if (ic == 0)
                    pt1 = _paths[(size_t)path.parentPathId].controlPoints[(size_t)path.parentPathCPId];
                else
                    pt1 = path.controlPoints[ic - 1];
                pt2 = path.controlPoints[ic + 0];
            }

            PhysicsEngine::getInstance().debugDrawLineOneFrame(
                pt1,
                pt2,
                physutil::lerp(controlColor1, controlColor2, glm::vec3(t))
            );
        }
    }
}

void MinecartSystem::lateUpdate(const float_t& deltaTime)
{}

void MinecartSystem::dump(DataSerializer& ds)
{
    Entity::dump(ds);

    for (auto& path : _paths)
    {
        ds.dumpString("__path__");
        ds.dumpFloat((float_t)path.parentPathId);
        ds.dumpFloat((float_t)path.parentPathCPId);
        ds.dumpFloat(path.pathScale);
        ds.dumpFloat(path.startPathDistance);
        ds.dumpFloat((float_t)path.controlPoints.size());
        for (auto& controlPoint : path.controlPoints)
            ds.dumpVec3(controlPoint);
    }
}

void MinecartSystem::load(DataSerialized& ds)
{
    Entity::load(ds);

    while (ds.getSerializedValuesCount() > 0)
    {
        std::string type = ds.loadString();
        if (type == "__path__")
        {
            Path newP = {};
            newP.parentPathId      = (int32_t)ds.loadFloat();
            newP.parentPathCPId    = (int32_t)ds.loadFloat();
            newP.pathScale         = ds.loadFloat();
            newP.startPathDistance = ds.loadFloat();

            size_t numCPs = (size_t)ds.loadFloat();
            for (size_t i = 0; i < numCPs; i++)
                newP.controlPoints.push_back(ds.loadVec3());

            _paths.push_back(newP);
        }
    }
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
        size_t    insertionIndex = std::max((int32_t)selectedControlPointIndex - 1, 0) / 3 * 3 + 4 + (_paths[selectedPathIndex].parentPathId < 0 ? 0 : -1);

        std::cout << "INDEX:  " << selectedControlPointIndex                      << std::endl;
        std::cout << "MAX:    " << _paths[selectedPathIndex].controlPoints.size() << std::endl;
        std::cout << "INSIND: " << (insertionIndex - 1)                           << std::endl;

        glm::vec3 midPos         = _paths[selectedPathIndex].controlPoints[insertionIndex - 1];
        glm::vec3 refPos         = _paths[selectedPathIndex].controlPoints[insertionIndex - 2];
        glm::vec3 nxtPos         = midPos + midPos - refPos;

        std::vector initPositions = {
            nxtPos,
            nxtPos + glm::vec3(0, 0,  5),
            nxtPos + glm::vec3(0, 0, 10),
        };
        _paths[selectedPathIndex].controlPoints.insert(
            _paths[selectedPathIndex].controlPoints.begin() + insertionIndex,
            initPositions.begin(),
            initPositions.end()
        );

        reconstructBezierCurves();  // Automatically just rebake when adding new points
    }

    if (showSelectedIndex &&
        ImGui::Button("Create child path after selected control point"))
    {
        size_t connectionIndex = std::max((int32_t)selectedControlPointIndex - 1, 0) / 3 * 3 + 3 + (_paths[selectedPathIndex].parentPathId < 0 ? 0 : -1);  // `insertionIndex` minus 1

        glm::vec3 midPos = _paths[selectedPathIndex].controlPoints[connectionIndex - 0];
        glm::vec3 refPos = _paths[selectedPathIndex].controlPoints[connectionIndex - 1];
        glm::vec3 nxtPos = midPos + midPos - refPos;

        std::vector initPositions = {
            nxtPos,
            nxtPos + glm::vec3(0, 0,  5),
            nxtPos + glm::vec3(0, 0, 10),
        };

        Path newP = {};
        newP.parentPathId   = (int32_t)selectedPathIndex;
        newP.parentPathCPId = (int32_t)connectionIndex;
        newP.controlPoints.insert(
            newP.controlPoints.begin(),
            initPositions.begin(),
            initPositions.end()
        );
        _paths.push_back(newP);

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
    {
        size_t ctrlPtIndex = path.parentPathId < 0 ? 0 : 1;
        for (auto& controlPoint : path.controlPoints)
        {
            _builder_bezierControlPointRenderObjs.push_back(
                _rom->registerRenderObject({
                    .model = _builder_bezierControlPointHandleModel,
                    .transformMatrix = glm::translate(glm::mat4(1.0f), controlPoint) * glm::scale(glm::mat4(1.0f), glm::vec3(ctrlPtIndex % 3 == 0 ? 1.0f : 0.5f)),
                    .renderLayer = RenderLayer::BUILDER,
                    .attachedEntityGuid = getGUID(),
                })
            );
            ctrlPtIndex++;
        }
    }
}
