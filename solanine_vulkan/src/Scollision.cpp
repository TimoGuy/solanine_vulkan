#include "Scollision.h"

#include "RenderObject.h"
#include "VkglTFModel.h"
#include "PhysicsEngine.h"
#include "DataSerialization.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"


Scollision::Scollision(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _rom(rom)
{
    if (ds)
        load(*ds);

    loadModelWithName(_modelName);

    _renderObj =
        _rom->registerRenderObject({
            .model = _model,
            .transformMatrix = _load_transform,
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });

    createCollisionMeshFromModel();
}

Scollision::~Scollision()
{
    _rom->unregisterRenderObject(_renderObj);
    _rom->removeModelCallbacks(this);
    PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj);

    // @TODO: is there a memory leak on that btTriangleMesh???? Idk
}

void Scollision::physicsUpdate(const float_t& physicsDeltaTime)
{}

void Scollision::lateUpdate(const float_t& deltaTime)
{}

void Scollision::dump(DataSerializer& ds)
{
    Entity::dump(ds);
    ds.dumpMat4(_renderObj->transformMatrix);
    ds.dumpString(_modelName);
    ds.dumpFloat(_groundedAccelMult);
}

void Scollision::load(DataSerialized& ds)
{
    Entity::load(ds);
    _load_transform = ds.loadMat4();
    _modelName = ds.loadString();

    // V2
    if (ds.getSerializedValuesCount() >= 1)
        _groundedAccelMult = ds.loadFloat();
}

float_t Scollision::getGroundedAccelMult()
{
    return _groundedAccelMult;
}

void Scollision::loadModelWithName(const std::string& modelName)
{
    _modelName     = modelName;
    _modelNameTemp = modelName;

    //
    // Load in model and use model triangles as collision mesh
    //
    _rom->removeModelCallbacks(this);
    _model = _rom->getModel(
        _modelName,
        this,
        [&]() {
            std::cout << "Hi" << std::endl;
            loadModelWithName(_modelName);
            createCollisionMeshFromModel();
        }
    );

    if (_renderObj != nullptr)
    {
        _renderObj->model = _model;
    }
}

void Scollision::createCollisionMeshFromModel()
{
    if (_physicsObj != nullptr)
        PhysicsEngine::getInstance().unregisterPhysicsObject(_physicsObj);

    glm::vec3  position        = physutil::getPosition(_renderObj->transformMatrix);
    glm::quat  rotation        = physutil::getRotation(_renderObj->transformMatrix);
    glm::vec3  scale           = physutil::getScale(_renderObj->transformMatrix);
    auto&      li              = _model->loaderInfo;
    int*       indicesCooked   = new int[li.indexCount];  // @NOTE: bullet3 used `int` so I must too...
    bool*      writtenVertices = new bool[li.vertexCount];
    glm::vec3* verticesCooked  = new glm::vec3[li.vertexCount];

    for (size_t i = 0; i < li.vertexCount; i++)
        writtenVertices[i] = false;

    for (size_t i = 0; i < li.indexCount; i++)
        indicesCooked[i] = (int)li.indexBuffer[i];

    std::vector<vkglTF::Node*> nodesWithAMesh = _model->fetchAllNodesWithAMesh();
    for (auto& node : nodesWithAMesh)
    {
        const glm::mat4& nodeMatrix = node->getMatrix();
        for (auto& primitive : node->mesh->primitives)
        {
            if (!primitive->hasIndices)
            {
                std::cerr << "[CREATE COLLISION MESH FROM MODEL]" << std::endl
                    << "ERROR: no indices primitive not supported." << std::endl;
                return;
            }

            for (size_t i = primitive->firstIndex; i < primitive->firstIndex + primitive->indexCount; i++)
            {
                auto& index = li.indexBuffer[i];
                if (writtenVertices[index])
                    continue;

                verticesCooked[index] =
                    glm::scale(glm::mat4(1.0f), scale) *
                    node->getMatrix() *
                    glm::vec4(li.vertexBuffer[(size_t)index].pos, 1.0f);
                writtenVertices[index] = true;
            }
        }
    }

    //
    // Add in vertices and indices into a trianglemesh
    //
    btTriangleMesh* tm = new btTriangleMesh();
    tm->preallocateIndices((int)li.indexCount);
    tm->preallocateVertices((int)li.vertexCount);

    for (size_t i = 0; i < li.vertexCount; i++)
        tm->findOrAddVertex(physutil::toVec3(verticesCooked[i]), false);  // Duplicates should already be removed in the previous step
    
    for (size_t i = 0; i < li.indexCount; i += 3)
    {
        tm->addTriangleIndices(
            indicesCooked[i + 0],
            indicesCooked[i + 1],
            indicesCooked[i + 2]
        );
    }

    //
    // Create rigidbody with the trianglemesh
    //
    btCollisionShape* shape = new btBvhTriangleMeshShape(tm, true);
    _physicsObj =
        PhysicsEngine::getInstance().registerPhysicsObject(
            false,
            position,
            rotation,
            shape,
            &getGUID()
        );
}

void Scollision::reportMoved(void* matrixMoved)
{
    createCollisionMeshFromModel();
}

void Scollision::renderImGui()
{
    ImGui::InputText("_modelNameTemp", &_modelNameTemp);
    if (_modelNameTemp != _modelName)
    {
        if (ImGui::Button("Reload Model with new Name"))
            loadModelWithName(_modelNameTemp);
    }

    ImGui::DragFloat("_groundedAccelMult", &_groundedAccelMult);
}