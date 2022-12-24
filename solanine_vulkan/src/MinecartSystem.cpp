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
        _paths[0].curves.push_back({});
        _paths[0].firstCtrlPt                = glm::vec3(0, 0,  0);
        _paths[0].curves[0].controlPoints[0] = glm::vec3(0, 0,  5);
        _paths[0].curves[0].controlPoints[1] = glm::vec3(0, 0, 10);
        _paths[0].curves[0].controlPoints[2] = glm::vec3(0, 0, 15);
    }

    reconstructBezierCurves();
    _isDirty = false;
    
    _enablePhysicsUpdate = true;
    _enableLateUpdate = true;
}

MinecartSystem::~MinecartSystem()
{
    for (auto& ro : _builder_bezierControlPointRenderObjs)
        _rom->unregisterRenderObject(ro);
    for (auto& ms : _minecartSims)
    {
        PhysicsEngine::getInstance().unregisterPhysicsObject(ms.physicsObj);
        _rom->unregisterRenderObject(ms.renderObj);
    }
    _rom->removeModelCallbacks(this);
}

void MinecartSystem::physicsUpdate(const float_t& physicsDeltaTime)
{
    //
    // Slide all minecart simulations along the path
    // @TODO: @INCOMPLETE!!!!! FINISH THIS!
    //
    /*for (auto& ms : _minecartSims)
    {
        if (!ms.isOnAPath) continue;

        // Move forward in the path
        ms.distanceTraveled += _minecartSimSettings.speed * ms.speedMultiplier * _paths[ms.pathIndex].pathScale * physicsDeltaTime;

        //
        // Calculate position and tangent vector of bezier
        // @COPYPASTA
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
                controlPointsCooked[0] = _paths[(size_t)path.parentPathId].controlPoints[(size_t)path.parentPathCurveId];
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
        glm::vec3 evalBezierTang  = glm::normalize(evalCtrlPtsLayer2[1] - evalCtrlPtsLayer2[0]);
        glm::vec3 evalBezierPoint = physutil::lerp(evalCtrlPtsLayer2[0], evalCtrlPtsLayer2[1], t3);

        // Calculate velocity to keep the cart on the exact position it needs to be
        btVector3 vbc = physutil::toVec3(velocity) / physicsDeltaTime;
        ms.physicsObj->setLinearVelocity(vbc);
    }*/

    //
    // Draw the debug lines for the minecart
    //
    const size_t    numSlices    = 10;
    const glm::vec3 bezierColor  = glm::vec3( 43, 217, 133) / 255.0f;
    const glm::vec3 controlColor = glm::vec3(250, 242, 101) / 255.0f;
    for (auto& path : _paths)
    {
        bool firstCurve = true;

        glm::vec3 prevEvalBezierPoint;
        if (path.parentPathId < 0)
            prevEvalBezierPoint = path.firstCtrlPt;
        else
            prevEvalBezierPoint = _paths[(size_t)path.parentPathId].curves[(size_t)path.parentPathCurveId].controlPoints[2];

        Path::Curve* prevCurve = nullptr;
        for (auto& curve : path.curves)
        {
            // Evaluate the control points
            glm::vec3 controlPointsCooked[] = {
                glm::vec3(),
                glm::vec3(),
                glm::vec3(),
                glm::vec3(),
            };
            if (firstCurve && path.parentPathId < 0)
                controlPointsCooked[0] = path.firstCtrlPt;
            else if (firstCurve)
                controlPointsCooked[0] = _paths[(size_t)path.parentPathId].curves[(size_t)path.parentPathCurveId].controlPoints[2];
            else
                controlPointsCooked[0] = prevCurve->controlPoints[2];
            controlPointsCooked[1] = curve.controlPoints[0];
            controlPointsCooked[2] = curve.controlPoints[1];
            controlPointsCooked[3] = curve.controlPoints[2];

            // Draw the bezier curve
            for (size_t i = 1; i <= numSlices; i++)
            {
                float_t t = (float_t)i / (float_t)numSlices;
                glm::vec3 t3(t);

                glm::vec3 evalCtrlPtsLayer1[] = {  // @NOTE: use geometric bezier curve evaluation in production code!!! Idk if this will be prod code however.
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

                PhysicsEngine::getInstance().debugDrawLineOneFrame(
                    prevEvalBezierPoint,
                    evalBezierPoint,
                    bezierColor
                );

                prevEvalBezierPoint = evalBezierPoint;
            }

            // Draw the control lines
            PhysicsEngine::getInstance().debugDrawLineOneFrame(
                controlPointsCooked[0],
                controlPointsCooked[1],
                controlColor
            );
            PhysicsEngine::getInstance().debugDrawLineOneFrame(
                controlPointsCooked[2],
                controlPointsCooked[3],
                controlColor
            );

            // Move to the next curve
            firstCurve = false;
            prevCurve  = &curve;
        }
    }
}

void MinecartSystem::lateUpdate(const float_t& deltaTime)
{
    for (auto& ms : _minecartSims)
    {
        ms.renderObj->transformMatrix = ms.physicsObj->interpolatedTransform;
    }
}

void MinecartSystem::dump(DataSerializer& ds)
{
    Entity::dump(ds);

    for (auto& path : _paths)
    {
        ds.dumpString("__path__");
        ds.dumpFloat((float_t)path.parentPathId);
        ds.dumpFloat((float_t)path.parentPathCurveId);
        ds.dumpVec3(path.firstCtrlPt);
        ds.dumpFloat((float_t)path.curves.size());
        for (auto& curve : path.curves)
        {
            ds.dumpFloat(curve.curveScale);
            ds.dumpVec3(curve.controlPoints[0]);
            ds.dumpVec3(curve.controlPoints[1]);
            ds.dumpVec3(curve.controlPoints[2]);
        }
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
            newP.parentPathCurveId = (int32_t)ds.loadFloat();
            newP.firstCtrlPt       = ds.loadVec3();

            size_t numCurves = (size_t)ds.loadFloat();
            for (size_t i = 0; i < numCurves; i++)
            {
                Path::Curve newC = {};
                newC.curveScale = ds.loadFloat();
                newC.controlPoints[0] = ds.loadVec3();
                newC.controlPoints[1] = ds.loadVec3();
                newC.controlPoints[2] = ds.loadVec3();

                newP.curves.push_back(newC);
            }

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
    size_t  selectedPathIndex;
    size_t  selectedCurveIndex;
    int32_t selectedControlPointIndex;
    bool found = getControlPointPathAndSubIndices(selectedPathIndex, selectedCurveIndex, selectedControlPointIndex);
    if (!found)
        return;

    glm::mat4 asMat4 = *(glm::mat4*)matrixMoved;
    glm::vec3 cpPos  = physutil::getPosition(asMat4);
    if (selectedControlPointIndex < 0)
        _paths[selectedPathIndex].firstCtrlPt = cpPos;
    else
        _paths[selectedPathIndex].curves[selectedCurveIndex].controlPoints[selectedControlPointIndex] = cpPos;
}

void MinecartSystem::renderImGui()
{
    //
    // Find the selected control point path index and index.
    //
    size_t  selectedPathIndex;
    size_t  selectedCurveIndex;
    int32_t selectedControlPointIndex;
    bool showSelectedIndex = getControlPointPathAndSubIndices(selectedPathIndex, selectedCurveIndex, selectedControlPointIndex);
    if (showSelectedIndex)
    {
        ImGui::Text(("Selected path: " + std::to_string(selectedPathIndex)).c_str());
        ImGui::Text(("Selected curve: " + std::to_string(selectedCurveIndex)).c_str());
        ImGui::Text(("Selected cp index: " + std::to_string(selectedControlPointIndex)).c_str());
    }

    //
    // Manipulations
    //
    if (showSelectedIndex &&
        ImGui::Button("Add curve after selected curve"))
    {
        auto& curve = _paths[selectedPathIndex].curves[selectedCurveIndex];

        glm::vec3 midPos     = curve.controlPoints[2];
        glm::vec3 reflectPos = curve.controlPoints[1];
        glm::vec3 nxtPos     = midPos + midPos - reflectPos;

        _paths[selectedPathIndex].curves.push_back({});

        auto& newCurve = _paths[selectedPathIndex].curves.back();
        newCurve.curveScale = 1.0f;  // @TODO calculate/bake this
        newCurve.controlPoints[0] = nxtPos;
        newCurve.controlPoints[1] = nxtPos + glm::vec3(0, 0,  5);
        newCurve.controlPoints[2] = nxtPos + glm::vec3(0, 0, 10);

        reconstructBezierCurves();  // Automatically just rebake when adding new points
    }

    if (showSelectedIndex &&
        ImGui::Button("Create child path after selected curve"))
    {

        auto& curve = _paths[selectedPathIndex].curves[selectedCurveIndex];

        glm::vec3 midPos     = curve.controlPoints[2];
        glm::vec3 reflectPos = curve.controlPoints[1];
        glm::vec3 nxtPos     = midPos + midPos - reflectPos;

        Path newP = {};
        newP.parentPathId   = (int32_t)selectedPathIndex;
        newP.parentPathCurveId = (int32_t)selectedCurveIndex;
        newP.curves.push_back({});

        auto& newCurve = newP.curves.back();
        newCurve.curveScale = 1.0f;  // @TODO calculate/bake this
        newCurve.controlPoints[0] = nxtPos;
        newCurve.controlPoints[1] = nxtPos + glm::vec3(0, 0,  5);
        newCurve.controlPoints[2] = nxtPos + glm::vec3(0, 0, 10);

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

    // @TODO fix simple syntax errors on this section!
    /*ImGui::Separator();
    if (ImGui::Button("Add 1 minecart simulation"))
    {
        MinecartSimulation newMS = {};
        newMS.physicsObj =
            PhysicsEngine::getInstance().registerPhysicsObject(
                100000.0f,
                _paths[0].controlPoints[0],
                glm::quat(),
                new btBoxShape({ 1, 1, 1 }),
                &getGUID()
            );
        newMS.renderObj =
            _rom->registerRenderObject({
                .model = _minecartModel,
                .transformMatrix = glm::translate(glm::mat4(1.0f), _paths[0].controlPoints[0]),
                .renderLayer = RenderLayer::VISIBLE,
                .attachedEntityGuid = getGUID(),
            });
        _minecartSims.push_back(newMS);
    }*/
}

bool MinecartSystem::getControlPointPathAndSubIndices(size_t& outPathIndex, size_t& outCurveIndex, int32_t& outControlPointIndex)
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
        size_t amt = path.curves.size() * 3 + 1;
        if (amt <= selectedControlPointIndex)
        {
            selectedControlPointIndex -= (int32_t)amt;
            selectedPathIndex++;
        }
        else break;
    }

    if (found)
    {
        outPathIndex = selectedPathIndex;

        if (selectedControlPointIndex == 0)
        {
            outCurveIndex = 0;
            outControlPointIndex = -1;
        }
        else
        {
            outCurveIndex        = (selectedControlPointIndex - 1) / 3;
            outControlPointIndex = (selectedControlPointIndex - 1) % 3;
        }
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
        bool firstCurve = true;
        for (auto& curve : path.curves)
        {
            for (
                size_t i = (firstCurve && path.parentPathId < 0) ? 1 : 0;
                i < 4;
                i++)
            {
                glm::vec3 pos = i == 0 ? path.firstCtrlPt : curve.controlPoints[i - 1];

                _builder_bezierControlPointRenderObjs.push_back(
                    _rom->registerRenderObject({
                        .model = _builder_bezierControlPointHandleModel,
                        .transformMatrix = glm::translate(glm::mat4(1.0f), pos) * glm::scale(glm::mat4(1.0f), glm::vec3(i % 3 == 0 ? 1.0f : 0.5f)),
                        .renderLayer = RenderLayer::BUILDER,
                        .attachedEntityGuid = getGUID(),
                    })
                );
            }

            firstCurve = false;
        }
    }
}
