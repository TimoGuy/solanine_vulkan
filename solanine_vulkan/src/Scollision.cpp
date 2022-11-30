#include "Scollision.h"

#include "RenderObject.h"
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
            .transformMatrix = _load_renderTransform,
            .renderLayer = RenderLayer::VISIBLE,
            .attachedEntityGuid = getGUID(),
            });

    createCollisionMeshFromModel();
}

Scollision::~Scollision()
{
    _rom->unregisterRenderObject(_renderObj);
    _rom->removeModelCallbacks(this);
}

void Scollision::physicsUpdate(const float_t& physicsDeltaTime)
{}

void Scollision::lateUpdate(const float_t& deltaTime)
{}

void Scollision::dump(DataSerializer& ds)
{}

void Scollision::load(DataSerialized& ds)
{}

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
            loadModelWithName(_modelName);
            createCollisionMeshFromModel();
        }
    );
}

void Scollision::createCollisionMeshFromModel()
{
    const glm::vec3& scale           = physutil::getScale(_renderObj->transformMatrix);
    auto&            li              = _model->loaderInfo;
    int*             indicesCooked   = new int[li.indexCount];  // @NOTE: bullet3 used `int` so I must too...
    bool*            writtenVertices = new bool[li.vertexCount];
    glm::vec3*       verticesCooked  = new glm::vec3[li.vertexCount];

    for (size_t i = 0; i < li.indexCount; i++)
        indicesCooked = (int)li.indexBuffer[i];

    std::vector<Node*> nodesWithAMesh = _model->fetchAllNodesWithAMesh();
    for (auto& node : nodesWithAMesh)
    {
        const glm::mat4& nodeMatrix = node->getMatrix();
        for (auto& primitive : node->mesh->primitives)
        {
            if (!primitive.hasIndices)
            {
                std::cerr << "[CREATE COLLISION MESH FROM MODEL]" << std::endl
                    << "ERROR: no indices primitive not supported." << std::endl;
                return;
            }

            for (size_t i = primitive->firstIndex; i < primitive->firstIndex + primitive->indexCount; i++)
            {
                auto& index = li.indexBuffer[i];
                if (!writtenVertices[index])
                {
                    verticesCooked[index] = scale * node->getMatrix() * li.vertexBuffer[(size_t)index].pos;
                    writtenVertices[index] = true;
                }
            }
        }
    }

    btTriangleIndexVertexArray jasdlfkjalsdkjf;
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
}